#ifndef COMMON_H
#define COMMON_H

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef signed char INT8;
typedef signed short INT16;
typedef signed int INT32;

/* bit I/O */

struct PutBitContext;

typedef void (*WriteDataFunc)(void *, UINT8 *, int);

typedef struct PutBitContext {
    UINT8 *buf, *buf_ptr, *buf_end;
    int bit_cnt;
    UINT32 bit_buf;
    long long data_out_size; /* in bytes */
    void *opaque;
    WriteDataFunc write_data;
} PutBitContext;

void init_put_bits(PutBitContext *s, 
                   UINT8 *buffer, int buffer_size,
                   void *opaque,
                   void (*write_data)(void *, UINT8 *, int));
void put_bits(PutBitContext *s, int n, unsigned int value);
long long get_bit_count(PutBitContext *s);
void align_put_bits(PutBitContext *s);
void flush_put_bits(PutBitContext *s);

/* jpeg specific put_bits */
void jput_bits(PutBitContext *s, int n, unsigned int value);
void jflush_put_bits(PutBitContext *s);

/* misc math functions */

extern inline int log2(unsigned int v)
{
    int n;

    n = 0;
    if (v & 0xffff0000) {
        v >>= 16;
        n += 16;
    }
    if (v & 0xff00) {
        v >>= 8;
        n += 8;
    }
    if (v & 0xf0) {
        v >>= 4;
        n += 4;
    }
    if (v & 0xc) {
        v >>= 2;
        n += 2;
    }
    if (v & 0x2) {
        n++;
    }
    return n;
}

#endif
