#include <string.h>
#ifdef RSS_DEMO_SLOW_DOWNLOADS
#include <Events.h>
#include <stdint.h>
#include "Connection.h"
#include "MacTCPTransport.h"
#endif

#ifdef TICKERTAPE_PACKET_HEADER
#include TICKERTAPE_PACKET_HEADER
#else
#include "DemoPackets.h"
#endif

short DemoResponse(const char *request, unsigned char *out,
                   unsigned long capacity, unsigned long *length)
{
    const unsigned char *source = 0;
    unsigned long size = 0;
    *length = 0;
    if (strncmp(request, "GET /sync?", 10) == 0) {
        source = demoIndex;
        size = sizeof(demoIndex);
        if (strstr(request, DEMO_FEED_KEY_1)) {
            source = demoFeedIndex1; size = sizeof(demoFeedIndex1);
        } else if (strstr(request, DEMO_FEED_KEY_2)) {
            source = demoFeedIndex2; size = sizeof(demoFeedIndex2);
        } else if (strstr(request, DEMO_FEED_KEY_3)) {
            source = demoFeedIndex3; size = sizeof(demoFeedIndex3);
        }
    } else if (strncmp(request, "GET /dates", 10) == 0) {
        source = demoDates; size = sizeof(demoDates);
    } else if (strncmp(request, "GET /feeds", 10) == 0) {
        source = demoFeeds; size = sizeof(demoFeeds);
    } else if (strncmp(request, "GET /image/00000001/0", 21) == 0) {
        source = demoImage1; size = sizeof(demoImage1);
    } else if (strncmp(request, "GET /image/00000002/0", 21) == 0) {
        source = demoImage2; size = sizeof(demoImage2);
    } else if (strncmp(request, "GET /image/00000003/0", 21) == 0) {
        source = demoImage3; size = sizeof(demoImage3);
    } else if (strncmp(request, "GET /article/", 13) == 0) {
        if (strncmp(request + 13, "00000001", 8) == 0) {
            source = demoArticle1;
            size = sizeof(demoArticle1);
        } else if (strncmp(request + 13, "00000002", 8) == 0) {
            source = demoArticle2;
            size = sizeof(demoArticle2);
        } else if (strncmp(request + 13, "00000003", 8) == 0) {
            source = demoArticle3;
            size = sizeof(demoArticle3);
        }
    }
    if (!source || size > capacity) return -1;
#ifdef RSS_DEMO_SLOW_DOWNLOADS
    {
        /* Deliver each packet over 1.5 seconds in small chunks. NetworkPoll
           drives the shipping cursor and handles Escape/navigation normally. */
        uint32_t started = (uint32_t)TickCount();
        unsigned long offset = 0;
        while (offset < size) {
            unsigned long chunk = size - offset;
            uint32_t target;
            if (chunk > 512) chunk = 512;
            target = (uint32_t)(90UL * (offset + chunk) / size);
            while ((uint32_t)((uint32_t)TickCount() - started) < target)
                if (NetworkPoll()) return kNetworkCancelled;
            memcpy(out + offset, source + offset, chunk);
            offset += chunk;
        }
    }
#else
    memcpy(out, source, size);
#endif
    *length = size;
    return 0;
}
