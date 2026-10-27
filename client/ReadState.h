#ifndef MACWIRE_READ_STATE_H
#define MACWIRE_READ_STATE_H
#include <stddef.h>
#include <stdint.h>
#define READ_STATE_HEADER_SIZE 20
#define READ_STATE_RECORD_SIZE 12
#define kShowAllArticles 0
#define kShowUnreadArticles 1
#define kShowReadArticles 2
typedef struct { char id[9]; uint32_t published; } ReadRecord;
int ReadStateHeader(const unsigned char *data, size_t length, size_t *count,
                    unsigned short *visibility, uint32_t *watermark);
int ReadStateDecode(const unsigned char *data, size_t length, ReadRecord *records,
                    size_t capacity, size_t *count, unsigned short *visibility, uint32_t *watermark);
int ReadStateEncode(const ReadRecord *records, size_t count, unsigned short visibility,
                    uint32_t watermark, unsigned char *data, size_t capacity);
int ReadStateFind(const ReadRecord *records, size_t count, const char *id, size_t *position);
int ReadStateSet(ReadRecord *records, size_t *count, size_t capacity,
                 const char *id, uint32_t published, int read);
void ReadStatePrune(ReadRecord *records, size_t *count, uint32_t watermark);
#endif
