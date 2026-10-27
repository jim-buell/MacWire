/*
 * UI.c — Window management and QuickDraw rendering
 *
 * Handles all visual aspects of the System 7 MacWire:
 *   - Creating and managing the two-pane main window
 *   - Drawing the article list and article content with QuickDraw
 *   - Drawing 1-bit dithered images (using QuickDraw)
 *   - Drawing the status bar
 *   - Drawing scroll bars
 *
 * The list takes one third of the resizable window. Each pane has a native
 * scroll bar at its right edge, and the window has a standard grow corner.
 */

#include "compat.h"
#include "articles.h"
#include "Connection.h"
#include "Footnotes.h"
#include "LinkDisplay.h"
#include "ReaderMarkup.h"
#include "ReaderFont.h"
#include "ReaderUnderline.h"
#include "ReaderViewport.h"
#include "WaitCursor.h"
#include "typography.h"
#include <stdlib.h>
#include <limits.h>
#include <Windows.h>
#include <QuickDraw.h>
#include <Fonts.h>
#include <Resources.h>
#include <TextEdit.h>

/* External declarations */
extern WindowPtr gMainWindow;
extern Rect gListRect, gContentRect;
extern short gSelectedIndex;
extern short gScrollOffset;
extern short gMaxArticles;

/* Pane rectangles (definition; declared in compat.h) */
Rect gListRect;
Rect gContentRect;
static Rect gListHeaderRect;
static Rect gListScrollRect, gContentScrollRect, gContentHScrollRect;
static ControlHandle gListScrollBar, gContentScrollBar, gContentHScrollBar;
static short gDefaultImageWidth;
static RgnHandle gListDamage;
static short gListPreview=-1, gListPaintedSelection=-1, gListPaintedScroll;
static Boolean gListPainted;
static void UpdateListScrollBar(void);
static Boolean gReaderArticleLoading;
static void DrawReaderArticleLoading(void);
#ifdef TICKERTAPE_UI_SMOKE
unsigned long gListPaintCount, gReaderPaintCount;
unsigned long gListStripPaints;
#endif

/* Content-pane vertical scroll offset (pixels); clamped on each redraw. */
long gContentScroll = 0;
long gContentHeight = 0;
static short gContentHorizontalScroll, gContentWidth;

/* Character offsets refer to the complete article, independent of wrapping. */
static char gReaderText[kMaxBodyLength + kMaxTitleLength + kMaxAuthorLength + 160];
static short gReaderLength, gTitleOffset, gTimeOffset, gFeedOffset, gBodyOffset;
static char gPublisher[kMaxFeedLength + kMaxAuthorLength + 5];
static Ptr gReaderBitmap;
static long gReaderBitmapCapacity;
static Ptr gListBitmap;
static long gListBitmapCapacity;
static Boolean gReaderTextDirty = true;
static short gSelectionStart, gSelectionEnd, gSelectionAnchor;
static unsigned long gLastTextClick;
static short gLastTextOffset = -1;
static Boolean gSelectionOnly;
static short gPreviousSelectionStart, gPreviousSelectionEnd;
static FootnoteLink gFootnoteLinks[kMaxFootnoteLinks];
static short gFootnoteCount;
static short gFootnoteReturnDefinition=-1;
static long gFootnoteReturnScroll;
static char gHoverURL[513];
static void SetReaderSelection(short start, short end);
static short ReaderTextWidth(const char *text, short start, short length);

void ClearReaderSelection(void)
{
    if (gHoverURL[0]) { gHoverURL[0]=0; UpdateStatusLine(); }
    gSelectionStart = gSelectionEnd = gSelectionAnchor = 0;
    gReaderLength = 0;
    gReaderText[0] = 0;
    gReaderTextDirty = true;
    gFootnoteCount = 0;
    gFootnoteReturnDefinition=-1;
    gContentHorizontalScroll = 0;
    InvalidateReaderLayout();
    gLastTextOffset = -1;
}

static void PrepareReaderText(void)
{
    char *body;
    short i;
    struct ArticleEntry *article;
    if (gSelectedIndex < 0 || gSelectedIndex >= gMaxArticles) {
        ClearReaderSelection();
        return;
    }
    if (!gReaderTextDirty) return;
    article = &gArticles[gSelectedIndex];
    body = GetCachedArticleBody(gSelectedIndex);
    if (article->author[0])
        snprintf(gPublisher, sizeof(gPublisher), "%s \321 %s", article->feed, article->author);
    else snprintf(gPublisher, sizeof(gPublisher), "%s", article->feed);
    gFeedOffset = 0;
    gTitleOffset = strlen(gPublisher) + 1;
    gTimeOffset = gTitleOffset + strlen(article->title) + 1;
    gBodyOffset = gTimeOffset + strlen(article->date[0] ? article->date : article->time) + 2;
    snprintf(gReaderText, sizeof(gReaderText), "%s\r%s\r%s\r\r%s",
             gPublisher, article->title, article->date[0] ? article->date : article->time,
             body ? body : "");
    gReaderLength = strlen(gReaderText);
    gFootnoteCount = BuildFootnoteLinks(body, gFootnoteLinks, kMaxFootnoteLinks);
    /* Classic TEXT scrap uses carriage returns, without changing offsets. */
    for (i = 0; i < gReaderLength; ++i)
        if (gReaderText[i] == '\n') gReaderText[i] = '\r';
    gSelectionStart = min(gSelectionStart, gReaderLength);
    gSelectionEnd = min(gSelectionEnd, gReaderLength);
    gReaderTextDirty = false;
}

Boolean ReaderHasText(void)
{
    if (gReaderArticleLoading) return false;
    PrepareReaderText();
    return gReaderLength > 0;
}

Boolean ReaderCanCopy(void)
{
    return ReaderHasText() && gSelectionEnd > gSelectionStart;
}

void SelectAllReaderText(void)
{
    if (!ReaderHasText()) return;
    gSelectionAnchor = 0;
    SetReaderSelection(0, gReaderLength);
}

static void SetReaderSelection(short start, short end)
{
    if (start == gSelectionStart && end == gSelectionEnd) return;
    gPreviousSelectionStart = gSelectionStart;
    gPreviousSelectionEnd = gSelectionEnd;
    gSelectionStart = start;
    gSelectionEnd = end;
    gSelectionOnly = true;
    DrawContentPane();
    gSelectionOnly = false;
}

#ifdef TICKERTAPE_UI_SMOKE
void UISmokeSelectRange(short start, short end) { SetReaderSelection(start, end); }
#endif

void CopyReaderText(void)
{
    if (!ReaderCanCopy()) return;
    if (ZeroScrap() != noErr ||
        PutScrap(gSelectionEnd - gSelectionStart, 'TEXT',
                 (Ptr)(gReaderText + gSelectionStart)) != noErr) SysBeep(1);
    UpdateClipboard();
}

static void InvertTextRange(const char *text, short start, short from, short to,
                            short left, short baseline, short right)
{
    FontInfo info;
    Rect selected;
    if (from >= to) return;
    ReaderFontInfo(&info);
    SetRect(&selected, left + ReaderTextWidth(text, start, from - start),
            baseline - info.ascent,
            min(right, left + ReaderTextWidth(text, start, to - start)),
            baseline + info.descent + 2);
    InvertRect(&selected);
}

static void HighlightText(const char *text, short start, short end,
                          short offset, short left, short baseline, short right)
{
    short from, to, oldFrom, oldTo, edges[4], i, j, value;
    if (offset < 0 || gInBackground || FrontWindow() != gMainWindow) return;
    from = max(start, min(end, gSelectionStart - offset));
    to = max(start, min(end, gSelectionEnd - offset));
    if (!gSelectionOnly) {
        InvertTextRange(text, start, from, to, left, baseline, right);
        return;
    }
    oldFrom = max(start, min(end, gPreviousSelectionStart - offset));
    oldTo = max(start, min(end, gPreviousSelectionEnd - offset));
    edges[0] = oldFrom; edges[1] = oldTo; edges[2] = from; edges[3] = to;
    for (i = 1; i < 4; ++i) {
        value = edges[i]; j = i;
        while (j > 0 && edges[j - 1] > value) { edges[j] = edges[j - 1]; --j; }
        edges[j] = value;
    }
    /* Invert only the difference. Text and overlapping highlight stay put. */
    for (i = 0; i < 3; ++i)
        if ((edges[i] >= oldFrom && edges[i] < oldTo) !=
            (edges[i] >= from && edges[i] < to))
            InvertTextRange(text, start, edges[i], edges[i + 1], left, baseline, right);
}

/* Style lookup is expanded once per layout, so drawing on a 68000 does
   constant-time lookups rather than scanning every formatting span per glyph. */
static unsigned short gBodyStyles[WIRE_MAX_BODY];
static const char *gStyleText;
static short gStyleOffset, gStyleRole;
static Boolean gStyleHeading;
static unsigned short gStyleBlock;
static short gRunRise;
static Boolean gLayoutInterrupted;
static Boolean gReaderLayoutActive;
static short gLoadingImageIndex;
static long gImagePreviousHeight, gImageViewportAnchor;

static unsigned short ReaderStyleAt(const char *text, short position)
{
    long bodyPosition=(long)gStyleOffset-gBodyOffset+position;
    return text==gStyleText && gStyleOffset>=gBodyOffset && bodyPosition>=0 && bodyPosition<WIRE_MAX_BODY ? gBodyStyles[bodyPosition] : 0;
}

static void ApplyReaderRun(unsigned short flags)
{
    ApplyTypography(gStyleRole);
    if (flags & (WIRE_STYLE_CODE|WIRE_STYLE_PRE|WIRE_STYLE_TABLE)) qd.thePort->txFont=monaco;
    if (gStyleHeading || ((flags & WIRE_STYLE_BOLD) && !(flags & WIRE_STYLE_TABLE))) Bold;
    if ((flags & WIRE_STYLE_ITALIC) && !(flags & WIRE_STYLE_TABLE)) Italic;
    gRunRise=0;
    if (flags & (WIRE_STYLE_SUP|WIRE_STYLE_SUB)) {
        FontInfo base;
        ReaderFontInfo(&base);
        gRunRise=ReaderScriptRise(&base,(flags & WIRE_STYLE_SUP)!=0);
        qd.thePort->txSize=max(6, (qd.thePort->txSize*3+2)/4);
    }
}

/* Charge clearance to the final italic character, including prefix widths
   used by selection and links. Do not add it within an italic word. */
static short ReaderBoundaryClearance(const char *text, short position)
{
    unsigned short flags=ReaderStyleAt(text,position);
    if (text==gStyleText && gStyleOffset>=gBodyOffset &&
        (flags & WIRE_STYLE_ITALIC) && text[position]!=' ' &&
        text[position+1] && !(flags & WIRE_STYLE_TABLE) &&
        ((flags ^ ReaderStyleAt(text,position+1)) & WIRE_STYLE_ITALIC))
    {
        ApplyReaderRun(flags);
        return ReaderItalicClearance((unsigned char)text[position+1]);
    }
    return 0;
}

