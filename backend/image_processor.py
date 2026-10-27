"""
Image processor for MacWire.

Pipeline:
  Original image -> Crop/resize -> Grayscale -> Atkinson dithering -> 1-bit bitmap

Image algorithms are implemented locally; see IMAGE_ALGORITHMS.md for provenance.

Outputs raw 1-bit bitmap data (width, height, row_bytes, bitmap) ready
for transport to the SE client, optionally PackBits-compressed.
"""

import io
import threading
from network import read_limited, validate_http_url, MAX_IMAGE_BYTES, DownloadLimitError
from urllib.parse import urlsplit, urlunsplit, parse_qsl, urlencode
from collections import OrderedDict
from typing import Dict, List, Optional, Tuple
from models import ImageData


from image_algorithms import (
    grayscale_rows as to_greyscale,
    atkinson_rows as dither,
    packbits_scanline as _pack_bits,
)


class ImageSizeError(ValueError):
    """The source image exceeds a pixel limit before decoding its pixels."""


class ImageProcessor:
    """
    Process images for System 7 display.

    Pipeline:
      1. Download image from URL
      2. Resize to target dimensions
      3. Convert to grayscale
      4. Apply Atkinson dithering
      5. Pack into 1-bit bitmap (optionally PackBits compressed)
    """

    # Target dimensions for article images (plan section 14)
    ARTICLE_WIDTH = 320
    ARTICLE_HEIGHT = 200
    THUMB_WIDTH = 120
    THUMB_HEIGHT = 80

    def __init__(self, article_size: tuple = None, thumb_size: tuple = None, cache_size: int = 64):
        if article_size:
            self.ARTICLE_WIDTH, self.ARTICLE_HEIGHT = article_size
        if thumb_size:
            self.THUMB_WIDTH, self.THUMB_HEIGHT = thumb_size
        self._cache: "OrderedDict[Tuple[str, str, Optional[int]], ImageData]" = OrderedDict()
        self._cache_max = cache_size
        self._cache_lock = threading.Lock()

    def process_image(self, image_data: bytes, variant: str = "article", max_width: int = None) -> ImageData:
        """
        Process a raw image (bytes) into a 1-bit bitmap.

        Args:
            image_data: Raw image bytes (JPEG, PNG, WebP, etc.)
            variant: "article" or "thumb" for size selection

        Returns:
            ImageData with width, height, row_bytes, and bitmap
        """
        import io
        import warnings
        from PIL import Image, ImageOps

        # Determine target size
        if variant == "thumb":
            target_w, target_h = self.THUMB_WIDTH, self.THUMB_HEIGHT
        else:
            target_w, target_h = self.ARTICLE_WIDTH, self.ARTICLE_HEIGHT

        if max_width is not None:
            target_w = min(target_w, max(1, max_width))

        # Decode with Pillow so JPEG/PNG/WebP all work (pypng only reads PNG).
        try:
            with warnings.catch_warnings():
                warnings.simplefilter("error", Image.DecompressionBombWarning)
                image = Image.open(io.BytesIO(image_data))
        except (Image.DecompressionBombWarning, Image.DecompressionBombError) as error:
            raise ImageSizeError(str(error)) from error
        if image.width * image.height > 20_000_000:
            image.close()
            raise ImageSizeError("Image exceeds 20 million pixel limit")
        image = ImageOps.exif_transpose(image)
        if image.mode in ("RGBA", "LA") or "transparency" in image.info:
            rgba = image.convert("RGBA")
            background = Image.new("RGBA", rgba.size, "white")
            image = Image.alpha_composite(background, rgba)
        image = image.convert("RGB")
        image.thumbnail((target_w, target_h), Image.Resampling.LANCZOS)
        target_w, target_h = image.size

        # Flatten RGB pixels into rows for the local grayscale stage.
        pixels = image.load()
        color_rows = [
            [channel for x in range(target_w) for channel in pixels[x, y]]
            for y in range(target_h)
        ]

        # Convert to grayscale
        grey_rows = to_greyscale(color_rows, alpha=False)

        # Apply Atkinson dithering
        dithered = dither(grey_rows)

        # Pack into 1-bit bitmap (MSB first, black == bit set).
        row_bytes = (target_w + 7) // 8
        bitmap = bytearray()
        for row in dithered:
            for x in range(0, target_w, 8):
                byte = 0
                for bit in range(min(8, target_w - x)):
                    if row[x + bit] == 0:  # BLACK
                        byte |= 0x80 >> bit
                bitmap.append(byte)

        # PackBits-compress each scanline separately; packets never cross rows.
        packed = bytearray()
        for i in range(0, len(bitmap), row_bytes):
            packed += _pack_bits(bytes(bitmap[i : i + row_bytes]))

        return ImageData(
            width=target_w,
            height=target_h,
            row_bytes=row_bytes,
            bitmap=packed,
            format="bitmap",
        )

    def _smaller_image_url(self, url: str, variant: str, max_width: int) -> Optional[str]:
        """Retry only services with known WordPress/Photon sizing parameters.

        Keep the publisher's crop and other options, replacing only conflicting
        size controls. Two times the display width leaves detail for dithering.
        """
        parsed = urlsplit(url)
        if parsed.hostname not in {"platform.theverge.com", "i0.wp.com", "i1.wp.com", "i2.wp.com"}:
            return None
        if parsed.hostname == "platform.theverge.com" and not parsed.path.startswith("/wp-content/uploads/"):
            return None
        width = self.THUMB_WIDTH if variant == "thumb" else self.ARTICLE_WIDTH
        if max_width is not None:
            width = min(width, max(1, max_width))
        width = min(1024, max(1, width * 2))
        query = [(key, value) for key, value in parse_qsl(parsed.query, keep_blank_values=True)
                 if key not in {"w", "h", "fit", "resize", "lb"}]
        query.append(("w", str(width)))
        smaller = urlunsplit(parsed._replace(query=urlencode(query)))
        return smaller if smaller != url else None

    def _download_and_process(self, url: str, variant: str, max_width: int) -> ImageData:
        import urllib.request
        validate_http_url(url)
        req = urllib.request.Request(url, headers={"User-Agent": "System7RSS/1.0"})
        with urllib.request.urlopen(req, timeout=10) as response:
            image_data = read_limited(response, MAX_IMAGE_BYTES)
        return self.process_image(image_data, variant, max_width)

    def process_from_url(self, url: str, variant: str = "article", max_width: int = None) -> Optional[ImageData]:
        """
        Download and process an image from a URL.

        Args:
            url: Image URL
            variant: "article" or "thumb"

        Returns:
            ImageData or None if processing fails
        """
        cache_key = (url, variant, max_width)
        with self._cache_lock:
            if cache_key in self._cache:
                self._cache.move_to_end(cache_key)
                return self._cache[cache_key]

        try:
            try:
                result = self._download_and_process(url, variant, max_width)
            except (ImageSizeError, DownloadLimitError):
                smaller = self._smaller_image_url(url, variant, max_width)
                if not smaller:
                    raise
                print(f"[image_processor] Oversized image; retrying smaller version: {smaller}")
                result = self._download_and_process(smaller, variant, max_width)
        except Exception as e:
            print(f"[image_processor] Failed to process {url}: {e}")
            return None

        if result is not None:
            with self._cache_lock:
                self._cache[cache_key] = result
                self._cache.move_to_end(cache_key)
                if len(self._cache) > self._cache_max:
                    self._cache.popitem(last=False)

        return result

    def clear_cache(self) -> None:
        """Drop all cached processed images."""
        with self._cache_lock:
            self._cache.clear()
