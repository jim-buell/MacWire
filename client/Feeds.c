#include "compat.h"
#include "articles.h"
#include "Connection.h"
#include "Feeds.h"
#include "ListPosition.h"
#include "MenuCommands.h"

char gFeedFilter[33];
char gFeedNames[WIRE_MAX_FEEDS][33];
short gFeedCount;

void UpdateFeedsMenu(void)
{
    MenuHandle menu = GetMenuHandle(kFeedsMenu);
    short i, j;
    Str255 name;
    if (!menu) return;
    /* Also include cached sources, including older servers without /feeds. */
    for (i = 0; i < gLoadedArticles; ++i) {
        for (j = 0; j < gFeedCount; ++j)
            if (!strcmp(gFeedNames[j], gArticles[i].feed)) break;
        if (j == gFeedCount && gFeedCount < WIRE_MAX_FEEDS && gArticles[i].feed[0])
            strcpy(gFeedNames[gFeedCount++], gArticles[i].feed);
    }
    while (CountMItems(menu) >= kFeedsFirstSource) DeleteMenuItem(menu, CountMItems(menu));
    CheckItem(menu, kFeedsAll, gFeedFilter[0] == 0);
    for (i = 0; i < gFeedCount; ++i) {
        AppendMenu(menu, (ConstStringPtr)"\001 ");
        StringToPStr(name, gFeedNames[i]); SetMenuItemText(menu, i + kFeedsFirstSource, name);
        CheckItem(menu, i + kFeedsFirstSource, !strcmp(gFeedFilter, gFeedNames[i]));
    }
}

void SelectFeed(short item)
{
    char previous[33];
    if (item != kFeedsAll && (item < kFeedsFirstSource || item >= gFeedCount + kFeedsFirstSource)) return;
    strcpy(previous, gFeedFilter);
    CancelListPositionRestore();
    if (item == kFeedsAll) gFeedFilter[0] = 0;
    else strcpy(gFeedFilter, gFeedNames[item - kFeedsFirstSource]);
    /* Acknowledge the released menu choice before any network request. */
    UpdateFeedsMenu();
    /* Startup placeholders belong to the local cache even if a configured
       server was unreachable. Refresh still attempts the configured server. */
    if (gUsingStartupSamples && !gConnection.sample) SyncStartupSamples();
    else SyncFeedWithServer();
    if (!gLastSyncSucceeded) strcpy(gFeedFilter, previous);
    UpdateFeedsMenu();
}
