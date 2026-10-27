"""
Rolling 24-hour database.

Maintains the current news window as described in plan section 5.
Articles older than 24 hours are automatically pruned.

Supports:
- Storing/retrieving articles
- Query by ID, feed, or time range
"""

import os
import json
import time
import threading
from datetime import datetime, timezone, timedelta
from typing import List, Dict, Optional, Tuple
from collections import OrderedDict
from dataclasses import replace
from storage import write_json
from pathlib import Path

from models import Article, ArticleIndexEntry, ArticleContent


class RollingDatabase:
    def __init__(self, data_dir: str = "rss_data", window_hours: float = 24):
        """
        Initialize the rolling database.

        Args:
            data_dir: Directory for persistent storage
            window_hours: Rolling window size in hours (default 24)
        """
        self.data_dir = data_dir
        self.window_hours = window_hours
        self._articles: Dict[str, Article] = {}
        self._lock = threading.Lock()

        os.makedirs(data_dir, exist_ok=True)
        try:
            self._load()
        except (ValueError, TypeError, KeyError, AttributeError) as error:
            self._articles.clear()
            source = Path(data_dir) / "articles.json"
            backup = source.with_name(f"articles.corrupt-{time.time_ns()}.json")
            source.rename(backup)
            print(f"[database] Damaged cache moved to {backup}; starting fresh ({error})")

    def add_articles(self, articles: List[Article]):
        """Add or update articles in the database."""
        with self._lock:
            for article in articles:
                previous = self._articles.get(article.id)
                if previous and previous.full_text and not article.full_text:
                    article = replace(article, full_text=previous.full_text, images=previous.images, links=previous.links, styles=previous.styles)
                self._articles[article.id] = article
            self._prune()
            self._save()

    def get_index(self) -> List[ArticleIndexEntry]:
        """Get the current 24-hour index (lightweight)."""
        with self._lock:
            self._prune()
            normalizer = __import__("normalizer", fromlist=["ArticleNormalizer"])
            normalizer = normalizer.ArticleNormalizer(self.window_hours)
            all_articles = list(self._articles.values())
            return normalizer.to_index_entries(all_articles)

    def get_article(self, article_id: str) -> Optional[ArticleContent]:
        """Get full article content by ID."""
        with self._lock:
            article = self._articles.get(article_id)
            if not article:
                return None
            from models import ArticleContent
            return ArticleContent(
                id=article.id,
                title=article.title,
                author=article.author,
                published=article.published,
                feed=article.feed,
                full_text=article.full_text,
                images=article.images,
                links=article.links,
                styles=article.styles,
            )

    def set_article_content(self, article_id: str, full_text: str, title: Optional[str] = None, images=None, links=None, styles=None):
        """Store (or refresh) the full body text for an article, fetched on demand."""
        with self._lock:
            article = self._articles.get(article_id)
            if not article:
                return False
            if full_text:
                article.full_text = full_text
                article.links = links or []
                article.styles = styles or []
            if images is not None:
                article.images = images
            elif full_text:
                for image in article.images:
                    image.pop("offset", None)
            if title:
                article.title = title
            self._save()
            return True

    def get_image(self, article_id: str, image_index: int = 0) -> Optional[dict]:
        """Get processed image data for an article."""
        with self._lock:
            article = self._articles.get(article_id)
            if not article or image_index < 0 or image_index >= len(article.images):
                return None
            return article.images[image_index]

    def get_original_url(self, article_id: str) -> Optional[str]:
        """Return the source URL for an article, or None if unknown/absent."""
        with self._lock:
            article = self._articles.get(article_id)
            return article.original_url if article else None

    def has_full_content(self, article_id: str) -> bool:
        """Return True if the article already has a fetched body."""
        with self._lock:
            article = self._articles.get(article_id)
            return bool(article and article.full_text)

    def get_articles_by_feed(self, feed_name: str) -> List[Article]:
        """Get articles filtered by feed name."""
        with self._lock:
            self._prune()
            return [
                a for a in self._articles.values()
                if a.feed == feed_name
            ]

    def get_last_sync_time(self) -> Optional[float]:
        """Get the timestamp of the last sync (for client protocol)."""
        meta_path = os.path.join(self.data_dir, "metadata.json")
        if os.path.exists(meta_path):
            with open(meta_path, "r") as f:
                meta = json.load(f)
                return meta.get("last_sync")
        return None

    def set_last_sync_time(self):
        """Update the last sync timestamp."""
        meta_path = os.path.join(self.data_dir, "metadata.json")
        meta = {"last_sync": time.time()}
        write_json(meta_path, meta)

    def _prune(self):
        """Remove articles older than the rolling window."""
        cutoff = datetime.now(timezone.utc) - timedelta(hours=self.window_hours)
        to_remove = []
        for article_id, article in self._articles.items():
            if article.published and article.published < cutoff:
                to_remove.append(article_id)
        for article_id in to_remove:
            del self._articles[article_id]
        if to_remove:
            self._save()

    def _load(self):
        """Load database from disk."""
        articles_path = os.path.join(self.data_dir, "articles.json")

        if os.path.exists(articles_path):
            with open(articles_path, "r") as f:
                data = json.load(f)
                if not isinstance(data, dict) or not isinstance(data.get("articles"), list):
                    raise ValueError("Cache must contain an articles array")
                for article_data in data["articles"]:
                    from models import Article
                    published = None
                    if article_data.get("published"):
                        published = datetime.fromisoformat(
                            article_data["published"].replace("Z", "+00:00")
                        )
                        if published.tzinfo is None:
                            raise ValueError("Cached publication date must have a time zone")
                    article = Article(
                        id=article_data["id"],
                        feed=article_data.get("feed"),
                        title=article_data.get("title", ""),
                        author=article_data.get("author"),
                        published=published,
                        original_url=article_data.get("original_url"),
                        summary=article_data.get("summary", ""),
                        full_text=article_data.get("full_text", ""),
                        images=article_data.get("images", []),
                        links=article_data.get("links", []),
                        styles=article_data.get("styles", []),
                    )
                    self._articles[article.id] = article

    def _save(self):
        """Persist database to disk."""
        articles_path = os.path.join(self.data_dir, "articles.json")

        articles_data = {
            "articles": [
                {
                    "id": a.id,
                    "feed": a.feed,
                    "title": a.title,
                    "author": a.author,
                    "published": a.published.isoformat() if a.published else None,
                    "original_url": a.original_url,
                    "summary": a.summary,
                    "full_text": a.full_text,
                    "images": a.images,
                    "links": a.links,
                    "styles": a.styles,
                }
                for a in self._articles.values()
            ]
        }
        write_json(articles_path, articles_data)
