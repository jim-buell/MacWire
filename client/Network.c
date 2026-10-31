/*
 * Network.c — MacTCP HTTP client
 *
 * Handles all network communication with the backend server.
 * Uses MacTCP (the System 7 networking stack) to make HTTP requests.
 *
 * This is the bridge between the SE client and the modern backend.
 *
 * The SE client makes simple HTTP GET requests to:
 *   http://<server>:<port>/sync?last_sync=<timestamp>
 *   http://<server>:<port>/article/<id>
 *   http://<server>:<port>/image/<article_id>/<index>
 *
 * The backend wraps responses in a custom binary protocol.
 */

#include "compat.h"
#include "articles.h"
#include "Connection.h"
#include "Feeds.h"
#include "ReadHistory.h"
#include "ListPosition.h"
#include "Wire.h"
#include "MacTCPTransport.h"
#include <Strings.h>
#include <stdio.h>
#include <string.h>

/* kPrefetchCount — articles to prefetch ahead (plan section 11) */
#define kPrefetchCount 3

/* External helpers (shim.c, Articles.c, Protocol.c, UI.c) */
extern void ShortToString(short n, char *str);
extern void UpdateArticleList(void);
extern void PrefetchArticles(short fromIndex, short count);
extern void SelectArticle(short index);
extern void DrawStatusLine(void);

/* Local helpers (defined later in this file) */
void GetLastSyncTime(char *str);
void SaveLastSyncTime(void);
void ShowConnectionError(void);

/* External state */
extern short gSelectedIndex;
extern short gMaxArticles;

/* HTTP request buffer sizes — kept small for SE's 4MB RAM */
#define kMaxHTTPRequest 1024
#define kMaxHTTPResponse 32768 /* Bounded packet buffer; transport must reject overflow */

/* Network state */
static Boolean gNetworkReady = false;
Boolean gNetworkUnavailable = false;
static int gNetworkError;
static short gFeedRefreshState;
static short gPendingArticleDirection;
int gLastSyncSucceeded;
int gIndexLoading;

/* Swap only while decoding a packet. Cooperative event handling always sees
   the previous complete list, never the partially assembled download. */
typedef struct {
    struct ArticleEntry *articles;
    short capacity, visible, loaded, selected, scroll;
    Boolean samples;
} ArticleListSnapshot;
static void SwapArticleList(ArticleListSnapshot *other)
{
    extern short gScrollOffset;
    ArticleListSnapshot current = {gArticles, gArticleCapacity, gMaxArticles,
        gLoadedArticles, gSelectedIndex, gScrollOffset, gUsingStartupSamples};
    gArticles=other->articles; gArticleCapacity=other->capacity;
    gMaxArticles=other->visible; gLoadedArticles=other->loaded;
    gSelectedIndex=other->selected; gScrollOffset=other->scroll;
    gUsingStartupSamples=other->samples;
    *other=current;
}
static void DisposeStagedList(ArticleListSnapshot *staged)
{
    SwapArticleList(staged);
    AllocateArticleList(0);
    SwapArticleList(staged);
}
static unsigned char gResponseBuffer[kMaxHTTPResponse];
static unsigned long gResponseLength = 0;
static char gHTTPRequest[kMaxHTTPRequest];

int ArticleNavigationPending(void) { return gPendingArticleDirection != 0; }

int QueueArticleSelection(short index)
{
    if (index < 0 || index >= gMaxArticles) return 0;
    gPendingArticleDirection = index - gSelectedIndex;
    PreviewArticleSelection(index);
    return ArticleNavigationPending();
}

int QueueArticleNavigation(short direction)
{
    short target = gSelectedIndex + gPendingArticleDirection + direction;
    if (target < 0 || target >= gMaxArticles) return 0;
    return QueueArticleSelection(target);
}

void ApplyPendingArticleNavigation(void)
{
    short direction = gPendingArticleDirection;
    gPendingArticleDirection = 0;
    if (direction) {
        gNetworkError = 0;
        gNetworkUnavailable = false;
        SelectArticle(gSelectedIndex + direction);
    }
}