static short ReaderTextWidth(const char *text, short start, short length)
{
    short end=start+length, run, total=0;
    if (text!=gStyleText || gStyleOffset<gBodyOffset) return ReaderFontWidth(text,start,length);
    while (start<end) {
        unsigned short flags=ReaderStyleAt(text,start);
        run=start+1;
        while (run<end && ReaderStyleAt(text,run)==flags) ++run;
        ApplyReaderRun(flags);
        total=min(32000L,(long)total+ReaderFontWidth(text,start,run-start)+ReaderBoundaryClearance(text,run-1));
        start=run;
    }
    ApplyReaderRun(gStyleBlock);
    return total;
}

static void DrawReaderLine(const char *text, short start, short end, short left, short baseline)
{
    short run, width, x=left, rise;
    while (start<end) {
        unsigned short flags=ReaderStyleAt(text,start);
        run=start+1;
        while (run<end && ReaderStyleAt(text,run)==flags) ++run;
        if (text==gStyleText && gStyleOffset>=gBodyOffset) ApplyReaderRun(flags);
        rise=gRunRise;
        MoveTo(x,baseline+rise);
        ReaderFontDraw(text,start,run-start);
        width=ReaderFontWidth(text,start,run-start);
        if ((flags & WIRE_STYLE_TABLE) && (flags & WIRE_STYLE_BOLD)) {
            FontInfo info;
            short underline;
            ReaderFontInfo(&info); underline=baseline+rise+ReaderUnderlineOffset(&info);
            MoveTo(x,underline); LineTo(x+width-1,underline);
        }
        if (flags & WIRE_STYLE_STRIKE) {
            MoveTo(x,baseline+rise-max(2,qd.thePort->txSize/3));
            LineTo(x+width-1,baseline+rise-max(2,qd.thePort->txSize/3));
        }
        x+=width+ReaderBoundaryClearance(text,run-1); start=run;
    }
    if (text==gStyleText && gStyleOffset>=gBodyOffset) ApplyReaderRun(gStyleBlock);
}

/* --- Body-text layout helpers --- */

#define kMaxBodyLines (kMaxBodyLength + kMaxTitleLength + kMaxAuthorLength + 128)

typedef struct { short start; short end; } BodyLine;

/*
 * LayoutText — wrap text to the pane width using the current port font.
 * Records the [start, end) character range of each line in lines[] and
 * returns the number of lines produced.
 */
static short LayoutText(const char *text, Rect *rect, BodyLine *lines, short maximum)
{
    short availWidth = rect->right - rect->left;
    int i = 0, len = 0;
    short lineCount = 0;
    unsigned char *t = (unsigned char *)text;
    short widths[256];
    unsigned short widthStyle=0xFFFF;
    memset(widths, 0xFF, sizeof(widths));

    while (t[len]) len++;

    while (i < len && lineCount < maximum) {
        while (i < len && t[i] == ' ' && !(text==gStyleText && (gStyleBlock & (WIRE_STYLE_PRE|WIRE_STYLE_TABLE)))) i++;
        if (i >= len) break;
        short lineStart = i;
        short x = 0;
        short breakAt = -1;
        short lineEnd = len;

        while (i < len) {
            unsigned char c = t[i];
            if (gReaderLayoutActive && !(i & 255) && PollReaderAction()) {
                gLayoutInterrupted=true;
                return lineCount;
            }

            if (c == '\n' || c == '\r') {
                lineEnd = i;
                i++;
                if (c == '\r' && t[i] == '\n') i++;
                break;
            }

            short cw = widths[c];
            if (text==gStyleText && gStyleOffset>=gBodyOffset) {
                unsigned short flags=ReaderStyleAt(text,i);
                if (flags!=widthStyle) { memset(widths,0xFF,sizeof(widths)); widthStyle=flags; cw=-1; }
                if (cw<0) { ApplyReaderRun(flags); cw=widths[c]=max(0,ReaderFontCharWidth(c)); }
            } else if (cw < 0) cw = widths[c] = max(0, CharWidth((CharParameter)c));

            if (text==gStyleText && gStyleOffset>=gBodyOffset) cw+=ReaderBoundaryClearance(text,i);

            if (x + cw > availWidth && i > lineStart) {
                lineEnd = (breakAt > 0) ? breakAt : i;
                if (breakAt > lineStart) i = breakAt;
                break;
            }

            x += cw;
            if (c == ' ') breakAt = i + 1;
            i++;
        }

        lines[lineCount].start = lineStart;
        /* Scaled fonts accumulate fractional advances. CharWidth's rounded
           per-glyph widths can underestimate the actual DrawText width. */
        if (ReaderTextWidth(text, lineStart, lineEnd - lineStart) > availWidth &&
            lineEnd > lineStart + 1) {
            short low = lineStart + 1, high = lineEnd, fit, wordEnd;
            while (low < high) {
                short middle = low + (high - low + 1) / 2;
                if (ReaderTextWidth(text, lineStart, middle - lineStart) <= availWidth)
                    low = middle;
                else high = middle - 1;
            }
            fit = low; wordEnd = fit;
            while (wordEnd > lineStart && t[wordEnd - 1] != ' ') --wordEnd;
            lineEnd = wordEnd > lineStart ? wordEnd : fit;
            i = lineEnd;
        }
        lines[lineCount].end = lineEnd;
        lineCount++;
        if (gReaderLayoutActive && !(lineCount & 15)) {
            UpdateNetworkWaitCursor();
            if (PollReaderAction()) { gLayoutInterrupted=true; return lineCount; }
        }
    }

    return lineCount;
}

/* Draw a single line with an ellipsis and a real right margin. */
static void DrawFittedText(const char *source, Rect *field, short baseline,
                           short offset)
{
    Str255 label;
    short allowed = field->right - field->left;
    short sourceLength;
    Boolean shortened = false;
    StringToPStr(label, source);
    while (label[0] > 0 && StringWidth(label) > allowed) {
        label[0]--;
        shortened = true;
    }
    sourceLength = label[0];
    if (shortened && allowed > StringWidth((ConstStringPtr)"\003...")) {
        while (label[0] > 0 &&
               StringWidth(label) + StringWidth((ConstStringPtr)"\003...") > allowed)
            label[0]--;
        sourceLength = label[0];
        if (label[0] <= 252) {
            label[label[0] + 1] = '.';
            label[label[0] + 2] = '.';
            label[label[0] + 3] = '.';
            label[0] += 3;
        }
    }
    ClipRect(field);
    if (gListDamage) SectRgn(qd.thePort->clipRgn, gListDamage, qd.thePort->clipRgn);
    MoveTo(field->left, baseline);
    if (!gSelectionOnly) DrawString(label);
    HighlightText(source, 0, sourceLength, offset,
                  field->left, baseline, field->right);
}

static void DrawFittedString(const char *source, Rect *field, short baseline)
{
    DrawFittedText(source, field, baseline, -1);
}

/* Wrap the preferred number of lines, fitting remaining text into the last. */
static void DrawListTitle(const char *source, Rect *field, short baseline)
{
    BodyLine lines[4];
    short i, length, count = LayoutText(source, field, lines, gListTitleLines);
    for (i = 0; i < count - 1; ++i) {
        length = lines[i].end - lines[i].start;
        while (length > 0 && source[lines[i].start + length - 1] == ' ') --length;
        ClipRect(field);
        if (gListDamage) SectRgn(qd.thePort->clipRgn, gListDamage, qd.thePort->clipRgn);
        MoveTo(field->left, baseline);
        DrawText((Ptr)source, lines[i].start, length);
        baseline += GetTypographyLineHeight(kTypeListTitle);
    }
    DrawFittedString(source + (count ? lines[count - 1].start : 0), field, baseline);
}

/* Cache the complete document once per article/font/width change. All text
   sections and images share one scroll offset; no sticky, truncated header. */
typedef struct {
    const char *text;
    short role, offset, count, lineHeight;
    Boolean heading;
    unsigned short block;
    short indent, width;
    FontInfo font;
    long top;
    BodyLine *lines;
} ReaderSection;
#define kMaxReaderSections 128
static ReaderSection gSections[kMaxReaderSections];
static short gSectionCount;
static BodyLine gReaderLines[kMaxBodyLines];
static long gImageTop[kMaxImagesPerArticle];
static short gImageWidth[kMaxImagesPerArticle], gImageHeight[kMaxImagesPerArticle];
static Boolean gReaderLayoutDirty = true;
static short gReaderLayoutWidth;
static RgnHandle gReaderDamage;
#ifdef TICKERTAPE_UI_SMOKE
unsigned long gReaderLayoutBuilds, gReaderStripPaints;
#endif

void InvalidateReaderLayout(void) { gReaderLayoutDirty = true; }

static long LayoutReaderImage(struct ArticleEntry *article, short i, short width, long top)
{
    Handle handle = article->images[i];
    gImageTop[i] = top;
    if (handle) {
        CachedImage *image;
        HLock(handle); image = (CachedImage *)*handle;
        gImageWidth[i] = image->width;
        gImageHeight[i] = image->height;
        HUnlock(handle);
    } else {
        gImageWidth[i] = width;
        gImageHeight[i] = GetBodyLineHeight() + 4;
    }
    return top + gImageHeight[i] + 8;
}

static long LayoutReaderText(const char *text, short role, short offset,
                             Rect *measure, short *used, long top, short gap, Boolean heading)
{
    ReaderSection *section = &gSections[gSectionCount++];
    section->text = text; section->role = role; section->offset = offset;
    section->heading = heading;
    section->block=offset>=gBodyOffset && offset-gBodyOffset<WIRE_MAX_BODY ? gBodyStyles[offset-gBodyOffset] & (WIRE_STYLE_PRE|WIRE_STYLE_TABLE|WIRE_STYLE_QUOTE) : 0;
    section->indent=(section->block & WIRE_STYLE_QUOTE) ? 12 : 0;
    gStyleText=text; gStyleOffset=offset; gStyleRole=role; gStyleHeading=heading; gStyleBlock=section->block;
    ApplyReaderRun(section->block);
    ReaderFontInfo(&section->font);
    section->lineHeight = max(1, section->font.ascent+section->font.descent+max(1,section->font.leading)+1);
    section->top = top; section->lines = gReaderLines + *used;
    {
        Rect available=*measure;
        short i;
        available.right-=section->indent;
        if (section->block & (WIRE_STYLE_PRE|WIRE_STYLE_TABLE)) available.right=32000;
        section->count = LayoutText(text, &available, section->lines, kMaxBodyLines - *used);
        section->width=0;
        for (i=0; i<section->count && !gLayoutInterrupted; ++i)
            section->width=max(section->width,ReaderTextWidth(text,section->lines[i].start,section->lines[i].end-section->lines[i].start));
        if (offset>=gBodyOffset) {
            unsigned short previous=0xFFFF;
            for (i=0; text[i] && !gLayoutInterrupted; ++i) {
                unsigned short flags=ReaderStyleAt(text,i);
                if (flags!=previous) {
                    FontInfo runFont;
                    ApplyReaderRun(flags); ReaderFontInfo(&runFont);
                    section->font.ascent=max(section->font.ascent,runFont.ascent-gRunRise);
                    section->font.descent=max(section->font.descent,runFont.descent+gRunRise);
                    section->font.leading=max(section->font.leading,runFont.leading);
                    previous=flags;
                }
            }
            ApplyReaderRun(section->block);
            section->lineHeight=max(1,section->font.ascent+section->font.descent+max(1,section->font.leading)+1);
        }
    }
    *used += section->count;
    return top + (section->count ? (long)section->count * section->lineHeight + gap : 0);
}

