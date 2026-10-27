"""Inline photos retain positions across HTML rendering, cache and MacRoman wire encoding."""
import sys
from pathlib import Path
import tempfile
import unittest
from datetime import datetime, timezone
sys.path.insert(0, str(Path(__file__).resolve().parent))
from html_text import html_to_text_with_images
from content_fetcher import _BodyExtractor
from models import Article, ArticleContent, Feed
from fetcher import FeedFetcher
from database import RollingDatabase
from protocol import encode_article, parse_article_response, _client_text
import feedparser


class ImagePositionTests(unittest.TestCase):
    def test_images_keep_paragraph_order_even_if_attachment_order_differs(self):
        images = [{'url': 'https://example.com/b.jpg'}, {'url': 'https://example.com/a.jpg'}]
        markup = '<p>Before.</p><img src="a.jpg"><p>Between.</p><img src="b.jpg"><p>After.</p>'
        body = html_to_text_with_images(markup, images, 'https://example.com/story')
        self.assertEqual(body, 'Before.\n\nBetween.\n\nAfter.')
        self.assertEqual(images[1]['offset'], body.index('Between.'))
        self.assertEqual(images[0]['offset'], body.index('After.'))

    def test_first_and_last_image_positions_and_duplicates(self):
        images = [{'url': 'https://example.com/a.jpg'}, {'url': 'https://example.com/b.jpg'}]
        body = html_to_text_with_images('<img src="a.jpg"><p>Text.</p><img src="a.jpg"><img src="b.jpg">', images, 'https://example.com/')
        self.assertEqual(body, 'Text.')
        self.assertEqual([image['offset'] for image in images], [0, len(body)])

    def test_original_article_collects_body_images_but_skips_navigation_and_tracking(self):
        parser = _BodyExtractor('https://example.com/story')
        parser.feed('<html><body><nav><img src="logo.jpg"></nav><p>Before.</p><img src="photo.jpg"><img src="tracker.gif" width="1"><p>After.</p></body></html>')
        body = parser.get_text()
        self.assertEqual(body, 'Before.\n\nAfter.')
        self.assertEqual(len(parser.images), 1)
        self.assertEqual(parser.images[0]['url'], 'https://example.com/photo.jpg')
        self.assertEqual(parser.images[0]['offset'], body.index('After.'))

    def test_wire_offsets_follow_macroman_transliteration_not_unicode_positions(self):
        body = 'Café → résumé\n\nAfter.'
        content = ArticleContent(id='12345678', full_text=body, images=[{'url': 'https://example.com/a.jpg', 'offset': body.index('After.')}, {'url': 'https://example.com/attachment.jpg'}])
        decoded = parse_article_response(encode_article(content)[3:])
        expected = len(_client_text(body[:body.index('After.')], 65535))
        self.assertEqual(decoded.images[0]['offset'], expected)
        self.assertEqual(decoded.full_text[expected:], 'After.')
        self.assertNotIn('offset', decoded.images[1])

    def test_image_beyond_truncated_body_becomes_unpositioned_attachment(self):
        content = ArticleContent(id='12345678', full_text='x' * 11000, images=[{'url': 'https://example.com/a.jpg', 'offset': 10500}])
        decoded = parse_article_response(encode_article(content)[3:])
        self.assertNotIn('offset', decoded.images[0])

    def test_feed_body_and_cache_preserve_image_anchors(self):
        entry = feedparser.FeedParserDict(id='sample', title='Test', link='https://example.com/story', content=[feedparser.FeedParserDict(value='<p>Before.</p><img src="a.jpg"><p>After.</p>')])
        article = FeedFetcher()._parse_entry(entry, Feed(url='https://example.com/rss', name='Test'))
        article.published = datetime.now(timezone.utc)
        self.assertEqual(article.images[0]['offset'], article.full_text.index('After.'))
        with tempfile.TemporaryDirectory() as directory:
            database = RollingDatabase(directory)
            database.add_articles([article])
            reloaded = RollingDatabase(directory).get_article(article.id)
            self.assertEqual(reloaded.images, article.images)
            from dataclasses import replace
            database.add_articles([replace(article, full_text='', images=[])])
            self.assertEqual(database.get_article(article.id).images, article.images)
            database.set_article_content(article.id, 'Replacement text')
            self.assertNotIn('offset', database.get_article(article.id).images[0])