void ClearCancelledRequest(void)
{
    if (gNetworkError==kNetworkCancelled) gNetworkError=0;
}

void PrefetchIdleArticle(void)
{
    static unsigned long retryAt, selectedRetryAt;
    static char selectedID[9];
    short i;
    int previousError=gNetworkError;
    Boolean previousUnavailable=gNetworkUnavailable;
    unsigned long now=TickCount();
    if (UserActionPending() || gSelectedIndex<0 || gSelectedIndex>=gMaxArticles) return;
    if (strcmp(selectedID,gArticles[gSelectedIndex].id)) {
        strcpy(selectedID,gArticles[gSelectedIndex].id); retryAt=selectedRetryAt=0;
    }
    /* Interrupted foreground work outranks speculative neighboring bodies. */
    if (!gArticles[gSelectedIndex].hasContent) {
        if ((long)(now-selectedRetryAt)<0) return;
        SelectArticle(gSelectedIndex);
        selectedRetryAt=TickCount()+(NetworkRequestFailed() && !UserActionPending() ? 60*60UL : 6UL);
        return;
    }
    i=NextArticleImage(gSelectedIndex);
    if (UserActionPending()) return;
    if (i>=0) {
        if ((long)(now-selectedRetryAt)<0) return;
        FetchImage(gArticles[gSelectedIndex].id,i);
        selectedRetryAt=TickCount()+6UL;
        return;
    }
    if ((long)(now-retryAt)<0) return;
    for (i=gSelectedIndex+1; i<gMaxArticles && i<=gSelectedIndex+kPrefetchCount; ++i) {
        if (!gArticles[i].hasContent) {
            FetchArticle(gArticles[i].id);
            /* Avoid retrying a failing server on every idle event. */
            retryAt=TickCount()+(NetworkRequestFailed() && !UserActionPending() ? 60*60UL : 6UL);
            gNetworkError=previousError;
            gNetworkUnavailable=previousUnavailable;
            return;
        }
    }
}

void InitNetwork(void)
{
    InitConnectionSettings();
#ifdef RSS_DEMO_TRANSPORT
    /* A demo must open bundled packets even with saved live preferences. */
    gConnection.sample = 1;
#endif
    gNetworkReady = false;
}

const char *NetworkErrorText(void)
{
    switch (gNetworkError) {
    case -2: return "Not enough memory for this view. Choose a shorter Article age.";
    case kNetworkNoDriver: return "MacTCP is not available on this machine.";
    case kNetworkCancelled: return "Connection cancelled.";
    case kNetworkTimeout: case -23016: return "The server did not respond in time.";
    case kNetworkHTTPError: return "The server sent an invalid or oversized response.";
    case -1: return "Enter a valid server IPv4 address and port.";
    default: return "Cannot reach the server. Check its address and port.";
    }
}

int NetworkRequestFailed(void) { return gNetworkError != 0; }

static int Request(const ConnectionSettings *settings, const char *path)
{
    uint32_t address;
    if (!ConnectionAddress(settings->address, &address) || !settings->port) {
        gNetworkError = -1;
        return -1;
    }
    snprintf(gHTTPRequest, sizeof(gHTTPRequest),
             "GET %s HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
             path, settings->address, settings->port);
    gNetworkError = MacTCPRequest(address, settings->port, gHTTPRequest,
                                 gResponseBuffer, sizeof(gResponseBuffer), &gResponseLength);
    return gNetworkError;
}

int TestBackendConnection(const ConnectionSettings *settings, short *articles)
{
    char path[64];
    snprintf(path, sizeof(path), "/status?hours=%u", settings->windowHours);
    if (Request(settings, path) != 0) return 0;
    if (ParseStatusResponse(gResponseBuffer, gResponseLength, articles) != 0) {
        gNetworkError = kNetworkHTTPError;
        return 0;
    }
    return 1;
}