static void ApplyReaderSectionStyle(ReaderSection *section)
{
    gStyleText=section->text; gStyleOffset=section->offset; gStyleRole=section->role;
    gStyleHeading=section->heading; gStyleBlock=section->block;
    ApplyReaderRun(section->block);
}

static long LayoutReaderBody(char *text, short offset, Rect *measure, short *used, long top)
{
    short start=0, end, length=strlen(text), prefix;
    char saved;
    unsigned short block;
    while (start<length && !gLayoutInterrupted) {
        block=gBodyStyles[offset-gBodyOffset+start] & (WIRE_STYLE_PRE|WIRE_STYLE_TABLE|WIRE_STYLE_QUOTE);
        prefix=(block & (WIRE_STYLE_PRE|WIRE_STYLE_TABLE)) ? 0 : ReaderHeadingPrefix(text+start);
        end=start;
        if (gSectionCount >= kMaxReaderSections-kMaxImagesPerArticle-1) end=length;
        else if (prefix) {
            while (end<length && text[end]!='\r' && text[end]!='\n') ++end;
        } else {
            while (end<length) {
                while (end<length && text[end]!='\r' && text[end]!='\n') ++end;
                while (end<length && (text[end]=='\r' || text[end]=='\n')) ++end;
                if (end<length && ((gBodyStyles[offset-gBodyOffset+end] & (WIRE_STYLE_PRE|WIRE_STYLE_TABLE|WIRE_STYLE_QUOTE))!=block || (!(block & (WIRE_STYLE_PRE|WIRE_STYLE_TABLE)) && ReaderHeadingPrefix(text+end)))) break;
            }
        }
        saved=text[end]; text[end]=0;
        top=LayoutReaderText(text+start+prefix,kTypeBody,offset+start+prefix,
                             measure,used,top,prefix ? 8 : 4,prefix!=0);
        text[end]=saved;
        start=end;
        while (start<length && (text[start]=='\r' || text[start]=='\n')) ++start;
    }
    return top;
}

static void EnsureReaderLayout(void)
{
    short i, j, used = 0, width = gContentRect.right - gContentRect.left - 16;
    short order[kMaxImagesPerArticle], positions[kMaxImagesPerArticle];
    short length, start = 0, position;
    long top = 4;
    Rect measure;
    struct ArticleEntry *article;
    char *body;
    if (gSelectedIndex < 0 || gSelectedIndex >= gMaxArticles) {
        gContentHeight = 0; gContentScroll = 0; gSectionCount = 0;
        gContentWidth = gContentRect.right - gContentRect.left;
        gContentHorizontalScroll = 0; return;
    }
    if (!gReaderLayoutDirty && width == gReaderLayoutWidth) return;
#ifdef TICKERTAPE_UI_SMOKE
    ++gReaderLayoutBuilds;
#endif
    gLayoutInterrupted=false;
    if (UserActionPending()) return;
    PrepareReaderText();
    gReaderLayoutActive=true;
    article = &gArticles[gSelectedIndex];
    SetRect(&measure, 0, 0, max(1, width), 1);
    gSectionCount = 0;
    top = LayoutReaderText(gPublisher, kTypeFeed, gFeedOffset, &measure, &used, top, 4, false);
    top = LayoutReaderText(article->title, kTypeReaderTitle, gTitleOffset, &measure, &used, top, 4, false);
    top = LayoutReaderText(article->date[0] ? article->date : article->time,
                          kTypeMetadata, gTimeOffset, &measure, &used, top, 8, false);
    memset(gBodyStyles,0,sizeof(gBodyStyles));
    if (article->links) {
        for (i=0; i<article->links->styleCount; ++i) {
            WireStyle *style=&article->links->styles[i];
            for (j=style->start; j<min(WIRE_MAX_BODY,(long)style->start+style->length); ++j) gBodyStyles[j]|=style->flags;
        }
    }
    body = GetCachedArticleBody(gSelectedIndex);
    if (!body || !body[0]) {
        top = LayoutReaderText(NetworkRequestFailed() ? "[Unable to load article]" : "[Loading]", kTypeBody, -1, &measure, &used, top, 4, false);
        for (i = 0; i < article->imageCount; ++i)
            top = LayoutReaderImage(article, i, width, top);
    } else {
        length = strlen(body);
        for (i = 0; i < article->imageCount; ++i) {
            order[i] = i;
            positions[i] = article->imageOffsets[i] == 0xFFFF ?
                           (i == 0 ? 0 : length) : min(article->imageOffsets[i], length);
        }
        for (i = 1; i < article->imageCount; ++i) {
            short image = order[i], anchor = positions[i];
            j = i;
            while (j > 0 && positions[j - 1] > anchor) {
                order[j] = order[j - 1]; positions[j] = positions[j - 1]; --j;
            }
            order[j] = image; positions[j] = anchor;
        }
        for (i = 0; i <= article->imageCount && !gLayoutInterrupted; ++i) {
            char saved;
            position = i < article->imageCount ? positions[i] : length;
            saved = body[position]; body[position] = 0;
            top = LayoutReaderBody(body + start, gBodyOffset + start,
                                   &measure, &used, top);
            body[position] = saved;
            if (i < article->imageCount) top = LayoutReaderImage(article, order[i], width, top);
            start = position;
        }
    }
    gReaderLayoutActive=false;
    if (gLayoutInterrupted) {
        gSectionCount=0;
        return; /* Keep the layout dirty; no partial document is published. */
    }
    gContentWidth = gContentRect.right - gContentRect.left;
    for (i = 0; i < article->imageCount; ++i)
        gContentWidth = max(gContentWidth, gImageWidth[i] + 16);
    for (i=0; i<gSectionCount; ++i) gContentWidth=max(gContentWidth,min(32000,gSections[i].width+gSections[i].indent+16));
    gContentHorizontalScroll = min(gContentHorizontalScroll,
        gContentWidth - (gContentRect.right - gContentRect.left));
    gContentHeight = top + 4;
    gReaderLayoutWidth = width; gReaderLayoutDirty = false;
    gContentScroll = max(0L, min(gContentScroll, gContentHeight - GetContentViewportHeight()));
}

/* Preserve the header and any content above a newly loaded photo. Images
   below the viewport only change the scroll range, without painting the body. */
short NextArticleImage(short index)
{
    if (index!=gSelectedIndex || index<0 || index>=gMaxArticles) return -1;
    EnsureReaderLayout();
    if (UserActionPending()) return -1;
    return ReaderNextImage(gImageTop,gImageHeight,gArticles[index].imageState,
                          gArticles[index].imageCount,gContentScroll,GetContentViewportHeight());
}

long ReaderImageUpdateTop(short imageIndex)
{
    EnsureReaderLayout();
    gLoadingImageIndex=imageIndex;
    gImagePreviousHeight=gImageHeight[imageIndex];
    gImageViewportAnchor=gContentScroll;
    {
        short i;
        long anchor=LONG_MAX;
        for (i=0; i<gSectionCount; ++i) {
            ReaderSection *section=&gSections[i];
            long row=max(0L,(gContentScroll-section->top)/section->lineHeight);
            long top=section->top+row*section->lineHeight;
            if (row<section->count && top+section->lineHeight>gContentScroll &&
                top<gContentScroll+GetContentViewportHeight()) anchor=min(anchor,top);
        }
        if (anchor!=LONG_MAX) gImageViewportAnchor=anchor;
    }
    return gImageTop[imageIndex];
}

void ReaderImageLoaded(long oldTop)
{
    long oldScroll=gContentScroll, newHeight, top, difference;
    short i, newWidth;
    Boolean reuseLayout=!gReaderLayoutDirty &&
        gReaderLayoutWidth==gContentRect.right-gContentRect.left-16;
    Rect damage=gContentRect;
    Handle handle=gArticles[gSelectedIndex].images[gLoadingImageIndex];
    CachedImage *image;
    if (!handle) return;
    HLock(handle); image=(CachedImage *)*handle;
    newHeight=image->height; newWidth=image->width; HUnlock(handle);
    /* Apply anchoring before cooperative layout can be interrupted by input.
       Its provisional height limit is exact: only this image changed size. */
    gContentScroll=ReaderScrollForImageAnchor(oldScroll,gImageViewportAnchor,oldTop,
        gImagePreviousHeight,newHeight,
        gContentHeight+newHeight-gImagePreviousHeight-GetContentViewportHeight());
    if (reuseLayout) {
        /* Photos keep their own pixel width and never change prose wrapping.
           Shift the already-measured sections instead of rewrapping the story. */
        difference=newHeight-gImagePreviousHeight;
        for (i=0; i<gSectionCount; ++i)
            if (gSections[i].top>oldTop) gSections[i].top+=difference;
        for (i=0; i<gArticles[gSelectedIndex].imageCount; ++i)
            if (gImageTop[i]>oldTop) gImageTop[i]+=difference;
        gImageHeight[gLoadingImageIndex]=newHeight;
        gImageWidth[gLoadingImageIndex]=newWidth;
        gContentHeight+=difference;
        gContentWidth=gContentRect.right-gContentRect.left;
        for (i=0; i<gArticles[gSelectedIndex].imageCount; ++i)
            gContentWidth=max(gContentWidth,gImageWidth[i]+16);
        for (i=0; i<gSectionCount; ++i)
            gContentWidth=max(gContentWidth,min(32000,gSections[i].width+gSections[i].indent+16));
        gContentHorizontalScroll=min(gContentHorizontalScroll,
            gContentWidth-(gContentRect.right-gContentRect.left));
    } else InvalidateReaderLayout();
    if (oldScroll!=gContentScroll) UpdateContentPane();
    else {
        top=gContentRect.top+oldTop-gContentScroll;
        if (top<damage.bottom) {
            if (top>damage.top) damage.top=(short)top;
            InvalidateRect(&damage);
        }
    }
    EnsureReaderLayout();
    if (UserActionPending()) return;
    UpdateScrollBars();
}

