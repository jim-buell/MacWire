/* Cooperative input routing while requests or reader layout are active. */
#include "compat.h"
#include "Connection.h"
#include "WaitCursor.h"

extern DialogPtr gConnectionDialog;
extern Boolean gConnectionCloseRequested;

/* One event is held only until the current request/layout unwinds. Other
   events stay in the Toolbox queue and retain their original order. */
static EventRecord gDeferredEvent;
static Boolean gHasDeferredEvent;
static long gDeferredMenu;
int DeferredActionPending(void) { return gHasDeferredEvent || gDeferredMenu!=0; }
int UserActionPending(void) { return ArticleNavigationPending() || DeferredActionPending(); }
static int DeferAction(const EventRecord *event)
{
    gDeferredEvent=*event; gHasDeferredEvent=true;
    return 1;
}
void ApplyDeferredAction(void)
{
    extern void HandleEvent(EventRecord *event);
    extern void DispatchMenuChoice(long choice);
    if (gDeferredMenu) {
        long choice=gDeferredMenu;
        gDeferredMenu=0;
        ClearCancelledRequest();
        DispatchMenuChoice(choice);
    } else if (gHasDeferredEvent) {
        EventRecord event=gDeferredEvent;
        gHasDeferredEvent=false;
        ClearCancelledRequest();
        HandleEvent(&event);
    }
}
int PollReaderAction(void)
{
    EventRecord event;
    if (UserActionPending()) return 1;
    /* Never dispatch callbacks from a partially constructed layout. */
    if (gConnectionDialog==NULL && EventAvail(mDownMask|keyDownMask|autoKeyMask,&event) &&
        (event.what==keyDown || event.what==autoKey || event.what==mouseDown) &&
        GetNextEvent(mDownMask|keyDownMask|autoKeyMask,&event)) return DeferAction(&event);
    return 0;
}

int NetworkPoll(void)
{
    extern void HandleEvent(EventRecord *event);
    EventRecord event;
    WindowPtr window;
    Point where;
    short type;
    Handle button;
    Rect rect;
    if (UserActionPending()) return 1;
    UpdateNetworkWaitCursor();
    if (!WaitNextEvent(everyEvent, &event, 1, NULL)) {
        UpdateReaderLinkHover();
        return 0;
    }
    if (event.what == keyDown &&
        ((event.message & 255) == 27 ||
         ((event.modifiers & cmdKey) && (event.message & 255) == '.'))) return 1;
    if (gConnectionDialog==NULL && event.what==mouseDown &&
        FindWindow(event.where,&window)==inMenuBar) {
        extern void UpdateMenus(void);
        UpdateMenus();
        gDeferredMenu=MenuSelect(event.where);
        HiliteMenu(0);
        return DeferredActionPending();
    }
    /* Navigation cancels the current stream; selection runs after its shared
       MacTCP storage has unwound, preventing nested requests. */
    if (gConnectionDialog == NULL && FrontWindow() == gMainWindow &&
        (event.what == keyDown || event.what == autoKey)) {
        short key = event.message & 255;
        if ((key == 0x1E || key == 0x1F) && (event.modifiers & (cmdKey | shiftKey))) {
            ScrollArticle(key == 0x1E ? -1 : 1);
            UpdateNetworkWaitCursor();
            return 0;
        }
        if ((key == 0x1E || key == 0x1F) && !(event.modifiers & cmdKey))
            return QueueArticleNavigation(key == 0x1E ? -1 : 1);
        if (!(event.modifiers & cmdKey) && (key==0x0B || key==0x0C)) {
            ScrollArticle(key==0x0B ? -1 : 1);
            return 0;
        }
        if (!(event.modifiers & cmdKey) && key==' ')
            return QueueArticleNavigation(1);
    }
    /* Body selection and scrolling cannot start another HTTP request. Keep
       those interactions available during prefetch; article/menu navigation
       waits for the active stream, whose parameter storage is shared. */
    if (gConnectionDialog == NULL && FrontWindow() == gMainWindow) {
        if (event.what == mouseDown &&
            FindWindow(event.where, &window) == inContent && window == gMainWindow) {
            SetPort(gMainWindow); where = event.where; GlobalToLocal(&where);
            if (PtInRect(where, &gListRect)) {
                short row=(where.v-gListRect.top)/GetListRowHeight()+gScrollOffset;
                return QueueArticleSelection(row);
            }
            if (!HandleScrollBarClick(where) && PtInRect(where, &gContentRect))
                HandleReaderTextClick(where, (event.modifiers & shiftKey) != 0);
            UpdateNetworkWaitCursor();
            return 0;
        }
        if (event.what == keyDown && (event.modifiers & cmdKey)) {
            short key = event.message & 255;
            if (key == 'c' || key == 'C') { CopyReaderText(); return 0; }
            else if (key == 'a' || key == 'A') { SelectAllReaderText(); return 0; }
        }
    }
    if (event.what == updateEvt && (WindowPtr)event.message == gConnectionDialog) {
        SetPort(gConnectionDialog);
        BeginUpdate(gConnectionDialog);
        DrawDialog(gConnectionDialog);
        EndUpdate(gConnectionDialog);
    } else if (event.what == mouseDown && gConnectionDialog != NULL) {
        where = event.where;
        if (FindWindow(where, &window) == inContent && window == gConnectionDialog) {
            SetPort(window); GlobalToLocal(&where);
            GetDialogItem(gConnectionDialog, 2, &type, &button, &rect);
            if (PtInRect(where, &rect) && TrackControl((ControlHandle)button, where, NULL)) {
                gConnectionCloseRequested = true;
                return 1;
            }
        }
    } else if (event.what == updateEvt || event.what == osEvt || event.what == activateEvt)
        HandleEvent(&event);
    else if (gConnectionDialog==NULL &&
             (event.what==mouseDown || event.what==keyDown || event.what==autoKey))
        return DeferAction(&event);
    /* Event handlers can set the arrow or I-beam; restore the animation now. */
    UpdateNetworkWaitCursor();
    return 0;
}
