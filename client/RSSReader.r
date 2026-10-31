/*
 * RSSReader.r — Resource definitions for the System 7 MacWire reader
 *
 * Compiled with Retro68's Rez. Provides the application signature and
 * name, the menu bar, the main two-pane window.
 */

#include "Multiverse.r"
#include "MenuCommands.h"

/* Keep the System 7 application partition below the SE's usable RAM. */
resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    768 * 1024,
    512 * 1024
};

/* Native resource types not shipped in Multiverse.r */
type 'SIG#' {
    longint;
};

type 'name' {
    pstring;
};

type 'CNTL' {
    rect;
    integer;
    byte invisible, visible;
    fill byte;
    integer;
    integer;
    integer;
    longint;
    pstring;
};

/* Application signature and name */
resource 'SIG#' (kFileMenu) {
    kFileMenu
};

resource 'name' (kFileMenu) {
    "MacWire"
};

/* Finder icon registration matches the application and preference creator. */
type 'RSSR' { pstring; };
resource 'RSSR' (0) { "MacWire" };
resource 'FREF' (128) { 'APPL', 0, "" };
resource 'BNDL' (128) {
    'RSSR', 0;
    {
        'ICN#', { 0, 128; };
        'FREF', { 0, 128; };
    }
};

/* Menu bar: Apple menu followed by the application menus. */
resource 'MBAR' (kAppleMenu) {
    { kAppleMenu, kFileMenu, kEditMenu, kArticleMenu, kFeedsMenu }
};

resource 'MENU' (kAppleMenu) {
    kAppleMenu, textMenuProc;
    allEnabled, enabled;
    apple;
    {
        "About MacWire", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
    }
};

/* File menu */
resource 'MENU' (kFileMenu) {
    kFileMenu, textMenuProc;
    allEnabled, enabled;
    "File";
    {
        "Show Reader", noIcon, "0", noMark, plain;
        "Close", noIcon, "W", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Preferences" $"C9", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Quit", noIcon, "Q", noMark, plain;
    }
};

/* Standard order and equivalents from Macintosh HIG (1992), chapter 4. */
resource 'MENU' (kEditMenu) {
    kEditMenu, textMenuProc;
    allEnabled, enabled;
    "Edit";
    {
        "Undo", noIcon, "Z", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Cut", noIcon, "X", noMark, plain;
        "Copy", noIcon, "C", noMark, plain;
        "Paste", noIcon, "V", noMark, plain;
        "Clear", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Select All", noIcon, "A", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Show Clipboard", noIcon, noKey, noMark, plain;
    }
};

/* Article actions */
resource 'MENU' (kArticleMenu) {
    kArticleMenu, textMenuProc;
    allEnabled, enabled;
    "Articles";
    {
        "Previous Article", noIcon, noKey, noMark, plain;
        "Next Article", noIcon, noKey, noMark, plain;
        "Next Unread", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Mark as Read", noIcon, noKey, noMark, plain;
        "Mark All as Read", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Show All Articles", noIcon, noKey, noMark, plain;
        "Show Unread Only", noIcon, noKey, noMark, plain;
        "Show Read Only", noIcon, noKey, noMark, plain;
    };
};

resource 'MENU' (kFeedsMenu) {
    kFeedsMenu, textMenuProc;
    allEnabled, enabled;
    "Feeds";
    { "Refresh Feeds", noIcon, "R", noMark, plain;
      "-", noIcon, noKey, noMark, plain;
      "All Feeds", noIcon, noKey, noMark, plain;
      "-", noIcon, noKey, noMark, plain; }
};

/* System 7 pop-up controls use CDEF 63 (proc ID 1008). */
resource 'MENU' (200) {
    200, textMenuProc;
    allEnabled, enabled;
    "";
    { }
};

/* Independent size menus keep font availability specific to each row. */
#define SIZE_MENU(menuID) \
resource 'MENU' (menuID) { \
    menuID, textMenuProc; \
    allEnabled, enabled; \
    ""; \
    { \
        "6 pt", noIcon, noKey, noMark, plain; \
        "7 pt", noIcon, noKey, noMark, plain; \
        "8 pt", noIcon, noKey, noMark, plain; \
        "9 pt", noIcon, noKey, noMark, plain; \
        "10 pt", noIcon, noKey, noMark, plain; \
        "11 pt", noIcon, noKey, noMark, plain; \
        "12 pt", noIcon, noKey, noMark, plain; \
        "13 pt", noIcon, noKey, noMark, plain; \
        "14 pt", noIcon, noKey, noMark, plain; \
        "16 pt", noIcon, noKey, noMark, plain; \
        "18 pt", noIcon, noKey, noMark, plain; \
        "20 pt", noIcon, noKey, noMark, plain; \
        "24 pt", noIcon, noKey, noMark, plain; \
        "28 pt", noIcon, noKey, noMark, plain; \
        "32 pt", noIcon, noKey, noMark, plain; \
        "36 pt", noIcon, noKey, noMark, plain; \
        "48 pt", noIcon, noKey, noMark, plain; \
        "72 pt", noIcon, noKey, noMark, plain; \
    } \
};