static short CharacterAtX(const char *text, short start, short end, short x)
{
    short low=start, high=end;
    while (low<high) {
        short middle=low+(high-low)/2;
        short before=ReaderTextWidth(text,start,middle-start);
        short after=ReaderTextWidth(text,start,middle+1-start);
        if (x<(before+after+1)/2) high=middle;
        else low=middle+1;
    }
    return low;
}

static short ReaderOffsetAtPoint(Point where)
{
    short i, row;
    long y;
    EnsureReaderLayout();
    y = where.v - gContentRect.top + gContentScroll;
    for (i = 0; i < gSectionCount; ++i) {
        ReaderSection *section = &gSections[i];
        if (!section->count) continue;
        if (y < section->top + (long)section->count * section->lineHeight) {
            if (section->offset < 0) return gReaderLength;
            row = max(0L, min((long)section->count - 1, (y - section->top) / section->lineHeight));
            ApplyReaderSectionStyle(section);
            return section->offset + CharacterAtX(section->text,
                section->lines[row].start, section->lines[row].end,
                where.h - gContentRect.left - 8 - section->indent + gContentHorizontalScroll);
        }
    }
    return gReaderLength;
}

/* Use the actual wrapped glyph rectangles, rather than selection's nearest
   character, so whitespace beside a link never displays its destination. */
static const char *ReaderLinkAtPoint(Point where)
{
    short i, row, j;
    long y;
    WireLinks *links;
    if (gReaderArticleLoading || !PtInRect(where, &gContentRect) || gSelectedIndex < 0 || gSelectedIndex >= gMaxArticles) return NULL;
    EnsureReaderLayout();
    links = gArticles[gSelectedIndex].links;
    if (!links) return NULL;
    y=where.v-gContentRect.top+gContentScroll;
    for (i=0; i<gSectionCount; ++i) {
        ReaderSection *section=&gSections[i];
        BodyLine *line;
        short x=where.h-gContentRect.left-8+gContentHorizontalScroll;
        if (section->offset<gBodyOffset || !section->count || y<section->top ||
            y>=section->top+(long)section->count*section->lineHeight) continue;
        row=(y-section->top)/section->lineHeight;
        line=&section->lines[row];
        ApplyReaderSectionStyle(section);
        for (j=0; j<links->count; ++j) {
            WireLink *link=&links->entries[j];
            short from=max(line->start,gBodyOffset+link->start-section->offset);
            short to=min(line->end,gBodyOffset+link->start+link->length-section->offset);
            if (from<to && x-section->indent>=ReaderTextWidth(section->text,line->start,from-line->start) &&
                x-section->indent<ReaderTextWidth(section->text,line->start,to-line->start))
                return links->urls+link->urlOffset;
        }
    }
    return NULL;
}

void UpdateReaderLinkHover(void)
{
    const char *url=NULL;
    Point where;
    if (!gInBackground && gMainWindow != NULL && FrontWindow()==gMainWindow) {
        SetPort(gMainWindow); GetMouse(&where);
        url=ReaderLinkAtPoint(where);
    }
    if (!url) url="";
    if (strcmp(gHoverURL,url)) {
        strncpy(gHoverURL,url,sizeof(gHoverURL)-1); gHoverURL[sizeof(gHoverURL)-1]=0;
        UpdateStatusLine();
    }
}

/* Always read mouse coordinates in the reader's port, including after an
   autoscroll paint or a scrollbar update changes QuickDraw state. */
static Boolean ReaderTrackMouse(Point *where)
{
#ifdef TICKERTAPE_UI_SMOKE
    extern short UISmokeReaderMouse(Point *where);
    short test = UISmokeReaderMouse(where);
    if (test >= 0) return test != 0;
#endif
    if (!StillDown()) return false;
    SetPort(gMainWindow); GetMouse(where);
    return true;
}

static Boolean WordCharacter(char c)
{
    unsigned char ch = (unsigned char)c;
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch >= 128 || ch == '\'' || ch == '_';
}

static void ActivateFootnote(short offset)
{
    short link, sectionIndex, row, target;
    for (link=0; link<gFootnoteCount; ++link) {
        FootnoteLink *note=&gFootnoteLinks[link];
        if (offset<gBodyOffset+note->start || offset>=gBodyOffset+note->start+note->length) continue;
        target=gBodyOffset+note->target;
        if (!strncmp(gReaderText+gBodyOffset+note->start,"[Back to ",9)) {
            short source;
            for (source=0; source<gFootnoteCount; ++source)
                if (gFootnoteLinks[source].start==note->target &&
                    gBodyOffset+gFootnoteLinks[source].target==gFootnoteReturnDefinition) {
                    ScrollReaderTo(gFootnoteReturnScroll);
                    return;
                }
        } else {
            gFootnoteReturnDefinition=target;
            gFootnoteReturnScroll=gContentScroll;
        }
        EnsureReaderLayout();
        for (sectionIndex=0; sectionIndex<gSectionCount; ++sectionIndex) {
            ReaderSection *section=&gSections[sectionIndex];
            for (row=0; row<section->count; ++row) {
                BodyLine *line=&section->lines[row];
                if (target>=section->offset+line->start && target<=section->offset+line->end) {
                    ScrollReaderTo(section->top+(long)row*section->lineHeight);
                    return;
                }
            }
        }
        return;
    }
}

void HandleReaderTextClick(Point where, Boolean extend)
{
    short offset, previous, start, end;
    unsigned long now, nextScroll;
    Boolean word;
    if (!PtInRect(where, &gContentRect) || !ReaderHasText()) return;
    offset = ReaderOffsetAtPoint(where);
    now = TickCount();
    /* DblTime is the Toolbox's user-configured double-click interval. */
    word = !extend && gLastTextOffset == offset &&
           now - gLastTextClick <= (unsigned long)LMGetDoubleTime();
    gLastTextClick = now;
    gLastTextOffset = offset;
    if (!extend) gSelectionAnchor = offset;
    if (word && offset < gReaderLength) {
        start = offset;
        end = offset + 1;
        while (start > 0 && WordCharacter(gReaderText[start - 1])) --start;
        while (end < gReaderLength && WordCharacter(gReaderText[end])) ++end;
        gSelectionAnchor = start;
        SetReaderSelection(start, end);
    } else {
        SetReaderSelection(min(gSelectionAnchor, offset), max(gSelectionAnchor, offset));
    }
    previous = offset;
    nextScroll = TickCount() + 4;
    while (ReaderTrackMouse(&where)) {
        now = TickCount();
        if (now >= nextScroll) {
            if (where.v < gContentRect.top && gContentScroll > 0) {
                ScrollReaderTo(gContentScroll - GetBodyLineHeight());
            } else if (where.v >= gContentRect.bottom &&
                       gContentScroll < max(0, gContentHeight - GetContentViewportHeight())) {
                ScrollReaderTo(gContentScroll + GetBodyLineHeight());
            }
            nextScroll = now + 4;
        }
        where.v = max(gContentRect.top, min(where.v, gContentRect.bottom - 1));
        offset = ReaderOffsetAtPoint(where);
        if (offset != previous) {
            SetReaderSelection(min(gSelectionAnchor, offset), max(gSelectionAnchor, offset));
            previous = offset;
        }
    }
    UpdateMenus();
    if (!extend && !word && gSelectionStart==gSelectionEnd) ActivateFootnote(offset);
}

void LayoutUI(void)
{
    Rect bounds;
    short split;
    if (gMainWindow == NULL) return;
    InvalidateReaderLayout();
    bounds = gMainWindow->portRect;
    split = bounds.left + (bounds.right - bounds.left) / 3;
    SetRect(&gListHeaderRect, bounds.left, bounds.top, split + 1, bounds.top + 18);
    SetRect(&gListRect, bounds.left + 2, bounds.top + 20,
            split - 15, bounds.bottom - 15);
    SetRect(&gListScrollRect, split - 15, gListRect.top - 1,
            split + 1, bounds.bottom - 14);
    SetRect(&gContentRect, split + 1, bounds.top + 2,
            bounds.right - 15, bounds.bottom - 15);
    if (!gDefaultImageWidth)
        gDefaultImageWidth = max(1, min(320, gContentRect.right - gContentRect.left - 16));
    SetRect(&gContentScrollRect, bounds.right - 15, bounds.top - 1,
            bounds.right + 1, bounds.bottom - 14);
    /* Share the list bar's right frame pixel at x=split. */
    SetRect(&gContentHScrollRect, split, bounds.bottom - 15,
            bounds.right - 14, bounds.bottom + 1);
    /* Hide before moving or sizing controls to prevent visible redraw steps. */
    if (gListScrollBar != NULL) HideControl(gListScrollBar);
    if (gContentScrollBar != NULL) HideControl(gContentScrollBar);
    if (gContentHScrollBar != NULL) HideControl(gContentHScrollBar);
    if (gListScrollBar != NULL) {
        MoveControl(gListScrollBar, gListScrollRect.left, gListScrollRect.top);
        SizeControl(gListScrollBar, 16,
                    gListScrollRect.bottom - gListScrollRect.top);
    }
    if (gContentScrollBar != NULL) {
        MoveControl(gContentScrollBar, gContentScrollRect.left,
                    gContentScrollRect.top);
        SizeControl(gContentScrollBar, 16,
                    gContentScrollRect.bottom - gContentScrollRect.top);
    }
    if (gContentHScrollBar != NULL) {
        MoveControl(gContentHScrollBar, gContentHScrollRect.left,
                    gContentHScrollRect.top);
        SizeControl(gContentHScrollBar,
                    gContentHScrollRect.right - gContentHScrollRect.left, 16);
    }
    if (gListScrollBar != NULL) ShowControl(gListScrollBar);
    if (gContentScrollBar != NULL) ShowControl(gContentScrollBar);
    if (gContentHScrollBar != NULL) ShowControl(gContentHScrollBar);
}

/*
 * InitUI — Initialize the user interface
 *
 * Creates the main window with two panes:
 *   1. Article list (left, ~200px wide)
 *   2. Article content (right, remaining width)
 */
void InitUI(void)
{
    Rect windowRect, desktop;
    Str255 windowTitle;

    /* Leave six pixels around the whole zoomDocProc frame on the desktop.
       Its border extends one pixel left/right/bottom and twenty above content.
       Calculate from the desktop, never the invisible window's empty regions. */
    desktop = (**LMGetGrayRgn()).rgnBBox;
    SetRect(&windowRect, desktop.left + 7, desktop.top + 26,
            desktop.right - 7, desktop.bottom - 7);
    StringToPStr(windowTitle, "MacWire");

    gMainWindow = NewWindow(
        NULL, &windowRect, windowTitle, false, 8 /* zoomDocProc */,
        (WindowPtr)-1, true, 0);
    if (gMainWindow == NULL) return;
    SetPort(gMainWindow);

    LayoutUI();

    // Initialize scroll bars
    InitScrollBars();

}

