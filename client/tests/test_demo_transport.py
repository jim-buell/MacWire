"""Compare simulated downloads with the instant demo and exercise cancellation."""
import ctypes as c
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DemoTransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        temp = Path(cls.temp.name)
        (temp / 'Events.h').write_text('#include <stdint.h>\nuint32_t TickCount(void);\n')
        (temp / 'clock.c').write_text('''
#include <stdint.h>
uint32_t ticks;
int polls, cancel;
uint32_t TickCount(void) { return ticks; }
int NetworkPoll(void) { ++ticks; ++polls; return cancel; }
''')
        cls.libs = []
        for slow in (False, True):
            path = temp / ('slow.so' if slow else 'instant.so')
            command = ['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-shared',
                       '-fPIC', '-I', str(temp), str(ROOT / 'DemoTransport.c'),
                       str(temp / 'clock.c'), '-o', str(path)]
            if slow:
                command.append('-DRSS_DEMO_SLOW_DOWNLOADS=1')
            subprocess.run(command, check=True)
            lib = c.CDLL(str(path))
            lib.DemoResponse.argtypes = [c.c_char_p, c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong)]
            lib.DemoResponse.restype = c.c_short
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        for lib in self.libs:
            c.c_uint32.in_dll(lib, 'ticks').value = 0
            for name in ('polls', 'cancel'):
                c.c_int.in_dll(lib, name).value = 0

    def response(self, lib, path, capacity=32768):
        out = c.create_string_buffer(32768)
        length = c.c_ulong(123)
        result = lib.DemoResponse(('GET ' + path + ' HTTP/1.0\r\n\r\n').encode(), out, capacity, c.byref(length))
        return result, out.raw[:length.value], length.value

    def test_slow_packets_match_instant_packets_and_poll_for_90_ticks(self):
        for path in ('/feeds', '/sync?feed=', '/dates', '/article/00000001',
                     '/article/00000002', '/article/00000003', '/image/00000001/0'):
            self.setUp()
            fast = self.response(self.libs[0], path)
            slow = self.response(self.libs[1], path)
            self.assertEqual(fast, slow, path)
            self.assertEqual(slow[0], 0)
            self.assertGreater(slow[2], 0)
            self.assertEqual(c.c_int.in_dll(self.libs[1], 'polls').value, 90)

    def test_cancel_reports_cancelled_and_does_not_expose_partial_packet(self):
        slow = self.libs[1]
        c.c_int.in_dll(slow, 'cancel').value = 1
        self.assertEqual(self.response(slow, '/article/00000001'), (-30001, b'', 0))
        c.c_int.in_dll(slow, 'cancel').value = 0
        self.assertEqual(self.response(slow, '/article/00000001')[0], 0)

    def test_unknown_request_and_small_buffer_fail_without_waiting(self):
        slow = self.libs[1]
        self.assertEqual(self.response(slow, '/unknown'), (-1, b'', 0))
        self.assertEqual(self.response(slow, '/feeds', 1), (-1, b'', 0))
        self.assertEqual(c.c_int.in_dll(slow, 'polls').value, 0)

    def test_download_clock_survives_wrap(self):
        slow = self.libs[1]
        c.c_uint32.in_dll(slow, 'ticks').value = 0xFFFFFFE0
        self.assertEqual(self.response(slow, '/feeds')[0], 0)
        self.assertEqual(c.c_int.in_dll(slow, 'polls').value, 90)


if __name__ == '__main__':
    unittest.main()