/*
 * BuildSyncRequest — Build an HTTP GET request for /sync
 *
 * Returns the HTTP request string in gHTTPRequest.
 * Includes the last_sync timestamp for incremental sync.
 */
static void BuildIndexRequest(const char *endpoint, char *lastSyncStr)
{
    char feed[65]; short i;
    for (i = 0; gFeedFilter[i]; ++i)
        snprintf(feed + 2 * i, 3, "%02x", (unsigned char)gFeedFilter[i]);
    feed[2 * i] = 0;
    snprintf(gHTTPRequest, sizeof(gHTTPRequest), "%s?last_sync=%s&feed=%s&hours=%u",
             endpoint, lastSyncStr, feed, gConnection.windowHours);
}

void BuildSyncRequest(char *lastSyncStr)
{
    BuildIndexRequest("/sync", lastSyncStr);
}

void BuildArticleRequest(char *articleID)
{
    snprintf(gHTTPRequest, sizeof(gHTTPRequest), "/article/%s", articleID);
}

void BuildImageRequest(char *articleID, short index)
{
    snprintf(gHTTPRequest, sizeof(gHTTPRequest), "/image/%s/%d?width=%d", articleID, index, GetReaderImageWidth());
}

void BuildStatusRequest(void)
{
    strcpy(gHTTPRequest, "/status");
}

/*
 * SendRequest — Send an HTTP request and receive the response
 *
 * Returns:
 *   0 on success
 *   Negative error code on failure
 *
 * On success, the response is stored in gResponseBuffer.
 */
short SendRequest(void)
{
    char path[256];
    gResponseLength = 0;
    if (UserActionPending()) { gNetworkError = kNetworkCancelled; return -1; }
    if (gConnection.sample) {
        extern short DemoResponse(const char *, unsigned char *, unsigned long,
                                  unsigned long *);
        char request[300];
        short result;
        snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\n\r\n", gHTTPRequest);
        result = DemoResponse(request, gResponseBuffer, sizeof(gResponseBuffer),
                              &gResponseLength);
        if (result == 0) {
            gNetworkError = 0;
            gNetworkReady = true;
            return 0;
        }
        gNetworkError = result == kNetworkCancelled ? kNetworkCancelled : kNetworkHTTPError;
        return -1;
    }
    snprintf(path, sizeof(path), "%s", gHTTPRequest);
    gNetworkReady = Request(&gConnection, path) == 0;
    return gNetworkReady ? 0 : -1;
}

/*
 * SyncWithServer — Fetch the current article index
 *
 * This is the main synchronization function. It:
 *   1. Creates a time-window view with /page
 *   2. Transfers every index/date batch in that stable view
 *   3. Assembles and sorts the complete list
 *   4. Updates the article list
 *   5. Triggers prefetch of next articles
 */
void SyncStartupSamples(void)
{
    ResetStartupSamples();
    gLastSyncSucceeded = 1;
    gNetworkUnavailable = false;
    SortArticles();
    UpdateArticleList();
    if (gMaxArticles > 0) SelectArticle(0);
    else UpdateContentPane();
    UpdateScrollBars(); UpdateFeedsMenu();
}

const char *FeedRefreshStatusText(void)
{
    switch (gFeedRefreshState) {
    case 1: return "Checking feeds...";
    case 2: return "Some feeds failed.";
    case 3: return "Feed check still running.";
    default: return NULL;
    }
}

