"""
Backend tests for the System 7 RSS Reader backend.

Network-free: exercises the wire protocol, the rolling-window normalizer, the
feed configuration defaults, and the image pipeline (synthetic PNG in, 1-bit
PackBits bitmap out). Run with:

    python3 -m unittest -v test_backend.py
"""

import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from datetime import datetime, timezone, timedelta
from unittest.mock import patch, MagicMock, call
import types

import png

# Make the backend modules importable when run from the repo root.
sys.path.insert(0, os.path.dirname(__file__))

from protocol import (
    CMD_INDEX,
    CMD_ARTICLE,
    CMD_STATUS,
    CMD_IMAGE,
    CMD_ERROR,
    encode_index,
    encode_article,
    encode_status,
    encode_error,
    encode_image,
    parse_index_response,
    parse_article_response,
)
from normalizer import ArticleNormalizer
from models import Article, ArticleIndexEntry, ArticleContent, ImageData
from config import FeedConfig
from database import RollingDatabase


def _make_png(width, height, alpha=False):
    """Build an in-memory PNG (RGB or RGBA) without touching the network."""
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            px = [(x * 3) % 256, (y * 5) % 256, (x + y) % 256]
            if alpha:
                px.append(255)
            row.extend(px)
        rows.append(bytes(row))
    buf = io.BytesIO()
    png.Writer(width, height, bitdepth=8, alpha=alpha, greyscale=False).write(buf, rows)
    return buf.getvalue()


class ProtocolRoundTripTests(unittest.TestCase):
    def _sample_index(self, count):
        return [
            ArticleIndexEntry(
                id=f"{i:08x}",
                published=datetime(2026, 1, 1, tzinfo=timezone.utc) + timedelta(minutes=i),
                feed=f"Feed {i % 3}",
                title=f"Headline number {i} with some length to exceed limits",
                summary=f"Summary text for article {i}",
                has_full_content=i % 2 == 0,
                image_count=i % 6,
            )
            for i in range(count)
        ]

    def test_index_round_trip_and_fifty_cap(self):
        entries = self._sample_index(60)
        payload = encode_index(entries)
        cmd, length = struct.unpack(">BH", payload[:3])
        body = payload[3:]
        self.assertEqual(cmd, CMD_INDEX)
        parsed = parse_index_response(body)
        self.assertEqual(len(parsed), 50)  # capped to newest 50
        # Oldest-to-newest order preserved, oldest 10 dropped.
        self.assertEqual(parsed[0].id, "0000000a")
        self.assertEqual(parsed[-1].id, "0000003b")

    def test_index_entry_fields_survive(self):
        entries = self._sample_index(3)
        payload = encode_index(entries)
        parsed = parse_index_response(payload[3:])
        for original, decoded in zip(entries, parsed):
            self.assertEqual(original.id, decoded.id)
            self.assertEqual(original.feed, decoded.feed)
            self.assertEqual(original.title, decoded.title)
            self.assertEqual(original.summary, decoded.summary)
            self.assertEqual(original.has_full_content, decoded.has_full_content)
            self.assertEqual(original.image_count, decoded.image_count)
            self.assertIsNotNone(decoded.published)

    def test_article_round_trip(self):
        content = ArticleContent(
            id="deadbeef",
            title="A full article title",
            author="Jane Doe",
            published=datetime(2026, 5, 5, 12, 30, tzinfo=timezone.utc),
            feed="BBC World",
            full_text="The body text of the article repeated.",
            images=[
                {
                    "url": "https://example.com/pic.jpg",
                    "width": 320,
                    "height": 200,
                    "format": "image/jpeg",
                }
            ],
        )
        payload = encode_article(content)
        cmd, length = struct.unpack(">BH", payload[:3])
        body = payload[3:]
        self.assertEqual(cmd, CMD_ARTICLE)
        decoded = parse_article_response(body)
        self.assertEqual(decoded.id, content.id)
        self.assertEqual(decoded.title, content.title)
        self.assertEqual(decoded.author, content.author)
        self.assertEqual(decoded.feed, content.feed)
        self.assertEqual(decoded.full_text, content.full_text)
        self.assertEqual(decoded.images[0]["url"], content.images[0]["url"])
        self.assertEqual(decoded.images[0]["width"], 320)

    def test_status_layout(self):
        payload = encode_status(article_count=42, unread_count=7)
        cmd, length = struct.unpack(">BH", payload[:3])
        body = payload[3:]
        self.assertEqual(cmd, CMD_STATUS)
        major, minor, articles, unread, flags = struct.unpack(">BBHHI", body)
        self.assertEqual((major, minor), (1, 0))
        self.assertEqual(articles, 42)
        self.assertEqual(unread, 7)
        self.assertEqual(flags, 0)

    def test_error_packet(self):
        payload = encode_error("something broke")
        cmd, length = struct.unpack(">BH", payload[:3])
        body = payload[3:]
        self.assertEqual(cmd, CMD_ERROR)
        (msg_len,) = struct.unpack(">H", body[:2])
        self.assertEqual(msg_len, len("something broke"))
        self.assertEqual(body[2:2 + msg_len].decode("ascii"), "something broke")

    def test_image_packet_layout(self):
        bitmap = bytes([0x0F, 0xF0, 0xAA, 0x55])
        payload = encode_image(
            ImageData(width=32, height=2, row_bytes=4, bitmap=bitmap),
            article_id="abc123",
            image_index=1,
        )
        cmd, length = struct.unpack(">BH", payload[:3])
        body = payload[3:]
        self.assertEqual(cmd, CMD_IMAGE)
        null = body.index(b"\x00")
        article_id = body[:null].decode("ascii")
        self.assertEqual(article_id, "abc123")
        w, h, rb = struct.unpack(">HHH", body[null + 3 : null + 9])
        self.assertEqual((w, h, rb), (32, 2, 4))
        self.assertEqual(body[null + 9:], bitmap)


