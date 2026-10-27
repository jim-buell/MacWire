/* Address validation is shared by the settings dialog and MacTCP transport. */
#include "Connection.h"
#include <string.h>

int ConnectionAddress(const char *text, uint32_t *address)
{
    uint32_t result = 0;
    unsigned value, digits, part;
    for (part = 0; part < 4; ++part) {
        value = digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (*text++ - '0');
            if (++digits > 3 || value > 255) return 0;
        }
        if (!digits) return 0;
        result = (result << 8) | value;
        if (part < 3 && *text++ != '.') return 0;
    }
    if (*text || result == 0 || result == UINT32_MAX) return 0;
    *address = result;
    return 1;
}

int ConnectionPort(const char *text, unsigned short *port)
{
    unsigned long value = 0;
    unsigned digits = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text++ - '0');
        if (++digits > 5 || value > 65535) return 0;
    }
    if (!digits || *text || value == 0) return 0;
    *port = value;
    return 1;
}

/* Fixed big-endian disk format; version 1 had no article-age field. */
int ConnectionDecodeSettings(const unsigned char *data, unsigned long length,
                             ConnectionSettings *settings)
{
    ConnectionSettings decoded;
    uint32_t address;
    unsigned short version;
    if (!data || !settings || (length != 22 && length != CONNECTION_FILE_SIZE)) return 0;
    version = ((unsigned short)data[0] << 8) | data[1];
    if ((version == 1 && length != 22) || (version == 2 && length != CONNECTION_FILE_SIZE) ||
        (version != 1 && version != 2)) return 0;
    memcpy(decoded.address, data + 2, sizeof(decoded.address));
    decoded.port = ((unsigned short)data[18] << 8) | data[19];
    decoded.sample = ((unsigned short)data[20] << 8) | data[21];
    decoded.windowHours = version == 1 ? 24 : ((unsigned short)data[22] << 8) | data[23];
    if (decoded.address[15] != 0 ||
        (decoded.address[0] && !ConnectionAddress(decoded.address, &address)) ||
        !decoded.port || decoded.sample > 1 ||
        (decoded.windowHours != 24 && decoded.windowHours != 48 &&
         decoded.windowHours != 72 && decoded.windowHours != 168)) return 0;
#ifndef RSS_DEMO_TRANSPORT
    /* Keep the old disk layout, but migrate its retired sample flag to live. */
    decoded.sample = 0;
#endif
    *settings = decoded;
    return 1;
}

void ConnectionEncodeSettings(const ConnectionSettings *settings,
                              unsigned char data[CONNECTION_FILE_SIZE])
{
    data[0] = 0; data[1] = 2;
    memcpy(data + 2, settings->address, sizeof(settings->address));
    data[18] = settings->port >> 8; data[19] = settings->port & 255;
    data[20] = settings->sample >> 8; data[21] = settings->sample & 255;
    data[22] = settings->windowHours >> 8; data[23] = settings->windowHours & 255;
}
