#ifndef TICKERTAPE_MACTCP_TRANSPORT_H
#define TICKERTAPE_MACTCP_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>
enum {
    kNetworkCancelled = -30001, kNetworkTimeout = -30002,
    kNetworkHTTPError = -30003, kNetworkNoDriver = -30004
};
int MacTCPRequest(uint32_t address, unsigned short port, const char *request,
                  unsigned char *body, size_t capacity, unsigned long *length);
#endif