void ZoomReaderWindow(short part)
{
    WindowPeek window = (WindowPeek)gMainWindow;
    Rect desktop, frame, content;
    WStateData *state;
    if (!window || !window->dataHandle) return;
    if (part == inZoomOut) {
        desktop = (**LMGetGrayRgn()).rgnBBox;
        InsetRect(&desktop, 2, 2);
        frame = (**window->strucRgn).rgnBBox;
        content = (**window->contRgn).rgnBBox;
        HLock(window->dataHandle);
        state = (WStateData *)*window->dataHandle;
        SetRect(&state->stdState, desktop.left + content.left - frame.left,
                desktop.top + content.top - frame.top,
                desktop.right - frame.right + content.right,
                desktop.bottom - frame.bottom + content.bottom);
        HUnlock(window->dataHandle);
    }
    ZoomWindow(gMainWindow, part, true);
    SetPort(gMainWindow); LayoutUI(); InvalRect(&gMainWindow->portRect);
}

short GetListRowHeight(void)
{
    return max(kListRowHeight, GetTypographyLineHeight(kTypeMetadata) +
        GetTypographyLineHeight(kTypeFeed) + gListTitleLines * GetTypographyLineHeight(kTypeListTitle) + 14);
}

short GetVisibleListRows(void)
{
    short rows = (gListRect.bottom - gListRect.top) / GetListRowHeight();
    return rows > 0 ? rows : 1;
}

short GetReaderImageWidth(void)
{
    return gDefaultImageWidth;
}

short GetContentViewportHeight(void)
{
    short height = gContentRect.bottom - gContentRect.top;
    return height > 1 ? height : 1;
}

/*
 * DrawListPane — Draw the article list
 *
 * Draws each article in the list with:
 *   - Local date/time with AM/PM
 *   - Feed name on its own line
 *   - Title (Geneva 12)
 *   - Unread indicator (filled circle)
 */
Boolean HandleListHeaderClick(Point where)
{
    if (!PtInRect(where, &gListHeaderRect)) return false;
    ToggleArticleSort();
    return true;
}

static void DrawListHeader(void)
{
    short x = gListHeaderRect.right - 12, y = gListHeaderRect.top + 7, row;
    Rect background = gListHeaderRect;
    background.bottom += 2;
    PenNormal();
    EraseRect(&background);
    TextFont(3); TextSize(9); TextFace(underline);
    MoveTo(gListHeaderRect.left + 5, gListHeaderRect.top + 11);
    DrawString("\pDate");
    /* Classic list-header direction triangle: down for newest first. */
    for (row = 0; row < 4; ++row) {
        short half = gNewestFirst ? 3 - row : row;
        MoveTo(x - half, y + row); LineTo(x + half, y + row);
    }
    MoveTo(gListHeaderRect.left, gListHeaderRect.bottom - 1);
    LineTo(gListHeaderRect.right - 1, gListHeaderRect.bottom - 1);
    MoveTo(gListHeaderRect.left, gListHeaderRect.bottom + 1);
    LineTo(gListHeaderRect.right - 1, gListHeaderRect.bottom + 1);
    MoveTo(gListHeaderRect.right - 1, gListHeaderRect.top);
    LineTo(gListHeaderRect.right - 1, gListHeaderRect.bottom + 1);
}

/* Render a complete pane offscreen, then publish it in one CopyBits. The
   window's visible/update region still clips the final copy. */
static Boolean BeginPaneBuffer(Rect *bounds, GrafPort *port, BitMap *bitmap,
                               Ptr *storage, long *capacity)
{
    long bytes;
    bitmap->bounds=*bounds;
    bitmap->rowBytes=((bounds->right-bounds->left+15)/16)*2;
    bytes=(long)bitmap->rowBytes*(bounds->bottom-bounds->top);
    if (bytes<=0) return false;
    if (bytes>*capacity) {
        Ptr replacement=NewPtr(bytes);
        if (!replacement) return false;
        if (*storage) DisposePtr(*storage);
        *storage=replacement; *capacity=bytes;
    }
    bitmap->baseAddr=*storage;
    OpenPort(port); SetPortBits(bitmap); port->portRect=*bounds;
    RectRgn(port->visRgn,bounds); ClipRect(bounds);
    return true;
}

void DrawListPane(void)
{
    GrafPtr oldPort;
    short y, i, rowHeight, topBaseline, titleBaseline;
    FontInfo titleFont, metadataFont, feedFont;
    short metadataHeight;
    Rect rowRect, fieldRect, background;
    GrafPort bufferPort;
    BitMap bitmap;
    Boolean buffered;

    if (gMainWindow == NULL) return;
#ifdef TICKERTAPE_UI_SMOKE
    if (gListDamage) ++gListStripPaints; else ++gListPaintCount;
#endif
    GetPort(&oldPort);
    SetPort(gMainWindow);
    UpdateListScrollBar();
    background = gListRect;
    background.left = gMainWindow->portRect.left;
    background.top = gMainWindow->portRect.top;
    /* The header includes the scrollbar column and its right divider. */
    background.right = gListHeaderRect.right;
    buffered=BeginPaneBuffer(&background,&bufferPort,&bitmap,
                             &gListBitmap,&gListBitmapCapacity);
    ClipRect(&background);
    if (gListDamage) SectRgn(qd.thePort->clipRgn, gListDamage, qd.thePort->clipRgn);
    EraseRect(&background);
    if (!gListDamage) {
        ClipRect(&gMainWindow->portRect);
        DrawListHeader();
    }
    ClipRect(&gListRect);

    rowHeight = GetListRowHeight();
    GetTypographyFontInfo(kTypeMetadata, &metadataFont);
    GetTypographyFontInfo(kTypeFeed, &feedFont);
    metadataHeight = GetTypographyLineHeight(kTypeMetadata) + GetTypographyLineHeight(kTypeFeed);
    y = gListRect.top;
    for (i = gScrollOffset;
         i < gMaxArticles && y < gListRect.bottom;
         ++i, y += rowHeight) {
        SetRect(&rowRect, gListRect.left + 1, y,
                gListRect.right - 1, y + rowHeight);
        if (gListDamage && !RectInRgn(&rowRect, gListDamage)) continue;
        topBaseline = y + 2 + metadataFont.ascent;
        ApplyTypography(kTypeMetadata);
        SetRect(&fieldRect, gListRect.left + 4, y + 1,
                gListRect.right - 16, min(gListRect.bottom, y + GetTypographyLineHeight(kTypeMetadata) + 2));
        DrawFittedString(gArticles[i].time, &fieldRect, topBaseline);

        ApplyTypography(kTypeFeed);
        topBaseline = y + 2 + GetTypographyLineHeight(kTypeMetadata) + feedFont.ascent;
        SetRect(&fieldRect, gListRect.left + 4, y + GetTypographyLineHeight(kTypeMetadata) + 1,
                gListRect.right - 16, min(gListRect.bottom, y + metadataHeight + 2));
        DrawFittedString(gArticles[i].feed, &fieldRect, topBaseline);

        ApplyTypography(kTypeListTitle);
        GetFontInfo(&titleFont);
        titleBaseline = y + metadataHeight + 4 + titleFont.ascent;
        SetRect(&fieldRect, gListRect.left + 4, y + metadataHeight + 3,
                gListRect.right - 8, min(gListRect.bottom, y + rowHeight - 1));
        DrawListTitle(gArticles[i].title, &fieldRect, titleBaseline);

        ClipRect(&gListRect);
        if (gListDamage) SectRgn(qd.thePort->clipRgn, gListDamage, qd.thePort->clipRgn);
        if (!gArticles[i].isRead) {
            SetRect(&fieldRect, gListRect.right - 11, y + 4,
                    gListRect.right - 5, y + 10);
            PaintOval(&fieldRect);
        }
        if (i + 1 < gMaxArticles) {
            /* A one-pixel dotted rule, like a monochrome menu separator.
               Repeat the same pattern row so vertical pixel scrolling keeps
               the dots in phase with a complete redraw. */
            /* Pattern is a byte array, but QuickDraw reads it as words.
               An odd address causes an Address Error on the SE's 68000. */
            static const Pattern dots __attribute__((aligned(2))) = {0xAA, 0xAA, 0xAA, 0xAA,
                                         0xAA, 0xAA, 0xAA, 0xAA};
            PenState saved;
            GetPenState(&saved); PenNormal(); PenPat(&dots);
            MoveTo(gListRect.left + 4, y + rowHeight - 3);
            LineTo(gListRect.right - 5, y + rowHeight - 3);
            SetPenState(&saved);
        }
        if (i == (gListPreview>=0 ? gListPreview : gSelectedIndex)) {
            Rect highlight = rowRect;
            /* Leave two white pixels on each side of a divider. The previous
               divider is at y-3; the next is at y+rowHeight-3. */
            highlight.bottom -= 5;
            InvertRect(&highlight);
        }
    }
    gListPaintedSelection=gListPreview>=0 ? gListPreview : gSelectedIndex;
    gListPaintedScroll=gScrollOffset; gListPainted=true;
    if (buffered) {
        SetPort(gMainWindow); ClipRect(&background);
        if (gListDamage) SectRgn(qd.thePort->clipRgn,gListDamage,qd.thePort->clipRgn);
        CopyBits(&bitmap,&gMainWindow->portBits,&background,&background,srcCopy,NULL);
        ClosePort(&bufferPort);
    }
    ClipRect(&gMainWindow->portRect);
    /* Finish the left pane before any reader layout or network work. */
    if (gListScrollBar) Draw1Control(gListScrollBar);
    SetPort(oldPort);
}

/* Paint only the changed selection rectangles. The article being downloaded
   retains its identity until transport unwinds; the list can acknowledge the
   user's new target immediately without starting another request. */
