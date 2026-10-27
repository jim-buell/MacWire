#include "ReaderUnderline.h"
#include <string.h>

int ReaderUnderlineMask(const unsigned char *bits, int rowBytes, int width,
                        int height, int y, unsigned char *mask, int capacity)
{
    int x, dx, dy, bytes=(width+7)/8;
    if (!bits || !mask || width<=0 || height<=0 || rowBytes<bytes ||
        bytes>capacity || y<0 || y>=height) return 0;
    memset(mask,0,bytes);
    for (x=0; x<width; ++x) {
        int clear=1;
        for (dy=-1; dy<=1 && clear; ++dy) {
            int row=y+dy;
            if (row<0 || row>=height) continue;
            for (dx=-1; dx<=1; ++dx) {
                int column=x+dx;
                if (column<0 || column>=width) continue;
                if (bits[(long)row*rowBytes+column/8] & (0x80>>(column&7))) {
                    clear=0; break;
                }
            }
        }
        if (clear) mask[x/8]|=0x80>>(x&7);
    }
    return 1;
}
