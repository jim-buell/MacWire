#include "MacTypes.h"
Ptr NewPtr(long size);
Ptr NewPtrClear(long size);
void DisposePtr(Ptr pointer);
Handle NewHandleClear(long size);
void DisposeHandle(Handle handle);
void HLock(Handle handle);
void HUnlock(Handle handle);
void BlockMove(const void *source, void *destination, long size);
