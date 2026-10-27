"""
RSS/Atom feed fetcher.

Handles fetching, parsing, and basic validation of RSS and Atom feeds.
Uses feedparser library for robust parsing.
"""

import time
import urllib.request
import urllib.error
from network import read_limited, validate_http_url, MAX_FEED_BYTES
import hashlib
from urllib.parse import urljoin
from datetime import datetime, timezone
from typing import List, Optional
from html.parser import HTMLParser
from html_text import html_to_text, html_to_text_with_images
import feedparser

from models import Article, Feed


class FeedFetcher:
    def __init__(self, fetch_interval: int = 3600, timeout: int = 15):
        """
        Initialize the feed fetcher.

        Args:
            fetch_interval: Seconds between feed refreshes (default 1 hour)
            timeout: Seconds to wait for a feed response before giving up (default 15)
        """
        self.fetch_interval = fetch_interval
        self.timeout = timeout
        self._last_fetch: dict = {}  # url -> last attempt timestamp
        self.last_errors = []
        self._validators: dict = {}  # url -> HTTP cache validators

    def fetch_feeds(self, feeds: List[Feed], force: bool = False) -> List[Article]:
        """Fetch all enabled feeds and return normalized articles."""
        articles = []
        self.last_errors = []
        for feed in feeds:
            if not feed.enabled:
                continue
            try:
                feed_articles = self._fetch_single_feed(feed, force=force)
                articles.extend(feed_articles)
            except Exception as e:
                self.last_errors.append(feed.name)
                print(f"[fetcher] Error fetching {feed.name} ({feed.url}): {e}")
        return articles

    def _fetch_single_feed(self, feed: Feed, force: bool = False) -> List[Article]:
        """Fetch and parse a single feed, returning raw articles."""
        # Respect fetch interval
        if not force and feed.url in self._last_fetch:
            elapsed = time.time() - self._last_fetch[feed.url]
            if elapsed < self.fetch_interval:
                return []

        self._last_fetch[feed.url] = time.time()
        validators = self._validators.get(feed.url, {})
        validate_http_url(feed.url)
        headers = {"User-Agent": "MacWire-Helper/1.0"}
        if validators.get("etag"):
            headers["If-None-Match"] = validators["etag"]
        if validators.get("modified"):
            headers["If-Modified-Since"] = validators["modified"]
        request = urllib.request.Request(feed.url, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as upstream:
                data = read_limited(upstream, MAX_FEED_BYTES)
                response_headers = {name.lower(): value for name, value in upstream.headers.items()}
                response_headers.setdefault("content-location", upstream.geturl())
        except urllib.error.HTTPError as error:
            error.close()
            if error.code == 304:
                return []
            raise
        response = feedparser.parse(data, response_headers=response_headers)
        # Validators belong to HTTP responses, not the feed's XML body.
        for key, header in (("etag", "ETag"), ("modified", "Last-Modified")):
            value = next((value for name, value in response_headers.items()
                          if name.lower() == header.lower()), None)
            if value:
                setattr(response, key, value)
        status = getattr(response, "status", 200)
        if status == 304:
            return []  # Keep the existing database articles.
        if status not in (200, 301, 302, 307, 308):
            raise RuntimeError(f"HTTP {status}")
        # Do not commit validators for a wholly unusable feed: retry its body.
        if getattr(response, "bozo", 0) and not response.entries:
            raise RuntimeError(str(getattr(response, "bozo_exception", "Invalid feed")))
        self._validators[feed.url] = {
            key: getattr(response, key) for key in ("etag", "modified")
            if getattr(response, key, None)
        }

        articles = []
        for entry in response.entries:
            article = self._parse_entry(entry, feed)
            if article:
                articles.append(article)

        return articles

    def _parse_entry(self, entry, feed: Feed) -> Optional[Article]:
        """Convert a feedparser entry to an Article."""
        # Get published time
        published = None
        if hasattr(entry, "published_parsed") and entry.published_parsed:
            published = datetime(*entry.published_parsed[:6]).replace(tzinfo=timezone.utc)
        elif hasattr(entry, "updated_parsed") and entry.updated_parsed:
            published = datetime(*entry.updated_parsed[:6]).replace(tzinfo=timezone.utc)

        # Generate stable ID from URL or title+date
        if hasattr(entry, "id") and entry.id:
            article_id = hashlib.md5(entry.id.encode()).hexdigest()[:8]
        elif hasattr(entry, "link") and entry.link:
            article_id = hashlib.md5(entry.link.encode()).hexdigest()[:8]
        else:
            article_id = hashlib.md5(
                (entry.get("title", "") + str(published)).encode()
            ).hexdigest()[:8]

        # Extract summary
        summary = ""
        if hasattr(entry, "summary"):
            summary = self._strip_html(entry.summary)
        elif hasattr(entry, "summary_detail"):
            summary = self._strip_html(entry.summary_detail.value)

        # Keep the chosen image list in sync with body anchors.
        images = self._extract_images(entry)
        markup = ""
        full_text = ""
        links = []
        styles = []
        if hasattr(entry, "content"):
            for content in entry.content:
                if content.value:
                    markup = content.value
                    full_text = html_to_text_with_images(markup, images, getattr(entry, "link", ""), links, styles)
                    break
        if not full_text and hasattr(entry, "description"):
            markup = entry.description
            full_text = html_to_text_with_images(markup, images, getattr(entry, "link", ""), links, styles)


        return Article(
            id=article_id,
            feed=feed.name,
            title=getattr(entry, "title", "Untitled"),
            author=self._extract_author(entry),
            published=published,
            original_url=getattr(entry, "link", None),
            summary=summary[:500],  # Truncate summary for index
            full_text=full_text,  # Preserve source text; wire limits belong to the client view.
            images=images,
            links=links,
            styles=styles,
        )

    def _strip_html(self, html: str) -> str:
        return html_to_text(html)

    def _extract_author(self, entry) -> Optional[str]:
        """Extract author name from feed entry."""
        if hasattr(entry, "author"):
            return entry.author
        if hasattr(entry, "author_detail") and entry.author_detail:
            return entry.author_detail.name
        return None

    def _extract_images(self, entry) -> List[dict]:
        """Extract image URLs from feed entry."""
        images = []

        # Check for enclosures (common in RSS)
        if hasattr(entry, "enclosures"):
            for enc in entry.enclosures:
                if enc.get("type", "").startswith("image/"):
                    images.append({
                        "url": enc["href"],
                        "width": 0,
                        "height": 0,
                        "format": "unknown",
                    })

        # Check for media:content (Atom media RSS)
        if hasattr(entry, "media_content"):
            for media in entry.media_content:
                if media.get("type", "").startswith("image/"):
                    images.append({
                        "url": media["url"],
                        "width": int(media.get("width", 0)),
                        "height": int(media.get("height", 0)),
                        "format": media.get("type", "unknown"),
                    })

        # Check for media:thumbnail
        if hasattr(entry, "media_thumbnail"):
            for thumb in entry.media_thumbnail:
                images.append({
                    "url": thumb["url"],
                    "width": 0,
                    "height": 0,
                    "format": "image/jpeg",
                })

        # Full Atom content often has photos that are absent from the summary.
        for content in getattr(entry, "content", []):
            images.extend(self._find_images_in_html(content.get("value", "") if hasattr(content, "get") else getattr(content, "value", "")))
        for field in ("summary", "description"):
            images.extend(self._find_images_in_html(getattr(entry, field, "")))

        unique = []
        seen = set()
        for image in images:
            url = urljoin(getattr(entry, "link", ""), image["url"])
            if url.startswith(("http://", "https://")) and url not in seen:
                image["url"] = url
                seen.add(url); unique.append(image)
        return unique[:5]

    def _find_images_in_html(self, markup: str) -> List[dict]:
        class Images(HTMLParser):
            def __init__(self):
                super().__init__(convert_charrefs=True)
                self.images = []
            def handle_starttag(self, tag, attrs):
                if tag != "img": return
                attrs = dict(attrs)
                src = attrs.get("data-src") or attrs.get("data-original") or attrs.get("src")
                if not src or src.startswith("data:"): return
                try:
                    width, height = int(attrs.get("width", 0)), int(attrs.get("height", 0))
                except (TypeError, ValueError):
                    width, height = 0, 0
                if width == 1 or height == 1: return
                self.images.append({"url": src, "width": width, "height": height, "format": "unknown"})
        parser = Images(); parser.feed(markup or "")
        return parser.images