class NormalizerTests(unittest.TestCase):
    def _article(self, hours_ago):
        return Article(
            id="x",
            title="t",
            published=datetime.now(timezone.utc) - timedelta(hours=hours_ago),
        )

    def test_filters_to_window_and_sorts_oldest_first(self):
        normalizer = ArticleNormalizer(window_hours=24)
        articles = [self._article(2), self._article(30), self._article(12)]
        normalized = normalizer.normalize(articles)
        ids = [a.published.hour for a in normalized]
        self.assertEqual(len(normalized), 2)  # the 30h-old article is dropped
        # Oldest (12h) first, then newest (2h).
        self.assertLess(normalized[0].published, normalized[1].published)

    def test_to_index_entries_truncates(self):
        normalizer = ArticleNormalizer(window_hours=24)
        article = Article(
            id="id1234",
            title="A" * 200,
            summary="B" * 500,
            full_text="C" * 100,
        )
        entries = normalizer.to_index_entries([article])
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0].title, "A" * 100)
        self.assertEqual(entries[0].summary, "B" * 200)
        self.assertTrue(entries[0].has_full_content)


class ConfigTests(unittest.TestCase):
    def _feed_command(self, path, *args):
        return subprocess.run([sys.executable,
            os.path.join(os.path.dirname(__file__), "main.py"),
            "--config", path, *args], check=True, capture_output=True,
            text=True, timeout=15).stdout

    def test_add_feed_command_preserves_existing_sources_and_settings(self):
        existing = {"url": "https://example.com/old.xml", "name": "Existing",
            "enabled": False, "author": "Editor",
            "image_preferences": {"max_width": 300},
            "extraction_preferences": {"selector": "article"}}
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "feeds.json")
            with open(path, "w") as file:
                json.dump({"feeds": [existing]}, file)
            output = self._feed_command(path, "--add-feed", "https://example.com/new.xml",
                                        "--feed-name", "New Source")
            with open(path) as file:
                feeds = json.load(file)["feeds"]
            self.assertIn("Added feed: New Source", output)
            self.assertEqual(len(feeds), 2)
            self.assertEqual(feeds[0], existing)
            self.assertEqual(feeds[1]["name"], "New Source")
            self.assertTrue(feeds[1]["enabled"])

    def test_remove_feed_command_loads_saved_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "feeds.json")
            with open(path, "w") as file:
                json.dump({"feeds": [
                    {"url": "https://example.com/remove.xml", "name": "Remove"},
                    {"url": "https://example.com/keep.xml", "name": "Keep", "enabled": False}
                ]}, file)
            output = self._feed_command(path, "--remove-feed", "https://example.com/remove.xml")
            with open(path) as file:
                feeds = json.load(file)["feeds"]
            self.assertIn("Removed feed:", output)
            self.assertEqual(len(feeds), 1)
            self.assertEqual(feeds[0]["name"], "Keep")
            self.assertFalse(feeds[0]["enabled"])

    def test_unknown_feed_removal_leaves_configuration_unchanged(self):
        original = '{"feeds": [{"url": "https://example.com/keep.xml", "name": "Keep"}]}\n'
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "feeds.json")
            with open(path, "w") as file:
                file.write(original)
            output = self._feed_command(path, "--remove-feed", "https://example.com/missing.xml")
            self.assertIn("Feed not found:", output)
            with open(path) as file:
                self.assertEqual(file.read(), original)

    def test_default_config_uses_live_feeds(self):
        path = os.path.join(tempfile.mkdtemp(), "feeds.json")
        config = FeedConfig(path)
        config._save_default()
        feeds = config.load()
        urls = {f.url for f in feeds}
        # The distribution starter set contains only the curated public feeds.
        self.assertNotIn("https://www.apple.com/apple/rss/feed-hub.xml", urls)
        self.assertIn("https://www.thisiscolossal.com/feed/", urls)
        self.assertIn("https://www.creativeboom.com/feed/", urls)
        self.assertFalse(any("theverge.com" in u for u in urls))


