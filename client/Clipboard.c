/* A System 7 Clipboard window showing the shared TEXT or PICT scrap.
 * It never edits the scrap merely by opening, refreshing, or closing. */
#include "compat.h"
/* Scrap Manager traps are provided by Multiverse through compat.h. */

WindowPtr gClipboardWindow;
static TEHandle gClipboardText;
static Handle gClipboardPicture;
static ControlHandle gClipboardScrollBar;
static Rect gClipboardView;
static Boolean gClipboardWanted;
static short gClipboardScrapCount;
static short gClipboardScroll;

Boolean ClipboardIsOpen(void) { return gClipboardWanted; }
Boolean ClipboardHasText(void)
{
    return gClipboardText != NULL && (*gClipboardText)->teLength > 0;
}
Boolean ClipboardCanCopy(void)
{
    return ClipboardHasText() &&
           (*gClipboardText)->selStart < (*gClipboardText)->selEnd;
}

static short ClipboardMaximum(void)
{
    short height;
    if (!ClipboardHasText()) return 0;
    height = (*gClipboardText)->nLines * (*gClipboardText)->lineHeight;
    return max(0, height - (gClipboardView.bottom - gClipboardView.top));
}

static void SetClipboardScroll(short value)
{
    short delta;
    value = max(0, min(value, ClipboardMaximum()));
    delta = gClipboardScroll - value;
    if (delta != 0 && gClipboardText != NULL) {
        TEScroll(0, delta, gClipboardText);
        gClipboardScroll = value;
    }
    SetControlMaximum(gClipboardScrollBar, ClipboardMaximum());
    SetControlValue(gClipboardScrollBar, gClipboardScroll);
}

static void ReadClipboard(void)
{
    Handle text;
    long size, offset;
    short i, count;
    Rect destination;
    SetPort(gClipboardWindow);
    if (gClipboardText != NULL) TEDispose(gClipboardText);
    gClipboardText = NULL;
    if (gClipboardPicture != NULL) DisposeHandle(gClipboardPicture);
    gClipboardPicture = NULL;
    gClipboardScroll = 0;
    gClipboardScrapCount = InfoScrap()->scrapCount;
    SetWTitle(gClipboardWindow, (ConstStringPtr)"\011Clipboard");
    text = NewHandle(0);
    if (text == NULL) return;
    size = GetScrap(text, 'TEXT', &offset);
    if (size > 0) {
        /* Plain TextEdit is limited to 32 KB on System 7. */
        count = min(size, 30000);
        HLock(text);
        for (i = 0; i < count; ++i) if ((*text)[i] == '\n') (*text)[i] = '\r';
        destination = gClipboardView;
        TextFont(geneva);
        TextSize(12);
        TextFace(0);
        gClipboardText = TENew(&destination, &gClipboardView);
        if (gClipboardText != NULL) TESetText(*text, count, gClipboardText);
        HUnlock(text);
        if (size > count)
            SetWTitle(gClipboardWindow, (ConstStringPtr)"\023Clipboard (partial)");
    } else {
        gClipboardPicture = NewHandle(0);
        if (gClipboardPicture != NULL &&
            GetScrap(gClipboardPicture, 'PICT', &offset) <= 0) {
            DisposeHandle(gClipboardPicture);
            gClipboardPicture = NULL;
        }
    }
    DisposeHandle(text);
    SetControlMaximum(gClipboardScrollBar, ClipboardMaximum());
    SetControlValue(gClipboardScrollBar, 0);
    InvalRect(&gClipboardWindow->portRect);
}

void UpdateClipboard(void)
{
    GrafPtr oldPort;
    if (gInBackground || !gClipboardWanted || gClipboardWindow == NULL) return;
    if (gClipboardScrapCount == InfoScrap()->scrapCount) return;
    GetPort(&oldPort);
    ReadClipboard();
    SetPort(oldPort);
}

void ToggleClipboard(void)
{
    Rect bounds, bar;
    static const unsigned char emptyTitle[] = { 0 };
    if (gClipboardWanted) { CloseClipboard(); return; }
    if (gClipboardWindow == NULL) {
        SetRect(&bounds, 76, 88, 456, 283);
        gClipboardWindow = NewWindow(NULL, &bounds, (ConstStringPtr)"\011Clipboard",
                                     false, noGrowDocProc, (WindowPtr)-1, true, 0);
        if (gClipboardWindow == NULL) { SysBeep(1); return; }
        SetPort(gClipboardWindow);
        SetRect(&gClipboardView, 8, 6, 356, 189);
        SetRect(&bar, 365, -1, 381, 196);
        gClipboardScrollBar = NewControl(gClipboardWindow, &bar, emptyTitle,
                                         true, 0, 0, 0, scrollBarProc, 0);
        if (gClipboardScrollBar == NULL) {
            DisposeWindow(gClipboardWindow);
            gClipboardWindow = NULL;
            SysBeep(1);
            return;
        }
    }
    gClipboardWanted = true;
    ReadClipboard();
    ShowWindow(gClipboardWindow);
    SelectWindow(gClipboardWindow);
}

