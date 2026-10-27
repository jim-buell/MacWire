#include "LinkDisplay.h"
#include <string.h>

void FormatLinkDestination(const char *url, char *out, short capacity, short columns)
{
    const char *start, *end, *path;
    short length, host, keep, front;
    if (capacity <= 0) return;
    out[0] = 0;
    if (!url || columns <= 0) return;
    if (columns >= capacity) columns = capacity-1;
    start = strstr(url, "://"); start = start ? start+3 : url;
    end = start + strcspn(start, "?#");
    path = memchr(start, '/', end-start);
    if (path && path+1 == end) end = path;
    length = end-start;
    host = path && path < end ? path-start : length;
    if (length <= columns) {
        memcpy(out,start,length); out[length]=0; return;
    }
    if (columns <= 3) { memset(out,'.',columns); out[columns]=0; return; }
    if (host+4 < columns) {
        /* Keep the entire domain, an omitted-path marker, and the useful tail. */
        keep=columns-host-4;
        memcpy(out,start,host); memcpy(out+host,"/...",4);
        memcpy(out+host+4,end-keep,keep);
    } else if (host <= columns-3) {
        memcpy(out,start,host); memcpy(out+host,"...",3);
        out[host+3]=0; return;
    } else {
        /* A very long hostname still retains its ending (including the TLD). */
        keep=(columns-3)/2; front=columns-3-keep;
        memcpy(out,start,front); memcpy(out+front,"...",3);
        memcpy(out+front+3,start+host-keep,keep);
    }
    out[columns]=0;
}
