#include "Wire.h"
#include <string.h>

#define CMD_INDEX 0x02
#define CMD_ARTICLE 0x03

typedef struct {
    const unsigned char *data;
    size_t pos;
    size_t end;
} Reader;

static int take(Reader *r, size_t length, const unsigned char **out)
{
    if (length > r->end - r->pos) return -1;
    *out = r->data + r->pos;
    r->pos += length;
    return 0;
}

static int word(Reader *r, unsigned short *out)
{
    const unsigned char *p;
    if (take(r, 2, &p)) return -1;
    *out = (unsigned short)(((unsigned short)p[0] << 8) | p[1]);
    return 0;
}

static int text_field(Reader *r, char *out, size_t capacity)
{
    unsigned short length;
    const unsigned char *p;
    size_t copy;
    if (word(r, &length) || take(r, length, &p)) return -1;
    copy = length < capacity ? length : capacity - 1;
    memcpy(out, p, copy);
    out[copy] = '\0';
    return 0;
}

static int article_id(Reader *r, char id[9])
{
    const unsigned char *p;
    if (take(r, 9, &p) || p[8] != 0) return -1;
    memcpy(id, p, 8);
    id[8] = '\0';
    return 0;
}

static int timestamp(Reader *r, double *out)
{
    const unsigned char *p;
    unsigned char *dest = (unsigned char *)out;
    const unsigned short endianProbe = 1;
    int i;
    if (sizeof(double) != 8 || take(r, 8, &p)) return -1;
    if (*(const unsigned char *)&endianProbe) {
        for (i = 0; i < 8; ++i) dest[i] = p[7 - i];
    } else {
        memcpy(dest, p, 8);
    }
    return 0;
}

static int packet_reader(const unsigned char *packet, size_t length,
                         unsigned char command, Reader *r)
{
    size_t payloadLength;
    if (!packet || length < 3 || packet[0] != command) return -1;
    payloadLength = ((size_t)packet[1] << 8) | packet[2];
    if (length != payloadLength + 3) return -1;
    r->data = packet;
    r->pos = 3;
    r->end = length;
    return 0;
}

static int decode_entries(Reader *r, WireIndexEntry *entries, size_t capacity, size_t *count)
{
    unsigned short n, i;
    const unsigned char *flag;
    if (!entries || !count || word(r, &n) || n > capacity || n > WIRE_MAX_ARTICLES) return -1;
    for (i = 0; i < n; ++i) {
        WireIndexEntry *entry = &entries[i];
        if (article_id(r, entry->id)
            || timestamp(r, &entry->published)
            || text_field(r, entry->feed, sizeof(entry->feed))
            || text_field(r, entry->title, sizeof(entry->title))
            || text_field(r, entry->summary, sizeof(entry->summary))
            || take(r, 1, &flag)
            || word(r, &entry->imageCount)) return -1;
        entry->fullContentAvailable = *flag != 0;
    }
    if (r->pos != r->end) return -1;
    *count = n;
    return 0;
}

int WireDecodeIndex(const unsigned char *packet, size_t length,
                    WireIndexEntry *entries, size_t capacity, size_t *count)
{
    Reader r;
    if (packet_reader(packet, length, CMD_INDEX, &r)) return -1;
    return decode_entries(&r, entries, capacity, count);
}

int WireDecodePage(const unsigned char *packet, size_t length,
                   WireIndexEntry *entries, size_t capacity, WirePage *page)
{
    Reader r;
    const unsigned char *p;
    WirePage result;
    unsigned short i;
    if (!page || packet_reader(packet, length, 0x0A, &r) || take(&r, 16, &p)) return -1;
    for (i = 0; i < 8; ++i)
        if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f'))) return -1;
    memcpy(result.view, p, 8); result.view[8] = 0;
    result.total = ((uint32_t)p[8] << 24) | ((uint32_t)p[9] << 16) | ((uint32_t)p[10] << 8) | p[11];
    result.offset = ((uint32_t)p[12] << 24) | ((uint32_t)p[13] << 16) | ((uint32_t)p[14] << 8) | p[15];
    if (decode_entries(&r, entries, capacity, &result.count) || result.offset > result.total ||
        result.count > result.total - result.offset ||
        (!result.count && result.offset < result.total)) return -1;
    *page = result;
    return 0;
}

int WireComparePublication(double a, const char *aID, double b, const char *bID,
                           int newestFirst)
{
    int order = a < b ? -1 : a > b ? 1 : strcmp(aID, bID);
    return newestFirst ? -order : order;
}

