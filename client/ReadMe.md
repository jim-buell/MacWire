# MacWire  
  
MacWire is an RSS reader for Classic Macintosh—specifically Compact Macs running Macintosh System 7.   
  
MacWire supports multiple RSS feeds and displays article text, metadata and black-and-white images. The images are resized and dithered (using Atkinson Dithering, naturally) for display on the 1-bit screens used on Compact Macs. Fonts are also customizable in the application’s settings.  
  
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


## Quick reference

## CONNECTING

The Helper and a new MacWire installation both default to port 8080. Older saved preferences keep their original port; change it if needed. Use the Helper computer's LAN address, not the Classic Mac's own address or 127.0.0.1. Keep the Helper running and the modern computer awake. Allow incoming connections in its firewall. Use a trusted local network; do not forward the Helper's port to the Internet.

For direct Ethernet/IP networking, open the MacTCP control panel and select Ethernet (your adapter may call it Ethernet Built-In). In More, choose manual addressing and enter your LAN's subnet mask and gateway. Return to the main MacTCP window and enter an unused static IP for the Classic Mac. Keep it outside the router's DHCP pool or reserve it there. Close MacTCP and restart if prompted. Adapter drivers and MacIP gateway setups have their own instructions.

Example only: on a 192.168.1.x LAN with mask 255.255.255.0 and router 192.168.1.1, the Classic Mac could use an available 192.168.1.20 and the Helper computer 192.168.1.10. Use your LAN's actual values. In MacWire, open File > Preferences > Show: Connection, enter the Helper address and port, click Test Connection, then OK. Choose Feeds > Refresh Feeds.

## FONT SETUP

MacWire uses installed Chicago, New York, and Monaco fonts by default. Quit MacWire before installing additional classic Mac font suitcases. On System 7.0/7.0.1, put screen fonts in the System file. On System 7.1 and later, use System Folder:Fonts. Preserve font resource forks. Relaunch MacWire, then open File > Preferences and choose Typography and Display from Show to choose each font, size, and style.

Defaults: list title Chicago 12 plain; reader title Chicago 18 bold; body New York 12 plain; metadata and feed name Monaco 9 italic. For bitmap fonts, choose an installed point size to avoid jagged scaling.

## ARTICLE WINDOWS

File > Preferences > Show: Connection > Show articles from offers: Past 24 hours (default), Past 48 hours, Past 3 days, and Past 7 days. Click OK, then choose Feeds > Refresh Feeds to apply the choice. These are rolling windows based on publication time. The Helper keeps at least seven days of available articles, but cannot recover articles a feed no longer supplies.

## KEYBOARD AND MOUSE SHORTCUTS

```
Up / Down              Previous / next article
Page Up / Page Down    Page up / down within the current article
Shift+Up / Shift+Down  Page up / down without changing articles
Command+Up / Down      Same article paging as Shift+Up / Down
Space / Shift+Space    Next article, even before reaching the bottom
Return                 Load the selected article if not cached
M / Shift+M            Toggle read / unread
Command+R              Refresh feeds
Command+A              Select all article text
Command+C              Copy selected text
Command+0 (zero)       Show the reader window
Command+W              Hide reader / close the front Clipboard window
Command+Q              Quit and save preferences and reading position
Escape / Command+.    Cancel a download or client wait
```

Drag to select article text; double-click selects a word. Shift-click extends the selection and can select footnote text without following its link. A plain click follows a footnote; click its Back link to return. External links show their destination when hovered; they do not open a browser.

In Connection preferences, Command+X/C/V/A edit the active address or port field. Return or keypad Enter accepts; Escape or Command+. cancels. In the Clipboard window, Up/Page Up scroll up; Down/Page Down/Space scroll down. Command+A/C select and copy Clipboard text.

## HELP AND LICENSES

Full instructions, troubleshooting, source, and release downloads: https://github.com/jim-buell/MacWire

This disk contains MacWire, this Read Me, and a Licenses folder. System software, MacTCP, drivers, fonts, and the Helper are supplied separately. The disk is not bootable. Keep the Licenses folder with the application. Long notices are split into numbered parts so classic text editors can open them. Read every part. MacWire is MIT licensed; third-party code retains its own notices and applicable runtime exception.
