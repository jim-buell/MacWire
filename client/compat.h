/*
 * compat.h — Compatibility bridge for the Retro68 / Multiversal toolchain.
 *
 * The RSS Reader client was written against Apple's Universal Interfaces.
 * Retro68 ships Multiversal Interfaces, which differs in a few type names,
 * struct member names, and a handful of missing helper macros. This header
 * normalizes those differences so the existing client code compiles against
 * the 68K Mac target.
 */

#ifndef RSS_COMPAT_H
#define RSS_COMPAT_H

#include <MacTypes.h>
#include <QuickDraw.h>
#include <Windows.h>
#include <Events.h>
#include <Menus.h>
#include <Fonts.h>
#include <TextEdit.h>
#include <Memory.h>
#include <Strings.h>
#include <Resources.h>
#include <Dialogs.h>

#include <stdio.h>
#include <string.h>

/* --- Type names that differ between Universal and Multiversal Interfaces --- */

#ifndef Str256
typedef unsigned char Str256[256];
#endif

#ifndef TextHandle
#define TextHandle TEHandle
#endif

#ifndef FontName
typedef short FontName;
#endif

/* --- QuickDraw font-style macros (not exported by Multiversal) ---
 *
 * Multiversal stores the font style in the GrafPort.txFace field rather
 * than the classic txStyle. */
#ifndef Italic
#define Italic  (qd.thePort->txFace |= italic)
#endif
#ifndef Bold
#define Bold    (qd.thePort->txFace |= bold)
#endif
#ifndef Underline
#define Underline (qd.thePort->txFace |= underline)
#endif
#ifndef Normal
#define Normal  (qd.thePort->txFace = 0)
#endif
#ifndef NoClip
#define NoClip  ClipRect(&qd.thePort->portRect)
#endif

/* --- Window type alias --- */
#ifndef documentWindow
#define documentWindow documentProc
#endif

/* --- Text-alignment constants for DrawInRect (not exported by Multiversal) --- */
#ifndef cmLeft
#define cmLeft   0
#endif
#ifndef cmCenter
#define cmCenter 1
#endif
#ifndef cmRight
#define cmRight  2
#endif

/* --- min/max helpers (not provided by Multiversal) --- */
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

/* --- Font name aliases (guarded by OLDROUTINENAMES in Multiverse) --- */
#ifndef monaco
#define monaco kFontIDMonaco
#endif
#ifndef geneva
#define geneva kFontIDGeneva
#endif
#ifndef newYork
#define newYork kFontIDNewYork
#endif

/* --- Shared layout constant --- */
#ifndef kTitleHeight
#define kTitleHeight 18
#endif

#define kListRowHeight 32

/* --- Toolbox traps inlined at each call site ---
 *
 * Declared here (a header) rather than defined in one .c file so that GCC
 * emits the .short selector at every call site. This is how Mini vMac's
 * toolbox emulation is reached (see Multiverse.h). */
pascal void DrawInRect(Str255 str, Rect *r, short align) M68K_INLINE(0xA8A4);
pascal void EnableMenuCmd(short item) M68K_INLINE(0xA155);
pascal void TransferBitMap(BitMap *src, Rect *srcRect, short op, Pattern *pat, Rect *dstRect, short mode) M68K_INLINE(0xA824);

/* --- Cross-file function declarations ---
 *
 * These helpers are defined in one translation unit and referenced from
 * others. Declaring them here keeps the individual .c files unchanged. */

/* shim.c */
extern void ShortToString(short n, char *str);
extern void LongToString(long n, char *str);
extern void StringToPStr(Str255 str, const char *cstr);
extern void DrawInRect(Str255 str, Rect *r, short align);
extern void InvalidateRect(Rect *r);
extern void HandleMenuClick(Point where);
extern void HandleMenuCommand(short menuID, short item);
extern void UpdateMenus(void);
extern void DispatchMenuChoice(long choice);
extern Boolean gInBackground;
extern void HandleListClick(Point where);
extern Boolean HandleListHeaderClick(Point where);
extern void HandleContentClick(Point where);
extern void ScrollList(int direction);
extern void InitProtocol(void);
extern void ParseArticleID(char *response, char *id);
extern char *ExtractBody(char *response);
extern void ParseImageInfo(char *response, char *id, short *imageIndex);
extern short GetImageSize(char *response);
extern void TransferBitMap(BitMap *src, Rect *srcRect, short op, Pattern *pat, Rect *dstRect, short mode);