int WireDecodeArticle(const unsigned char *packet, size_t length,
                      WireArticle *article)
{
    Reader r;
    unsigned short images, i, fieldLength;
    const unsigned char *unused;
    if (!article || packet_reader(packet, length, CMD_ARTICLE, &r)
        || article_id(&r, article->id)
        || text_field(&r, article->title, sizeof(article->title))
        || text_field(&r, article->author, sizeof(article->author))
        || timestamp(&r, &article->published)
        || text_field(&r, article->feed, sizeof(article->feed))
        || text_field(&r, article->body, sizeof(article->body))
        || word(&r, &images)) return -1;
    for (i = 0; i < images; ++i) {
        if (word(&r, &fieldLength) || take(&r, fieldLength, &unused)
            || take(&r, 8, &unused)
            || word(&r, &fieldLength) || take(&r, fieldLength, &unused)) return -1;
    }
    for (i = 0; i < 5; ++i) article->imageOffsets[i] = 0xFFFF;
    if (r.end-r.pos >= 4 && !memcmp(r.data+r.pos, "IPOS", 4)) {
        if (images > 5 || take(&r, 4, &unused)) return -1;
        for (i = 0; i < images; ++i) {
            if (word(&r, &article->imageOffsets[i]) ||
                (article->imageOffsets[i] != 0xFFFF && article->imageOffsets[i] > strlen(article->body))) return -1;
        }
    }
    article->links.count = article->links.urlBytes = article->links.styleCount = 0;
    if (r.end-r.pos >= 4 && !memcmp(r.data+r.pos, "LINK", 4)) {
        unsigned short n, bytes, start, span;
        if (take(&r, 4, &unused) || memcmp(unused, "LINK", 4) || word(&r, &n) || n > WIRE_MAX_LINKS) return -1;
        for (i = 0; i < n; ++i) {
            WireLink *link = &article->links.entries[i];
            if (word(&r, &start) || word(&r, &span) || word(&r, &bytes) || !span ||
                (unsigned long)start + span > strlen(article->body) || !bytes || bytes > 512 ||
                article->links.urlBytes + bytes + 1 > WIRE_LINK_URL_BYTES || take(&r, bytes, &unused)) return -1;
            if (memchr(unused, 0, bytes) ||
                !((bytes >= 7 && !memcmp(unused,"http://",7)) ||
                  (bytes >= 8 && !memcmp(unused,"https://",8)))) return -1;
            link->start=start; link->length=span; link->urlOffset=article->links.urlBytes;
            memcpy(article->links.urls+link->urlOffset, unused, bytes);
            article->links.urls[link->urlOffset+bytes]=0;
            article->links.urlBytes += bytes+1;
        }
        article->links.count = n;
    }
    if (r.end-r.pos >= 4 && !memcmp(r.data+r.pos, "STYL", 4)) {
        unsigned short count;
        if (take(&r, 4, &unused) || word(&r, &count) || count > WIRE_MAX_STYLES) return -1;
        for (i=0; i<count; ++i) {
            WireStyle *style=&article->links.styles[i];
            if (word(&r, &style->start) || word(&r, &style->length) || word(&r, &style->flags) ||
                !style->length || (unsigned long)style->start+style->length > strlen(article->body) ||
                !style->flags || (style->flags & ~511)) return -1;
        }
        article->links.styleCount=count;
    }
    if (r.pos != r.end) return -1;
    article->imageCount = images;
    return 0;
}

int WireDecodeFeeds(const unsigned char *packet, size_t length,
                    char names[WIRE_MAX_FEEDS][33], short *count)
{
    Reader r; unsigned short n, i;
    if (!names || !count || packet_reader(packet, length, 0x06, &r) ||
        word(&r, &n) || n > WIRE_MAX_FEEDS) return -1;
    for (i = 0; i < n; ++i)
        if (text_field(&r, names[i], 33) || names[i][0] == 0) return -1;
    if (r.pos != r.end) return -1;
    *count = n;
    return 0;
}

int WireDecodeDates(const unsigned char *packet, size_t length,
                    WireDateEntry *entries, short *count)
{
    Reader r; unsigned short n, i;
    if (!entries || !count || packet_reader(packet, length, 0x09, &r) ||
        word(&r, &n) || n > WIRE_MAX_ARTICLES) return -1;
    for (i = 0; i < n; ++i)
        if (article_id(&r, entries[i].id) ||
            text_field(&r, entries[i].time, sizeof(entries[i].time)) ||
            text_field(&r, entries[i].date, sizeof(entries[i].date))) return -1;
    if (r.pos != r.end) return -1;
    *count = n; return 0;
}

/* Each backend PackBits run belongs to a single scanline. Reject runs that
   cross rows, missing data, and trailing bytes before allocating a bitmap. */
int WireDecodeImage(const unsigned char *packet, size_t length, WireImage *image,
                    unsigned char *bitmap, size_t capacity)
{
    Reader r; unsigned short row; size_t column, run, output = 0;
    const unsigned char *control, *data;
    if (!image || !bitmap || packet_reader(packet, length, 0x04, &r) ||
        article_id(&r, image->id) || word(&r, &image->index) ||
        word(&r, &image->width) || word(&r, &image->height) ||
        word(&r, &image->rowBytes) || image->index >= 5 ||
        !image->width || image->width > 320 || !image->height || image->height > 200 ||
        image->rowBytes != (image->width + 7) / 8 ||
        (size_t)image->rowBytes * image->height > capacity) return -1;
    for (row = 0; row < image->height; ++row) {
        column = 0;
        while (column < image->rowBytes) {
            if (take(&r, 1, &control)) return -1;
            if (*control == 128) continue;
            run = *control < 128 ? *control + 1 : 257 - *control;
            if (run > image->rowBytes - column) return -1;
            if (*control < 128) {
                if (take(&r, run, &data)) return -1;
                memcpy(bitmap + output, data, run);
            } else {
                if (take(&r, 1, &data)) return -1;
                memset(bitmap + output, *data, run);
            }
            column += run; output += run;
        }
    }
    return r.pos == r.end ? 0 : -1;
}

int WireDecodeRefresh(const unsigned char *packet, size_t length, WireRefresh *refresh)
{
    Reader r;
    const unsigned char *p;
    unsigned short i;
    if (!refresh || packet_reader(packet, length, 0x0B, &r) || take(&r, 11, &p) || r.pos != r.end)
        return -1;
    for (i = 0; i < 8; ++i)
        if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f'))) return -1;
    if (p[8] > 1) return -1;
    memcpy(refresh->job, p, 8); refresh->job[8] = 0;
    refresh->running = p[8];
    refresh->failures = ((unsigned short)p[9] << 8) | p[10];
    return 0;
}
