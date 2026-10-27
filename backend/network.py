"""Bound upstream downloads before parsing or image decoding."""
from urllib.parse import urlparse

MAX_FEED_BYTES = 4 * 1024 * 1024
MAX_ARTICLE_BYTES = 4 * 1024 * 1024
MAX_IMAGE_BYTES = 16 * 1024 * 1024


class DownloadLimitError(ValueError):
    """Upstream content exceeds the bounded download size."""


def read_limited(response, limit):
    data = response.read(limit + 1)
    if len(data) > limit:
        raise DownloadLimitError(f"Download exceeds {limit // 1024} KiB limit")
    return data


def validate_http_url(url):
    parsed = urlparse(url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname:
        raise ValueError("Only HTTP and HTTPS downloads are supported")