void PreviewArticleSelection(short index)
{
    short selected=index>=0 ? index : gSelectedIndex;
    short pass, rowHeight;
    GrafPtr oldPort;
    RgnHandle clip;
    Rect highlight;
    gListPreview=index;
    if (index>=0 && index<gMaxArticles) {
        if (!gArticles[index].hasContent) {
            if (!gReaderArticleLoading) {
                gReaderArticleLoading=true;
                if (gHoverURL[0]) { gHoverURL[0]=0; UpdateStatusLine(); }
                DrawReaderArticleLoading();
                UpdateContentPane();
            }
        } else if (!ArticleNavigationPending()) FinishReaderArticleLoading();
    }
    if (!gMainWindow || gIndexLoading) return;
    if (!gListPainted || gListPaintedScroll!=gScrollOffset) {
        DrawListPane();
        return;
    }
    if (selected==gListPaintedSelection) return;
    clip=NewRgn();
    if (!clip) { UpdateArticleList(); return; }
    GetPort(&oldPort); SetPort(gMainWindow); GetClip(clip); ClipRect(&gListRect);
    rowHeight=GetListRowHeight();
    for (pass=0; pass<2; ++pass) {
        short row=pass ? selected : gListPaintedSelection;
        long top=gListRect.top+(long)(row-gScrollOffset)*rowHeight;
        if (row<gScrollOffset || row>=gMaxArticles || top>=gListRect.bottom) continue;
        SetRect(&highlight,gListRect.left+1,(short)top,gListRect.right-1,(short)(top+rowHeight-5));
        InvertRect(&highlight);
    }
    gListPaintedSelection=selected;
    SetClip(clip); DisposeRgn(clip); SetPort(oldPort);
}

void ScrollListTo(short offset)
{
    short previous;
    long delta;
    GrafPtr oldPort;
    RgnHandle damage, saved;
    if (!gMainWindow) return;
    GetPort(&oldPort); SetPort(gMainWindow);
    previous = gScrollOffset;
    gScrollOffset = max(0, min(offset, max(0, gMaxArticles - GetVisibleListRows())));
    delta = (long)(previous - gScrollOffset) * GetListRowHeight();
    if (!delta) { UpdateScrollBars(); SetPort(oldPort); return; }
    damage = NewRgn(); saved = NewRgn();
    if (damage && saved && labs(delta) < gListRect.bottom - gListRect.top &&
        FrontWindow() == gMainWindow && !gInBackground) {
        GetClip(saved); ClipRect(&gListRect);
        ScrollRect(&gListRect, 0, (short)delta, damage);
        gListDamage = damage; DrawListPane(); gListDamage = NULL;
        SetClip(saved);
    } else DrawListPane();
    if (damage) DisposeRgn(damage); if (saved) DisposeRgn(saved);
    SetPort(oldPort);
}

/* A queued click must not leave the previous story under a new highlight.
   This small immediate paint does not touch the article cache or its layout. */
static void DrawReaderArticleLoading(void)
{
    GrafPtr oldPort;
    RgnHandle clip;
    short font, size;
    Style face;
    FontInfo metrics;
    GrafPort bufferPort;
    BitMap bitmap;
    Boolean buffered;
    static const char label[]="Loading article...";
    if (!gMainWindow || gIndexLoading) return;
    clip=NewRgn();
    if (!clip) { UpdateContentPane(); return; }
    GetPort(&oldPort); SetPort(gMainWindow); GetClip(clip);
    font=qd.thePort->txFont; size=qd.thePort->txSize; face=qd.thePort->txFace;
    buffered=BeginPaneBuffer(&gContentRect,&bufferPort,&bitmap,
                             &gReaderBitmap,&gReaderBitmapCapacity);
    ClipRect(&gContentRect); EraseRect(&gContentRect);
    ApplyTypography(kTypeFeed); ReaderFontInfo(&metrics);
    /* Match the publisher section's inset, document top, and font ascent. */
    MoveTo(gContentRect.left+8,gContentRect.top+4+metrics.ascent);
    ReaderFontDraw(label,0,sizeof(label)-1);
    if (buffered) {
        SetPort(gMainWindow); ClipRect(&gContentRect);
        CopyBits(&bitmap,&gMainWindow->portBits,&gContentRect,&gContentRect,srcCopy,NULL);
        ClosePort(&bufferPort);
    }
    TextFont(font); TextSize(size); TextFace(face);
    SetClip(clip); DisposeRgn(clip); SetPort(oldPort);
    UpdateNetworkWaitCursor();
}

void FinishReaderArticleLoading(void)
{
    if (!gReaderArticleLoading) return;
    gReaderArticleLoading=false;
    UpdateContentPane();
}

/* Read the unselected glyph pixels once per line, before drawing any rules.
   The offscreen port and ordinary window port both use local bitmap bounds. */
static unsigned char gReaderUnderlineMask[4096];
static void DrawReaderLinkUnderline(short left, short right, short baseline,
                                    Boolean *ready)
{
    BitMap *bits=&qd.thePort->portBits;
    short x, run=-1, y=baseline+1;
    short width=bits->bounds.right-bits->bounds.left;
    PenState pen;
    if (y<gContentRect.top || y>=gContentRect.bottom) return;
    if (!*ready) {
        if (!ReaderUnderlineMask((const unsigned char *)bits->baseAddr,
                bits->rowBytes & 0x3FFF,width,bits->bounds.bottom-bits->bounds.top,
                y-bits->bounds.top,gReaderUnderlineMask,sizeof(gReaderUnderlineMask))) return;
        *ready=true;
    }
    left=max(left,max(gContentRect.left,bits->bounds.left));
    right=min(right,min(gContentRect.right,bits->bounds.right));
    GetPenState(&pen); PenNormal();
    for (x=left; x<=right; ++x) {
        short column=x-bits->bounds.left;
        Boolean draw=x<right && (gReaderUnderlineMask[column/8] & (0x80>>(column&7)));
        if (draw && run<0) run=x;
        if (!draw && run>=0) {
            MoveTo(run,y); LineTo(x-1,y); run=-1;
        }
    }
    SetPenState(&pen);
}

/*
 * DrawContentPane — Draw the article content
 *
 * Draws:
 *   - Headline (New York 14 Bold)
 *   - Metadata (date, author, feed — Monaco 9 Italic)
 *   - Body text (Geneva 12)
 *   - 1-bit dithered images (if any)
 */
void DrawContentPane(void)
{
    GrafPtr oldPort;
    short i, row, first, last;
    Rect lineRect, damageBounds;
    long baseline, documentY, bytes;
    GrafPort bufferPort;
    BitMap bitmap;
    Boolean buffered = false;
    if (gMainWindow == NULL) return;
    if (gReaderArticleLoading) { DrawReaderArticleLoading(); return; }
#ifdef TICKERTAPE_UI_SMOKE
    if (!gSelectionOnly) {
        if (gReaderDamage) ++gReaderStripPaints; else ++gReaderPaintCount;
    }
#endif
    GetPort(&oldPort); SetPort(gMainWindow);
    damageBounds = gReaderDamage ? (**gReaderDamage).rgnBBox : gContentRect;
    ClipRect(&gContentRect);
    if (gReaderDamage) SectRgn(qd.thePort->clipRgn, gReaderDamage, qd.thePort->clipRgn);
    PrepareReaderText(); EnsureReaderLayout();
    if (UserActionPending()) {
        ClipRect(&gMainWindow->portRect); SetPort(oldPort); return;
    }
    if (!gSelectionOnly) {
        bitmap.bounds = gContentRect;
        bitmap.rowBytes = ((gContentRect.right - gContentRect.left + 15) / 16) * 2;
        bytes = (long)bitmap.rowBytes * (gContentRect.bottom - gContentRect.top);
        if (bytes > gReaderBitmapCapacity) {
            Ptr replacement = NewPtr(bytes);
            if (replacement) {
                if (gReaderBitmap) DisposePtr(gReaderBitmap);
                gReaderBitmap = replacement; gReaderBitmapCapacity = bytes;
            }
        }
        if (bytes > 0 && gReaderBitmap && bytes <= gReaderBitmapCapacity) {
            bitmap.baseAddr = gReaderBitmap;
            OpenPort(&bufferPort); SetPortBits(&bitmap);
            bufferPort.portRect = gContentRect;
            RectRgn(bufferPort.visRgn, &gContentRect); ClipRect(&gContentRect);
            if (gReaderDamage) {
                Rect inkDamage=(**gReaderDamage).rgnBBox;
                InsetRect(&inkDamage,-1,-1); ClipRect(&inkDamage);
                RectRgn(bufferPort.visRgn,&gContentRect);
            }
            buffered = true;
        }
        EraseRect(&gContentRect);
    }
    if (gSelectedIndex >= 0 && gSelectedIndex < gMaxArticles)
    for (i = 0; i < gSectionCount; ++i) {
        ReaderSection *section = &gSections[i];
        ApplyReaderSectionStyle(section);
        first = max(0L, (gContentScroll + damageBounds.top - gContentRect.top - section->top) / section->lineHeight);
        last = min((long)section->count, (gContentScroll + damageBounds.bottom - gContentRect.top - section->top) / section->lineHeight + 1);
        for (row = first; row < last; ++row) {
            BodyLine *line = &section->lines[row];
            Boolean underlineReady=false;
            documentY = section->top + (long)row * section->lineHeight;
            baseline = gContentRect.top + documentY - gContentScroll + section->font.ascent;
            if (baseline + section->font.descent + 1 < gContentRect.top ||
                baseline - section->font.ascent >= gContentRect.bottom) continue;
            SetRect(&lineRect, gContentRect.left + 8 + section->indent - gContentHorizontalScroll,
                    baseline - section->font.ascent,
                    max(gContentRect.right-8,gContentRect.left+8+section->indent+section->width)-gContentHorizontalScroll,
                    baseline + section->font.descent + 2);
            if (gReaderDamage && !RectInRgn(&lineRect, gReaderDamage)) continue;
            MoveTo(lineRect.left, (short)baseline);
            if (!gSelectionOnly) DrawReaderLine(section->text,line->start,line->end,lineRect.left,baseline);
            if (!gSelectionOnly && section->offset>=gBodyOffset) {
                short link;
                for (link=0; link<gFootnoteCount; ++link) {
                    short from=max(line->start,gBodyOffset+gFootnoteLinks[link].start-section->offset);
                    short to=min(line->end,gBodyOffset+gFootnoteLinks[link].start+gFootnoteLinks[link].length-section->offset);
                    if (from<to) {
                        DrawReaderLinkUnderline(
                            lineRect.left+ReaderTextWidth(section->text,line->start,from-line->start),
                            lineRect.left+ReaderTextWidth(section->text,line->start,to-line->start),
                            baseline,&underlineReady);
                    }
                }
                if (gArticles[gSelectedIndex].links) {
                    WireLinks *links=gArticles[gSelectedIndex].links;
                    for (link=0; link<links->count; ++link) {
                        short from=max(line->start,gBodyOffset+links->entries[link].start-section->offset);
                        short to=min(line->end,gBodyOffset+links->entries[link].start+links->entries[link].length-section->offset);
                        if (from<to) {
                            DrawReaderLinkUnderline(
                                lineRect.left+ReaderTextWidth(section->text,line->start,from-line->start),
                                lineRect.left+ReaderTextWidth(section->text,line->start,to-line->start),
                                baseline,&underlineReady);
                        }
                    }
                }
            }
            HighlightText(section->text, line->start, line->end, section->offset,
                          lineRect.left, baseline, lineRect.right);
        }
    }
    if (!gSelectionOnly && gSelectedIndex >= 0 && gSelectedIndex < gMaxArticles)
        DrawArticleImages(gSelectedIndex, &gContentRect);
    if (buffered) {
        SetPort(gMainWindow);
        CopyBits(&bitmap, &gMainWindow->portBits, &gContentRect, &gContentRect, srcCopy, NULL);
        ClosePort(&bufferPort);
    }
    ClipRect(&gMainWindow->portRect);
    if (!gSelectionOnly && !gReaderDamage) UpdateScrollBars();
    SetPort(oldPort);
}

