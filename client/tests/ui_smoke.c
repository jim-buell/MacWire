/* Run with TICKERTAPE_UI_SMOKE and a disposable System 7 boot disk.
 * These checks use the real Toolbox, not a model of the implementation. */
#include "../compat.h"
#include "../articles.h"
#include "../MenuCommands.h"
#include "../typography.h"
#include <Dialogs.h>
#include <Icons.h>
#include "../Connection.h"
#include "../Feeds.h"
#include "../ReadHistory.h"

extern void HandleEvent(EventRecord *event);
extern void HandleKeyDown(EventRecord *event);
extern void HandleMouseDown(EventRecord *event);
extern unsigned long gListPaintCount, gReaderPaintCount;

static short failures;
static FILE *logFile;

static short pendingDialogKey, pendingDialogModifiers;
static short preferencePane, preferenceFont, preferenceLines;
static short mouseCount = -1, mouseIndex;
static Point mousePoints[4];
short UISmokeReaderMouse(Point *where)
{
    if (mouseCount < 0) return -1;
    if (mouseIndex < mouseCount) { *where = mousePoints[mouseIndex++]; return 1; }
    mouseCount = -1; return 0;
}
extern unsigned long gReaderLayoutBuilds, gReaderStripPaints;
extern long UISmokeBodyTop(void), UISmokeImageTop(void);
extern short UISmokeReaderHit(Point where);
extern short UISmokeReaderScrollValue(void), UISmokeListScrollValue(void);
extern void UISmokeSelectRange(short start, short end);
extern Boolean UISmokeWrappedLinesFit(void);
extern short UISmokeHorizontalMaximum(void), UISmokeHorizontalValue(void);
extern Point UISmokeCornerPoint(void);
extern void UISmokeHorizontalArrow(short part);
extern void UISmokeListArrow(short part);
extern unsigned long gListStripPaints;
extern Boolean UISmokeListRangeChangedInPaneUpdate(void), UISmokeListThumbVisible(void);
extern Boolean UISmokeListThumbPaintedWithList(void);
extern void UISmokeConnectionPane(DialogPtr dialog);
extern void UISmokeUpdateSizeMenu(DialogPtr dialog, short row);
extern void UISmokePaintPreferences(DialogPtr dialog);
extern Boolean UISmokePreferencesCacheWorks(DialogPtr dialog);
static void Check(Boolean condition, const char *description);

void UISmokePreparePreferences(DialogPtr dialog)
{
    short type; Handle control; Rect rect;
    if (preferencePane == 2) UISmokeConnectionPane(dialog);
    if (preferencePane == 2) {
        Rect address, port, age, help, status, selector;
        FontInfo dialogFont;
        Str255 text, firstLine, secondLine;
        short i;
        Boolean labelsFit = true;
        static const short labels[] = {27,29,31,38};
        GetDialogItem(dialog, 30, &type, &control, &address);
        GetDialogItem(dialog, 32, &type, &control, &port);
        GetDialogItem(dialog, 39, &type, &control, &age);
        age=(**(ControlHandle)control).contrlRect;
        GetDialogItem(dialog, 28, &type, &control, &selector);
        selector=(**(ControlHandle)control).contrlRect;
        GetDialogItem(dialog, 40, &type, &control, &help);
        GetDialogItem(dialog, 34, &type, &control, &status);
        for (i = 0; i < 4; ++i) {
            GetDialogItem(dialog, labels[i], &type, &control, &rect);
            GetDialogItemText(control, text);
            if (rect.right != 144 || rect.left < 16 ||
                rect.right - rect.left < StringWidth(text)+4) labelsFit = false;
        }
        Check(labelsFit, "Connection labels reserve TextEdit padding and share the same colon column");
        Check(address.left - 3 == age.left && port.left == address.left &&
              selector.left == age.left && selector.right == age.right,
              "Connection fields and popup share their visible left edge");
        Check(help.left == age.left && help.top > age.bottom && help.bottom < status.top,
              "article-period explanation fits between its popup and status");
        StringToPStr(firstLine, "Only fetches articles published");
        StringToPStr(secondLine, "during this time period.");
        Check(StringWidth(firstLine)+4 <= help.right - help.left &&
              StringWidth(secondLine)+4 <= help.right - help.left,
              "article-period explanation lines fit the native dialog font");
        GetFontInfo(&dialogFont);
        Check(help.bottom-help.top >= 2*(dialogFont.ascent+dialogFont.descent+dialogFont.leading)+4,
              "article-period explanation has height for both complete lines");
    }
    if (preferenceFont) {
        GetDialogItem(dialog, 7, &type, &control, &rect);
        SetControlValue((ControlHandle)control, preferenceFont);
        UISmokeUpdateSizeMenu(dialog, 0);
    }
    if (preferenceLines) {
        GetDialogItem(dialog, 37, &type, &control, &rect);
        SetControlValue((ControlHandle)control, preferenceLines);
    }
    {
        short row, i, font;
        Str255 name;
        Boolean matches = true;
        static const short sizes[] = {6,7,8,9,10,11,12,13,14,16,18,20,24,28,32,36,48,72};
        for (row = 0; row < kTypeCount; ++row) {
            MenuHandle menu = GetMenuHandle(kTypographyFirstSizeMenu + row);
            GetDialogItem(dialog, 7 + row * 4, &type, &control, &rect);
            GetMenuItemText(GetMenuHandle(200), GetControlValue((ControlHandle)control), name);
            GetFNum(name, &font);
            if (!menu) { matches = false; continue; }
            for (i = 0; i < 18; ++i) {
                short style;
                GetItemStyle(menu, i + 1, &style);
                if (style != (RealFont(font, sizes[i]) ? outline : 0)) matches = false;
            }
        }
        Check(matches, "each role outlines available sizes for its own selected font");
    }
    {
        BitMap screen = dialog->portBits;
        RgnHandle clip = NewRgn(), visible = NewRgn(), current = NewRgn();
        SetPort(dialog);
        GetClip(clip); CopyRgn(dialog->visRgn, visible);
        UISmokePaintPreferences(dialog);
        GetClip(current);
        Check(dialog->portBits.baseAddr == screen.baseAddr &&
              dialog->portBits.rowBytes == screen.rowBytes &&
              EqualRect(&dialog->portBits.bounds, &screen.bounds) &&
              EqualRgn(clip, current) && EqualRgn(visible, dialog->visRgn),
              "buffered preferences repaint restores screen bitmap and regions");
        Check(UISmokePreferencesCacheWorks(dialog),
              "unchanged preferences repaint reuses its image without redrawing controls");
        DisposeRgn(clip); DisposeRgn(visible); DisposeRgn(current);
    }
    UISmokeSnapshot(preferencePane == 2 ? "connection.pbm" : "font-picker.pbm");
}

/* Post the key once the dialog has drawn. */
static void PostTestKey(long character, short modifiers)
{
    pendingDialogKey = character;
    pendingDialogModifiers = modifiers;
}

void UISmokePostDialogKey(void)
{
    if (pendingDialogKey != 0) {
        register long result __asm__("d0") = pendingDialogKey;
        register EvQElPtr element __asm__("a0") = (EvQElPtr)(long)keyDown;
        pendingDialogKey = 0;
        __asm__ volatile(".word 0xA12F" : "+d"(result), "+a"(element)
                         : : "d1", "d2", "a1", "memory", "cc");
        if ((short)result == noErr && element != NULL)
            element->evtQModifiers = pendingDialogModifiers;
    }
}

static Boolean PostMouse(Point global, short modifiers)
{
    register long result __asm__("d0") = 0;
    register EvQElPtr element __asm__("a0") = (EvQElPtr)(long)mouseDown;
    __asm__ volatile(".word 0xA12F" : "+d"(result), "+a"(element)
                     : : "d1", "d2", "a1", "memory", "cc");
    if ((short)result != noErr || element == NULL) return false;
    element->evtQWhere = global; element->evtQModifiers = modifiers;
    return true;
}

