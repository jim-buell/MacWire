"""Refresh joins one worker, bypasses the scheduled interval and publishes before completion."""
import io
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
import urllib.request
from datetime import datetime, timezone
from unittest.mock import patch, MagicMock
sys.path.insert(0, str(Path(__file__).resolve().parent))
from refresh import FeedRefreshManager
from fetcher import FeedFetcher
from models import Feed, Article
from database import RollingDatabase
from server import RSSHandler, RSSServer
from protocol import parse_index_response


def finished(manager, token):
    deadline = time.monotonic() + 3
    while manager.status(token)[0] and time.monotonic() < deadline:
        time.sleep(0.005)
    if manager.status(token)[0]:
        raise AssertionError('Refresh did not complete')


class RefreshWorkerTests(unittest.TestCase):
    def test_manual_requests_join_worker_and_override_hourly_schedule(self):
        entered, release = threading.Event(), threading.Event()
        calls = []
        def fetch(force):
            calls.append(force)
            if force:
                entered.set()
                release.wait(3)
            return 0
        manager = FeedRefreshManager(fetch, interval=3600, cooldown=0)
        manager.start()
        try:
            finished(manager, manager.current)
            token = manager.request()
            self.assertTrue(entered.wait(3))
            self.assertEqual(manager.request(), token)
            self.assertTrue(manager.status(token)[0])
            release.set()
            finished(manager, token)
            self.assertEqual(calls, [False, True])
        finally:
            release.set()
            manager.stop()

    def test_cooldown_reuses_completed_job_and_reports_partial_failure(self):
        calls = []
        manager = FeedRefreshManager(lambda force: calls.append(force) or 2)
        manager.start()
        try:
            token = manager.current
            finished(manager, token)
            self.assertEqual(manager.request(), token)
            self.assertEqual(manager.status(token), (False, 2))
            self.assertEqual(calls, [False])
        finally:
            manager.stop()

    def test_worker_exception_finishes_job_so_client_can_use_cache(self):
        def fail(force):
            raise RuntimeError('test failure')
        manager = FeedRefreshManager(fail)
        manager.start()
        try:
            token = manager.current
            finished(manager, token)
            self.assertEqual(manager.status(token), (False, 1))
        finally:
            manager.stop()

    def test_forced_fetch_keeps_http_validators_and_bypasses_recent_attempt(self):
        fetcher = FeedFetcher(fetch_interval=3600)
        feed = Feed(url='https://example.com/rss', name='Example')
        fetcher._last_fetch[feed.url] = time.time()
        fetcher._validators[feed.url] = {'etag': '"v1"'}
        response = MagicMock()
        response.headers.items.return_value = [('Content-Type', 'application/rss+xml'), ('ETag', '"v2"')]
        response.geturl.return_value = feed.url
        response.read.return_value = b'<rss version="2.0"><channel><title>Example</title><description>Test</description><link>https://example.com/</link></channel></rss>'
        with patch('urllib.request.urlopen') as opened:
            opened.return_value.__enter__.return_value = response
            fetcher.fetch_feeds([feed])
            opened.assert_not_called()
            fetcher.fetch_feeds([feed], force=True)
            opened.assert_called_once()
            self.assertEqual(opened.call_args.args[0].get_header('If-none-match'), '"v1"')
        self.assertEqual(fetcher._validators[feed.url], {'etag': '"v2"'})


class RefreshHTTPTests(unittest.TestCase):
    def test_completed_job_exposes_new_articles_and_same_token_for_joiners(self):
        with tempfile.TemporaryDirectory() as directory:
            database = RollingDatabase(directory)
            entered, release = threading.Event(), threading.Event()
            def fetch(force):
                if force:
                    entered.set()
                    release.wait(3)
                    database.add_articles([Article(id='12345678', title='New article', published=datetime.now(timezone.utc))])
                return 0
            manager = FeedRefreshManager(fetch, cooldown=0)
            manager.start()
            finished(manager, manager.current)
            class Handler(RSSHandler):
                pass
            Handler.database = database
            Handler.default_window_hours = 24
            listener = RSSServer(('127.0.0.1', 0), Handler)
            listener.refresh_manager = manager
            thread = threading.Thread(target=listener.serve_forever, daemon=True)
            thread.start()
            base = f'http://127.0.0.1:{listener.server_address[1]}'
            def read(path):
                with urllib.request.urlopen(base + path, timeout=3) as response:
                    return response.read()
            try:
                packet = read('/refresh')
                self.assertEqual(packet[:3], b'\x0b\x00\x0b')
                token = packet[3:11].decode()
                self.assertEqual(packet[11], 1)
                self.assertTrue(entered.wait(3))
                self.assertEqual(read('/refresh')[3:11], token.encode())
                self.assertEqual(read('/refresh?job=' + token)[11], 1)
                release.set()
                finished(manager, token)
                self.assertEqual(read('/refresh?job=' + token)[11:], b'\x00\x00\x00')
                self.assertEqual(parse_index_response(read('/sync')[3:])[0].title, 'New article')
            finally:
                release.set()
                listener.shutdown()
                listener.server_close()
                thread.join(timeout=3)
                manager.stop()
