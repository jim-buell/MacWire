#ifndef MACWIRE_READER_UNDERLINE_H
#define MACWIRE_READER_UNDERLINE_H
/* One bit per pixel: draw where no glyph is within one pixel of the rule. */
int ReaderUnderlineMask(const unsigned char *bits, int rowBytes, int width,
                        int height, int y, unsigned char *mask, int capacity);
#endif
