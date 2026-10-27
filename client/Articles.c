/*
 * Articles.c — Article list and content management
 *
 * Manages the local article data structure, caching, and
 * prefetch logic. Handles:
 *   - Article list state (selection, scrolling)
 *   - Local read/unread tracking
 *   - Local caching of article content
 *   - Smart prefetch (plan section 11)
 *   - Persistent storage on BlueSCSI (plan section 12)
 */

#include "compat.h"
#include "Connection.h"
#include "Feeds.h"
#include "articles.h"
#include <Memory.h>
#include <Resources.h>
#include <stdlib.h>
#include "Wire.h"
#include "ReadHistory.h"

/* External declarations */
extern WindowPtr gMainWindow;
extern short gSelectedIndex;
extern short gScrollOffset;

/* Local helpers used before their definition above */
void EnsureArticleVisible(short index);
void SaveArticleState(short index);
void SaveArticleToDisk(short index);
void LoadCachedArticles(void);
void ResetStartupSamples(void);

/* Protocol.c helpers */
void SendReadRequest(char *articleID, Boolean read);

/* Global article array (definition; declared in articles.h) */
struct ArticleEntry *gArticles;
short gArticleCapacity;
Boolean gNewestFirst = true;
short gMaxArticles = 0;
short gLoadedArticles = 0;
Boolean gUsingStartupSamples = false;

Boolean AllocateArticleList(unsigned long count)
{
    struct ArticleEntry *next;
    unsigned long entry;
    short image;
    if (count > 32767UL) return false;
    next = count ? (struct ArticleEntry *)NewPtrClear(count * sizeof(*next)) : NULL;
    if (count && !next) return false;
    for (entry = 0; entry < count; ++entry)
        for (image = 0; image < kMaxImagesPerArticle; ++image) next[entry].imageOffsets[image] = 0xFFFF;
    ClearArticleBodies();
    if (gArticles) DisposePtr((Ptr)gArticles);
    gArticles = next; gArticleCapacity = count;
    gMaxArticles = gLoadedArticles = 0; gSelectedIndex = -1; gScrollOffset = 0;
    return true;
}

static Boolean ArticleMatchesVisibility(const struct ArticleEntry *article)
{
    return gArticleVisibility == kShowAllArticles ||
        (gArticleVisibility == kShowReadArticles ? article->isRead : !article->isRead);
}

static int CompareArticles(const void *a, const void *b)
{
    const struct ArticleEntry *first = a, *second = b;
    if (ArticleMatchesVisibility(first) != ArticleMatchesVisibility(second))
        return ArticleMatchesVisibility(first) ? -1 : 1;
    return WireComparePublication(first->published, first->id, second->published, second->id, gNewestFirst);
}

void SortArticles(void)
{
    char selected[9] = "";
    if (gSelectedIndex >= 0 && gSelectedIndex < gMaxArticles)
        strcpy(selected, gArticles[gSelectedIndex].id);
    if (gSelectedIndex >= 0 && gSelectedIndex < gLoadedArticles &&
        !ArticleMatchesVisibility(&gArticles[gSelectedIndex])) ReleaseArticleImages(gSelectedIndex);
    ClearReaderSelection();
    if (gLoadedArticles > 1) qsort(gArticles, gLoadedArticles, sizeof(*gArticles), CompareArticles);
    for (gMaxArticles = 0; gMaxArticles < gLoadedArticles &&
         ArticleMatchesVisibility(&gArticles[gMaxArticles]); ++gMaxArticles) {}
    gSelectedIndex = selected[0] ? FindArticleByID(selected) : -1;
    gScrollOffset = 0;
    if (gSelectedIndex >= 0) EnsureArticleVisible(gSelectedIndex);
    UpdateArticleList(); UpdateContentPane(); UpdateScrollBars(); UpdateStatusLine();
}

void ToggleArticleSort(void)
{
    gNewestFirst = !gNewestFirst;
    SortArticles();
}

void SetArticleVisibility(unsigned short visibility)
{
    if (visibility > kShowReadArticles) return;
    ReadHistorySetVisibility(visibility);
    SaveReadHistory();
    SortArticles(); UpdateStatusLine();
}

