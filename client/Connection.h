#ifndef TICKERTAPE_CONNECTION_H
#define TICKERTAPE_CONNECTION_H
#include <stdint.h>

typedef struct {
    char address[16];
    unsigned short port;
    unsigned short sample;
    unsigned short windowHours;
} ConnectionSettings;
extern ConnectionSettings gConnection;
#define CONNECTION_FILE_SIZE 24
int ConnectionDecodeSettings(const unsigned char *data, unsigned long length, ConnectionSettings *settings);
void ConnectionEncodeSettings(const ConnectionSettings *settings, unsigned char data[CONNECTION_FILE_SIZE]);
int ConnectionAddress(const char *text, uint32_t *address);
int ConnectionPort(const char *text, unsigned short *port);
void InitConnectionSettings(void);
int SaveConnectionSettings(void);
int TestBackendConnection(const ConnectionSettings *settings, short *articles);
const char *NetworkErrorText(void);
int NetworkRequestFailed(void);
int NetworkPoll(void);
void RefreshFeedsFromServer(void);
const char *FeedRefreshStatusText(void);
int QueueArticleNavigation(short direction);
int QueueArticleSelection(short index);
int ArticleNavigationPending(void);
int DeferredActionPending(void);
int UserActionPending(void);
void ApplyDeferredAction(void);
int PollReaderAction(void);
void PrefetchIdleArticle(void);
void ClearCancelledRequest(void);
extern int gIndexLoading;
void BeginNetworkWait(void);
void EndNetworkWait(void);
void ApplyPendingArticleNavigation(void);
#endif
