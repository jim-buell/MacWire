#ifndef MACWIRE_HOST_FILES
#define MACWIRE_HOST_FILES
#include <stdint.h>
typedef short OSErr;
typedef struct { short vRefNum; long parID; unsigned char name[64]; } FSSpec;
#define noErr 0
#define fnfErr -43
#define eofErr -39
#define ioErr -36
#define fsRdPerm 1
#define fsRdWrPerm 3
#define kPreferencesFolderType 0
OSErr FindFolder(short volume, long kind, unsigned char create, short *foundVolume, long *folder);
OSErr FSMakeFSSpec(short volume, long folder, const unsigned char *name, FSSpec *spec);
OSErr FSpOpenDF(const FSSpec *spec, unsigned char mode, short *ref);
OSErr FSpCreate(const FSSpec *spec, uint32_t creator, uint32_t type, short script);
OSErr FSRead(short ref, long *count, void *data);
OSErr FSWrite(short ref, long *count, const void *data);
OSErr GetEOF(short ref, long *length);
OSErr SetEOF(short ref, long length);
OSErr FSClose(short ref);
OSErr FlushVol(const unsigned char *name, short volume);
OSErr FSpDelete(const FSSpec *spec);
OSErr FSpRename(const FSSpec *spec, const unsigned char *name);
void TestSetPrefsDirectory(const char *path);
#endif
