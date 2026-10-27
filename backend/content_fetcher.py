"""
Full-article content extraction for the prefetch path (plan section 11).

Feed entries often carry only a summary; the body lives on the original web
page. When the client is reading article N the backend prefetches the bodies
of the next few articles so they are available locally without a round-trip.

Extraction is intentionally lightweight: it drops navigation/script/style chrome
and keeps the readable text of the main content region.
"""

import re
from network import read_limited, validate_http_url, MAX_ARTICLE_BYTES
import urllib.request
from html_text import HTMLImageTextRenderer, CHROME
from html_structure import prepare_html
from typing import Optional

from models import ArticleContent


# Tags whose text is UI chrome, not article body.
_CHROME_TAGS = CHROME


class _BodyExtractor(HTMLImageTextRenderer):
    """Collect visible body text, dropping scripts/styles/nav chrome."""

    def __init__(self, base_url=""):
        super().__init__(base_url=base_url, collect_images=True, links=[], styles=[])
        self._skip_depth = 0
        self._in_body = False
        self._title_parts: list[str] = []
        self._in_title = False

    def handle_starttag(self, tag, attrs):
        if tag in _CHROME_TAGS:
            self._skip_depth += 1
        if tag == "title":
            self._in_title = True
        if tag == "body":
            self._in_body = True
        if self._skip_depth == 0:
            super().handle_starttag(tag, attrs)

    def handle_endtag(self, tag):
        if self._skip_depth == 0:
            super().handle_endtag(tag)
        if tag in _CHROME_TAGS and self._skip_depth > 0:
            self._skip_depth -= 1
        if tag == "title":
            self._in_title = False
        if tag == "body":
            self._in_body = False

    def handle_data(self, data):
        if self._in_title:
            data = data.strip()
            if data:
                self._title_parts.append(data)
        elif not self._skip_depth:
            super().handle_data(data)

    def get_title(self) -> str:
        return _collapse_whitespace(" ".join(self._title_parts))


def _collapse_whitespace(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def fetch_article(url: str, timeout: int = 15) -> Optional[ArticleContent]:
    """
    Download a full article page and extract its title and body text.

    Returns an ArticleContent with full_text populated, or None on failure.
    """
    try:
        validate_http_url(url)
        req = urllib.request.Request(url, headers={"User-Agent": "System7RSS/1.0"})
        with urllib.request.urlopen(req, timeout=timeout) as response:
            charset = response.headers.get_content_charset() or "utf-8"
            raw = read_limited(response, MAX_ARTICLE_BYTES).decode(charset, errors="replace")
    except Exception as e:
        print(f"[content_fetcher] Failed to fetch {url}: {e}")
        return None

    parser = _BodyExtractor(url)
    try:
        parser.feed(prepare_html(raw, url, collect_links=True, rich=True))
        parser.close()
    except Exception as e:
        print(f"[content_fetcher] Failed to parse {url}: {e}")
        return None

    title = parser.get_title()
    full_text = parser.get_text()
    if not full_text:
        return None

    return ArticleContent(
        id="",  # filled in by the caller
        title=title,
        full_text=full_text,
        images=parser.images,
        links=parser.links,
        styles=parser.styles,
    )
