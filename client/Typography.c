/* Independent list and reader typography, stored in System 7 Preferences. */
#include "typography.h"
#include <Dialogs.h>
#include <Fonts.h>
#include "Connection.h"
#include "MenuCommands.h"

#define kTypographyDialog 136
#define kTypographyVersion 3
#define kSystemDisk (-32768)

typedef struct {
    short version;
    TypographySetting settings[kTypeCount];
    short listTitleLines;
} TypographyFile;

short gListTitleLines = 3;

TypographySetting gTypography[kTypeCount] = {
    { systemFont, 18, bold, 0 }, /* Chicago */
    { newYork, 12, 0, 0 },
    { monaco, 9, italic, 0 },
    { monaco, 9, italic, 0 },
    { systemFont, 12, 0, 0 }    /* Chicago */
};

static const short dialogRoles[] = {
    kTypeListTitle, kTypeReaderTitle, kTypeBody, kTypeMetadata, kTypeFeed
};

static const short sizes[] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72 };
#define kSizeCount (sizeof(sizes) / sizeof(sizes[0]))
static const unsigned char faces[] = { 0, bold, italic, bold | italic };
static const unsigned char prefsName[] = "\014MacWire Type";

static MenuHandle gFontMenu;

static void BuildFontMenu(void)
{
    Str255 systemName, itemName;
    short i;
    Boolean found = false;
    if (gFontMenu != NULL) { DeleteMenu(200); DisposeMenu(gFontMenu); }
    gFontMenu = GetMenu(200);
    if (gFontMenu == NULL) return;
    /* The Menu Manager includes System-file fonts and System 7.1 suitcases. */
    AppendResMenu(gFontMenu, 'FOND');
    /* Some System files leave the built-in font resource unnamed. */
    GetFontName(0, systemName);
    for (i = 1; i <= CountMItems(gFontMenu); ++i) {
        GetMenuItemText(gFontMenu, i, itemName);
        if (EqualString(systemName, itemName, false, true)) found = true;
    }
    if (!found && systemName[0]) {
        InsertMenuItem(gFontMenu, (ConstStringPtr)"\001 ", 0);
        SetMenuItemText(gFontMenu, 1, systemName);
    }
    InsertMenu(gFontMenu, -1);
}

static short FontIndex(short font)
{
    Str255 wanted, name;
    short i;
    if (gFontMenu == NULL) return 1;
    GetFontName(font, wanted);
    for (i = 1; i <= CountMItems(gFontMenu); ++i) {
        GetMenuItemText(gFontMenu, i, name);
        if (EqualString(wanted, name, false, true)) return i;
    }
    return 1;
}

static short FontAtIndex(short index)
{
    Str255 name;
    short font = geneva;
    if (gFontMenu != NULL && index >= 1 && index <= CountMItems(gFontMenu)) {
        GetMenuItemText(gFontMenu, index, name);
        GetFNum(name, &font);
    }
    return font;
}

static short SizeIndex(short size)
{
    short i;
    for (i = 0; i < kSizeCount; ++i) if (sizes[i] == size) return i;
    return 6;
}

static short FaceIndex(unsigned char face)
{
    short i;
    for (i = 0; i < 4; ++i) if (faces[i] == face) return i;
    return 0;
}

static Boolean PreferenceSpec(FSSpec *spec)
{
    short volume;
    long folder;
    OSErr error;
    if (FindFolder(kSystemDisk, kPreferencesFolderType, true,
                   &volume, &folder) != noErr) return false;
    error = FSMakeFSSpec(volume, folder, prefsName, spec);
    return error == noErr || error == fnfErr;
}

static Boolean ValidSetting(TypographySetting *setting)
{
    short i;
    Boolean sizeFound = false, faceFound = false;
    Str255 fontName;
    GetFontName(setting->font, fontName);
    for (i = 0; i < 4; ++i) {
        if (setting->face == faces[i]) faceFound = true;
    }
    for (i = 0; i < kSizeCount; ++i)
        if (setting->size == sizes[i]) sizeFound = true;
    return fontName[0] != 0 && sizeFound && faceFound;
}

