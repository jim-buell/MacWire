#ifndef MACWIRE_LINK_DISPLAY_H
#define MACWIRE_LINK_DISPLAY_H
/* Fit a destination to the status line's available monospaced characters. */
void FormatLinkDestination(const char *url, char *out, short capacity, short columns);
#endif
