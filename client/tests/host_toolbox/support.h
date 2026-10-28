#pragma once
/* Host replacements for Toolbox/UI calls only. Articles.c and Protocol.c
   compile unchanged, with their real allocation, paging and sorting logic. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MacTypes.h"
#include "Memory.h"
#include "articles.h"
#define true 1
#define false 0
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
extern short gSelectedIndex, gScrollOffset;
extern long gContentScroll;
void ClearReaderSelection(void);
void InvalidateReaderLayout(void);
void UpdateArticleList(void);
void FinishReaderArticleLoading(void);
void UpdateArticleRow(short index);
void UpdateArticleReadIndicator(short index);
void UpdateContentPane(void);
void DrawContentPane(void);
void UpdateScrollBars(void);
void UpdateStatusLine(void);
short GetVisibleListRows(void);
short GetContentViewportHeight(void);
short GetReaderImageWidth(void);
void SyncWithServer(void);
void ScrollList(int direction);
void ScrollReaderTo(long offset);
void FetchArticle(char *id);
void FetchImage(char *id, short index);
int NetworkRequestFailed(void);
const char *NetworkErrorText(void);
void ParseArticleID(char *packet, char *id);
char *ExtractBody(char *packet);
void StringToPStr(unsigned char *result, const char *text);
void LongToString(long value, char *result);
void ShortToString(short value, char *result);
long ReaderImageUpdateTop(short index);
void ReaderImageLoaded(long top);

unsigned long TickCount(void);
void RefreshFeedsFromServer(void);

void PreviewArticleSelection(short index);

short NextArticleImage(short index);

/* Host Menu Manager for the actual Feeds.c implementation. */
typedef void *MenuHandle;
typedef const unsigned char *ConstStringPtr;
typedef unsigned char Str255[256];
MenuHandle GetMenuHandle(short id);
short CountMItems(MenuHandle menu);
void DeleteMenuItem(MenuHandle menu, short item);
void AppendMenu(MenuHandle menu, ConstStringPtr text);
void SetMenuItemText(MenuHandle menu, short item, ConstStringPtr text);
void CheckItem(MenuHandle menu, short item, Boolean checked);

void SyncStartupSamples(void);