SIZE_MENU(kTypographyFirstSizeMenu + 0)
SIZE_MENU(kTypographyFirstSizeMenu + 1)
SIZE_MENU(kTypographyFirstSizeMenu + 2)
SIZE_MENU(kTypographyFirstSizeMenu + 3)
SIZE_MENU(kTypographyFirstSizeMenu + 4)
#undef SIZE_MENU

resource 'MENU' (202) {
    202, textMenuProc;
    allEnabled, enabled;
    "";
    {
        "Plain", noIcon, noKey, noMark, plain;
        "Bold", noIcon, noKey, noMark, plain;
        "Italic", noIcon, noKey, noMark, plain;
        "Bold Italic", noIcon, noKey, noMark, plain;
    }
};

resource 'DLOG' (136) {
    { 25, 25, 321, 487 }, dBoxProc;
    invisible, noGoAway;
    0, 136, "Preferences";
    centerMainScreen
};

resource 'DITL' (136) {
    {
        { 261, 375, 282, 446 }, Button { enabled, "OK" };
        { 261, 293, 282, 364 }, Button { enabled, "Cancel" };
        { 44, 116, 62, 238 }, StaticText { disabled, "Font" };
        { 44, 248, 62, 326 }, StaticText { disabled, "Size" };
        { 44, 336, 62, 446 }, StaticText { disabled, "Style" };

        { 76, 16, 95, 110 }, StaticText { disabled, "List title" };
        { 73, 116, 94, 238 }, Control { enabled, 210 };
        { 73, 248, 94, 326 }, Control { enabled, 211 };
        { 73, 336, 94, 446 }, Control { enabled, 212 };

        { 112, 16, 131, 110 }, StaticText { disabled, "Reader title" };
        { 109, 116, 130, 238 }, Control { enabled, 213 };
        { 109, 248, 130, 326 }, Control { enabled, 214 };
        { 109, 336, 130, 446 }, Control { enabled, 215 };

        { 148, 16, 167, 110 }, StaticText { disabled, "Body" };
        { 145, 116, 166, 238 }, Control { enabled, 216 };
        { 145, 248, 166, 326 }, Control { enabled, 217 };
        { 145, 336, 166, 446 }, Control { enabled, 218 };

        { 184, 16, 203, 110 }, StaticText { disabled, "Metadata" };
        { 181, 116, 202, 238 }, Control { enabled, 219 };
        { 181, 248, 202, 326 }, Control { enabled, 220 };
        { 181, 336, 202, 446 }, Control { enabled, 221 };

        { 220, 16, 239, 110 }, StaticText { disabled, "Feed name" };
        { 217, 116, 238, 238 }, Control { enabled, 222 };
        { 217, 248, 238, 326 }, Control { enabled, 223 };
        { 217, 336, 238, 446 }, Control { enabled, 224 };
        { 257, 371, 286, 450 }, UserItem { enabled };
        { 14, 16, 32, 110 }, StaticText { disabled, "Show:" };
        { 11, 160, 32, 354 }, Control { enabled, 225 };
        { 46, 16, 64, 144 }, StaticText { disabled, "Server address:" };
        { 43, 163, 61, 443 }, EditText { enabled, "" };
        { 82, 16, 100, 144 }, StaticText { disabled, "Port:" };
        { 79, 163, 97, 241 }, EditText { enabled, "8080" };
        { 76, 274, 97, 446 }, Button { enabled, "Test Connection" };
        { 201, 16, 229, 446 }, StaticText { disabled, "" };
        { 0, 0, 0, 0 }, StaticText { disabled, "" };
        { 264, 16, 282, 116 }, StaticText { disabled, "List title lines:" };
        { 261, 116, 282, 194 }, Control { enabled, 226 };
        { 124, 16, 142, 144 }, StaticText { disabled, "Show articles from:" };
        { 121, 160, 142, 354 }, Control { enabled, 227 };
        { 153, 160, 193, 446 }, StaticText { disabled, "Only fetches articles published\rduring this time period." };

    }
};