/* Articles.c */
extern void SelectArticle(short index);
extern void SelectPreviousArticle(void);
extern void SelectNextArticle(void);
extern void ScrollArticle(short direction);
extern void AdvanceToNextArticle(void);
extern void OpenSelectedArticle(void);
extern void ToggleMarkReadSelected(void);
extern void PrefetchArticles(short fromIndex, short count);
extern short FindArticleByID(char *id);
extern char *GetCachedArticleBody(short index);
extern Handle GetCachedImage(short articleIndex, short imageIndex);
extern void GetStatusMessage(Str256 str);

/* UI.c */
extern void DrawListPane(void);
extern void DrawContentPane(void);
extern void DrawArticleImages(short articleIndex, Rect *clipRect);
extern void InitScrollBars(void);
extern void LayoutUI(void);
extern void ZoomReaderWindow(short part);
extern void ScrollReaderHorizontallyTo(short offset);
extern void ScrollListTo(short offset);
extern void UpdateArticleList(void);
void FinishReaderArticleLoading(void);
void UpdateArticleRow(short index);
void UpdateArticleReadIndicator(short index);
extern void UpdateContentPane(void);
extern void DrawStatusLine(void);
extern void UpdateStatusLine(void);
extern short GetVisibleListRows(void);
extern short GetListRowHeight(void);
extern short GetContentViewportHeight(void);
short GetReaderImageWidth(void);
extern void UpdateScrollBars(void);
extern Boolean HandleScrollBarClick(Point where);
extern void ClearReaderSelection(void);
extern void UpdateReaderLinkHover(void);
extern Boolean ReaderCanCopy(void);
extern Boolean ReaderHasText(void);
extern void SelectAllReaderText(void);
extern void CopyReaderText(void);
extern void HandleReaderTextClick(Point where, Boolean extend);

/* Clipboard.c: a read-only view of the system scrap, never a private scrap. */
extern WindowPtr gClipboardWindow;
extern Boolean ClipboardIsOpen(void);
extern void ToggleClipboard(void);
extern void CloseClipboard(void);
extern void SuspendClipboard(void);
extern void ResumeClipboard(void);
extern void UpdateClipboard(void);
extern void DrawClipboard(void);
extern void HandleClipboardClick(Point where, Boolean extend);
extern void ScrollClipboard(short direction);
extern Boolean ClipboardCanCopy(void);
extern Boolean ClipboardHasText(void);
extern void CopyClipboardText(void);
extern void SelectAllClipboardText(void);
extern void DisposeClipboard(void);
extern void PrepareStandardDialog(DialogPtr dialog, short outlineItem, short cancelItem);
extern pascal Boolean StandardDialogFilter(DialogPtr dialog, EventRecord *event, short *item);
#ifdef TICKERTAPE_UI_SMOKE
extern void UISmokeSnapshot(const char *name);
extern void UISmokePostDialogKey(void);
#endif

/* Network.c */
extern void InitNetwork(void);
extern void SyncWithServer(void);
void SyncStartupSamples(void);
extern void FetchArticle(char *articleID);
extern void FetchImage(char *articleID, short index);

/* Protocol.c */
extern short ParseIndexResponse(const unsigned char *response, unsigned long length);
extern short ParseStatusResponse(const unsigned char *response, unsigned long length,
                                 short *articleCount);
extern void TimestampToString(double timestamp, char *str);

/* External global state shared across files */
extern WindowPtr gMainWindow;
extern Rect gListRect, gContentRect;
extern short gSelectedIndex;
extern short gScrollOffset;
extern long gContentScroll;
extern long gContentHeight;
extern void InvalidateReaderLayout(void);
extern void PreviewArticleSelection(short index);
extern short NextArticleImage(short index);
extern long ReaderImageUpdateTop(short imageIndex);
extern void ReaderImageLoaded(long oldTop);
extern void ScrollReaderTo(long offset);
extern short gMaxArticles;

#endif /* RSS_COMPAT_H */
