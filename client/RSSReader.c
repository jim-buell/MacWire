/*
 * RSSReader.c — Main application entry point and event loop
 *
 * System 7 MacWire Client for Macintosh
 *
 * This is the main application file. It:
 *   1. Initializes the Macintosh system (toolbox)
 *   2. Opens the main two-pane window
 *   3. Runs the event loop
 *   4. Handles menu commands and keyboard shortcuts
 *   5. Coordinates network requests and UI updates
 */

#include "compat.h"
#include "typography.h"
#include "MenuCommands.h"
#include "articles.h"
#include "ReadHistory.h"
#include "ListPosition.h"
#include "Connection.h"
#include "Feeds.h"
#include <Events.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Fonts.h>
#include <Dialogs.h>

/* External function declarations */
void InitNetwork(void);
void InitUI(void);
void InitArticles(void);
void InitProtocol(void);
void RunEventLoop(void);
void HandleMenuCommand(short menuID, short item);
void HandleEvent(EventRecord *event);
void HandleMouseDown(EventRecord *event);
void HandleKeyDown(EventRecord *event);
void HandleUpdate(WindowPtr window);
void DoCommand(short command);

/* External global variables */
extern WindowPtr gMainWindow;
extern Rect gListRect, gContentRect;
extern short gSelectedIndex;
extern short gScrollOffset;
extern Boolean gContentLoaded;

/* Main window (definition; declared in compat.h) */
WindowPtr gMainWindow;

/* Closing the last window does not end the application. */
static Boolean gDone = false;
Boolean gInBackground = false;

/*
 * kMaxArticles — Maximum articles to display in the list
 * Limited by SE's 4MB RAM constraint
 */
#define kMaxArticles 50

/*
 * kPrefetchCount — Number of articles to prefetch ahead
 * Per plan section 11: 2-3 articles
 */
#define kPrefetchCount 3

/*
 * kListWidth — Width of the article list pane (pixels)
 * Per plan section 6: ~30-35% of total width
 */
#define kListWidth 200

/*
 * kTitleHeight — Height of the title bar
 */
#define kTitleHeight 18

/* Toolbox key-code and modifier masks (missing from Multiversal) */
#define charCodeMask 0x00FF
#define modKeyMask   0xFF00
#define cmdKeyMask   0xC000
#define upArrow      30
#define downArrow    31
#define leftArrow    28
#define rightArrow   29
#define pageUp       11
#define pageDown     12
#define returnKey    13
#define windowNormal 0

/* External handlers (defined in Articles.c, UI.c, Network.c, shim.c) */
extern void SelectPreviousArticle(void);
extern void SelectNextArticle(void);
extern void ScrollArticle(short direction);
extern void AdvanceToNextArticle(void);
extern void OpenSelectedArticle(void);
extern void ToggleMarkReadSelected(void);
extern void DrawListPane(void);
extern void DrawContentPane(void);
extern void SyncWithServer(void);
extern void HandleMenuClick(Point where);
extern void HandleListClick(Point where);
extern void HandleContentClick(Point where);

/*
 * BuildMenuBar — load each MENU resource and install it in the menu bar.
 * Without this the application has no menus at all.
 */
static void BuildMenuBar(void)
{
    MenuHandle appleMenu;
    SetMenuBar(GetNewMBar(kAppleMenu));
    appleMenu = GetMenu(kAppleMenu);
    if (appleMenu != nil) AppendResMenu(appleMenu, 'DRVR');
    DrawMenuBar();
    UpdateFeedsMenu();
}

static void UpdateReaderCursor(void)
{
    Point where;
    CursHandle cursor;
    UpdateReaderLinkHover();
    if (gInBackground) return;
    if (gMainWindow != NULL && FrontWindow() == gMainWindow) {
        SetPort(gMainWindow);
        GetMouse(&where);
        if (PtInRect(where, &gContentRect) && ReaderHasText()) {
            cursor = GetCursor(iBeamCursor);
            if (cursor != NULL) { SetCursor(*cursor); return; }
        }
    }
    InitCursor();
}

/*
 * Main entry point for a System 7 application
 */
