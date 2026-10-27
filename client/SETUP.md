# Set up MacWire on a classic Macintosh

MacWire's verified hardware target is a Macintosh SE running System 7.0.1 with
MacTCP. Install and configure a compatible TCP/IP stack on the classic Mac and
connect it to the same trusted network as the computer running MacWire Helper.
System software, MacTCP, and fonts are supplied separately.

## Transfer the application

Use a matching MacWire client and Helper from the same release. Transfer the
MacBinary application (`.bin`) in binary mode, then decode it on the classic Mac
with both its data and resource forks preserved. Alternatively, use the supplied
application-only HFS disk image, if available. It is not a bootable System disk.
Launch the decoded MacWire application in Finder.

## Connect to the Helper

Start MacWire Helper using the [README instructions](../README.md). Keep the
computer awake and allow incoming connections through its firewall.

1. Open File > Preferences and select Connection.
2. Enter the Helper computer's LAN IPv4 address and port (8080 by default).
   Use the numeric address printed by the Helper. `localhost` and `127.0.0.1`
   refer to the classic Mac itself.
3. Click Test Connection, then OK.
4. Choose Feeds > Refresh Feeds to check for news.

MacWire currently accepts IPv4 addresses rather than DNS names. If the connection
fails, check MacTCP settings, the address and port, the host firewall, and router
client isolation. A working FTP client alone does not confirm that MacWire can
use the installed TCP/IP stack.

## Read articles

Connection preferences offer article windows from 24 hours to seven days. Use
the Feeds menu to select a source and the Date header to change sort order.
The Articles menu filters read/unread items and changes read status.

Up/Down selects articles. Page Up/Down or Command/Shift with Up/Down pages within
the current article. Escape or Command-period cancels a download. Preferences
also control typography using fonts already installed on the classic Mac.

Normal Quit saves connection settings, read state, selected article, and list
position. The reader currently displays about 10,000 body characters and up to
five photos per article; longer bodies show a continuation notice. External
links show their destinations but do not launch a browser.
