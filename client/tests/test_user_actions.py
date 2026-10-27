"""Exercise shipping cooperative event routing with a controllable Toolbox queue."""
import ctypes as c
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class UserActionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        path = Path(cls.temp.name)
        (path / 'support.h').write_text('''
#pragma once
#include <stddef.h>
typedef unsigned char Boolean;
typedef void *WindowPtr, *DialogPtr, *Handle, *ControlHandle;
typedef struct { short v,h; } Point;
typedef struct { short top,left,bottom,right; } Rect;
typedef struct { short what; unsigned long message,when; Point where; short modifiers; } EventRecord;
enum { false=0,true=1,mouseDown=1,keyDown=3,autoKey=5,updateEvt=6,
activateEvt=8,osEvt=15,cmdKey=256,shiftKey=512,inContent=3,inMenuBar=1,
mDownMask=2,keyDownMask=8,autoKeyMask=32,everyEvent=-1 };
extern WindowPtr gMainWindow;
extern DialogPtr gConnectionDialog;
extern Boolean gConnectionCloseRequested;
extern Rect gListRect,gContentRect;
extern short gScrollOffset;
int WaitNextEvent(short,EventRecord *,long,void *);
int EventAvail(short,EventRecord *);
int GetNextEvent(short,EventRecord *);
WindowPtr FrontWindow(void);
short FindWindow(Point,WindowPtr *);
long MenuSelect(Point);
void HiliteMenu(short);
void UpdateMenus(void);
void SetPort(void *);
void GlobalToLocal(Point *);
int PtInRect(Point,const Rect *);
int HandleScrollBarClick(Point);
void HandleReaderTextClick(Point,Boolean);
void UpdateReaderLinkHover(void);
void ScrollArticle(short);
short GetListRowHeight(void);
void CopyReaderText(void);
void SelectAllReaderText(void);
void BeginUpdate(void *);
void DrawDialog(void *);
void EndUpdate(void *);
void GetDialogItem(void *,short,short *,Handle *,Rect *);
int TrackControl(ControlHandle,Point,void *);
''')
        (path / 'toolbox.c').write_text('''
#include "support.h"
#include "Connection.h"
WindowPtr gMainWindow=(void *)1;
DialogPtr gConnectionDialog;
Boolean gConnectionCloseRequested;
Rect gListRect={0,0,200,100},gContentRect={0,100,200,300};
short gScrollOffset=3;
EventRecord testEvent;
int testQueued,testHandled,testScroll,testSelected=-1,testNavigation,
testCopies,testAll,testHover,testCursor,testCancelledCleared,testPart=3,
testMenuChoice,testDispatched;
WindowPtr testFront=(void *)1;
void TestEvent(short what,short key,short modifiers,short x,short y,short part) {
testEvent.what=what; testEvent.message=key; testEvent.modifiers=modifiers;
testEvent.where.h=x; testEvent.where.v=y; testPart=part; testQueued=1; }
int WaitNextEvent(short m,EventRecord *e,long ticks,void *r) {
(void)m;(void)ticks;(void)r;if(!testQueued)return 0;*e=testEvent;testQueued=0;return 1; }
int EventAvail(short m,EventRecord *e) {
if(!testQueued || !(m&(1<<testEvent.what)))return 0;*e=testEvent;return 1; }
int GetNextEvent(short m,EventRecord *e) { return WaitNextEvent(m,e,0,NULL); }
int ArticleNavigationPending(void) { return testNavigation!=0; }
int QueueArticleNavigation(short direction) { testNavigation+=direction; return testNavigation!=0; }
int QueueArticleSelection(short index) { testSelected=index;testNavigation=1;return 1; }
void ClearCancelledRequest(void) { ++testCancelledCleared; }
void HandleEvent(EventRecord *e) { testHandled=e->what; }
WindowPtr FrontWindow(void) { return testFront; }
short FindWindow(Point p,WindowPtr *w) { (void)p;*w=gMainWindow;return testPart; }
long MenuSelect(Point p) { (void)p;return testMenuChoice; }
void HiliteMenu(short m) { (void)m; }
void UpdateMenus(void) {}
void DispatchMenuChoice(long choice) { testDispatched=choice; }
void SetPort(void *p) { (void)p; }
void GlobalToLocal(Point *p) { (void)p; }
int PtInRect(Point p,const Rect *r) { return p.h>=r->left&&p.h<r->right&&p.v>=r->top&&p.v<r->bottom; }
int HandleScrollBarClick(Point p) { (void)p;return 0; }
void HandleReaderTextClick(Point p,Boolean b) { (void)p;(void)b; }
void UpdateReaderLinkHover(void) { ++testHover; }
void UpdateNetworkWaitCursor(void) { ++testCursor; }
void ScrollArticle(short direction) { testScroll+=direction; }
short GetListRowHeight(void) { return 20; }
void CopyReaderText(void) { ++testCopies; }
void SelectAllReaderText(void) { ++testAll; }
void BeginUpdate(void *p) { (void)p; }
void DrawDialog(void *p) { (void)p; }
void EndUpdate(void *p) { (void)p; }
void GetDialogItem(void *d,short i,short *t,Handle *h,Rect *r) { (void)d;(void)i;(void)t;(void)h;(void)r; }
int TrackControl(ControlHandle c,Point p,void *f) { (void)c;(void)p;(void)f;return 0; }
''')
        library = path / 'actions.so'
        subprocess.run(['cc', '-std=c99', '-Wall', '-Werror=implicit-function-declaration',
                        '-shared', '-fPIC', '-DRSS_COMPAT_H', '-I'+str(path),
                        '-I'+str(ROOT), '-include', str(path/'support.h'),
                        str(ROOT/'UserActions.c'), str(path/'toolbox.c'),
                        '-o', str(library)], check=True)
        cls.lib = c.CDLL(str(library))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def value(self, name, value=None):
        field = c.c_int.in_dll(self.lib, name)
        if value is not None:
            field.value = value
        return field.value

    def setUp(self):
        self.value('testNavigation', 0)
        self.lib.ApplyDeferredAction()
        for name in ('testQueued','testHandled','testScroll','testCopies','testAll',
                     'testHover','testCancelledCleared','testMenuChoice','testDispatched'):
            self.value(name, 0)

    def event(self, what=3, key=0, modifiers=0, x=0, y=0, part=3):
        self.lib.TestEvent(what,key,modifiers,x,y,part)

    def test_page_keys_and_modified_arrows_scroll_without_cancelling(self):
        for key, modifiers, direction in ((11,0,-1),(12,0,1),(30,256,-1),(31,512,1)):
            self.value('testScroll',0)
            self.event(key=key,modifiers=modifiers)
            self.assertEqual(self.lib.NetworkPoll(),0)
            self.assertEqual(self.value('testScroll'),direction)
            self.assertEqual(self.lib.UserActionPending(),0)

    def test_space_and_click_queue_article_navigation_without_nested_dispatch(self):
        self.event(key=32)
        self.assertEqual(self.lib.NetworkPoll(),1)
        self.assertEqual(self.value('testNavigation'),1)
        self.value('testNavigation',0)
        self.event(what=1,x=20,y=80)
        self.assertEqual(self.lib.NetworkPoll(),1)
        self.assertEqual(self.value('testSelected'),7)
        self.assertEqual(self.value('testHandled'),0)

    def test_menu_window_and_command_actions_dispatch_only_after_unwinding(self):
        for what, key, modifiers, part in ((1,0,0,4),(1,0,0,6),
                                          (3,ord('q'),256,3),(3,ord('m'),0,3)):
            self.event(what,key,modifiers,part=part)
            self.assertEqual(self.lib.NetworkPoll(),1)
            self.assertEqual(self.lib.DeferredActionPending(),1)
            self.assertEqual(self.value('testHandled'),0)
            self.lib.ApplyDeferredAction()
            self.assertEqual(self.value('testHandled'),what)
            self.assertEqual(self.lib.DeferredActionPending(),0)
            self.value('testHandled',0)

    def test_menu_tracks_immediately_and_dispatches_after_request_unwinds(self):
        choice=(129<<16)|6
        self.value('testMenuChoice',choice)
        self.event(what=1,part=1)
        self.assertEqual(self.lib.NetworkPoll(),1)
        self.assertEqual(self.value('testDispatched'),0)
        self.lib.ApplyDeferredAction()
        self.assertEqual(self.value('testDispatched'),choice)
        self.assertEqual(self.lib.UserActionPending(),0)

    def test_layout_yields_to_input_without_dispatching_or_consuming_repaint(self):
        self.event(what=6)
        self.assertEqual(self.lib.PollReaderAction(),0)
        self.assertEqual(self.value('testQueued'),1)
        self.event(key=31)
        self.assertEqual(self.lib.PollReaderAction(),1)
        self.assertEqual(self.value('testHandled'),0)
        self.assertEqual(self.lib.UserActionPending(),1)
        self.lib.ApplyDeferredAction()
        self.assertEqual(self.value('testHandled'),3)

    def test_copy_and_select_all_remain_available(self):
        for key, field in ((ord('c'),'testCopies'),(ord('a'),'testAll')):
            self.event(key=key,modifiers=256)
            self.assertEqual(self.lib.NetworkPoll(),0)
            self.assertEqual(self.value(field),1)
            self.assertEqual(self.lib.UserActionPending(),0)