/* Photos retain their processed pixel size and follow the body anchors supplied
   by the server. Attachments without positions use the legacy fallback. */
void DrawArticleImages(short articleIndex, Rect *clipRect)
{
    short i, left;
    Rect destination, source;
    BitMap bits;
    long top;
    (void)clipRect;
    for (i = 0; i < gArticles[articleIndex].imageCount; ++i) {
        Handle handle = GetCachedImage(articleIndex, i);
        top = gContentRect.top + gImageTop[i] - gContentScroll;
        if (top + gImageHeight[i] <= gContentRect.top || top >= gContentRect.bottom) continue;
        left = gContentRect.left + 8 - gContentHorizontalScroll +
               max(0, (gContentRect.right - gContentRect.left - 16 - gImageWidth[i]) / 2);
        SetRect(&destination, left, top, left + gImageWidth[i], top + gImageHeight[i]);
        if (gReaderDamage && !RectInRgn(&destination, gReaderDamage)) continue;
        if (handle) {
            CachedImage *image;
            HLock(handle); image = (CachedImage *)*handle;
            SetRect(&source, 0, 0, image->width, image->height);
            bits.baseAddr = (Ptr)image->bits; bits.rowBytes = image->rowBytes; bits.bounds = source;
            CopyBits(&bits, &qd.thePort->portBits, &source, &destination, srcCopy, NULL);
            HUnlock(handle);
        } else {
            FontInfo font;
            ApplyTypography(kTypeBody); GetFontInfo(&font);
            MoveTo(destination.left, destination.top + font.ascent);
            DrawString(gArticles[articleIndex].imageState[i] == 2 ?
                (ConstStringPtr)"\021Image unavailable" : (ConstStringPtr)"\020Loading image...");
        }
    }
}

static void RedrawReaderForScroll(void)
{
    DrawContentPane();
    if (!UserActionPending() && !gReaderLayoutDirty && !gReaderArticleLoading)
        ValidRect(&gContentRect);
}

void ScrollReaderTo(long offset)
{
    long old, maximum, delta;
    GrafPtr oldPort;
    RgnHandle damage, saved;
    Boolean stale;
    if (gMainWindow == NULL || gReaderArticleLoading) return;
    stale=gReaderLayoutDirty;
    GetPort(&oldPort); SetPort(gMainWindow); EnsureReaderLayout();
    if (UserActionPending()) { SetPort(oldPort); return; }
    stale=stale || RectInRgn(&gContentRect,((WindowPeek)gMainWindow)->updateRgn);
    maximum = max(0L, gContentHeight - GetContentViewportHeight());
    old = gContentScroll; gContentScroll = max(0L, min(offset, maximum));
    delta = old - gContentScroll;
    if (!delta) {
        if (stale) RedrawReaderForScroll();
        UpdateScrollBars(); SetPort(oldPort); return;
    }
    damage = NewRgn(); saved = NewRgn();
    if (!stale && damage && saved && labs(delta) < GetContentViewportHeight() &&
        FrontWindow() == gMainWindow && !gInBackground) {
        GetClip(saved); ClipRect(&gContentRect);
        ScrollRect(&gContentRect, 0, (short)delta, damage);
        gReaderDamage = damage; DrawContentPane(); gReaderDamage = NULL;
        SetClip(saved);
    } else RedrawReaderForScroll();
    if (damage) DisposeRgn(damage); if (saved) DisposeRgn(saved);
    UpdateScrollBars(); SetPort(oldPort);
}

/*
 * InitScrollBars — Initialize the native pane scroll bars
 */
void InitScrollBars(void)
{
    static const unsigned char emptyTitle[] = { 0 };
    if (gMainWindow == NULL) return;
    gListScrollBar = NewControl(gMainWindow, &gListScrollRect,
                                emptyTitle, true, 0, 0, 0, scrollBarProc, 0);
    gContentScrollBar = NewControl(gMainWindow, &gContentScrollRect,
                                   emptyTitle, true, 0, 0, 0, scrollBarProc, 0);
    /* Horizontal scrolling exposes fixed-size photos in a narrower viewport. */
    gContentHScrollBar = NewControl(gMainWindow, &gContentHScrollRect,
                                   emptyTitle, true, 0, 0, 0, scrollBarProc, 0);
    UpdateScrollBars();
}

static long ContentScrollUnit(void)
{
    long maximum = max(0L, gContentHeight - GetContentViewportHeight());
    return maximum > 32767 ? maximum / 32767 + 1 : 1;
}

static void UpdateScrollControl(ControlHandle control, short maximum, short value)
{
    Boolean changed = false;
    if (!control) return;
    if (GetControlMaximum(control) != maximum) {
        SetControlMaximum(control, maximum); changed = true;
    }
    if (GetControlValue(control) != value) {
        SetControlValue(control, value); changed = true;
    }
    /* A pane-only BeginUpdate may clip the Control Manager's immediate draw.
       Queue the control itself so its thumb is repainted after EndUpdate. */
    if (changed) InvalRect(&(**control).contrlRect);
}

static void UpdateListScrollBar(void)
{
    short maximum=max(0,gMaxArticles-GetVisibleListRows());
    gScrollOffset=max(0,min(gScrollOffset,maximum));
    UpdateScrollControl(gListScrollBar,maximum,gScrollOffset);
}

void UpdateScrollBars(void)
{
    if (gIndexLoading) return;
    short listMaximum = max(0, gMaxArticles - GetVisibleListRows());
    long maximum, unit;
    short controlMaximum, value;
    GrafPtr oldPort;
    if (!gMainWindow) return;
    GetPort(&oldPort); SetPort(gMainWindow);
    EnsureReaderLayout();
    maximum = max(0L, gContentHeight - GetContentViewportHeight());
    unit = ContentScrollUnit();
    controlMaximum = (maximum + unit - 1) / unit;
    gScrollOffset = max(0, min(gScrollOffset, listMaximum));
    gContentScroll = max(0L, min(gContentScroll, maximum));
    value = gContentScroll == maximum ? controlMaximum : gContentScroll / unit;
    UpdateScrollControl(gListScrollBar, listMaximum, gScrollOffset);
    UpdateScrollControl(gContentScrollBar, controlMaximum, value);
    UpdateScrollControl(gContentHScrollBar, max(0, gContentWidth -
        (gContentRect.right - gContentRect.left)), gContentHorizontalScroll);
    SetPort(oldPort);
}

void ScrollReaderHorizontallyTo(short offset)
{
    short previous, delta;
    GrafPtr oldPort;
    RgnHandle damage, saved;
    Boolean stale;
    if (!gMainWindow || gReaderArticleLoading) return;
    stale=gReaderLayoutDirty;
    GetPort(&oldPort); SetPort(gMainWindow);
    EnsureReaderLayout();
    if (UserActionPending()) { SetPort(oldPort); return; }
    stale=stale || RectInRgn(&gContentRect,((WindowPeek)gMainWindow)->updateRgn);
    previous = gContentHorizontalScroll;
    gContentHorizontalScroll = max(0, min(offset,
        gContentWidth - (gContentRect.right - gContentRect.left)));
    delta = previous - gContentHorizontalScroll;
    if (!delta) {
        if (stale) RedrawReaderForScroll();
        UpdateScrollBars(); SetPort(oldPort); return;
    }
    damage = NewRgn(); saved = NewRgn();
    if (!stale && damage && saved && abs(delta) < gContentRect.right - gContentRect.left &&
        FrontWindow() == gMainWindow && !gInBackground) {
        GetClip(saved); ClipRect(&gContentRect);
        ScrollRect(&gContentRect, delta, 0, damage);
        gReaderDamage = damage; DrawContentPane(); gReaderDamage = NULL;
        SetClip(saved);
    } else RedrawReaderForScroll();
    if (damage) DisposeRgn(damage); if (saved) DisposeRgn(saved);
    UpdateScrollBars(); SetPort(oldPort);
}

static pascal void HorizontalScrollAction(ControlHandle control, short part)
{
    short amount = (part == inUpButton || part == inDownButton) ? 16 :
        max(16, gContentRect.right - gContentRect.left - 16);
    (void)control;
    if (part == inUpButton || part == inPageUp) amount = -amount;
    ScrollReaderHorizontallyTo(gContentHorizontalScroll + amount);
}

static pascal void ContentScrollAction(ControlHandle control, short part)
{
    long amount = (part == inUpButton || part == inDownButton) ?
                  GetBodyLineHeight() : GetContentViewportHeight();
    (void)control;
    if (part == inUpButton || part == inPageUp) amount = -amount;
    ScrollReaderTo(gContentScroll + amount);
}

static pascal void ListScrollAction(ControlHandle control, short part)
{
    short amount = (part == inUpButton || part == inDownButton) ? 1 : GetVisibleListRows();
    (void)control;
    if (part == inUpButton || part == inPageUp) amount = -amount;
    ScrollListTo(gScrollOffset + amount);
}

Boolean HandleScrollBarClick(Point where)
{
    ControlHandle control = NULL;
    short part = FindControl(where, gMainWindow, &control);
    short value;
    if (part == 0 || control == NULL) return false;
    if (control == gContentHScrollBar) {
        if (part == inThumb) {
            TrackControl(control, where, NULL);
            ScrollReaderHorizontallyTo(GetControlValue(control));
        } else TrackControl(control, where, NewControlActionUPP(HorizontalScrollAction));
        return true;
    }
    if (control == gContentScrollBar) {
        if (part == inThumb) {
            TrackControl(control, where, NULL);
            value = GetControlValue(control);
            ScrollReaderTo(value == GetControlMaximum(control) ?
                max(0L, gContentHeight - GetContentViewportHeight()) : value * ContentScrollUnit());
        } else TrackControl(control, where, NewControlActionUPP(ContentScrollAction));
        return true;
    }
    if (control == gListScrollBar) {
        if (part == inThumb) {
            TrackControl(control, where, NULL);
            ScrollListTo(GetControlValue(control));
        } else TrackControl(control, where, NewControlActionUPP(ListScrollAction));
        return true;
    }
    UpdateScrollBars();
    return true;
}

