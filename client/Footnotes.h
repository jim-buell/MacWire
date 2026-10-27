#ifndef MACWIRE_FOOTNOTES_H
#define MACWIRE_FOOTNOTES_H
#define kMaxFootnoteLinks 128
typedef struct { short start, length, target; } FootnoteLink;
/* Offsets refer to the plain-text body; only references with a definition link. */
short BuildFootnoteLinks(const char *body, FootnoteLink *links, short capacity);
#endif
