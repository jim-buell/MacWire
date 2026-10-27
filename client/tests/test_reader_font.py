"""Run the shipping Font Manager adapter with bitmap/outline Toolbox fixtures."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FontInfo(ctypes.Structure):
    _fields_ = [(name, ctypes.c_short) for name in
                ('ascent', 'descent', 'widMax', 'leading')]


class ReaderFontTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        path = Path(cls.temp.name)
        (path / 'QuickDraw.h').write_text('''
#ifndef TEST_QD_H
#define TEST_QD_H
typedef unsigned char Boolean, Style;
typedef char *Ptr;
typedef short CharParameter;
typedef struct { short v,h; } Point;
typedef struct { short ascent,descent,widMax,leading; } FontInfo;
typedef struct { short txFont,txSize; Style txFace; } GrafPort;
extern struct QD { GrafPort *thePort; } qd;
short TextWidth(Ptr,short,short);
short CharWidth(CharParameter);
void GetFontInfo(FontInfo *);
void DrawText(Ptr,short,short);
#endif
''')
        (path / 'Fonts.h').write_text('''
#include "QuickDraw.h"
typedef struct { short family,size; Style face; Boolean needBits;
short device; Point numer,denom; } FMInput;
typedef struct { unsigned char italic; } FMOutput;
typedef FMOutput *FMOutPtr;
Boolean GetOutlinePreferred(void);
void SetOutlinePreferred(Boolean);
FMOutPtr FMSwapFont(FMInput *);
''')
        (path / 'toolbox.c').write_text('''
#include "Fonts.h"
GrafPort port={16,12,2};
struct QD qd={&port};
int testPreferred, testSeen, testOutlineAvailable=1;
Boolean GetOutlinePreferred(void) { return testPreferred; }
void SetOutlinePreferred(Boolean b) { testPreferred=b; }
void TestFont(short family,short size) { port.txFont=family; port.txSize=size; }
short TextWidth(Ptr p,short s,short n) { (void)p; (void)s;
testSeen=testPreferred; return n*6; }
short CharWidth(CharParameter c) { (void)c; testSeen=testPreferred; return 6; }
void GetFontInfo(FontInfo *f) { testSeen=testPreferred;
f->ascent=10; f->descent=3; f->leading=1; f->widMax=8; }
void DrawText(Ptr p,short s,short n) { (void)p; (void)s; (void)n;
testSeen=testPreferred; }
FMOutPtr FMSwapFont(FMInput *i) { static FMOutput o; (void)i;
testSeen=testPreferred; o.italic=!(testPreferred && testOutlineAvailable); return &o; }
''')
        lib = path / 'reader-font.so'
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-I', str(path),
                        str(ROOT / 'ReaderFont.c'), str(path / 'toolbox.c'),
                        '-o', str(lib)], check=True)
        cls.lib = ctypes.CDLL(str(lib))
        cls.lib.ReaderItalicClearance.argtypes = [ctypes.c_ubyte]
        cls.lib.ReaderUnderlineOffset.argtypes = [ctypes.POINTER(FontInfo)]
        cls.lib.ReaderScriptRise.argtypes = [ctypes.POINTER(FontInfo), ctypes.c_ubyte]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def value(self, name, value=None):
        field = ctypes.c_int.in_dll(self.lib, name)
        if value is not None:
            field.value = value
        return field.value

    def setUp(self):
        self.lib.TestFont(16, 12)  # Palatino family ID
        self.value('testOutlineAvailable', 1)
        self.value('testPreferred', 0)

    def test_measure_draw_and_metrics_choose_the_same_font_and_restore_preference(self):
        for saved in (0, 1):
            for family, expected in ((16, 1), (20, 1), (2, 0), (4, 0), (0, 0)):
                self.lib.TestFont(family, 12)
                self.value('testPreferred', saved)
                operations = [lambda: self.lib.ReaderFontWidth(b'abc', 0, 3),
                              lambda: self.lib.ReaderFontCharWidth(ord('a')),
                              lambda: self.lib.ReaderFontDraw(b'abc', 0, 3),
                              lambda: self.lib.ReaderFontInfo(ctypes.byref(FontInfo()))]
                for operation in operations:
                    operation()
                    self.assertEqual(self.value('testSeen'), expected)
                    self.assertEqual(self.value('testPreferred'), saved)

    def test_synthetic_italic_reserves_more_room_than_designed_italic(self):
        self.assertEqual(self.lib.ReaderItalicClearance(ord('a')), 1)
        self.value('testOutlineAvailable', 0)
        self.assertEqual(self.lib.ReaderItalicClearance(ord('a')), 3)
        self.assertEqual(self.value('testPreferred'), 0)
        self.lib.TestFont(16, 24)
        self.assertEqual(self.lib.ReaderItalicClearance(ord('a')), 6)

    def test_new_york_tightens_only_comma_and_period_boundaries(self):
        self.value('testOutlineAvailable', 0)
        for size in (12, 24):
            self.lib.TestFont(2, size)
            for following in b',.':
                self.assertEqual(self.lib.ReaderItalicClearance(following), 1)
            for following in b'a ;:':
                self.assertEqual(self.lib.ReaderItalicClearance(following), size // 4)
            self.lib.TestFont(16, size)
            for following in b',.':
                self.assertEqual(self.lib.ReaderItalicClearance(following), size // 4)
        self.assertEqual(self.value('testPreferred'), 0)

    def test_scripts_follow_font_metrics_at_different_sizes(self):
        for ascent, descent, superscript, subscript in ((10, 3, -3, 2),
                                                       (16, 5, -5, 3),
                                                       (0, 0, -1, 1)):
            info = FontInfo(ascent, descent, 8, 1)
            self.assertEqual(self.lib.ReaderScriptRise(ctypes.byref(info), 1), superscript)
            self.assertEqual(self.lib.ReaderScriptRise(ctypes.byref(info), 0), subscript)

    def test_table_heading_rules_clear_descenders_in_every_font_family_and_size(self):
        # New York 12 has descending digits; Palatino 12 has a deeper descent.
        for family, ascent, descent in ((2, 12, 3), (16, 10, 4), (20, 9, 3),
                                        (3, 12, 3), (4, 9, 2), (0, 12, 3)):
            for scale in (1, 2):
                self.lib.TestFont(family, 12 * scale)
                metrics = FontInfo(ascent * scale, descent * scale, 8, 0)
                offset = self.lib.ReaderUnderlineOffset(ctypes.byref(metrics))
                self.assertGreater(offset, metrics.descent)
                height = metrics.ascent + metrics.descent + max(1, metrics.leading) + 1
                self.assertLess(metrics.ascent + offset, height)
        self.assertEqual(self.lib.ReaderUnderlineOffset(ctypes.byref(FontInfo())), 1)
