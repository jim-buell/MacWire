"""Exercise the shipping cursor animation against a controllable Toolbox clock."""
import ctypes as c
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Cursor(c.Structure):
    _fields_ = [('data', c.c_ushort * 16), ('mask', c.c_ushort * 16),
                ('hotspot', c.c_short * 2)]


class WaitCursorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        temp = Path(cls.temp.name)
        (temp / 'QuickDraw.h').write_text('''
#include <stdint.h>
typedef struct { uint16_t data[16], mask[16]; short hotSpot[2]; } Cursor;
void SetCursor(Cursor *cursor);
void InitCursor(void);
''')
        (temp / 'Events.h').write_text('#include <stdint.h>\nuint32_t TickCount(void);\n')
        (temp / 'toolbox.c').write_text('''
#include "QuickDraw.h"
uint32_t testTicks;
int testSetCalls, testArrowCalls;
Cursor testCursor;
uint32_t TickCount(void) { return testTicks; }
void SetCursor(Cursor *cursor) { testCursor = *cursor; ++testSetCalls; }
void InitCursor(void) { ++testArrowCalls; }
''')
        library = temp / 'cursor.so'
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-I', str(temp),
                        str(ROOT / 'WaitCursor.c'), str(temp / 'toolbox.c'),
                        '-o', str(library)], check=True)
        cls.lib = c.CDLL(str(library))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.ticks(0)
        for name in ('testSetCalls', 'testArrowCalls'):
            c.c_int.in_dll(self.lib, name).value = 0

    def tearDown(self):
        # Balance every scope even if an assertion fails.
        for _ in range(5):
            self.lib.EndNetworkWait()

    def ticks(self, value):
        c.c_uint32.in_dll(self.lib, 'testTicks').value = value

    def cursor(self):
        return bytes(Cursor.in_dll(self.lib, 'testCursor'))

    def count(self, name):
        return c.c_int.in_dll(self.lib, name).value

    def test_complete_eased_loop_matches_source_pixels_and_alpha(self):
        from PIL import Image
        strip = Image.open(ROOT / 'assets/wait-cursor.png').convert('RGBA')
        order = list(range(8)) + list(range(6, 0, -1))
        durations = [7, 6, 5, 4, 4, 5, 6, 7, 6, 5, 4, 4, 5, 6]
        self.lib.BeginNetworkWait()
        tick = 0
        for pose, duration in zip(order, durations):
            cursor = Cursor.in_dll(self.lib, 'testCursor')
            self.assertEqual(list(cursor.hotspot), [8, 8])
            for y in range(16):
                for x in range(16):
                    r, g, b, a = strip.getpixel((pose * 16 + x, y))
                    bit = 0x8000 >> x
                    self.assertEqual(bool(cursor.mask[y] & bit), bool(a))
                    self.assertEqual(bool(cursor.data[y] & bit), bool(a and r == 0))
            snapshot = self.cursor()
            self.ticks(tick + duration - 1)
            self.lib.UpdateNetworkWaitCursor()
            self.assertEqual(self.cursor(), snapshot)
            tick += duration
            self.ticks(tick)
            self.lib.UpdateNetworkWaitCursor()
        self.assertEqual(tick, 74)
        self.lib.EndNetworkWait()
        self.assertEqual(self.count('testArrowCalls'), 1)

    def test_nested_wait_keeps_phase_and_restores_arrow_only_at_end(self):
        self.lib.BeginNetworkWait()
        first = self.cursor()
        self.ticks(7)
        self.lib.UpdateNetworkWaitCursor()
        second = self.cursor()
        self.assertNotEqual(first, second)
        self.lib.BeginNetworkWait()
        self.assertEqual(self.cursor(), second)
        self.lib.EndNetworkWait()
        self.assertEqual(self.count('testArrowCalls'), 0)
        self.lib.EndNetworkWait()
        self.assertEqual(self.count('testArrowCalls'), 1)
        calls = self.count('testSetCalls')
        self.lib.UpdateNetworkWaitCursor()
        self.lib.EndNetworkWait()
        self.assertEqual(self.count('testSetCalls'), calls)
        self.assertEqual(self.count('testArrowCalls'), 1)
        self.lib.BeginNetworkWait()
        self.assertEqual(self.cursor(), first)

    def test_clock_wrap_and_long_poll_gap_preserve_phase(self):
        self.ticks(0xFFFFFFFC)
        self.lib.BeginNetworkWait()
        first = self.cursor()
        self.ticks(3)  # Seven elapsed ticks, across the 32-bit wrap.
        self.lib.UpdateNetworkWaitCursor()
        second = self.cursor()
        self.assertNotEqual(first, second)
        self.ticks(3 + 74 * 10000)
        self.lib.UpdateNetworkWaitCursor()
        self.assertEqual(self.cursor(), second)

    def test_generated_frames_are_reproducible(self):
        spec = importlib.util.spec_from_file_location('cursor_generator', ROOT / 'tools/generate_wait_cursor.py')
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        self.assertEqual((ROOT / 'WaitCursorFrames.h').read_text(), generator.generate())


if __name__ == '__main__':
    unittest.main()