class DatabaseWindowTests(unittest.TestCase):
    def _article(self, hours_ago):
        return Article(
            id=f"id{hours_ago}",
            title=f"Title {hours_ago}",
            published=datetime.now(timezone.utc) - timedelta(hours=hours_ago),
        )

    def test_get_index_respects_rolling_window(self):
        with tempfile.TemporaryDirectory() as d:
            db = RollingDatabase(d, window_hours=24)
            db.add_articles([self._article(2), self._article(12), self._article(30)])
            ids = {e.id for e in db.get_index()}
            self.assertEqual(ids, {"id2", "id12"})  # the 30h-old article is pruned

    def test_new_articles_survive_restart_without_pruning(self):
        with tempfile.TemporaryDirectory() as d:
            db = RollingDatabase(d)
            db.add_articles([self._article(2)])
            restarted = RollingDatabase(d)
            self.assertEqual(restarted.get_article("id2").title, "Title 2")

    def test_feed_refresh_keeps_cached_body_and_updates_metadata(self):
        with tempfile.TemporaryDirectory() as d:
            db = RollingDatabase(d)
            db.add_articles([self._article(2)])
            db.set_article_content("id2", "Fetched full body")
            refreshed = self._article(2)
            refreshed.title = "Updated headline"
            db.add_articles([refreshed])
            self.assertEqual(db.get_article("id2").full_text, "Fetched full body")
            restarted = RollingDatabase(d)
            content = restarted.get_article("id2")
            self.assertEqual(content.full_text, "Fetched full body")
            self.assertEqual(content.title, "Updated headline")
            self.assertEqual(refreshed.full_text, "")

    def test_feed_refresh_accepts_updated_nonempty_body(self):
        with tempfile.TemporaryDirectory() as d:
            db = RollingDatabase(d)
            db.add_articles([self._article(2)])
            db.set_article_content("id2", "Old body")
            refreshed = self._article(2)
            refreshed.full_text = "Corrected body"
            db.add_articles([refreshed])
            self.assertEqual(RollingDatabase(d).get_article("id2").full_text,
                             "Corrected body")

    def test_negative_image_index_does_not_return_last_image(self):
        with tempfile.TemporaryDirectory() as d:
            db = RollingDatabase(d)
            article = self._article(2)
            article.images = [{"url": "https://example.com/image.png"}]
            db.add_articles([article])
            self.assertIsNone(db.get_image("id2", -1))
            self.assertIsNone(db.get_image("id2", 1))
            self.assertEqual(db.get_image("id2", 0), article.images[0])


