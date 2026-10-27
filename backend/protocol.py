"""
Wire protocol for MacWire.

Simple binary protocol optimized for:
- Low bandwidth
- Slow vintage hardware
- Simple parsing
- Minimal RAM usage
- Local network use

Protocol format (plan section 10):
  [1 byte: command] [variable: payload]

Commands:
  0x01 - SYNC (client -> server): Request current index
  0x02 - INDEX (server -> client): Article index response
  0x03 - ARTICLE (server -> client): Full article content
  0x04 - IMAGE (server -> client): Processed image data
  0x05 - STATUS (server -> client): Server status
  0x08 - PREFETCH (client -> server): Request prefetch of articles

Each payload is:
  [2 bytes: length] [data...]
"""

import struct
import unicodedata
import html
import re
from datetime import datetime, timezone
from zoneinfo import ZoneInfo
from typing import List, Optional, Dict, Any
from models import ArticleIndexEntry, ArticleContent, ImageData


# Protocol constants
CMD_SYNC = 0x01
CMD_INDEX = 0x02
CMD_ARTICLE = 0x03
CMD_IMAGE = 0x04
CMD_STATUS = 0x05
CMD_PREFETCH = 0x08
CMD_ERROR = 0xFF

VERSION_MAJOR = 1
VERSION_MINOR = 0

# Transfer batch size for the System 7 client; not a browsing limit.
MAX_INDEX_ENTRIES = 50
MAX_IMAGES_PER_ARTICLE = 5
CMD_FEEDS = 0x06


def _client_text(value: Optional[str], limit: int, preserve_spacing=False) -> bytes:
    # QuickDraw's Roman fonts understand MacRoman, including smart quotes,
    # dashes and accents. Normalize web spacing/formatting before encoding.
    value = html.unescape(value or "").translate(str.maketrans({
        "\u00a0": " ", "\u202f": " ", "\u2009": " ", "\u200b": "",
        "\u200c": "", "\u200d": "", "\ufeff": "", "\u00ad": "",
        "\u2212": "-", "\u2011": "-", "\u2010": "-", "\t": "    ",
        "→": "->", "←": "<-", "↔": "<->", "×": "x", "✓": "[yes]",
        "★": "*",  # Daring Fireball original-post/permalink marker.
        "↦": "->",  # Six Colors external-link marker.
        "\u2060": "", "\ufe0e": "", "\ufe0f": "",
    }))
    value = value.replace("\r\n", "\n")
    if not preserve_spacing:
        value = re.sub(r"[ \t\f\v]+", " ", value).replace("\r\n", "\n")
        value = re.sub(r" *\n *", "\n", value)
        # Repair spaces introduced by older HTML extraction in persisted articles.
        value = re.sub(r"([“‘]) +", r"\1", value)
        value = re.sub(r" +([”’.,;:!?])", r"\1", value)
        value = re.sub(r"\n{3,}", "\n\n", value)
    output = bytearray()
    for ch in unicodedata.normalize("NFC", value):
        if ord(ch) < 32 and ch not in "\r\n\t":
            continue
        try:
            output.extend(ch.encode("mac_roman"))
        except UnicodeEncodeError:
            fallback = unicodedata.normalize("NFKD", ch).encode("ascii", "ignore")
            output.extend(fallback or b"[symbol]")
    return bytes(output[:limit])


def encode_feeds(names) -> bytes:
    names = sorted({_client_text(name, 32) for name in names if name})[:64]
    payload = struct.pack(">H", len(names))
    for name in names:
        payload += struct.pack(">H", len(name)) + name
    return _build_packet(CMD_FEEDS, payload)



def local_timezone(name=None):
    """Use an explicit IANA zone, or the backend computer's local DST rules."""
    if name:
        return ZoneInfo(name)
    with open("/etc/localtime", "rb") as local:
        return ZoneInfo.from_file(local)


def encode_dates(entries, display_timezone=None, compact=False):
    """Separate optional packet keeps UTC wire timestamps backward compatible."""
    zone = display_timezone or local_timezone()
    payload = bytearray(struct.pack(">H", min(len(entries), MAX_INDEX_ENTRIES)))
    months = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")
    for entry in entries[-MAX_INDEX_ENTRIES:]:
        payload += entry.id.encode("ascii") + b"\0"
        if entry.published:
            date = entry.published.astimezone(zone)
            short = f"{months[date.month-1]} {date.day}, {date.hour % 12 or 12}:{date.minute:02d} {'AM' if date.hour < 12 else 'PM'}"
            if not compact:
                short = date.strftime("%H:%M")
            full = f"{months[date.month-1]} {date.day}, {date.year} {date.hour % 12 or 12}:{date.minute:02d} {'AM' if date.hour < 12 else 'PM'}"
        else:
            short, full = "--:--", "Date unavailable"
        for text in (short, full):
            data = _client_text(text, 31)
            payload += struct.pack(">H", len(data)) + data
    return _build_packet(0x09, payload)

