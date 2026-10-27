"""Oversized images retry a known resizing service once, without relaxing limits."""
import io
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
from urllib.parse import urlsplit, parse_qs

sys.path.insert(0, str(Path(__file__).resolve().parent))
from PIL import Image
from image_processor import ImageProcessor, ImageSizeError
from network import DownloadLimitError

VERGE = 'https://platform.theverge.com/wp-content/uploads/sites/2/photo.jpeg?quality=90&strip=all&crop=0,0,100,100'


def png():
    output = io.BytesIO()
    Image.new('RGB', (32, 16), 'white').save(output, 'PNG')
    return output.getvalue()


class ImageFallbackTests(unittest.TestCase):
    def test_pixel_rejection_retries_smaller_and_caches_under_original_url(self):
        processor = ImageProcessor()
        real_open = Image.open
        def open_image(data):
            if data.getvalue() == b'oversized':
                raise Image.DecompressionBombWarning('Image too large')
            return real_open(data)
        with patch('urllib.request.urlopen', side_effect=[io.BytesIO(b'oversized'), io.BytesIO(png())]) as download, patch('PIL.Image.open', side_effect=open_image):
            result = processor.process_from_url(VERGE, max_width=188)
            again = processor.process_from_url(VERGE, max_width=188)
        self.assertIsNotNone(result)
        self.assertIs(again, result)
        self.assertEqual(download.call_count, 2)
        query = parse_qs(urlsplit(download.call_args_list[1].args[0].full_url).query)
        self.assertEqual(query, {'quality': ['90'], 'strip': ['all'], 'crop': ['0,0,100,100'], 'w': ['376']})

    def test_valid_image_does_not_retry(self):
        with patch('urllib.request.urlopen', return_value=io.BytesIO(png())) as download:
            self.assertIsNotNone(ImageProcessor().process_from_url(VERGE))
        self.assertEqual(download.call_count, 1)

    def test_download_size_rejection_also_retries(self):
        processor = ImageProcessor()
        with patch('image_processor.read_limited', side_effect=[DownloadLimitError('too much data'), png()]), patch('urllib.request.urlopen', side_effect=[io.BytesIO(b''), io.BytesIO(b'')]) as download:
            self.assertIsNotNone(processor.process_from_url(VERGE))
        self.assertEqual(download.call_count, 2)

    def test_rejected_fallback_is_not_retried_or_cached(self):
        processor = ImageProcessor()
        with patch.object(processor, '_download_and_process', side_effect=ImageSizeError('too many pixels')) as download:
            self.assertIsNone(processor.process_from_url(VERGE))
        self.assertEqual(download.call_count, 2)
        self.assertEqual(len(processor._cache), 0)

    def test_unknown_hosts_and_other_verge_paths_are_not_rewritten(self):
        for url in ('https://example.com/photo.jpg', 'https://platform.theverge.com.evil.example/wp-content/uploads/a.jpg', 'https://platform.theverge.com/other/a.jpg'):
            processor = ImageProcessor()
            with patch.object(processor, '_download_and_process', side_effect=ImageSizeError('too large')) as download:
                self.assertIsNone(processor.process_from_url(url))
            self.assertEqual(download.call_count, 1)

    def test_unrelated_download_and_decode_errors_do_not_retry(self):
        for error in (OSError('network failure'), ValueError('broken image')):
            processor = ImageProcessor()
            with patch.object(processor, '_download_and_process', side_effect=error) as download:
                self.assertIsNone(processor.process_from_url(VERGE))
            self.assertEqual(download.call_count, 1)

    def test_conflicting_sizes_are_removed_and_crop_is_preserved(self):
        processor = ImageProcessor()
        smaller = processor._smaller_image_url(VERGE+'&w=9000&h=9000&fit=9000,9000&resize=8000,8000&lb=9000,9000', 'article', None)
        self.assertEqual(urlsplit(smaller).hostname, 'platform.theverge.com')
        self.assertEqual(parse_qs(urlsplit(smaller).query), {'quality': ['90'], 'strip': ['all'], 'crop': ['0,0,100,100'], 'w': ['640']})
        for host in ('i0.wp.com', 'i1.wp.com', 'i2.wp.com'):
            url = processor._smaller_image_url(f'https://{host}/example.com/a.jpg', 'thumb', None)
            self.assertEqual(parse_qs(urlsplit(url).query), {'w': ['240']})

    def test_both_pillow_bomb_errors_remain_size_errors(self):
        for error in (Image.DecompressionBombWarning('too large'), Image.DecompressionBombError('much too large')):
            with patch('PIL.Image.open', side_effect=error):
                with self.assertRaises(ImageSizeError):
                    ImageProcessor().process_image(b'not decoded')


if __name__ == '__main__':
    unittest.main()
