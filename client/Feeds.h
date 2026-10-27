#ifndef TICKERTAPE_FEEDS_H
#define TICKERTAPE_FEEDS_H
#include "Wire.h"
extern char gFeedFilter[33];
extern char gFeedNames[WIRE_MAX_FEEDS][33];
extern short gFeedCount;
void UpdateFeedsMenu(void);
void SelectFeed(short item);
extern int gLastSyncSucceeded;
#endif
