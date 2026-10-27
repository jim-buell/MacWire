"""Quotes retain HTML nesting, quoted speech, apostrophes, and wire offsets."""
import unittest
from html_text import html_to_text, html_to_text_with_images
from models import ArticleContent
from protocol import encode_article, parse_article_response


class QuoteTests(unittest.TestCase):
    def test_literal_speech_inside_block_uses_single_quotes(self):
        for speech in ('"I\'m going."', '“I’m going.”'):
            text = html_to_text('<blockquote>Bill said, '+speech+'</blockquote>')
            expected = '“Bill said, ‘I’m going.’”' if 'I’m' in speech else '“Bill said, ‘I\'m going.’”'
            self.assertEqual(text, expected)

    def test_already_quoted_block_does_not_add_a_second_pair(self):
        for source in ('"Already quoted."', '“Already quoted.”', '‘Already quoted.’'):
            expected = '‘Already quoted.’' if source.startswith('‘') else '“Already quoted.”'
            self.assertEqual(html_to_text('<blockquote>'+source+'</blockquote>'),expected)

    def test_separate_quotes_are_not_mistaken_for_one_outer_pair(self):
        self.assertEqual(html_to_text('<blockquote>"First" and "second".</blockquote>'),
                         '“‘First’ and ‘second’.”')

    def test_inline_q_and_literal_quoted_speech_alternate(self):
        self.assertEqual(html_to_text('<p>Bill said <q>I’m going.</q> today.</p>'),
                         'Bill said “I’m going.” today.')
        self.assertEqual(html_to_text('<q>Outer <q>inner <q>deep</q></q>.</q>'),
                         '“Outer ‘inner “deep”’.”')
        self.assertEqual(html_to_text('<q>He said "yes".</q>'), '“He said ‘yes’.”')

    def test_nested_literal_quotes_without_html_quote_tags(self):
        self.assertEqual(html_to_text('<p>"Bill said, "I\'m going.""</p>'),
                         '“Bill said, ‘I\'m going.’”')

    def test_apostrophes_inch_marks_unmatched_quotes_and_code_are_preserved(self):
        self.assertEqual(html_to_text('<blockquote>Bill’s 5" screen. <code>print("a")</code></blockquote>'),
                         '“Bill’s 5" screen. print("a")”')
        self.assertEqual(html_to_text('<pre>"one"\n  "two"</pre>'), '"one"\n  "two"')
        self.assertEqual(html_to_text('<p>An unmatched "opening.</p>'), 'An unmatched "opening.')
        self.assertEqual(html_to_text('<p>‘A single quote’ and "2026".</p>'),
                         '‘A single quote’ and “2026”.')

    def test_normalized_quotes_keep_links_emphasis_and_image_offsets(self):
        images=[{'url':'https://example.com/photo'}]; links=[]; styles=[]
        text=html_to_text_with_images('<blockquote>Bill said <a href="/said"><em>"I’m going."</em></a></blockquote>'
                                     '<img src="/photo"><p>After.</p>',images,'https://example.com',links,styles)
        decoded=parse_article_response(encode_article(ArticleContent(id='12345678',full_text=text,
                                      images=images,links=links,styles=styles))[3:])
        self.assertEqual(decoded.full_text[:len('“Bill said ‘I’m going.’”')],'“Bill said ‘I’m going.’”')
        for span in (decoded.links[0],decoded.styles[0]):
            self.assertEqual(decoded.full_text[span['start']:span['start']+span['length']], '‘I’m going.’')
        self.assertEqual(decoded.images[0]['offset'],decoded.full_text.index('After.'))
