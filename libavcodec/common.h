#ifndef COMMON_H
#define COMMON_H

#define FFMPEG_VERSION_INT 0x000406
#define FFMPEG_VERSION     "0.4.6"

#if defined(WIN32) && !defined(__MINGW32__)
#define CONFIG_WIN32
#endif

//#define ALT_BITSTREAM_READER
//#define ALIGNED_BITSTREAM
#define FAST_GET_FIRST_VLC

#ifdef HAVE_AV_CONFIG_H
/* only include the following when compiling package */
#include "../config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifndef ENODATA
#define ENODATA  61
#endif

#endif

#ifdef CONFIG_WIN32

/* windows */

typedef unsigned short UINT16;
typedef signed short INT16;
typedef unsigned char UINT8;
typedef unsigned int UINT32;
typedef unsigned __int64 UINT64;
typedef signed char INT8;
typedef signed int INT32;
typedef signed __int64 INT64;

typedef UINT8 uint8_t;
typedef INT8 int8_t;
typedef UINT16 uint16_t;
typedef INT16 int16_t;
typedef UINT32 uint32_t;
typedef INT32 int32_t;

#ifndef __MINGW32__
#define INT64_C(c)     (c ## i64)
#define UINT64_C(c)    (c ## i64)

#define inline __inline

/*
  Disable warning messages:
    warning C4244: '=' : conversion from 'double' to 'float', possible loss of data
    warning C4305: 'argument' : truncation from 'const double' to 'float'
*/
#pragma warning( disable : 4244 )
#pragma warning( disable : 4305 )

#else
#define INT64_C(c)     (c ## LL)
#define UINT64_C(c)    (c ## ULL)
#endif /* __MINGW32__ */

#define M_PI    3.14159265358979323846
#define M_SQRT2 1.41421356237309504880  /* sqrt(2) */

#ifdef _DEBUG
#define DEBUG
#endif

