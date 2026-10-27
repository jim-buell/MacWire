#ifndef RSS_TYPOGRAPHY_H
#define RSS_TYPOGRAPHY_H

#include "compat.h"

enum {
    kTypeReaderTitle = 0,
    kTypeBody = 1,
    kTypeMetadata = 2,
    kTypeFeed = 3,
    kTypeListTitle = 4,
    kTypeCount = 5
};

typedef struct {
    short font;
    short size;
    unsigned char face;
    unsigned char reserved;
} TypographySetting;

extern TypographySetting gTypography[kTypeCount];
extern short gListTitleLines;
extern void InitTypography(void);
extern void ApplyTypography(short role);
extern void ShowTypographyDialog(void);
extern short GetBodyLineHeight(void);
extern void GetTypographyFontInfo(short role, FontInfo *info);
extern short GetTypographyLineHeight(short role);

#endif
