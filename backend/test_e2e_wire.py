"""
End-to-end wire-contract test for the System 7 RSS Reader.

This is the closest thing to a true end-to-end test we can run on a modern
host without a 68K emulator: it starts the real backend HTTP server with a
seeded database, fetches a live /sync packet over HTTP, and decodes those exact
bytes with the client's actual parser (client/Wire.c, compiled for the host).

That exercises the full backend->client wire path:
    encode_index (backend)  ->  HTTP transport  ->  WireDecodeIndex (client)

A raw-HTTP test also exercises the client HTTPResponse.c framer. The MacTCP
driver needs a physical System 7 networking test; GUI checks use Mini vMac.

Run with:
    python3 -m unittest -v test_e2e_wire.py
"""

import os
import shutil
import types
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from datetime import datetime, timezone, timedelta

sys.path.insert(0, os.path.dirname(__file__))

from database import RollingDatabase
from fetcher import FeedFetcher
from normalizer import ArticleNormalizer
from server import RSSHandler, RSSServer
from models import Article


def _make_article(article_id, feed, title, summary, published_dt,
                  full_text="", image_count=0):
    return Article(
        id=article_id,
        feed=feed,
        title=title,
        summary=summary,
        published=published_dt,
        original_url=f"https://example.com/{article_id}",
        full_text=full_text,
        images=[{"url": f"https://example.com/img/{article_id}_{i}.png",
                 "width": 10, "height": 10, "format": "bitmap"}
                for i in range(image_count)],
    )


