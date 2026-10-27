"""
Feed configuration management.

Loads feeds from a JSON file and provides access to enabled feeds.
"""

import json
import os
from typing import List, Dict, Optional
from models import Feed
from storage import write_json
from urllib.parse import urlparse


DEFAULT_CONFIG_PATH = "feeds.json"
DISTRIBUTION_CONFIG_PATH = os.path.join(os.path.dirname(__file__), "feeds.example.json")


class FeedConfig:
    def __init__(self, config_path: str = DEFAULT_CONFIG_PATH):
        self.config_path = config_path
        self._feeds: Dict[str, Feed] = {}
        self.defaults_revision = None

    def load(self) -> List[Feed]:
        """Load feeds from config file. Returns list of Feed objects."""
        if not os.path.exists(self.config_path):
            print(f"[config] No config file found at {self.config_path}")
            print("[config] Creating configuration from the public starter feeds...")
            self._save_default()
            return self.get_enabled_feeds()

        with open(self.config_path, "r") as f:
            data = json.load(f)

        if not isinstance(data, dict) or not isinstance(data.get("feeds"), list):
            raise ValueError("Feed configuration must contain a 'feeds' array")
        revision = data.get("defaults_revision")
        if revision is not None and (type(revision) is not int or revision < 1):
            raise ValueError("defaults_revision must be a positive integer")
        self.defaults_revision = revision
        self._feeds = {}
        for feed_data in data["feeds"]:
            if not isinstance(feed_data, dict):
                raise ValueError("Each feed must be an object")
            self._validate_url(feed_data.get("url"))
            if not isinstance(feed_data.get("name", feed_data["url"]), str):
                raise ValueError("Feed name must be text")
            if not isinstance(feed_data.get("enabled", True), bool):
                raise ValueError("Feed enabled must be true or false")
            feed = Feed(
                url=feed_data["url"],
                name=feed_data.get("name", feed_data["url"]),
                author=feed_data.get("author"),
                enabled=feed_data.get("enabled", True),
                image_preferences=feed_data.get("image_preferences"),
                extraction_preferences=feed_data.get("extraction_preferences"),
            )
            self._feeds[feed.url] = feed

        return list(self._feeds.values())

    def compare_defaults(self):
        """Read-only comparison; private URLs and names never enter the result.

        A revision records which starter set seeded a configuration. It does
        not imply that the user's current subscriptions still match that set.
        """
        distribution = FeedConfig(DISTRIBUTION_CONFIG_PATH)
        defaults = distribution.load()
        exists = os.path.exists(self.config_path)
        local = {feed.url: feed for feed in self.load()} if exists else {}
        rows = []
        for default in defaults:
            feed = local.get(default.url)
            rows.append({
                "name": default.name,
                "status": "missing" if feed is None else "enabled" if feed.enabled else "disabled",
                "customized": feed is not None and (
                    feed.name != default.name or feed.author != default.author or
                    feed.image_preferences != default.image_preferences or
                    feed.extraction_preferences != default.extraction_preferences),
            })
        return {"distribution_revision": distribution.defaults_revision,
                "seeded_revision": self.defaults_revision if exists else None,
                "config_exists": exists, "feeds": rows,
                "personal_only_count": len(set(local) - {feed.url for feed in defaults})}

    def get_enabled_feeds(self) -> List[Feed]:
        """Return only enabled feeds."""
        if not self._feeds:
            self.load()
        return [f for f in self._feeds.values() if f.enabled]

    def get_feed_by_name(self, name: str) -> Optional[Feed]:
        """Look up a feed by name (not URL)."""
        if not self._feeds:
            self.load()
        for feed in self._feeds.values():
            if feed.name == name:
                return feed
        return None

    def add_feed(self, url: str, name: str = None, **kwargs) -> Feed:
        """Add a new feed to the configuration."""
        self._validate_url(url)
        self.load()
        if name is None:
            name = url
        feed = Feed(url=url, name=name, **kwargs)
        self._feeds[url] = feed
        self._save()
        return feed

    def remove_feed(self, url: str) -> bool:
        """Remove a feed by URL. Returns True if found and removed."""
        self.load()
        if url in self._feeds:
            del self._feeds[url]
            self._save()
            return True
        return False

    def _save(self):
        """Save current feeds to config file."""
        data = {
            "feeds": [
                {
                    "url": feed.url,
                    "name": feed.name,
                    "author": feed.author,
                    "enabled": feed.enabled,
                    "image_preferences": feed.image_preferences,
                    "extraction_preferences": feed.extraction_preferences,
                }
                for feed in self._feeds.values()
            ]
        }
        if self.defaults_revision is not None:
            data["defaults_revision"] = self.defaults_revision
        write_json(self.config_path, data)

    def _save_default(self):
        """Create a default config with a few example feeds."""
        with open(DISTRIBUTION_CONFIG_PATH, encoding="utf-8") as file:
            write_json(self.config_path, json.load(file))
        self.load()

    @staticmethod
    def _validate_url(url):
        if not isinstance(url, str):
            raise ValueError("Each feed needs an HTTP or HTTPS URL")
        parsed = urlparse(url)
        if parsed.scheme not in ("http", "https") or not parsed.hostname:
            raise ValueError("Feed URLs must use HTTP or HTTPS")
