"""Run the shipping underline mask on monochrome glyph pixels."""
import ctypes as c
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReaderUnderlineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / 'underline.so'
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', str(ROOT / 'ReaderUnderline.c'),
                        '-o', str(library)], check=True)
        cls.lib = c.CDLL(str(library))
        cls.lib.ReaderUnderlineMask.argtypes = [c.c_void_p, c.c_int, c.c_int,
                                                c.c_int, c.c_int, c.c_void_p, c.c_int]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def mask(self, width, height, y, ink=(), stride=None):
        stride = stride or ((width + 15) // 16 * 2)
        bitmap = bytearray(stride * height)
        for x, row in ink:
            bitmap[row * stride + x // 8] |= 0x80 >> (x % 8)
        source = c.create_string_buffer(bytes(bitmap))
        output = c.create_string_buffer((width + 7) // 8)
        self.assertEqual(self.lib.ReaderUnderlineMask(source, stride, width, height,
                                                      y, output, len(output)), 1)
        return {x for x in range(width) if output.raw[x // 8] & (0x80 >> (x % 8))}

    def test_rule_stays_continuous_below_letters_without_descenders(self):
        # Baseline row 4, underline row 5; ordinary glyph ends at row 3.
        self.assertEqual(self.mask(16, 8, 5, [(x, 3) for x in range(2, 13)]), set(range(16)))

    def test_descenders_and_low_numerals_get_one_pixel_clearance(self):
        for ink_row in (4, 5, 6):
            allowed = self.mask(16, 8, 5, [(5, ink_row), (6, ink_row)])
            self.assertEqual(allowed, set(range(16)) - {4, 5, 6, 7})

    def test_gaps_cross_byte_boundaries_and_handle_padding_and_edges(self):
        self.assertEqual(self.mask(19, 8, 5, [(7, 5), (8, 5), (18, 6)], stride=4),
                         set(range(19)) - {6, 7, 8, 9, 17, 18})
        self.assertEqual(self.mask(9, 1, 0, [(0, 0), (8, 0)]), {2, 3, 4, 5, 6})

    def test_invalid_dimensions_and_small_destination_are_rejected(self):
        source = c.create_string_buffer(16)
        output = c.create_string_buffer(1)
        for width, height, stride, y in ((16, 8, 2, 3), (9, 8, 1, 3),
                                         (8, 8, 2, -1), (8, 8, 2, 8),
                                         (0, 8, 2, 0), (8, 0, 2, 0)):
            self.assertEqual(self.lib.ReaderUnderlineMask(source, stride, width, height,
                                                          y, output, len(output)), 0)
