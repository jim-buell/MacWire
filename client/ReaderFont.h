#ifndef MACWIRE_READER_FONT_H
#define MACWIRE_READER_FONT_H
#include <QuickDraw.h>
/* Each operation restores the application's Font Manager preference. */
short ReaderFontWidth(const char *text, short start, short length);
short ReaderFontCharWidth(unsigned char character);
void ReaderFontInfo(FontInfo *info);
void ReaderFontDraw(const char *text, short start, short length);
short ReaderItalicClearance(unsigned char following);
short ReaderUnderlineOffset(const FontInfo *info);
short ReaderScriptRise(const FontInfo *base, Boolean superscript);
#endif