int main(void)
{
    int exitStatus = 0;
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();

    // Finish toolbox initialization (menus, dialogs, cursor).
    InitMenus();
    TEInit();
    InitDialogs(nil);
    InitCursor();

    // Initialize components
    InitTypography();
    InitUI();
    InitReadHistory();
    InitArticles();
    InitNetwork();
    InitProtocol();
    LoadListPosition();

    // Install the menu bar from the MENU resources.
    BuildMenuBar();

    /* Expose the prepared window once; its update event paints it once. */
    if (gMainWindow != NULL) {
        ShowWindow(gMainWindow);
        SelectWindow(gMainWindow);
    }
    UpdateMenus();
    if (gConnection.sample || gConnection.address[0]) SyncWithServer();

#ifdef TICKERTAPE_UI_SMOKE
    {
        extern int RunUISmokeChecks(void);
        exitStatus = RunUISmokeChecks();
        gDone = true;
    }
#endif

    // Run the main event loop
    RunEventLoop();
    DisposeClipboard();
    SaveListPosition();
    FinishReadingSelected(); SaveReadHistory();
    ClearArticleBodies();
    if (gMainWindow != NULL) DisposeWindow(gMainWindow);
    return exitStatus;
}

/*
 * RunEventLoop — Standard Macintosh event loop
 *
 * Processes events until the user quits.
 */
void RunEventLoop(void)
{
    EventRecord event;
    Boolean moreEvents;
    unsigned long lastInput=TickCount();

    while (!gDone) {
        ApplyPendingArticleNavigation();
        ApplyDeferredAction();
        if (gDone) break;
        if (UserActionPending()) continue;
        SystemTask();
        moreEvents = WaitNextEvent(everyEvent, &event, 6, nil);

        if (moreEvents) {
            if (event.what==mouseDown || event.what==keyDown || event.what==autoKey)
                lastInput=TickCount();
            HandleEvent(&event);
        } else if (!gInBackground && FrontWindow()==gMainWindow &&
                   TickCount()-lastInput>=30UL) PrefetchIdleArticle();
        UpdateClipboard();
        UpdateMenus();
        UpdateReaderCursor();
    }
}

/*
 * HandleEvent — Dispatch events to handlers
 */
void HandleEvent(EventRecord *event)
{
    short what = event->what;

    switch (what) {
        case mouseDown:
            HandleMouseDown(event);
            break;
        case keyDown:
        case autoKey:
            HandleKeyDown(event);
            break;
        case updateEvt:
            HandleUpdate((WindowPtr)event->message);
            break;
        case activateEvt:
            if ((WindowPtr)event->message == gMainWindow ||
                (WindowPtr)event->message == gClipboardWindow) {
                SetPort((WindowPtr)event->message);
                InvalRect(&((WindowPtr)event->message)->portRect);
            }
            break;
        case osEvt:
            if (((unsigned long)event->message >> 24) == 1) {
                gInBackground = !(event->message & RESUME);
                if (gInBackground) SuspendClipboard();
                else {
                    ResumeClipboard();
                    DrawMenuBar();
                }
                if (gMainWindow != NULL) {
                    SetPort(gMainWindow);
                    InvalRect(&gMainWindow->portRect);
                }
            }
            break;
    }
}

/*
 * HandleMouseDown — Process mouse clicks
 *
 * Handles:
 *   - Title bar dragging
 *   - Window resizing (if enabled)
 *   - Clicks in article list (selection)
 *   - Clicks in article content (scrolling)
 *   - Menu bar clicks
 *   - Scroll bar clicks
 */
