#ifndef COMMON_H
#define COMMON_H

#ifdef HAVE_AV_CONFIG_H
#include "../config.h"
#endif

#ifndef __WINE_WINDEF16_H
/* workaround for typedef conflict in MPlayer (wine typedefs) */
typedef unsigned short UINT16;
typedef signed short INT16;
#endif

typedef unsigned char UINT8;
typedef unsigned int UINT32;
typedef unsigned long long UINT64;
typedef signed char INT8;
typedef signed int INT32;
typedef signed long long INT64;

/* bit output */

struct PutBitContext;

typedef void (*WriteDataFunc)(void *, UINT8 *, int);

typedef struct PutBitContext {
    UINT32 bit_buf;
    int bit_cnt;
    UINT8 *buf, *buf_ptr, *buf_end;
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

/* bit input */

typedef struct GetBitContext {
    UINT32 bit_buf;
    int bit_cnt;
    UINT8 *buf, *buf_ptr, *buf_end;
} GetBitContext;

typedef struct VLC {
    int bits;
    INT16 *table_codes;
    INT8 *table_bits;
    int table_size, table_allocated;
} VLC;

void init_get_bits(GetBitContext *s, 
                   UINT8 *buffer, int buffer_size);

unsigned int get_bits_long(GetBitContext *s, int n);

static inline unsigned int get_bits(GetBitContext *s, int n){
    if(s->bit_cnt>=n){
        /* most common case here */
        unsigned int val = s->bit_buf >> (32 - n);
        s->bit_buf <<= n;
	s->bit_cnt -= n;
#ifdef STATS
	st_bit_counts[st_current_index] += n;
#endif
	return val;
    }
    return get_bits_long(s,n);
}

static inline unsigned int get_bits1(GetBitContext *s){
    if(s->bit_cnt>0){
        /* most common case here */
        unsigned int val = s->bit_buf >> 31;
        s->bit_buf <<= 1;
	s->bit_cnt--;
#ifdef STATS
	st_bit_counts[st_current_index]++;
#endif
	return val;
    }
    return get_bits_long(s,1);
}

static inline void skip_bits(GetBitContext *s, int n){
    if(s->bit_cnt>=n){
        /* most common case here */
        s->bit_buf <<= n;
	s->bit_cnt -= n;
#ifdef STATS
	st_bit_counts[st_current_index] += n;
#endif
    } else {
	get_bits_long(s,n);
    }
}

static inline void skip_bits1(GetBitContext *s){
    if(s->bit_cnt>0){
        /* most common case here */
        s->bit_buf <<= 1;
	s->bit_cnt--;
#ifdef STATS
	st_bit_counts[st_current_index]++;
#endif
    } else {
	get_bits_long(s,1);
    }
}


void align_get_bits(GetBitContext *s);
int init_vlc(VLC *vlc, int nb_bits, int nb_codes,
             const void *bits, int bits_wrap, int bits_size,
             const void *codes, int codes_wrap, int codes_size);
void free_vlc(VLC *vlc);
int get_vlc(GetBitContext *s, VLC *vlc);

/* macro to go faster */
/* n must be <= 24 */
/* XXX: optimize buffer end test */
#define SHOW_BITS(s, val, n)\
{\
    if (bit_cnt < n && buf_ptr < (s)->buf_end) {\
        bit_buf |= *buf_ptr++ << (24 - bit_cnt);\
        bit_cnt += 8;\
        if (bit_cnt < n && buf_ptr < (s)->buf_end) {\
            bit_buf |= *buf_ptr++ << (24 - bit_cnt);\
            bit_cnt += 8;\
            if (bit_cnt < n && buf_ptr < (s)->buf_end) {\
                bit_buf |= *buf_ptr++ << (24 - bit_cnt);\
                bit_cnt += 8;\
            }\
        }\
    }\
    val = bit_buf >> (32 - n);\
}

/* SHOW_BITS with n1 >= n must be been done before */
#define FLUSH_BITS(n)\
{\
    bit_buf <<= n;\
    bit_cnt -= n;\
}

#define SAVE_BITS(s) \
{\
    bit_cnt = (s)->bit_cnt;\
    bit_buf = (s)->bit_buf;\
    buf_ptr = (s)->buf_ptr;\
}

#define RESTORE_BITS(s) \
{\
    (s)->buf_ptr = buf_ptr;\
    (s)->bit_buf = bit_buf;\
    (s)->bit_cnt = bit_cnt;\
}

/* define it to include statistics code (useful only for optimizing
   codec efficiency */
//#define STATS

#ifdef STATS

enum {
    ST_UNKNOWN,
    ST_DC,
    ST_INTRA_AC,
    ST_INTER_AC,
    ST_INTRA_MB,
    ST_INTER_MB,
    ST_MV,
    ST_NB,
};

extern int st_current_index;
extern unsigned int st_bit_counts[ST_NB];
extern unsigned int st_out_bit_counts[ST_NB];

void print_stats(void);
#endif

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

/* memory */
void *av_mallocz(int size);

#endif