void InitTypography(void)
{
    FSSpec spec;
    short refNum;
    long count;
    TypographyFile file;
    short i;
    if (!PreferenceSpec(&spec)) return;
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) return;
    count = sizeof(file);
    {
        OSErr error = FSRead(refNum, &count, &file);
        short settingCount = 0;
        if (error == noErr && count == sizeof(file) &&
            file.version == kTypographyVersion) {
            settingCount = kTypeCount;
            if (file.listTitleLines >= 1 && file.listTitleLines <= 4)
                gListTitleLines = file.listTitleLines;
        }
        /* Version 2 has independent fonts, but no title line preference. */
        else if ((error == noErr || error == eofErr) &&
                 count == sizeof(short) + kTypeCount * sizeof(TypographySetting) &&
                 file.version == 2) settingCount = kTypeCount;
        /* Version 1 used one headline setting for both panes. Keep its
           reader setting and give the list its own compact default. */
        else if ((error == noErr || error == eofErr) &&
                 count == sizeof(short) + 4 * sizeof(TypographySetting) &&
                 file.version == 1) settingCount = 4;
        for (i = 0; i < settingCount; ++i) {
            if (ValidSetting(&file.settings[i]))
                gTypography[i] = file.settings[i];
        }
    }
    FSClose(refNum);
}

static void SaveTypography(void)
{
    FSSpec spec;
    short refNum;
    long count;
    TypographyFile file;
    short i;
    OSErr error;
    if (!PreferenceSpec(&spec)) return;
    error = FSpOpenDF(&spec, fsRdWrPerm, &refNum);
    if (error == fnfErr) {
        if (FSpCreate(&spec, 'RSSR', 'pref', 0) != noErr) return;
        error = FSpOpenDF(&spec, fsRdWrPerm, &refNum);
    }
    if (error != noErr) return;
    file.version = kTypographyVersion;
    file.listTitleLines = gListTitleLines;
    for (i = 0; i < kTypeCount; ++i) file.settings[i] = gTypography[i];
    count = sizeof(file);
    SetEOF(refNum, 0);
    FSWrite(refNum, &count, &file);
    FSClose(refNum);
}

void ApplyTypography(short role)
{
    if (role < 0 || role >= kTypeCount) return;
    qd.thePort->txFont = gTypography[role].font;
    qd.thePort->txSize = gTypography[role].size;
    qd.thePort->txFace = gTypography[role].face;
}

short GetBodyLineHeight(void)
{
    return GetTypographyLineHeight(kTypeBody);
}

void GetTypographyFontInfo(short role, FontInfo *info)
{
    static TypographySetting keys[kTypeCount];
    static FontInfo metrics[kTypeCount];
    static Boolean valid[kTypeCount];
    short font = qd.thePort->txFont, size = qd.thePort->txSize;
    Style face = qd.thePort->txFace;
    if (role < 0 || role >= kTypeCount) return;
    if (valid[role] && !memcmp(&keys[role], &gTypography[role], sizeof(TypographySetting))) {
        *info = metrics[role]; return;
    }
    ApplyTypography(role); GetFontInfo(info);
    keys[role] = gTypography[role]; metrics[role] = *info; valid[role] = true;
    TextFont(font); TextSize(size); TextFace(face);
}

short GetTypographyLineHeight(short role)
{
    FontInfo info;
    GetTypographyFontInfo(role, &info);
    return max(1, info.ascent + info.descent + max(0, info.leading) + 1);
}

static ControlHandle PopupControl(DialogPtr dialog, short itemNumber)
{
    short itemType;
    Handle item;
    Rect itemRect;
    GetDialogItem(dialog, itemNumber, &itemType, &item, &itemRect);
    return (ControlHandle)item;
}

static void UpdateSizeMenu(DialogPtr dialog, short row)
{
    MenuHandle menu = GetMenuHandle(kTypographyFirstSizeMenu + row);
    short i, font;
    ControlHandle control = PopupControl(dialog, 7 + row * 4);
    if (menu == NULL || control == NULL) return;
    font = FontAtIndex(GetControlValue(control));
    /* RealFont reports installed bitmap strikes and renderable TrueType sizes.
       Each role has its own menu so changing a font cannot alter another row. */
    for (i = 0; i < kSizeCount; ++i)
        SetItemStyle(menu, i + 1, RealFont(font, sizes[i]) ? outline : 0);
    CalcMenuSize(menu);
}

static void DialogText(DialogPtr dialog, short number, const char *text)
{
    short type; Handle item; Rect rect; Str255 label;
    GetDialogItem(dialog, number, &type, &item, &rect);
    StringToPStr(label, text);
    SetDialogItemText(item, label);
}

