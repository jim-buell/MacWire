/* Persistent backend settings and cooperative networking UI. */
#include "compat.h"
#include "Connection.h"
#include "WaitCursor.h"

ConnectionSettings gConnection = { "", 8080,
#ifdef RSS_DEMO_TRANSPORT
    1,
#else
    0,
#endif
    24
};
DialogPtr gConnectionDialog;
Boolean gConnectionCloseRequested;
static const unsigned char prefsName[] = "\017MacWire Network";

static Boolean Spec(FSSpec *spec)
{
    short volume;
    long folder;
    OSErr error;
    if (FindFolder(-32768, kPreferencesFolderType, true, &volume, &folder) != noErr)
        return false;
    error = FSMakeFSSpec(volume, folder, prefsName, spec);
    return error == noErr || error == fnfErr;
}

void InitConnectionSettings(void)
{
    FSSpec spec;
    short refNum;
    long count = CONNECTION_FILE_SIZE;
    unsigned char file[CONNECTION_FILE_SIZE];
    ConnectionSettings settings;
    OSErr error;
    if (!Spec(&spec) || FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) return;
    error = FSRead(refNum, &count, file);
    if ((error == noErr || error == eofErr) &&
        ConnectionDecodeSettings(file, count, &settings)) gConnection = settings;
    FSClose(refNum);
}

int SaveConnectionSettings(void)
{
    FSSpec spec;
    short refNum;
    long count = CONNECTION_FILE_SIZE;
    unsigned char file[CONNECTION_FILE_SIZE];
    OSErr error;
    if (!Spec(&spec)) return 0;
    error = FSpOpenDF(&spec, fsRdWrPerm, &refNum);
    if (error == fnfErr) {
        if (FSpCreate(&spec, 'RSSR', 'pref', 0) != noErr) return 0;
        error = FSpOpenDF(&spec, fsRdWrPerm, &refNum);
    }
    if (error != noErr) return 0;
    ConnectionEncodeSettings(&gConnection, file);
    error = FSWrite(refNum, &count, &file);
    if (error == noErr && count == sizeof(file)) error = SetEOF(refNum, count);
    FSClose(refNum);
    return error == noErr && count == sizeof(file);
}
