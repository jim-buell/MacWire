"""Exercise the portable 68K decoder with packets produced by the backend."""

import ctypes
from datetime import datetime, timezone
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "backend"))

from models import Article as BackendArticle, ArticleContent, ArticleIndexEntry, ImageData  # noqa: E402
from normalizer import ArticleNormalizer  # noqa: E402
from protocol import encode_refresh, encode_article, encode_index, encode_feeds, encode_dates, local_timezone, encode_image, _client_text  # noqa: E402
from image_processor import ImageProcessor, _pack_bits


class Image(ctypes.Structure):
    _fields_ = [("id", ctypes.c_char * 9), ("index", ctypes.c_ushort),
                ("width", ctypes.c_ushort), ("height", ctypes.c_ushort),
                ("rowBytes", ctypes.c_ushort)]


class IndexEntry(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_char * 9),
        ("feed", ctypes.c_char * 33),
        ("title", ctypes.c_char * 101),
        ("summary", ctypes.c_char * 201),
        ("published", ctypes.c_double),
        ("imageCount", ctypes.c_ushort),
        ("fullContentAvailable", ctypes.c_ubyte),
    ]


class Link(ctypes.Structure):
    _fields_ = [('start', ctypes.c_ushort), ('length', ctypes.c_ushort), ('urlOffset', ctypes.c_ushort)]

class Style(ctypes.Structure):
    _fields_ = [('start', ctypes.c_ushort), ('length', ctypes.c_ushort), ('flags', ctypes.c_ushort)]

class Links(ctypes.Structure):
    _fields_ = [('count', ctypes.c_ushort), ('urlBytes', ctypes.c_ushort), ('entries', Link * 48), ('styleCount', ctypes.c_ushort), ('styles', Style * 256), ('urls', ctypes.c_char * 8192)]


class Article(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_char * 9),
        ("feed", ctypes.c_char * 33),
        ("title", ctypes.c_char * 512),
        ("body", ctypes.c_char * 10000),
        ("author", ctypes.c_char * 256),
        ("published", ctypes.c_double),
        ("imageCount", ctypes.c_ushort),
        ("imageOffsets", ctypes.c_ushort * 5),
        ("links", Links),
    ]


class Refresh(ctypes.Structure):
    _fields_ = [('job', ctypes.c_char * 9), ('running', ctypes.c_ubyte), ('failures', ctypes.c_ushort)]


class DateEntry(ctypes.Structure):
    _fields_ = [("id", ctypes.c_char * 9), ("time", ctypes.c_char * 24), ("date", ctypes.c_char * 32)]


class WireTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / "libwire.so"
        subprocess.run(
            ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
             "-DRSS_DEMO_TRANSPORT=1", str(ROOT / "client/Wire.c"),
             str(ROOT / "client/DemoTransport.c"), "-o", str(library)],
            check=True,
        )
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.WireDecodeDates.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(DateEntry), ctypes.POINTER(ctypes.c_short)]
        cls.lib.WireDecodeIndex.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                             ctypes.POINTER(IndexEntry), ctypes.c_size_t,
                                             ctypes.POINTER(ctypes.c_size_t)]
        cls.lib.WireDecodeIndex.restype = ctypes.c_int
        cls.lib.WireDecodeArticle.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                               ctypes.POINTER(Article)]
        cls.lib.WireDecodeArticle.restype = ctypes.c_int
        cls.lib.WireDecodeRefresh.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Refresh)]
        cls.lib.WireDecodeRefresh.restype = ctypes.c_int
        cls.lib.DemoResponse.argtypes = [ctypes.c_char_p, ctypes.c_void_p,
                                          ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong)]
        cls.lib.DemoResponse.restype = ctypes.c_short
        cls.lib.WireDecodeImage.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                            ctypes.POINTER(Image), ctypes.c_void_p, ctypes.c_size_t]
        cls.lib.WireDecodeFeeds.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                            ctypes.POINTER(ctypes.c_short)]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def decode_index(self, data):
        entries = (IndexEntry * 50)()
        count = ctypes.c_size_t(999)
        result = self.lib.WireDecodeIndex(data, len(data), entries, 50,
                                           ctypes.byref(count))
        return result, count.value, entries

    def test_rich_styles_round_trip_without_link_extension(self):
        body='A bold word and code.'
        packet=encode_article(ArticleContent(id='12345678',full_text=body,styles=[{'start':2,'length':4,'flags':3},{'start':16,'length':4,'flags':4}]))
        article=Article()
        self.assertEqual(self.lib.WireDecodeArticle(packet,len(packet),ctypes.byref(article)),0)
        self.assertEqual(article.links.count,0)
        self.assertEqual(article.links.styleCount,2)
        self.assertEqual([(s.start,s.length,s.flags) for s in article.links.styles[:2]],[(2,4,3),(16,4,4)])
        legacy=encode_article(ArticleContent(id='12345678',full_text=body))
        self.assertEqual(self.lib.WireDecodeArticle(legacy,len(legacy),ctypes.byref(article)),0)
        self.assertEqual(article.links.styleCount,0)

    def test_rejects_malformed_style_extensions(self):
        import struct
        base=encode_article(ArticleContent(id='12345678',full_text='word'))[3:]
        for extension in [b'STYL', b'STYL'+struct.pack('>H',257), b'STYL'+struct.pack('>HHHH',1,0,5,1), b'STYL'+struct.pack('>HHHH',1,0,4,512), b'STYL'+struct.pack('>HHHH',1,0,0,1)]:
            payload=base+extension
            packet=struct.pack('>BH',3,len(payload))+payload
            self.assertNotEqual(self.lib.WireDecodeArticle(packet,len(packet),ctypes.byref(Article())),0)

    def test_refresh_job_decoder_accepts_running_and_done_and_rejects_bad_packets(self):
        decoded = Refresh()
        for running, failures in ((True, 0), (False, 3)):
            packet = encode_refresh('1234abcd', running, failures)
            self.assertEqual(self.lib.WireDecodeRefresh(packet, len(packet), ctypes.byref(decoded)), 0)
            self.assertEqual((decoded.job, decoded.running, decoded.failures), (b'1234abcd', running, failures))
        for broken in (packet[:-1], packet + b'x', b'\x05' + packet[1:], packet[:3] + b'zzzzzzzz' + packet[11:], packet[:11] + b'\x02' + packet[12:]):
            self.assertNotEqual(self.lib.WireDecodeRefresh(broken, len(broken), ctypes.byref(decoded)), 0)

    def test_author_decode_retains_macroman_and_omits_missing_name(self):
        decoded = Article()
        for author in ('José Example', None):
            packet = encode_article(ArticleContent(id='12345678', author=author, full_text='Body'))
            self.assertEqual(self.lib.WireDecodeArticle(packet, len(packet), ctypes.byref(decoded)), 0)
            self.assertEqual(decoded.author, _client_text(author, 255))

    def test_bounded_index_preserves_order_and_availability(self):
        now = datetime.now(timezone.utc)
        source = [ArticleIndexEntry(id=f"{i:08x}", published=now,
                                    feed="Feed" * 20, title="T" * 140,
                                    summary="S" * 250,
                                    has_full_content=True)
                  for i in range(61)]
        data = encode_index(source)
        self.assertLess(len(data), 32768)
        result, count, entries = self.decode_index(data)
        self.assertEqual((result, count), (0, 50))
        self.assertEqual(entries[0].id, b"0000000b")
        self.assertEqual(entries[49].id, b"0000003c")
        self.assertEqual(entries[0].fullContentAvailable, 1)
        self.assertEqual(len(entries[0].title), 100)
        self.assertEqual(len(entries[0].summary), 200)

    def test_server_sorts_before_applying_the_fifty_article_cap(self):
        now = datetime.now(timezone.utc)
        articles = [BackendArticle(id=f"{i:08x}", published=now.replace(minute=i),
                                   title=str(i)) for i in range(3)]
        index = ArticleNormalizer().to_index_entries(articles[::-1])
        self.assertEqual([entry.id for entry in index],
                         ["00000000", "00000001", "00000002"])

    def test_rejects_truncated_and_oversized_index(self):
        data = encode_index([ArticleIndexEntry(id="12345678", title="Hello")])
        self.assertNotEqual(self.decode_index(data[:-1])[0], 0)
        self.assertNotEqual(self.decode_index(data + b"x")[0], 0)
        malformed = bytearray(data)
        malformed[3:5] = b"\x00\x33"  # Count exceeds the SE cache.
        self.assertNotEqual(self.decode_index(bytes(malformed))[0], 0)

    def test_inline_image_positions_decode_and_reject_out_of_range_extensions(self):
        content = ArticleContent(id='12345678', full_text='Before.\n\nAfter.', images=[{'url': 'https://example.com/a.jpg', 'offset': 9}, {'url': 'https://example.com/b.jpg'}])
        packet = encode_article(content)
        decoded = Article()
        self.assertEqual(self.lib.WireDecodeArticle(packet, len(packet), ctypes.byref(decoded)), 0)
        self.assertEqual(list(decoded.imageOffsets)[:2], [9, 65535])
        bad = packet[:-4] + b'\xff\xfe' + packet[-2:]
        self.assertNotEqual(self.lib.WireDecodeArticle(bad, len(bad), ctypes.byref(decoded)), 0)
        legacy = encode_article(ArticleContent(id='12345678', full_text='Body', images=[{'url': 'https://example.com/a.jpg'}]))
        self.assertEqual(self.lib.WireDecodeArticle(legacy, len(legacy), ctypes.byref(decoded)), 0)
        self.assertEqual(decoded.imageOffsets[0], 65535)

    def test_article_body_and_metadata_are_decoded(self):
        source = ArticleContent(id="12345678", title="Headline", feed="Source",
                                published=datetime.now(timezone.utc),
                                full_text="Body " * 2500,
                                images=[{"url": "https://example.com/a.png",
                                         "width": 320, "height": 200,
                                         "format": "png"}])
        data = encode_article(source)
        article = Article()
        self.assertEqual(self.lib.WireDecodeArticle(data, len(data),
                                                     ctypes.byref(article)), 0)
        self.assertEqual(article.id, b"12345678")
        self.assertEqual(article.title, b"Headline")
        self.assertEqual(article.feed, b"Source")
        self.assertEqual(len(article.body), 9999)
        self.assertEqual(article.imageCount, 1)
        self.assertNotEqual(self.lib.WireDecodeArticle(data[:-1], len(data) - 1,
                                                        ctypes.byref(article)), 0)

    def test_external_links_decode_with_image_positions_and_reject_bad_spans(self):
        import struct
        source=ArticleContent(id='12345678',full_text='See the linked story.',
            links=[{'start':8,'length':6,'url':'https://example.com/story?q=1'}],
            styles=[{'start':8,'length':6,'flags':1}],
            images=[{'url':'https://example.com/photo','offset':8}])
        data=encode_article(source); article=Article()
        self.assertEqual(self.lib.WireDecodeArticle(data,len(data),ctypes.byref(article)),0)
        self.assertEqual(article.links.count,1)
        self.assertEqual(article.links.styleCount,1)
        self.assertEqual(article.links.styles[0].start,8)
        self.assertEqual(article.imageOffsets[0],8)
        entry=article.links.entries[0]
        self.assertEqual((entry.start,entry.length),(8,6))
        self.assertEqual(article.links.urls[entry.urlOffset:],'https://example.com/story?q=1'.encode())
        broken=bytearray(data); offset=data.index(b'LINK')+6
        broken[offset:offset+2]=struct.pack('>H',9999)
        self.assertNotEqual(self.lib.WireDecodeArticle(bytes(broken),len(broken),ctypes.byref(article)),0)
        self.assertNotEqual(self.lib.WireDecodeArticle(data[:-1],len(data)-1,ctypes.byref(article)),0)

    def test_demo_transport_routes_index_and_selected_article(self):
        buffer = ctypes.create_string_buffer(32768)
        length = ctypes.c_ulong()
        self.assertEqual(self.lib.DemoResponse(b"GET /sync?last_sync=0 HTTP/1.0",
                                                 buffer, len(buffer),
                                                 ctypes.byref(length)), 0)
        result, count, entries = self.decode_index(buffer.raw[:length.value])
        self.assertEqual((result, count), (0, 3))
        self.assertEqual(entries[0].id, b"00000001")
        self.assertEqual(self.lib.DemoResponse(b"GET /article/00000001 HTTP/1.0",
                                                 buffer, len(buffer),
                                                 ctypes.byref(length)), 0)
        article = Article()
        self.assertEqual(self.lib.WireDecodeArticle(buffer, length.value,
                                                     ctypes.byref(article)), 0)
        self.assertEqual(article.id, entries[0].id)
        self.assertIn(b"index arrives first", article.body)

    def test_macroman_punctuation_accents_and_full_reader_title(self):
        text = "“Café”—naïve, résumé…\u00a0č → test"
        self.assertNotIn(b"?", _client_text(text, 200))
        self.assertEqual(_client_text(text, 200).decode("mac_roman"),
                         "“Café”—naïve, résumé… c -> test")
        self.assertEqual(_client_text("&ldquo;Mac&rdquo; &hellip; &amp; café", 200).decode("mac_roman"),
                         "“Mac” … & café")
        title = "A long headline with descenders gyp " * 8
        source = ArticleContent(id="12345678", title=title, full_text=text,
                                published=datetime(2026, 9, 29, 18, 30, tzinfo=timezone.utc))
        packet = encode_article(source)
        article = Article()
        self.assertEqual(self.lib.WireDecodeArticle(packet, len(packet), ctypes.byref(article)), 0)
        self.assertEqual(article.title.decode("mac_roman"), title)
        self.assertEqual(article.body.decode("mac_roman"), _client_text(text, 9999).decode("mac_roman"))
        self.assertEqual(article.published, source.published.timestamp())

    def test_local_dates_use_chicago_dst_rules_and_keep_utc_timestamps(self):
        source = [ArticleIndexEntry(id="12345678", published=datetime(2026, 9, 30, 1, 5, tzinfo=timezone.utc)),
                  ArticleIndexEntry(id="87654321", published=datetime(2026, 1, 30, 1, 5, tzinfo=timezone.utc))]
        packet = encode_dates(source, local_timezone("America/Chicago"), compact=True)
        entries = (DateEntry * 50)(); count = ctypes.c_short()
        self.assertEqual(self.lib.WireDecodeDates(packet, len(packet), entries, ctypes.byref(count)), 0)
        self.assertEqual(count.value, 2)
        self.assertEqual((entries[0].time, entries[0].date), (b"Sep 29, 8:05 PM", b"Sep 29, 2026 8:05 PM"))
        self.assertEqual((entries[1].time, entries[1].date), (b"Jan 29, 7:05 PM", b"Jan 29, 2026 7:05 PM"))
        self.assertNotEqual(self.lib.WireDecodeDates(packet[:-1], len(packet)-1, entries, ctypes.byref(count)), 0)
        self.assertNotEqual(self.lib.WireDecodeDates(packet+b"x", len(packet)+1, entries, ctypes.byref(count)), 0)
        _, _, raw = self.decode_index(encode_index(source))
        self.assertEqual(raw[0].published, source[0].published.timestamp())

    def test_spacing_is_normalized_without_destroying_paragraphs_or_quotes(self):
        text = "Here’s  how\u00a0\u00a0it works.  Really?\n\n  A   new paragraph."
        out = _client_text(text, 9999).decode("mac_roman")
        self.assertEqual(out, "Here’s how it works. Really?\n\nA new paragraph.")

    def test_feed_names_boundaries_and_metacharacters(self):
        names = (ctypes.c_char * 33 * 64)()
        count = ctypes.c_short()
        packet = encode_feeds(["All sorts / news", "Café & Co", "Café & Co"])
        self.assertEqual(self.lib.WireDecodeFeeds(packet, len(packet), names, ctypes.byref(count)), 0)
        self.assertEqual(count.value, 2)
        self.assertEqual([names[i].value.decode("mac_roman") for i in range(count.value)],
                         ["All sorts / news", "Café & Co"])
        self.assertNotEqual(self.lib.WireDecodeFeeds(packet[:-1], len(packet)-1, names, ctypes.byref(count)), 0)

    def decode_image(self, packet, capacity=8000):
        image = Image(); pixels = ctypes.create_string_buffer(capacity)
        result = self.lib.WireDecodeImage(packet, len(packet), ctypes.byref(image), pixels, capacity)
        return result, image, pixels.raw[:image.rowBytes * image.height]

    def test_jpeg_photo_is_resized_before_dithering_and_decoded_by_client(self):
        import io
        from PIL import Image as PILImage
        image = PILImage.new("RGB", (640, 160))
        image.putdata([(x % 256, y % 256, (x+y) % 256) for y in range(160) for x in range(640)])
        stream = io.BytesIO(); image.save(stream, format="JPEG")
        processed = ImageProcessor().process_image(stream.getvalue(), max_width=188)
        self.assertEqual((processed.width, processed.height), (188, 47))
        result, decoded, pixels = self.decode_image(encode_image(processed, "12345678", 0))
        self.assertEqual(result, 0)
        self.assertEqual(len(pixels), decoded.rowBytes * 47)
        self.assertGreater(len(set(pixels)), 8)  # Real dithered detail, not an empty box.

    def test_packbits_pixels_and_odd_width_images(self):
        raw = bytes([0, 255, 0xAA, 0x55, 0, 1])
        packed = _pack_bits(raw[:3]) + _pack_bits(raw[3:])
        packet = encode_image(ImageData(19, 2, 3, packed), "12345678", 0)
        result, image, pixels = self.decode_image(packet)
        self.assertEqual((result, image.width, image.height, image.rowBytes), (0, 19, 2, 3))
        self.assertEqual(pixels, raw)

    def test_rejects_corrupt_image_runs_dimensions_and_capacity(self):
        for image in (ImageData(16, 1, 2, b"\x02abc"),
                      ImageData(16, 1, 2, b"\x00a"),
                      ImageData(16, 1, 2, b"\xffaX"),
                      ImageData(321, 1, 41, b"\xd8a"),
                      ImageData(16, 201, 2, b"\xffa" * 201),
                      ImageData(16, 1, 3, b"\xfea")):
            self.assertNotEqual(self.decode_image(encode_image(image, "12345678", 0))[0], 0)
        valid = encode_image(ImageData(16, 1, 2, b"\xffa"), "12345678", 0)
        self.assertNotEqual(self.decode_image(valid, capacity=1)[0], 0)
        self.assertNotEqual(self.decode_image(valid[:-1])[0], 0)

    def test_backend_image_pipeline_reaches_native_bitmap_decoder(self):
        from PIL import Image as PillowImage
        import io
        png = io.BytesIO()
        PillowImage.new("RGB", (24, 10), "black").save(png, format="PNG")
        processed = ImageProcessor().process_image(png.getvalue())
        result, image, pixels = self.decode_image(encode_image(processed, "12345678", 0))
        self.assertEqual((result, image.width, image.height), (0, 24, 10))
        self.assertEqual(pixels, b"\xff" * 30)


if __name__ == "__main__":
    unittest.main()