void RefreshFeedsFromServer(void)
{
    WireRefresh refresh;
    unsigned long started, nextPoll;
    char job[9];
    short warning = 0;
    if (gConnection.sample || (gUsingStartupSamples && !gConnection.address[0])) {
        gFeedRefreshState = 0;
        SyncWithServer();
        return;
    }
    gFeedRefreshState = 1;
    gNetworkUnavailable = false;
    UpdateStatusLine();
    strcpy(gHTTPRequest, "/refresh");
    if (SendRequest() != 0) goto failed;
    if (WireDecodeRefresh(gResponseBuffer, gResponseLength, &refresh)) {
        gNetworkError = kNetworkHTTPError; goto failed;
    }
    strcpy(job, refresh.job);
    started = nextPoll = TickCount();
    while (refresh.running) {
        if ((unsigned long)(TickCount() - started) >= 300UL * 60UL) { warning = 3; break; }
        nextPoll += 60;
        while ((long)(TickCount() - nextPoll) < 0) {
            if (NetworkPoll()) { gNetworkError = kNetworkCancelled; goto failed; }
        }
        snprintf(gHTTPRequest, sizeof(gHTTPRequest), "/refresh?job=%s", job);
        if (SendRequest() != 0) goto failed;
        if (WireDecodeRefresh(gResponseBuffer, gResponseLength, &refresh) || strcmp(job, refresh.job)) {
            gNetworkError = kNetworkHTTPError; goto failed;
        }
        /* A slow request must not cause a burst of catch-up polls. */
        nextPoll = TickCount();
    }
    if (!warning && refresh.failures) warning = 2;
    gFeedRefreshState = 0;
    SyncWithServer();
    if (gLastSyncSucceeded) gFeedRefreshState = warning;
    UpdateStatusLine();
    return;
failed:
    gFeedRefreshState = 0;
    gLastSyncSucceeded = 0;
    ShowConnectionError();
}

static void SyncArticleList(Boolean changingFeed)
{
    char lastSync[20];
    short result;
    Boolean waitingForList;
    ArticleListSnapshot staged = {NULL,0,0,0,-1,0,false};
    gLastSyncSucceeded = 0;
    gFeedRefreshState = 0;
    FinishReadingSelected();
    if (gUsingStartupSamples && !gConnection.sample && !gConnection.address[0]) {
        SyncStartupSamples();
        return;
    }
    /* A feed change waits for its replacement list, even with an old list visible. */
    waitingForList = changingFeed || gMaxArticles == 0;
    if (waitingForList) BeginNetworkWait();
    strcpy(gHTTPRequest, "/feeds");
    if (SendRequest() == 0 &&
        WireDecodeFeeds(gResponseBuffer, gResponseLength, gFeedNames, &gFeedCount)==0) {
        ValidateSavedFeed();
        /* Feed names are ready independently of the article index and photos. */
        UpdateFeedsMenu();
    }

    // Get last sync timestamp (from persistent storage)
    GetLastSyncTime(lastSync);

    if (gConnection.sample) {
        BuildSyncRequest(lastSync);
        result = SendRequest();
        if (result == 0) {
            SwapArticleList(&staged); gIndexLoading=true;
            result = ParseIndexResponse(gResponseBuffer, gResponseLength);
            gIndexLoading=false; SwapArticleList(&staged);
        }
        if (result == 0) {
            BuildIndexRequest("/dates", lastSync);
            if (SendRequest() == 0) {
                SwapArticleList(&staged); gIndexLoading=true;
                ParseDatesResponse(gResponseBuffer, gResponseLength);
                gIndexLoading=false; SwapArticleList(&staged);
            }
        }
    } else {
        char view[9] = "", path[256];
        unsigned long total = 0, offset = 0, pageOffset;
        short count;
        do {
            BuildIndexRequest("/page", lastSync);
            snprintf(path, sizeof(path), "%s&view=%s&offset=%lu", gHTTPRequest, view, offset);
            strcpy(gHTTPRequest, path);
            result = SendRequest();
            if (result != 0) break;
            SwapArticleList(&staged); gIndexLoading=true;
            result = ParsePageResponse(gResponseBuffer, gResponseLength, view, &total, &pageOffset, &count);
            gIndexLoading=false; SwapArticleList(&staged);
            if (result != 0) { gNetworkError = result == -2 ? -2 : kNetworkHTTPError; break; }
            snprintf(gHTTPRequest, sizeof(gHTTPRequest), "/dates?view=%s&offset=%lu", view, pageOffset);
            result = SendRequest();
            if (result == 0) {
                SwapArticleList(&staged); gIndexLoading=true;
                result = ParseDatesResponse(gResponseBuffer, gResponseLength);
                gIndexLoading=false; SwapArticleList(&staged);
            }
            if (result != 0) break;
            offset += count;
        } while (offset < total);
    }
    if (result == 0) {
        /* Release the old cache only after the replacement is complete. */
        AllocateArticleList(0); SwapArticleList(&staged);
        gLastSyncSucceeded = 1; gNetworkUnavailable = false;
        gIndexLoading=true;
        SaveReadHistory(); SortArticles();
        gIndexLoading=false;
        /* The list is ready: stop the blocking signal before article downloads. */
        if (waitingForList) { EndNetworkWait(); waitingForList=false; }
        /* Include cached sources even when an older Helper lacks /feeds. */
        UpdateFeedsMenu();
        /* Queue and invalidate the complete left pane before first selection. */
        UpdateArticleList();
        if (!RestoreListPosition()) {
            if (gMaxArticles > 0) SelectArticle(0);
            else UpdateContentPane();
        }
        /* Nearby bodies are prefetched by the idle event loop. */
        SaveLastSyncTime(); UpdateScrollBars(); UpdateFeedsMenu();
    } else {
        DisposeStagedList(&staged);
        if (!gNetworkError) gNetworkError = kNetworkHTTPError;
        ShowConnectionError(); UpdateArticleList(); UpdateContentPane(); UpdateScrollBars();
    }
    if (waitingForList) EndNetworkWait();
}