#ifdef TICKERTAPE_UI_SMOKE
Boolean UISmokeScrollArrowsAligned(void)
{
    return (**gListScrollBar).contrlRect.bottom == (**gContentScrollBar).contrlRect.bottom;
}
Boolean UISmokePartialRowHasInk(void)
{
    short x, y, top = gListRect.top + GetVisibleListRows() * GetListRowHeight();
    Point origin = {0, 0}; unsigned char *row;
    if (gScrollOffset + GetVisibleListRows() >= gMaxArticles || top >= gListRect.bottom)
        return false;
    SetPort(gMainWindow); LocalToGlobal(&origin);
    for (y = top; y < gListRect.bottom; ++y) {
        row = (unsigned char *)qd.screenBits.baseAddr +
              (long)(y + origin.v - qd.screenBits.bounds.top) * (qd.screenBits.rowBytes & 0x3FFF);
        for (x = gListRect.left + 4; x < gListRect.right - 16; ++x) {
            short bit = x + origin.h - qd.screenBits.bounds.left;
            if (row[bit / 8] & (0x80 >> (bit % 8))) return true;
        }
    }
    return false;
}

Boolean UISmokeReaderBlank(void)
{
    short x, y;
    Point origin = {0, 0};
    unsigned char *row;
    SetPort(gMainWindow); LocalToGlobal(&origin);
    for (y = gContentRect.top; y < gContentRect.bottom; ++y) {
        row = (unsigned char *)qd.screenBits.baseAddr +
              (long)(y + origin.v - qd.screenBits.bounds.top) *
              (qd.screenBits.rowBytes & 0x3FFF);
        for (x = gContentRect.left; x < gContentRect.right; ++x) {
            short bit = x + origin.h - qd.screenBits.bounds.left;
            if (row[bit / 8] & (0x80 >> (bit % 8))) return false;
        }
    }
    return true;
}
short UISmokeReaderScrollValue(void) { return GetControlValue(gContentScrollBar); }
short UISmokeHorizontalMaximum(void) { return GetControlMaximum(gContentHScrollBar); }
short UISmokeHorizontalValue(void) { return GetControlValue(gContentHScrollBar); }
void UISmokeHorizontalArrow(short part) { HorizontalScrollAction(gContentHScrollBar, part); }
void UISmokeListArrow(short part) { ListScrollAction(gListScrollBar, part); }
Boolean UISmokeListRangeChangedInPaneUpdate(void)
{
    SetPort(gMainWindow);
    SetControlMaximum(gListScrollBar, 0); SetControlValue(gListScrollBar, 0);
    Draw1Control(gListScrollBar);
    InvalRect(&gListRect); BeginUpdate(gMainWindow);
    UpdateScrollBars(); EndUpdate(gMainWindow);
    return RectInRgn(&gListScrollRect, ((WindowPeek)gMainWindow)->updateRgn);
}
Boolean UISmokeListThumbVisible(void)
{
    short x, y; Boolean black;
    Rect rect = (**gListScrollBar).contrlRect;
    SetPort(gMainWindow);
    for (y = rect.top + 18; y < rect.top + 35; ++y) {
        black = true;
        for (x = rect.left + 2; x < rect.right - 2; ++x)
            if (!GetPixel(x, y)) { black = false; break; }
        if (black) return true;
    }
    return false;
}
Boolean UISmokeListHeaderDividerVisible(void)
{
    short y;
    SetPort(gMainWindow);
    for (y=gListHeaderRect.top; y<gListScrollRect.top; ++y)
        if (!GetPixel(gListHeaderRect.right-1,y)) return false;
    return true;
}
Boolean UISmokeListThumbPaintedWithList(void)
{
    Boolean visible;
    SetPort(gMainWindow);
    EraseRect(&gListScrollRect);
    UpdateArticleList(); BeginUpdate(gMainWindow);
    DrawListPane();
    visible=UISmokeListThumbVisible();
    EndUpdate(gMainWindow);
    return visible;
}
Boolean UISmokeWrappedLinesFit(void)
{
    short i, row, end;
    EnsureReaderLayout();
    for (i = 0; i < gSectionCount; ++i) {
        ApplyReaderSectionStyle(&gSections[i]);
        if (gSections[i].block & (WIRE_STYLE_PRE|WIRE_STYLE_TABLE)) continue;
        for (row = 0; row < gSections[i].count; ++row) {
            BodyLine *line = &gSections[i].lines[row];
            end = line->end;
            while (end > line->start && gSections[i].text[end - 1] == ' ') --end;
            if (end - line->start > 1 &&
                ReaderTextWidth(gSections[i].text, line->start, end - line->start) > gReaderLayoutWidth-gSections[i].indent)
                return false;
        }
    }
    return true;
}
short UISmokeTableRows(void)
{
    short i, row, count=0; EnsureReaderLayout();
    for (i=0; i<gSectionCount; ++i) if (gSections[i].block & WIRE_STYLE_TABLE)
        for (row=0; row<gSections[i].count; ++row)
            if (gSections[i].lines[row].end>gSections[i].lines[row].start) ++count;
    return count;
}
Boolean UISmokeHeadingVisible(void)
{
    short i; EnsureReaderLayout();
    for (i=0; i<gSectionCount; ++i) if (gSections[i].heading && gSections[i].text[0]!='#') return true;
    return false;
}
Point UISmokeCornerPoint(void)
{
    Point point;
    point.h = gListScrollRect.left + 3;
    point.v = gListScrollRect.bottom + 3;
    return point;
}
short UISmokeListScrollValue(void) { return GetControlValue(gListScrollBar); }
short UISmokeReaderHit(Point where) { SetPort(gMainWindow); return ReaderOffsetAtPoint(where); }
void UISmokeActivateFootnote(short bodyOffset) { PrepareReaderText(); ActivateFootnote(gBodyOffset+bodyOffset); }
short UISmokeImageWidth(void) { EnsureReaderLayout(); return gImageWidth[0]; }
short UISmokeImageHeight(void) { EnsureReaderLayout(); return gImageHeight[0]; }
const char *UISmokeReaderText(void) { PrepareReaderText(); return gReaderText; }
long UISmokeFeedTop(void) { EnsureReaderLayout(); return gSections[0].top; }
long UISmokeBodyTop(void) {
    short i; EnsureReaderLayout();
    for (i = 3; i < gSectionCount; ++i) if (gSections[i].count) return gSections[i].top;
    return gSections[gSectionCount - 1].top;
}
long UISmokeImageAt(short index) { EnsureReaderLayout(); return gImageTop[index]; }
long UISmokeImageTop(void) { EnsureReaderLayout(); return gImageTop[0]; }
#endif

/*
 * UpdateArticleList — Redraw the article list
 */
void UpdateArticleList(void)
{
    if (gIndexLoading) return;
    Rect background = gListRect;
    Rect header = gListHeaderRect;
    if (!gMainWindow) return;
    gListPainted=false; /* Inversion requires rows from the current complete list. */
    background.left = gMainWindow->portRect.left;
    background.right = gListScrollRect.right;
    background.top = gMainWindow->portRect.top;
    InvalidateRect(&background);
    header.bottom += 2;
    InvalidateRect(&header);
}

/* Metadata changes need only the visible row; idle prefetch must not erase
   the entire list. The unread dot has its own smaller damage rectangle. */
static void UpdateArticleListPart(short index, Boolean indicatorOnly)
{
    Rect damage;
    short top, rowHeight;
    if (gIndexLoading || !gMainWindow || index < gScrollOffset ||
        index < 0 || index >= gMaxArticles) return;
    rowHeight=GetListRowHeight();
    if ((long)(index-gScrollOffset)*rowHeight >= gListRect.bottom-gListRect.top) return;
    top=gListRect.top+(index-gScrollOffset)*rowHeight;
    if (indicatorOnly)
        SetRect(&damage,gListRect.right-11,top+4,gListRect.right-5,top+10);
    else
        SetRect(&damage,gListRect.left,top,gListRect.right,min(gListRect.bottom,top+rowHeight));
    InvalidateRect(&damage);
}

void UpdateArticleRow(short index) { UpdateArticleListPart(index,false); }
void UpdateArticleReadIndicator(short index) { UpdateArticleListPart(index,true); }

/*
 * UpdateContentPane — Redraw the article content pane
 */
void UpdateContentPane(void)
{
    if (gIndexLoading) return;
    InvalidateRect(&gContentRect);
}

void UpdateStatusLine(void)
{
    if (gIndexLoading) return;
    GrafPtr oldPort;
    Rect status;
    if (gMainWindow == NULL) return;
    GetPort(&oldPort);
    SetPort(gMainWindow);
    status = gMainWindow->portRect;
    status.top = status.bottom - 15;
    status.right = gListScrollRect.left;
    InvalRect(&status);
    SetPort(oldPort);
}

/*
 * DrawStatusLine — Draw the status bar at the bottom
 */
void DrawStatusLine(void)
{
    GrafPtr oldPort;
    Str256 statusStr;
    char statusText[256];
    Rect statusRect;
    if (gMainWindow == NULL) return;

    GetPort(&oldPort);
    SetPort(gMainWindow);
    ClipRect(&gMainWindow->portRect);

    statusRect = gMainWindow->portRect;
    /* The corner beneath the list's down arrow is outside both panes/bars. */
    statusRect.left = gListScrollRect.left;
    statusRect.right = gContentHScrollRect.left;
    statusRect.top = gListScrollRect.bottom;
    EraseRect(&statusRect);
    statusRect = gMainWindow->portRect;
    statusRect.top = statusRect.bottom - 15;
    statusRect.right = gListScrollRect.left;
    EraseRect(&statusRect);
    MoveTo(statusRect.left, statusRect.top);
    LineTo(statusRect.right, statusRect.top);

    // Show status message
    GetStatusMessage(statusStr);
    BlockMove(statusStr + 1, statusText, statusStr[0]);
    statusText[statusStr[0]] = '\0';
    qd.thePort->txFont = monaco;
    qd.thePort->txSize = 9;
    Normal;
    statusRect.left += 6;
    statusRect.right -= 4;
    if (gHoverURL[0]) FormatLinkDestination(gHoverURL,statusText,sizeof(statusText),
        max(1,(statusRect.right-statusRect.left)/max(1,CharWidth('M'))));
    DrawFittedString(statusText, &statusRect, statusRect.top + 12);
    ClipRect(&gMainWindow->portRect);
    /* Native bars already supply their outlines. Draw only the size box. */
    statusRect = gMainWindow->portRect;
    statusRect.left = statusRect.right - 15;
    statusRect.top = statusRect.bottom - 15;
    ClipRect(&statusRect);
    DrawGrowIcon(gMainWindow);
    ClipRect(&gMainWindow->portRect);

    SetPort(oldPort);
}
