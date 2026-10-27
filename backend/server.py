"""
HTTP server for MacWire.

Serves the custom binary protocol over HTTP GET/POST.
The SE client uses MacTCP to make HTTP requests.

Endpoints:
  GET /page?hours=24&feed=<hex>     -> Full time-window view, in transfer batches
  GET /sync                        -> Legacy client index (newest 50)
  GET /dates?hours=24&feed=<hex>    -> Matching local date strings
   GET /article/<id>                -> Full article content
   GET /image/<article_id>/<index>  -> Processed image
   POST /prefetch/<id>?count=N      -> Prefetch articles
   GET /status                      -> Server status
  GET /refresh[?job=<hex>]          -> Start/join or poll a background feed check

The server uses simple HTTP with query parameters for the SE client,
but wraps the actual data in the binary protocol format.
"""

import http.server
import socketserver
import threading
import urllib.parse
import math
import time
import secrets
import socket
import subprocess
import sys
from collections import OrderedDict
from datetime import datetime, timezone, timedelta
from typing import Optional

from refresh import FeedRefreshManager
from models import Article, ArticleIndexEntry, ArticleContent, ImageData
from database import RollingDatabase
from fetcher import FeedFetcher
from normalizer import ArticleNormalizer
from config import FeedConfig
from image_processor import ImageProcessor
from content_fetcher import fetch_article
from protocol import (
    encode_refresh,
    encode_sync,
    encode_index,
    encode_index_page,
    MAX_INDEX_ENTRIES,
    encode_feeds,
    encode_dates,
    local_timezone,
    _client_text,
    encode_article,
    encode_image,
    encode_status,
    encode_prefetch_request,
    encode_error,
    parse_packet,
    parse_index_response,
    parse_article_response,
)


