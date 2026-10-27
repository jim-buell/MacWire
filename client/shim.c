/*
 * shim.c — Compatibility layer for the Retro68 / Multiversal toolchain
 *
 * The original code was written against Apple's Universal Interfaces.
 * Multiversal Interfaces (shipped with Retro68) covers most of the
 * toolbox this app uses, but a handful of helpers, constants and
 * application-level handlers are missing. This file provides them so
 * the client can be compiled for the 68K Mac target and run in an
 * emulator (Mini vMac).
 */

#include "compat.h"
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>

/* --- Text alignment constants (missing from Multiversal) --- */
#ifndef cmLeft
#define cmLeft   0
#endif
#ifndef cmCenter
#define cmCenter 1
#endif
#ifndef cmRight
#define cmRight  2
#endif

/* --- Toolbox calls that are missing from Multiversal --- */

/* SetFont — select the current port's font (not a real toolbox call). */
void SetFont(FontName *font)
{
    GrafPtr port;
    GetPort(&port);
    if (port && font) {
        port->txFont = *font;
    }
}

/* Queue updates so a refresh, selection, and prefetch share one repaint. */
void InvalidateRect(Rect *r)
{
    GrafPtr oldPort;
    if (gMainWindow == NULL) return;
    GetPort(&oldPort);
    SetPort(gMainWindow);
    InvalRect(r);
    SetPort(oldPort);
    UpdateStatusLine();
}

/* --- Simple C-string helpers (used instead of StringMgr calls) --- */

void ShortToString(short n, char *str)
{
    sprintf(str, "%d", (int)n);
}

void LongToString(long n, char *str)
{
    sprintf(str, "%ld", n);
}

/* Convert a NUL-terminated C string into a Pascal (Str255) string. */
void StringToPStr(Str255 str, const char *cstr)
{
    unsigned char len = 0;
    while (cstr[len] && len < 255) {
        str[len + 1] = cstr[len];
        len++;
    }
    str[0] = len;
}

/* --- Application-level handlers referenced but not defined elsewhere --- */

extern short gSelectedIndex;
extern short gScrollOffset;
extern short gMaxArticles;
extern Rect gListRect;

extern void SelectArticle(short index);

/* HandleMenuClick — track a menu selection after a click in the menu bar.
 * `where` must be in global coordinates (see HandleMouseDown). */
void HandleMenuClick(Point where)
{
    long menuChoice;

    UpdateMenus();
    menuChoice = MenuSelect(where);
    DispatchMenuChoice(menuChoice);
}

/* HandleListClick — turn a click in the list pane into a selection. */
void HandleListClick(Point where)
{
    int row;
    if (where.h < gListRect.left || where.h >= gListRect.right ||
        where.v < gListRect.top || where.v >= gListRect.bottom) return;
    row = (where.v - gListRect.top) / GetListRowHeight() + gScrollOffset;
    if (row >= 0 && row < gMaxArticles) {
        SelectArticle(row);
    }
}

/* HandleContentClick — content pane clicks currently do nothing. */
void HandleContentClick(Point where)
{
    (void)where;
}

/* ScrollList — nudge the list scroll offset (used at the top edge). */
void ScrollList(int direction)
{
    ScrollListTo(gScrollOffset + (direction < 0 ? -1 : direction > 0 ? 1 : 0));
}

/* InitProtocol — no global protocol state to initialise. */
void InitProtocol(void)
{
}

/* --- Protocol helpers used by Articles.c caching (network is stubbed,
 *      so these only need to exist and link). --- */

void ParseArticleID(char *response, char *id)
{
    (void)response;
    id[0] = '\0';
}

char *ExtractBody(char *response)
{
    (void)response;
    return NULL;
}

void ParseImageInfo(char *response, char *id, short *imageIndex)
{
    (void)response;
    (void)id;
    if (imageIndex) *imageIndex = 0;
}

short GetImageSize(char *response)
{
    (void)response;
    return 0;
}
