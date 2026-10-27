#include "ReaderFont.h"
#include <Fonts.h>

enum { kNewYorkFamily = 2 };

static Boolean BeginReaderFont(void)
{
    Boolean saved=GetOutlinePreferred();
    /* Keep the traditional built-in screen fonts at their bitmap sizes.
       Families such as Palatino and Times have designed outline faces. */
    Boolean preferred=qd.thePort->txFont>12;
    if (saved!=preferred) SetOutlinePreferred(preferred);
    return saved;
}

static void EndReaderFont(Boolean saved)
{
    if (GetOutlinePreferred()!=saved) SetOutlinePreferred(saved);
}

short ReaderFontWidth(const char *text, short start, short length)
{
    Boolean saved=BeginReaderFont();
    short width=TextWidth((Ptr)text,start,length);
    EndReaderFont(saved);
    return width;
}

short ReaderFontCharWidth(unsigned char character)
{
    Boolean saved=BeginReaderFont();
    short width=CharWidth((CharParameter)character);
    EndReaderFont(saved);
    return width;
}

void ReaderFontInfo(FontInfo *info)
{
    Boolean saved=BeginReaderFont();
    GetFontInfo(info);
    EndReaderFont(saved);
}

void ReaderFontDraw(const char *text, short start, short length)
{
    Boolean saved=BeginReaderFont();
    DrawText((Ptr)text,start,length);
    EndReaderFont(saved);
}

short ReaderItalicClearance(unsigned char following)
{
    Boolean saved;
    FMInput input;
    FMOutPtr output;
    short clearance=1;
    /* Low punctuation does not need the full allowance for the slanted top
       of New York's bitmap glyphs. Keep a single pixel of separation. */
    if (qd.thePort->txFont==kNewYorkFamily && (following==',' || following=='.'))
        return 1;
    saved=BeginReaderFont();
    input.family=qd.thePort->txFont; input.size=qd.thePort->txSize;
    input.face=qd.thePort->txFace; input.needBits=0; input.device=0;
    input.numer.h=input.numer.v=input.denom.h=input.denom.v=1;
    output=FMSwapFont(&input);
    /* QuickDraw's synthetic slant extends beyond the advance width. */
    if (output && output->italic) {
        clearance=(qd.thePort->txSize+3)/4;
        if (clearance<1) clearance=1;
    }
    EndReaderFont(saved);
    return clearance;
}

/* Table heading rules sit below the entire face. Link rules skip glyph ink. */
short ReaderUnderlineOffset(const FontInfo *info)
{
    return info->descent > 0 ? info->descent + 1 : 1;
}

short ReaderScriptRise(const FontInfo *base, Boolean superscript)
{
    short distance=superscript ? (base->ascent+1)/3 : (base->descent+1)/2;
    if (distance<1) distance=1;
    return superscript ? -distance : distance;
}