void HandleMouseDown(EventRecord *event)
{
    WindowPtr whichWindow;
    short partCode;
    Point where;
    Point globalWhere;

    where = event->where;
    globalWhere = where;

    partCode = FindWindow(globalWhere, &whichWindow);

    switch (partCode) {
        case inMenuBar:
            HandleMenuClick(globalWhere);
            break;
        case inContent:
            if (whichWindow == gMainWindow) {
                /* The first click in an inactive window only activates it. */
                if (FrontWindow() != whichWindow) {
                    SelectWindow(whichWindow);
                    break;
                }
                SetPort(gMainWindow);
                GlobalToLocal(&where);
                if (HandleScrollBarClick(where)) break;
                if (HandleListHeaderClick(where)) break;
                // Check if click is in the list pane
                if (where.h < gListRect.right) {
                    // Click in article list — select article
                    HandleListClick(where);
                } else {
                    // Click in content pane — scroll
                    HandleReaderTextClick(where, (event->modifiers & shiftKey) != 0);
                }
            } else if (whichWindow == gClipboardWindow) {
                if (FrontWindow() != whichWindow) {
                    SelectWindow(whichWindow);
                    break;
                }
                SetPort(whichWindow);
                GlobalToLocal(&where);
                HandleClipboardClick(where, (event->modifiers & shiftKey) != 0);
            }
            break;
        case inSysWindow:
            SystemClick(event, whichWindow);
            break;
        case inDrag:
            DragWindow(whichWindow, globalWhere, &qd.screenBits.bounds);
            break;
        case inGrow:
            if (whichWindow == gMainWindow) {
                Rect limits;
                long dimensions;
                Point contentOrigin;
                short maxWidth, maxHeight;
                contentOrigin.h = 0;
                contentOrigin.v = 0;
                SetPort(gMainWindow);
                LocalToGlobal(&contentOrigin);
                maxWidth = qd.screenBits.bounds.right - contentOrigin.h - 4;
                maxHeight = qd.screenBits.bounds.bottom - contentOrigin.v - 4;
                SetRect(&limits, 360, 190, max(360, maxWidth),
                        max(190, maxHeight));
                dimensions = GrowWindow(gMainWindow, globalWhere, &limits);
                if (dimensions != 0) {
                    SizeWindow(gMainWindow, (short)(dimensions & 0xFFFF),
                               (short)((unsigned long)dimensions >> 16), true);
                    SetPort(gMainWindow);
                    LayoutUI();
                    InvalRect(&gMainWindow->portRect);
                }
            }
            break;
        case inZoomIn:
        case inZoomOut:
            if (whichWindow == gMainWindow && TrackBox(whichWindow, globalWhere, partCode))
                ZoomReaderWindow(partCode);
            break;
        case inGoAway:
            if (TrackGoAway(whichWindow, globalWhere)) {
                if (whichWindow == gMainWindow) HideWindow(gMainWindow);
                else if (whichWindow == gClipboardWindow) CloseClipboard();
                else if (((WindowPeek)whichWindow)->windowKind < 0)
                    CloseDeskAcc(((WindowPeek)whichWindow)->windowKind);
            }
            break;
    }
}

/*
 * HandleKeyDown — Process keyboard input
 *
 * Key bindings (per plan section 8):
 *   Up Arrow    — Previous article
 *   Down Arrow  — Next article
 *   Page Up     — Scroll article upward
 *   Page Down   — Scroll article downward
 *   Space       — Advance to next article (when at bottom)
 *   Enter       — Focus/open selected article
 *   M           — Mark read/unread
 */
void HandleKeyDown(EventRecord *event)
{
    short keyCode = event->message & charCodeMask;
    if (!gInBackground && gMainWindow != NULL && FrontWindow() == gMainWindow &&
        ((event->modifiers & cmdKey) || (event->modifiers & shiftKey)) &&
        (keyCode == upArrow || keyCode == downArrow)) {
        ScrollArticle(keyCode == upArrow ? -1 : 1);
        return;
    }

    if (event->modifiers & cmdKey) {
        /* Do not repeatedly open dialogs or close windows on key repeat. */
        if (event->what == keyDown) DoCommand(keyCode);
        return;
    }
    if (gInBackground) return;
    if (FrontWindow() == gClipboardWindow && gClipboardWindow != NULL) {
        if (keyCode == pageUp || keyCode == upArrow) ScrollClipboard(-1);
        else if (keyCode == pageDown || keyCode == downArrow || keyCode == ' ')
            ScrollClipboard(1);
        return;
    }
    if (gMainWindow == NULL || !((WindowPeek)gMainWindow)->visible ||
        FrontWindow() != gMainWindow) return;

    // Arrow keys and navigation
    switch (keyCode) {
        case upArrow:
            SelectPreviousArticle();
            break;
        case downArrow:
            SelectNextArticle();
            break;
        case pageUp:
            ScrollArticle(-1);
            break;
        case pageDown:
            ScrollArticle(1);
            break;
        case ' ':  // Space
            AdvanceToNextArticle();
            break;
        case returnKey:
            OpenSelectedArticle();
            break;
        case 'm':
        case 'M':
            ToggleMarkReadSelected();
            break;
    }
}

