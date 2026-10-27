# Set up MacWire on a classic Macintosh

MacWire's verified hardware target is a Macintosh SE running System 7.0.1 with
MacTCP. Install and configure a compatible TCP/IP stack on the classic Mac and
connect it to the same trusted network as the computer running MacWire Helper.
System software, MacTCP, and fonts are supplied separately. For adapter setup,
manual MacTCP addressing, and a worked LAN example, follow the
[README networking instructions](../README.md#configure-the-classic-macs-network).

## Transfer the application

Use a matching MacWire client and Helper from the same release. Transfer the
MacBinary application (`.bin`) in binary mode, then decode it on the classic Mac
with both its data and resource forks preserved. Alternatively, use the supplied
application-only HFS disk image, if available. It is not a bootable System disk.
Launch the decoded MacWire application in Finder.

## Connect to the Helper

Start MacWire Helper using the [README instructions](../README.md). Keep the
computer awake and allow incoming connections through its firewall.

1. Open File > Preferences and choose Connection from Show.
2. Enter the Helper computer's LAN IPv4 address and port (8080 by default).
   New client settings also default to 8080. Existing settings keep their saved
   port; change an older 8081 setting if the Helper is listening on 8080.
   Use the numeric address printed by the Helper. `localhost` and `127.0.0.1`
   refer to the classic Mac itself.
3. Click Test Connection, then OK.
4. Choose Feeds > Refresh Feeds to check for news.

MacWire currently accepts IPv4 addresses rather than DNS names. If the connection
fails, check MacTCP settings, the address and port, the host firewall, and router
client isolation. A working FTP client alone does not confirm that MacWire can
use the installed TCP/IP stack.

## Set up fonts

MacWire uses installed Chicago, New York, and Monaco fonts by default. No extra
font pack is required if these are available. System 7.0.1 stores screen fonts
in the System suitcase; System 7.1 and later use System Folder > Fonts.
See the [README font instructions](../README.md#install-and-select-fonts) for
installation steps and the default sizes/styles. Relaunch MacWire after adding
fonts, then use File > Preferences > Show: Typography and Display to choose them.

## Read articles

In File > Preferences > Show: Connection, choose Past 24 hours (the default),
Past 48 hours, Past 3 days, or Past 7 days under Show articles from. Click OK,
then choose Feeds > Refresh Feeds to apply the window. These are rolling
publication-age windows; the Helper retains at least seven days of available
feed articles. See [time-window details](../README.md#choose-an-article-time-window).
Use the Feeds menu to select a source and the Date header to change sort order.
The Articles menu filters read/unread items and changes read status.

Up/Down selects articles. Page Up/Down or Command/Shift with Up/Down pages within
the current article. Escape or Command-period cancels a download. Preferences
also control typography using fonts already installed on the classic Mac.
See the [full shortcut reference](../README.md#keyboard-shortcuts), including
Shift+Up/Down for article paging and Shift-click to extend a text selection.

Normal Quit saves connection settings, read state, selected article, and list
position. The reader currently displays about 10,000 body characters and up to
five photos per article; longer bodies show a continuation notice. External
links show their destinations but do not launch a browser.