class ImageExtractionTests(unittest.TestCase):
    def test_inline_links_do_not_insert_spaces_inside_quotes_or_before_periods(self):
        from fetcher import FeedFetcher
        from content_fetcher import _BodyExtractor
        from protocol import _client_text
        markup = '<p>He calls it “<a href="/ai">Super Intelligence</a>.” Here’s <em>how</em> it works.</p><p>Another paragraph.</p>'
        text = FeedFetcher()._strip_html(markup)
        self.assertEqual(_client_text(text, 9999).decode("mac_roman"),
                         'He calls it “Super Intelligence.” Here’s how it works.\n\nAnother paragraph.')
        parser = _BodyExtractor(); parser.feed('<body>'+markup+'</body>')
        self.assertIn('“Super Intelligence.” Here’s how', parser.get_text())
        self.assertEqual(_client_text('“ Super Intelligence .”', 100).decode("mac_roman"),
                         '“Super Intelligence.”')

    def test_full_content_lazy_images_relative_urls_and_duplicate_sources(self):
        from fetcher import FeedFetcher
        entry = types.SimpleNamespace(link="https://example.com/post/",
                content=[{"value": "<img data-src='../photo.jpg' src='data:image/gif,x'><img src=\"/two.png\">"}],
                summary="<img src='https://example.com/photo.jpg'><img src='/pixel.png' width='1' height='1'>")
        images = FeedFetcher()._extract_images(entry)
        self.assertEqual([a["url"] for a in images],
                         ["https://example.com/photo.jpg", "https://example.com/two.png"])