def encode_sync(last_sync: Optional[float] = None) -> bytes:
    """Encode a SYNC request from the client."""
    payload = struct.pack(">d", last_sync or 0.0)
    return _build_packet(CMD_SYNC, payload)


def encode_index(entries: List[ArticleIndexEntry]) -> bytes:
    """Encode an INDEX response with article metadata."""
    # Keep the newest entries while preserving oldest-to-newest order.
    entries = entries[-MAX_INDEX_ENTRIES:]
    payload = bytearray()
    payload += struct.pack(">H", len(entries))
    for entry in entries:
        payload += _encode_index_entry(entry)
    return _build_packet(CMD_INDEX, bytes(payload))


def encode_index_page(entries, view, total, offset):
    """A stable view transferred in small packets, with an explicit total."""
    if len(entries) > MAX_INDEX_ENTRIES or offset + len(entries) > total:
        raise ValueError("Invalid page bounds")
    payload = view.encode("ascii") + struct.pack(">II", total, offset)
    payload += encode_index(entries)[3:]
    return _build_packet(0x0A, payload)


def _encode_index_entry(entry: ArticleIndexEntry) -> bytes:
    """Encode a single index entry."""
    payload = bytearray()
    payload += entry.id.encode("ascii")
    payload += b"\x00"  # null terminator for ID

    if entry.published:
        ts = entry.published.timestamp()
    else:
        ts = 0.0
    payload += struct.pack(">d", ts)

    feed = _client_text(entry.feed, 32)
    payload += struct.pack(">H", len(feed))
    payload += feed

    title = _client_text(entry.title, 100)
    payload += struct.pack(">H", len(title))
    payload += title

    summary = _client_text(entry.summary, 200)
    payload += struct.pack(">H", len(summary))
    payload += summary

    payload += struct.pack(">B", 1 if entry.has_full_content else 0)
    payload += struct.pack(">H", entry.image_count)

    return bytes(payload)


def encode_article(content: ArticleContent) -> bytes:
    """Encode a full article content response."""
    payload = bytearray()
    payload += content.id.encode("ascii")
    payload += b"\x00"

    title = _client_text(content.title, 511)
    payload += struct.pack(">H", len(title))
    payload += title

    author = _client_text(content.author, 255)
    payload += struct.pack(">H", len(author))
    payload += author

    if content.published:
        ts = content.published.timestamp()
    else:
        ts = 0.0
    payload += struct.pack(">d", ts)

    feed = _client_text(content.feed, 32)
    payload += struct.pack(">H", len(feed))
    payload += feed

    preserve_spacing = any(style.get("flags", 0) & 24 for style in content.styles)
    def body_text(value): return _client_text(value, 65535, preserve_spacing)
    full_text = body_text(content.full_text)
    visible_text_length = min(len(full_text), 9999)
    if len(full_text) > 9999:
        notice = b'\r\r[Article continues beyond this reader\'s text limit.]'
        visible_text_length = 9999-len(notice)
        full_text = full_text[:visible_text_length] + notice
    payload += struct.pack(">H", len(full_text))
    payload += full_text

    images = content.images[:MAX_IMAGES_PER_ARTICLE]
    payload += struct.pack(">H", len(images))
    for img in images:
        img_url = _client_text(img.get("url", ""), 2048)
        payload += struct.pack(">H", len(img_url))
        payload += img_url
        payload += struct.pack(">I", img.get("width", 0))
        payload += struct.pack(">I", img.get("height", 0))
        img_format = _client_text(img.get("format", "unknown"), 16)
        payload += struct.pack(">H", len(img_format))
        payload += img_format

    if any("offset" in image for image in images):
        payload += b"IPOS"
        for image in images:
            position = image.get("offset")
            wire_position = 0xFFFF
            if isinstance(position, int) and 0 <= position <= len(content.full_text):
                prefix = body_text(content.full_text[:position])
                if len(prefix) <= visible_text_length:
                    wire_position = len(prefix)
            payload += struct.pack(">H", wire_position)
    from urllib.parse import urlsplit
    links = []
    url_bytes = 0
    for link in content.links:
        start, length, url = link.get('start'), link.get('length'), link.get('url', '')
        if not isinstance(start, int) or not isinstance(length, int) or start < 0 or length <= 0 or start+length > len(content.full_text):
            continue
        try:
            parsed = urlsplit(url)
            if parsed.scheme not in ('http', 'https') or not parsed.hostname: continue
        except (ValueError, TypeError):
            continue
        begin = len(body_text(content.full_text[:start]))
        end = len(body_text(content.full_text[:start+length]))
        encoded_url = _client_text(url, 512)
        if end > visible_text_length or end <= begin or not encoded_url: continue
        if len(links) >= 48 or url_bytes + len(encoded_url)+1 > 8192: break
        links.append(struct.pack('>HHH', begin, end-begin, len(encoded_url)) + encoded_url)
        url_bytes += len(encoded_url)+1
    if links:
        payload += b'LINK' + struct.pack('>H', len(links)) + b''.join(links)
    styles = []
    for style in content.styles:
        start, length, flags = style.get('start'), style.get('length'), style.get('flags')
        if not all(isinstance(v, int) for v in (start, length, flags)) or start < 0 or length <= 0 or start+length > len(content.full_text) or not flags or flags & ~511: continue
        begin, end = len(body_text(content.full_text[:start])), len(body_text(content.full_text[:start+length]))
        end = min(end, visible_text_length)
        if end > begin: styles.append(struct.pack('>HHH', begin, end-begin, flags))
        if len(styles) == 256: break
    if styles: payload += b'STYL' + struct.pack('>H', len(styles)) + b''.join(styles)
    return _build_packet(CMD_ARTICLE, bytes(payload))


