#include "ReaderViewport.h"
#include <limits.h>

long ReaderScrollAfterImage(long scroll, long imageTop, long oldHeight,
                           long newHeight, long maximum)
{
    return ReaderScrollForImageAnchor(scroll,scroll,imageTop,oldHeight,newHeight,maximum);
}

long ReaderScrollForImageAnchor(long scroll, long anchor, long imageTop,
                               long oldHeight, long newHeight, long maximum)
{
    /* Anchor to visible text, including text just below a placeholder at the
       viewport top. A viewport containing only the image uses its old scroll. */
    if (imageTop+oldHeight<=anchor) scroll+=newHeight-oldHeight;
    if (maximum<0) maximum=0;
    if (scroll<0) scroll=0;
    if (scroll>maximum) scroll=maximum;
    return scroll;
}

short ReaderNextImage(const long *tops, const short *heights,
                      const unsigned char *states, short count,
                      long scroll, long viewportHeight)
{
    short i, next=-1;
    long nearest=LONG_MAX, bottom=scroll+viewportHeight;
    if (viewportHeight<=0) return -1;
    for (i=0; i<count; ++i) {
        long distance=0, end=tops[i]+heights[i];
        if (states[i]!=0) continue;
        if (end<=scroll) distance=scroll-end+1;
        else if (tops[i]>=bottom) distance=tops[i]-bottom+1;
        if (distance<nearest) { nearest=distance; next=i; }
    }
    return next;
}
