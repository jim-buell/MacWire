/* Bounded HTTP/1.0 framing; the backend sends a Content-Length and closes. */
#include "HTTPResponse.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int Prefix(const char *text, const char *prefix)
{
    while (*prefix) {
        char c = *text++;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != *prefix++) return 0;
    }
    return 1;
}

void HTTPResponseInit(HTTPResponse *r, unsigned char *body, size_t capacity)
{
    memset(r, 0, sizeof(*r));
    r->body = body; r->capacity = capacity; r->expected = -1;
}

static int Headers(HTTPResponse *r)
{
    char *line, *next, *value, *end;
    long length;
    int major, minor, status;
    if (sscanf(r->header, "HTTP/%d.%d %d", &major, &minor, &status) != 3 ||
        major != 1 || status != 200) return -1;
    line = strstr(r->header, "\r\n");
    if (!line) return -1;
    line += 2;
    while (*line && (next = strstr(line, "\r\n")) != NULL) {
        *next = 0;
        if (Prefix(line, "content-length:")) {
            value = line + 15;
            while (*value == ' ' || *value == '\t') ++value;
            if (*value < '0' || *value > '9') return -1;
            length = strtol(value, &end, 10);
            while (*end == ' ' || *end == '\t') ++end;
            if (*end || length < 0 || (unsigned long)length > r->capacity ||
                (r->expected >= 0 && length != r->expected)) return -1;
            r->expected = length;
        } else if (Prefix(line, "transfer-encoding:")) return -1;
        line = next + 2;
    }
    r->headersDone = 1;
    if (r->expected == 0) r->complete = 1;
    return 0;
}

int HTTPResponseFeed(HTTPResponse *r, const unsigned char *data, size_t length)
{
    size_t i = 0, remaining;
    if (r->failed) return -1;
    while (!r->headersDone && i < length) {
        if (r->headerLength >= sizeof(r->header) - 1 || data[i] == 0) goto fail;
        r->header[r->headerLength++] = data[i++];
        r->header[r->headerLength] = 0;
        if (r->headerLength >= 4 &&
            memcmp(r->header + r->headerLength - 4, "\r\n\r\n", 4) == 0)
            if (Headers(r) < 0) goto fail;
    }
    remaining = length - i;
    if (remaining > r->capacity - r->length ||
        (r->expected >= 0 && remaining > (size_t)r->expected - r->length)) goto fail;
    if (remaining) memcpy(r->body + r->length, data + i, remaining);
    r->length += remaining;
    if (r->headersDone && r->expected >= 0 && r->length == (size_t)r->expected)
        r->complete = 1;
    return r->complete;
fail:
    r->failed = 1;
    return -1;
}

int HTTPResponseEnd(HTTPResponse *r)
{
    if (r->failed || !r->headersDone ||
        (r->expected >= 0 && r->length != (size_t)r->expected)) {
        r->failed = 1;
        return -1;
    }
    r->complete = 1;
    return 1;
}
