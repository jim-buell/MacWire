#ifndef RSS_WIRE_H
#define RSS_WIRE_H

#include <stddef.h>
#include <stdint.h>

/* Maximum entries per transfer packet, not per article list. */
#define WIRE_MAX_ARTICLES 50
#define WIRE_MAX_BODY 10000
#define WIRE_MAX_TITLE 512
#define WIRE_MAX_FEEDS 64
#define WIRE_MAX_AUTHOR 256
#define WIRE_MAX_LINKS 48
#define WIRE_LINK_URL_BYTES 8192
#define WIRE_MAX_STYLES 256
#define WIRE_STYLE_BOLD 1
#define WIRE_STYLE_ITALIC 2
#define WIRE_STYLE_CODE 4
#define WIRE_STYLE_PRE 8
#define WIRE_STYLE_TABLE 16
#define WIRE_STYLE_QUOTE 32
#define WIRE_STYLE_SUP 64
#define WIRE_STYLE_SUB 128
#define WIRE_STYLE_STRIKE 256
typedef struct { unsigned short start, length, flags; } WireStyle;
typedef struct { unsigned short start, length, urlOffset; } WireLink;
typedef struct {
    unsigned short count, urlBytes;
    WireLink entries[WIRE_MAX_LINKS];
    unsigned short styleCount;
    WireStyle styles[WIRE_MAX_STYLES];
    char urls[WIRE_LINK_URL_BYTES];
} WireLinks;

typedef struct {
    char id[9];
    char feed[33];
    char title[101];
    char summary[201];
    double published;
    unsigned short imageCount;
    unsigned char fullContentAvailable;
} WireIndexEntry;

typedef struct {
    char id[9];
    char feed[33];
    char title[WIRE_MAX_TITLE];
    char body[WIRE_MAX_BODY];
    char author[WIRE_MAX_AUTHOR];
    double published;
    unsigned short imageCount;
    unsigned short imageOffsets[5];
    WireLinks links;
} WireArticle;

typedef struct { char id[9], time[24], date[32]; } WireDateEntry;
int WireDecodeDates(const unsigned char *packet, size_t length,
                    WireDateEntry *entries, short *count);

typedef struct {
    char id[9];
    unsigned short index, width, height, rowBytes;
} WireImage;
int WireDecodeImage(const unsigned char *packet, size_t length, WireImage *image,
                    unsigned char *bitmap, size_t capacity);
int WireDecodeFeeds(const unsigned char *packet, size_t length,
                    char names[WIRE_MAX_FEEDS][33], short *count);

typedef struct {
    char view[9];
    uint32_t total, offset;
    size_t count;
} WirePage;
int WireDecodePage(const unsigned char *packet, size_t length,
                   WireIndexEntry *entries, size_t capacity, WirePage *page);
int WireComparePublication(double a, const char *aID, double b, const char *bID,
                           int newestFirst);

/* Return 0 only for a complete, valid packet of the expected command. */
int WireDecodeIndex(const unsigned char *packet, size_t length,
                    WireIndexEntry *entries, size_t capacity, size_t *count);
int WireDecodeArticle(const unsigned char *packet, size_t length,
                      WireArticle *article);

typedef struct { char job[9]; unsigned char running; unsigned short failures; } WireRefresh;
int WireDecodeRefresh(const unsigned char *packet, size_t length, WireRefresh *refresh);

#endif