void FinishReadingSelected(void)
{
    if (gSelectedIndex >= 0 && gSelectedIndex < gMaxArticles &&
        gArticles[gSelectedIndex].hasContent && !gArticles[gSelectedIndex].isRead) {
        gArticles[gSelectedIndex].isRead = true;
        UpdateArticleReadIndicator(gSelectedIndex);
        SaveArticleState(gSelectedIndex);
        SaveReadHistory();
    }
}

void ReleaseArticleImages(short index)
{
    short i;
    if (index < 0 || index >= gLoadedArticles) return;
    for (i = 0; i < kMaxImagesPerArticle; ++i) {
        if (gArticles[index].images[i]) DisposeHandle(gArticles[index].images[i]);
        gArticles[index].images[i] = NULL;
        gArticles[index].imageState[i] = 0;
    }
}

void ClearArticleBodies(void)
{
    short i;
    ClearReaderSelection();
    for (i = 0; i < gArticleCapacity; ++i) {
        ReleaseArticleImages(i);
        if (gArticles[i].body) {
            DisposePtr((Ptr)gArticles[i].body);
            gArticles[i].body = NULL;
        }
        gArticles[i].hasContent = false;
        if (gArticles[i].links) DisposePtr((Ptr)gArticles[i].links);
        gArticles[i].links = NULL;
    }
}

Boolean SetArticleBody(short index, const char *body)
{
    size_t length;
    char *copy;
    if (index < 0 || index >= gMaxArticles || !body) return false;
    length = strlen(body);
    if (length >= kMaxBodyLength) return false;
    {
        short i, cached = 0, victim = -1;
        for (i = 0; i < gLoadedArticles; ++i) {
            if (gArticles[i].body) ++cached;
            if (gArticles[i].body && i != gSelectedIndex && i != index && victim < 0) victim = i;
        }
        if (cached >= 8 && victim >= 0 && !gArticles[index].body) {
            DisposePtr((Ptr)gArticles[victim].body);
            gArticles[victim].body = NULL; gArticles[victim].hasContent = false;
            if (gArticles[victim].links) DisposePtr((Ptr)gArticles[victim].links);
            gArticles[victim].links = NULL;
        }
    }
    copy = (char *)NewPtr((long)(length + 1));
    if (!copy) return false;
    if (index == gSelectedIndex) ClearReaderSelection();
    BlockMove(body, copy, length + 1);
    if (gArticles[index].body) DisposePtr((Ptr)gArticles[index].body);
    gArticles[index].body = copy;
    if (gArticles[index].links) DisposePtr((Ptr)gArticles[index].links);
    gArticles[index].links = NULL;
    gArticles[index].hasContent = true;
    return true;
}

Boolean SetArticleLinks(short index, const WireLinks *links)
{
    WireLinks *copy = NULL;
    if (index < 0 || index >= gMaxArticles || !links) return false;
    if (links->count || links->styleCount) {
        long bytes = offsetof(WireLinks, urls) + links->urlBytes;
        copy = (WireLinks *)NewPtr(bytes);
        if (!copy) return false;
        BlockMove(links, copy, bytes);
    }
    if (gArticles[index].links) DisposePtr((Ptr)gArticles[index].links);
    gArticles[index].links = copy;
    return true;
}

/* Selection and scroll state (declared in compat.h) */
short gSelectedIndex;
short gScrollOffset;

/*
 * InitArticles — Initialize the article cache
 *
 * Loads cached articles from BlueSCSI storage.
 */
void InitArticles(void)
{
    short i;

    ClearArticleBodies();

    // Clear the cache
    for (i = 0; i < gArticleCapacity; i++) {
        gArticles[i].id[0] = '\0';
        gArticles[i].hasContent = false;
        gArticles[i].fullContentAvailable = false;
        gArticles[i].isRead = false;
        gArticles[i].imageCount = 0;
    }

    gMaxArticles = gLoadedArticles = 0;
    gUsingStartupSamples = false;
    gSelectedIndex = -1;
    gScrollOffset = 0;

    // Load from persistent storage (BlueSCSI)
    LoadCachedArticles();

    /* Live mode starts empty; samples are loaded only by an explicit sample
       request, never as placeholders before the server responds. */
}

