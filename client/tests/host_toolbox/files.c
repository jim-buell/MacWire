/* File Manager test shim backed by real temporary files. */
#include "Files.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char prefsDirectory[1024];
int testFailWrite, testFailRename, testRenameCount, testFailFlush;
void TestSetPrefsDirectory(const char *path) {
    snprintf(prefsDirectory,sizeof(prefsDirectory),"%s",path);
    testFailWrite=testFailRename=testRenameCount=testFailFlush=0;
}
static void path_for(const FSSpec *spec, char path[1200]) {
    snprintf(path,1200,"%s/%.*s",prefsDirectory,spec->name[0],spec->name+1);
}
static OSErr error_for(int result) { return result>=0 ? noErr : errno==ENOENT ? fnfErr : ioErr; }
OSErr FindFolder(short volume, long kind, unsigned char create, short *foundVolume, long *folder) {
    (void)volume; (void)kind; (void)create;
    *foundVolume=1; *folder=1; return prefsDirectory[0] ? noErr : ioErr;
}
OSErr FSMakeFSSpec(short volume, long folder, const unsigned char *name, FSSpec *spec) {
    char path[1200]; struct stat info;
    spec->vRefNum=volume; spec->parID=folder; memcpy(spec->name,name,name[0]+1);
    path_for(spec,path); return error_for(stat(path,&info));
}
OSErr FSpOpenDF(const FSSpec *spec, unsigned char mode, short *ref) {
    char path[1200]; int descriptor;
    path_for(spec,path); descriptor=open(path,mode==fsRdPerm ? O_RDONLY : O_RDWR);
    if (descriptor>=0) *ref=descriptor;
    return error_for(descriptor);
}
OSErr FSpCreate(const FSSpec *spec, uint32_t creator, uint32_t type, short script) {
    char path[1200]; int descriptor;
    (void)creator; (void)type; (void)script;
    path_for(spec,path); descriptor=open(path,O_CREAT|O_EXCL|O_RDWR,0600);
    if (descriptor>=0) close(descriptor);
    return error_for(descriptor);
}
OSErr FSRead(short ref, long *count, void *data) {
    long expected=*count; ssize_t actual=read(ref,data,expected);
    if (actual<0) return ioErr;
    *count=actual; return actual==expected ? noErr : eofErr;
}
OSErr FSWrite(short ref, long *count, const void *data) {
    ssize_t actual=write(ref,data,testFailWrite ? *count/2 : *count);
    if (actual<0) return ioErr;
    *count=actual; return noErr;
}
OSErr GetEOF(short ref, long *length) {
    struct stat info; if (fstat(ref,&info)<0) return ioErr; *length=info.st_size; return noErr;
}
OSErr SetEOF(short ref, long length) { return error_for(ftruncate(ref,length)); }
OSErr FSClose(short ref) { return error_for(close(ref)); }
OSErr FlushVol(const unsigned char *name, short volume) {
    (void)name; (void)volume; return testFailFlush ? ioErr : noErr;
}
OSErr FSpDelete(const FSSpec *spec) { char path[1200]; path_for(spec,path); return error_for(unlink(path)); }
OSErr FSpRename(const FSSpec *spec, const unsigned char *name) {
    char source[1200], target[1200]; FSSpec destination=*spec;
    ++testRenameCount;
    if (testFailRename && testRenameCount==testFailRename) return ioErr;
    memcpy(destination.name,name,name[0]+1);
    path_for(spec,source); path_for(&destination,target);
    return error_for(rename(source,target));
}