class RSSHandler(http.server.BaseHTTPRequestHandler):
    """HTTP request handler for the RSS protocol."""

    server_version = "MacWire/1.0"

    # Class-level references set by serve()
    database: RollingDatabase = None
    fetcher: FeedFetcher = None
    normalizer: ArticleNormalizer = None
    config: FeedConfig = None
    processor: ImageProcessor = None
    fetch_thread: threading.Thread = None
    display_timezone = None
    default_window_hours = 24

    def log_message(self, format, *args):
        """Suppress default logging for bandwidth reasons."""
        pass

    def do_GET(self):
        """Handle GET requests from the SE client."""
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)

        try:
            if parsed.path == "/refresh":
                manager = getattr(self.server, "refresh_manager", None)
                if manager is None:
                    self._send_error(503, "Feed refresh worker unavailable")
                    return
                token = params.get("job", [""])[0]
                if not token:
                    token = manager.request()
                try:
                    running, failures = manager.status(token)
                except KeyError:
                    self._send_error(404, "Feed refresh job expired")
                    return
                self._send_response(200, encode_refresh(token, running, failures))
            elif parsed.path == "/sync":
                self._handle_sync(params)
            elif parsed.path == "/page":
                self._handle_page(params)
            elif parsed.path == "/dates":
                index = self._view_page(params) if "view" in params else self._filter_index(self.database.get_index(), params)
                if index is not None:
                    self._send_response(200, encode_dates(index, self.display_timezone, compact="view" in params))
            elif parsed.path == "/feeds":
                names = [feed.name for feed in self.config.get_enabled_feeds()]
                names.extend(entry.feed for entry in self.database.get_index())
                self._send_response(200, encode_feeds(names))
            elif parsed.path.startswith("/article/"):
                article_id = parsed.path[len("/article/"):]
                self._handle_article(article_id)
            elif parsed.path.startswith("/image/"):
                parts = parsed.path[len("/image/"):].split("/")
                if len(parts) >= 2:
                    article_id = parts[0]
                    image_index = int(parts[1])
                    self._handle_image(article_id, image_index, params)
                else:
                    self._send_error(400, "Image path needs article ID and index")
            elif parsed.path == "/status":
                self._handle_status(params)
            else:
                self._send_error(404, "Unknown endpoint")
        except ValueError:
            self._send_error(400, "Invalid request parameter")
        except Exception as e:
            print(f"[MacWire] Error handling {self.path}: {e}")
            self._send_error(500, str(e))

    def do_POST(self):
        """Handle POST requests from the SE client."""
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)

        try:
            content_length = int(self.headers.get("Content-Length", 0))
            if content_length < 0 or content_length > 4096 or self.headers.get("Transfer-Encoding"):
                raise ValueError()
        except ValueError:
            self.close_connection = True
            self._send_error(400, "Invalid request body length (maximum 4096 bytes)")
            return
        if content_length and len(self.rfile.read(content_length)) != content_length:
            self.close_connection = True
            self._send_error(400, "Incomplete request body")
            return

        try:
            if parsed.path.startswith("/prefetch/"):
                article_id = parsed.path[len("/prefetch/"):]
                count = int(params.get("count", ["3"])[0])
                if not 1 <= count <= 10:
                    self._send_error(400, "Prefetch count must be between 1 and 10")
                    return
                self._handle_prefetch(article_id, count)
            else:
                self._send_error(404, "Unknown endpoint")
        except ValueError:
            self._send_error(400, "Invalid prefetch count")
        except Exception as e:
            print(f"[MacWire] Error handling POST {self.path}: {e}")
            self._send_error(500, str(e))

    def _handle_sync(self, params):
        """Handle /sync request - return current index."""
        # Upstream I/O belongs to the background worker; SE refreshes use cache.
        index = self._filter_index(self.database.get_index(), params)
        if index is not None:
            self._send_response(200, encode_index(index))

    def _view_page(self, params, create=False):
        with self.server.article_views_lock:
            return self._locked_view_page(params, create)

    def _locked_view_page(self, params, create):
        # Keep a snapshot while the SE fetches index/date batches. Background
        # polling cannot shift page boundaries or change the time cutoff.
        now = time.monotonic()
        views = self.server.article_views
        for token, (created, _) in list(views.items()):
            if now - created > 600:
                del views[token]
        try:
            offset = int(params.get("offset", ["0"])[0])
            if offset < 0:
                raise ValueError()
        except ValueError:
            self._send_error(400, "Invalid page offset")
            return None
        token = params.get("view", [""])[0]
        if not token and create and offset == 0:
            index = self._filter_index(self.database.get_index(), params)
            if index is None:
                return None
            index = sorted(index, key=lambda entry: (entry.published, entry.id))
            token = secrets.token_hex(4)
            while token in views:
                token = secrets.token_hex(4)
            while len(views) >= 8:
                views.popitem(last=False)
            views[token] = (now, index)
        if token not in views:
            self._send_error(409, "Article view expired; refresh again")
            return None
        _, index = views[token]
        views[token] = (now, index)
        views.move_to_end(token)
        if offset > len(index):
            self._send_error(400, "Page offset exceeds view")
            return None
        self._view_token, self._view_total, self._view_offset = token, len(index), offset
        return index[offset:offset + MAX_INDEX_ENTRIES]

    def _handle_page(self, params):
        index = self._view_page(params, create=True)
        if index is not None:
            self._send_response(200, encode_index_page(
                index, self._view_token, self._view_total, self._view_offset))

    def _filter_index(self, index, params):
        try:
            hours = float(params.get("hours", [str(self.default_window_hours)])[0])
            if not math.isfinite(hours) or not 1 <= hours <= self.database.window_hours:
                raise ValueError()
        except (ValueError, TypeError):
            self._send_error(400, "Invalid article age window")
            return None
        cutoff = datetime.now(timezone.utc) - timedelta(hours=hours)
        index = [entry for entry in index if entry.published and entry.published >= cutoff]
        feed = params.get("feed", [""])[0]
        if feed:
            try:
                wanted = bytes.fromhex(feed)
            except ValueError:
                self._send_error(400, "Invalid feed filter")
                return None
            index = [entry for entry in index if _client_text(entry.feed, 32) == wanted]
        return index

    def _handle_article(self, article_id: str):
        """Handle /article/<id> request."""
        content = self.database.get_article(article_id)
        if content:
            response = encode_article(content)
            self._send_response(200, response)
        else:
            self._send_error(404, f"Article {article_id} not found")

    def _handle_image(self, article_id: str, image_index: int, params=None):
        """Handle /image/<article_id>/<index> request."""
        image_info = self.database.get_image(article_id, image_index)
        if not image_info:
            self._send_error(404, f"Image {article_id}/{image_index} not found")
            return

        url = image_info.get("url")
        if not url:
            self._send_error(404, "No image URL")
            return

        # Process and cache the image
        try:
            width = int((params or {}).get("width", ["320"])[0])
            if not 1 <= width <= 320: raise ValueError()
        except ValueError:
            self._send_error(400, "Invalid image width"); return
        processed = self.processor.process_from_url(url, max_width=width)
        if processed:
            response = encode_image(processed, article_id, image_index)
            self._send_response(200, response)
        else:
            self._send_error(404, "Image processing failed")

    def _handle_status(self, params=None):
        """Handle /status request."""
        index = self._filter_index(self.database.get_index(), params or {})
        if index is None:
            return
        response = encode_status(len(index))
        self._send_response(200, response)

    def _handle_prefetch(self, article_id: str, count: int):
        """Handle /prefetch/<id> request - fetch next N articles' bodies."""
        index = self.database.get_index()
        current_idx = None
        for i, entry in enumerate(index):
            if entry.id == article_id:
                current_idx = i
                break

        if current_idx is not None:
            for entry in index[current_idx + 1 : current_idx + 1 + count]:
                if entry.has_full_content:
                    continue
                url = self.database.get_original_url(entry.id)
                if not url:
                    continue
                content = fetch_article(url, timeout=self.fetcher.timeout)
                if content:
                    content.id = entry.id
                    self.database.set_article_content(
                        entry.id, content.full_text, content.title, images=content.images, links=content.links, styles=content.styles
                    )

        self._send_response(200, b"\x00")  # Simple ack

    def _send_response(self, status: int, data: bytes):
        """Send an HTTP response with protocol data."""
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _send_error(self, status: int, message: str):
        """Send an error response."""
        error_data = encode_error(message)
        self._send_response(status, error_data)


