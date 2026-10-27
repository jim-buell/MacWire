#include "Footnotes.h"
#include <string.h>
#include <stdlib.h>

short BuildFootnoteLinks(const char *body, FootnoteLink *links, short capacity)
{
    short ids[64], targets[64], definitions=0, count=0, i, j;
    long pos, number, length;
    char *end;
    if (!body || !links || capacity<=0) return 0;
    length=strlen(body);
    if (length>30000) return 0;
    for (pos=0; pos<length && definitions<64; ++pos) {
        if (body[pos]!='[' || (pos && body[pos-1]!='\r' && body[pos-1]!='\n')) continue;
        number=strtol(body+pos+1,&end,10);
        if (number<1 || number>999 || end==body+pos+1 || end[0]!=']' || end[1]!=':') continue;
        for (i=0; i<definitions; ++i) if (ids[i]==number) break;
        if (i==definitions) { ids[i]=number; targets[i]=pos; ++definitions; }
    }
    for (pos=0; pos<length && count<capacity; ++pos) {
        short target=-1;
        const char *digits;
        int back;
        if (body[pos]!='[') continue;
        back=!strncmp(body+pos,"[Back to ",9);
        digits=body+pos+(back ? 9 : 1);
        number=strtol(digits,&end,10);
        if (end==digits || number<1 || number>999 || *end!=']' || (!back && end[1]==':')) continue;
        for (i=0; i<definitions; ++i) if (ids[i]==number) break;
        if (i==definitions) continue;
        if (back) {
            for (j=0; j<count; ++j)
                if (links[j].target==targets[i]) { target=links[j].start; break; }
        } else target=targets[i];
        if (target<0) continue;
        links[count].start=pos; links[count].length=end-body-pos+1;
        links[count].target=target; ++count;
        pos=end-body;
    }
    return count;
}