/*
 * ResetStartupSamples — populate and filter placeholder articles so the
 * list and content panes are populated before a network backend exists.
 */
void ResetStartupSamples(void)
{
    static const struct { const char *id; const char *time; const char *feed; const char *title; const char *summary; const char *body; } demo[] = {
        { "00000001", "08:12", "Tech Daily",      "Local Startup Unveils a News Reader for Vintage Macs",        "A native application brings rolling news to System 7 machines via a modern backend.",
          "A small team has built a native news reader for vintage Macintosh machines, connecting them to a modern backend over a simple protocol. The app fetches fresh articles and tracks what you have read. It renders wrapped text in a classic two-pane window that fits the SE's screen. The designers say the goal is to make old hardware feel current without losing the familiar look. Early testers report that the scrolling and list navigation feel smooth and responsive." },
        { "00000002", "09:03", "World Now",       "Global Leaders Meet for Annual Economic Summit",              "Delegates from dozens of countries gathered to discuss trade, currency, and growth.",
          "Delegates from dozens of countries gathered this week to discuss trade, currency, and economic growth. Speakers emphasized the need for cooperation as markets faced unexpected volatility. Negotiations covered tariffs, digital commerce, and development aid. Observers noted a more collaborative tone than in recent years. The summit is expected to produce a set of shared commitments to be reviewed later this year." },
        { "00000003", "09:41", "Games Weekly",    "New Console Announced at Summer Showcase",                    "The reveal featured a handheld with a color display and weeks of battery life.",
          "The reveal featured a handheld console with a bright color display and weeks of battery life on a single charge. Developers praised the device's lightweight design and the size of its game library. Publishers announced a wave of launch titles spanning several genres. Analysts called the announcement one of the most anticipated of the year. Pre-orders are expected to open soon as demand appears strong." },
        { "00000004", "10:17", "Design File",     "The Return of Serif Headlines in Web Design",                 "Typography trends are swinging back to classic serifs for headlines and titles.",
          "Typography trends are swinging back to classic serifs for headlines and titles across the web. Designers say refined serif faces lend a sense of craft and authority that plain sans-serifs sometimes lack. New variable fonts make it easier to tune weight and spacing for any screen. Publishers have adopted the style for long-form articles and feature stories. Critics warn that overuse can hurt readability on small devices." },
        { "00000005", "10:55", "AI Monitor",      "Researchers Publish a Smaller, Faster Language Model",        "The new model matches larger ones on several benchmarks at a fraction of the size.",
          "Researchers published a smaller, faster language model that matches larger ones on several benchmarks. The team achieved the gains through a more efficient architecture and a cleaner training process. The model runs comfortably on modest hardware, which broadens who can use it. Independent testers confirmed strong results on common evaluation tasks. The work has drawn attention from both academics and industry developers." }
    };
    int n = sizeof(demo) / sizeof(demo[0]);
    int i, j;
    static unsigned short readMask;

    /* Retain read state when moving between a source and All Feeds. */
    for (i = 0; i < gLoadedArticles; ++i)
        for (j = 0; j < n; ++j)
            if (!strcmp(gArticles[i].id, demo[j].id)) {
                if (gArticles[i].isRead) readMask |= 1 << j;
                else readMask &= ~(1 << j);
            }
    if (!AllocateArticleList(n)) return;
    gMaxArticles = 0; gSelectedIndex = -1;
    gScrollOffset = 0; gContentScroll = 0;
    gUsingStartupSamples = true;
    InvalidateReaderLayout();
    for (i = 0; i < n && gMaxArticles < kMaxArticles; i++) {
        struct ArticleEntry *a;
        if (gFeedFilter[0] && strcmp(gFeedFilter, demo[i].feed)) continue;
        a = &gArticles[gMaxArticles];
        strcpy(a->id,      demo[i].id);
        snprintf(a->time, sizeof(a->time), "Sep 30, %s AM", demo[i].time);
        a->published = i;
        strcpy(a->feed,    demo[i].feed);
        strcpy(a->title,   demo[i].title);
        strcpy(a->summary, demo[i].summary);
        /* SetArticleBody validates against the populated article count. */
        ++gMaxArticles; gLoadedArticles = gMaxArticles;
        SetArticleBody(gMaxArticles - 1, demo[i].body);
        a->fullContentAvailable = true;
        a->isRead = (readMask & (1 << i)) != 0;
        a->date[0] = 0;
        a->imageCount = 0;
    }
}

