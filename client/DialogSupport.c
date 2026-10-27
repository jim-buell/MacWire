/* System 7 modal dialog defaults, including repainting the default outline. */
#include "compat.h"
#include <Dialogs.h>

static pascal void DrawDefaultButton(DialogPtr dialog, short itemNumber)
{
    short type;
    Handle item;
    Rect rect;
    PenState saved;
    GetDialogItem(dialog, 1, &type, &item, &rect);
    GetPenState(&saved);
    InsetRect(&rect, -4, -4);
    PenNormal();
    PenSize(3, 3);
    FrameRoundRect(&rect, 16, 16);
    SetPenState(&saved);
#ifdef TICKERTAPE_UI_SMOKE
    UISmokePostDialogKey();
#endif
}

void PrepareStandardDialog(DialogPtr dialog, short outlineItem, short cancelItem)
{
    short type;
    Handle item;
    Rect rect;
    SetWRefCon(dialog, cancelItem);
    GetDialogItem(dialog, outlineItem, &type, &item, &rect);
    SetDialogItem(dialog, outlineItem, type, (Handle)NewUserItemUPP(DrawDefaultButton), &rect);
}

pascal Boolean StandardDialogFilter(DialogPtr dialog, EventRecord *event, short *item)
{
    short key, type;
    Handle button;
    Rect rect;
    unsigned long ticks;
    if (event->what != keyDown) return false;
    key = event->message & 0xFF;
    if (key == 27 || ((event->modifiers & cmdKey) && key == '.'))
        *item = (short)GetWRefCon(dialog);
    else if (key == 13 || key == 3) *item = 1;
    else return false;
#ifdef TICKERTAPE_UI_SMOKE
    UISmokeSnapshot(GetWRefCon(dialog) == 2 ? "preferences.pbm" : "about.pbm");
#endif
    GetDialogItem(dialog, *item, &type, &button, &rect);
    if (button != NULL) {
        HiliteControl((ControlHandle)button, 1);
        Delay(8, &ticks);
        HiliteControl((ControlHandle)button, 0);
    }
    return true;
}