static void Check(Boolean condition, const char *description)
{
    fprintf(logFile, "%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++failures;
    fflush(logFile);
    FlushVol(NULL, 0);
}

static void PaintPending(void)
{
    EventRecord event;
    short i;
    for (i = 0; i < 12; ++i)
        if (WaitNextEvent(everyEvent, &event, 1, NULL)) HandleEvent(&event);
    UpdateMenus();
}

static void CheckResizeChrome(void)
{
    Rect original = gMainWindow->portRect, pixel;
    Point corner;
    SizeWindow(gMainWindow, 360, 210, true);
    LayoutUI(); InvalRect(&gMainWindow->portRect); PaintPending();
    corner = UISmokeCornerPoint();
    SetRect(&pixel, corner.h, corner.v, corner.h + 2, corner.v + 2);
    SetPort(gMainWindow); ClipRect(&gMainWindow->portRect); PaintRect(&pixel);
    DrawStatusLine();
    Check(!GetPixel(corner.h, corner.v),
          "repaint clears artifacts under the list down arrow and beside the left arrow");
    ZoomReaderWindow(inZoomOut); PaintPending();
    Check(gMainWindow->portRect.right - gMainWindow->portRect.left > 360 &&
          gMainWindow->portRect.bottom - gMainWindow->portRect.top > 210,
          "native zoom box expands a smaller window to fill the desktop");
    UISmokeSnapshot("zoomed-window.pbm");
    ZoomReaderWindow(inZoomIn); PaintPending();
    Check(gMainWindow->portRect.right - gMainWindow->portRect.left == 360 &&
          gMainWindow->portRect.bottom - gMainWindow->portRect.top == 210,
          "native zoom box restores the user's previous smaller window");
    SizeWindow(gMainWindow, original.right - original.left,
               original.bottom - original.top, true);
    LayoutUI(); InvalRect(&gMainWindow->portRect); PaintPending();
}

static void CheckListScrolling(void)
{
    Rect original = gMainWindow->portRect;
    short originalOffset = gScrollOffset;
    short originalSelection = gSelectedIndex;
    unsigned long fullPaints, stripPaints;
    long bytes = (long)(qd.screenBits.rowBytes & 0x3FFF) *
        (qd.screenBits.bounds.bottom - qd.screenBits.bounds.top);
    Ptr pixels;
    gSelectedIndex = 1; ClearReaderSelection();
    SizeWindow(gMainWindow, 360, 150, true);
    LayoutUI(); InvalRect(&gMainWindow->portRect); PaintPending();
    ScrollListTo(0);
    Check(UISmokeListRangeChangedInPaneUpdate(),
          "a pane-only startup update queues the list scrollbar's new range for repaint");
    PaintPending();
    Check(UISmokeListThumbVisible(), "list thumb becomes visible after articles arrive");
    Check(UISmokeListThumbPaintedWithList(),
          "the left thumb is painted in the same pass as the list, before reader layout");
    {
        extern Boolean UISmokeListHeaderDividerVisible(void);
        Check(UISmokeListHeaderDividerVisible(),
              "buffered list header divider reaches from the scrollbar to the window top");
        DrawListPane();
        Check(UISmokeListHeaderDividerVisible(),
              "repeated buffered list paints retain the full header divider");
    }
    UISmokeSnapshot("visible-list-thumb.pbm");
    fullPaints = gListPaintCount; stripPaints = gListStripPaints;
    UISmokeListArrow(inDownButton);
    Check(gScrollOffset == 1 && UISmokeListScrollValue() == 1 &&
          gListPaintCount == fullPaints && gListStripPaints > stripPaints,
          "list down arrow copies existing pixels and paints only the exposed row");
    pixels = NewPtr(bytes);
    if (pixels) {
        BlockMove(qd.screenBits.baseAddr, pixels, bytes); DrawListPane();
        Check(memcmp(pixels, qd.screenBits.baseAddr, bytes) == 0,
              "list strip scroll preserves selected, unread, and partial-row pixels");
        DisposePtr(pixels);
    } else Check(false, "allocate list scrolling verification bitmap");
    UISmokeListArrow(inUpButton);
    Check(gScrollOffset == 0 && UISmokeListScrollValue() == 0,
          "list up arrow returns to the first article");
    UISmokeSnapshot("scrolled-list.pbm");
    SizeWindow(gMainWindow, original.right - original.left,
               original.bottom - original.top, true);
    gScrollOffset = originalOffset;
    gSelectedIndex = originalSelection; ClearReaderSelection();
    LayoutUI(); InvalRect(&gMainWindow->portRect); PaintPending();
}

/* PBM is the Mac's monochrome screen bitmap with a portable header. */
void UISmokeSnapshot(const char *name)
{
    FSSpec spec;
    Str255 filename;
    short refNum;
    char header[40];
    long count;
    short width = qd.screenBits.bounds.right - qd.screenBits.bounds.left;
    short height = qd.screenBits.bounds.bottom - qd.screenBits.bounds.top;
    short y;
    StringToPStr(filename, name);
    FSMakeFSSpec(0, 0, filename, &spec);
    FSpCreate(&spec, 'RSSR', 'TEXT', 0);
    if (FSpOpenDF(&spec, fsWrPerm, &refNum) != noErr) {
        Check(false, "create UI snapshot"); return;
    }
    SetEOF(refNum, 0);
    count = sprintf(header, "P4\n%d %d\n", width, height);
    FSWrite(refNum, &count, header);
    if ((qd.screenBits.rowBytes & 0x3FFF) == (width + 7) / 8)
    {
        count = (long)height * ((width + 7) / 8);
        FSWrite(refNum, &count, qd.screenBits.baseAddr);
    } else for (y = 0; y < height; ++y) {
        count = (width + 7) / 8;
        FSWrite(refNum, &count, qd.screenBits.baseAddr +
                y * (qd.screenBits.rowBytes & 0x3FFF));
    }
    FSClose(refNum);
}

static void Command(char key)
{
    EventRecord event;
    memset(&event, 0, sizeof(event));
    event.what = keyDown;
    event.message = key;
    event.modifiers = cmdKey;
    fprintf(logFile, "TRACE: Command-%c begin\n", key); fflush(logFile);
    HandleKeyDown(&event);
    fprintf(logFile, "TRACE: Command-%c handled\n", key); fflush(logFile);
    PaintPending();
    fprintf(logFile, "TRACE: Command-%c painted\n", key); fflush(logFile);
}

static Boolean MenuEnabled(short menu, short item)
{
    return ((*GetMenuHandle(menu))->enableFlags & (1UL << item)) != 0;
}

static void CheckArticleMenu(void)
{
    Boolean read[kMaxArticles];
    short i, selected = gSelectedIndex;
    char filter[33];
    Boolean scoped = true;
    Str255 label;
    Check(GetMenuHandle(kArticleMenu) != NULL &&
          CountMItems(GetMenuHandle(kArticleMenu)) == 10,
          "Article menu groups navigation and read-state actions");
    if (gMaxArticles < 2) { Check(false, "article menu fixtures available"); return; }
    for (i = 0; i < gMaxArticles; ++i) read[i] = gArticles[i].isRead;
    SelectArticle(0);
    for (i = 0; i < gMaxArticles; ++i) gArticles[i].isRead = true;
    UpdateMenus();
    Check(!MenuEnabled(kArticleMenu, kArticlePrevious) &&
          MenuEnabled(kArticleMenu, kArticleNext) &&
          !MenuEnabled(kArticleMenu, kArticleNextUnread),
          "Article navigation reflects boundaries and unread availability");
    gArticles[1].isRead = false; UpdateMenus();
    HandleMenuCommand(kArticleMenu, kArticleNextUnread);
    Check(gSelectedIndex == 1, "Next Unread selects the next unread article");
    GetMenuItemText(GetMenuHandle(kArticleMenu), kArticleMarkRead, label);
    Check(EqualString(label, (ConstStringPtr)"\014Mark as Read", true, true),
          "unread article offers Mark as Read");
    Command('m');
    Check(!gArticles[1].isRead, "Command-M has no read-state action");
    HandleMenuCommand(kArticleMenu, kArticleMarkRead);
    GetMenuItemText(GetMenuHandle(kArticleMenu), kArticleMarkRead, label);
    Check(gArticles[1].isRead && EqualString(label,
          (ConstStringPtr)"\016Mark as Unread", true, true),
          "Mark as Read updates read state and the action label");
    HandleMenuCommand(kArticleMenu, kArticleMarkRead);
    Check(!gArticles[1].isRead, "Mark as Unread clears the read state");
    gArticles[1].isRead = true; gArticles[0].isRead = false;
    HandleMenuCommand(kArticleMenu, kArticleNextUnread);
    Check(gSelectedIndex == 0, "Next Unread wraps to an earlier unread article");
    HandleMenuCommand(kArticleMenu, kArticleNext);
    Check(gSelectedIndex == 1, "Next Article menu command advances selection");
    HandleMenuCommand(kArticleMenu, kArticlePrevious);
    Check(gSelectedIndex == 0, "Previous Article menu command moves back");
    gSelectedIndex = -1; UpdateMenus();
    Check(!MenuEnabled(kArticleMenu, kArticleMarkRead),
          "read-state action is disabled without a selected article");
    strcpy(filter, gFeedFilter);
    strcpy(gFeedFilter, gArticles[0].feed);
    for (i = 0; i < gMaxArticles; ++i) gArticles[i].isRead = false;
    UpdateMenus();
    Check(MenuEnabled(kArticleMenu, kArticleMarkAllRead),
          "Mark All as Read is available without selecting an article");
    HandleMenuCommand(kArticleMenu, kArticleMarkAllRead);
    for (i = 0; i < gMaxArticles; ++i)
        if (gArticles[i].isRead != !strcmp(gArticles[i].feed, gFeedFilter)) scoped = false;
    Check(scoped && !MenuEnabled(kArticleMenu, kArticleMarkAllRead) && gSelectedIndex == -1,
          "Mark All as Read affects only the current feed and preserves selection");
    gFeedFilter[0] = 0; UpdateMenus();
    HandleMenuCommand(kArticleMenu, kArticleMarkAllRead);
    Check(!HasUnreadArticlesInFeed(), "All Feeds bulk action marks the remaining queue read");
    {
        short loaded = gLoadedArticles, expectedUnread = 0, mark;
        Boolean visible = true;
        for (i = 0; i < loaded; ++i) {
            gArticles[i].isRead = (i % 2) == 0;
            if (!gArticles[i].isRead) ++expectedUnread;
        }
        HandleMenuCommand(kArticleMenu, kArticleShowUnread);
        for (i = 0; i < gMaxArticles; ++i) if (gArticles[i].isRead) visible = false;
        GetItemMark(GetMenuHandle(kArticleMenu), kArticleShowUnread, &mark);
        Check(visible && gMaxArticles == expectedUnread && gLoadedArticles == loaded && mark != 0,
              "Show Unread Only checks the menu choice and hides read rows locally");
        HandleMenuCommand(kArticleMenu, kArticleShowRead);
        visible = true;
        for (i = 0; i < gMaxArticles; ++i) if (!gArticles[i].isRead) visible = false;
        Check(visible && gMaxArticles == loaded - expectedUnread,
              "Show Read Only restores hidden read rows");
        HandleMenuCommand(kArticleMenu, kArticleShowAll);
        Check(gMaxArticles == loaded && gArticleVisibility == kShowAllArticles,
              "Show All Articles restores the complete loaded window");
    }
    strcpy(gFeedFilter, filter);
    if (selected >= 0) SelectArticle(selected);
    for (i = 0; i < gMaxArticles; ++i) {
        extern void SaveArticleState(short);
        gArticles[i].isRead = read[i]; SaveArticleState(i);
    }
    InvalidateReaderLayout(); UpdateContentPane(); UpdateArticleList();
    UpdateMenus(); PaintPending();
}

static void CheckTitleLinePreferences(void)
{
    short saved = gListTitleLines, initialHeight = GetListRowHeight();
    Check(saved == 3, "list titles default to three lines");
    preferenceLines = 1; PostTestKey(27, 0);
    HandleMenuCommand(kFileMenu, kFilePreferences);
    Check(gListTitleLines == saved && GetListRowHeight() == initialHeight,
          "Cancel leaves the title line preference and row height unchanged");
    preferenceLines = 4; PostTestKey(13, 0);
    HandleMenuCommand(kFileMenu, kFilePreferences);
    Check(gListTitleLines == 4 && GetListRowHeight() > initialHeight,
          "four title lines increase the article list row height");
    gListTitleLines = 1; InitTypography();
    Check(gListTitleLines == 4, "title line preference is saved and reloaded");
    preferenceLines = saved; PostTestKey(13, 0);
    HandleMenuCommand(kFileMenu, kFilePreferences);
    preferenceLines = 0; PaintPending();
}

static void CheckIconMask(void)
{
    Rect icon, background;
    OSErr error;
    SetPort(gMainWindow);
    SetRect(&icon, gContentRect.left + 16, gContentRect.top + 16,
            gContentRect.left + 48, gContentRect.top + 48);
    background = icon; InsetRect(&background, -8, -8);
    PaintRect(&background);
    error = PlotIconID(&icon, 0, 0, 128);
    Check(error == noErr && !GetPixel(icon.left + 10, icon.top + 16) &&
          GetPixel(icon.left + 12, icon.top + 16) && GetPixel(icon.left, icon.top),
          "native Beacon icon preserves opaque white fill, black ink, and outside transparency");
    UISmokeSnapshot("beacon-mask.pbm");
    UpdateContentPane(); PaintPending();
}

static void CheckInlineImageLayout(void)
{
    extern long UISmokeImageAt(short index);
    char *oldBody, *saved;
    short count;
    unsigned short offsets[kMaxImagesPerArticle];
    if (gSelectedIndex < 0) return;
    oldBody = GetCachedArticleBody(gSelectedIndex);
    if (!oldBody) return;
    saved = NewPtr(strlen(oldBody) + 1);
    if (!saved) { Check(false, "allocate inline-image fixture"); return; }
    strcpy(saved, oldBody);
    count = gArticles[gSelectedIndex].imageCount;
    memcpy(offsets, gArticles[gSelectedIndex].imageOffsets, sizeof(offsets));
    SetArticleBody(gSelectedIndex, "Before the photo.\r\rAfter the photo.");
    gArticles[gSelectedIndex].imageCount = 1;
    gArticles[gSelectedIndex].imageOffsets[0] = 19;
    InvalidateReaderLayout(); UpdateContentPane(); PaintPending();
    Check(UISmokeImageAt(0) > UISmokeBodyTop(), "inline photo follows its preceding body paragraph");
    UISmokeSnapshot("inline-photo.pbm");
    {
        char *fixture=NewPtr(4096);
        Handle previous=gArticles[gSelectedIndex].images[0];
        unsigned char previousState=gArticles[gSelectedIndex].imageState[0];
        Handle loaded=NewHandleClear(6L+20L*120L);
        if (fixture && loaded) {
            CachedImage *image;
            long top, before, difference;
            unsigned long full, strips, layouts;
            short i;
            strcpy(fixture,"Before the photo.\r\r");
            for (i=0; i<40; ++i) strcat(fixture,"A paragraph below the photo for scrolling verification.\r\r");
            gArticles[gSelectedIndex].images[0]=NULL;
            gArticles[gSelectedIndex].imageState[0]=0;
            SetArticleBody(gSelectedIndex,fixture);
            InvalidateReaderLayout(); UpdateContentPane(); PaintPending();
            top=UISmokeImageAt(0);
            ScrollReaderTo(top+GetBodyLineHeight()+10); PaintPending();
            before=gContentScroll;
            top=ReaderImageUpdateTop(0);
            HLock(loaded); image=(CachedImage *)*loaded;
            image->width=160; image->height=120; image->rowBytes=20;
            memset(image->bits,0xAA,20*120); HUnlock(loaded);
            gArticles[gSelectedIndex].images[0]=loaded;
            gArticles[gSelectedIndex].imageState[0]=1;
            difference=120-GetBodyLineHeight()-4;
            layouts=gReaderLayoutBuilds;
            ReaderImageLoaded(top);
            Check(gReaderLayoutBuilds==layouts,
                  "image arrival shifts cached sections without rewrapping a long article");
            Check(gContentScroll==before+difference,
                  "image growth anchors text after scrolling past its loading placeholder");
            full=gReaderPaintCount; strips=gReaderStripPaints;
            ScrollReaderTo(gContentScroll+GetBodyLineHeight());
            Check(gReaderPaintCount>full && gReaderStripPaints==strips,
                  "scrolling before the image update paints cannot reuse old-layout pixels");
            Check(gReaderLayoutBuilds==layouts,
                  "scrolling after image arrival reuses the measured text layout");
            PaintPending();
            {
                long height=gContentHeight, scroll=gContentScroll;
                InvalidateReaderLayout(); UpdateContentPane(); PaintPending();
                Check(gContentHeight==height && gContentScroll==scroll,
                      "incremental image geometry matches a complete rewrap");
            }
            UISmokeSnapshot("image-arrival-scroll.pbm");
            gArticles[gSelectedIndex].images[0]=previous;
            gArticles[gSelectedIndex].imageState[0]=previousState;
        } else Check(false,"allocate image arrival scrolling fixture");
        if (loaded) DisposeHandle(loaded);
        if (fixture) DisposePtr(fixture);
    }
    SetArticleBody(gSelectedIndex, saved);
    gArticles[gSelectedIndex].imageCount = count;
    memcpy(gArticles[gSelectedIndex].imageOffsets, offsets, sizeof(offsets));
    DisposePtr(saved);
    InvalidateReaderLayout(); UpdateContentPane(); PaintPending();
}

static void CheckFootnoteNavigation(void)
{
    extern void UISmokeActivateFootnote(short bodyOffset);
    char *oldBody, *saved, *fixture;
    short i, forward, back;
    long sourceScroll;
    if (gSelectedIndex<0) return;
    oldBody=GetCachedArticleBody(gSelectedIndex);
    if (!oldBody) return;
    saved=NewPtr(strlen(oldBody)+1); fixture=NewPtr(4096);
    if (!saved || !fixture) {
        if (saved) DisposePtr(saved);
        if (fixture) DisposePtr(fixture);
        Check(false,"allocate footnote navigation fixture"); return;
    }
    strcpy(saved,oldBody); strcpy(fixture,"Reference [1].\r\r");
    for (i=0; i<80; ++i) strcat(fixture,"Paragraph between reference and note.\r\r");
    strcat(fixture,"[1]: Footnote text.\r\r[Back to 1]");
    forward=strstr(fixture,"[1]")-fixture;
    back=strstr(fixture,"[Back to 1]")-fixture;
    SetArticleBody(gSelectedIndex,fixture);
    InvalidateReaderLayout(); UpdateContentPane(); PaintPending();
    ScrollReaderTo(0); sourceScroll=gContentScroll;
    UISmokeActivateFootnote(forward);
    Check(gContentScroll>sourceScroll,"footnote reference jumps down to its note");
    UISmokeSnapshot("footnote-links.pbm");
    UISmokeActivateFootnote(back);
    Check(gContentScroll==sourceScroll,"footnote return link restores original reader viewport");
    SetArticleBody(gSelectedIndex,saved); DisposePtr(saved); DisposePtr(fixture);
    InvalidateReaderLayout(); UpdateContentPane(); PaintPending();
}

static void CheckRichFormatting(void)
{
    extern short UISmokeTableRows(void);
    extern Boolean UISmokeHeadingVisible(void);
    extern void HandleEvent(EventRecord *event);
    char *saved, *fixture;
    WireLinks *metadata, *savedMetadata;
    short tableStart, tableEnd, images, i, selected, listTop;
    EventRecord event;
    if (gSelectedIndex<0 || !GetCachedArticleBody(gSelectedIndex)) return;
    saved=NewPtr(strlen(GetCachedArticleBody(gSelectedIndex))+1);
    fixture=NewPtr(4096); metadata=(WireLinks *)NewPtrClear(sizeof(WireLinks));
    if (!saved || !fixture || !metadata) {
        if (saved) DisposePtr(saved);
        if (fixture) DisposePtr(fixture);
        if (metadata) DisposePtr((Ptr)metadata);
        Check(false,"allocate rich-text fixture"); return;
    }
    strcpy(saved,GetCachedArticleBody(gSelectedIndex));
    savedMetadata=gArticles[gSelectedIndex].links; gArticles[gSelectedIndex].links=NULL;
    images=gArticles[gSelectedIndex].imageCount; gArticles[gSelectedIndex].imageCount=0;
    strcpy(fixture,"## A real subheading\r\rBold and italic, H2O and x2.\r\r");
    tableStart=strlen(fixture);
    strcat(fixture,"Publication | A long article title about the Macintosh that should never wrap | Author\rSix Colors  | A second row, preserving the same column positions              | Jason Snell\r\r");
    tableEnd=strlen(fixture);
    for (i=0; i<30; ++i) strcat(fixture,"A paragraph for testing keyboard paging within this article.\r\r");
    metadata->styleCount=4;
    metadata->styles[0].start=tableStart; metadata->styles[0].length=tableEnd-tableStart; metadata->styles[0].flags=WIRE_STYLE_PRE|WIRE_STYLE_TABLE;
    metadata->styles[1].start=strstr(fixture,"Bold")-fixture; metadata->styles[1].length=4; metadata->styles[1].flags=WIRE_STYLE_BOLD;
    metadata->styles[2].start=strstr(fixture,"italic")-fixture; metadata->styles[2].length=6; metadata->styles[2].flags=WIRE_STYLE_ITALIC;
    metadata->styles[3].start=strstr(fixture,"H2O")-fixture+1; metadata->styles[3].length=1; metadata->styles[3].flags=WIRE_STYLE_SUB;
    SetArticleBody(gSelectedIndex,fixture); Check(SetArticleLinks(gSelectedIndex,metadata),"cache rich-text metadata");
    DisposePtr((Ptr)metadata);
    ClearReaderSelection(); UpdateContentPane(); PaintPending();
    Check(UISmokeHeadingVisible(),"subheadings hide Markdown hashes");
    Check(UISmokeTableRows()==2,"table rows stay on one line each");
    Check(UISmokeHorizontalMaximum()>0,"wide table exposes horizontal scrolling");
    Check(UISmokeWrappedLinesFit(),"styled prose fits the article pane");
    UISmokeSnapshot("rich-formatting.pbm");
    selected=gSelectedIndex; listTop=gScrollOffset;
    memset(&event,0,sizeof(event)); event.what=keyDown; event.message=0x1F; event.modifiers=cmdKey;
    HandleEvent(&event);
    Check(gContentScroll>0 && gSelectedIndex==selected && gScrollOffset==listTop,"Command-Down pages the article without moving the list");
    event.what=autoKey; event.message=0x1E; event.modifiers=shiftKey; HandleEvent(&event);
    Check(gContentScroll==0 && gSelectedIndex==selected && gScrollOffset==listTop,"Shift-Up pages back with key repeat");
    SetArticleBody(gSelectedIndex,saved); gArticles[gSelectedIndex].links=savedMetadata;
    gArticles[gSelectedIndex].imageCount=images;
    DisposePtr(saved); DisposePtr(fixture);
    ClearReaderSelection(); UpdateContentPane(); PaintPending();
}

static void CheckAuthorMetadata(void)
{
    extern const char *UISmokeReaderText(void);
    char saved[kMaxAuthorLength + 1];
    if (gSelectedIndex < 0 || gSelectedIndex >= gMaxArticles) return;
    strcpy(saved, gArticles[gSelectedIndex].author);
    strcpy(gArticles[gSelectedIndex].author, "Test Author");
    ClearReaderSelection(); InvalidateReaderLayout(); UpdateContentPane(); PaintPending();
    Check(strstr(UISmokeReaderText(), " \321 Test Author\r") != NULL,
          "publisher and author share one line above the headline with an em dash");
    UISmokeSnapshot("article-author.pbm");
    gArticles[gSelectedIndex].author[0] = 0;
    ClearReaderSelection(); InvalidateReaderLayout(); UpdateContentPane(); PaintPending();
    Check(strstr(UISmokeReaderText(), " \321 ") == NULL,
          "missing author leaves no em dash or empty author row");
    strcpy(gArticles[gSelectedIndex].author, saved);
    ClearReaderSelection(); InvalidateReaderLayout(); UpdateContentPane(); PaintPending();
}

int RunUISmokeChecks(void)
{
    EventRecord event;
    short selected, listScroll;
    long contentScroll;
    Rect bounds;
    Handle scrap;
    long size, offset;
    TypographySetting settings[kTypeCount];
    char *body;
    PicHandle picture;
    Rect pictureFrame;
    unsigned long listPaints, readerPaints;
    short rowHeight;
    long documentHeight;
    Ptr pixels;
    long pixelLength;
    unsigned long layouts, stripPaints, started;
    short dragStart, dragEnd;
    Point click;
    ConnectionSettings connection;
    short available, installedFont;
    Str255 fontName;

    logFile = fopen("smoke.log", "w");
    if (logFile == NULL) return 1;
#ifdef TICKERTAPE_PREFERENCES_ONLY
    {
        ConnectionSettings saved = gConnection;
        preferencePane = 2;
        PostTestKey(27, 0);
        HandleMenuCommand(kFileMenu, kFilePreferences);
        Check(memcmp(&saved, &gConnection, sizeof(saved)) == 0,
              "Connection Cancel preserves settings");
        preferencePane = 1;
        PostTestKey(27, 0);
        HandleMenuCommand(kFileMenu, kFilePreferences);
        fprintf(logFile, "Preferences failures: %d\n", failures);
        fclose(logFile);
        return failures != 0;
    }
#endif
    {
        Point origin = {0, 0};
        Rect frame, desktop;
        SetPort(gMainWindow);
        LocalToGlobal(&origin);
        frame = (**((WindowPeek)gMainWindow)->strucRgn).rgnBBox;
        desktop = (**LMGetGrayRgn()).rgnBBox;
        Check(origin.h == desktop.left + 7 && origin.v == desktop.top + 26 &&
              gMainWindow->portRect.right - gMainWindow->portRect.left ==
                  desktop.right - desktop.left - 14 &&
              gMainWindow->portRect.bottom - gMainWindow->portRect.top ==
                  desktop.bottom - desktop.top - 33,
              "reader starts at its final global position and size");
        Check(frame.left >= desktop.left && frame.top >= desktop.top &&
              frame.right <= desktop.right && frame.bottom <= desktop.bottom,
              "initial reader frame fits completely inside the desktop");
        Check(frame.left - desktop.left == 6 && desktop.right - frame.right == 6 &&
              frame.top - desktop.top == 6 && desktop.bottom - frame.bottom == 6,
              "initial reader frame has equal desktop padding on all four sides");
    }
#ifndef RSS_DEMO_TRANSPORT
    {
        extern Boolean UISmokeReaderBlank(void);
        PaintPending();
        Check(gMaxArticles == 0 && gSelectedIndex == -1 &&
              !gUsingStartupSamples && UISmokeReaderBlank(),
              "live-mode startup has no placeholder news or selected article");
        UISmokeSnapshot("empty-live-startup.pbm");
        /* Explicit fixtures keep offline UI coverage separate from startup. */
        SetArticleVisibility(kShowAllArticles);
        ResetStartupSamples(); UpdateFeedsMenu();
        {
            char selectedID[9];
            char *selectedBody;
            Point header;
            Check(gNewestFirst, "live list defaults to newest first");
            gSelectedIndex = 1;
            strcpy(selectedID, gArticles[1].id); selectedBody = gArticles[1].body;
            SortArticles();
            Check(!strcmp(gArticles[0].id, "00000005") &&
                  !strcmp(gArticles[gSelectedIndex].id, selectedID) &&
                  gArticles[gSelectedIndex].body == selectedBody,
                  "newest-first sort preserves selected article and cached body");
            header.h = gListRect.left + 8; header.v = gMainWindow->portRect.top + 8;
            Check(HandleListHeaderClick(header) && !gNewestFirst &&
                  !strcmp(gArticles[0].id, "00000001"),
                  "Date header reverses the list to oldest first");
            gSelectedIndex = -1; gScrollOffset = 0; ClearReaderSelection();
        }
        UpdateArticleList(); UpdateContentPane(); PaintPending();
    }
#endif
#if defined(TICKERTAPE_RENDER_ONLY) && !defined(RSS_DEMO_TRANSPORT)
    SelectArticle(0); PaintPending();
    CheckAuthorMetadata(); CheckInlineImageLayout(); CheckFootnoteNavigation(); CheckRichFormatting();
    fprintf(logFile, "UI smoke: %d failure(s)\n", failures);
    fclose(logFile); FlushVol(NULL, 0);
    HandleMenuCommand(kFileMenu, kFileQuit);
    return failures;
#endif
    CheckTitleLinePreferences();
    CheckIconMask();
    CheckResizeChrome();
    CheckArticleMenu();
    CheckListScrolling();
#ifndef RSS_DEMO_TRANSPORT
    Check(gMaxArticles == 5, "explicit offline UI fixture contains all five sample articles");
    for (selected = 0; selected < gMaxArticles; ++selected) {
        body = GetCachedArticleBody(selected);
        Check(body != NULL && *body != 0,
              "seeded article body is cached before network access");
    }
    PaintPending();
    {
        extern Boolean UISmokeReaderBlank(void), UISmokeScrollArrowsAligned(void), UISmokePartialRowHasInk(void);
        Check(gSelectedIndex == -1 && UISmokeReaderBlank(),
              "unselected reader is completely blank");
        Check(UISmokeScrollArrowsAligned(), "pane down arrows share a baseline");
        UISmokeSnapshot("blank-reader.pbm");
        SelectArticle(0); SelectArticle(1); /* Leaving the first article marks it read. */
        {
            short count=gMaxArticles, selectedIndex=gSelectedIndex;
            unsigned long paints;
            gMaxArticles=0; gSelectedIndex=-1;
            DrawListPane(); /* Remember a completely empty startup list. */
            gMaxArticles=count; UpdateArticleList();
            paints=gListPaintCount;
            PreviewArticleSelection(0);
            Check(gListPaintCount==paints+1,
                  "first selection paints populated rows before inverting a startup highlight");
            gSelectedIndex=selectedIndex; PreviewArticleSelection(-1);
            FinishReaderArticleLoading(); PaintPending();
        }
        Check(QueueArticleSelection(0) && ReaderHasText(),
              "cached selection avoids a temporary loading pane");
        QueueArticleSelection(1);
        {
        Boolean cached=gArticles[0].hasContent;
        gArticles[0].hasContent=false;
        Check(QueueArticleSelection(0) && gSelectedIndex==1 && !ReaderHasText(),
              "queued selection hides the old reader without changing network identity");
        PaintPending();
        Check(!ReaderHasText() && !ReaderCanCopy(),
              "update events retain loading feedback and cannot copy the hidden old story");
        UISmokeSnapshot("article-loading.pbm");
        Check(!QueueArticleSelection(1), "clicking the active story cancels queued navigation");
        PaintPending();
        Check(ReaderHasText() && gSelectedIndex==1,
              "cancelling the queued selection restores the original reader");
        gArticles[0].hasContent=cached;
        }
        CheckAuthorMetadata(); CheckInlineImageLayout(); CheckFootnoteNavigation(); CheckRichFormatting();
        {
            Rect original = gMainWindow->portRect, pixel;
            SetRect(&pixel, original.left, gListRect.bottom - 2,
                    original.left + 1, gListRect.bottom - 1);
            SetPort(gMainWindow); ClipRect(&gMainWindow->portRect); PaintRect(&pixel);
            DrawListPane();
            Check(!GetPixel(pixel.left, pixel.top), "list repaint clears a stray pixel in the unused left edge");
            SizeWindow(gMainWindow, original.right - original.left,
                       original.bottom - original.top - 20, true);
            LayoutUI(); InvalRect(&gMainWindow->portRect); PaintPending();
            Check(UISmokePartialRowHasInk(), "next article paints its visible partial row instead of leaving a blank gap");
            UISmokeSnapshot("partial-row.pbm");
            SizeWindow(gMainWindow, original.right - original.left,
                       original.bottom - original.top, true);
            LayoutUI(); InvalRect(&gMainWindow->portRect); PaintPending();
        }
        Check(gFeedCount == 5, "startup Feeds menu lists all five sources");
        strcpy(gConnection.address, "192.0.2.1"); /* Unreachable configured server. */
        for (selected = 0; selected < 5; ++selected) {
            char source[33];
            strcpy(source, gFeedNames[selected]);
            SelectFeed(selected + kFeedsFirstSource); PaintPending();
            Check(gMaxArticles == 1 && !strcmp(gArticles[0].feed, source) &&
                  !strcmp(gFeedFilter, source) && GetCachedArticleBody(0) != NULL,
                  "startup feed filters locally and retains its body");
        }
        gConnection.address[0] = 0;
        SelectFeed(kFeedsAll); PaintPending();
        Check(gMaxArticles == 5 && !gFeedFilter[0] && gContentScroll == 0 &&
              UISmokeReaderScrollValue() == 0 && UISmokeListScrollValue() == 0,
              "All Feeds restores startup articles and resets both thumbs");
        Check(gArticles[0].isRead, "feed filtering preserves sample read state");
    }
    SelectArticle(0);
    PaintPending();
    UISmokeSnapshot("startup.pbm");
    {
        char *longBody = (char *)NewPtr(9999 + 1);
        short n, previousSize = gTypography[kTypeBody].size;
        if (longBody) {
            for (n = 0; n < 9999; ++n) longBody[n] = "Large glyphs gyp "[n % 17];
            longBody[9999] = 0;
            SetArticleBody(0, longBody); DisposePtr((Ptr)longBody);
            gTypography[kTypeBody].size = 72;
            InvalidateReaderLayout(); DrawContentPane();
            Check(UISmokeWrappedLinesFit(), "72-point text wraps within the reader width using native string metrics");
            Check(gContentHeight > 32767, "72-point article layout exceeds signed-short height safely");
            ScrollReaderTo(gContentHeight);
            Check(gContentScroll > 32767 && UISmokeReaderScrollValue() > 0,
                  "scaled native thumb reaches the end of a tall article");
            UISmokeSnapshot("large-font-bottom.pbm");
            PostTestKey(13, 0);
            HandleMenuCommand(kFileMenu, kFilePreferences);
            gTypography[kTypeBody].size = 12; InitTypography();
            Check(gTypography[kTypeBody].size == 72,
                  "new 72-point size can be selected, saved, and reloaded");
            gTypography[kTypeBody].size = previousSize;
            SelectArticle(1); PaintPending();
            Check(gContentScroll == 0 && UISmokeReaderScrollValue() == 0,
                  "next article resets thumb after a tall, large-font document");
        } else Check(false, "allocate tall-article fixture");
    }
    fprintf(logFile, "UI smoke: %d failure(s)\n", failures);
    fclose(logFile);
    FlushVol(NULL, 0);
    HandleMenuCommand(kFileMenu, kFileQuit);
    return failures;
#endif
    PaintPending();
    if (gSelectedIndex < 0 && gMaxArticles > 0) SelectArticle(0);
    PaintPending();
    pixelLength = (long)(qd.screenBits.rowBytes & 0x3FFF) *
                  (qd.screenBits.bounds.bottom - qd.screenBits.bounds.top);
    pixels = NewPtr(pixelLength);
    readerPaints = gReaderPaintCount;
    UISmokeSelectRange(0, 25);
    UISmokeSelectRange(0, 65);
    UISmokeSelectRange(15, 65);
    UISmokeSelectRange(15, 40);
    Check(gReaderPaintCount == readerPaints,
          "extending and reversing text selection causes no reader repaint");
    if (pixels != NULL) {
        BlockMove(qd.screenBits.baseAddr, pixels, pixelLength);
        DrawContentPane();
        Check(memcmp(pixels, qd.screenBits.baseAddr, pixelLength) == 0,
              "incremental selection highlight matches a full render");
        DisposePtr(pixels);
    } else Check(false, "allocate selection verification bitmap");
    UISmokeSelectRange(0, 0);
    Check(gMainWindow != NULL && FrontWindow() == gMainWindow, "reader launches");
    CheckAuthorMetadata(); CheckInlineImageLayout(); CheckFootnoteNavigation(); CheckRichFormatting();
    UISmokeSnapshot("reader.pbm");
    Check(GetMenuHandle(kEditMenu) != NULL && GetMenuHandle(134) == NULL,
          "Edit menu installed; redundant Window menu removed");
    Check(!MenuEnabled(kEditMenu, kEditCut) && !MenuEnabled(kEditMenu, kEditPaste) &&
          !MenuEnabled(kEditMenu, kEditUndo), "read-only editing commands disabled");
    Check(MenuKey('X') == 0 && MenuKey('V') == 0, "disabled shortcuts do not dispatch");

    SetPort(gMainWindow);
    body = GetCachedArticleBody(gSelectedIndex);
    click.h = gContentRect.left + 8;
    ScrollReaderTo(max(0L, UISmokeBodyTop() - GetContentViewportHeight() / 3));
    click.v = gContentRect.top + UISmokeBodyTop() - gContentScroll + 3;
    dragStart = UISmokeReaderHit(click);
    ApplyTypography(kTypeBody);
    mousePoints[0] = click;
    mousePoints[0].h += TextWidth(body, 0, 3);
    dragEnd = UISmokeReaderHit(mousePoints[0]);
    mouseIndex = 0; mouseCount = 1;
    memset(&event, 0, sizeof(event)); event.what = mouseDown;
    event.where = click; LocalToGlobal(&event.where);
    HandleMouseDown(&event);
    CopyReaderText();
    scrap = NewHandle(0); size = GetScrap(scrap, 'TEXT', &offset);
    Check(ReaderCanCopy() && dragEnd - dragStart == 3 && size == 3 &&
          memcmp(*scrap, body, 3) == 0,
          "mouse-down and tracked drag select and copy the hit-tested body text");
    DisposeHandle(scrap);
    UISmokeSnapshot("mouse-selection.pbm");
    UISmokeSelectRange(0, 0);
    event.where = mousePoints[0]; SetPort(gMainWindow); LocalToGlobal(&event.where);
    mouseIndex = 0; mouseCount = 0;
    Check(PostMouse(event.where, shiftKey), "queue a selection click during network polling");
    for (selected = 0; selected < 16 && mouseCount == 0; ++selected) NetworkPoll();
    CopyReaderText();
    scrap = NewHandle(0); size = GetScrap(scrap, 'TEXT', &offset);
    Check(mouseCount == -1 && ReaderCanCopy() && size == 3 && memcmp(*scrap, body, 3) == 0,
          "network polling handles reader selection and Copy instead of discarding clicks");
    DisposeHandle(scrap); UISmokeSelectRange(0, 0);
    Check(strstr(gArticles[gSelectedIndex].date, "2026") != NULL &&
          strstr(gArticles[gSelectedIndex].date, "UTC") == NULL &&
          (strstr(gArticles[gSelectedIndex].date, "AM") || strstr(gArticles[gSelectedIndex].date, "PM")),
          "reader metadata uses a human local publication date");
    Check(strlen(gArticles[gSelectedIndex].title) > 70 && UISmokeBodyTop() > 55,
          "reader headline wraps across multiple lines without one-line fitting");

    layouts = gReaderLayoutBuilds; readerPaints = gReaderPaintCount;
    stripPaints = gReaderStripPaints; started = TickCount();
    for (selected = 0; selected < 12; ++selected)
        ScrollReaderTo(gContentScroll + GetBodyLineHeight());
    fprintf(logFile, "Twelve cached arrow scrolls: %lu ticks\n", TickCount() - started);
    Check(gReaderLayoutBuilds == layouts && gReaderPaintCount == readerPaints &&
          gReaderStripPaints > stripPaints,
          "small scrolls reuse line layout and repaint only exposed strips");
    pixels = NewPtr(pixelLength);
    if (pixels) {
        BlockMove(qd.screenBits.baseAddr, pixels, pixelLength);
        DrawContentPane();
        Check(memcmp(pixels, qd.screenBits.baseAddr, pixelLength) == 0,
              "scrolled title, metadata, and body pixels match a full redraw");
        DisposePtr(pixels);
    } else Check(false, "allocate scrolling verification bitmap");
    UISmokeSnapshot("scrolled-reader.pbm");
    SelectArticle(1); PaintPending();
    Check(gContentScroll == 0 && UISmokeReaderScrollValue() == 0,
          "next article resets both content position and scrollbar thumb");
    SelectArticle(0); PaintPending();
    Check(GetCachedImage(0, 0) != NULL && UISmokeImageTop() < UISmokeBodyTop(),
          "decoded lead photo appears before the body without overlap");
    {
        short savedCount = gArticles[0].imageCount;
        Handle saved = gArticles[0].images[1], copy = gArticles[0].images[0];
        unsigned char savedState = gArticles[0].imageState[1];
        long top;
        gArticles[0].imageCount = 2; gArticles[0].images[1] = NULL;
        InvalidateReaderLayout(); UpdateContentPane(); PaintPending();
        top = ReaderImageUpdateTop(1); readerPaints = gReaderPaintCount;
        if (HandToHand(&copy) == noErr) {
            gArticles[0].images[1] = copy; gArticles[0].imageState[1] = 1;
            ReaderImageLoaded(top); PaintPending();
            Check(top > GetContentViewportHeight() && gReaderPaintCount == readerPaints,
                  "loading a later photo below the viewport does not repaint the reader");
            DisposeHandle(copy);
        } else Check(false, "allocate image arrival fixture");
        gArticles[0].images[1] = saved; gArticles[0].imageState[1] = savedState;
        gArticles[0].imageCount = savedCount;
        InvalidateReaderLayout(); UpdateContentPane(); PaintPending();
    }
    {
        extern short UISmokeImageWidth(void), UISmokeImageHeight(void);
        short width = UISmokeImageWidth(), height = UISmokeImageHeight();
        short requestWidth = GetReaderImageWidth();
        Rect original = gMainWindow->portRect;
        SizeWindow(gMainWindow, original.right - original.left - 60,
                   original.bottom - original.top, true);
        LayoutUI(); InvalRect(&gMainWindow->portRect); PaintPending();
        Check(UISmokeImageWidth() == width && UISmokeImageHeight() == height &&
              GetReaderImageWidth() == requestWidth,
              "window resizing preserves photo pixels and the default image request width");
        UISmokeSnapshot("clipped-photo.pbm");
        SizeWindow(gMainWindow, 360, original.bottom - original.top, true);
        LayoutUI(); InvalRect(&gMainWindow->portRect); PaintPending();
        {
            short previousSize = gTypography[kTypeBody].size;
            gTypography[kTypeBody].size = 36;
            InvalidateReaderLayout(); DrawContentPane();
            Check(UISmokeWrappedLinesFit(), "36-point body wraps correctly in a narrowed reader");
            gTypography[kTypeBody].size = previousSize;
            InvalidateReaderLayout(); DrawContentPane();
        }
        Check(UISmokeHorizontalMaximum() == max(0, width + 16 -
            (gContentRect.right - gContentRect.left)),
            "native horizontal bar represents fixed-size photo overflow");
        ScrollReaderTo(max(0L, UISmokeImageTop() - 4));
        UISmokeHorizontalArrow(inDownButton);
        Check(UISmokeHorizontalValue() == min(16, UISmokeHorizontalMaximum()),
              "horizontal right arrow advances the native scrollbar and photo");
        ScrollReaderHorizontallyTo(32767);
        Check(UISmokeHorizontalMaximum() > 0 &&
              UISmokeHorizontalValue() == UISmokeHorizontalMaximum(),
              "horizontal scrolling reaches the cropped right edge of the photo");
        UISmokeSnapshot("photo-scrolled-right.pbm");
        pixels = NewPtr(pixelLength);
        if (pixels) {
            BlockMove(qd.screenBits.baseAddr, pixels, pixelLength);
            DrawContentPane();
            Check(memcmp(pixels, qd.screenBits.baseAddr, pixelLength) == 0,
                  "horizontal strip scrolling matches a complete text and photo redraw");
            DisposePtr(pixels);
        } else Check(false, "allocate horizontal scrolling verification bitmap");
        UISmokeHorizontalArrow(inUpButton);
        Check(UISmokeHorizontalValue() == max(0, UISmokeHorizontalMaximum() - 16),
              "horizontal left arrow moves back toward the photo's left edge");
        ScrollReaderHorizontallyTo(0);
        Check(UISmokeHorizontalValue() == 0, "horizontal scrolling returns to the photo's left edge");
        SizeWindow(gMainWindow, original.right - original.left,
                   original.bottom - original.top, true);
        LayoutUI(); InvalRect(&gMainWindow->portRect); PaintPending();
    }

    ScrollReaderTo(0);
    UISmokeSnapshot("article-image.pbm");
    ScrollReaderTo(0);
    selected = gMaxArticles; gMaxArticles = 20; gScrollOffset = 10;
    UpdateScrollBars();
    Check(UISmokeListScrollValue() > 0, "prepare a scrolled article list");
    MarkAllArticlesRead();
    SyncWithServer(); PaintPending();
    Check(!HasUnreadArticlesInFeed(), "read state survives refresh of the article index");
    Check(gMaxArticles == selected && gScrollOffset == 0 && UISmokeListScrollValue() == 0 &&
          UISmokeReaderScrollValue() == 0, "Refresh resets both panes and native scrollbar thumbs");

    Check(GetMenuHandle(kFeedsMenu) != NULL && gFeedCount == 3,
          "Feeds menu contains All Feeds and each configured sample feed");
    SelectFeed(kFeedsFirstSource); PaintPending();
    Check(gMaxArticles == 1 && !strcmp(gArticles[0].feed, gFeedFilter) &&
          UISmokeListScrollValue() == 0 && UISmokeReaderScrollValue() == 0,
          "individual feed filters articles and resets scrolling");
    SelectFeed(kFeedsAll); PaintPending();
    Check(gMaxArticles == 3 && !gFeedFilter[0], "All Feeds restores the complete queue");
    {
        extern short DemoResponse(const char *, unsigned char *, unsigned long, unsigned long *);
        unsigned char *packet = (unsigned char *)NewPtr(32768);
        unsigned long length = 0;
        if (packet && DemoResponse("GET /sync?last_sync=0 HTTP/1.0\r\n\r\n", packet, 32768, &length) == 0) {
            strcpy(gFeedFilter, gFeedNames[0]);
            ParseIndexResponse(packet, length);
            Check(gMaxArticles == 1 && !strcmp(gArticles[0].feed, gFeedFilter),
                  "client filters an All Feeds response from a legacy server");
            gFeedFilter[0] = 0; SyncWithServer(); PaintPending();
        } else Check(false, "allocate legacy server filter fixture");
        if (packet) DisposePtr((Ptr)packet);
    }

#ifdef TICKERTAPE_RENDER_ONLY
    SelectArticle(1); PaintPending();
    UISmokeSnapshot("long-real-article.pbm");
    ScrollReaderTo(gContentHeight / 2);
    UISmokeSnapshot("long-real-middle.pbm");
    SelectArticle(2); PaintPending();
    UISmokeSnapshot("third-real-article.pbm");
    ScrollReaderTo(0);
    UISmokeSnapshot("third-real-image.pbm");
    fprintf(logFile, "UI smoke: %d failure(s)\n", failures);
    fclose(logFile); FlushVol(NULL, 0);
    HandleMenuCommand(kFileMenu, kFileQuit);
    return failures;
#endif

    SizeWindow(gMainWindow, 390, 215, true);
    LayoutUI();
    SelectArticle(min(1, gMaxArticles - 1));
    ScrollArticle(1);
    PaintPending();
    UISmokeSnapshot("resized.pbm");
    bounds = gMainWindow->portRect;
    selected = gSelectedIndex;
    listScroll = gScrollOffset;
    contentScroll = gContentScroll;
    Command('W');
    Check(!((WindowPeek)gMainWindow)->visible, "Command-W hides reader");
    Check(MenuKey('W') == 0, "Close disabled when no windows are open");
    memset(&event, 0, sizeof(event));
    event.what = keyDown;
    event.message = 31;
    HandleKeyDown(&event);
    Check(gSelectedIndex == selected, "hidden reader ignores navigation");
    Command('0');
    Check(((WindowPeek)gMainWindow)->visible && FrontWindow() == gMainWindow,
          "Command-0 reopens reader");
    Check(gSelectedIndex == selected && gScrollOffset == listScroll &&
          gContentScroll == contentScroll && EqualRect(&bounds, &gMainWindow->portRect),
          "closing preserves selection, scrolling, and size");

    Command('A');
    Check(ReaderCanCopy(), "Select All selects article text");
    Command('C');
    scrap = NewHandle(0);
    size = GetScrap(scrap, 'TEXT', &offset);
    Check(size > 0, "Copy writes a TEXT scrap");
    body = GetCachedArticleBody(gSelectedIndex);
    Check(size == strlen(gArticles[gSelectedIndex].title) + 1 +
          strlen(gArticles[gSelectedIndex].date[0] ? gArticles[gSelectedIndex].date : gArticles[gSelectedIndex].time) + 2 +
          strlen(gArticles[gSelectedIndex].feed) + 2 + (body ? strlen(body) : 0),
          "Select All copies the complete article, including offscreen text");
    UISmokeSnapshot("selected.pbm");
    fprintf(logFile, "TRACE: opening Clipboard\n"); fflush(logFile);
    ToggleClipboard();
    fprintf(logFile, "TRACE: Clipboard opened\n"); fflush(logFile);
    PaintPending();
    Check(ClipboardIsOpen() && FrontWindow() == gClipboardWindow && ClipboardHasText(),
          "Show Clipboard displays system text");
    UISmokeSnapshot("clipboard.pbm");
    fprintf(logFile, "TRACE: Clipboard snapshot saved\n"); fflush(logFile);
    Command('W');
    Check(!ClipboardIsOpen() && ((WindowPeek)gMainWindow)->visible,
          "Command-W closes only the front Clipboard window");
    ToggleClipboard();
    SelectAllClipboardText();
    Check(ClipboardCanCopy(), "Clipboard text can be selected");
    CopyClipboardText();
    Check(GetScrap(NULL, 'TEXT', &offset) == size,
          "copying all Clipboard text preserves its contents");
    CloseClipboard();

    ZeroScrap();
    PutScrap(18, 'TEXT', (Ptr)"From another app.\r");
    ToggleClipboard();
    PaintPending();
    Check(ClipboardHasText() && GetScrap(NULL, 'TEXT', &offset) == 18,
          "Clipboard reads external scrap without modifying it");
    SuspendClipboard();
    Check(ClipboardIsOpen() && !((WindowPeek)gClipboardWindow)->visible,
          "suspend hides Clipboard but remembers user choice");
    ResumeClipboard();
    Check(((WindowPeek)gClipboardWindow)->visible, "resume restores Clipboard");
    CloseClipboard();
    SetPort(gMainWindow);
    SetRect(&pictureFrame, 0, 0, 96, 48);
    picture = OpenPicture(&pictureFrame);
    FrameRect(&pictureFrame);
    TextFont(geneva); TextSize(12); TextFace(0);
    MoveTo(8, 20);
    DrawString((ConstStringPtr)"\017Clipboard image");
    ClosePicture();
    ZeroScrap();
    HLock((Handle)picture);
    PutScrap(GetHandleSize((Handle)picture), 'PICT', *(Handle)picture);
    HUnlock((Handle)picture);
    KillPicture(picture);
    ToggleClipboard();
    PaintPending();
    Check(!ClipboardHasText() && GetScrap(NULL, 'PICT', &offset) > 0,
          "Clipboard displays picture scrap without changing it");
    UISmokeSnapshot("picture.pbm");
    ZeroScrap();
    UpdateClipboard();
    PaintPending();
    Check(!ClipboardHasText() && !ClipboardCanCopy(), "empty Clipboard disables Copy");
    UISmokeSnapshot("empty.pbm");
    CloseClipboard();
    Command('0');
    SelectArticle(0);
    UpdateMenus();
    Check(!ReaderCanCopy() && !MenuEnabled(kEditMenu, kEditCopy),
          "changing article clears selection and disables Copy");
    PaintPending();
    listPaints = gListPaintCount;
    readerPaints = gReaderPaintCount;
    Command('R');
    Check(gMaxArticles > 0, "Refresh menu shortcut still works");
    fprintf(logFile, "Refresh paints: list=%lu reader=%lu\n",
            gListPaintCount - listPaints, gReaderPaintCount - readerPaints);
    Check(gListPaintCount - listPaints == 1 && gReaderPaintCount - readerPaints == 1,
          "Refresh repaints each pane once, including prefetch");
    listPaints = gListPaintCount;
    readerPaints = gReaderPaintCount;
    FetchArticle(gArticles[1].id);
    PaintPending();
    Check(gListPaintCount == listPaints && gReaderPaintCount == readerPaints,
          "prefetching unchanged nonselected article causes no repaint");

    memcpy(settings, gTypography, sizeof(settings));
    rowHeight = GetListRowHeight();
    DrawContentPane();
    documentHeight = gContentHeight;
    gTypography[kTypeReaderTitle].size = 18;
    InvalidateReaderLayout(); DrawContentPane();
    Check(GetListRowHeight() == rowHeight &&
          gContentHeight != documentHeight,
          "reader title size changes only the reader layout");
    memcpy(gTypography, settings, sizeof(settings));
    InvalidateReaderLayout(); DrawContentPane();
    gTypography[kTypeListTitle].size = 18;
    Check(GetListRowHeight() > rowHeight &&
          gContentHeight == documentHeight,
          "list title size changes only the list layout");
    memcpy(gTypography, settings, sizeof(settings));
    InvalidateReaderLayout();
    PaintPending();
    UISmokeSnapshot("two-line-list.pbm");
    {
        /* Install additional families in a disposable font suitcase. Reuse an
           existing bitmap association; no user System file is modified. */
        short previous = CurResFile(), file, i;
        OSErr error = noErr;
        CreateResFile((ConstStringPtr)"\010QA Fonts");
        file = OpenResFile((ConstStringPtr)"\010QA Fonts");
        if (file == -1) error = ResError();
        for (i = 0; error == noErr && i < 2; ++i) {
            Handle family = GetIndResource('FOND', 1);
            error = family == NULL ? fnfErr : HandToHand(&family);
            if (error == noErr) {
                HLock(family); *((short *)(*family + 2)) = 900 + i; HUnlock(family);
                UseResFile(file);
                AddResource(family, 'FOND', 900 + i,
                    i == 0 ? (ConstStringPtr)"\007QA Font" : (ConstStringPtr)"\013QA Font Two");
                error = ResError();
            }
        }
        UseResFile(previous);
        fprintf(logFile, "QA font installation error=%d\n", error);
        Check(error == noErr, "install additional fonts in a disposable QA suitcase");
    }
    PostTestKey(27, 0);
    HandleMenuCommand(kFileMenu, kFilePreferences);
    Check(memcmp(settings, gTypography, sizeof(settings)) == 0,
          "Escape cancels Preferences without changing settings");
    {
        short i, j, id, families = 0; ResType type; Boolean found, allFound = true;
        MenuHandle fonts = GetMenuHandle(200); Str255 itemName;
        for (i = 1; i <= CountResources('FOND'); ++i) {
            Handle family = GetIndResource('FOND', i);
            GetResInfo(family, &id, &type, fontName);
            if (!fontName[0] || fontName[1] == '.' || fontName[1] == '%') continue;
            ++families; found = false;
            for (j = 1; j <= CountMItems(fonts); ++j) {
                GetMenuItemText(fonts, j, itemName);
                if (EqualString(fontName, itemName, false, true)) found = true;
            }
            if (!found) allFound = false;
        }
        fprintf(logFile, "Font families: resources=%d menu=%d\n", families, CountMItems(fonts));
        Check(families >= 5 && CountMItems(fonts) > 4 && allFound,
              "font picker includes every named installed font family");
    }
    preferenceFont = CountMItems(GetMenuHandle(200));
    GetMenuItemText(GetMenuHandle(200), preferenceFont, fontName);
    GetFNum(fontName, &installedFont);
    PostTestKey('.', cmdKey);
    HandleMenuCommand(kFileMenu, kFilePreferences);
    Check(memcmp(settings, gTypography, sizeof(settings)) == 0,
          "Command-period cancels Preferences");
    gTypography[kTypeListTitle].size = 10;
    gTypography[kTypeListTitle].face = bold | italic;
    gTypography[kTypeReaderTitle].size = 18;
    PostTestKey(13, 0);
    HandleMenuCommand(kFileMenu, kFilePreferences);
    preferenceFont = 0;
    memcpy(settings, gTypography, sizeof(settings));
    memset(gTypography, 0, sizeof(settings));
    InitTypography();
    Check(memcmp(settings, gTypography, sizeof(settings)) == 0 &&
          gTypography[kTypeListTitle].size == 10 &&
          gTypography[kTypeReaderTitle].size == 18,
          "OK saves and reloads independent title settings");
    Check(gTypography[kTypeListTitle].font == installedFont,
          "an installed font can be selected, saved, and reloaded");
    connection = gConnection;
    preferencePane = 2;
    PostTestKey(27, 0);
    HandleMenuCommand(kFileMenu, kFilePreferences);
    Check(memcmp(&connection, &gConnection, sizeof(connection)) == 0,
          "Connection pane Cancel leaves stored settings unchanged");
    PostTestKey(13, 0);
    HandleMenuCommand(kFileMenu, kFilePreferences);
    Check(strcmp(gConnection.address, "192.0.2.1") == 0 &&
          gConnection.port == 8081 && gConnection.sample == connection.sample && gConnection.windowHours == 168,
          "Connection pane accepts address after Copy, Clear, and Paste");
    memset(&gConnection, 0, sizeof(gConnection));
    InitConnectionSettings();
    Check(strcmp(gConnection.address, "192.0.2.1") == 0 &&
          gConnection.port == 8081 && gConnection.sample == connection.sample && gConnection.windowHours == 168,
          "Connection settings persist in System Preferences");
    {
        /* Write the previous on-disk format and verify an actual upgrade read. */
        typedef struct { short version; char address[16]; unsigned short port, sample; } LegacyConnection;
        LegacyConnection legacy;
        FSSpec spec;
        short volume, refNum;
        long folder, count = sizeof(legacy);
        OSErr error;
        connection = gConnection;
        memset(&legacy, 0, sizeof(legacy));
        legacy.version = 1; strcpy(legacy.address, connection.address);
        legacy.port = connection.port; legacy.sample = connection.sample;
        error = FindFolder(-32768, kPreferencesFolderType, true, &volume, &folder);
        if (error == noErr) error = FSMakeFSSpec(volume, folder,
                                                (ConstStringPtr)"\017MacWire Network", &spec);
        if (error == noErr) error = FSpOpenDF(&spec, fsRdWrPerm, &refNum);
        if (error == noErr) {
            error = FSWrite(refNum, &count, &legacy);
            if (error == noErr) error = SetEOF(refNum, count);
            FSClose(refNum);
        }
        memset(&gConnection, 0, sizeof(gConnection)); InitConnectionSettings();
        Check(error == noErr && !strcmp(gConnection.address, connection.address) &&
              gConnection.port == connection.port && gConnection.sample == connection.sample &&
              gConnection.windowHours == 24,
              "version 1 connection preferences retain settings and default article age to 24 hours");
        gConnection = connection; SaveConnectionSettings();
    }
    Check(!TestBackendConnection(&gConnection, &available) &&
          strstr(NetworkErrorText(), "MacTCP") != NULL,
          "missing MacTCP reports a useful error without crashing");
    preferencePane = 0;
    PostTestKey(13, 0);
    HandleMenuCommand(kAppleMenu, 1);
    Check(true, "About accepts Return and draws without NoteAlert icon");

    DisposeHandle(scrap);
    fprintf(logFile, "UI smoke: %d failure(s)\n", failures);
    fclose(logFile);
    FlushVol(NULL, 0);
    HandleMenuCommand(kFileMenu, kFileQuit);
    return failures;
}
