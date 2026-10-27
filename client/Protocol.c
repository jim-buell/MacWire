/*
 * Protocol.c — Parse backend protocol responses
 *
 * Handles parsing of the custom binary protocol used to
 * communicate with the backend server.
 *
 * Protocol format (plan section 10):
 *   [1 byte: command] [2 bytes: length] [payload...]
 *
 * Commands:
 *   0x01 - SYNC request (client -> server)
 *   0x02 - INDEX response (server -> client)
 *   0x03 - ARTICLE response (server -> client)
 *   0x04 - IMAGE response (server -> client)
 *   0x05 - STATUS response (server -> client)
 *   0x07 - READ request (client -> server)
 *   0x08 - PREFETCH request (client -> server)
 *   0xFF - ERROR response (server -> client)
 */

#include "compat.h"
#include "articles.h"
#include "Wire.h"
#include "Feeds.h"
#include "ReadHistory.h"
#include "Connection.h"

/* Local helper used before its definition above */
void ShowErrorMessage(char *message, short length);

/* Protocol constants */
#define CMD_SYNC        0x01
#define CMD_INDEX       0x02
#define CMD_ARTICLE     0x03
#define CMD_IMAGE       0x04
#define CMD_STATUS      0x05
#define CMD_READ        0x07
#define CMD_PREFETCH    0x08
#define CMD_ERROR       0xFF

/* Protocol header size */
#define kProtocolHeader 3  // 1 byte command + 2 bytes length

/* Decode into temporary storage before changing the visible article list. */
static WireIndexEntry gDecodedIndex[WIRE_MAX_ARTICLES];
static WireArticle gDecodedArticle;
static WireDateEntry gDecodedDates[WIRE_MAX_ARTICLES];
static short gDateCount;

static void ApplyLocalDate(short index)
{
    short i;
    for (i = 0; i < gDateCount; ++i)
        if (!strcmp(gArticles[index].id, gDecodedDates[i].id)) {
            const char *comma = strchr(gDecodedDates[i].date, ',');
            const char *clock = comma ? strchr(comma + 2, ' ') : NULL;
            if (!strchr(gDecodedDates[i].time, ',') && comma && clock)
                snprintf(gArticles[index].time, sizeof(gArticles[index].time), "%.*s,%s",
                         (int)(comma - gDecodedDates[i].date), gDecodedDates[i].date, clock);
            else strcpy(gArticles[index].time, gDecodedDates[i].time);
            strcpy(gArticles[index].date, gDecodedDates[i].date);
            break;
        }
}

short ParseDatesResponse(const unsigned char *packet, unsigned long length)
{
    short i;
    if (WireDecodeDates(packet, length, gDecodedDates, &gDateCount)) return -1;
    ClearReaderSelection();
    for (i = 0; i < gMaxArticles; ++i) ApplyLocalDate(i);
    InvalidateReaderLayout();
    if (!gIndexLoading) { UpdateArticleList(); UpdateContentPane(); }
    return 0;
}


short ParseIndexResponse(const unsigned char *packet, unsigned long length)
{
    size_t count, i;
    if (WireDecodeIndex(packet, length, gDecodedIndex, WIRE_MAX_ARTICLES, &count))
        return -1;

    if (!AllocateArticleList(count)) return -1;
    gDateCount = 0;
    gUsingStartupSamples = false;
    gMaxArticles = 0;
    gSelectedIndex = -1;
    gScrollOffset = 0;
    for (i = 0; i < count; ++i) {
        struct ArticleEntry *article = &gArticles[gMaxArticles];
        WireIndexEntry *decoded = &gDecodedIndex[i];
        if (gFeedFilter[0] && strcmp(gFeedFilter, decoded->feed)) continue;
        strcpy(article->id, decoded->id);
        strcpy(article->feed, decoded->feed);
        strcpy(article->title, decoded->title);
        strcpy(article->summary, decoded->summary);
        article->published = decoded->published;
        ReadHistoryObserve(decoded->published);
        TimestampToString(decoded->published, article->time);
        TimestampToDate(decoded->published, article->date);
        article->fullContentAvailable = decoded->fullContentAvailable;
        article->hasContent = false;
        article->imageCount = min(decoded->imageCount, kMaxImagesPerArticle);
        article->isRead = IsArticleRead(article->id);
        gMaxArticles++; gLoadedArticles = gMaxArticles;
    }
    if (!gIndexLoading) UpdateArticleList();
    return 0;
}

short ParsePageResponse(const unsigned char *packet, unsigned long length,
                        char view[9], unsigned long *total, unsigned long *offset, short *count)
{
    WirePage page;
    size_t i;
    if (WireDecodePage(packet, length, gDecodedIndex, WIRE_MAX_ARTICLES, &page)) return -1;
    if (!view[0]) {
        if (page.offset) return -1;
        if (!AllocateArticleList(page.total)) return -2;
        strcpy(view, page.view); *total = page.total;
        gDateCount = 0; gUsingStartupSamples = false;
    }
    if (strcmp(view, page.view) || *total != page.total || page.offset != (unsigned long)gMaxArticles ||
        page.count > (unsigned long)gArticleCapacity - gMaxArticles) return -1;
    for (i = 0; i < page.count; ++i) {
        struct ArticleEntry *article = &gArticles[gMaxArticles++];
        WireIndexEntry *decoded = &gDecodedIndex[i];
        strcpy(article->id, decoded->id); strcpy(article->feed, decoded->feed);
        strcpy(article->title, decoded->title); strcpy(article->summary, decoded->summary);
        article->published = decoded->published;
        ReadHistoryObserve(decoded->published);
        TimestampToString(decoded->published, article->time);
        TimestampToDate(decoded->published, article->date);
        article->fullContentAvailable = decoded->fullContentAvailable;
        article->imageCount = min(decoded->imageCount, kMaxImagesPerArticle);
        article->isRead = IsArticleRead(article->id);
    }
    gLoadedArticles = gMaxArticles;
    *offset = page.offset; *count = page.count;
    return 0;
}

