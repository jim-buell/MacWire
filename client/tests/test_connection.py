"""Exercise the actual portable HTTP framing and network-setting validators."""
import ctypes as c
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Response(c.Structure):
    _fields_ = [("header", c.c_char * 4096), ("headerLength", c.c_size_t),
                ("length", c.c_size_t), ("capacity", c.c_size_t),
                ("expected", c.c_long), ("body", c.c_void_p),
                ("headersDone", c.c_int), ("complete", c.c_int), ("failed", c.c_int)]


class Settings(c.Structure):
    _fields_ = [("address", c.c_char * 16), ("port", c.c_ushort),
                ("sample", c.c_ushort), ("windowHours", c.c_ushort)]


class ConnectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / "connection.so"
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-shared", "-fPIC", str(ROOT / "client/HTTPResponse.c"),
                        str(ROOT / "client/ConnectionConfig.c"), "-o", str(library)], check=True)
        cls.lib = c.CDLL(str(library))
        cls.lib.HTTPResponseInit.argtypes = [c.POINTER(Response), c.c_void_p, c.c_size_t]
        cls.lib.HTTPResponseFeed.argtypes = [c.POINTER(Response), c.c_void_p, c.c_size_t]
        cls.lib.HTTPResponseEnd.argtypes = [c.POINTER(Response)]
        cls.lib.ConnectionAddress.argtypes = [c.c_char_p, c.POINTER(c.c_uint32)]
        cls.lib.ConnectionPort.argtypes = [c.c_char_p, c.POINTER(c.c_ushort)]
        cls.lib.ConnectionDecodeSettings.argtypes = [c.c_char_p, c.c_ulong, c.POINTER(Settings)]
        cls.lib.ConnectionEncodeSettings.argtypes = [c.POINTER(Settings), c.c_void_p]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def parse(self, data, step=1, capacity=32768):
        r, body = Response(), c.create_string_buffer(capacity)
        self.lib.HTTPResponseInit(c.byref(r), body, capacity)
        for start in range(0, len(data), step):
            part = data[start:start + step]
            if self.lib.HTTPResponseFeed(c.byref(r), part, len(part)) < 0:
                return -1, body.raw[:r.length]
        return self.lib.HTTPResponseEnd(c.byref(r)), body.raw[:r.length]

    def test_binary_body_survives_arbitrary_tcp_boundaries(self):
        body = bytes(range(256)) * 8
        response = b"HTTP/1.0 200 OK\r\nContent-Length: 2048\r\n\r\n" + body
        for step in (1, 2, 7, 43, 2048, len(response)):
            with self.subTest(step=step):
                self.assertEqual(self.parse(response, step), (1, body))

    def test_close_delimited_response(self):
        self.assertEqual(self.parse(b"HTTP/1.0 200 OK\r\nServer: Test\r\n\r\nabc"), (1, b"abc"))

    def test_truncated_headers_and_body(self):
        for data in (b"HTTP/1.0 200 OK\r\n", b"HTTP/1.0 200 OK\r\nContent-Length: 4\r\n\r\nabc"):
            self.assertEqual(self.parse(data)[0], -1)

    def test_rejects_errors_and_unsupported_framing(self):
        for headers in (b"HTTP/1.0 404 Not Found", b"HTTP/2.0 200 OK",
                        b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked",
                        b"HTTP/1.0 200 OK\r\nContent-Length: -1",
                        b"HTTP/1.0 200 OK\r\nContent-Length: 2junk",
                        b"HTTP/1.0 200 OK\r\nContent-Length: 2\r\nContent-Length: 3"):
            with self.subTest(headers=headers):
                self.assertEqual(self.parse(headers + b"\r\n\r\nabc")[0], -1)

    def test_bounds_and_extra_bytes(self):
        for data in (b"HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nabcde",
                     b"HTTP/1.0 200 OK\r\n\r\nabcde",
                     b"HTTP/1.0 200 OK\r\nContent-Length: 3\r\n\r\nabcd",
                     b"HTTP/1.0 200 OK\r\nX: " + b"x" * 4096 + b"\r\n\r\n",
                     b"HTTP/1.0 200 OK\r\nX: \x00\r\n\r\n"):
            self.assertEqual(self.parse(data, capacity=4)[0], -1)

    def test_length_case_insensitivity_and_identical_duplicates(self):
        self.assertEqual(self.parse(b"HTTP/1.1 200 OK\r\ncOnTeNt-LeNgTh: 3\r\n"
                                    b"Content-Length: 3\r\n\r\nabc"), (1, b"abc"))

    def test_ipv4_validation(self):
        result = c.c_uint32()
        self.assertEqual(self.lib.ConnectionAddress(b"192.168.1.10", c.byref(result)), 1)
        self.assertEqual(result.value, 0xC0A8010A)
        for text in (b"", b"localhost", b"192.168.1", b"256.1.2.3", b"1..2.3",
                     b"1.2.3.4:8081", b"1.2.3.4 ", b"0.0.0.0", b"255.255.255.255",
                     b"1234.1.2.3", b"1.2.3.4.5"):
            with self.subTest(text=text):
                self.assertEqual(self.lib.ConnectionAddress(text, c.byref(result)), 0)

    def test_port_validation(self):
        result = c.c_ushort()
        for port in (1, 8081, 65535):
            self.assertEqual(self.lib.ConnectionPort(str(port).encode(), c.byref(result)), 1)
            self.assertEqual(result.value, port)
        for text in (b"", b"0", b"65536", b"-1", b"8081 ", b"80x", b"1000000"):
            self.assertEqual(self.lib.ConnectionPort(text, c.byref(result)), 0)

    def settings_packet(self, version=2, address=b"192.168.1.10", port=8081, sample=0, hours=24):
        return (version.to_bytes(2, "big") + address.ljust(16, b"\0") +
                port.to_bytes(2, "big") + sample.to_bytes(2, "big") +
                (hours.to_bytes(2, "big") if version == 2 else b""))

    def test_legacy_settings_upgrade_preserves_connection_and_uses_24_hours(self):
        packet = self.settings_packet(version=1, sample=1)
        settings = Settings()
        self.assertEqual(self.lib.ConnectionDecodeSettings(packet, len(packet), c.byref(settings)), 1)
        self.assertEqual((settings.address, settings.port, settings.sample, settings.windowHours),
                         (b"192.168.1.10", 8081, 0, 24))

    def test_retired_sample_flag_migrates_to_live_without_changing_saved_age(self):
        packet = self.settings_packet(sample=1, hours=72)
        settings = Settings()
        self.assertEqual(self.lib.ConnectionDecodeSettings(packet, len(packet), c.byref(settings)), 1)
        self.assertEqual((settings.address, settings.port, settings.sample, settings.windowHours),
                         (b"192.168.1.10", 8081, 0, 72))

    def test_saved_age_choices_round_trip_in_actual_disk_format(self):
        for hours in (24, 48, 72, 168):
            settings = Settings(b"192.168.1.10", 8081, 0, hours)
            output = c.create_string_buffer(24)
            self.lib.ConnectionEncodeSettings(c.byref(settings), output)
            self.assertEqual(output.raw, self.settings_packet(hours=hours))
            decoded = Settings()
            self.assertEqual(self.lib.ConnectionDecodeSettings(output.raw, 24, c.byref(decoded)), 1)
            self.assertEqual(decoded.windowHours, hours)

    def test_invalid_settings_do_not_replace_current_connection(self):
        packets = [self.settings_packet(hours=0), self.settings_packet(hours=25),
                   self.settings_packet(address=b"localhost"), self.settings_packet(port=0),
                   self.settings_packet(sample=2), self.settings_packet(version=3)]
        good = self.settings_packet()
        packets += [good[:n] for n in range(24)] + [good + b"x"]
        for packet in packets:
            settings = Settings(b"10.0.0.1", 80, 1, 72)
            with self.subTest(packet=packet):
                self.assertEqual(self.lib.ConnectionDecodeSettings(packet, len(packet), c.byref(settings)), 0)
                self.assertEqual((settings.address, settings.port, settings.sample, settings.windowHours),
                                 (b"10.0.0.1", 80, 1, 72))


if __name__ == "__main__":
    unittest.main()
