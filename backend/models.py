"""
MacWire — Backend

Handles all modern Internet concerns:
- RSS/Atom fetching
- Article normalization
- Content extraction
- Image processing (Atkinson dithering)
- Rolling 24h database
- Custom protocol serving
"""

from datetime import datetime, timezone
from dataclasses import dataclass, field
from typing import List, Optional
import uuid


@dataclass
class Feed:
    """Configuration for a single RSS/Atom feed."""
    url: str
    name: str
    author: Optional[str] = None
    enabled: bool = True
    image_preferences: Optional[dict] = None
    extraction_preferences: Optional[dict] = None


@dataclass
class Article:
    """Normalized article representation (backend-internal)."""
    id: str = field(default_factory=lambda: str(uuid.uuid4())[:8])
    feed: Optional[str] = None
    title: str = ""
    author: Optional[str] = None
    published: Optional[datetime] = None
    original_url: Optional[str] = None
    summary: str = ""
    full_text: str = ""
    images: List[dict] = field(default_factory=list)  # list of dicts with url, width, height, format
    links: List[dict] = field(default_factory=list)
    styles: List[dict] = field(default_factory=list)


@dataclass
class ArticleIndexEntry:
    """Lightweight article metadata for the SE client index (low bandwidth)."""
    id: str
    published: Optional[datetime] = None
    feed: Optional[str] = None
    title: str = ""
    summary: str = ""
    has_full_content: bool = False
    image_count: int = 0


@dataclass
class ArticleContent:
    """Full article content for the SE client (fetched on demand)."""
    id: str
    title: str = ""
    author: Optional[str] = None
    published: Optional[datetime] = None
    feed: Optional[str] = None
    full_text: str = ""
    images: List[dict] = field(default_factory=list)  # may include dithered bitmap data
    links: List[dict] = field(default_factory=list)
    styles: List[dict] = field(default_factory=list)


@dataclass
class ImageData:
    """Processed 1-bit image ready for SE client."""
    width: int
    height: int
    row_bytes: int
    bitmap: bytes  # raw 1-bit bitmap data (PackBits compressed for transport)
    format: str = "bitmap"  # "bitmap" or "pict"