static Boolean ReadDialogText(DialogPtr dialog, short number, char *text, short capacity)
{
    short type; Handle item; Rect rect; Str255 label;
    GetDialogItem(dialog, number, &type, &item, &rect);
    GetDialogItemText(item, label);
    if (label[0] >= capacity) { text[0] = 0; return false; }
    BlockMove(label + 1, text, label[0]);
    text[label[0]] = 0;
    return true;
}

static const unsigned short windowHours[] = {24, 48, 72, 168};
/* Native static text is left justified. Place these labels by their measured
   Chicago width plus TextEdit padding so the final word/colon cannot wrap. */
static void AlignConnectionLabels(DialogPtr dialog)
{
    static const short labels[] = {27, 29, 31, 38};
    short i, type;
    Handle item;
    Rect rect;
    Str255 text;
    SetPort(dialog); TextFont(systemFont); TextSize(12); TextFace(0);
    for (i = 0; i < 4; ++i) {
        GetDialogItem(dialog, labels[i], &type, &item, &rect);
        GetDialogItemText(item, text);
        rect.right = 144;
        rect.left = rect.right - StringWidth(text) - 4;
        SetDialogItem(dialog, labels[i], type, item, &rect);
    }
}
static short gPreferencesPane;
static Ptr gPanePixels[2];
static Boolean gPanePainted[2];
#ifdef TICKERTAPE_UI_SMOKE
static unsigned long gPreferencesRenderCount;
#endif

static void PaintPreferences(DialogPtr dialog)
{
    BitMap screen, buffer;
    RgnHandle savedClip = NewRgn(), savedVisible = NewRgn();
    Ptr pixels;
    short pane = gPreferencesPane == 2 ? 1 : 0;
    SetPort(dialog);
    buffer.bounds = dialog->portRect;
    buffer.rowBytes = ((buffer.bounds.right - buffer.bounds.left + 15) / 16) * 2;
    if (!gPanePixels[pane])
        gPanePixels[pane] = NewPtrClear((long)buffer.rowBytes *
                                       (buffer.bounds.bottom - buffer.bounds.top));
    pixels = gPanePixels[pane];
    if (savedClip == NULL || savedVisible == NULL || pixels == NULL) {
        if (savedClip) DisposeRgn(savedClip);
        if (savedVisible) DisposeRgn(savedVisible);
        DrawDialog(dialog);
        return;
    }
    buffer.baseAddr = pixels;
    if (gPanePainted[pane]) {
        CopyBits(&buffer, &dialog->portBits, &buffer.bounds, &dialog->portRect,
                 srcCopy, NULL);
        ValidRect(&dialog->portRect);
        DisposeRgn(savedClip); DisposeRgn(savedVisible);
        return;
    }
    screen = dialog->portBits;
    GetClip(savedClip);
    CopyRgn(dialog->visRgn, savedVisible);
    /* Controls draw through their owner port. Redirect that port only while
       painting; its position, control values, and screen regions stay intact. */
    SetPortBits(&buffer);
    RectRgn(dialog->visRgn, &dialog->portRect);
    ClipRect(&dialog->portRect);
    EraseRect(&dialog->portRect);
    DrawDialog(dialog);
#ifdef TICKERTAPE_UI_SMOKE
    ++gPreferencesRenderCount;
#endif
    gPanePainted[pane] = true;
    SetPortBits(&screen);
    CopyRgn(savedVisible, dialog->visRgn);
    SetClip(savedClip);
    CopyBits(&buffer, &dialog->portBits, &buffer.bounds, &dialog->portRect,
             srcCopy, NULL);
    ValidRect(&dialog->portRect);
    DisposeRgn(savedClip); DisposeRgn(savedVisible);
}

