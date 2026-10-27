#ifndef TICKERTAPE_HTTP_RESPONSE_H
#define TICKERTAPE_HTTP_RESPONSE_H
#include <stddef.h>
typedef struct {
    char header[4096];
    size_t headerLength, length, capacity;
    long expected;
    unsigned char *body;
    int headersDone, complete, failed;
} HTTPResponse;
void HTTPResponseInit(HTTPResponse *response, unsigned char *body, size_t capacity);
/* 1: complete, 0: needs more input, -1: invalid/truncated/oversized response. */
int HTTPResponseFeed(HTTPResponse *response, const unsigned char *data, size_t length);
int HTTPResponseEnd(HTTPResponse *response);
#endif
