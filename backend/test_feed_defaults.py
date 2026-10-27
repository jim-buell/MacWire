"""Public distribution defaults stay separate from private subscriptions."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parent))
from config import FeedConfig, DISTRIBUTION_CONFIG_PATH

ROOT = Path(__file__).resolve().parents[1]


class FeedDefaultsTests(unittest.TestCase):
    def test_distribution_contains_only_dedicated_public_starter_feeds(self):
        data = json.loads(Path(DISTRIBUTION_CONFIG_PATH).read_text())
        self.assertEqual(data['defaults_revision'], 2)
        self.assertEqual(len(data['feeds']), 6)
        self.assertEqual(len({feed['url'] for feed in data['feeds']}), 6)
        for feed in data['feeds']:
            url = urlsplit(feed['url'])
            self.assertEqual(url.scheme, 'https')
            self.assertIsNone(url.username)
            self.assertIsNone(url.password)
            self.assertEqual(url.query, '')
            self.assertEqual(url.fragment, '')
            self.assertNotIn('theverge', url.hostname)
            self.assertTrue(feed['enabled'])

    def test_new_configuration_copies_public_revision_and_preserves_it_on_edit(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'feeds.json'
            with contextlib.redirect_stdout(io.StringIO()):
                config = FeedConfig(str(path))
                config.load()
            expected = json.loads(Path(DISTRIBUTION_CONFIG_PATH).read_text())
            self.assertEqual(json.loads(path.read_text()), expected)
            config.add_feed('https://example.com/personal?token=secret', 'Private')
            saved = json.loads(path.read_text())
            self.assertEqual(saved['defaults_revision'], 2)
            self.assertEqual(len(saved['feeds']), 7)
            self.assertEqual(json.loads(Path(DISTRIBUTION_CONFIG_PATH).read_text()), expected)

    def test_compare_is_read_only_and_omits_private_urls_and_names(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'personal.json'
            path.write_text(json.dumps({'feeds': [
                {'url': 'https://www.thisiscolossal.com/feed/', 'name': 'My art', 'enabled': False,
                 'image_preferences': {'max_images': 2}},
                {'url': 'https://secret.example/private?token=NEVER_SHARE', 'name': 'SECRET_NAME', 'enabled': True}]}))
            before = path.read_bytes()
            report = FeedConfig(str(path)).compare_defaults()
            self.assertEqual(report['personal_only_count'], 1)
            self.assertIsNone(report['seeded_revision'])
            self.assertEqual(report['feeds'][0], {'name': 'Colossal', 'status': 'disabled', 'customized': True})
            self.assertEqual(sum(row['status'] == 'missing' for row in report['feeds']), 5)
            text = json.dumps(report)
            for value in ('NEVER_SHARE', 'SECRET_NAME', 'secret.example', 'My art'):
                self.assertNotIn(value, text)
            self.assertEqual(path.read_bytes(), before)
            result = subprocess.run([sys.executable, str(ROOT / 'backend/main.py'),
                '--config', str(path), '--compare-default-feeds'], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('revision 2', result.stdout)
            self.assertIn('Additional personal feeds: 1', result.stdout)
            self.assertNotIn('NEVER_SHARE', result.stdout + result.stderr)
            self.assertNotIn('SECRET_NAME', result.stdout + result.stderr)
            self.assertEqual(path.read_bytes(), before)

    def test_compare_does_not_create_a_missing_personal_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'new' / 'feeds.json'
            report = FeedConfig(str(path)).compare_defaults()
            self.assertFalse(report['config_exists'])
            self.assertTrue(all(feed['status'] == 'missing' for feed in report['feeds']))
            self.assertFalse(path.parent.exists())

    def test_existing_legacy_config_is_not_seeded_or_merged(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'feeds.json'
            original = {'feeds': [{'url': 'https://example.com/feed', 'name': 'Personal', 'enabled': True}]}
            path.write_text(json.dumps(original))
            config = FeedConfig(str(path))
            self.assertEqual(len(config.load()), 1)
            self.assertIsNone(config.defaults_revision)
            config.add_feed('https://example.org/feed', 'Another')
            saved = json.loads(path.read_text())
            self.assertNotIn('defaults_revision', saved)
            self.assertEqual(len(saved['feeds']), 2)

    def test_invalid_revision_stops_without_rewriting_config(self):
        for revision in (0, -1, True, '1'):
            with self.subTest(revision=revision), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'feeds.json'
                original = json.dumps({'defaults_revision': revision, 'feeds': []})
                path.write_text(original)
                with self.assertRaisesRegex(ValueError, 'defaults_revision'):
                    FeedConfig(str(path)).load()
                self.assertEqual(path.read_text(), original)


if __name__ == '__main__':
    unittest.main()
