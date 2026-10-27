/* Local read IDs and visibility preference, saved through a recoverable rename. */
#include "compat.h"
#include "Connection.h"
#include "articles.h"
#include "ReadHistory.h"
#include <Files.h>

unsigned short gArticleVisibility = kShowAllArticles;
static ReadRecord *gReadRecords;
static size_t gReadCount, gReadCapacity;
static uint32_t gReadWatermark;
static Boolean gReadDirty, gReadWritable = true, gReadMutationFailed;
static const char *gReadError;
static const unsigned char mainName[] = "\015MacWire Reads";
static const unsigned char tempName[] = "\022MacWire Reads Temp";
static const unsigned char backupName[] = "\024MacWire Reads Backup";

const char *ReadHistoryError(void) { return gReadError; }
static Boolean ReadSpec(FSSpec *spec, const unsigned char *name)
{
    short volume; long folder; OSErr error;
    if (FindFolder(-32768,kPreferencesFolderType,true,&volume,&folder)!=noErr) return false;
    error=FSMakeFSSpec(volume,folder,name,spec);
    return error==noErr || error==fnfErr;
}
static Boolean ReserveReadRecords(size_t needed)
{
    ReadRecord *next; size_t capacity;
    if (needed<=gReadCapacity) return true;
    if (needed>0x7fffffffUL/sizeof(*next)-128) return false;
    capacity=((needed+127)/128)*128;
    next=(ReadRecord *)NewPtr(capacity*sizeof(*next));
    if (!next) return false;
    if (gReadCount) BlockMove(gReadRecords,next,gReadCount*sizeof(*next));
    if (gReadRecords) DisposePtr((Ptr)gReadRecords);
    gReadRecords=next; gReadCapacity=capacity;
    return true;
}
static int LoadFile(const unsigned char *name)
{
    FSSpec spec; short ref; long length, actual;
    unsigned char *data; size_t count; unsigned short visibility; uint32_t watermark;
    OSErr error; int valid;
    if (!ReadSpec(&spec,name)) return -1;
    error=FSpOpenDF(&spec,fsRdPerm,&ref);
    if (error==fnfErr) return 0;
    if (error!=noErr) return -1;
    if (GetEOF(ref,&length)!=noErr || length<READ_STATE_HEADER_SIZE) { FSClose(ref); return -1; }
    data=(unsigned char *)NewPtr(length);
    if (!data) { FSClose(ref); return -1; }
    actual=length; error=FSRead(ref,&actual,data);
    valid=error==noErr && actual==length;
    if (FSClose(ref)!=noErr) valid=0;
    if (valid) valid=ReadStateHeader(data,length,&count,&visibility,&watermark) && ReserveReadRecords(count);
    if (valid) valid=ReadStateDecode(data,length,gReadRecords,gReadCapacity,&gReadCount,
                                   &gArticleVisibility,&gReadWatermark);
    DisposePtr((Ptr)data);
    return valid ? 1 : -1;
}
void InitReadHistory(void)
{
    int primary, backup, temporary;
    if (gReadRecords) DisposePtr((Ptr)gReadRecords);
    gReadRecords=NULL; gReadCount=gReadCapacity=0; gReadWatermark=0;
    gArticleVisibility=kShowAllArticles; gReadDirty=false; gReadWritable=true; gReadMutationFailed=false; gReadError=NULL;
    primary=LoadFile(mainName);
    if (primary==1) return;
    backup=LoadFile(backupName);
    if (backup==1) { gReadDirty=true; return; }
    temporary=LoadFile(tempName);
    if (temporary==1) { gReadDirty=true; return; }
    if (primary<0 || backup<0 || temporary<0) {
        gReadWritable=false;
        gReadError="Saved read state could not be loaded; it will not be overwritten.";
    }
}
int ReadHistoryContains(const char *id)
{ return ReadStateFind(gReadRecords,gReadCount,id,NULL); }
void ReadHistoryObserve(double published)
{
    uint32_t timestamp;
    if (gUsingStartupSamples || gConnection.sample || !(published>0 && published<=4294967295.0)) return;
    timestamp=(uint32_t)published;
    if (timestamp>gReadWatermark) {
        gReadWatermark=timestamp;
        ReadStatePrune(gReadRecords,&gReadCount,gReadWatermark);
        gReadDirty=true;
    }
}
void ReadHistorySet(const char *id, double published, int read)
{
    uint32_t timestamp;
    if (gUsingStartupSamples || gConnection.sample || !(published>0 && published<=4294967295.0)) return;
    ReadHistoryObserve(published); timestamp=(uint32_t)published;
    if ((read && !ReadHistoryContains(id) && !ReserveReadRecords(gReadCount+1)) ||
        !ReadStateSet(gReadRecords,&gReadCount,gReadCapacity,id,timestamp,read)) {
        gReadMutationFailed=true;
        gReadError="Not enough memory to remember read state.";
        return;
    }
    gReadMutationFailed=false;
    gReadDirty=true;
}
void ReadHistorySetVisibility(unsigned short visibility)
{
    if (visibility>kShowReadArticles || visibility==gArticleVisibility) return;
    gArticleVisibility=visibility; gReadDirty=true;
}
int SaveReadHistory(void)
{
    FSSpec main, temp, backup; short ref; long length, actual;
    unsigned char *data; OSErr error, exists;
    if (!gReadWritable) return 0;
    if (!gReadDirty) return !gReadMutationFailed;
    if (!ReadSpec(&main,mainName) || !ReadSpec(&temp,tempName) || !ReadSpec(&backup,backupName)) goto failed;
    if (gReadCount>(0x7fffffffUL-READ_STATE_HEADER_SIZE)/READ_STATE_RECORD_SIZE) goto failed;
    length=READ_STATE_HEADER_SIZE+gReadCount*READ_STATE_RECORD_SIZE;
    data=(unsigned char *)NewPtr(length);
    if (!data) goto failed;
    if (!ReadStateEncode(gReadRecords,gReadCount,gArticleVisibility,gReadWatermark,data,length)) {
        DisposePtr((Ptr)data); goto failed;
    }
    error=FSpOpenDF(&temp,fsRdWrPerm,&ref);
    if (error==fnfErr) {
        error=FSpCreate(&temp,'RSSR','pref',0);
        if (error==noErr) error=FSpOpenDF(&temp,fsRdWrPerm,&ref);
    }
    if (error!=noErr) { DisposePtr((Ptr)data); goto failed; }
    actual=length; error=FSWrite(ref,&actual,data); DisposePtr((Ptr)data);
    if (error==noErr && actual==length) error=SetEOF(ref,length);
    if (FSClose(ref)!=noErr) error=ioErr;
    if (error!=noErr || actual!=length || FlushVol(NULL,temp.vRefNum)!=noErr) goto failed;
    /* A crash between these renames leaves a complete backup or temp file.
       Never delete the previous good data before the replacement is complete. */
    error=FSpOpenDF(&main,fsRdPerm,&ref); exists=error;
    if (error==noErr) FSClose(ref);
    else if (error!=fnfErr) goto failed;
    if (exists==noErr) {
        error=FSpDelete(&backup);
        if (error!=noErr && error!=fnfErr) goto failed;
        if (FSpRename(&main,backupName)!=noErr) goto failed;
    }
    if (FSpRename(&temp,mainName)!=noErr) {
        if (exists==noErr) FSpRename(&backup,mainName);
        goto failed;
    }
    if (FlushVol(NULL,main.vRefNum)!=noErr) goto failed;
    FSpDelete(&backup);
    gReadDirty=false;
    if (!gReadMutationFailed) gReadError=NULL;
    return !gReadMutationFailed;
failed:
    gReadError="Read state could not be saved. Check disk space and permissions.";
    return 0;
}