resource 'CNTL' (210) {
    { 73, 116, 94, 238 },
    0, visible, 0, 200, 1008, 0, ""
};

resource 'CNTL' (211) {
    { 73, 248, 94, 326 },
    0, visible, 0, kTypographyFirstSizeMenu + 0, 1008, 0, ""
};

resource 'CNTL' (212) {
    { 73, 336, 94, 446 },
    0, visible, 0, 202, 1008, 0, ""
};

resource 'CNTL' (213) {
    { 109, 116, 130, 238 },
    0, visible, 0, 200, 1008, 0, ""
};

resource 'CNTL' (214) {
    { 109, 248, 130, 326 },
    0, visible, 0, kTypographyFirstSizeMenu + 1, 1008, 0, ""
};

resource 'CNTL' (215) {
    { 109, 336, 130, 446 },
    0, visible, 0, 202, 1008, 0, ""
};

resource 'CNTL' (216) {
    { 145, 116, 166, 238 },
    0, visible, 0, 200, 1008, 0, ""
};

resource 'CNTL' (217) {
    { 145, 248, 166, 326 },
    0, visible, 0, kTypographyFirstSizeMenu + 2, 1008, 0, ""
};

resource 'CNTL' (218) {
    { 145, 336, 166, 446 },
    0, visible, 0, 202, 1008, 0, ""
};

resource 'CNTL' (219) {
    { 181, 116, 202, 238 },
    0, visible, 0, 200, 1008, 0, ""
};

resource 'CNTL' (220) {
    { 181, 248, 202, 326 },
    0, visible, 0, kTypographyFirstSizeMenu + 3, 1008, 0, ""
};

resource 'CNTL' (221) {
    { 181, 336, 202, 446 },
    0, visible, 0, 202, 1008, 0, ""
};

resource 'CNTL' (222) {
    { 217, 116, 238, 238 },
    0, visible, 0, 200, 1008, 0, ""
};

resource 'CNTL' (223) {
    { 217, 248, 238, 326 },
    0, visible, 0, kTypographyFirstSizeMenu + 4, 1008, 0, ""
};

resource 'CNTL' (224) {
    { 217, 336, 238, 446 },
    0, visible, 0, 202, 1008, 0, ""
};

resource 'MENU' (203) {
    203, textMenuProc; allEnabled, enabled; "";
    {
        "Typography and Display", noIcon, noKey, noMark, plain;
        "Connection", noIcon, noKey, noMark, plain;
    }
};
resource 'CNTL' (225) {
    { 11, 160, 32, 354 }, 1, visible, 0, 203, 1009, 0, ""
};

resource 'MENU' (204) {
    204, textMenuProc; allEnabled, enabled; "";
    {
        "1", noIcon, noKey, noMark, plain;
        "2", noIcon, noKey, noMark, plain;
        "3", noIcon, noKey, noMark, plain;
        "4", noIcon, noKey, noMark, plain;
    }
};
resource 'CNTL' (226) {
    { 261, 116, 282, 194 }, 3, visible, 0, 204, 1008, 0, ""
};

resource 'DLOG' (135) {
    { 90, 80, 236, 432 }, dBoxProc;
    invisible, noGoAway;
    0, 135, "About MacWire";
    centerMainScreen
};

resource 'DITL' (135) {
    {
        { 112, 264, 132, 336 }, Button { enabled, "OK" };
        { 16, 64, 34, 336 }, StaticText { disabled, "MacWire" };
        { 84, 64, 102, 336 }, StaticText { disabled, "Version 1.0.2" };
        { 20, 18, 52, 50 }, Icon { disabled, 128 };
        { 108, 260, 136, 340 }, UserItem { enabled };
        { 36, 64, 54, 336 }, StaticText { disabled, "by Jim Buell" };
    }
};

/* Main two-pane window (list on the left, content on the right) */
resource 'WIND' (kFileMenu, "MacWire") {
    {41, 6, 329, 504}, documentProc;
    visible, goAway;
    0, "MacWire";
    noAutoCenter;
};

resource 'MENU' (205) {
    205, textMenuProc; allEnabled, enabled; "";
    {
        "Past 24 hours", noIcon, noKey, noMark, plain;
        "Past 48 hours", noIcon, noKey, noMark, plain;
        "Past 3 days", noIcon, noKey, noMark, plain;
        "Past 7 days", noIcon, noKey, noMark, plain;
    }
};
resource 'CNTL' (227) {
    { 121, 160, 142, 354 }, 1, visible, 0, 205, 1009, 0, ""
};
