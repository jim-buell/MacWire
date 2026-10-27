"""Check image growth preserves visible text and respects viewport boundaries."""
import ctypes as c
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReaderViewportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name)/'viewport.so'
        subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-shared','-fPIC',
                        str(ROOT/'ReaderViewport.c'),'-o',str(library)],check=True)
        cls.lib = c.CDLL(str(library))
        cls.scroll = cls.lib.ReaderScrollAfterImage
        cls.scroll.argtypes = [c.c_long]*5
        cls.scroll.restype = c.c_long
        cls.anchor_scroll=cls.lib.ReaderScrollForImageAnchor
        cls.anchor_scroll.argtypes=[c.c_long]*6
        cls.anchor_scroll.restype=c.c_long
        cls.next_image=cls.lib.ReaderNextImage
        cls.next_image.argtypes=[c.POINTER(c.c_long),c.POINTER(c.c_short),
                                c.POINTER(c.c_ubyte),c.c_short,c.c_long,c.c_long]
        cls.next_image.restype=c.c_short

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_growth_and_shrink_above_viewport_preserve_text_position(self):
        for height in (10, 80, 250):
            old_scroll, old_text_top = 150, 160
            new_scroll = self.scroll(old_scroll,40,60,height,1000)
            new_text_top = old_text_top+height-60
            self.assertEqual(new_text_top-new_scroll,old_text_top-old_scroll)

    def test_visible_or_lower_image_does_not_move_viewport(self):
        for top in (100,140,200):
            self.assertEqual(self.scroll(150,top,60,250,1000),150)

    def test_boundary_and_document_limits(self):
        self.assertEqual(self.scroll(100,40,60,100,1000),140)
        self.assertEqual(self.scroll(150,40,60,250,300),300)
        self.assertEqual(self.scroll(150,40,60,250,-10),0)
        self.assertEqual(self.scroll(0,0,0,0,100),0)

    def test_visible_images_win_and_priority_changes_as_you_scroll(self):
        tops=(c.c_long*4)(0,500,200,1000)
        heights=(c.c_short*4)(50,50,50,50)
        states=(c.c_ubyte*4)(0,0,0,0)
        self.assertEqual(self.next_image(tops,heights,states,4,180,100),2)
        self.assertEqual(self.next_image(tops,heights,states,4,480,100),1)
        states[1]=1
        self.assertEqual(self.next_image(tops,heights,states,4,480,100),2)
        states[2]=2
        self.assertEqual(self.next_image(tops,heights,states,4,480,100),3)

    def test_image_priority_handles_ties_boundaries_and_completed_images(self):
        tops=(c.c_long*3)(50,200,180)
        heights=(c.c_short*3)(50,40,10)
        states=(c.c_ubyte*3)(0,0,0)
        self.assertEqual(self.next_image(tops,heights,states,3,100,100),2)
        states[2]=1
        self.assertEqual(self.next_image(tops,heights,states,3,100,100),0)
        states[0]=1; states[1]=2
        self.assertEqual(self.next_image(tops,heights,states,3,100,100),-1)
        self.assertEqual(self.next_image(tops,heights,states,3,100,0),-1)

    def test_visible_text_below_partially_visible_placeholder_stays_in_place(self):
        # View starts inside the tiny placeholder; the first text is below it.
        scroll, anchor, top, old_height, height = 110, 130, 100, 20, 200
        new_scroll = self.anchor_scroll(scroll, anchor, top, old_height, height, 1000)
        self.assertEqual(anchor + height - old_height - new_scroll, anchor - scroll)
        self.assertEqual(new_scroll, 290)

    def test_visible_text_above_image_and_an_image_only_viewport_keep_scroll(self):
        self.assertEqual(self.anchor_scroll(100, 105, 200, 20, 200, 1000), 100)
        self.assertEqual(self.anchor_scroll(150, 150, 100, 200, 250, 1000), 150)

    def test_anchor_uses_latest_scroll_and_handles_successive_images(self):
        # User has scrolled further during the transfer, not stayed at launch position.
        scroll = self.anchor_scroll(300, 305, 40, 20, 180, 1500)
        self.assertEqual(scroll, 460)
        scroll = self.anchor_scroll(scroll, 465, 250, 20, 120, 1600)
        self.assertEqual(scroll, 560)
        self.assertEqual(305 + 160 + 100 - scroll, 5)
