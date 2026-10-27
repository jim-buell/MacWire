#ifndef MACWIRE_READER_VIEWPORT_H
#define MACWIRE_READER_VIEWPORT_H
long ReaderScrollAfterImage(long scroll, long imageTop, long oldHeight,
                           long newHeight, long maximum);
long ReaderScrollForImageAnchor(long scroll, long anchor, long imageTop,
                               long oldHeight, long newHeight, long maximum);
short ReaderNextImage(const long *tops, const short *heights,
                      const unsigned char *states, short count,
                      long scroll, long viewportHeight);
#endif
