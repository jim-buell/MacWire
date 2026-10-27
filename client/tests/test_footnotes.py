"""Use the native portable link resolver with real publisher footnote markup."""
import ctypes as c
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'backend'))
from html_text import html_to_text

class Link(c.Structure):
    _fields_=[('start',c.c_short),('length',c.c_short),('target',c.c_short)]

class FootnoteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        path=Path(cls.temp.name)/'footnotes.so'
        subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-shared','-fPIC',str(ROOT/'client/Footnotes.c'),'-o',str(path)],check=True)
        cls.lib=c.CDLL(str(path))
        cls.lib.BuildFootnoteLinks.argtypes=[c.c_char_p,c.POINTER(Link),c.c_short]
        cls.lib.BuildFootnoteLinks.restype=c.c_short
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def links(self,text,capacity=128):
        body=text.encode('mac_roman');links=(Link*max(1,capacity))()
        count=self.lib.BuildFootnoteLinks(body,links,capacity)
        return body,[(body[l.start:l.start+l.length].decode('mac_roman'),l.start,l.target) for l in links[:count]]
    def test_publisher_references_and_backlinks_resolve_in_both_directions(self):
        text=html_to_text('<p>Text<sup id="fnr1-x"><a href="#fn1-x">1</a></sup>.</p><ol><li id="fn1-x"><p>Note <a href="#fnr1-x">↩</a></p></li></ol>')
        body,links=self.links(text)
        self.assertEqual([l[0] for l in links],['[1]','[Back to 1]'])
        self.assertEqual(links[0][2],body.index(b'[1]:'))
        self.assertEqual(links[1][2],body.index(b'[1]'))
    def test_unresolved_references_and_url_text_are_not_links(self):
        self.assertEqual(self.links('Text [1]. [www.example.com].')[1],[])
    def test_multiple_references_return_to_first_reference(self):
        body,links=self.links('A [2]. B [2].\r\r[2]: Note. [Back to 2]')
        self.assertEqual(len(links),3)
        self.assertEqual(links[-1][2],body.index(b'[2]'))
    def test_cap_and_invalid_numbers_are_bounded(self):
        self.assertEqual(len(self.links('A [1]. B [1].\r\r[1]: Note. [Back to 1]',1)[1]),1)
        self.assertEqual(self.links('[0] [-1] [99999999]\r\r[0]: Invalid.')[1],[])
        self.assertEqual(self.links('A [1].\r\r[1]: Note.',0)[1],[])
