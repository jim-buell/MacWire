import tempfile
import unittest
from datetime import datetime, timezone
from html_text import html_to_text_with_images
from models import Article, ArticleContent
from protocol import encode_article, parse_article_response
from database import RollingDatabase


class LinkTests(unittest.TestCase):
    def test_labels_offsets_images_and_relative_urls(self):
        links=[]; images=[{'url':'https://example.com/photo'}]
        text=html_to_text_with_images('<p>This <a href="/story">is <b>a link</b></a>.</p>'
            '<img src="/photo"><h2>The <a href="/second">second</a> question</h2>',images,'https://example.com/feed',links)
        self.assertEqual(text,'This is a link.\n\n## The second question')
        self.assertEqual([text[x['start']:x['start']+x['length']] for x in links],['is a link','second'])
        self.assertEqual(links[0]['url'],'https://example.com/story')
        self.assertEqual(images[0]['offset'],text.index('##'))
        self.assertNotIn('example.com',text)

    def test_quotes_footnotes_and_unlinked_elements(self):
        links=[]
        text=html_to_text_with_images('<blockquote><a href="/quote">“Quoted”</a></blockquote>'
            '<p>Note<a href="#fn1">1</a> <a href="javascript:x">unsafe</a></p>'
            '<ol><li id="fn1">Footnote.</li></ol><a href="/image"><img src="/photo"></a>',[], 'https://example.com',links)
        self.assertTrue(text.startswith('“Quoted”'))
        self.assertIn('[1]',text)
        self.assertEqual(len(links),1)
        self.assertEqual(text[links[0]['start']:links[0]['start']+links[0]['length']],'“Quoted”')

    def test_macroman_link_offsets_and_image_extension_round_trip(self):
        text='Café → a linked résumé.'
        start=text.index('linked')
        source=ArticleContent(id='12345678',full_text=text,links=[{'start':start,'length':len('linked résumé'),'url':'https://example.com/story?q=1'}],images=[{'url':'https://example.com/photo','offset':start}])
        decoded=parse_article_response(encode_article(source)[3:])
        link=decoded.links[0]
        self.assertEqual(decoded.full_text[link['start']:link['start']+link['length']],'linked résumé')
        self.assertEqual(decoded.images[0]['offset'],link['start'])
        self.assertEqual(link['url'],source.links[0]['url'])

    def test_clipped_invalid_and_excessive_links(self):
        content=ArticleContent(id='12345678',full_text='x'*12000,links=[{'start':11000,'length':4,'url':'https://example.com'}])
        self.assertEqual(parse_article_response(encode_article(content)[3:]).links,[])
        content.full_text='word'
        content.links=[{'start':0,'length':4,'url':'https://example.com/'+str(i)} for i in range(60)]
        self.assertEqual(len(parse_article_response(encode_article(content)[3:]).links),48)
        content.links=[{'start':-1,'length':4,'url':'https://example.com'}, {'start':0,'length':4,'url':'javascript:x'}]
        self.assertEqual(parse_article_response(encode_article(content)[3:]).links,[])

    def test_metadata_persists_with_body_and_is_cleared_on_replacement(self):
        with tempfile.TemporaryDirectory() as folder:
            db=RollingDatabase(folder)
            article=Article(id='12345678',full_text='word',published=datetime.now(timezone.utc),links=[{'start':0,'length':4,'url':'https://example.com'}])
            db.add_articles([article])
            restored=RollingDatabase(folder).get_article(article.id)
            self.assertEqual(restored.links,article.links)
            db.set_article_content(article.id,'New body')
            self.assertEqual(db.get_article(article.id).links,[])


if __name__ == '__main__': unittest.main()
