/* System 7 MacTCP Device Manager ABI, from Apple's MacTCP Programmer's Guide
 * (1989), chapter 4. No SDK object library or Open Transport is required.
 * All driver-owned storage stays fixed until pending operations are aborted
 * and the stream is released. UI polling permits repainting and cancellation. */
#include "compat.h"
#include "Connection.h"
#include "MacTCPTransport.h"
#include "HTTPResponse.h"
#include <Devices.h>
#include <stddef.h>

enum { kTCPCreate = 30, kTCPActiveOpen = 32, kTCPSend = 34,
       kTCPReceive = 37, kTCPAbort = 39, kTCPRelease = 42 };

typedef struct {
    Ptr qLink;
    short qType, ioTrap;
    Ptr ioCmdAddr, ioCompletion;
    volatile short ioResult;
    StringPtr ioNamePtr;
    short ioVRefNum, ioCRefNum, csCode;
    Ptr stream;
    union {
        struct { Ptr buffer; unsigned long length; Ptr notify, user; } create;
        struct {
            unsigned char timeout, action, flags, commandTimeout;
            uint32_t remoteHost; unsigned short remotePort;
            uint32_t localHost; unsigned short localPort;
            unsigned char tos, precedence, dontFragment, ttl, security, optionCount;
            unsigned char options[40]; Ptr user;
        } open;
        struct {
            unsigned char timeout, action, flags, push, urgent;
            Ptr wds; unsigned long free; unsigned short length; Ptr user;
        } send;
        struct {
            unsigned char timeout, filler, mark, urgent;
            Ptr buffer; unsigned short length;
            Ptr rds; unsigned short rdsLength, timestamp; Ptr user;
        } receive;
        unsigned char reserved[96];
    } param;
} TCPBlock;
typedef struct { unsigned short length; Ptr data; } TCPWDS;

_Static_assert(offsetof(TCPBlock, ioResult) == 16, "MacTCP ioResult ABI");
_Static_assert(offsetof(TCPBlock, csCode) == 26, "MacTCP csCode ABI");
_Static_assert(offsetof(TCPBlock, stream) == 28, "MacTCP stream ABI");
_Static_assert(offsetof(TCPBlock, param) == 32, "MacTCP parameters ABI");
_Static_assert(offsetof(TCPBlock, param.send.wds) == 38, "MacTCP WDS ABI");
_Static_assert(offsetof(TCPBlock, param.receive.buffer) == 36, "MacTCP receive ABI");
_Static_assert(sizeof(TCPWDS) == 6, "MacTCP WDS entry ABI");

static TCPBlock gTCPBlock;
static char gTCPReceiveBuffer[8192];
static unsigned char gHTTPChunk[2048];
static TCPWDS gWDS[2];
static HTTPResponse gHTTP;
static short gDriver;
static Ptr gStream;

static void Prepare(short command)
{
    memset(&gTCPBlock, 0, sizeof(gTCPBlock));
    gTCPBlock.ioCRefNum = gDriver;
    gTCPBlock.stream = gStream;
    gTCPBlock.csCode = command;
}

static void Abort(void)
{
    TCPBlock abort;
    memset(&abort, 0, sizeof(abort));
    abort.ioCRefNum = gDriver;
    abort.stream = gStream;
    abort.csCode = kTCPAbort;
    PBControlSync((ParmBlkPtr)&abort);
}

static int Run(void)
{
    unsigned long started = TickCount();
    /* Even operations completed immediately by MacTCP must yield to a new
       selection, rather than consuming a whole response without polling. */
    if (NetworkPoll()) return kNetworkCancelled;
    gTCPBlock.ioResult = 1;
    short error = PBControlAsync((ParmBlkPtr)&gTCPBlock);
    if (error != noErr) return error;
    while (gTCPBlock.ioResult > 0) {
        if (NetworkPoll()) { Abort(); return kNetworkCancelled; }
        if (TickCount() - started > 20 * 60UL) { Abort(); return kNetworkTimeout; }
    }
    return gTCPBlock.ioResult;
}

int MacTCPRequest(uint32_t address, unsigned short port, const char *request,
                  unsigned char *body, size_t capacity, unsigned long *length)
{
    int error, result;
    *length = 0;
    gStream = NULL;
    if (OpenDriver((ConstStringPtr)"\004.IPP", &gDriver) != noErr)
        return kNetworkNoDriver;
    Prepare(kTCPCreate);
    gTCPBlock.param.create.buffer = gTCPReceiveBuffer;
    gTCPBlock.param.create.length = sizeof(gTCPReceiveBuffer);
    error = PBControlSync((ParmBlkPtr)&gTCPBlock);
    if (error != noErr) return error;
    gStream = gTCPBlock.stream;
    Prepare(kTCPActiveOpen);
    gTCPBlock.param.open.timeout = 15;
    gTCPBlock.param.open.action = 1;
    gTCPBlock.param.open.flags = 0xC0;
    gTCPBlock.param.open.remoteHost = address;
    gTCPBlock.param.open.remotePort = port;
    error = Run();
    if (error) goto cleanup;

    Prepare(kTCPSend);
    gWDS[0].length = strlen(request); gWDS[0].data = (Ptr)request;
    gWDS[1].length = 0; gWDS[1].data = NULL;
    gTCPBlock.param.send.timeout = 15;
    gTCPBlock.param.send.action = 0;
    gTCPBlock.param.send.flags = 0xC0;
    gTCPBlock.param.send.push = true;
    gTCPBlock.param.send.wds = (Ptr)gWDS;
    error = Run();
    if (error) goto cleanup;

    HTTPResponseInit(&gHTTP, body, capacity);
    for (;;) {
        Prepare(kTCPReceive);
        gTCPBlock.param.receive.timeout = 15;
        gTCPBlock.param.receive.buffer = (Ptr)gHTTPChunk;
        gTCPBlock.param.receive.length = sizeof(gHTTPChunk);
        error = Run();
        if (error == -23005 || error == -23012) { /* Closing/terminated. */
            error = HTTPResponseEnd(&gHTTP) == 1 ? 0 : kNetworkHTTPError;
            break;
        }
        if (error) break;
        if (gTCPBlock.param.receive.length == 0) {
            error = HTTPResponseEnd(&gHTTP) == 1 ? 0 : kNetworkHTTPError;
            break;
        }
        result = HTTPResponseFeed(&gHTTP, gHTTPChunk, gTCPBlock.param.receive.length);
        if (result < 0) { error = kNetworkHTTPError; break; }
        if (result == 1) break;
    }
    if (!error) *length = gHTTP.length;
cleanup:
    Abort();
    Prepare(kTCPRelease);
    PBControlSync((ParmBlkPtr)&gTCPBlock);
    gStream = NULL;
    /* Leave the shared .IPP driver open for other MacTCP applications. */
    return error;
}
