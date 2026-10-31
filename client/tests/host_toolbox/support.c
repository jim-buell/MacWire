#include "support.h"
#include "Connection.h"
#include "MacTCPTransport.h"
#include "Wire.h"
#include "MenuCommands.h"
ConnectionSettings gConnection = {"127.0.0.1", 8080, 0, 24};
WindowPtr gMainWindow;
long gContentScroll;
int testFailAllocation;
Ptr NewPtr(long size) { return testFailAllocation ? NULL : malloc(size); }
Ptr NewPtrClear(long size) { return testFailAllocation ? NULL : calloc(1, size); }
void DisposePtr(Ptr pointer) { free(pointer); }
Handle NewHandleClear(long size) {
    Handle handle = malloc(sizeof(*handle));
    if (handle) { *handle = calloc(1, size); if (!*handle) { free(handle); return NULL; } }
    return handle;
}
void DisposeHandle(Handle handle) { free(*handle); free(handle); }
void HLock(Handle handle) { (void)handle; }
void HUnlock(Handle handle) { (void)handle; }
void BlockMove(const void *source, void *destination, long size) { memmove(destination, source, size); }
void ClearReaderSelection(void) {}
void InvalidateReaderLayout(void) {}
int testUnsortedListPaints;
int testListUpdates, testRowUpdates, testIndicatorUpdates;
int testListReady, testEmptyPreview;
short testUpdatedRow;
void UpdateArticleRow(short index) { ++testRowUpdates; testUpdatedRow=index; }
void UpdateArticleReadIndicator(short index) { ++testIndicatorUpdates; testUpdatedRow=index; }
void UpdateArticleList(void) {
    short i;
    ++testListUpdates; testListReady=1;
    if (gIndexLoading) return;
    for (i=1; i<gMaxArticles; ++i)
        if (WireComparePublication(gArticles[i-1].published,gArticles[i-1].id,
            gArticles[i].published,gArticles[i].id,gNewestFirst)>0) {
            ++testUnsortedListPaints; break;
        }
}
void UpdateContentPane(void) {}
short testReaderPaintedSelection=-1;
void DrawContentPane(void) {
    if (!UserActionPending()) testReaderPaintedSelection=gSelectedIndex;
}
void UpdateScrollBars(void) {}
void UpdateStatusLine(void) {}
int testVisibleListRows=4;
short GetVisibleListRows(void) { return testVisibleListRows; }
short GetContentViewportHeight(void) { return 200; }
void ScrollList(int direction) { (void)direction; }
void ScrollReaderTo(long offset) { gContentScroll = offset; }
void ParseArticleID(char *packet, char *id) { (void)packet; id[0] = 0; }
char *ExtractBody(char *packet) { (void)packet; return NULL; }
void StringToPStr(unsigned char *result, const char *text) { result[0] = strlen(text); memcpy(result+1, text, result[0]); }
void LongToString(long value, char *result) { sprintf(result, "%ld", value); }
void ShortToString(short value, char *result) { sprintf(result, "%d", value); }
long ReaderImageUpdateTop(short index) { (void)index; return 0; }
void ReaderImageLoaded(long top) { (void)top; }
const char *TestArticleID(short index) { return gArticles[index].id; }
const char *TestArticleDate(short index) { return gArticles[index].date; }
const char *TestArticleTime(short index) { return gArticles[index].time; }

