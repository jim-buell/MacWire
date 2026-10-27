import ctypes as c
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class LinkDisplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        path=Path(cls.temp.name)/'display.so'
        subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-shared','-fPIC',
            str(ROOT/'LinkDisplay.c'),str(ROOT/'ReaderMarkup.c'),'-o',str(path)],check=True)
        cls.lib=c.CDLL(str(path))
        cls.lib.FormatLinkDestination.argtypes=[c.c_char_p,c.c_void_p,c.c_short,c.c_short]
        cls.lib.ReaderHeadingPrefix.argtypes=[c.c_char_p]
        cls.lib.ReaderHeadingPrefix.restype=c.c_short

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def display(self,url,columns=30,capacity=256):
        out=c.create_string_buffer(capacity)
        self.lib.FormatLinkDestination(url.encode(),out,capacity,columns)
        return out.value.decode()

    def test_domain_preserved_with_path_tail_and_no_tracking(self):
        self.assertEqual(self.display('https://example.com/'), 'example.com')
        self.assertEqual(self.display('https://example.com/story?tracking=1#part'), 'example.com/story')
        text=self.display('https://example.com/a/very/long/path/story-title',30)
        self.assertTrue(text.startswith('example.com/...'))
        self.assertTrue(text.endswith('story-title'))
        self.assertEqual(len(text),30)

    def test_long_hostname_and_small_widths_are_bounded(self):
        url='https://averylongsubdomain.averylongdomain.example/story'
        text=self.display(url,24)
        self.assertEqual(len(text),24)
        self.assertIn('...',text)
        self.assertTrue(text.endswith('.example'))
        for width in range(0,50):
            self.assertLessEqual(len(self.display(url,width)),width)
        self.assertLessEqual(len(self.display(url,30,8)),7)

    def test_headings_only_recognize_nonempty_standalone_prefix(self):
        for source,count in [('## The Siri AI question',3),('# Heading',2),('### More',4),('###### More',7)]:
            self.assertEqual(self.lib.ReaderHeadingPrefix(source.encode()),count)
        for source in ['#hashtag','C# text','# ','##\n','#\r','####### More','ordinary']:
            self.assertEqual(self.lib.ReaderHeadingPrefix(source.encode()),0)


if __name__ == '__main__': unittest.main()
