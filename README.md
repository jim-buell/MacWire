# MacWire

A native RSS reader for a classic Macintosh, with MacWire Helper, a small Python server on a modern computer. MacWire and MacWire Helper are being prepared for release 1.0. The server fetches modern HTTPS feeds and converts text and photos for the classic Mac. This is an enthusiast project; the verified hardware target is a Macintosh SE running System 7.0.1 with MacTCP.

The latest client addresses article text remaining stuck loading while photos
load. It has been tested on a Macintosh SE. See [security guidance](SECURITY.md)
for the Helper's trusted-network requirements and limitations.

## Start the server

Use Python 3.11 or newer (`python3 --version`). Python installation is separate; no administrator access is needed for the server itself.

From the downloaded project directory:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r backend/requirements.txt
.venv/bin/python backend/main.py
```

The first run creates a configuration from the versioned public starter list: Colossal, Design Milk, Creative Boom, 512 Pixels, Six Colors, and Daring Fireball. The server listens on port 8080 and prints candidate IPv4 addresses. In MacWire's Connection preferences, enter the address on the same network as your classic Mac and the server port, turn off sample mode, and test the connection. Do not enter `localhost` or `127.0.0.1`: those refer to the classic Mac itself. Allow incoming connections in the modern Mac's firewall if necessary.

In the classic app, **Feeds > Refresh Feeds** asks the server to check all enabled feeds, shows “Checking feeds…”, then loads the updated list. Overlapping requests share one background check, and requests within 60 seconds of the last check reuse its result. The server keeps its scheduled hourly polling too. Feed failures preserve existing cached articles; a partial failure is reported in the client's status line. Escape or Command-period cancels the client wait without stopping the server's check. After five minutes, the client loads available cached news and reports that the check is still running. Starting the client or switching feed filters reads the cache without forcing another upstream fetch.

Up/down arrow keys can interrupt an article or photo download and move to another article. The active connection is closed before starting the next request. Author names, when supplied by the feed, appear below the publisher in the article reader; list rows remain unchanged.

Stop the server with Control-C. Keep the modern computer awake and the server running while reading. A DHCP reservation in your router can keep its address stable. Guest Wi-Fi and client isolation may prevent the machines from communicating.

Defaults on macOS:

- Configuration: `~/Library/Application Support/MacWire Helper/feeds.json`
- Cache: `~/Library/Application Support/MacWire Helper/cache/`

On Linux, defaults are under `$XDG_DATA_HOME/macwire-helper` or `~/.local/share/macwire-helper`. Existing installations keep using the legacy TickerTape data directory when it exists and the new directory does not. No files are moved automatically. Default paths are independent of the working directory. Explicit relative paths resolve from the directory where you run the command.

Existing users can retain their project-local feeds and cache:

```sh
.venv/bin/python backend/main.py --config backend/feeds.json --data-dir backend/rss_data --port 8081
```

The server is an unauthenticated HTTP service for a **trusted local network**. Anyone who can reach its port can read cached content and request downloads. Do not forward its port through your router or expose it to the Internet. Subscriber feed URLs may contain credentials: keep your configuration private.

## Manage feeds

```sh
.venv/bin/python backend/main.py --list-feeds
.venv/bin/python backend/main.py --add-feed 'https://example.com/feed.xml' --feed-name 'Example'
.venv/bin/python backend/main.py --remove-feed 'https://example.com/feed.xml'
```

To disable a feed without deleting it, set its `enabled` field to `false` in the JSON configuration. Restart the server after any feed changes; then refresh the classic client. Configuration changes are not reloaded automatically. `backend/feeds.example.json` is the versioned public distribution list; `backend/feeds.json` is reserved for local use. Existing configurations are never overwritten or merged when the distribution list changes.

Use `--compare-default-feeds` for a read-only comparison with the public starter
list; it omits personal-only names and URLs. The optional `defaults_revision` field
records which starter list created a new configuration. Keep personal subscriptions
in your per-user configuration; `backend/feeds.example.json` supplies public defaults
for new configurations only.

The default view covers 24 hours; the cache retains at least seven days. It can only retain articles a feed actually supplied. Feeds without publication dates do not appear. Full text and image quality depend on the source; some feeds contain only excerpts.

Other settings:

```sh
.venv/bin/python backend/main.py --help
.venv/bin/python backend/main.py --host 192.168.1.10 --port 8081 --timezone America/Chicago
.venv/bin/python backend/main.py --fetch-interval 1800 --fetch-timeout 15 --window-hours 48
```

`--host` selects a local IPv4 interface; by default the server binds all IPv4 interfaces. `--host 127.0.0.1` allows testing on the server computer only. If you bind a specific DHCP address, a changed address will require updating the command. Port and timezone must also match your intended setup.

## Classic Mac application

Use a current hardware MacBinary (`.bin`) or HFS disk image from the release. Preserve the application's resource fork when transferring it.

See [classic Mac setup](client/SETUP.md) for transfer instructions, MacTCP setup, and known limitations. Client and server should come from the same release; older servers may lack the current paging endpoints. Persistent classic-client article caching remains unfinished.

The article reader shows publisher and author above the headline, with an em dash
between them when an author is supplied. Inline photos follow their source positions
when available; feed attachments without positions use fallback placement. Refresh
feeds after upgrading to update image positions in cached articles. Normal Quit
saves your selected article, list position, feed filter, and sort order; they return
after the next startup sync, with the selected title visible in the left list. If a saved article has expired, the reader uses the
nearest publication date. An animated wire cursor indicates loading; the list appears
only when its full sorted view is ready. Refresh keeps the previous list visible
until the replacement finishes downloading.

Article bodies show bold subheadings without Markdown hashes, inline bold and
italic text, Monaco code with preserved indentation, indented pull quotes,
superscripts, subscripts, and strikethrough. Tables keep aligned columns and use
the article pane's horizontal scrollbar; their headers are underlined to preserve
fixed-width alignment. List markers, captions, and readable quote blocks remain.
External links are underlined; hovering shows a shortened destination in the
article-count status line, preserving the hostname where space permits. External
links are informational and do not launch a browser. Footnote references such as
`[1]` are clickable internal links; use `[Back to 1]` to return to the original
viewport. Drag or Shift-click to select text instead of following a footnote link.
Regular Up/Down navigates the article list. Command or Shift with Up/Down pages
within the selected article, like Page Up/Down.

Update the client and server together, restart the server, and choose **Refresh
Feeds** to reparse cached feed text with the new formatting and link ranges.
The wire cursor is restored immediately after network event handlers; its clock
keeps the animation in phase, though long operations can delay drawing a frame.

The server retains the complete available feed text. The current SE reader still
has a roughly 10,000-character body limit and displays a continuation notice when
text exceeds it. Up to five
photos are shown; omitted photos and embedded media leave labeled placeholders.

## Troubleshooting

- **Port already in use:** stop the other server, or launch with `--port 8081` and update the client.
- **Connection fails:** check the printed IPv4 address, port, firewall, MacTCP configuration, and that both machines share a reachable network. The `/status` endpoint returns binary data, so it is not a browser dashboard.
- **No articles immediately:** the server listens before its initial feed download finishes. Wait for a feed-update message, then refresh. Check the age window and enabled feeds.
- **Oversized photos:** the server retries once with a smaller version for The Verge and WordPress image-CDN URLs. Other hosts keep their original URLs. Download and pixel limits apply to both attempts; unsupported or still-oversized photos remain unavailable. Restart the server after updating, then reopen the article on the classic Mac to retry its photos.
- **One source fails:** errors appear in Terminal; other feeds continue. Check the URL and upstream availability.
- **Invalid JSON:** fix the configuration at the path printed by the server. Configuration errors stop startup rather than replacing your settings.
- **Damaged cache:** an unreadable article cache is renamed to `articles.corrupt-<timestamp>.json`; the server starts fresh and fetches again. Keep that backup if you need to inspect it.

Downloads are limited to 4 MiB for feeds/articles and 16 MiB for images; decoded images are limited to 20 million pixels. Requests have socket timeouts and the server permits up to 16 concurrent clients. These are basic bounds, not a guarantee of a fixed total request duration or suitability for public Internet hosting.

## Optional start at login (macOS)

See [the user LaunchAgent instructions](backend/LAUNCH_AGENT.md). Installation is optional and manual. A login agent runs while your account is logged in; it does not keep a sleeping Mac reachable.

## Build from source

See [the build guide](BUILDING.md) for the classic application toolchain,
automated tests, and source packaging. Retro68 is needed only to build the
classic application; it is not needed to run MacWire Helper.

MacWire and MacWire Helper are [MIT licensed](LICENSE). Third-party components
retain their [own licenses and notices](THIRD_PARTY.md). This is a prerelease;
validate the client and server together on your hardware before relying on them.
