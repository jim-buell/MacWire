#ifndef MACWIRE_READ_HISTORY_H
#define MACWIRE_READ_HISTORY_H
#include "ReadState.h"
extern unsigned short gArticleVisibility;
void InitReadHistory(void);
int SaveReadHistory(void);
int ReadHistoryContains(const char *id);
void ReadHistorySet(const char *id, double published, int read);
void ReadHistoryObserve(double published);
void ReadHistorySetVisibility(unsigned short visibility);
const char *ReadHistoryError(void);
#endif
