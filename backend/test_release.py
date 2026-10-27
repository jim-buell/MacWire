"""Release regressions: storage recovery, clean packaging, CLI errors and bounds."""
import io
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from config import FeedConfig
from database import RollingDatabase
from network import read_limited
from storage import write_json
from server import RSSHandler, RSSServer

ROOT = Path(__file__).resolve().parents[1]


class ReleaseTests(unittest.TestCase):
    def test_classic_bundle_preserves_downloads_and_carries_notices(self):
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = bytearray(256)
            binary[65:69] = b'APPL'
            binary[87:91] = (4).to_bytes(4, 'big')
            binary[128:132] = b'fork'
            disk = bytearray(819200)
            disk[1024:1026] = b'BD'
            app, image, output = root / 'app.bin', root / 'app.dsk', root / 'classic.zip'
            app.write_bytes(binary)
            image.write_bytes(disk)
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/package_release.py'),
                                     '--classic-only', '--client-bin', str(app),
                                     '--client-disk', str(image), '--output', str(output)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(archive.read('MacWire/downloads/MacWire.bin'), binary)
                self.assertEqual(archive.read('MacWire/downloads/MacWire.dsk'), disk)
                for notice in ('LICENSE', 'THIRD_PARTY.md', 'SECURITY.md',
                               'client/licenses/Multiversal-MIT.txt',
                               'client/licenses/newlib-4.6.0.txt',
                               'client/licenses/GPL-3.0.txt',
                               'client/licenses/GCC-Runtime-Exception.txt'):
                    self.assertEqual(archive.read('MacWire/' + notice), (ROOT / notice).read_bytes())
                self.assertNotIn('MacWire/backend/main.py', archive.namelist())
                self.assertIn('MacWire/README.txt', archive.namelist())
            self.assertEqual(output.with_suffix('.zip.sha256').read_text(),
                             hashlib.sha256(output.read_bytes()).hexdigest() + '  classic.zip\n')

    def test_classic_bundle_rejects_missing_inputs_and_bad_disk(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            command = [sys.executable, str(ROOT / 'scripts/package_release.py'),
                       '--classic-only', '--output', str(root / 'bad.zip')]
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            disk = root / 'bad.dsk'
            disk.write_bytes(b'not an HFS disk')
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/package_release.py'),
                                     '--client-disk', str(disk), '--output', str(root / 'bad.zip')],
                                    capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((root / 'bad.zip').exists())

    def test_atomic_write_failure_preserves_original_and_cleans_temporary(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'feeds.json'
            path.write_text('{"feeds": []}')
            with patch('storage.os.replace', side_effect=OSError('disk error')):
                with self.assertRaises(OSError):
                    write_json(path, {'feeds': [{'url': 'https://example.com/rss'}]})
            self.assertEqual(json.loads(path.read_text()), {'feeds': []})
            self.assertEqual(list(Path(directory).iterdir()), [path])

    def test_corrupt_cache_is_preserved_and_rebuilt(self):
        for damaged in ('{"articles":', '{"articles": "wrong type"}'):
            with self.subTest(damaged=damaged), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'articles.json'
                path.write_text(damaged)
                database = RollingDatabase(directory)
                self.assertEqual(database.get_index(), [])
                backups = list(Path(directory).glob('articles.corrupt-*.json'))
                self.assertEqual(len(backups), 1)
                self.assertEqual(backups[0].read_text(), damaged)
                database.add_articles([])
                self.assertEqual(json.loads(path.read_text()), {'articles': []})

    def test_cli_initializes_nested_configuration_from_another_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'nested' / 'feeds.json'
            command = [sys.executable, str(ROOT / 'backend/main.py'), '--config', str(path), '--list-feeds']
            result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Colossal', result.stdout)
            self.assertTrue(path.exists())

    def test_cli_reports_invalid_configuration_without_overwriting_it(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'feeds.json'
            path.write_text('{bad json')
            result = subprocess.run([sys.executable, str(ROOT / 'backend/main.py'),
                                     '--config', str(path), '--list-feeds'],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 1)
            self.assertIn('Invalid configuration', result.stderr)
            self.assertNotIn('Traceback', result.stderr)
            self.assertEqual(path.read_text(), '{bad json')

    def test_default_locations_are_absolute_and_do_not_follow_working_directory(self):
        from main import default_home
        with patch('main.Path.home', return_value=Path('/example/user')), patch('main.sys.platform', 'darwin'):
            self.assertEqual(default_home(), Path('/example/user/Library/Application Support/MacWire Helper'))

    def test_helper_reuses_legacy_data_unless_new_location_exists(self):
        from main import default_home
        for platform in ('darwin', 'linux'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                home = Path(directory)
                if platform == 'darwin':
                    base = home / 'Library/Application Support'
                    current, legacy = base / 'MacWire Helper', base / 'TickerTape'
                else:
                    base = home / 'data'
                    current, legacy = base / 'macwire-helper', base / 'tickertape'
                with patch('main.Path.home', return_value=home), patch('main.sys.platform', platform), \
                     patch.dict('main.os.environ', {'XDG_DATA_HOME': str(base)}):
                    self.assertEqual(default_home(), current)
                    legacy.mkdir(parents=True)
                    self.assertEqual(default_home(), legacy)
                    current.mkdir()
                    self.assertEqual(default_home(), current)

    def test_non_http_feed_is_rejected_without_creating_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'feeds.json'
            with self.assertRaises(ValueError):
                FeedConfig(str(path)).add_feed('file:///etc/passwd')
            self.assertFalse(path.exists())

    def test_download_rejects_oversize_before_parser_receives_it(self):
        self.assertEqual(read_limited(io.BytesIO(b'1234'), 4), b'1234')
        with self.assertRaises(ValueError):
            read_limited(io.BytesIO(b'12345'), 4)

    def test_large_image_is_rejected_before_pixel_conversion(self):
        from image_processor import ImageProcessor
        from unittest.mock import MagicMock
        image = MagicMock(width=5000, height=5000)
        with patch('PIL.Image.open', return_value=image), patch('PIL.ImageOps.exif_transpose') as transpose:
            with self.assertRaises(ValueError):
                ImageProcessor().process_image(b'fake')
        transpose.assert_not_called()

    def test_feed_relative_links_resolve_against_final_download_url(self):
        from fetcher import FeedFetcher
        from models import Feed
        from unittest.mock import MagicMock
        upstream = MagicMock()
        upstream.read.return_value = b'<rss version="2.0"><channel><title>Test</title><link>https://example.com</link><description>Test</description><item><title>Story</title><link>story.html</link></item></channel></rss>'
        upstream.headers.items.return_value = [('Content-Type', 'application/rss+xml')]
        upstream.geturl.return_value = 'https://example.com/news/feed.xml'
        with patch('urllib.request.urlopen') as opened:
            opened.return_value.__enter__.return_value = upstream
            articles = FeedFetcher(fetch_interval=0).fetch_feeds([Feed(url='https://example.com/redirect', name='Test')])
        self.assertEqual(articles[0].original_url, 'https://example.com/news/story.html')

    def test_http_304_keeps_feed_validators_and_skips_parsing(self):
        from fetcher import FeedFetcher
        from models import Feed
        feed = Feed(url='https://example.com/rss', name='Test')
        fetcher = FeedFetcher(fetch_interval=0)
        fetcher._validators[feed.url] = {'etag': '"cached"'}
        unchanged = urllib.error.HTTPError(feed.url, 304, 'Not modified', {}, None)
        with patch('urllib.request.urlopen', side_effect=unchanged) as opened, patch('feedparser.parse') as parse:
            self.assertEqual(fetcher.fetch_feeds([feed]), [])
        self.assertEqual(opened.call_args.args[0].get_header('If-none-match'), '"cached"')
        self.assertEqual(fetcher._validators[feed.url], {'etag': '"cached"'})
        parse.assert_not_called()

    def test_release_archive_excludes_personal_files_and_includes_example(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'release.zip'
            subprocess.run([sys.executable, str(ROOT / 'scripts/package_release.py'), '--output', str(archive)], check=True, capture_output=True)
            with zipfile.ZipFile(archive) as file:
                names = file.namelist()
                self.assertIn('MacWire/backend/feeds.example.json', names)
                self.assertIn('MacWire/backend/main.py', names)
                self.assertIn('MacWire/client/CMakeLists.txt', names)
                self.assertIn('MacWire/BUILDING.md', names)
                self.assertIn('MacWire/LICENSE', names)
                self.assertIn('MacWire/client/licenses/Multiversal-MIT.txt', names)
                self.assertIn('MacWire/client/licenses/newlib-4.6.0.txt', names)
                self.assertIn('MacWire/backend/licenses/Pillow-MIT-CMU.txt', names)
                self.assertIn('MacWire/backend/image_algorithms.py', names)
                self.assertEqual(
                    {name for name in names if name.startswith('MacWire/scripts/')},
                    {'MacWire/scripts/package_release.py', 'MacWire/scripts/release_manifest.txt'},
                )
                self.assertNotIn('MacWire/backend/test_image_review.py', names)
                self.assertNotIn('MacWire/backend/test_image_algorithms.py', names)
                self.assertEqual(
                    {name for name in names if name.lower().endswith('.md')},
                    {'MacWire/' + name for name in (
                        'README.md', 'BUILDING.md', 'THIRD_PARTY.md', 'SECURITY.md',
                        'backend/LAUNCH_AGENT.md', 'backend/IMAGE_ALGORITHMS.md',
                        'client/SETUP.md',
                    )},
                )
                for internal in ('DISTRIBUTION.md', 'backend/feeds.json',
                                 'backend/ENCODING_OPTIONS.md', 'backend/DISTRIBUTION_FEEDS.md',
                                 'backend/IMAGE_PROCESSING.md', 'client/LaunchAPPL.cfg',
                                 'client/HARDWARE_TESTING.md', 'client/ICON_CREATION.md',
                                 'client/Makefile', 'client/BUILD_PROGRESS.md',
                                 'client/PROGRESSIVE_IMAGES.md', 'client/tests/capture_real_packets.py'):
                    self.assertNotIn('MacWire/' + internal, names)
                self.assertFalse(any('/rss_data/' in name or '/.venv/' in name or '/build/' in name for name in names))
            again = subprocess.run([sys.executable, str(ROOT / 'scripts/package_release.py'), '--output', str(archive)], capture_output=True)
            self.assertNotEqual(again.returncode, 0)

    def test_release_files_exclude_symlinked_roots_and_files(self):
        spec = importlib.util.spec_from_file_location('package_release', ROOT / 'scripts/package_release.py')
        package = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(package)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            outside = root / 'private'
            outside.mkdir()
            (outside / 'secret.py').write_text('PRIVATE = True\n')
            (root / 'backend').symlink_to(outside, target_is_directory=True)
            (root / 'README.md').symlink_to(outside / 'secret.py')
            client = root / 'client'
            client.mkdir()
            (client / 'linked.py').symlink_to(outside / 'secret.py')
            (client / 'private').symlink_to(outside, target_is_directory=True)
            (client / 'public.c').write_text('/* public */\n')
            (client / 'NEW_DECISIONS.md').write_text('# Internal decisions\n')
            scripts = root / 'scripts'
            scripts.mkdir()
            (scripts / 'future_review.py').write_text('# Internal review tool\n')
            (client / 'private.cfg').write_text('local configuration')
            (scripts / 'release_manifest.txt').write_text(
                'README.md\nbackend/secret.py\nclient/linked.py\n'
                'client/private/secret.py\nclient/public.c\n')
            with patch.object(package, 'ROOT', root):
                self.assertEqual(list(package.release_files()), [client / 'public.c'])

    def test_release_manifest_rejects_missing_or_invalid_entries(self):
        spec = importlib.util.spec_from_file_location('package_release', ROOT / 'scripts/package_release.py')
        package = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(package)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'scripts').mkdir()
            (root / 'public.py').write_text('# public source\n')
            manifest = root / 'scripts/release_manifest.txt'
            for content in ('../secret.py', '/secret.py', 'missing.py',
                            'public.py\npublic.py', './public.py'):
                with self.subTest(content=content), patch.object(package, 'ROOT', root):
                    manifest.write_text(content)
                    with self.assertRaises(ValueError):
                        list(package.release_files())


class RequestBoundsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = RSSServer(('127.0.0.1', 0), RSSHandler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f'http://127.0.0.1:{cls.server.server_address[1]}'

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=3)

    def assert_bad_request(self, path, data=None):
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(self.base + path, data=data, timeout=3)
        self.assertEqual(caught.exception.code, 400)
        caught.exception.close()

    def test_large_post_body_is_rejected(self):
        self.assert_bad_request('/prefetch/test?count=1', data=b'x' * 4097)

    def test_prefetch_count_is_validated_before_upstream_work(self):
        for count in ('-1', '0', '11', 'invalid'):
            with self.subTest(count=count):
                self.assert_bad_request('/prefetch/test?count=' + count, data=b'')

    def test_invalid_image_path_returns_error(self):
        self.assert_bad_request('/image/test')
        self.assert_bad_request('/image/test/invalid')

    def test_saturated_server_closes_extra_connections(self):
        acquired = []
        try:
            deadline = time.monotonic() + 3
            while True:
                while self.server._workers.acquire(blocking=False):
                    acquired.append(True)
                if len(acquired) == 16:
                    break
                for _ in acquired:
                    self.server._workers.release()
                acquired.clear()
                if time.monotonic() >= deadline:
                    self.fail("Prior request workers did not finish")
                time.sleep(0.005)
            with socket.create_connection(self.server.server_address, timeout=3) as stream:
                # Wait for server accept without sending a request, avoiding reset races.
                self.assertEqual(stream.recv(1), b'')
        finally:
            for _ in acquired:
                self.server._workers.release()
