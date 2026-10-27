/*
 * articles.h — Article cache layout shared between Articles.c and Protocol.c
 */

#ifndef RSS_ARTICLES_H
#define RSS_ARTICLES_H

#include <MacTypes.h>
#include "Wire.h"

/* Transfer/sample sizing; the live metadata list is allocated to fit its view. */
#define kMaxArticles       50
#define kMaxTitleLength    511
#define kMaxFeedLength     32
#define kMaxAuthorLength   255
#define kMaxSummaryLength  200
#define kMaxBodyLength     10000
#define kMaxImagesPerArticle 5

/*
 * Article cache entry
 *
 * Each entry stores the article ID, list metadata, summary, full body
 * text (cached when the article is opened), image bitmaps, and the
 * local read state.
 */
struct ArticleEntry {
    char id[9];                // 8-char hex ID + null
    char title[kMaxTitleLength + 1]; // Full reader title; list wraps 1-4 lines
    char author[kMaxAuthorLength + 1]; // Available after fetching the article
    char feed[33];             // Feed name
    char time[24];             // Compact local date/time with AM/PM
    char date[32];             // Publication date and time, local time
    char summary[201];         // Truncated summary
    Boolean fullContentAvailable; // Server reports a body for this article
    Boolean hasContent;        // Has full content cached?
    Boolean isRead;            // Marked as read (local)?
    double published;         // UTC publication time for sorting
    unsigned short imageOffsets[kMaxImagesPerArticle]; // Body character offsets; 0xFFFF for attachments
    short imageCount;          // Number of images
    char *body;                // Allocated only after this article is fetched
    WireLinks *links;          // Kept only with its bounded body cache
    Handle images[kMaxImagesPerArticle]; // Image bitmaps (cached)
    unsigned char imageState[kMaxImagesPerArticle]; // 0 pending, 1 loaded, 2 unavailable
};

typedef struct { short width, height, rowBytes; unsigned char bits[1]; } CachedImage;
void ReleaseArticleImages(short index);

/* Article cache (defined in Articles.c) */
extern struct ArticleEntry *gArticles;
extern short gArticleCapacity;
extern Boolean gNewestFirst;
Boolean AllocateArticleList(unsigned long count);
void SortArticles(void);
void ToggleArticleSort(void);
extern short gMaxArticles;       // Visible prefix of the sorted metadata array
extern short gLoadedArticles;    // Includes rows hidden by read/unread filtering
void SetArticleVisibility(unsigned short visibility);
void FinishReadingSelected(void);
extern Boolean gUsingStartupSamples;
void ResetStartupSamples(void);

/* Articles.c entry points */
void InitArticles(void);
void SelectArticle(short index);
void EnsureArticleVisible(short index);
void SelectPreviousArticle(void);
void SelectNextArticle(void);
short FindNextUnreadArticle(void);
void SelectNextUnreadArticle(void);
void ScrollArticle(short direction);
void AdvanceToNextArticle(void);
void OpenSelectedArticle(void);
void ToggleMarkReadSelected(void);
void MarkAllArticlesRead(void);
Boolean HasUnreadArticlesInFeed(void);
Boolean IsArticleRead(const char *id);
void PrefetchArticles(short fromIndex, short count);
Boolean SetArticleBody(short index, const char *body);
Boolean SetArticleLinks(short index, const WireLinks *links);
void ClearArticleBodies(void);
short FindArticleByID(char *id);
char *GetCachedArticleBody(short index);
Handle GetCachedImage(short articleIndex, short imageIndex);
void GetStatusMessage(Str256 str);

/* Protocol.c entry points used by the network layer */
short ParseIndexResponse(const unsigned char *response, unsigned long length);
short ParsePageResponse(const unsigned char *response, unsigned long length,
                        char view[9], unsigned long *total, unsigned long *offset, short *count);
short ParseDatesResponse(const unsigned char *response, unsigned long length);
short ParseArticleResponse(const unsigned char *response, unsigned long length);
short ParseImageResponse(const unsigned char *response, unsigned long length);
short ParseStatusResponse(const unsigned char *response, unsigned long length,
                           short *articleCount);
void TimestampToString(double timestamp, char *str);
void TimestampToDate(double timestamp, char *str);

#endif /* RSS_ARTICLES_H */
