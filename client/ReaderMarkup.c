#include "ReaderMarkup.h"
short ReaderHeadingPrefix(const char *line)
{
    short count=0;
    if (!line) return 0;
    while (line[count]=='#' && count<6) ++count;
    if (!count || line[count]!=' ' || !line[count+1] ||
        line[count+1]=='\r' || line[count+1]=='\n') return 0;
    return count+1;
}