class RSSServer(socketserver.ThreadingTCPServer):
    """Threaded HTTP server so a slow System 7 client can't block others."""

    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, *args, **kwargs):
        self._workers = threading.BoundedSemaphore(16)
        self.article_views = OrderedDict()
        self.article_views_lock = threading.Lock()
        super().__init__(*args, **kwargs)

    def get_request(self):
        connection, address = super().get_request()
        connection.settimeout(30)
        return connection, address

    def process_request(self, request, client_address):
        if not self._workers.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self._workers.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self._workers.release()


def connection_addresses(host):
    """Report candidate IPv4 addresses without sending network traffic."""
    if host not in ("", "0.0.0.0"):
        return [host]
    addresses = set()
    if sys.platform == "darwin":
        try:
            output = subprocess.run(["/sbin/ifconfig"], capture_output=True,
                                    text=True, timeout=2, check=True).stdout
            for line in output.splitlines():
                words = line.split()
                if len(words) > 1 and words[0] == "inet" and not words[1].startswith("127."):
                    addresses.add(words[1])
        except (OSError, subprocess.SubprocessError):
            pass
    try:
        addresses.update(address[4][0] for address in socket.getaddrinfo(
            socket.gethostname(), None, socket.AF_INET)
            if not address[4][0].startswith("127."))
    except OSError:
        pass
    return sorted(addresses)


def serve(port: int = 8080, data_dir: str = "rss_data", fetch_interval: int = 3600, fetch_timeout: int = 15, window_hours: float = 24, config_path: str = "feeds.json", display_timezone: str = None, host: str = "0.0.0.0"):
    """
    Start the RSS server.

    Args:
        port: Port to listen on (default 8080)
        data_dir: Directory for persistent storage
        fetch_interval: Seconds between feed fetches (default 3600)
        fetch_timeout: Seconds to wait for a feed response (default 15)
        window_hours: Default display window in hours (default 24); retain at least 168
    """
    # Initialize components
    config = FeedConfig(config_path)
    config.load()

    fetcher = FeedFetcher(fetch_interval=fetch_interval, timeout=fetch_timeout)
    retention_hours = max(168, window_hours)
    normalizer = ArticleNormalizer(window_hours=retention_hours)
    database = RollingDatabase(data_dir, window_hours=retention_hours)
    processor = ImageProcessor()

    # Set up handler with references
    RSSHandler.database = database
    RSSHandler.fetcher = fetcher
    RSSHandler.normalizer = normalizer
    RSSHandler.config = config
    RSSHandler.processor = processor
    RSSHandler.display_timezone = local_timezone(display_timezone)
    RSSHandler.default_window_hours = window_hours
    print(f"[MacWire] Display time zone: {display_timezone or RSSHandler.display_timezone}")

    # Bind before fetching so cached news is available even if a feed is slow.
    def update_feeds(force):
        feeds = config.get_enabled_feeds()
        articles = fetcher.fetch_feeds(feeds, force=True) if force else fetcher.fetch_feeds(feeds)
        if articles:
            normalized = normalizer.normalize(articles)
            database.add_articles(normalized)
            print(f"[MacWire] Feed update: {len(normalized)} articles")
        return len(fetcher.last_errors)

    refresh_manager = FeedRefreshManager(update_feeds, interval=fetch_interval)

    with RSSServer((host, port), RSSHandler) as httpd:
        print(f"[MacWire] Listening on port {port}")
        print("[MacWire] Trusted local network only; this service has no authentication.")
        for address in connection_addresses(host):
            print(f"[MacWire] Client connection: IPv4 {address}, port {port}")
        print("[MacWire] Choose the address on the classic Mac's network; 127.0.0.1 is local only.")
        print("[MacWire] Stop with Control-C. Restart after editing feeds.")
        print(f"[MacWire] Retaining {retention_hours:g} hours; default view {window_hours:g} hours")
        httpd.refresh_manager = refresh_manager
        refresh_manager.start()
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[MacWire] Shutting down...")
        finally:
            refresh_manager.stop()