/*
 * HandleUpdate — Redraw window content
 */
void HandleUpdate(WindowPtr window)
{
    SetPort(window);
    BeginUpdate(window);

    if (window == gMainWindow) {
        /* BeginUpdate's visible region contains only the queued damage. */
        {
            Rect listDamage = gListRect;
            listDamage.top = window->portRect.top;
            listDamage.right += 16; /* Header spans the list scrollbar too. */
            if (RectInRgn(&listDamage, window->visRgn)) DrawListPane();
        }
        if (RectInRgn(&gContentRect, window->visRgn)) DrawContentPane();
        DrawControls(gMainWindow);
        DrawStatusLine();
    } else if (window == gClipboardWindow) DrawClipboard();

    EndUpdate(window);
}

/*
 * HandleMenuCommand — Process menu selections
 */
static void SetItemEnabled(short menuID, short item, Boolean enabled)
{
    MenuHandle menu = GetMenuHandle(menuID);
    if (menu == NULL) return;
    if (enabled) EnableItem(menu, item);
    else DisableItem(menu, item);
}

void UpdateMenus(void)
{
    WindowPtr front = FrontWindow();
    Boolean reader = front != NULL && front == gMainWindow;
    Boolean clipboard = front != NULL && front == gClipboardWindow;
    Boolean accessory = front != NULL && ((WindowPeek)front)->windowKind < 0;
    Boolean article = reader && gSelectedIndex >= 0 && gSelectedIndex < gMaxArticles;
    MenuHandle edit = GetMenuHandle(kEditMenu);
    SetItemEnabled(kFileMenu, kFileShowReader, gMainWindow != NULL);
    SetItemEnabled(kFileMenu, kFileClose, reader || clipboard || accessory);
    SetItemEnabled(kFeedsMenu, kFeedsRefresh, true);
    MenuHandle actions = GetMenuHandle(kArticleMenu);
    SetItemEnabled(kArticleMenu, kArticlePrevious, article && gSelectedIndex > 0);
    SetItemEnabled(kArticleMenu, kArticleNext, reader && gSelectedIndex < gMaxArticles - 1);
    SetItemEnabled(kArticleMenu, kArticleNextUnread, reader && FindNextUnreadArticle() >= 0);
    SetItemEnabled(kArticleMenu, kArticleMarkRead, article);
    SetItemEnabled(kArticleMenu, kArticleMarkAllRead, reader && HasUnreadArticlesInFeed());
    SetItemEnabled(kArticleMenu, kArticleShowAll, reader);
    SetItemEnabled(kArticleMenu, kArticleShowUnread, reader);
    SetItemEnabled(kArticleMenu, kArticleShowRead, reader);
    if (actions != NULL) {
        CheckItem(actions, kArticleShowAll, gArticleVisibility == kShowAllArticles);
        CheckItem(actions, kArticleShowUnread, gArticleVisibility == kShowUnreadArticles);
        CheckItem(actions, kArticleShowRead, gArticleVisibility == kShowReadArticles);
    }
    if (actions != NULL)
        SetMenuItemText(actions, kArticleMarkRead, article && gArticles[gSelectedIndex].isRead ?
                       (ConstStringPtr)"\016Mark as Unread" : (ConstStringPtr)"\014Mark as Read");
    SetItemEnabled(kEditMenu, kEditUndo, accessory);
    SetItemEnabled(kEditMenu, kEditCut, accessory);
    SetItemEnabled(kEditMenu, kEditPaste, accessory);
    SetItemEnabled(kEditMenu, kEditClear, accessory);
    SetItemEnabled(kEditMenu, kEditCopy, accessory ||
                   (reader && ReaderCanCopy()) || (clipboard && ClipboardCanCopy()));
    SetItemEnabled(kEditMenu, kEditSelectAll,
                   (reader && ReaderHasText()) || (clipboard && ClipboardHasText()));
    if (edit != NULL)
        SetMenuItemText(edit, kEditClipboard, ClipboardIsOpen() ?
                       (ConstStringPtr)"\016Hide Clipboard" :
                       (ConstStringPtr)"\016Show Clipboard");
}

