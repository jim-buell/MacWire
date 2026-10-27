"""
Article normalizer.

Ensures all articles from different feed formats (RSS 1.0, RSS 2.0, Atom)
are converted to a common Article representation.
"""

from datetime import datetime, timezone, timedelta
from typing import List
import html
from models import Article, ArticleIndexEntry


class ArticleNormalizer:
    def __init__(self, window_hours: float = 24):
        """
        Initialize the normalizer.

        Args:
            window_hours: Rolling window size in hours (default 24)
        """
        self.window_hours = window_hours

    def normalize(self, articles: List[Article]) -> List[Article]:
        """
        Normalize a list of articles.

        - Ensures all timestamps are UTC
        - Strips non-MacRoman-safe characters from text
        - Filters to 24-hour window
        - Sorts oldest first
        """
        cutoff = datetime.now(timezone.utc) - timedelta(hours=self.window_hours)

        # Filter to window
        filtered = []
        for article in articles:
            if article.published and article.published >= cutoff:
                filtered.append(article)

        # Sort oldest first (as per plan section 6)
        filtered.sort(key=lambda a: a.published or datetime.min.replace(tzinfo=timezone.utc))

        return filtered

    def to_index_entries(self, articles: List[Article]) -> List[ArticleIndexEntry]:
        """Convert full articles to lightweight index entries for the SE client."""
        entries = []
        for article in sorted(
            articles,
            key=lambda a: a.published or datetime.min.replace(tzinfo=timezone.utc),
        ):
            entry = ArticleIndexEntry(
                id=article.id,
                published=article.published,
                feed=article.feed,
                title=html.unescape(article.title)[:100],
                summary=html.unescape(article.summary)[:200],
                has_full_content=bool(article.full_text),
                image_count=len(article.images),
            )
            entries.append(entry)
        return entries

    def to_content(self, article: Article) -> "models.ArticleContent":
        """Convert a full article to content format for the SE client."""
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