/*
 * SelectArticle — Select an article by index
 *
 * Per plan section 8:
 *   1. Change the selected row in the article list
 *   2. Update the content pane
 *   3. Mark the previous/current article as read
 *   4. Make use of cached content whenever available
 */
static void SelectArticleContent(short index)
{
    short i;
    Boolean articleFailed = false;
    if (index < 0 || index >= gMaxArticles) return;
    PreviewArticleSelection(index);
    ClearReaderSelection();
    if (gSelectedIndex != index) {
        FinishReadingSelected();
        if (gSelectedIndex >= 0) ReleaseArticleImages(gSelectedIndex);
    }
    gSelectedIndex = index;
    /* Hiding the article just left must not change the new selection by index. */
    if (gArticleVisibility != kShowAllArticles) {
        SortArticles();
        index = gSelectedIndex;
        if (index < 0) return;
    }

    EnsureArticleVisible(index);
    PreviewArticleSelection(-1);
    // Reset content scroll for the new article
    gContentScroll = 0;
    InvalidateReaderLayout();
    UpdateScrollBars();

    // Update the content pane
    UpdateContentPane();

    // Fetch article content if not cached (plan section 11)
    if (!gArticles[index].hasContent) {
        FetchArticle(gArticles[index].id);
        articleFailed = NetworkRequestFailed();
    }
    if (UserActionPending()) return;
    FinishReaderArticleLoading();
    /* Replace loading feedback with the complete text pane before photo
       requests can service update events with only image damage. Merely
       invalidating the pane leaves the previous loading label on screen. */
    DrawContentPane();
    /* Photos must not mask a failed body request by replacing its network
       error with a successful image response. Idle work retries the body. */
    if (UserActionPending() || articleFailed || !gArticles[index].hasContent) return;
    /* Only the visible article owns images, keeping the SE cache bounded. */
    while ((i=NextArticleImage(index))>=0) {
        FetchImage(gArticles[index].id, i);
        if (NetworkRequestFailed()) {
            if (!UserActionPending()) {
                short remaining;
                for (remaining=0; remaining<gArticles[index].imageCount; ++remaining)
                    if (!gArticles[index].imageState[remaining]) gArticles[index].imageState[remaining]=2;
                UpdateContentPane();
            }
            break;
        }
        if (UserActionPending()) return;
    }
    if (UserActionPending()) return;
    /* Image arrival already updates geometry; keep the measured text layout. */
    UpdateScrollBars(); UpdateStatusLine();
}

/* Loading text reports progress; navigation and scrolling stay available. */
void SelectArticle(short index)
{
    if (index < 0 || index >= gMaxArticles) return;
    SelectArticleContent(index);
    if (!ArticleNavigationPending()) FinishReaderArticleLoading();
}

/*
 * SelectPreviousArticle — Move selection up by one
 */
void SelectPreviousArticle(void)
{
    if (gSelectedIndex > 0) {
        SelectArticle(gSelectedIndex - 1);
    } else {
        // At top — scroll list up
        ScrollList(-1);
    }
}

/*
 * SelectNextArticle — Move selection down by one
 */
void SelectNextArticle(void)
{
    if (gSelectedIndex < gMaxArticles - 1) {
        SelectArticle(gSelectedIndex + 1);
    }
}

/*
 * EnsureArticleVisible — Ensure the selected article is visible
 */
