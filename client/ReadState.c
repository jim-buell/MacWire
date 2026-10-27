/* Portable, checked disk format. Records are sorted by ID for fast lookup. */
#include "ReadState.h"
#include <string.h>
static uint32_t word32(const unsigned char *p)
{ return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | p[3]; }
static void put32(unsigned char *p, uint32_t value)
{ p[0]=value>>24; p[1]=value>>16; p[2]=value>>8; p[3]=value; }
static uint32_t checksum(const unsigned char *data, size_t length)
{
    uint32_t hash = 2166136261UL;
    size_t i;
    for (i=0; i<length; ++i)
        if (i<16 || i>=20) hash = (hash ^ data[i]) * 16777619UL;
    return hash;
}
static int valid_id(const char *id)
{
    size_t i;
    if (!id || strlen(id)!=8) return 0;
    for (i=0; i<8; ++i)
        if (!((id[i]>='0' && id[i]<='9') || (id[i]>='a' && id[i]<='f'))) return 0;
    return 1;
}
int ReadStateHeader(const unsigned char *data, size_t length, size_t *count,
                    unsigned short *visibility, uint32_t *watermark)
{
    size_t n, i;
    unsigned short filter;
    if (!data || !count || !visibility || !watermark || length<READ_STATE_HEADER_SIZE ||
        memcmp(data,"MWRD",4) || data[4]!=0 || data[5]!=1 || data[6]!=0) return 0;
    filter=data[7]; n=word32(data+12);
    if (filter>kShowReadArticles || n!=(length-READ_STATE_HEADER_SIZE)/READ_STATE_RECORD_SIZE ||
        (length-READ_STATE_HEADER_SIZE)%READ_STATE_RECORD_SIZE || word32(data+16)!=checksum(data,length)) return 0;
    for (i=0; i<n; ++i) {
        char id[9]; const unsigned char *record=data+20+i*12;
        memcpy(id,record,8); id[8]=0;
        if (!valid_id(id) || (i && memcmp(record-12,record,8)>=0) || word32(record+8)>word32(data+8)) return 0;
    }
    *count=n; *visibility=filter; *watermark=word32(data+8);
    return 1;
}
int ReadStateDecode(const unsigned char *data, size_t length, ReadRecord *records,
                    size_t capacity, size_t *count, unsigned short *visibility, uint32_t *watermark)
{
    size_t n, i; unsigned short filter; uint32_t newest;
    if (!count || !visibility || !watermark ||
        !ReadStateHeader(data,length,&n,&filter,&newest) || n>capacity || (n && !records)) return 0;
    for (i=0; i<n; ++i) {
        memcpy(records[i].id,data+20+i*12,8); records[i].id[8]=0;
        records[i].published=word32(data+28+i*12);
    }
    *count=n; *visibility=filter; *watermark=newest;
    return 1;
}
int ReadStateEncode(const ReadRecord *records, size_t count, unsigned short visibility,
                    uint32_t watermark, unsigned char *data, size_t capacity)
{
    size_t length, i;
    if (!data || capacity<20 || count>(capacity-20)/12 || count>UINT32_MAX ||
        visibility>kShowReadArticles || (count && !records)) return 0;
    for (i=0; i<count; ++i)
        if (!valid_id(records[i].id) || records[i].published>watermark ||
            (i && strcmp(records[i-1].id,records[i].id)>=0)) return 0;
    length=20+count*12;
    memcpy(data,"MWRD",4); data[4]=0; data[5]=1; data[6]=0; data[7]=visibility;
    put32(data+8,watermark); put32(data+12,count); put32(data+16,0);
    for (i=0; i<count; ++i) {
        memcpy(data+20+i*12,records[i].id,8); put32(data+28+i*12,records[i].published);
    }
    put32(data+16,checksum(data,length));
    return 1;
}
int ReadStateFind(const ReadRecord *records, size_t count, const char *id, size_t *position)
{
    size_t low=0, high=count;
    while (low<high) {
        size_t mid=low+(high-low)/2;
        if (strcmp(records[mid].id,id)<0) low=mid+1; else high=mid;
    }
    if (position) *position=low;
    return low<count && !strcmp(records[low].id,id);
}
int ReadStateSet(ReadRecord *records, size_t *count, size_t capacity,
                 const char *id, uint32_t published, int read)
{
    size_t position; int found;
    if (!count || !valid_id(id) || (*count && !records)) return 0;
    found=ReadStateFind(records,*count,id,&position);
    if (!read) {
        if (found) { --*count; memmove(records+position,records+position+1,(*count-position)*sizeof(*records)); }
    } else if (found) records[position].published=published;
    else {
        if (*count>=capacity || !records) return 0;
        memmove(records+position+1,records+position,(*count-position)*sizeof(*records));
        strcpy(records[position].id,id); records[position].published=published; ++*count;
    }
    return 1;
}
void ReadStatePrune(ReadRecord *records, size_t *count, uint32_t watermark)
{
    uint32_t cutoff=watermark>604800UL ? watermark-604800UL : 0;
    size_t i, kept=0;
    for (i=0; i<*count; ++i)
        if (records[i].published>=cutoff) records[kept++]=records[i];
    *count=kept;
}
