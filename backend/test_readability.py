"""Readable feed structure and text-only external URL annotations."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
from html_text import html_to_text, html_to_text_with_images
from fetcher import FeedFetcher
from models import Feed, ArticleContent
from protocol import encode_article, parse_article_response
import feedparser

class ReadabilityTests(unittest.TestCase):
    def test_links_keep_destination_without_repeating_bare_urls(self):
        self.assertEqual(html_to_text('<p>This is <a href="https://www.link.com/story?a=1&amp;b=2">a link</a>.</p>'), 'This is a link [www.link.com/story?a=1&b=2].')
        self.assertEqual(html_to_text('<a href="https://example.com/">https://example.com/</a>'), 'https://example.com/')
        self.assertEqual(html_to_text('<a href="/story">Story</a>', 'https://example.com/feed'), 'Story [example.com/story]')
    def test_non_http_and_image_only_anchors_do_not_add_urls(self):
        self.assertEqual(html_to_text('<a href="javascript:alert(1)">Text</a> <a href="#section">Section</a>'), 'Text Section')
        self.assertEqual(html_to_text('<a href="https://example.com/a.jpg"><img src="a.jpg"></a>'), '')
        self.assertEqual(html_to_text('<a id="section">Anchor</a>', 'https://example.com/article'), 'Anchor')
    def test_nested_lists_keep_numbering_and_paragraphs(self):
        self.assertEqual(html_to_text('<ol start="3"><li><p>First</p><ul><li>Nested</li></ul></li><li>Second</li></ol>'), '3. First\n\n  • Nested\n\n4. Second')
    def test_table_cells_are_separated(self):
        self.assertEqual(html_to_text('<table><tr><th>Pros</th><th>Cons</th></tr><tr><td>Fast</td><td>Cost</td></tr></table>'), 'Pros | Cons\n\nFast | Cost')
    def test_section_headings_and_rules_are_visible(self):
        self.assertEqual(html_to_text('<h2>Section</h2><p>Text.</p><hr><h3>Subsection</h3>'), '## Section\n\nText.\n\n---\n\n### Subsection')
    def test_omitted_photos_are_labeled_before_their_captions(self):
        text=html_to_text_with_images('<img src="a.jpg"><figcaption>Photo credit.</figcaption>',[], 'https://example.com/')
        self.assertEqual(text,'[Photo omitted: image limit]\n\nCaption: Photo credit.')
    def test_already_quoted_blocks_do_not_gain_duplicate_quotes(self):
        self.assertEqual(html_to_text('<blockquote><p>“Already quoted.”</p></blockquote>'), '“Already quoted.”')
        self.assertEqual(html_to_text('<blockquote>Outer.<blockquote>Inner.</blockquote>Again.</blockquote>'), '“Outer.\n\n‘Inner.’\n\nAgain.”')
    def test_pullquotes_drop_only_matching_text_and_label_unique_text(self):
        markup='<p>Repeated sentence.</p><figure class="wp-block-pullquote"><blockquote>Repeated sentence.</blockquote></figure><figure class="wp-block-pullquote"><blockquote>Unique sentence.</blockquote></figure>'
        text=html_to_text(markup)
        self.assertEqual(text.count('Repeated sentence.'),1)
        self.assertIn('Pull quote:\n\n“Unique sentence.”',text)
    def test_captions_and_missing_media_are_explained(self):
        self.assertEqual(html_to_text('<figure><img src="a.jpg"><figcaption><p>Photo credit.</p></figcaption></figure>'), 'Caption: Photo credit.')
        self.assertEqual(html_to_text('<figcaption></figcaption>'), '')
        self.assertEqual(html_to_text('<figure class="kg-card kg-embed-card"></figure>'), '[Embedded media not included in feed]')
        self.assertEqual(html_to_text('<iframe title="A film" src="https://example.com/video"></iframe>'), '[Embedded media: A film]')
    def test_footnotes_keep_reference_definition_and_return_marker(self):
        markup='<p>Word<sup id="fnref-a"><a href="#fn-a">1</a></sup>.</p><ol><li id="fn-a"><p>Footnote <a href="#fnref-a">↩</a>.</p></li></ol>'
        self.assertEqual(html_to_text(markup), 'Word[1].\n\n[1]: Footnote [Back to 1].')
        self.assertNotIn('example.com',html_to_text(markup,'https://example.com/article'))
    def test_footnotes_follow_reference_order_not_original_numbers(self):
        markup='<p><a href="#fn-b">2</a> <a href="#fn-a">1</a> <a href="#fn-b">2</a></p><ol><li id="fn-a">A</li><li id="fn-b">B</li></ol>'
        self.assertEqual(html_to_text(markup),'[1] [2] [1]\n\n[2]: A [Back to 2]\n\n[1]: B [Back to 1]')
    def test_image_offsets_follow_expanded_link_annotations(self):
        images=[{'url':'https://example.com/a.jpg'}]
        text=html_to_text_with_images('<p><a href="/story">Story</a>.</p><img src="a.jpg"><p>After.</p>',images,'https://example.com/')
        self.assertEqual(text,'Story [example.com/story].\n\nAfter.')
        self.assertEqual(images[0]['offset'],text.index('After.'))
    def test_full_text_is_kept_on_server_and_wire_clipping_is_explicit(self):
        entry=feedparser.FeedParserDict(id='test',title='Test',link='https://example.com',content=[feedparser.FeedParserDict(value='<p>'+'text '*5000+'</p>')])
        article=FeedFetcher()._parse_entry(entry,Feed(url='https://example.com/rss',name='Test'))
        self.assertGreater(len(article.full_text),20000)
        decoded=parse_article_response(encode_article(ArticleContent(id=article.id,full_text=article.full_text))[3:])
        self.assertEqual(len(decoded.full_text),9999)
        self.assertTrue(decoded.full_text.endswith("[Article continues beyond this reader's text limit.]"))
    def test_image_anchor_in_clipped_region_is_not_placed_on_notice(self):
        content=ArticleContent(id='12345678',full_text='x'*11000,images=[{'url':'https://example.com/a.jpg','offset':9980}])
        decoded=parse_article_response(encode_article(content)[3:])
        self.assertNotIn('offset',decoded.images[0])
