/* Small, versioned preference file; article IDs survive new arrivals and sorting. */
#include "compat.h"
#include "articles.h"
#include "Connection.h"
#include "Feeds.h"
#include "ListPosition.h"
#include <Files.h>
#include <stdint.h>
#include <string.h>

#define POSITION_SIZE 80
static unsigned char saved[POSITION_SIZE];
static Boolean pending;
extern short gSelectedIndex, gScrollOffset;
static const unsigned char mainName[] = "\020MacWire Position";
static const unsigned char tempName[] = "\025MacWire Position Temp";
static const unsigned char backupName[] = "\027MacWire Position Backup";
static uint32_t get32(const unsigned char *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void put32(unsigned char *p, uint32_t value)
{ p[0]=value>>24; p[1]=value>>16; p[2]=value>>8; p[3]=value; }
static uint32_t checksum(const unsigned char *data)
{
    uint32_t hash=2166136261UL; short i;
    for (i=0; i<76; ++i) hash=(hash^data[i])*16777619UL;
    return hash;
}
static Boolean specFor(FSSpec *spec, const unsigned char *name)
{
    short volume; long folder; OSErr error;
    if (FindFolder(-32768,kPreferencesFolderType,true,&volume,&folder)!=noErr) return false;
    error=FSMakeFSSpec(volume,folder,name,spec);
    return error==noErr || error==fnfErr;
}
static Boolean validAnchor(const unsigned char *anchor)
{
    short i;
    if (!anchor[0]) return true;
    for (i=0; i<8; ++i)
        if (!((anchor[i]>='0' && anchor[i]<='9') || (anchor[i]>='a' && anchor[i]<='f'))) return false;
    return true;
}
static Boolean loadFile(const unsigned char *name)
{
    FSSpec spec; short ref; long length, actual=POSITION_SIZE;
    OSErr error;
    if (!specFor(&spec,name) || FSpOpenDF(&spec,fsRdPerm,&ref)!=noErr) return false;
    error=GetEOF(ref,&length);
    if (error==noErr && length==POSITION_SIZE) error=FSRead(ref,&actual,saved);
    else error=ioErr;
    if (FSClose(ref)!=noErr) error=ioErr;
    return error==noErr && actual==POSITION_SIZE && !memcmp(saved,"MWPS",4) &&
        saved[4]==1 && saved[5]<=1 && saved[6]==0 && saved[7]==0 &&
        saved[64]==0 && validAnchor(saved+8) && validAnchor(saved+20) &&
        get32(saved+76)==checksum(saved);
}
void LoadListPosition(void)
{
    pending=false;
    if (gConnection.sample) return;
    if (!loadFile(mainName) && !loadFile(backupName) && !loadFile(tempName)) return;
    pending=true;
    gNewestFirst=saved[5];
    memcpy(gFeedFilter,saved+32,33);
}
void CancelListPositionRestore(void) { pending=false; }
void ValidateSavedFeed(void)
{
    short i;
    if (!pending || !gFeedFilter[0]) return;
    for (i=0; i<gFeedCount; ++i)
        if (!strcmp(gFeedNames[i],gFeedFilter)) return;
    gFeedFilter[0]=0;
}
static short findAnchor(const unsigned char *anchor)
{
    char id[9]; short i, nearest=-1; double distance, best=0, timestamp;
    if (!anchor[0]) return -1;
    memcpy(id,anchor,8); id[8]=0;
    timestamp=get32(anchor+8);
    for (i=0; i<gMaxArticles; ++i) {
        if (!strcmp(gArticles[i].id,id)) return i;
        distance=gArticles[i].published-timestamp;
        if (distance<0) distance=-distance;
        if (nearest<0 || distance<best) { nearest=i; best=distance; }
    }
    return nearest;
}
int RestoreListPosition(void)
{
    short selected, top;
    if (!pending) return 0;
    pending=false;
    selected=findAnchor(saved+8); top=findAnchor(saved+20);
    /* Set the viewport before fetching the selected body: cooperative update
       events during that request must already see the restored list position. */
    if (top>=0) gScrollOffset=top;
    if (selected>=0) SelectArticle(selected);
    /* A saved blank reader stays blank. Selection fetching may be interrupted,
       so let queued arrow navigation take priority over restoring the viewport. */
    if (!ArticleNavigationPending()) {
        if (top>=0) gScrollOffset=top;
        /* New arrivals, expired rows, or a smaller window can put the saved
           top row outside the selected article's viewport. Selection wins. */
        if (gSelectedIndex>=0) EnsureArticleVisible(gSelectedIndex);
    }
    UpdateScrollBars(); UpdateArticleList(); UpdateContentPane();
    return 1;
}
static void saveAnchor(unsigned char *anchor, short index)
{
    double published;
    if (index<0 || index>=gMaxArticles) return;
    memcpy(anchor,gArticles[index].id,8);
    published=gArticles[index].published;
    put32(anchor+8,published>0 && published<=4294967295.0 ? (uint32_t)published : 0);
}
int SaveListPosition(void)
{
    unsigned char data[POSITION_SIZE];
    FSSpec main, temp, backup; short ref; long actual=POSITION_SIZE;
    OSErr error, exists;
    /* Don't overwrite a saved place after an unsuccessful startup download. */
    if (pending || gConnection.sample || gUsingStartupSamples || !gLastSyncSucceeded) return 0;
    memset(data,0,sizeof(data)); memcpy(data,"MWPS",4); data[4]=1; data[5]=gNewestFirst;
    saveAnchor(data+8,gSelectedIndex); saveAnchor(data+20,gScrollOffset);
    memcpy(data+32,gFeedFilter,33); put32(data+76,checksum(data));
    if (!specFor(&main,mainName) || !specFor(&temp,tempName) || !specFor(&backup,backupName)) return 0;
    error=FSpOpenDF(&temp,fsRdWrPerm,&ref);
    if (error==fnfErr) {
        error=FSpCreate(&temp,'RSSR','pref',0);
        if (error==noErr) error=FSpOpenDF(&temp,fsRdWrPerm,&ref);
    }
    if (error!=noErr) return 0;
    error=FSWrite(ref,&actual,data);
    if (error==noErr && actual==POSITION_SIZE) error=SetEOF(ref,POSITION_SIZE);
    if (FSClose(ref)!=noErr) error=ioErr;
    if (error!=noErr || actual!=POSITION_SIZE || FlushVol(NULL,temp.vRefNum)!=noErr) return 0;
    exists=FSpOpenDF(&main,fsRdPerm,&ref);
    if (exists==noErr) {
        FSClose(ref);
        error=FSpDelete(&backup);
        if (error!=noErr && error!=fnfErr) return 0;
        if (FSpRename(&main,backupName)!=noErr) return 0;
    } else if (exists!=fnfErr) return 0;
    if (FSpRename(&temp,mainName)!=noErr) {
        if (exists==noErr) FSpRename(&backup,mainName);
        return 0;
    }
    if (FlushVol(NULL,main.vRefNum)!=noErr) return 0;
    FSpDelete(&backup);
    return 1;
}