/* A modal settings/about box accepts only its own controls. */
static void SetModalMenus(Boolean modal)
{
    SetItemEnabled(kFileMenu, 0, !modal);
    SetItemEnabled(kEditMenu, 0, !modal);
    SetItemEnabled(kArticleMenu, 0, !modal);
    SetItemEnabled(kFeedsMenu, 0, !modal);
    DrawMenuBar();
}

static void ShowAboutDialog(void)
{
    DialogPtr dialog = GetNewDialog(135, NULL, (WindowPtr)-1);
    short item;
    if (dialog == NULL) return;
    SetPort(dialog);
    PrepareStandardDialog(dialog, 5, 1);
    ShowWindow(dialog);
    do {
        ModalDialog(NewModalFilterUPP(StandardDialogFilter), &item);
    } while (item != 1);
    DisposeDialog(dialog);
    if (gMainWindow != NULL) SetPort(gMainWindow);
}

void DispatchMenuChoice(long choice)
{
    if (choice != 0)
        HandleMenuCommand((short)(choice >> 16), (short)(choice & 0xFFFF));
    HiliteMenu(0);
}

void HandleMenuCommand(short menuID, short item)
{
    WindowPtr front = FrontWindow();
    switch (menuID) {
        case kFeedsMenu:
            if (item == kFeedsRefresh) RefreshFeedsFromServer();
            else SelectFeed(item);
            break;
        case kAppleMenu:
            if (item == 1) {
                SetModalMenus(true);
                ShowAboutDialog();
                SetModalMenus(false);
            } else if (item > 2) {
                Str255 deskAccessory;
                GetMenuItemText(GetMenu(kAppleMenu), item, deskAccessory);
                OpenDeskAcc(deskAccessory);
            }
            break;
        case kFileMenu:
            switch (item) {
                case kFileShowReader:
                    if (gMainWindow != NULL) {
                        ShowWindow(gMainWindow);
                        SelectWindow(gMainWindow);
                        SetPort(gMainWindow);
                        InvalRect(&gMainWindow->portRect);
                    }
                    break;
                case kFileClose:
                    if (front != NULL && front == gMainWindow) HideWindow(front);
                    else if (front != NULL && front == gClipboardWindow) CloseClipboard();
                    else if (front != NULL && ((WindowPeek)front)->windowKind < 0)
                        CloseDeskAcc(((WindowPeek)front)->windowKind);
                    break;
                case kFilePreferences:
                    SetModalMenus(true);
                    ShowTypographyDialog();
                    SetModalMenus(false);
                    break;
                case kFileQuit:
                    gDone = true;
                    break;
            }
            break;
        case kEditMenu:
            if (item == kEditClipboard) ToggleClipboard();
            else if (item == kEditSelectAll) {
                if (front != NULL && front == gMainWindow) SelectAllReaderText();
                else if (front != NULL && front == gClipboardWindow) SelectAllClipboardText();
            } else {
                short systemCommand = -1;
                if (item == kEditUndo) systemCommand = 0;
                else if (item == kEditCut) systemCommand = 2;
                else if (item == kEditCopy) systemCommand = 3;
                else if (item == kEditPaste) systemCommand = 4;
                else if (item == kEditClear) systemCommand = 5;
                if (systemCommand >= 0 && !SystemEdit(systemCommand) && item == kEditCopy) {
                    if (front != NULL && front == gMainWindow) CopyReaderText();
                    else if (front != NULL && front == gClipboardWindow) CopyClipboardText();
                }
            }
            break;
        case kArticleMenu:
            if (front != gMainWindow || !front || !((WindowPeek)front)->visible) break;
            switch (item) {
                case kArticlePrevious: SelectPreviousArticle(); break;
                case kArticleNext: SelectNextArticle(); break;
                case kArticleNextUnread: SelectNextUnreadArticle(); break;
                case kArticleMarkRead: ToggleMarkReadSelected(); break;
                case kArticleMarkAllRead: MarkAllArticlesRead(); break;
                case kArticleShowAll: SetArticleVisibility(kShowAllArticles); break;
                case kArticleShowUnread: SetArticleVisibility(kShowUnreadArticles); break;
                case kArticleShowRead: SetArticleVisibility(kShowReadArticles); break;
            }
            break;
    }
    UpdateMenus();
}

/*
 * DoCommand — Process command-key combinations
 */
void DoCommand(short command)
{
    UpdateMenus();
    DispatchMenuChoice(MenuKey((CharParameter)command));
}