class ImageProcessorTests(unittest.TestCase):
    def test_png_to_1bit_bitmap_dimensions(self):
        from image_processor import ImageProcessor
        from test_support import MacPaintFile

        png_bytes = _make_png(96, 48, alpha=True)
        proc = ImageProcessor()
        out = proc.process_image(png_bytes, "article")
        self.assertEqual((out.width, out.height), (96, 48))
        self.assertEqual(out.row_bytes, (out.width + 7) // 8)
        # Packed bitmap decompresses to exactly row_bytes * height bytes.
        decoder = MacPaintFile.__new__(MacPaintFile)
        self.assertEqual(len(decoder._unpack_bits(out.bitmap)), out.row_bytes * out.height)

    def test_viewport_width_has_its_own_cached_conversion(self):
        from image_processor import ImageProcessor
        proc = ImageProcessor()
        with patch("urllib.request.urlopen") as mock_open:
            mock_open.return_value.__enter__.return_value.read.return_value = _make_png(640, 320)
            small = proc.process_from_url("http://example.com/photo.png", max_width=188)
            large = proc.process_from_url("http://example.com/photo.png", max_width=280)
            again = proc.process_from_url("http://example.com/photo.png", max_width=188)
        self.assertEqual((small.width, small.height), (188, 94))
        self.assertEqual((large.width, large.height), (280, 140))
        self.assertIs(again, small)
        self.assertEqual(mock_open.call_count, 2)

    def test_packbits_scanline_round_trip(self):
        from image_processor import ImageProcessor
        from test_support import MacPaintFile

        png_bytes = _make_png(96, 48)
        proc = ImageProcessor()
        out = proc.process_image(png_bytes, "article")

        decoder = MacPaintFile.__new__(MacPaintFile)
        decoded = decoder._unpack_bits(out.bitmap)
        self.assertEqual(len(decoded), out.row_bytes * out.height)

    def test_thumb_variant_is_smaller(self):
        from image_processor import ImageProcessor

        proc = ImageProcessor()
        out = proc.process_image(_make_png(640, 320), "thumb")
        self.assertEqual((out.width, out.height), (120, 60))

    def test_process_from_url_is_cached(self):
        from image_processor import ImageProcessor

        proc = ImageProcessor()
        png_bytes = _make_png(96, 48)
        real_process = proc.process_image
        calls = {"n": 0}

        def counting_process(image_data, variant="article", max_width=None):
            calls["n"] += 1
            return real_process(image_data, variant, max_width)

        proc.process_image = counting_process
        with patch("urllib.request.urlopen") as mock_open:
            mock_open.return_value.__enter__.return_value.read.return_value = png_bytes
            first = proc.process_from_url("http://example.com/a.png")
            second = proc.process_from_url("http://example.com/a.png")
        self.assertIsNotNone(first)
        self.assertIs(first, second)  # identical cached object
        self.assertEqual(calls["n"], 1)  # download+process happened once
        self.assertEqual(mock_open.call_count, 1)  # only one HTTP request

    def test_clear_cache_forgets_images(self):
        from image_processor import ImageProcessor

        proc = ImageProcessor()
        png_bytes = _make_png(96, 48)
        real_process = proc.process_image
        calls = {"n": 0}

        def counting_process(image_data, variant="article", max_width=None):
            calls["n"] += 1
            return real_process(image_data, variant, max_width)

        proc.process_image = counting_process
        with patch("urllib.request.urlopen") as mock_open:
            def make_response(*a, **k):
                resp = MagicMock()
                resp.__enter__.return_value.read.return_value = png_bytes
                return resp
            mock_open.side_effect = make_response
            proc.process_from_url("http://example.com/a.png")
            proc.clear_cache()
            proc.process_from_url("http://example.com/a.png")
        self.assertEqual(calls["n"], 2)  # reprocessed after clearing cache
        self.assertEqual(mock_open.call_count, 2)


def _fake_entry(**overrides):
    """Build a feedparser-like entry for fetcher tests."""
    data = dict(
        published_parsed=(2026, 1, 1, 12, 0, 0) + (0, 0, 0),
        id="urn:test:1",
        link="https://example.com/1",
        summary="<p>Hello <b>world</b></p>",
        content=[types.SimpleNamespace(value="<p>Full body text here</p>")],
        author="Jane Doe",
        media_content=[
            {"type": "image/jpeg", "url": "https://example.com/img.jpg", "width": 640, "height": 480}
        ],
    )
    data.update(overrides)
    return types.SimpleNamespace(**data)


class FetchTests(unittest.TestCase):
    def setUp(self):
        self.response = MagicMock()
        self.response.read.return_value = b"<rss><channel></channel></rss>"
        self.response.headers.items.return_value = []
        self.response.geturl.return_value = "https://example.com/feed.xml"
        transport = patch("urllib.request.urlopen")
        self.upstream = transport.start()
        self.addCleanup(transport.stop)
        self.upstream.return_value.__enter__.return_value = self.response

    def test_publication_time_wins_over_update_time(self):
        from fetcher import FeedFetcher
        from models import Feed
        entry = _fake_entry(updated_parsed=(2026, 1, 2, 15, 0, 0, 0, 0, 0))
        article = FeedFetcher()._parse_entry(entry, Feed(url="https://example.com/rss", name="Test"))
        self.assertEqual(article.published, datetime(2026, 1, 1, 12, tzinfo=timezone.utc))

    def test_updated_time_is_only_a_missing_publication_fallback(self):
        from fetcher import FeedFetcher
        from models import Feed
        entry = _fake_entry(published_parsed=None,
                            updated_parsed=(2026, 1, 2, 15, 0, 0, 0, 0, 0))
        article = FeedFetcher()._parse_entry(entry, Feed(url="https://example.com/rss", name="Test"))
        self.assertEqual(article.published, datetime(2026, 1, 2, 15, tzinfo=timezone.utc))

    def test_missing_feed_dates_do_not_become_download_time(self):
        from fetcher import FeedFetcher
        from models import Feed
        entry = _fake_entry(published_parsed=None)
        article = FeedFetcher()._parse_entry(entry, Feed(url="https://example.com/rss", name="Test"))
        self.assertIsNone(article.published)
        self.assertEqual(ArticleNormalizer().normalize([article]), [])

    def test_fetch_parses_entries_and_passes_timeout(self):
        import fetcher as fetcher_module
        from fetcher import FeedFetcher
        from models import Feed

        captured = {}

        def fake_parse(url, **kwargs):
            captured["url"] = url
            captured["kwargs"] = kwargs
            return types.SimpleNamespace(entries=[_fake_entry()], bozo=0)

        with patch.object(fetcher_module.feedparser, "parse", side_effect=fake_parse):
            f = FeedFetcher(fetch_interval=0, timeout=42)
            articles = f.fetch_feeds([Feed(url="https://example.com/feed.xml", name="Test")])

        self.assertEqual(len(articles), 1)
        a = articles[0]
        self.assertIn("Hello", a.summary)
        self.assertIn("world", a.summary)
        self.assertEqual(a.author, "Jane Doe")
        self.assertEqual(a.original_url, "https://example.com/1")
        self.assertEqual(a.full_text, "Full body text here")
        self.assertEqual(a.images[0]["url"], "https://example.com/img.jpg")

    def test_fetch_uses_request_timeout_and_bounded_read(self):
        from fetcher import FeedFetcher
        from models import Feed
        with patch("socket.setdefaulttimeout") as global_timeout:
            FeedFetcher(fetch_interval=0, timeout=7).fetch_feeds(
                [Feed(url="https://example.com/feed.xml", name="Test")])
        self.upstream.assert_called_once()
        self.assertEqual(self.upstream.call_args.kwargs["timeout"], 7)
        self.response.read.assert_called_once_with(4 * 1024 * 1024 + 1)
        global_timeout.assert_not_called()

    def test_fetch_skips_failed_feed(self):
        import fetcher as fetcher_module
        from fetcher import FeedFetcher
        from models import Feed

        def boom(url, **kwargs):
            raise RuntimeError("connection refused")

        with patch.object(fetcher_module.feedparser, "parse", side_effect=boom):
            f = FeedFetcher(fetch_interval=0)
            articles = f.fetch_feeds([Feed(url="https://example.com/bad.xml", name="Bad")])
        self.assertEqual(articles, [])

    def test_fetch_respects_interval(self):
        import fetcher as fetcher_module
        from fetcher import FeedFetcher
        from models import Feed

        calls = {"n": 0}

        def fake_parse(url, **kwargs):
            calls["n"] += 1
            return types.SimpleNamespace(entries=[], bozo=0)

        with patch.object(fetcher_module.feedparser, "parse", side_effect=fake_parse):
            f = FeedFetcher(fetch_interval=300)
            f.fetch_feeds([Feed(url="https://example.com/feed.xml", name="Test")])
            f.fetch_feeds([Feed(url="https://example.com/feed.xml", name="Test")])
        self.assertEqual(calls["n"], 1)  # second call skipped, within interval

    def test_conditional_requests_keep_validators_on_304(self):
        from fetcher import FeedFetcher
        from models import Feed
        feed = Feed(url="https://example.com/rss", name="Test")
        unchanged = types.SimpleNamespace(status=304, entries=[])
        response = types.SimpleNamespace(status=200, entries=[_fake_entry()], bozo=0,
                                         etag='"v1"', modified="Wed, 01 Jan 2026 12:00:00 GMT")
        with patch("fetcher.feedparser.parse", side_effect=[response, unchanged, unchanged]) as parse:
            fetcher = FeedFetcher(fetch_interval=0)
            self.assertEqual(len(fetcher.fetch_feeds([feed])), 1)
            self.assertEqual(fetcher.fetch_feeds([feed]), [])
            self.assertEqual(fetcher.fetch_feeds([feed]), [])
        self.assertEqual(parse.call_count, 3)
        for request in self.upstream.call_args_list[1:]:
            headers = dict(request.args[0].header_items())
            self.assertEqual(headers["If-none-match"], '"v1"')
            self.assertEqual(headers["If-modified-since"], response.modified)

    def test_failed_response_does_not_replace_validators_and_is_throttled(self):
        from fetcher import FeedFetcher
        from models import Feed
        feed = Feed(url="https://example.com/rss", name="Test")
        success = types.SimpleNamespace(status=200, entries=[], bozo=0, etag='"good"')
        failure = types.SimpleNamespace(status=503, entries=[], etag='"error"')
        with patch("fetcher.feedparser.parse", side_effect=[success, failure]) as parse, \
             patch("fetcher.time.time", side_effect=[0, 400, 400, 401]):
            fetcher = FeedFetcher(fetch_interval=300)
            fetcher.fetch_feeds([feed])
            fetcher.fetch_feeds([feed])
            fetcher.fetch_feeds([feed])
        self.assertEqual(parse.call_count, 2)
        self.assertEqual(fetcher._validators[feed.url], {"etag": '"good"'})

    def test_malformed_feed_does_not_commit_validators(self):
        from fetcher import FeedFetcher
        from models import Feed
        broken = types.SimpleNamespace(status=200, entries=[], bozo=1, etag='"broken"')
        with patch("fetcher.feedparser.parse", return_value=broken):
            fetcher = FeedFetcher(fetch_interval=0)
            self.assertEqual(fetcher.fetch_feeds([Feed(url="https://example.com/rss", name="Test")]), [])
            self.assertEqual(fetcher._validators, {})


class ServerStartupTests(unittest.TestCase):
    def test_cached_index_is_available_during_initial_background_fetch(self):
        import threading
        import server
        from database import RollingDatabase
        started, release, finished = threading.Event(), threading.Event(), threading.Event()
        bound = []
        responses = []
        def slow_fetch(feeds):
            self.assertTrue(bound, "fetch began before the listener was bound")
            started.set()
            release.wait(3)
            finished.set()
            return []
        test = self
        class Listener:
            def __init__(self, address, handler):
                bound.append(address)
            def __enter__(self):
                return self
            def __exit__(self, *args):
                pass
            def serve_forever(self):
                test.assertTrue(started.wait(3))
                test.assertFalse(finished.is_set())
                handler = server.RSSHandler.__new__(server.RSSHandler)
                handler._send_response = lambda status, packet: responses.append((status, packet))
                handler._handle_sync({})
                # Reading the cache did not wait for the pending feed request.
                test.assertFalse(finished.is_set())
                raise KeyboardInterrupt()
        with tempfile.TemporaryDirectory() as directory:
            db = RollingDatabase(directory, window_hours=168)
            now = datetime.now(timezone.utc)
            db.add_articles([Article(id="cache001", title="Cached", published=now-timedelta(hours=2)),
                             Article(id="cache002", title="Older", published=now-timedelta(hours=30))])
            with patch.object(server, "RSSServer", Listener), \
                 patch.object(server.FeedFetcher, "fetch_feeds", side_effect=slow_fetch):
                try:
                    server.serve(port=8081, data_dir=directory, config_path=os.path.join(directory, "feeds.json"))
                finally:
                    release.set()
                    self.assertTrue(finished.wait(3))
            self.assertEqual(responses[0][0], 200)
            self.assertEqual(int.from_bytes(responses[0][1][3:5], "big"), 1)
            self.assertEqual(len(server.RSSHandler.database.get_index()), 2)


SAMPLE_ARTICLE_HTML = """
<html><head><title>Real Article Title</title><style>.x{color:red}</style></head>
<body>
  <nav>Home About Contact</nav>
  <script>var x = 1;</script>
  <article>
    <h1>Headline</h1>
    <p>First paragraph of the story.</p>
    <p>Second paragraph with <b>bold</b> text.</p>
  </article>
  <footer>Copyright 2026</footer>
</body></html>
"""


class ContentFetcherTests(unittest.TestCase):
    def _response(self, body, charset="utf-8"):
        resp = MagicMock()
        resp.headers.get_content_charset.return_value = charset
        resp.read.return_value = body.encode(charset)
        cm = MagicMock()
        cm.__enter__.return_value = resp
        cm.__exit__.return_value = False
        return cm

    def test_fetch_article_extracts_body_and_title(self):
        from content_fetcher import fetch_article
        with patch("urllib.request.urlopen", return_value=self._response(SAMPLE_ARTICLE_HTML)):
            content = fetch_article("https://example.com/article")
        self.assertIsNotNone(content)
        self.assertEqual(content.title, "Real Article Title")
        self.assertIn("First paragraph of the story.", content.full_text)
        self.assertIn("Second paragraph", content.full_text)
        # Navigation, script, and footer chrome must be dropped.
        self.assertNotIn("Home About Contact", content.full_text)
        self.assertNotIn("Copyright 2026", content.full_text)
        self.assertNotIn("var x", content.full_text)

    def test_fetch_article_returns_none_on_failure(self):
        from content_fetcher import fetch_article
        with patch("urllib.request.urlopen", side_effect=RuntimeError("boom")):
            content = fetch_article("https://example.com/down")
        self.assertIsNone(content)

    def test_fetch_article_applies_timeout(self):
        from content_fetcher import fetch_article
        with patch("urllib.request.urlopen", return_value=self._response("<p>hi there</p>")) as opened, \
             patch("socket.setdefaulttimeout") as global_timeout:
            content = fetch_article("https://example.com/x", timeout=9)
        self.assertIsNotNone(content)
        self.assertEqual(opened.call_args.kwargs["timeout"], 9)
        global_timeout.assert_not_called()


class PrefetchTests(unittest.TestCase):
    def test_prefetch_fetches_only_missing_bodies(self):
        import server as server_module
        from server import RSSHandler
        from fetcher import FeedFetcher

        now = datetime.now(timezone.utc)
        with tempfile.TemporaryDirectory() as d:
            db = RollingDatabase(d)
            db.add_articles([
                Article(id="a1", title="One", published=now - timedelta(minutes=60),
                        original_url="https://example.com/1", full_text="already present"),
                Article(id="a2", title="Two", published=now - timedelta(minutes=30),
                        original_url="https://example.com/2", full_text=""),
            ])

            handler = RSSHandler.__new__(RSSHandler)
            handler.database = db
            handler.fetcher = FeedFetcher(fetch_interval=0, timeout=15)
            handler._send_response = lambda status, data: None

            fetched = {}

            def fake_fetch(url, timeout=15):
                fetched[url] = timeout
                return ArticleContent(id="", title="Body Two", full_text="The full body of article two.")

            original_fetch = server_module.fetch_article
            server_module.fetch_article = fake_fetch
            try:
                handler._handle_prefetch("a1", count=3)
            finally:
                server_module.fetch_article = original_fetch

            # Only the article lacking content is fetched, with the configured timeout.
            self.assertEqual(list(fetched.keys()), ["https://example.com/2"])
            self.assertEqual(fetched["https://example.com/2"], 15)
            # And the fetched body is persisted.
            self.assertTrue(db.has_full_content("a2"))
            self.assertEqual(db._articles["a2"].full_text, "The full body of article two.")


if __name__ == "__main__":
    unittest.main()