def encode_image(image_data: ImageData, article_id: str, image_index: int) -> bytes:
    """Encode processed image data for transport."""
    payload = bytearray()
    payload += article_id.encode("ascii")
    payload += b"\x00"
    payload += struct.pack(">H", image_index)
    payload += struct.pack(">HHH", image_data.width, image_data.height, image_data.row_bytes)
    payload += image_data.bitmap
    return _build_packet(CMD_IMAGE, bytes(payload))


def encode_status(article_count: int, unread_count: int = 0) -> bytes:
    """Encode server status response.

    Layout matches the System 7 client's ParseStatusResponse:
    version as two single bytes (major, minor), then article/unread
    counts as 2-byte big-endian, then 4 bytes of flags.
    """
    payload = struct.pack(
        ">BBHHI",
        VERSION_MAJOR,
        VERSION_MINOR,
        article_count,
        unread_count,
        0,  # flags: 0 = normal
    )
    return _build_packet(CMD_STATUS, payload)


def encode_prefetch_request(article_id: str, count: int = 3) -> bytes:
    """Encode a prefetch request from client."""
    payload = article_id.encode("ascii") + struct.pack(">H", count)
    return _build_packet(CMD_PREFETCH, payload)


def encode_error(message: str) -> bytes:
    """Encode an error response."""
    msg = message.encode("ascii", errors="replace")
    payload = struct.pack(">H", len(msg)) + msg
    return _build_packet(CMD_ERROR, payload)


def _build_packet(command: int, payload: bytes) -> bytes:
    """Build a complete protocol packet."""
    return struct.pack(">BH", command, len(payload)) + payload


def parse_packet(data: bytes) -> tuple:
    """
    Parse a protocol packet.

    Returns:
        (command, payload_bytes)
    """
    if len(data) < 3:
        raise ValueError("Packet too short")
    command, length = struct.unpack(">BH", data[:3])
    payload = data[3 : 3 + length]
    return command, payload


def parse_index_response(payload: bytes) -> List[ArticleIndexEntry]:
    """Parse an INDEX response payload."""
    entries = []
    offset = 0
    entry_count, = struct.unpack(">H", payload[offset : offset + 2])
    offset += 2

    for _ in range(entry_count):
        # Read null-terminated ID
        null_pos = payload.index(b"\x00", offset)
        article_id = payload[offset:null_pos].decode("ascii")
        offset = null_pos + 1

        # Timestamp
        ts, = struct.unpack(">d", payload[offset : offset + 8])
        offset += 8
        published = datetime.fromtimestamp(ts, tz=timezone.utc) if ts > 0 else None

        # Feed (length-prefixed)
        feed_len, = struct.unpack(">H", payload[offset : offset + 2])
        offset += 2
        feed = payload[offset : offset + feed_len].decode("mac_roman")
        offset += feed_len

        # Title (length-prefixed)
        title_len, = struct.unpack(">H", payload[offset : offset + 2])
        offset += 2
        title = payload[offset : offset + title_len].decode("mac_roman")
        offset += title_len

        # Summary (length-prefixed)
        summary_len, = struct.unpack(">H", payload[offset : offset + 2])
        offset += 2
        summary = payload[offset : offset + summary_len].decode("mac_roman")
        offset += summary_len

        # has_full_content (1 byte)
        has_full, = struct.unpack(">B", payload[offset : offset + 1])
        offset += 1

        # image_count (2 bytes)
        image_count, = struct.unpack(">H", payload[offset : offset + 2])
        offset += 2

        entries.append(ArticleIndexEntry(
            id=article_id,
            published=published,
            feed=feed,
            title=title,
            summary=summary,
            has_full_content=bool(has_full),
            image_count=image_count,
        ))

    return entries