static void ShowPane(DialogPtr dialog, short pane)
{
    short i;
    Rect empty = {0, 0, 0, 0};
    Rect body, footer;
    RgnHandle savedClip;
    CursHandle busy;
    MenuHandle edit = GetMenuHandle(kEditMenu);
    if (pane == gPreferencesPane) return;
    SetPort(dialog);
    SetControlValue(PopupControl(dialog, 28), pane);
    if (((WindowPeek)dialog)->visible) {
        /* Show the newly selected pane immediately, before control setup. */
        SetRect(&body, 16, 39, 446, 253);
        SetRect(&footer, 16, 257, 194, 286);
        EraseRect(&body); EraseRect(&footer);
        Draw1Control(PopupControl(dialog, 28));
        busy = GetCursor(watchCursor);
        if (busy) SetCursor(*busy);
    }
    savedClip = NewRgn();
    /* Hide/show operations can erase or draw immediately. Suppress those
       intermediate pixels, then present the complete new pane once. */
    if (savedClip) { GetClip(savedClip); ClipRect(&empty); }
    if (pane == 2) {
        EnableItem(edit, 0);
        EnableItem(edit, kEditCut); EnableItem(edit, kEditCopy);
        EnableItem(edit, kEditPaste); EnableItem(edit, kEditClear);
        EnableItem(edit, kEditSelectAll);
        DisableItem(edit, kEditUndo); DisableItem(edit, kEditClipboard);
    } else DisableItem(edit, 0);
    DrawMenuBar();
    for (i = 3; i <= 25; ++i) {
        if (pane == 1) ShowDialogItem(dialog, i); else HideDialogItem(dialog, i);
    }
    for (i = 29; i <= 35; ++i) {
        if (pane == 2) ShowDialogItem(dialog, i); else HideDialogItem(dialog, i);
    }
    for (i = 36; i <= 37; ++i) {
        if (pane == 1) ShowDialogItem(dialog, i); else HideDialogItem(dialog, i);
    }
    for (i = 38; i <= 40; ++i) {
        if (pane == 2) ShowDialogItem(dialog, i); else HideDialogItem(dialog, i);
    }
    gPreferencesPane = pane;
    if (pane == 2) SelectDialogItemText(dialog, 30, 0, 32767);
    SetPort(dialog);
    if (savedClip) { SetClip(savedClip); DisposeRgn(savedClip); }
    if (((WindowPeek)dialog)->visible) {
        PaintPreferences(dialog);
        InitCursor();
    }
    else InvalRect(&dialog->portRect);
}

static Boolean ReadConnection(DialogPtr dialog, ConnectionSettings *settings, Boolean testing)
{
    char port[8]; uint32_t address;
    *settings = gConnection;
    Boolean fits = ReadDialogText(dialog, 30, settings->address, sizeof(settings->address));
    ReadDialogText(dialog, 32, port, sizeof(port));
#ifndef RSS_DEMO_TRANSPORT
    settings->sample = 0;
#endif
    short age = GetControlValue(PopupControl(dialog, 39));
    settings->windowHours = windowHours[max(1, min(age, 4)) - 1];
    if (!fits || ((testing || settings->address[0]) &&
        !ConnectionAddress(settings->address, &address))) {
        ShowPane(dialog, 2);
        DialogText(dialog, 34, "Enter an IPv4 address, for example 192.168.1.10.");
        return false;
    }
    if (!ConnectionPort(port, &settings->port)) {
        ShowPane(dialog, 2);
        DialogText(dialog, 34, "Enter a port number from 1 to 65535.");
        SelectDialogItemText(dialog, 32, 0, 32767);
        return false;
    }
    return true;
}

/* Retro68 supplies TextEdit traps, but not the DialogCut/Copy/Paste glue.
   Use the dialog's native edit record and the shared system TEXT scrap. */
static void PreferencesEdit(DialogPtr dialog, short command)
{
    TEHandle text = ((DialogPeek)dialog)->textH;
    Handle data;
    long length, offset;
    short start = (*text)->selStart, end = (*text)->selEnd;
    SetPort(dialog);
    if (command == kEditCopy || command == kEditCut) {
        if (end <= start) return;
        data = (Handle)TEGetText(text);
        HLock(data);
        if (ZeroScrap() == noErr && PutScrap(end - start, 'TEXT', *data + start) == noErr) {
            HUnlock(data);
            if (command == kEditCut) TEDelete(text);
        } else HUnlock(data);
    } else if (command == kEditClear) TEDelete(text);
    else if (command == kEditPaste) {
        data = NewHandle(0);
        if (data == NULL) return;
        length = GetScrap(data, 'TEXT', &offset);
        /* Dialog Manager edit items use Pascal strings (255 characters). */
        if (length > 0 && length <= 255 - ((*text)->teLength - (end - start))) {
            HLock(data); TEInsert(*data, length, text); HUnlock(data);
        } else if (length > 0) SysBeep(1);
        DisposeHandle(data);
    }
}