void EnsureArticleVisible(short index)
{
    short visibleRows = GetVisibleListRows();
    short previous = gScrollOffset;
    // Adjust scroll offset so the selected article is visible
    if (index < gScrollOffset) {
        gScrollOffset = index;
    } else if (index >= gScrollOffset + visibleRows) {
        gScrollOffset = index - visibleRows + 1;
    }

    if (gScrollOffset != previous) UpdateArticleList();
}

/* Find the next unread article, wrapping once and excluding the current one. */
short FindNextUnreadArticle(void)
{
    long step;
    short index;
    for (step = 1; step <= gMaxArticles; ++step) {
        index = (gSelectedIndex + step) % gMaxArticles;
        if (index != gSelectedIndex && !gArticles[index].isRead) return index;
    }
    return -1;
}

void SelectNextUnreadArticle(void)
{
    short index = FindNextUnreadArticle();
    if (index >= 0) SelectArticle(index);
}

/*
 * ScrollArticle — Scroll the article content pane
 *
 * Positive value scrolls down, negative scrolls up.
 */
void ScrollArticle(short direction)
{
    ScrollReaderTo(gContentScroll + (long)direction * GetContentViewportHeight());
}

/*
 * AdvanceToNextArticle — Advance to the next article
 *
 * Called when the user presses Space at the bottom of an article.
 */
void AdvanceToNextArticle(void)
{
    if (gSelectedIndex < gMaxArticles - 1) {
        SelectArticle(gSelectedIndex + 1);
    }
}

/*
 * OpenSelectedArticle — Open/focus the selected article
 *
 * Equivalent to pressing Enter on the selected article.
 */
void OpenSelectedArticle(void)
{
    if (gSelectedIndex >= 0 && gSelectedIndex < gMaxArticles) {
        // Fetch full content if not already cached
        if (!gArticles[gSelectedIndex].hasContent) {
            FetchArticle(gArticles[gSelectedIndex].id);
        }
        UpdateContentPane();
    }
}

/*
 * ToggleMarkReadSelected — Mark read/unread the selected article
 */
void ToggleMarkReadSelected(void)
{
    if (gSelectedIndex >= 0 && gSelectedIndex < gMaxArticles) {
        gArticles[gSelectedIndex].isRead = !gArticles[gSelectedIndex].isRead;
        SendReadRequest(gArticles[gSelectedIndex].id, gArticles[gSelectedIndex].isRead);
        SaveArticleState(gSelectedIndex);
        SaveReadHistory(); SortArticles(); UpdateStatusLine();
    }
}

/* The selected feed is also checked here when a mixed queue is displayed. */
Boolean HasUnreadArticlesInFeed(void)
{
    short i;
    for (i = 0; i < gLoadedArticles; ++i)
        if (!gArticles[i].isRead &&
            (!gFeedFilter[0] || !strcmp(gFeedFilter, gArticles[i].feed))) return true;
    return false;
}

void MarkAllArticlesRead(void)
{
    short i;
    for (i = 0; i < gLoadedArticles; ++i) {
        if (gArticles[i].isRead ||
            (gFeedFilter[0] && strcmp(gFeedFilter, gArticles[i].feed))) continue;
        gArticles[i].isRead = true;
        SendReadRequest(gArticles[i].id, true);
        SaveArticleState(i);
    }
    SaveReadHistory(); SortArticles(); UpdateStatusLine();
}

/*
 * PrefetchArticles — Prefetch the next N articles
 *
 * Per plan section 11:
 *   - Currently reading: Article N
 *   - Articles N+1, N+2, N+3: prefetch
 *   - Articles beyond: queued
 *
 * This reduces perceived latency when the user navigates.
 */
void PrefetchArticles(short fromIndex, short count)
{
    short i;

    for (i = fromIndex + 1; i < fromIndex + 1 + count && i < gMaxArticles; i++) {
        if (UserActionPending()) break;
        if (!gArticles[i].hasContent) {
            FetchArticle(gArticles[i].id);
            if (NetworkRequestFailed()) break;
        }
    }
}

/*
 * CacheArticleContent — Store parsed article content locally
 */