def parse_article_response(payload: bytes) -> ArticleContent:
    """Parse an ARTICLE response payload."""
    offset = 0

    # Null-terminated ID
    null_pos = payload.index(b"\x00", offset)
    article_id = payload[offset:null_pos].decode("ascii")
    offset = null_pos + 1

    # Title
    title_len, = struct.unpack(">H", payload[offset : offset + 2])
    offset += 2
    title = payload[offset : offset + title_len].decode("mac_roman")
    offset += title_len

    # Author
    author_len, = struct.unpack(">H", payload[offset : offset + 2])
    offset += 2
    author = payload[offset : offset + author_len].decode("mac_roman") if author_len > 0 else None
    offset += author_len

    # Timestamp
    ts, = struct.unpack(">d", payload[offset : offset + 8])
    offset += 8
    from datetime import datetime, timezone
    published = datetime.fromtimestamp(ts, tz=timezone.utc) if ts > 0 else None

    # Feed
    feed_len, = struct.unpack(">H", payload[offset : offset + 2])
    offset += 2
    feed = payload[offset : offset + feed_len].decode("mac_roman") if feed_len > 0 else None
    offset += feed_len

    # Full text
    text_len, = struct.unpack(">H", payload[offset : offset + 2])
    offset += 2
    full_text = payload[offset : offset + text_len].decode("mac_roman")
    offset += text_len

    # Images
    image_count, = struct.unpack(">H", payload[offset : offset + 2])
    offset += 2
    images = []
    for _ in range(image_count):
        url_len, = struct.unpack(">H", payload[offset : offset + 2])
        offset += 2
        url = payload[offset : offset + url_len].decode("mac_roman")
        offset += url_len

        width, = struct.unpack(">I", payload[offset : offset + 4])
        offset += 4
        height, = struct.unpack(">I", payload[offset : offset + 4])
        offset += 4

        fmt_len, = struct.unpack(">H", payload[offset : offset + 2])
        offset += 2
        fmt = payload[offset : offset + fmt_len].decode("ascii")
        offset += fmt_len

        images.append({
            "url": url,
            "width": width,
            "height": height,
            "format": fmt,
        })

    if payload[offset:offset + 4] == b'IPOS':
        if len(payload) < offset + 4 + 2 * image_count: raise ValueError('Invalid image positions')
        for i, image in enumerate(images):
            position, = struct.unpack(">H", payload[offset + 4 + 2*i:offset + 6 + 2*i])
            if position != 0xFFFF:
                if position > len(full_text):
                    raise ValueError("Image position exceeds article text")
                image["offset"] = position
        offset += 4 + 2 * image_count
    links = []
    if payload[offset:offset+4] == b'LINK':
        if payload[offset:offset+4] != b'LINK' or len(payload) < offset+6: raise ValueError('Invalid links')
        count, = struct.unpack('>H', payload[offset+4:offset+6]); offset += 6
        if count > 48: raise ValueError('Too many links')
        url_bytes = 0
        for _ in range(count):
            if len(payload) < offset+6: raise ValueError('Truncated link')
            start, length, size = struct.unpack('>HHH', payload[offset:offset+6]); offset += 6
            url_bytes += size+1
            if not length or start+length > len(full_text) or not size or size > 512 or url_bytes > 8192 or len(payload) < offset+size:
                raise ValueError('Invalid link bounds')
            url = payload[offset:offset+size].decode('mac_roman'); offset += size
            links.append({'start': start, 'length': length, 'url': url})
    styles = []
    if payload[offset:offset+4] == b'STYL':
        if len(payload) < offset+6: raise ValueError('Truncated styles')
        count, = struct.unpack('>H', payload[offset+4:offset+6]); offset += 6
        if count > 256 or len(payload) < offset+6*count: raise ValueError('Invalid style count')
        for _ in range(count):
            start, length, flags = struct.unpack('>HHH', payload[offset:offset+6]); offset += 6
            if not length or start+length > len(full_text) or not flags or flags & ~511: raise ValueError('Invalid style bounds')
            styles.append({'start': start, 'length': length, 'flags': flags})
    if offset != len(payload): raise ValueError('Trailing article data')

    from models import ArticleContent
    return ArticleContent(
        id=article_id,
        title=title,
        author=author,
        published=published,
        feed=feed,
        full_text=full_text,
        images=images,
        links=links,
        styles=styles,
    )


def encode_refresh(token, running, failures=0):
    """Refresh job: eight ASCII hex bytes, running flag, failed-feed count."""
    return _build_packet(0x0B, token.encode("ascii") + struct.pack(">BH", bool(running), failures))