static pascal Boolean PreferencesFilter(DialogPtr dialog, EventRecord *event, short *item)
{
    short key;
    WindowPtr window;
    if (event->what == keyDown || event->what == autoKey)
        gPanePainted[gPreferencesPane == 2 ? 1 : 0] = false;
    if (event->what == mouseDown &&
        FindWindow(event->where, &window) == inContent && window == dialog) {
        short type; Handle control; Rect selector;
        Point where = event->where;
        SetPort(dialog); GlobalToLocal(&where);
        GetDialogItem(dialog, 28, &type, &control, &selector);
        if (!PtInRect(where, &selector))
            gPanePainted[gPreferencesPane == 2 ? 1 : 0] = false;
    }
    if (event->what == updateEvt && (WindowPtr)event->message == dialog) {
        SetPort(dialog);
        BeginUpdate(dialog);
        PaintPreferences(dialog);
        EndUpdate(dialog);
        return true;
    }
    if (event->what == mouseDown &&
        FindWindow(event->where, &window) == inMenuBar) {
        long command = MenuSelect(event->where);
        if ((command >> 16) == kEditMenu) {
            gPanePainted[gPreferencesPane == 2 ? 1 : 0] = false;
            switch ((short)command) {
            case kEditCut: PreferencesEdit(dialog, kEditCut); break;
            case kEditCopy: PreferencesEdit(dialog, kEditCopy); break;
            case kEditPaste: PreferencesEdit(dialog, kEditPaste); break;
            case kEditClear: PreferencesEdit(dialog, kEditClear); break;
            case kEditSelectAll: TESetSelect(0, 32767, ((DialogPeek)dialog)->textH); break;
            }
        }
        HiliteMenu(0);
        return true;
    }
    if (event->what == keyDown && (event->modifiers & cmdKey) &&
        GetControlValue(PopupControl(dialog, 28)) == 2) {
        key = event->message & 255;
        if (key == 'x' || key == 'X') { PreferencesEdit(dialog, kEditCut); return true; }
        if (key == 'c' || key == 'C') { PreferencesEdit(dialog, kEditCopy); return true; }
        if (key == 'v' || key == 'V') { PreferencesEdit(dialog, kEditPaste); return true; }
        if (key == 'a' || key == 'A') {
            TESetSelect(0, 32767, ((DialogPeek)dialog)->textH); return true;
        }
    }
    return StandardDialogFilter(dialog, event, item);
}