void CloseClipboard(void)
{
    gClipboardWanted = false;
    if (gClipboardWindow != NULL) HideWindow(gClipboardWindow);
}

void SuspendClipboard(void)
{
    if (gClipboardWanted && gClipboardWindow != NULL) HideWindow(gClipboardWindow);
}

void ResumeClipboard(void)
{
    if (gClipboardWanted && gClipboardWindow != NULL) {
        UpdateClipboard();
        ShowWindow(gClipboardWindow);
    }
}

void DrawClipboard(void)
{
    Rect frame;
    short width, height, availableWidth, availableHeight;
    SetPort(gClipboardWindow);
    ClipRect(&gClipboardWindow->portRect);
    EraseRect(&gClipboardWindow->portRect);
    ClipRect(&gClipboardView);
    if (gClipboardText != NULL) {
        if (FrontWindow() == gClipboardWindow && !gInBackground)
            TEActivate(gClipboardText);
        else TEDeactivate(gClipboardText);
        TEUpdate(&gClipboardView, gClipboardText);
    } else if (gClipboardPicture != NULL && GetHandleSize(gClipboardPicture) >= 10) {
        HLock(gClipboardPicture);
        frame = (*(PicHandle)gClipboardPicture)->picFrame;
        width = frame.right - frame.left;
        height = frame.bottom - frame.top;
        availableWidth = gClipboardView.right - gClipboardView.left;
        availableHeight = gClipboardView.bottom - gClipboardView.top;
        if (width > 0 && height > 0) {
            if (width > availableWidth) {
                height = (long)height * availableWidth / width;
                width = availableWidth;
            }
            if (height > availableHeight) {
                width = (long)width * availableHeight / height;
                height = availableHeight;
            }
            SetRect(&frame, gClipboardView.left, gClipboardView.top,
                    gClipboardView.left + width, gClipboardView.top + height);
            DrawPicture((PicHandle)gClipboardPicture, &frame);
        }
        HUnlock(gClipboardPicture);
    } else {
        TextFont(geneva); TextSize(12); TextFace(0);
        MoveTo(gClipboardView.left, gClipboardView.top + 14);
        DrawString(InfoScrap()->scrapSize == 0 ?
                   (ConstStringPtr)"\027The Clipboard is empty." :
                   (ConstStringPtr)"\043This Clipboard cannot be displayed.");
    }
    ClipRect(&gClipboardWindow->portRect);
    DrawControls(gClipboardWindow);
}

void HandleClipboardClick(Point where, Boolean extend)
{
    ControlHandle control;
    short part, value, amount;
    part = FindControl(where, gClipboardWindow, &control);
    if (part != 0 && control == gClipboardScrollBar) {
        part = TrackControl(control, where, NULL);
        value = gClipboardScroll;
        amount = gClipboardView.bottom - gClipboardView.top;
        if (part == inThumb) value = GetControlValue(control);
        else if (part == inUpButton) value -= 15;
        else if (part == inDownButton) value += 15;
        else if (part == inPageUp) value -= amount;
        else if (part == inPageDown) value += amount;
        SetClipboardScroll(value);
    } else if (gClipboardText != NULL && PtInRect(where, &gClipboardView)) {
        TEActivate(gClipboardText);
        TEClick(where, extend, gClipboardText);
    }
}

void ScrollClipboard(short direction)
{
    SetPort(gClipboardWindow);
    SetClipboardScroll(gClipboardScroll + direction *
                       (gClipboardView.bottom - gClipboardView.top));
}

void SelectAllClipboardText(void)
{
    if (!ClipboardHasText()) return;
    SetPort(gClipboardWindow);
    TESetSelect(0, (*gClipboardText)->teLength, gClipboardText);
    TEActivate(gClipboardText);
}

void CopyClipboardText(void)
{
    TEHandle text = gClipboardText;
    Handle characters;
    short start, length;
    if (!ClipboardCanCopy()) return;
    start = (*text)->selStart;
    length = (*text)->selEnd - start;
    characters = (Handle)TEGetText(text);
    HLock(characters);
    if (ZeroScrap() != noErr || PutScrap(length, 'TEXT', *characters + start) != noErr)
        SysBeep(1);
    HUnlock(characters);
    UpdateClipboard();
}

void DisposeClipboard(void)
{
    if (gClipboardText != NULL) TEDispose(gClipboardText);
    if (gClipboardPicture != NULL) DisposeHandle(gClipboardPicture);
    if (gClipboardWindow != NULL) DisposeWindow(gClipboardWindow);
    gClipboardText = NULL;
    gClipboardPicture = NULL;
    gClipboardWindow = NULL;
}
