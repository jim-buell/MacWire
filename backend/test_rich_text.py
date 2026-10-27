import tempfile
import unittest
from datetime import datetime, timezone
from html_text import html_to_text_with_images
from models import Article, ArticleContent
from protocol import encode_article, parse_article_response
from database import RollingDatabase


class RichTextTests(unittest.TestCase):
    def render(self, markup):
        images, links, styles = [], [], []
        text = html_to_text_with_images(markup, images, 'https://example.com', links, styles)
        return text, images, links, styles

    def test_black_star_fallback_preserves_links_styles_and_image_positions(self):
        from protocol import _client_text
        self.assertEqual(_client_text('★ Story ★', 100).decode('mac_roman'), '* Story *')
        # Other proposed mappings remain unapproved.
        self.assertEqual(_client_text('☆⚠', 100), b'[symbol][symbol]')
        images=[{'url':'https://example.com/photo'}]; links=[]; styles=[]
        text=html_to_text_with_images('<p>★ <a href="/story"><b>★ linked</b></a></p>'
                                     '<img src="/photo"><p>After.</p>',
                                     images, 'https://example.com', links, styles)
        decoded=parse_article_response(encode_article(ArticleContent(
            id='12345678', title='★ Story', full_text=text,
            links=links, styles=styles, images=images))[3:])
        self.assertEqual(decoded.title, '* Story')
        self.assertTrue(decoded.full_text.startswith('* * linked'))
        link=decoded.links[0]; style=decoded.styles[0]
        self.assertEqual(decoded.full_text[link['start']:link['start']+link['length']], '* linked')
        self.assertEqual(decoded.full_text[style['start']:style['start']+style['length']], '* linked')
        self.assertEqual(decoded.full_text[decoded.images[0]['offset']:], 'After.')

    def test_approved_arrow_and_invisible_markers_preserve_wire_positions(self):
        from protocol import _client_text
        self.assertEqual(_client_text('Six Colors ↦', 100), b'Six Colors ->')
        self.assertEqual(_client_text('word\u2060join ⚠\ufe0f ☆\ufe0e', 100),
                         b'wordjoin [symbol] [symbol]')
        text='↦ a\u2060b\ufe0f linked'
        start=text.index('linked')
        decoded=parse_article_response(encode_article(ArticleContent(
            id='12345678', full_text=text,
            links=[{'start':start,'length':6,'url':'https://example.com'}],
            styles=[{'start':start,'length':6,'flags':1}],
            images=[{'url':'https://example.com/photo','offset':start}]))[3:])
        self.assertEqual(decoded.full_text, '-> ab linked')
        self.assertEqual(decoded.links[0]['start'], 6)
        self.assertEqual(decoded.styles[0]['start'], 6)
        self.assertEqual(decoded.images[0]['offset'], 6)

    def test_nested_emphasis_and_script_positions(self):
        text, _, _, styles = self.render('<p>A <strong>bold <em>both</em></strong>, H<sub>2</sub>O and x<sup>2</sup>; <del>gone</del>.</p>')
        self.assertEqual(text, 'A bold both, H2O and x2; gone.')
        self.assertEqual([(text[s['start']:s['start']+s['length']], s['flags']) for s in styles], [('both',2),('bold both',1),('2',128),('2',64),('gone',256)])

    def test_preformatted_whitespace_and_literal_headings_survive_wire(self):
        text, _, _, styles = self.render('<pre>    indented\n\n\n  ## literal\n  next</pre>')
        # The renderer must preserve blank code lines as well as indentation.
        content = parse_article_response(encode_article(ArticleContent(id='12345678', full_text=text, styles=styles))[3:])
        self.assertTrue(content.full_text.startswith('    indented'))
        self.assertIn('indented\n\n\n  ## literal', content.full_text)
        self.assertEqual(content.styles[0]['flags'],8)

    def test_table_alignment_including_links_and_symbol_expansion(self):
        text, _, links, styles = self.render('<table><tr><th>Direction</th><th>Destination</th></tr><tr><td>→</td><td><a href="/there">There</a></td></tr><tr><td>Long</td><td>Here</td></tr></table>')
        decoded = parse_article_response(encode_article(ArticleContent(id='12345678',full_text=text,styles=styles,links=links))[3:])
        rows = decoded.full_text.splitlines()
        self.assertEqual(len(rows),3)
        self.assertEqual(len({row.index('|') for row in rows}),1)
        link = decoded.links[0]
        self.assertEqual(decoded.full_text[link['start']:link['start']+link['length']], 'There')
        self.assertTrue(any(s['flags'] & 16 for s in decoded.styles))

    def test_ragged_table_and_spanning_cells_keep_every_value(self):
        text, _, _, _ = self.render('<table><tr><td rowspan="2">A</td><td>B</td></tr><tr><td>C</td></tr><tr><td colspan="2">D</td><td>E</td></tr></table>')
        rows = text.splitlines()
        self.assertIn('A',rows[0]); self.assertIn('B',rows[0]); self.assertIn('C',rows[1]); self.assertIn('D',rows[2]); self.assertIn('E',rows[2])
        self.assertEqual([row.count('|') for row in rows],[2,2,2])

    def test_table_caption_is_retained(self):
        text, _, _, styles = self.render('<table><caption>Important context</caption><tr><td>A</td><td>B</td></tr></table>')
        self.assertIn('Table: Important context',text)
        self.assertIn('A | B',text)
        self.assertTrue(any(s['flags'] & 16 for s in styles))

    def test_pullquote_deduplication_and_unique_quote_style(self):
        text, _, _, styles = self.render('<p>Already quoted.</p><div class="pullquote">Already quoted.</div><div class="pullquote">Unique quotation.</div>')
        self.assertEqual(text.count('Already quoted.'),1)
        quote = next(s for s in styles if s['flags'] & 32)
        self.assertEqual(text[quote['start']:quote['start']+quote['length']], 'Unique quotation.')

    def test_images_links_and_formatting_share_the_same_offsets(self):
        images=[{'url':'https://example.com/photo'}]; links=[]; styles=[]
        text=html_to_text_with_images('<p><a href="/a"><b>Bold café →</b></a></p><img src="/photo"><p><em>After</em></p>', images,'https://example.com',links,styles)
        decoded = parse_article_response(encode_article(ArticleContent(id='12345678',full_text=text,images=images,links=links,styles=styles))[3:])
        self.assertEqual(decoded.images[0]['offset'],decoded.full_text.index('After'))
        self.assertEqual(decoded.full_text[decoded.links[0]['start']:decoded.links[0]['start']+decoded.links[0]['length']], 'Bold café ->')
        self.assertEqual(decoded.styles[0]['length'],len('Bold café ->'))

    def test_persisted_styles_follow_body_replacement(self):
        with tempfile.TemporaryDirectory() as folder:
            db=RollingDatabase(folder)
            article=Article(id='12345678',full_text='Bold',published=datetime.now(timezone.utc),styles=[{'start':0,'length':4,'flags':1}])
            db.add_articles([article])
            self.assertEqual(RollingDatabase(folder).get_article(article.id).styles,article.styles)
            db.set_article_content(article.id,'Replacement')
            self.assertEqual(db.get_article(article.id).styles,[])

    def test_invalid_styles_and_style_limit(self):
        content=ArticleContent(id='12345678',full_text='word',styles=[{'start':-1,'length':2,'flags':1},{'start':0,'length':4,'flags':1024}])
        self.assertEqual(parse_article_response(encode_article(content)[3:]).styles,[])
        content.styles=[{'start':0,'length':4,'flags':1}]*300
        self.assertEqual(len(parse_article_response(encode_article(content)[3:]).styles),256)