class E2EWireTest(unittest.TestCase):
    """Drive the real backend over HTTP and decode with the client parser."""

    harness_bin = None
    httpd = None
    base_url = None

    @classmethod
    def setUpClass(cls):
        # Build the host harness that links the client's real WireDecodeIndex.
        client_dir = os.path.join(os.path.dirname(__file__), "..", "client")
        harness_src = os.path.join(client_dir, "e2e_wire_check.c")
        wire_src = os.path.join(client_dir, "Wire.c")
        tmp = tempfile.mkdtemp(prefix="e2e_wire_")
        cls.harness_bin = os.path.join(tmp, "e2e_wire_check")

        clang = shutil.which("clang") or shutil.which("cc")
        if not clang:
            cls.skipTest("no C compiler (clang/cc) available for the client harness")

        try:
            subprocess.run(
                [clang, "-I", client_dir, harness_src, wire_src,
                 os.path.join(client_dir, "HTTPResponse.c"),
                 "-o", cls.harness_bin],
                check=True, capture_output=True, text=True,
            )
        except subprocess.CalledProcessError as e:
            cls.skipTest(f"failed to build client harness: {e.stderr}")

    def _start_server(self, articles, retention_hours=24):
        """Seed a database, start the real HTTP server on an ephemeral port."""
        tmpdir = tempfile.mkdtemp(prefix="e2e_db_")
        db = RollingDatabase(tmpdir, window_hours=retention_hours)
        db.add_articles(articles)

        normalizer = ArticleNormalizer(window_hours=24)
        fetcher = FeedFetcher()
        # No live network: pretend the fetcher found nothing new.
        fetcher.fetch_feeds = lambda feeds: []
        # No live network: pretend there are no feeds to fetch.
        config = types.SimpleNamespace(get_enabled_feeds=lambda: [])

        RSSHandler.default_window_hours = 24
        RSSHandler.database = db
        RSSHandler.fetcher = fetcher
        RSSHandler.normalizer = normalizer
        RSSHandler.config = config
        RSSHandler.processor = None

        self.httpd = RSSServer(("", 0), RSSHandler)
        self.base_url = f"http://127.0.0.1:{self.httpd.server_address[1]}"

        thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(self.httpd.shutdown)
        self.addCleanup(self.httpd.server_close)

    def _fetch_sync(self):
        import urllib.request
        with urllib.request.urlopen(f"{self.base_url}/sync", timeout=10) as resp:
            return resp.read()

    def _run_harness(self, packet, http=False):
        with tempfile.NamedTemporaryFile(
            prefix="sync_", suffix=".bin", delete=False
        ) as tf:
            tf.write(packet)
            path = tf.name
        self.addCleanup(os.unlink, path)

        proc = subprocess.run(
            [self.harness_bin, path] + (["--http"] if http else []),
            capture_output=True, text=True,
        )
        return proc.returncode, proc.stdout, proc.stderr

    def _parse_harness_output(self, stdout):
        lines = stdout.splitlines()
        self.assertEqual(lines[0], "OK",
                         f"client parser did not decode the packet: {stdout}")
        count = int(lines[1].split()[1])
        entries = []
        cur = {}
        for line in lines[2:]:
            if line == "---":
                if cur:
                    entries.append(cur)
                    cur = {}
                continue
            if ":" in line:
                key, _, value = line.partition(":")
                cur[key.strip()] = value
        if cur:
            entries.append(cur)
        return count, entries

    def test_sync_packet_round_trips_through_client_parser(self):
        now = datetime.now(timezone.utc)
        articles = [
            _make_article("art00001", "Colossal",
                          "A headline about design", "A short summary here",
                          now - timedelta(hours=5),
                          full_text="full body text", image_count=2),
            _make_article("art00002", "The Verge",
                          "Quickpost title", "Quickpost summary",
                          now - timedelta(hours=3), image_count=0),
            _make_article("art00003", "Six Colors",
                          "Subscriber only article", "Body preview",
                          now - timedelta(hours=1), image_count=1),
        ]
        self._start_server(articles)

        packet = self._fetch_sync()
        self.assertGreater(len(packet), 0, "empty /sync response")

        # Sanity: the backend's own reference parser must agree on the count.
        from protocol import parse_index_response
        parsed = parse_index_response(packet[3:])
        self.assertEqual(len(parsed), len(articles))

        rc, stdout, stderr = self._run_harness(packet)
        self.assertEqual(rc, 0, f"harness failed: {stderr}")

        count, entries = self._parse_harness_output(stdout)
        self.assertEqual(count, len(articles))
        self.assertEqual(len(entries), len(articles))

        # Entries are ordered oldest-first, matching the seed order.
        expected_ids = ["art00001", "art00002", "art00003"]
        self.assertEqual([e["id"] for e in entries], expected_ids)

        first = entries[0]
        self.assertEqual(first["feed"], "Colossal")
        self.assertEqual(first["title"], "A headline about design")
        self.assertEqual(first["summary"], "A short summary here")
        self.assertEqual(first["imageCount"], "2")
        self.assertEqual(first["full"], "1")

        second = entries[1]
        self.assertEqual(second["feed"], "The Verge")
        self.assertEqual(second["imageCount"], "0")
        self.assertEqual(second["full"], "0")

        # published timestamps must survive the big-endian double round trip.
        self.assertAlmostEqual(float(first["published"]),
                               (now - timedelta(hours=5)).timestamp(), delta=1.0)

    def test_local_dates_endpoint_matches_the_filtered_queue(self):
        import urllib.request, struct
        from protocol import local_timezone
        now = datetime.now(timezone.utc).replace(hour=1, minute=5)
        self._start_server([_make_article("12345678", "Café", "Here’s  how", "", now),
                            _make_article("87654321", "Other", "Other", "", now)])
        RSSHandler.display_timezone = local_timezone("America/Chicago")
        key = "Café".encode("mac_roman").hex()
        with urllib.request.urlopen(self.base_url + "/dates?feed=" + key) as r:
            data = r.read()
        self.assertEqual(data[0], 9)
        self.assertEqual(struct.unpack(">H", data[3:5])[0], 1)
        self.assertEqual(data[5:14], b"12345678\0")
        self.assertIn(b"20:05", data)
        self.assertIn(b"8:05 PM", data)

    def test_real_http_photo_is_sized_dithered_and_sent(self):
        import io, struct, urllib.request, http.server
        from PIL import Image
        from image_processor import ImageProcessor
        image = Image.new("RGB", (640, 160))
        image.putdata([(x % 256, y, (x+y) % 256) for y in range(160) for x in range(640)])
        stream = io.BytesIO(); image.save(stream, format="JPEG"); photo = stream.getvalue()
        class Photos(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_response(200); self.end_headers(); self.wfile.write(photo)
            def log_message(self, *args): pass
        photos = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Photos)
        threading.Thread(target=photos.serve_forever, daemon=True).start()
        self.addCleanup(photos.server_close); self.addCleanup(photos.shutdown)
        article = _make_article("12345678", "Photo", "Photo", "", datetime.now(timezone.utc), image_count=1)
        article.images[0]["url"] = f"http://127.0.0.1:{photos.server_address[1]}/photo.jpg"
        self._start_server([article]); RSSHandler.processor = ImageProcessor()
        with urllib.request.urlopen(self.base_url + "/image/12345678/0?width=188") as r: data = r.read()
        self.assertEqual(data[0], 4)
        self.assertEqual(struct.unpack(">HHHH", data[12:20]), (0, 188, 47, 24))
        self.assertGreater(len(data), 100)

    def test_empty_database_yields_zero_entries(self):
        self._start_server([])
        packet = self._fetch_sync()
        rc, stdout, stderr = self._run_harness(packet)
        self.assertEqual(rc, 0, f"harness failed: {stderr}")
        count, entries = self._parse_harness_output(stdout)
        self.assertEqual(count, 0)
        self.assertEqual(entries, [])

    def test_feed_filter_applies_before_the_fifty_article_cap(self):
        import urllib.request
        now = datetime.now(timezone.utc)
        articles = [_make_article(f"{i:08x}", "Café" if i == 0 else "Other",
                                  str(i), "Summary", now - timedelta(minutes=70-i))
                    for i in range(70)]
        self._start_server(articles)
        wanted = "Café".encode("mac_roman").hex()
        with urllib.request.urlopen(f"{self.base_url}/sync?feed={wanted}", timeout=10) as response:
            packet = response.read()
        rc, stdout, stderr = self._run_harness(packet)
        self.assertEqual(rc, 0, stderr)
        count, entries = self._parse_harness_output(stdout)
        self.assertEqual(count, 1)
        self.assertEqual(entries[0]["id"], "00000000")
        with urllib.request.urlopen(f"{self.base_url}/feeds", timeout=10) as response:
            packet = response.read()
        self.assertEqual(packet[0], 0x06)
        self.assertEqual(int.from_bytes(packet[3:5], "big"), 2)
        self.assertIn("Café".encode("mac_roman"), packet)

    def test_feed_filter_with_no_articles_is_empty(self):
        import urllib.request
        self._start_server([])
        with urllib.request.urlopen(f"{self.base_url}/sync?feed=4e6f6e65", timeout=10) as response:
            packet = response.read()
        rc, stdout, stderr = self._run_harness(packet)
        self.assertEqual(rc, 0, stderr)
        count, entries = self._parse_harness_output(stdout)
        self.assertEqual((count, entries), (0, []))

    def test_raw_http_response_round_trips_through_client_framing_and_decoder(self):
        self._start_server([_make_article("raw00001", "Feed", "Raw HTTP title",
                                         "Summary", datetime.now(timezone.utc))])
        with socket.create_connection(("127.0.0.1", self.httpd.server_address[1]), timeout=10) as stream:
            stream.sendall(b"GET /sync HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n")
            chunks = []
            while True:
                chunk = stream.recv(2048)
                if not chunk:
                    break
                chunks.append(chunk)
        rc, stdout, stderr = self._run_harness(b"".join(chunks), http=True)
        self.assertEqual(rc, 0, stderr)
        count, entries = self._parse_harness_output(stdout)
        self.assertEqual(count, 1)
        self.assertEqual(entries[0]["id"], "raw00001")
        self.assertEqual(entries[0]["title"], "Raw HTTP title")

    def test_real_feed_http_conditional_request_preserves_cached_articles(self):
        import http.server
        from email.utils import format_datetime
        from models import Feed
        seen = []
        now = datetime.now(timezone.utc)
        feed_xml = (f'<rss version="2.0"><channel><title>Local</title>'
                    f'<link>http://example.com</link><description>Test</description>'
                    f'<item><guid>cached-feed-story</guid><title>Cached story</title>'
                    f'<pubDate>{format_datetime(now)}</pubDate></item></channel></rss>').encode()
        class FeedHTTP(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass
            def do_GET(self):
                seen.append((self.headers.get("If-None-Match"), self.headers.get("If-Modified-Since")))
                if self.headers.get("If-None-Match") == '"v1"':
                    self.send_response(304)
                    self.end_headers()
                else:
                    self.send_response(200)
                    self.send_header("ETag", '"v1"')
                    self.send_header("Last-Modified", format_datetime(now, usegmt=True))
                    self.send_header("Content-Length", str(len(feed_xml)))
                    self.end_headers()
                    self.wfile.write(feed_xml)
        httpd = http.server.HTTPServer(("127.0.0.1", 0), FeedHTTP)
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(httpd.server_close)
        self.addCleanup(httpd.shutdown)
        fetcher = FeedFetcher(fetch_interval=0)
        feed = Feed(url=f"http://127.0.0.1:{httpd.server_address[1]}/rss", name="Local")
        self._start_server([], retention_hours=168)
        first = fetcher.fetch_feeds([feed])
        self.assertEqual(len(first), 1)
        RSSHandler.database.add_articles(first)
        second = fetcher.fetch_feeds([feed])
        self.assertEqual(second, [])
        self.assertEqual(seen[0], (None, None))
        self.assertEqual(seen[1], ('"v1"', format_datetime(now, usegmt=True)))
        rc, output, error = self._run_harness(self._fetch_sync())
        self.assertEqual(rc, 0, error)
        self.assertEqual(self._parse_harness_output(output)[0], 1)

    def test_refresh_never_contacts_upstream_feeds(self):
        from unittest.mock import Mock
        self._start_server([_make_article("cache001", "Feed", "Cached story", "", datetime.now(timezone.utc))])
        RSSHandler.fetcher.fetch_feeds = Mock(side_effect=AssertionError("Refresh contacted upstream"))
        rc, output, error = self._run_harness(self._fetch_sync())
        self.assertEqual(rc, 0, error)
        self.assertEqual(self._parse_harness_output(output)[0], 1)
        RSSHandler.fetcher.fetch_feeds.assert_not_called()

    def test_age_window_filters_index_dates_and_status(self):
        import urllib.request
        now = datetime.now(timezone.utc)
        stories = [_make_article(f"{i:08x}", "Quiet", str(age), "", now-timedelta(hours=age))
                   for i, age in enumerate((2, 30, 60, 100, 170))]
        self._start_server(stories, retention_hours=168)
        for hours, expected in ((24, 1), (48, 2), (72, 3), (168, 4)):
            for endpoint in ("sync", "dates", "status"):
                with urllib.request.urlopen(f"{self.base_url}/{endpoint}?hours={hours}", timeout=10) as response:
                    packet = response.read()
                if endpoint == "sync":
                    rc, output, error = self._run_harness(packet)
                    self.assertEqual(rc, 0, error)
                    count, entries = self._parse_harness_output(output)
                    self.assertEqual(count, expected)
                elif endpoint == "dates":
                    self.assertEqual(int.from_bytes(packet[3:5], "big"), expected)
                else:
                    self.assertEqual(int.from_bytes(packet[5:7], "big"), expected)
        self.assertEqual(self._parse_harness_output(self._run_harness(self._fetch_sync())[1])[0], 1)
        # Quiet-feed history is selected before the cap for busy sources.
        RSSHandler.database.add_articles([
            _make_article(f"{i+100:08x}", "Busy", "Busy", "", now-timedelta(minutes=i)) for i in range(60)])
        with urllib.request.urlopen(f"{self.base_url}/sync?hours=168&feed={'Quiet'.encode().hex()}", timeout=10) as response:
            rc, output, error = self._run_harness(response.read())
        self.assertEqual(rc, 0, error)
        self.assertEqual(self._parse_harness_output(output)[0], 4)
        self.assertEqual(self._parse_harness_output(self._run_harness(self._fetch_sync())[1])[0], 50)

    def test_paged_view_has_all_articles_and_stays_stable_during_polling(self):
        import urllib.request
        from unittest.mock import Mock
        now = datetime.now(timezone.utc)
        stories = [_make_article(f"{i:08x}", "Busy", str(i), "", now-timedelta(minutes=150-i))
                   for i in range(125)]
        stories.append(_make_article("old00001", "Busy", "Outside window", "", now-timedelta(hours=25)))
        self._start_server(stories)
        RSSHandler.fetcher.fetch_feeds = Mock(side_effect=AssertionError("View fetched upstream"))
        ids, view = [], ""
        for offset, expected in ((0, 50), (50, 50), (100, 25)):
            path = f"/page?hours=24&view={view}&offset={offset}"
            with urllib.request.urlopen(self.base_url+path) as response:
                packet = response.read()
            rc, output, error = self._run_harness(packet)
            self.assertEqual(rc, 0, error)
            count, entries = self._parse_harness_output(output)
            self.assertEqual(count, expected)
            self.assertEqual(int.from_bytes(packet[11:15], "big"), 125)
            self.assertEqual(int.from_bytes(packet[15:19], "big"), offset)
            token = packet[3:11].decode()
            if view:
                self.assertEqual(token, view)
            view = token
            page_ids = [entry["id"] for entry in entries]
            ids.extend(page_ids)
            with urllib.request.urlopen(self.base_url+f"/dates?view={view}&offset={offset}") as response:
                dates = response.read()
            self.assertEqual(int.from_bytes(dates[3:5], "big"), expected)
            for article_id in page_ids:
                self.assertIn(article_id.encode()+b"\0", dates)
            # Add an article that sorts before this page; an offset-based live
            # query would shift boundaries and duplicate/omit stories.
            RSSHandler.database.add_articles([_make_article("new00001", "Busy", "Arrived during refresh", "",
                                                            now-timedelta(minutes=200))])
        self.assertEqual(ids, [f"{i:08x}" for i in range(125)])
        RSSHandler.fetcher.fetch_feeds.assert_not_called()
        with urllib.request.urlopen(self.base_url+"/page?hours=24") as response:
            self.assertEqual(int.from_bytes(response.read()[11:15], "big"), 126)

    def test_paged_views_filter_feed_and_age_without_a_count_cap(self):
        import urllib.request
        now = datetime.now(timezone.utc)
        stories = [_make_article(f"{i:08x}", "Quiet", str(i), "", now-timedelta(hours=i))
                   for i in range(100)]
        stories += [_make_article(f"{i+100:08x}", "Busy", str(i), "", now-timedelta(minutes=i))
                    for i in range(70)]
        self._start_server(stories, retention_hours=168)
        for hours, expected in ((24, 24), (48, 48), (72, 72), (168, 100)):
            view, ids, offset = "", [], 0
            while True:
                with urllib.request.urlopen(self.base_url+f"/page?hours={hours}&feed=5175696574&view={view}&offset={offset}") as response:
                    packet = response.read()
                rc, output, error = self._run_harness(packet)
                self.assertEqual(rc, 0, error)
                count, entries = self._parse_harness_output(output)
                self.assertTrue(all(entry["feed"] == "Quiet" for entry in entries))
                ids.extend(entry["id"] for entry in entries)
                view = packet[3:11].decode()
                offset += count
                total = int.from_bytes(packet[11:15], "big")
                if offset == total:
                    break
            self.assertEqual(len(set(ids)), expected)

    def test_concurrent_clients_keep_independent_feed_and_age_views(self):
        import urllib.request
        from concurrent.futures import ThreadPoolExecutor
        now = datetime.now(timezone.utc)
        stories = [_make_article(f"{i:08x}", "A" if i < 60 else "B", str(i), "",
                                 now-timedelta(minutes=i+1)) for i in range(120)]
        stories.append(_make_article("older001", "A", "Yesterday", "", now-timedelta(hours=30)))
        self._start_server(stories, retention_hours=168)
        choices = (("A", 24, 60), ("A", 48, 61), ("B", 24, 60), ("B", 48, 60))
        def fetch(choice):
            feed, hours, _ = choice
            path = f"/page?feed={feed.encode().hex()}&hours={hours}"
            with urllib.request.urlopen(self.base_url+path) as response:
                return response.read()
        with ThreadPoolExecutor(max_workers=4) as executor:
            packets = list(executor.map(fetch, choices))
        self.assertEqual(len({packet[3:11] for packet in packets}), 4)
        for (feed, hours, expected), packet in zip(choices, packets):
            self.assertEqual(int.from_bytes(packet[11:15], "big"), expected)
            view = packet[3:11].decode()
            with urllib.request.urlopen(self.base_url+f"/page?view={view}&offset=50") as response:
                tail = response.read()
            rc, output, error = self._run_harness(tail)
            self.assertEqual(rc, 0, error)
            count, entries = self._parse_harness_output(output)
            self.assertEqual(count, expected-50)
            self.assertTrue(all(entry["feed"] == feed for entry in entries))

    def test_paged_empty_view_and_invalid_cursors(self):
        import urllib.request, urllib.error
        self._start_server([])
        with urllib.request.urlopen(self.base_url+"/page") as response:
            packet = response.read()
        rc, output, error = self._run_harness(packet)
        self.assertEqual(rc, 0, error)
        self.assertEqual(self._parse_harness_output(output)[0], 0)
        view = packet[3:11].decode()
        for path, status in (("/page?offset=-1", 400), ("/page?offset=garbage", 400),
                             ("/page?view=missing", 409), (f"/page?view={view}&offset=1", 400),
                             ("/page?hours=0", 400)):
            with self.assertRaises(urllib.error.HTTPError) as error:
                urllib.request.urlopen(self.base_url+path)
            self.assertEqual(error.exception.code, status)
            error.exception.close()
        # Expired cursors fail explicitly, never fall back to a different view.
        self.httpd.article_views[view] = (0, [])
        with self.assertRaises(urllib.error.HTTPError) as error:
            urllib.request.urlopen(self.base_url+f"/dates?view={view}")
        self.assertEqual(error.exception.code, 409)
        error.exception.close()

    def test_invalid_age_windows_are_rejected(self):
        import urllib.request
        from urllib.error import HTTPError
        self._start_server([], retention_hours=168)
        for hours in ("0", "-1", "169", "nan", "inf", "bad"):
            for endpoint in ("sync", "dates", "status"):
                with self.assertRaises(HTTPError) as caught:
                    urllib.request.urlopen(f"{self.base_url}/{endpoint}?hours={hours}", timeout=10)
                self.assertEqual(caught.exception.code, 400)
                caught.exception.close()


if __name__ == "__main__":
    unittest.main()