void InitConnectionSettings(void) {}
static short testMenuItems=4;
static Boolean testMenuChecks[WIRE_MAX_FEEDS+5];
MenuHandle GetMenuHandle(short id) { return id==kFeedsMenu ? (MenuHandle)1 : NULL; }
short CountMItems(MenuHandle menu) { (void)menu; return testMenuItems; }
void DeleteMenuItem(MenuHandle menu, short item) { (void)menu; testMenuChecks[item]=false; --testMenuItems; }
void AppendMenu(MenuHandle menu, ConstStringPtr text) { (void)menu; (void)text; ++testMenuItems; }
void SetMenuItemText(MenuHandle menu, short item, ConstStringPtr text) { (void)menu; (void)item; (void)text; }
void CheckItem(MenuHandle menu, short item, Boolean checked) { (void)menu; testMenuChecks[item]=checked; }
short TestFeedCheckedItem(void) {
    short i;
    for (i=1; i<=testMenuItems; ++i) if (testMenuChecks[i]) return i;
    return 0;
}
int testFeedsAtFirstPage=-1, testFeedsAtFirstArticle=-1, testFeedCheckedAtFirstRequest=-1;
short GetReaderImageWidth(void) { return 160; }
struct TestPacket { char path[256]; unsigned char *data; unsigned long length; int error; };
static struct TestPacket packets[512];
static int packetCount, packetIndex;
int testNavigateOnRequest, testSelectOnRequest;
int testLoadingFinished, testPhotoBeforeTextReady;
int testExpectedListCount=-1, testPartialLists, testWaitDepth, testMaxWaitDepth, testMissingWait, testUnexpectedWait;
int testExpectedListWait=-1;
void BeginNetworkWait(void) {
    ++testWaitDepth;
    if (testWaitDepth>testMaxWaitDepth) testMaxWaitDepth=testWaitDepth;
}
void EndNetworkWait(void) { --testWaitDepth; }
void TestResetPackets(void) {
    int i;
    for (i = 0; i < packetCount; ++i) free(packets[i].data);
    packetCount = packetIndex = 0;
    testNavigateOnRequest=testSelectOnRequest=0;
    extern int testDeferredAction; testDeferredAction=0;
    testExpectedListCount=-1; testPartialLists=testUnsortedListPaints=0;
    testExpectedListWait=-1;
    testWaitDepth=testMaxWaitDepth=testMissingWait=testUnexpectedWait=0;
    testLoadingFinished=testPhotoBeforeTextReady=0;
    testReaderPaintedSelection=-1;
    testMenuItems=4; memset(testMenuChecks,0,sizeof(testMenuChecks));
    testFeedsAtFirstPage=testFeedsAtFirstArticle=testFeedCheckedAtFirstRequest=-1;
    testListReady=testEmptyPreview=0;
    testVisibleListRows=4;
}
void TestQueuePacket(const char *path, const unsigned char *data, unsigned long length, int error) {
    struct TestPacket *packet = &packets[packetCount++];
    strcpy(packet->path, path); packet->length = length; packet->error = error;
    packet->data = malloc(length ? length : 1);
    if (length) memcpy(packet->data, data, length);
}
int TestPacketsRemaining(void) { return packetCount - packetIndex; }
int MacTCPRequest(uint32_t address, unsigned short port, const char *request,
                  unsigned char *body, size_t capacity, unsigned long *length) {
    char path[256]; struct TestPacket *packet;
    (void)address; (void)port;
    if (testFeedCheckedAtFirstRequest<0) testFeedCheckedAtFirstRequest=TestFeedCheckedItem();
    if (!strncmp(request,"GET /page?",10) && testFeedsAtFirstPage<0)
        testFeedsAtFirstPage=testMenuItems-4;
    if (!strncmp(request,"GET /article/",13) && testFeedsAtFirstArticle<0)
        testFeedsAtFirstArticle=testMenuItems-4;
    if (packetIndex >= packetCount) return kNetworkHTTPError;
    packet = &packets[packetIndex++];
    sscanf(request, "GET %255s", path);
    if (testExpectedListWait>=0 && (!strcmp(path,"/feeds") ||
        !strncmp(path,"/page?",6) || !strncmp(path,"/dates?",7))) {
        if (testExpectedListWait && !testWaitDepth) ++testMissingWait;
        if (!testExpectedListWait && testWaitDepth) ++testUnexpectedWait;
    }
    if (!strncmp(path,"/article/",9) || !strncmp(path,"/image/",7)) {
        if (testWaitDepth) ++testUnexpectedWait;
        if (!strncmp(path,"/image/",7) &&
            (testReaderPaintedSelection!=gSelectedIndex || !gArticles[gSelectedIndex].hasContent))
            ++testPhotoBeforeTextReady;
    }
    if (!strncmp(path,"/page?",6) || !strncmp(path,"/dates?",7)) {
        if (testExpectedListCount>=0 && gMaxArticles!=testExpectedListCount) ++testPartialLists;
        if (gMaxArticles==0 && !testWaitDepth) ++testMissingWait;
        if (gMaxArticles>0 && testWaitDepth && testExpectedListWait!=1) ++testUnexpectedWait;
    }
    if (strcmp(path, packet->path) || packet->length > capacity) return kNetworkHTTPError;
    if (testSelectOnRequest) {
        QueueArticleSelection(testSelectOnRequest-1); testSelectOnRequest=0;
        return kNetworkCancelled;
    }
    if (testNavigateOnRequest) {
        QueueArticleNavigation(testNavigateOnRequest); testNavigateOnRequest = 0;
        return kNetworkCancelled;
    }
    if (packet->error) return packet->error;
    memcpy(body, packet->data, packet->length); *length = packet->length;
    return 0;
}

static unsigned long testTicks;
int testCancelPoll;
unsigned long TickCount(void) { return ++testTicks; }
void TestAdvanceTicks(unsigned long ticks) { testTicks+=ticks; }
int NetworkPoll(void) { testTicks += 60; return testCancelPoll; }
const char *TestArticleAuthor(short index) { return gArticles[index].author; }
short TestArticleLinkCount(short index) { return gArticles[index].links ? gArticles[index].links->count : 0; }
const char *TestArticleLinkURL(short index, short link) {
    WireLinks *links=gArticles[index].links;
    return links && link>=0 && link<links->count ? links->urls+links->entries[link].urlOffset : NULL;
}

void TestSetImageCount(short index, short count) { gArticles[index].imageCount = count; }
unsigned char TestImageState(short index, short image) { return gArticles[index].imageState[image]; }

short TestArticleStyleCount(short index) { return gArticles[index].links ? gArticles[index].links->styleCount : 0; }

int testDeferredAction;
int DeferredActionPending(void) { return testDeferredAction; }
int UserActionPending(void) { return ArticleNavigationPending() || testDeferredAction; }

short testPreviewSelection=-1;
int testPreviewCalls;
void FinishReaderArticleLoading(void) { ++testLoadingFinished; }
void PreviewArticleSelection(short index) {
    testPreviewSelection=index; ++testPreviewCalls;
    if (index>=0 && !testListReady) ++testEmptyPreview;
}

short NextArticleImage(short index) {
    short image;
    if (UserActionPending()) return -1;
    for (image=0; image<gArticles[index].imageCount; ++image)
        if (!gArticles[index].imageState[image]) return image;
    return -1;
}