void SyncWithServer(void)
{
    SyncArticleList(false);
}

void SyncFeedWithServer(void)
{
    SyncArticleList(true);
}

/*
 * FetchArticle — Fetch full article content by ID
 *
 * Called when the user selects an article that hasn't been loaded yet.
 * The article content is stored in the local cache for later display.
 */
void FetchArticle(char *articleID)
{
    BuildArticleRequest(articleID);
    if (SendRequest() == 0) {
        if (!gResponseLength || ParseArticleResponse(gResponseBuffer, gResponseLength) != 0)
            gNetworkError = kNetworkHTTPError;
    }
    if (NetworkRequestFailed()) ShowConnectionError();
}

/*
 * FetchImage — Fetch a processed 1-bit image
 *
 * Called when an article with images is displayed.
 * Images are fetched lazily (plan section 14).
 */
void FetchImage(char *articleID, short imageIndex)
{
    BuildImageRequest(articleID, imageIndex);
    if (SendRequest() == 0 && gResponseLength > 0 &&
        ParseImageResponse(gResponseBuffer, gResponseLength) == 0) return;
    if (UserActionPending()) return;
    {
        short index = FindArticleByID(articleID);
        if (index >= 0 && imageIndex >= 0 && imageIndex < kMaxImagesPerArticle) {
            gArticles[index].imageState[imageIndex] = 2;
            if (index==gSelectedIndex) UpdateContentPane();
        }
    }
    /* A missing/invalid photo is optional; try the remaining photos. Transport
       cancellation, timeout, or driver failure still stops the image sequence. */
    if (gNetworkError == kNetworkHTTPError) gNetworkError = 0;
}

/*
 * ShowConnectionError — Display a connection error dialog
 */
void ShowConnectionError(void)
{
    if (UserActionPending()) return;
    gNetworkUnavailable = true;
    UpdateStatusLine();
}

/*
 * Helper: Get last sync time from persistent storage
 */
void GetLastSyncTime(char *str)
{
    /* Read from a small file on BlueSCSI storage */
    // Default to "0" for first sync
    strcpy(str, "0");
}

/*
 * Helper: Save last sync time to persistent storage
 */
void SaveLastSyncTime(void)
{
    /* Write current time to a metadata file on BlueSCSI */
    // TimeGetCurrent() would be used here
}
