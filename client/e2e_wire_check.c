/*
 * Host-side end-to-end harness for the SE wire contract.
 *
 * Reads a raw /sync packet from argv[1] and decodes it with the client's
 * actual WireDecodeIndex implementation (client/Wire.c). This exercises the
 * exact byte format the backend produces against the exact parser the System 7
 * client uses. Optional --http passes raw HTTP bytes through HTTPResponse.c
 * first; the device-driver layer still requires the physical System 7 Mac.
 *
 * Output format (stdout):
 *   OK
 *   COUNT <n>
 *   ---
 *   id: <id>
 *   feed: <feed>
 *   title: <title>
 *   summary: <summary>
 *   published: <published>
 *   imageCount: <n>
 *   full: <0|1>
 *   ---
 *   ...
 *
 * Exit code 0 on a successful decode, 1 otherwise.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Wire.h"
#include "HTTPResponse.h"

static unsigned char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    *out_len = rd;
    return buf;
}

static void print_field(const char *label, const char *raw, size_t len) {
    printf("%s:", label);
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)raw[i];
        if (c == 0) break;  /* C-string: stop at terminator */
        if (c == '\n') printf("\\n");
        else if (c == '\t') printf("\\t");
        else if (c >= 32 && c < 127) putchar(c);
        else printf("\\x%02x", c);
    }
    putchar('\n');
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <packet.bin>\n", argv[0]);
        return 1;
    }
    size_t len = 0;
    unsigned char *packet = read_file(argv[1], &len);
    if (!packet) return 1;

    unsigned char body[32768];
    if (argc > 2 && strcmp(argv[2], "--http") == 0) {
        HTTPResponse response;
        HTTPResponseInit(&response, body, sizeof(body));
        /* Deliberately split both headers and binary packets into chunks. */
        for (size_t offset = 0; offset < len; offset += 7) {
            size_t chunk = len - offset < 7 ? len - offset : 7;
            if (HTTPResponseFeed(&response, packet + offset, chunk) < 0) {
                free(packet); return 1;
            }
        }
        if (HTTPResponseEnd(&response) != 1) { free(packet); return 1; }
        free(packet);
        packet = malloc(response.length);
        if (!packet) return 1;
        memcpy(packet, body, response.length);
        len = response.length;
    }

    WireIndexEntry entries[WIRE_MAX_ARTICLES];
    size_t count = 0;
    WirePage page;
    int rc;
    if (packet[0] == 0x0A) {
        rc = WireDecodePage(packet, len, entries, WIRE_MAX_ARTICLES, &page);
        count = rc == 0 ? page.count : 0;
        if (!rc) fprintf(stderr, "VIEW %s TOTAL %lu OFFSET %lu\n", page.view,
                         (unsigned long)page.total, (unsigned long)page.offset);
    } else rc = WireDecodeIndex(packet, len, entries, WIRE_MAX_ARTICLES, &count);

    if (rc != 0) {
        printf("FAIL decode=%d\n", rc);
        free(packet);
        return 1;
    }

    printf("OK\n");
    printf("COUNT %zu\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("---\n");
        print_field("id", entries[i].id, sizeof(entries[i].id));
        print_field("feed", entries[i].feed, sizeof(entries[i].feed));
        print_field("title", entries[i].title, sizeof(entries[i].title));
        print_field("summary", entries[i].summary, sizeof(entries[i].summary));
        printf("published:%f\n", entries[i].published);
        printf("imageCount:%u\n", entries[i].imageCount);
        printf("full:%u\n", entries[i].fullContentAvailable);
    }

    free(packet);
    return 0;
}