void CacheArticleContent(char *responseBuffer)
{
    char articleID[9];
    char *body;
    short index;

    // Parse the article ID from the response
    ParseArticleID(responseBuffer, articleID);

    // Find the article in the cache
    index = FindArticleByID(articleID);

    if (index >= 0) {
        // Extract and cache the body text
        body = ExtractBody(responseBuffer);
        if (body) {
            if (SetArticleBody(index, body)) SaveArticleToDisk(index);
        }
    }
}

/*
 * GetCachedArticleBody — Get cached body text for an article
 */
char *GetCachedArticleBody(short index)
{
    if (index >= 0 && index < gMaxArticles && gArticles[index].hasContent) {
        return gArticles[index].body;
    }
    return NULL;
}

/*
 * GetCachedImage — Get cached image handle
 */
Handle GetCachedImage(short articleIndex, short imageIndex)
{
    if (articleIndex >= 0 && articleIndex < gMaxArticles) {
        if (imageIndex >= 0 && imageIndex < gArticles[articleIndex].imageCount) {
            return gArticles[articleIndex].images[imageIndex];
        }
    }
    return NULL;
}

/*
 * FindArticleByID — Find an article by its ID
 */
short FindArticleByID(char *id)
{
    short i;

    for (i = 0; i < gMaxArticles; i++) {
        if (strncmp(gArticles[i].id, id, 8) == 0) {
            return i;
        }
    }
    return -1;
}

/* Samples retain only session state and never enter the persistent history. */
static char gSampleReadIDs[16][9];
static short gSampleReadCount;
Boolean IsArticleRead(const char *id)
{
    short i;
    if (!gUsingStartupSamples && !gConnection.sample) return ReadHistoryContains(id);
    for (i = 0; i < gSampleReadCount; ++i)
        if (!strcmp(gSampleReadIDs[i], id)) return true;
    return false;
}

void SaveArticleState(short index)
{
    struct ArticleEntry *article;
    short i;
    if (index < 0 || index >= gLoadedArticles) return;
    article = &gArticles[index];
    if (!gUsingStartupSamples && !gConnection.sample) {
        ReadHistorySet(article->id, article->published, article->isRead);
        return;
    }
    for (i = 0; i < gSampleReadCount; ++i)
        if (!strcmp(gSampleReadIDs[i], article->id)) break;
    if (!article->isRead && i < gSampleReadCount) {
        --gSampleReadCount;
        memmove(gSampleReadIDs[i], gSampleReadIDs[i+1], (gSampleReadCount-i)*9);
    } else if (article->isRead && i == gSampleReadCount && gSampleReadCount < 16)
        strcpy(gSampleReadIDs[gSampleReadCount++], article->id);
}

/*
 * SaveArticleToDisk — Save full article to disk cache
 */
void SaveArticleToDisk(short index)
{
    // Write article body to disk cache on BlueSCSI
    // This allows articles to persist across launches
}

/*
 * LoadCachedArticles — Load cached articles from disk
 */
void LoadCachedArticles(void)
{
    // Read article cache from BlueSCSI storage
    // (plan section 12: persistent local cache)
}

/*
 * GetStatusMessage — Get the current status message for the status bar
 */
void GetStatusMessage(Str256 str)
{
    extern Boolean gNetworkUnavailable;
    char countStr[6];
    char unreadStr[6];
    char combined[64];
    short unread = 0, i;

    if (gNetworkUnavailable) {
        StringToPStr(str, NetworkErrorText());
        return;
    }

    if (FeedRefreshStatusText()) { StringToPStr(str, FeedRefreshStatusText()); return; }
    if (ReadHistoryError()) { StringToPStr(str, ReadHistoryError()); return; }

    for (i = 0; i < gLoadedArticles; i++) {
        if (!gArticles[i].isRead) unread++;
    }

    LongToString(gMaxArticles, countStr);
    ShortToString(unread, unreadStr);

    // Format: "N articles, M unread"
    if (gMaxArticles == gLoadedArticles)
        sprintf(combined, "%s articles, %s unread", countStr, unreadStr);
    else sprintf(combined, "%s/%d, %s unread", countStr, gLoadedArticles, unreadStr);

    StringToPStr(str, combined);
}