void ShowTypographyDialog(void)
{
    DialogPtr dialog;
    short item, row, role, base;
    ControlHandle fontControl, sizeControl, faceControl;
    ConnectionSettings connection, previousConnection;
    short articles;
    char text[128];
    extern DialogPtr gConnectionDialog;
    extern Boolean gConnectionCloseRequested;
    CursHandle busy = GetCursor(watchCursor);
    if (busy) SetCursor(*busy);
    gPreferencesPane = 0;
    gPanePainted[0] = gPanePainted[1] = false;
    BuildFontMenu();
    dialog = GetNewDialog(kTypographyDialog, NULL, (WindowPtr)-1);
    if (dialog == NULL) { InitCursor(); return; }
    SetPort(dialog);
    PrepareStandardDialog(dialog, 26, 2);
    AlignConnectionLabels(dialog);
    for (row = 0; row < kTypeCount; ++row) {
        role = dialogRoles[row];
        base = 7 + row * 4;
        fontControl = PopupControl(dialog, base);
        sizeControl = PopupControl(dialog, base + 1);
        faceControl = PopupControl(dialog, base + 2);
        if (fontControl != NULL)
            SetControlValue(fontControl, FontIndex(gTypography[role].font));
        if (sizeControl != NULL)
            SetControlValue(sizeControl, SizeIndex(gTypography[role].size) + 1);
        if (faceControl != NULL)
            SetControlValue(faceControl, FaceIndex(gTypography[role].face) + 1);
        UpdateSizeMenu(dialog, row);
    }
    DialogText(dialog, 30, gConnection.address);
    snprintf(text, sizeof(text), "%u", gConnection.port);
    DialogText(dialog, 32, text);
    for (row = 0; row < 4 && windowHours[row] != gConnection.windowHours; ++row) {}
    SetControlValue(PopupControl(dialog, 39), row < 4 ? row + 1 : 1);
    SetControlValue(PopupControl(dialog, 37), gListTitleLines);
    ShowPane(dialog, 1);
    ShowWindow((WindowPtr)dialog);
    PaintPreferences(dialog);
    InitCursor();
#ifdef TICKERTAPE_UI_SMOKE
    { extern void UISmokePreparePreferences(DialogPtr dialog);
      UISmokePreparePreferences(dialog); }
#endif
    for (;;) {
        ModalDialog(NewModalFilterUPP(PreferencesFilter), &item);
        if (item == 2) break;
        for (row = 0; row < kTypeCount; ++row) {
            if (item == 7 + row * 4) {
                UpdateSizeMenu(dialog, row);
                InvalRect(&dialog->portRect);
                break;
            }
        }
        if (item == 28) ShowPane(dialog, GetControlValue(PopupControl(dialog, 28)));
        if (item == 33 && ReadConnection(dialog, &connection, true)) {
            DialogText(dialog, 34, "Connecting...");
            SetPort(dialog); DrawDialog(dialog);
            gConnectionDialog = dialog; gConnectionCloseRequested = false;
            if (TestBackendConnection(&connection, &articles)) {
                snprintf(text, sizeof(text), "Connected. %d articles available.", articles);
                DialogText(dialog, 34, text);
            } else DialogText(dialog, 34, NetworkErrorText());
            gConnectionDialog = NULL;
            SetPort(dialog);
            if (gConnectionCloseRequested) { item = 2; break; }
        }
        if (item == 1 && ReadConnection(dialog, &connection, false)) {
            previousConnection = gConnection;
            gConnection = connection;
            if (SaveConnectionSettings()) break;
            gConnection = previousConnection;
            ShowPane(dialog, 2);
            DialogText(dialog, 34, "Cannot save settings. Check that the disk is writable.");
        }
    }
    /* Remove the whole window before saving or disposing individual controls. */
    HideWindow((WindowPtr)dialog);
    if (item == 1) {
        for (row = 0; row < kTypeCount; ++row) {
            role = dialogRoles[row];
            base = 7 + row * 4;
            fontControl = PopupControl(dialog, base);
            sizeControl = PopupControl(dialog, base + 1);
            faceControl = PopupControl(dialog, base + 2);
            if (fontControl != NULL)
                gTypography[role].font = FontAtIndex(GetControlValue(fontControl));
            if (sizeControl != NULL)
                gTypography[role].size = sizes[max(0, min(kSizeCount - 1,
                                      GetControlValue(sizeControl) - 1))];
            if (faceControl != NULL)
                gTypography[role].face = faces[max(0, min(3,
                                      GetControlValue(faceControl) - 1))];
        }
        gListTitleLines = max(1, min(4, GetControlValue(PopupControl(dialog, 37))));
        SaveTypography();
    }
    DisposeDialog(dialog);
    for (row = 0; row < 2; ++row) {
        if (gPanePixels[row]) DisposePtr(gPanePixels[row]);
        gPanePixels[row] = NULL;
    }
    gPreferencesPane = 0;
    if (gMainWindow != NULL) SetPort(gMainWindow);
    if (item == 1 && gMainWindow != NULL) {
        InvalidateReaderLayout();
        UpdateScrollBars();
        InvalRect(&gMainWindow->portRect);
    }
}

#ifdef TICKERTAPE_UI_SMOKE
void UISmokePaintPreferences(DialogPtr dialog)
{
    gPanePainted[gPreferencesPane == 2 ? 1 : 0] = false;
    PaintPreferences(dialog);
}

Boolean UISmokePreferencesCacheWorks(DialogPtr dialog)
{
    unsigned long before = gPreferencesRenderCount;
    PaintPreferences(dialog);
    return before == gPreferencesRenderCount;
}

void UISmokeUpdateSizeMenu(DialogPtr dialog, short row)
{
    UpdateSizeMenu(dialog, row);
}

void UISmokeConnectionPane(DialogPtr dialog)
{
    ShowPane(dialog, 2);
    DialogText(dialog, 30, "192.0.2.1");
    DialogText(dialog, 32, "8081");
    SetControlValue(PopupControl(dialog, 39), 4);
    SelectDialogItemText(dialog, 30, 0, 32767);
    PreferencesEdit(dialog, kEditCopy);
    PreferencesEdit(dialog, kEditClear);
    PreferencesEdit(dialog, kEditPaste);
}
#endif
