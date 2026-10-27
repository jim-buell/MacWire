#!/usr/bin/env python3
"""
MacWire Helper — Backend Entry Point

Starts the RSS feed backend that serves a custom protocol
to a System 7 Macintosh SE client.

Usage:
    python main.py [--port PORT] [--data-dir DIR] [--config CONFIG]
"""

import argparse
import math
import sys
import os
import errno
from pathlib import Path

VERSION = "1.0"

def default_home():
    if sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support"
        current, legacy = base / "MacWire Helper", base / "TickerTape"
    else:
        base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
        current, legacy = base / "macwire-helper", base / "tickertape"
    # Keep existing development feeds and cache together without moving files.
    return legacy if not current.exists() and legacy.exists() else current

# Add backend directory to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))



def run():
    parser = argparse.ArgumentParser(
        description="MacWire Helper — RSS server for the classic MacWire application"
    )
    parser.add_argument("--version", action="version", version=f"MacWire Helper {VERSION}")
    parser.add_argument("--host", default="0.0.0.0", help="IPv4 interface to bind (default: all interfaces)")
    parser.add_argument(
        "--port", type=int, default=8080,
        help="Port to listen on (default 8080)"
    )
    parser.add_argument(
        "--data-dir", type=str, default=str(default_home() / "cache"),
        help="Cache directory (default: user application data)"
    )
    parser.add_argument(
        "--config", type=str, default=str(default_home() / "feeds.json"),
        help="Feed configuration (default: user application data)"
    )
    parser.add_argument(
        "--fetch-interval", type=int, default=3600,
        help="Seconds between feed fetches (default 3600)"
    )
    parser.add_argument(
        "--fetch-timeout", type=int, default=15,
        help="Seconds to wait for a feed response before giving up (default 15)"
    )
    parser.add_argument(
        "--window-hours", type=float, default=24,
        help="Default article view in hours (default 24; cache retains at least 168 hours)"
    )
    parser.add_argument(
        "--add-feed", type=str, metavar="URL",
        help="Add a feed and exit (use with --feed-name)"
    )
    parser.add_argument(
        "--feed-name", type=str,
        help="Name for --add-feed (default: URL)"
    )
    parser.add_argument(
        "--remove-feed", type=str, metavar="URL",
        help="Remove a feed by URL and exit"
    )
    parser.add_argument(
        "--list-feeds", action="store_true",
        help="List configured feeds and exit"
    )
    parser.add_argument(
        "--compare-default-feeds", action="store_true",
        help="Compare your configuration with the distribution starter feeds without changing it or printing private URLs"
    )

    parser.add_argument("--timezone", help="Display time zone, e.g. America/Chicago (default: this computer)")

    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("Port must be between 1 and 65535")
    import ipaddress
    try:
        ipaddress.IPv4Address(args.host)
    except ipaddress.AddressValueError:
        parser.error("Host must be a local IPv4 address, such as 0.0.0.0 or 192.168.1.10")
    args.config = str(Path(args.config).expanduser().resolve())
    args.data_dir = str(Path(args.data_dir).expanduser().resolve())
    if args.fetch_interval <= 0 or args.fetch_timeout <= 0:
        parser.error("Fetch interval and timeout must be positive")
    if not math.isfinite(args.window_hours) or args.window_hours < 1:
        parser.error("Article window must be at least 1 hour")

    # Handle CLI management commands
    if args.compare_default_feeds:
        from config import FeedConfig
        report = FeedConfig(args.config).compare_defaults()
        print(f"Distribution starter feeds: revision {report['distribution_revision']}")
        if report["config_exists"]:
            print(f"Your configuration was seeded from revision: {report['seeded_revision'] or 'not recorded'}")
        else:
            print("No personal configuration yet; comparison did not create one.")
        for feed in report["feeds"]:
            suffix = "; custom name or settings" if feed["customized"] else ""
            print(f"  [{feed['status']}{suffix}] {feed['name']}")
        print(f"Additional personal feeds: {report['personal_only_count']} (private names and URLs omitted)")
        print("Read-only comparison. Existing feeds and settings were not changed.")
        return
    if args.add_feed:
        from config import FeedConfig
        config = FeedConfig(args.config)
        feed = config.add_feed(args.add_feed, args.feed_name)
        print(f"Added feed: {feed.name} ({feed.url})")
        return

    if args.remove_feed:
        from config import FeedConfig
        config = FeedConfig(args.config)
        if config.remove_feed(args.remove_feed):
            print(f"Removed feed: {args.remove_feed}")
        else:
            print(f"Feed not found: {args.remove_feed}")
        return

    if args.list_feeds:
        from config import FeedConfig
        config = FeedConfig(args.config)
        feeds = config.load()
        if not feeds:
            print("No feeds configured.")
        else:
            print("Configured feeds:")
            for feed in feeds:
                status = "enabled" if feed.enabled else "disabled"
                print(f"  [{status}] {feed.name}: {feed.url}")
        return

    # Start the server
    print("=" * 50)
    print(f"  MacWire Helper {VERSION}")
    print("=" * 50)
    print()

    print(f"Configuration: {args.config}")
    print(f"Cache: {args.data_dir}")
    from server import serve
    serve(
        host=args.host,
        port=args.port,
        data_dir=args.data_dir,
        fetch_interval=args.fetch_interval,
        fetch_timeout=args.fetch_timeout,
        window_hours=args.window_hours,
        config_path=args.config,
        display_timezone=args.timezone,
    )


def main():
    try:
        run()
    except KeyboardInterrupt:
        return 0
    except ModuleNotFoundError as error:
        print(f"Missing dependency {error.name}. Install backend/requirements.txt using your virtual environment's Python.", file=sys.stderr)
        return 1
    except OSError as error:
        if error.errno == errno.EADDRINUSE:
            print("Server port is already in use. Stop the other server or choose --port 8081.", file=sys.stderr)
        else:
            print(f"Unable to start or access server files: {error}", file=sys.stderr)
        return 1
    except (ValueError, KeyError) as error:
        print(f"Invalid configuration or settings: {error}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
