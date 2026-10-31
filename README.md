# MacWire 1.0.2  
  
**MacWire is an RSS reader for Classic Macintosh—specifically Compact Macs running Macintosh System 7.**   
  
[Download the latest MacWire release](https://github.com/jim-buell/MacWire/releases/latest).

MacWire supports multiple RSS feeds and displays article text, metadata and black-and-white images. The images are resized and dithered (using [Atkinson Dithering](https://en.wikipedia.org/wiki/Atkinson_dithering), naturally) for display on the 1-bit screens used on Compact Macs. Fonts are also customizable in the application’s settings.  
  
I aimed to create a user experience that would have felt familiar in the early 1990s, but which also provides much of the basic functionality of an RSS reader.   
  
The left panel allows you to view articles from all your feeds sorted by date published (with the ability to change the sort order). The Feeds menu allows you to select an individual feed to show on the left as well.   
  
The panel on the right shows the article along with a maximum of 5 images. This limit keeps the download time *somewhat* speedy on these old machines.   
  
By default, MacWire will download articles published to a feed in the past 24 hours. You can change this to 48 hours, 3 days, or 7 days in Preferences.  
  
**What are the requirements?**  
  
MacWire runs on Classic Mac OS and requires MacTCP for networking. My personal Macintosh SE runs System 7.0.1, so that’s what I’ve tested it on. You may have success with other versions of the OS that can run MacTCP, but I haven’t tested anything beyond my setup. The user interface is designed specifically for a Compact Macintosh of this era though.   
  
If you’re running this on a real Classic Macintosh, you will of course need hardware to connect to your local network. I use a [BlueSCSI](https://bluescsi.com) which I find works very well.   
  
You will also require a modern computer. See below.   
  
**Isn’t a Compact Mac too old to use RSS?**  
  
Yeah, pretty much. That’s why MacWire relies on a helper app (a lightweight Python server) running on a modern computer to fetch and serve articles from RSS feeds. The modern computer needs to be on the same local network. The modern machine handles HTTPS requests that the Classic Mac would choke on. The modern machine also resizes and dithers the images before serving them to MacWire.   
  
RSS was initially released in 1999, and System 7 first came out in 1991.   
  
The helper app is run on the command-line, but in the future I *may* create a GUI interface. Heavy emphasis on the “may.”   
  
**How do I get set up?**  
  
*On the Modern Machine*  
  
First set up the helper app on your modern machine. You’ll need Python 3.11 or newer. On macOS or Linux, clone the repo and run:  
  
```
git clone https://github.com/jim-buell/MacWire.git
cd MacWire
python3 -m venv .venv
.venv/bin/python -m pip install -r backend/requirements.txt
.venv/bin/python backend/main.py
```
  
The helper starts with a small selection of feeds, listens on port 8080, and prints the modern computer’s local IP addresses. Keep it running and keep the computer awake while using MacWire. Control-C stops it.  
  
*On the Classic Macintosh*  
  
Next, transfer MacWire to your Classic Macintosh. Use the release’s MacBinary (.bin) application or HFS disk image. Decode the MacBinary file with a compatible utility, or copy the application from the mounted disk image. Preserve the application’s resource fork during transfer, or the Mac will have a bad time.  
  
Make sure your network adapter’s driver and MacTCP are installed and configured. The Classic Mac needs its own IP address on your local network, along with the correct subnet mask and gateway. MacTCP doesn’t support modern DHCP, so a manually assigned address is generally the way to go. The detailed networking instructions in the documentation cover this.  
  
Launch MacWire and open **File > Preferences**. Choose **Connection** from the **Show** menu and input the static IP address of the modern machine and the port used on the helper app (which defaults to 8080). You can then test the connection. Click OK, then choose **Feeds > Refresh Feeds** (or Cmd + R).   
  
**How do you customize the feeds?**   
  
Feeds can be added/removed on the modern machine using command-line options.   
  
```
# Add a feed
.venv/bin/python backend/main.py \
  --add-feed 'https://example.com/feed.xml' \
  --feed-name 'Example'

# Remove a feed
.venv/bin/python backend/main.py \
  --remove-feed 'https://example.com/feed.xml'
```
  
You’ll need to restart the helper app after any feed changes.   
  
**Why did I make this?**  
  
For fun I guess. I have a Macintosh SE (the FDHD variety that supports 1.44MB floppies), and it’s a lovely machine to play around with. I routinely check RSS feeds, and I wanted to see if I could create a useable RSS reader for a machine that existed before RSS was even created.   
  
**Why is it called MacWire?**  
  
[Wire services](https://en.wikipedia.org/wiki/News_agency) traditionally delivered news stories from remote locations, and RSS is in that vein. Plus the Classic Mac needs to fetch *its* articles from a remote (though local) location as well. Also I grew up in St. Louis, and pronouncing “MacWire” like [a certain steroid-using baseball player](https://en.wikipedia.org/wiki/Mark_McGwire#) has some nostalgia value for me.   
  
**Does MacWire run on emulators like Mini vMac?**  
  
Yes, the application will run on an emulator running Classic Mac OS, but a standard Mini vMac setup doesn’t provide the IP connection needed to reach the helper app. Live-feed operation through an emulator hasn’t been verified.  

---

## Detailed setup and reference

- [Helper configuration](#helper-configuration)
- [Feed configuration](#feed-configuration)
- [Classic Mac setup](#classic-mac-setup): [networking](#configure-the-classic-macs-network), [fonts](#install-and-select-fonts), and [time windows](#choose-an-article-time-window)
- [Reading articles](#reading-articles) and [keyboard shortcuts](#keyboard-shortcuts)
- [Troubleshooting](#troubleshooting)
- [Start at login](#optional-start-at-login-macos)
- [Build from source](#build-from-source) and [license](#license)

See [security guidance](SECURITY.md) for the Helper's trusted-network requirements
and limitations.

### Helper configuration

The FAQ above covers installation and startup. Check your Python version with
`python3 --version`; the Helper requires Python 3.11 or newer.

The first run creates a configuration from the versioned public starter list: Colossal, Design Milk, Creative Boom, 512 Pixels, Six Colors, and Daring Fireball. The server listens on port 8080 and prints candidate IPv4 addresses. In MacWire's Connection preferences, enter the address on the same network as your classic Mac and the server port, then test the connection. Do not enter `localhost` or `127.0.0.1`: those refer to the classic Mac itself. Allow incoming connections in the modern Mac's firewall if necessary.

Stop the server with Control-C. Keep the modern computer awake and the server running while reading. A DHCP reservation in your router can keep its address stable. Guest Wi-Fi and client isolation may prevent the machines from communicating.

#### Configuration and cache locations

Defaults on macOS:

- Configuration: `~/Library/Application Support/MacWire Helper/feeds.json`
- Cache: `~/Library/Application Support/MacWire Helper/cache/`

On Linux, defaults are under `$XDG_DATA_HOME/macwire-helper` or `~/.local/share/macwire-helper`. Existing installations keep using the legacy TickerTape data directory when it exists and the new directory does not. No files are moved automatically. Default paths are independent of the working directory. Explicit relative paths resolve from the directory where you run the command.

Existing users can retain their project-local feeds and cache:

```sh
.venv/bin/python backend/main.py --config backend/feeds.json --data-dir backend/rss_data --port 8081
```

The server is an unauthenticated HTTP service for a **trusted local network**. Anyone who can reach its port can read cached content and request downloads. Do not forward its port through your router or expose it to the Internet. Subscriber feed URLs may contain credentials: keep your configuration private.

#### Command-line settings

```sh
.venv/bin/python backend/main.py --help
.venv/bin/python backend/main.py --host 192.168.1.10 --port 8081 --timezone America/Chicago
.venv/bin/python backend/main.py --fetch-interval 1800 --fetch-timeout 15 --window-hours 48
```

`--host` selects a local IPv4 interface; by default the server binds all IPv4 interfaces. `--host 127.0.0.1` allows testing on the server computer only. If you bind a specific DHCP address, a changed address will require updating the command. Port and timezone must also match your intended setup.

### Feed configuration

The FAQ above shows how to add and remove feeds. To list the current configuration:


```sh
.venv/bin/python backend/main.py --list-feeds
```

To disable a feed without deleting it, set its `enabled` field to `false` in the JSON configuration. Restart the server after any feed changes; then refresh the classic client. Configuration changes are not reloaded automatically. `backend/feeds.example.json` is the versioned public distribution list; `backend/feeds.json` is reserved for local use. Existing configurations are never overwritten or merged when the distribution list changes.

Use `--compare-default-feeds` for a read-only comparison with the public starter
list; it omits personal-only names and URLs. The optional `defaults_revision` field
records which starter list created a new configuration. Keep personal subscriptions
in your per-user configuration; `backend/feeds.example.json` supplies public defaults
for new configurations only.

#### Refresh behavior

In the classic app, **Feeds > Refresh Feeds** asks the server to check all enabled feeds, shows “Checking feeds…”, then loads the updated list. Overlapping requests share one background check, and requests within 60 seconds of the last check reuse its result. The server keeps its scheduled hourly polling too. Feed failures preserve existing cached articles; a partial failure is reported in the client's status line. Escape or Command-period cancels the client wait without stopping the server's check. After five minutes, the client loads available cached news and reports that the check is still running. Starting the client or switching feed filters reads the cache without forcing another upstream fetch.

### Classic Mac setup

Set up the classic Mac as well as the Helper. The tested configuration is a
Macintosh SE running System 7.0.1 with MacTCP. You need a working network adapter
and its System 7-compatible driver, MacTCP, and the standard Macintosh fonts.
System software, drivers, MacTCP, and fonts are supplied separately.

#### Install the application

Use the classic ZIP from the release and keep its license notices. Transfer the
MacBinary application (`.bin`) in binary mode, then decode it on the classic Mac
with a MacBinary-compatible utility so both the data and resource forks survive.
For **Disk Copy 6.3.3 on the classic Mac**, transfer `MacWire.image.bin` in binary
mode and decode MacBinary, then mount the resulting `MacWire.image` and copy
MacWire to your hard disk in Finder. Decoding preserves the image's Finder type
and creator; changing those codes on a raw image is not a format conversion.
The `.dsk` file is raw HFS for emulators and compatible raw-image tools, not a
Disk Copy container. Both images contain the same application and notices,
not a bootable System disk. Disk Copy 4.2 itself writes disks rather than mounting
them; use the later Disk Copy 6.3.3 for desktop mounting.
Launch the decoded or copied application in Finder. Use client and Helper builds
from the same release; older Helpers may lack the current paging endpoints.

#### Configure the classic Mac's network

These steps cover a direct Ethernet/IP connection to your local network. Install
your adapter's driver and MacTCP according to their installation instructions,
then restart. Adapter-specific settings come from its documentation. LocalTalk
or EtherTalk through a MacIP gateway requires a different network setup.

1. Connect the classic Mac and Helper computer to the same trusted LAN. In the
   classic Mac's **Apple menu > Control Panels > MacTCP**, select **Ethernet**
   for direct IP networking, rather than EtherTalk.
2. Choose **More...** and set **Obtain Address** to **Manually**. MacTCP does not
   support modern DHCP; its **Dynamically** option is not DHCP. Obtain an unused
   static IPv4 address, subnet mask, and router address for your LAN. Reserve the
   address or keep it outside the router's DHCP pool so another device cannot
   receive it.
3. Set the subnet mask and **Gateway Address** (your router) in the More dialog;
   older MacTCP versions also expose address-class settings. Return to the main
   window and enter the classic Mac's assigned IP address. Use your network's
   values, not the Helper computer's address. Close MacTCP and restart if prompted.

For example, on a `192.168.1.x` LAN with mask `255.255.255.0` and router
`192.168.1.1`, the classic Mac might use an available `192.168.1.20` and the Helper
`192.168.1.10`. These are examples, not addresses to copy onto a different LAN.
DNS is not needed for MacWire's numeric Helper address; the Helper handles
publisher DNS and HTTPS. See Apple's archived [MacTCP setup guidance](https://savagetaylor.com/TIL/TIL20223.pdf)
and [addressing notes](https://savagetaylor.com/TIL/TIL19878.pdf) for the control-panel fields.

Start the Helper using the instructions above, keep that computer awake, and
allow incoming connections through its firewall. In MacWire:

1. Open **File > Preferences**, then choose **Connection** from **Show**.
2. Enter the Helper's LAN IPv4 address in **Server address** and its listening
   port in **Port**. Both client and Helper default to **8080**. Existing client
   preferences retain their saved port; if yours shows 8081, change it to 8080
   unless you deliberately run the Helper on 8081. Use neither the classic Mac's
   own address nor `localhost`/`127.0.0.1`, and do not include `http://`.
3. Click **Test Connection**, then **OK** to save. Choose **Feeds > Refresh Feeds**.

If the test fails, check the adapter driver, MacTCP address and mask, Helper
address/port, firewall, and router client isolation. Guest Wi-Fi can prevent the
Helper from reaching other LAN devices. An AppleTalk file share alone does not
establish an IP connection to the Helper.

#### Install and select fonts

MacWire uses fonts installed on the classic Mac. No additional font pack is
required when **Chicago**, **New York**, and **Monaco** are available; optional
fonts must be classic Mac-compatible bitmap or TrueType fonts, not modern web
fonts. Obtain them from your licensed system/font media and preserve their
resource forks during transfer.

Quit MacWire and other applications before installing fonts:

- **System 7.0 or 7.0.1:** open the System Folder and drag the font icons from
  their suitcase onto the **System** file. These systems keep screen fonts in
  the System suitcase, not in a Fonts folder.
- **System 7.1 or later:** put the font suitcase in **System Folder > Fonts**.
  You can also drag it onto the closed System Folder and let Finder place it.

Relaunch MacWire after installation. These locations follow Apple's archived
[font installation instructions](https://savagetaylor.com/til/TA30640.html).
In **File > Preferences > Show: Typography and Display**, choose a font, size,
and style independently for each row. The defaults are:

| Setting | Font | Size | Style |
| --- | --- | --- | --- |
| List title | Chicago | 12 pt | Plain |
| Reader title | Chicago | 18 pt | Bold |
| Body | New York | 12 pt | Plain |
| Metadata | Monaco | 9 pt | Italic |
| Feed name | Monaco | 9 pt | Italic |

Inline code also uses Monaco. For bitmap fonts, use an installed point size to
avoid scaled, jagged letters. The size menu outlines sizes the system reports
as available for the selected font. If a font is missing from the font menu,
check its installation location and transfer format, then relaunch MacWire.
Click **OK** to save your typography choices.

#### Choose an article time window

Open **File > Preferences > Show: Connection** and use **Show articles from**:

| Choice | Articles included |
| --- | --- |
| Past 24 hours | Published within the last day; the default |
| Past 48 hours | Published within the last two days |
| Past 3 days | Published within the last 72 hours |
| Past 7 days | Published within the last 168 hours |

These are rolling windows based on each article's publication timestamp, not
calendar-day boundaries or the time it was downloaded. Click **OK** to save,
then choose **Feeds > Refresh Feeds** to load the selected window. The choice
persists across launches and also applies when viewing an individual feed.
Larger windows can take longer to load on the SE.

The window filters the list; it does not reduce the Helper's cache retention,
which is at least seven days. A newly started Helper can only show the articles
its feeds currently provide; selecting seven days cannot recover older articles
that the feeds no longer supply. Articles without publication dates are omitted.

The Helper's `--window-hours` option sets its default view for requests that do
not specify a window. MacWire sends the choice from its own preferences, so
changing that command-line option does not override the client's selection.

### Reading articles

Up/down arrow keys can interrupt an article or photo download and move to another article. The active connection is closed before starting the next request. Author names, when supplied by the feed, appear below the publisher in the article reader; list rows remain unchanged.

See [classic Mac setup](client/SETUP.md) for a shorter setup checklist and keyboard
controls. Persistent classic-client article caching remains unfinished.

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

The default view covers 24 hours; the cache retains at least seven days. It can only retain articles a feed actually supplied. Feeds without publication dates do not appear. Full text and image quality depend on the source; some feeds contain only excerpts.

#### Keyboard shortcuts

These shortcuts apply when the main reader window is in front:

| Shortcut | Action |
| --- | --- |
| Up / Down | Select the previous / next article |
| Page Up / Page Down | Scroll the current article up / down by one viewport |
| **Shift+Up / Shift+Down** | Scroll the article up / down by one viewport, without changing articles |
| Command+Up / Command+Down | The same article scrolling as Shift+Up / Shift+Down |
| Space, including Shift+Space | Select the next article immediately |
| Return | Load the selected article's text if it is not already cached |
| M, including Shift+M | Toggle the selected article between read and unread |
| Command+A | Select all text in the article reader |
| Command+C | Copy selected article text |
| Command+R | Refresh feeds |
| Command+0 (zero) | Show the reader window |
| Command+W | Hide the reader window, or close the Clipboard window when it is in front |
| Command+Q | Quit MacWire and save preferences and reading position |
| Escape or Command+period | Cancel the current download or client wait |

The **Shift+arrow** shortcuts are useful on compact keyboards without Page Up
and Page Down. Space advances to the next article even when the current article
has not been scrolled to the bottom. Up/Down and Space can interrupt an article
or photo download to select another article. Modified Up/Down and Page Up/Down
scroll without cancelling the active request. Cancelling a feed-refresh wait
does not stop the Helper's background feed check.

Drag across article text to select it; double-click selects a word.
**Shift-click extends the selection from its anchor** and lets you select a
footnote reference without following it. A plain click on a footnote reference
follows it; click its `[Back to ...]` link to return. External links display their
destination when hovered but do not open a browser.

In **Connection preferences**, Command+X/C/V/A cut, copy, paste, or select all
within the active address/port field. Return or keypad Enter accepts a dialog;
Escape or Command+period cancels Preferences. In the **Clipboard window**,
Up/Page Up scroll upward, and Down/Page Down/Space scroll downward; Command+A/C
select and copy its text.

### Troubleshooting

- **Port already in use:** stop the other server, or launch with `--port 8081` and update the client.
- **Connection fails:** check the printed IPv4 address, port, firewall, MacTCP configuration, and that both machines share a reachable network. The `/status` endpoint returns binary data, so it is not a browser dashboard.
- **No articles immediately:** the server listens before its initial feed download finishes. Wait for a feed-update message, then refresh. Check the age window and enabled feeds.
- **Oversized photos:** the server retries once with a smaller version for The Verge and WordPress image-CDN URLs. Other hosts keep their original URLs. Download and pixel limits apply to both attempts; unsupported or still-oversized photos remain unavailable. Restart the server after updating, then reopen the article on the classic Mac to retry its photos.
- **One source fails:** errors appear in Terminal; other feeds continue. Check the URL and upstream availability.
- **Invalid JSON:** fix the configuration at the path printed by the server. Configuration errors stop startup rather than replacing your settings.
- **Damaged cache:** an unreadable article cache is renamed to `articles.corrupt-<timestamp>.json`; the server starts fresh and fetches again. Keep that backup if you need to inspect it.

Downloads are limited to 4 MiB for feeds/articles and 16 MiB for images; decoded images are limited to 20 million pixels. Requests have socket timeouts and the server permits up to 16 concurrent clients. These are basic bounds, not a guarantee of a fixed total request duration or suitability for public Internet hosting.

### Optional start at login (macOS)

See [the user LaunchAgent instructions](backend/LAUNCH_AGENT.md). Installation is optional and manual. A login agent runs while your account is logged in; it does not keep a sleeping Mac reachable.

### Build from source

See [the build guide](BUILDING.md) for the classic application toolchain,
automated tests, and source packaging. Retro68 is needed only to build the
classic application; it is not needed to run MacWire Helper.

### License

MacWire and MacWire Helper are [MIT licensed](LICENSE). Third-party components
retain their [own licenses and notices](THIRD_PARTY.md). Use matching client and
Helper versions and follow the setup instructions for your hardware.
