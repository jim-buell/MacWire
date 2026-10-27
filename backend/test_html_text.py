"""HTML source line wraps must not become hard breaks on the classic Mac."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parent))
from html_text import html_to_text
from fetcher import FeedFetcher
from content_fetcher import _BodyExtractor


class HTMLTextTests(unittest.TestCase):
    def test_feed_quotes_reflow_source_lines_and_keep_paragraphs(self):
        markup = '''<p>A writer says:</p>
<blockquote>
  <p>These source lines are wrapped
for readability, with an <a href="https://example.com">inline
link</a> and <em>emphasis</em>.</p>

<p>This is a second
paragraph.</p>
</blockquote>
<p>The author's response.</p>'''
        expected = 'A writer says:\n\n“These source lines are wrapped for readability, with an inline link [example.com] and emphasis.\n\nThis is a second paragraph.”\n\nThe author\'s response.'
        self.assertEqual(FeedFetcher()._strip_html(markup), expected)

    def test_inline_punctuation_and_whitespace_survive_tag_boundaries(self):
        self.assertEqual(html_to_text('<p>A <b>word</b>, then <i>another</i>.</p>'), 'A word, then another.')
        self.assertEqual(html_to_text('<p>some<b>thing</b> else &amp; more</p>'), 'something else & more')

    def test_explicit_break_is_preserved_while_source_newline_flows(self):
        self.assertEqual(html_to_text('<p>First\nline<br>Second\nline</p>'), 'First line\nSecond line')

    def test_list_inside_quote_has_clear_item_boundaries(self):
        self.assertEqual(html_to_text('<blockquote>\n<ul><li>First\nitem</li><li>Second item</li></ul>\n</blockquote>'), '“• First item\n\n• Second item”')

    def test_nested_quotes_are_distinguishable(self):
        self.assertEqual(html_to_text('<blockquote><p>Outer.</p><blockquote><p>Inner.</p></blockquote><p>Outer again.</p></blockquote>'), '“Outer.\n\n‘Inner.’\n\nOuter again.”')

    def test_preformatted_lines_keep_spacing(self):
        self.assertEqual(html_to_text('<p>Code:</p><pre>one\n  two\nthree</pre><p>After.</p>'), 'Code:\n\none\n  two\nthree\n\nAfter.')

    def test_quote_markers_survive_macroman_article_packet(self):
        from models import ArticleContent
        from protocol import encode_article, parse_article_response
        text = html_to_text('<p>Intro.</p><blockquote><p>Quoted\nwords.</p></blockquote>')
        packet = encode_article(ArticleContent(id='12345678', full_text=text))
        self.assertEqual(parse_article_response(packet[3:]).full_text, text)

    def test_downloaded_article_uses_same_quotes_and_reflow(self):
        parser = _BodyExtractor()
        parser.feed('<html><head><title>A\n title</title></head><body><nav>Skip me</nav><p>Intro.</p><blockquote><p>Quoted\nwords.</p></blockquote><p>After.</p><footer>Skip this too</footer></body></html>')
        parser.close()
        self.assertEqual(parser.get_title(), 'A title')
        self.assertEqual(parser.get_text(), 'Intro.\n\n“Quoted words.”\n\nAfter.')