// code from bits/byteswap.h (C) 1997, 1998 Free Software Foundation, Inc.
#define bswap_32(x) \
     ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | \
      (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#define be2me_32(x) bswap_32(x)

#define snprintf _snprintf

#ifndef __MINGW32__
/* no config.h with VC */
#define CONFIG_ENCODERS 1
#define CONFIG_DECODERS 1
#define CONFIG_AC3      1
#endif

#else

/* unix */

#include <inttypes.h>

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

#ifdef HAVE_AV_CONFIG_H

#ifdef __FreeBSD__
#include <sys/param.h>
#endif

#ifndef INT64_C
#define INT64_C(c)     (c ## LL)
#define UINT64_C(c)    (c ## ULL)
#endif

#include "../bswap.h"

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif

#endif /* HAVE_AV_CONFIG_H */

#endif /* !CONFIG_WIN32 */


/* debug stuff */
#ifdef HAVE_AV_CONFIG_H

#ifndef DEBUG
#define NDEBUG
#endif
#include <assert.h>

/* dprintf macros */
#if defined(CONFIG_WIN32) && !defined(__MINGW32__)

inline void dprintf(const char* fmt,...) {}

#else

#ifdef DEBUG
#define dprintf(fmt,args...) printf(fmt, ## args)
#else
#define dprintf(fmt,args...)
#endif

#endif /* !CONFIG_WIN32 */

#endif /* HAVE_AV_CONFIG_H */

/* bit output */

struct PutBitContext;

typedef void (*WriteDataFunc)(void *, UINT8 *, int);

typedef struct PutBitContext {
    UINT32 bit_buf;
    int bit_cnt;
    UINT8 *buf, *buf_ptr, *buf_end;
    INT64 data_out_size; /* in bytes */
    void *opaque;
    WriteDataFunc write_data;
} PutBitContext;

void init_put_bits(PutBitContext *s, 
                   UINT8 *buffer, int buffer_size,
                   void *opaque,
                   void (*write_data)(void *, UINT8 *, int));
void put_bits(PutBitContext *s, int n, unsigned int value);
INT64 get_bit_count(PutBitContext *s); /* XXX: change function name */
void align_put_bits(PutBitContext *s);
void flush_put_bits(PutBitContext *s);

/* jpeg specific put_bits */
void jput_bits(PutBitContext *s, int n, unsigned int value);
void jflush_put_bits(PutBitContext *s);

/* bit input */

typedef struct GetBitContext {
#ifdef ALT_BITSTREAM_READER
    int index;
    UINT8 *buffer;
#else
    UINT32 bit_buf;
    int bit_cnt;
    UINT8 *buf, *buf_ptr, *buf_end;
#endif
} GetBitContext;

typedef struct VLC {
    int bits;
    INT16 *table_codes;
    INT8 *table_bits;
    int table_size, table_allocated;
} VLC;

/* used to avoid missaligned exceptions on some archs (alpha, ...) */
#ifdef ARCH_X86
#define unaligned32(a) (*(UINT32*)(a))
#else
#ifdef __GNUC__
static inline uint32_t unaligned32(const void *v) {
    struct Unaligned {
	uint32_t i;
    } __attribute__((packed));

    return ((const struct Unaligned *) v)->i;
}
#elif defined(__DECC)
static inline uint32_t unaligned32(const void *v) {
    return *(const __unaligned uint32_t *) v;
}
#else
static inline uint32_t unaligned32(const void *v) {
    return *(const uint32_t *) v;
}
#endif
#endif //!ARCH_X86

void init_get_bits(GetBitContext *s, 
                   UINT8 *buffer, int buffer_size);

#ifndef ALT_BITSTREAM_READER
unsigned int get_bits_long(GetBitContext *s, int n);
unsigned int show_bits_long(GetBitContext *s, int n);
#endif

static inline unsigned int get_bits(GetBitContext *s, int n){
#ifdef ALT_BITSTREAM_READER
#ifdef ALIGNED_BITSTREAM
    int index= s->index;
    uint32_t result1= be2me_32( ((uint32_t *)s->buffer)[index>>5] );
    uint32_t result2= be2me_32( ((uint32_t *)s->buffer)[(index>>5) + 1] );
#ifdef ARCH_X86
    asm ("shldl %%cl, %2, %0\n\t"
         : "=r" (result1)
	 : "0" (result1), "r" (result2), "c" (index));
#else
    result1<<= (index&0x1F);
    result2= (result2>>1) >> (31-(index&0x1F));
    result1|= result2;
#endif
    result1>>= 32 - n;
    index+= n;
    s->index= index;
    
    return result1;
#else //ALIGNED_BITSTREAM
    int index= s->index;
    uint32_t result= be2me_32( unaligned32( ((uint8_t *)s->buffer)+(index>>3) ) );

    result<<= (index&0x07);
    result>>= 32 - n;
    index+= n;
    s->index= index;
    
    return result;
#endif //!ALIGNED_BITSTREAM
#else //ALT_BITSTREAM_READER
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
#endif //!ALT_BITSTREAM_READER
}

static inline unsigned int get_bits1(GetBitContext *s){
#ifdef ALT_BITSTREAM_READER
    int index= s->index;
    uint8_t result= s->buffer[ index>>3 ];
    result<<= (index&0x07);
    result>>= 8 - 1;
    index++;
    s->index= index;
    
    return result;
#else
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
#endif
}

/* This function is identical to get_bits(), the only */
/* diference is that it doesn't touch the buffer      */
/* it is usefull to see the buffer.                   */
static inline unsigned int show_bits(GetBitContext *s, int n)
{
#ifdef ALT_BITSTREAM_READER
#ifdef ALIGNED_BITSTREAM
    int index= s->index;
    uint32_t result1= be2me_32( ((uint32_t *)s->buffer)[index>>5] );
    uint32_t result2= be2me_32( ((uint32_t *)s->buffer)[(index>>5) + 1] );
#ifdef ARCH_X86
    asm ("shldl %%cl, %2, %0\n\t"
         : "=r" (result1)
	 : "0" (result1), "r" (result2), "c" (index));
#else
    result1<<= (index&0x1F);
    result2= (result2>>1) >> (31-(index&0x1F));
    result1|= result2;
#endif
    result1>>= 32 - n;
    
    return result1;
#else //ALIGNED_BITSTREAM
    int index= s->index;
    uint32_t result= be2me_32( unaligned32( ((uint8_t *)s->buffer)+(index>>3) ) );

    result<<= (index&0x07);
    result>>= 32 - n;
    
    return result;
#endif //!ALIGNED_BITSTREAM
#else //ALT_BITSTREAM_READER
    if(s->bit_cnt>=n) {
        /* most common case here */
        unsigned int val = s->bit_buf >> (32 - n);
        return val;
    }
    return show_bits_long(s,n);
#endif //!ALT_BITSTREAM_READER
}

static inline void skip_bits(GetBitContext *s, int n){
#ifdef ALT_BITSTREAM_READER
    s->index+= n;
#else
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
#endif
}

static inline void skip_bits1(GetBitContext *s){
#ifdef ALT_BITSTREAM_READER
    s->index++;
#else
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
#endif
}

static inline int get_bits_count(GetBitContext *s)
{
#ifdef ALT_BITSTREAM_READER
    return s->index;
#else
    return (s->buf_ptr - s->buf) * 8 - s->bit_cnt;
#endif
}

void align_get_bits(GetBitContext *s);
int init_vlc(VLC *vlc, int nb_bits, int nb_codes,
             const void *bits, int bits_wrap, int bits_size,
             const void *codes, int codes_wrap, int codes_size);
void free_vlc(VLC *vlc);

#ifdef ALT_BITSTREAM_READER
#ifdef ALIGNED_BITSTREAM
#ifdef ARCH_X86
#define SHOW_BITS(s, val, n) \
    val= be2me_32( ((uint32_t *)(s)->buffer)[bit_cnt>>5] );\
    {uint32_t result2= be2me_32( ((uint32_t *)(s)->buffer)[(bit_cnt>>5) + 1] );\
    asm ("shldl %%cl, %2, %0\n\t"\
         : "=r" (val)\
         : "0" (val), "r" (result2), "c" (bit_cnt));\
    ((uint32_t)val)>>= 32 - n;}
#else //ARCH_X86
#define SHOW_BITS(s, val, n) \
    val= be2me_32( ((uint32_t *)(s)->buffer)[bit_cnt>>5] );\
    {uint32_t result2= be2me_32( ((uint32_t *)(s)->buffer)[(bit_cnt>>5) + 1] );\
    val<<= (bit_cnt&0x1F);\
    result2= (result2>>1) >> (31-(bit_cnt&0x1F));\
    val|= result2;\
    ((uint32_t)val)>>= 32 - n;}
#endif //!ARCH_X86
#else //ALIGNED_BITSTREAM
#define SHOW_BITS(s, val, n) \
    val= be2me_32( unaligned32( ((uint8_t *)(s)->buffer)+(bit_cnt>>3) ) );\
    val<<= (bit_cnt&0x07);\
    ((uint32_t)val)>>= 32 - n;
#endif // !ALIGNED_BITSTREAM
#define FLUSH_BITS(n) bit_cnt+=n; 
#define SAVE_BITS(s) bit_cnt= (s)->index;
#define RESTORE_BITS(s) (s)->index= bit_cnt;
#else

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
#endif // !ALT_BITSTREAM_READER

static inline int get_vlc(GetBitContext *s, VLC *vlc)
{
    int code, n, nb_bits, index;
    INT16 *table_codes;
    INT8 *table_bits;
    int bit_cnt;
#ifndef ALT_BITSTREAM_READER
    UINT32 bit_buf;
    UINT8 *buf_ptr;
#endif

    SAVE_BITS(s);
    nb_bits = vlc->bits;
    table_codes = vlc->table_codes;
    table_bits = vlc->table_bits;

#ifdef FAST_GET_FIRST_VLC
    SHOW_BITS(s, index, nb_bits);
    code = table_codes[index];
    n = table_bits[index];
    if (n > 0) {
        /* most common case (90%)*/
        FLUSH_BITS(n);
        RESTORE_BITS(s);
        return code;
    } else if (n == 0) {
        return -1;
    } else {
        FLUSH_BITS(nb_bits);
        nb_bits = -n;
        table_codes = vlc->table_codes + code;
        table_bits = vlc->table_bits + code;
    }
#endif
    for(;;) {
        SHOW_BITS(s, index, nb_bits);
        code = table_codes[index];
        n = table_bits[index];
        if (n > 0) {
            /* most common case */
            FLUSH_BITS(n);
#ifdef STATS
            st_bit_counts[st_current_index] += n;
#endif
            break;
        } else if (n == 0) {
            return -1;
        } else {
            FLUSH_BITS(nb_bits);
#ifdef STATS
            st_bit_counts[st_current_index] += nb_bits;
#endif
            nb_bits = -n;
            table_codes = vlc->table_codes + code;
            table_bits = vlc->table_bits + code;
        }
    }
    RESTORE_BITS(s);
    return code;
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

static inline int av_log2(unsigned int v)
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