short ParseArticleResponse(const unsigned char *packet, unsigned long length)
{
    short index;
    if (WireDecodeArticle(packet, length, &gDecodedArticle)) return -1;
    index = FindArticleByID(gDecodedArticle.id);
    if (index < 0) return -1;
    if (!SetArticleBody(index, gDecodedArticle.body)) return -1;
    if (!SetArticleLinks(index, &gDecodedArticle.links)) return -1;
    if (strcmp(gArticles[index].title, gDecodedArticle.title) != 0 ||
        strcmp(gArticles[index].feed, gDecodedArticle.feed) != 0)
        UpdateArticleRow(index);
    strcpy(gArticles[index].title, gDecodedArticle.title);
    strcpy(gArticles[index].feed, gDecodedArticle.feed);
    strcpy(gArticles[index].author, gDecodedArticle.author);
    /* Keep this article’s local date from its own index page. */
    ApplyLocalDate(index);
    gArticles[index].imageCount = min(gDecodedArticle.imageCount, kMaxImagesPerArticle);
    memcpy(gArticles[index].imageOffsets, gDecodedArticle.imageOffsets, sizeof(gArticles[index].imageOffsets));
    /* Prefetching a different article must not repaint the visible reader. */
    if (index == gSelectedIndex) UpdateContentPane();
    return 0;
}

short ParseImageResponse(const unsigned char *packet, unsigned long length)
{
    static unsigned char bitmap[8000];
    WireImage image;
    short index, row, rowBytes;
    Handle handle;
    CachedImage *cached;
    long oldTop;
    if (WireDecodeImage(packet, length, &image, bitmap, sizeof(bitmap))) return -1;
    index = FindArticleByID(image.id);
    if (index < 0 || index != gSelectedIndex ||
        image.index >= gArticles[index].imageCount) return -1;
    rowBytes = (image.rowBytes + 1) & ~1;
    handle = NewHandleClear(6L + (long)rowBytes * image.height);
    if (handle == NULL) return -1;
    HLock(handle);
    cached = (CachedImage *)*handle;
    cached->width = image.width; cached->height = image.height; cached->rowBytes = rowBytes;
    for (row = 0; row < image.height; ++row)
        BlockMove(bitmap + row * image.rowBytes, cached->bits + row * rowBytes, image.rowBytes);
    HUnlock(handle);
    oldTop = ReaderImageUpdateTop(image.index);
    if (gArticles[index].images[image.index]) DisposeHandle(gArticles[index].images[image.index]);
    gArticles[index].images[image.index] = handle;
    gArticles[index].imageState[image.index] = 1;
    ReaderImageLoaded(oldTop);
    return 0;
}

short ParseStatusResponse(const unsigned char *packet, unsigned long length,
                           short *articleCount)
{
    if (!packet || !articleCount || length != 13 ||
        (unsigned char)packet[0] != CMD_STATUS ||
        (unsigned char)packet[1] != 0 ||
        (unsigned char)packet[2] != 10) return -1;
    *articleCount = ((unsigned char)packet[5] << 8) | (unsigned char)packet[6];
    return 0;
}

short ParseErrorResponse(const unsigned char *packet, unsigned long length)
{
    unsigned long payloadLength, messageLength;
    if (!packet || length < 5 || (unsigned char)packet[0] != CMD_ERROR)
        return -1;
    payloadLength = ((unsigned char)packet[1] << 8) | (unsigned char)packet[2];
    messageLength = ((unsigned char)packet[3] << 8) | (unsigned char)packet[4];
    if (payloadLength + 3 != length || messageLength + 2 != payloadLength)
        return -1;
    ShowErrorMessage((char *)packet + 5, (short)messageLength);
    return 0;
}

/*
 * TimestampToString — Convert a Unix timestamp to HH:MM string
 */
void TimestampToString(double timestamp, char *str)
{
    /* Display strings arrive in /dates with the backend's local DST rules. */
    strcpy(str, "--:--");
}

void TimestampToDate(double timestamp, char *str)
{
    strcpy(str, "Date unavailable");
}

/*
 * ShowErrorMessage — Display an error message to the user
 */
void ShowErrorMessage(char *message, short length)
{
    /*
     * Display an error alert dialog.
     * Uses the Macintosh Standard Alert API.
     */

    // In real code:
    // Alert errorAlert, 0;

    // For now, just print to console (Mini vMac)
}

/*
 * Helper: Send a SYNC request to the server
 */
void SendSyncRequest(char *lastSync)
{
    /*
     * Build and send a SYNC request packet:
     *   [0x01] [length] [8 bytes: last_sync timestamp]
     */
}

/*
 * Helper: Send a read request to the server
 */
void SendReadRequest(char *articleID, Boolean read)
{
    /*
     * Build and send a READ request packet:
     *   [0x07] [length] [8 bytes: ID] [1 byte: read (0 or 1)]
     */
}
