#ifndef COMMON_H
#define COMMON_H

#define FFMPEG_VERSION_INT 0x000406
#define FFMPEG_VERSION     "0.4.6"

#if defined(WIN32) && !defined(__MINGW32__) && !defined(__CYGWIN__)
#define CONFIG_WIN32
#endif

//#define ALT_BITSTREAM_WRITER
//#define ALIGNED_BITSTREAM_WRITER

#define ALT_BITSTREAM_READER
//#define LIBMPEG2_BITSTREAM_READER
//#define A32_BITSTREAM_READER

#ifdef ARCH_ALPHA
#define ALT_BITSTREAM_READER
#endif

//#define ALIGNED_BITSTREAM
#define FAST_GET_FIRST_VLC
//#define DUMP_STREAM

#ifdef HAVE_AV_CONFIG_H
/* only include the following when compiling package */
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#ifndef ENODATA
#define ENODATA  61
#endif

#endif /* HAVE_AV_CONFIG_H */

/* Suppress restrict if it was not defined in config.h.  */
#ifndef restrict
#define restrict
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
typedef UINT64 uint64_t;
typedef INT64 int64_t;

#ifndef __MINGW32__
#define INT64_C(c)     (c ## i64)
#define UINT64_C(c)    (c ## i64)

#define inline __inline

#else
#define INT64_C(c)     (c ## LL)
#define UINT64_C(c)    (c ## ULL)
#endif /* __MINGW32__ */

#define M_PI    3.14159265358979323846
#define M_SQRT2 1.41421356237309504880  /* sqrt(2) */

#ifdef _DEBUG
#define DEBUG
#endif

#define snprintf _snprintf

#else /* CONFIG_WIN32 */

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

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif

#endif /* HAVE_AV_CONFIG_H */

#endif /* !CONFIG_WIN32 */

#ifdef HAVE_AV_CONFIG_H

#include "bswap.h"

#if defined(__MINGW32__) || defined(__CYGWIN__) || \
    defined(__OS2__) || defined (__OpenBSD__)
#define MANGLE(a) "_" #a
#else
#define MANGLE(a) #a
#endif

/* debug stuff */

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

#define av_abort()      do { fprintf(stderr, "Abort at %s:%d\n", __FILE__, __LINE__); abort(); } while (0)

/* assume b>0 */
#define ROUNDED_DIV(a,b) (((a)>0 ? (a) + ((b)>>1) : (a) - ((b)>>1))/(b))
#define ABS(a) ((a) >= 0 ? (a) : (-(a)))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) > (b) ? (b) : (a))

#ifdef ARCH_X86
// avoid +32 for shift optimization (gcc should do that ...)
static inline  int32_t NEG_SSR32( int32_t a, int8_t s){
    asm ("sarl %1, %0\n\t"
         : "+r" (a)
         : "ic" ((uint8_t)(-s))
    );
    return a;
}
static inline uint32_t NEG_USR32(uint32_t a, int8_t s){
    asm ("shrl %1, %0\n\t"
         : "+r" (a)
         : "ic" ((uint8_t)(-s))
    );
    return a;
}
#else
#define NEG_SSR32(a,s) ((( int32_t)(a))>>(32-(s)))
#define NEG_USR32(a,s) (((uint32_t)(a))>>(32-(s)))
#endif

/* bit output */

struct PutBitContext;

typedef void (*WriteDataFunc)(void *, UINT8 *, int);

typedef struct PutBitContext {
#ifdef ALT_BITSTREAM_WRITER
    UINT8 *buf, *buf_end;
    int index;
#else
    UINT32 bit_buf;
    int bit_left;
    UINT8 *buf, *buf_ptr, *buf_end;
#endif
    INT64 data_out_size; /* in bytes */
} PutBitContext;

void init_put_bits(PutBitContext *s, 
                   UINT8 *buffer, int buffer_size,
                   void *opaque,
                   void (*write_data)(void *, UINT8 *, int));

INT64 get_bit_count(PutBitContext *s); /* XXX: change function name */
void align_put_bits(PutBitContext *s);
void flush_put_bits(PutBitContext *s);
void put_string(PutBitContext * pbc, char *s);

/* jpeg specific put_bits */
void jflush_put_bits(PutBitContext *s);

/* bit input */

typedef struct GetBitContext {
    UINT8 *buffer, *buffer_end;
#ifdef ALT_BITSTREAM_READER
    int index;
#elif defined LIBMPEG2_BITSTREAM_READER
    UINT8 *buffer_ptr;
    UINT32 cache;
    int bit_count;
#elif defined A32_BITSTREAM_READER
    UINT32 *buffer_ptr;
    UINT32 cache0;
    UINT32 cache1;
    int bit_count;
#endif
    int size;
} GetBitContext;

static inline int get_bits_count(GetBitContext *s);

#define VLC_TYPE INT16

typedef struct VLC {
    int bits;
    VLC_TYPE (*table)[2]; // code, bits
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

#ifndef ALT_BITSTREAM_WRITER
static inline void put_bits(PutBitContext *s, int n, unsigned int value)
{
    unsigned int bit_buf;
    int bit_left;

#ifdef STATS
    st_out_bit_counts[st_current_index] += n;
#endif
    //    printf("put_bits=%d %x\n", n, value);
    assert(n == 32 || value < (1U << n));
    
    bit_buf = s->bit_buf;
    bit_left = s->bit_left;

    //    printf("n=%d value=%x cnt=%d buf=%x\n", n, value, bit_cnt, bit_buf);
    /* XXX: optimize */
    if (n < bit_left) {
        bit_buf = (bit_buf<<n) | value;
        bit_left-=n;
    } else {
	bit_buf<<=bit_left;
        bit_buf |= value >> (n - bit_left);
        *(UINT32 *)s->buf_ptr = be2me_32(bit_buf);
        //printf("bitbuf = %08x\n", bit_buf);
        s->buf_ptr+=4;
	bit_left+=32 - n;
        bit_buf = value;
    }

    s->bit_buf = bit_buf;
    s->bit_left = bit_left;
}
#endif


#ifdef ALT_BITSTREAM_WRITER
static inline void put_bits(PutBitContext *s, int n, unsigned int value)
{
#ifdef ALIGNED_BITSTREAM_WRITER
#ifdef ARCH_X86
    asm volatile(
	"movl %0, %%ecx			\n\t"
	"xorl %%eax, %%eax		\n\t"
	"shrdl %%cl, %1, %%eax		\n\t"
	"shrl %%cl, %1			\n\t"
	"movl %0, %%ecx			\n\t"
	"shrl $3, %%ecx			\n\t"
	"andl $0xFFFFFFFC, %%ecx	\n\t"
	"bswapl %1			\n\t"
	"orl %1, (%2, %%ecx)		\n\t"
	"bswapl %%eax			\n\t"
	"addl %3, %0			\n\t"
	"movl %%eax, 4(%2, %%ecx)	\n\t"
	: "=&r" (s->index), "=&r" (value)
	: "r" (s->buf), "r" (n), "0" (s->index), "1" (value<<(-n))
	: "%eax", "%ecx"
    );
#else
    int index= s->index;
    uint32_t *ptr= ((uint32_t *)s->buf)+(index>>5);
    
    value<<= 32-n; 
    
    ptr[0] |= be2me_32(value>>(index&31));
    ptr[1]  = be2me_32(value<<(32-(index&31)));
//if(n>24) printf("%d %d\n", n, value);
    index+= n;
    s->index= index;
#endif
#else //ALIGNED_BITSTREAM_WRITER
#ifdef ARCH_X86
    asm volatile(
	"movl $7, %%ecx			\n\t"
	"andl %0, %%ecx			\n\t"
	"addl %3, %%ecx			\n\t"
	"negl %%ecx			\n\t"
	"shll %%cl, %1			\n\t"
	"bswapl %1			\n\t"
	"movl %0, %%ecx			\n\t"
	"shrl $3, %%ecx			\n\t"
	"orl %1, (%%ecx, %2)		\n\t"
	"addl %3, %0			\n\t"
	"movl $0, 4(%%ecx, %2)		\n\t"
	: "=&r" (s->index), "=&r" (value)
	: "r" (s->buf), "r" (n), "0" (s->index), "1" (value)
	: "%ecx"
    );
#else
    int index= s->index;
    uint32_t *ptr= (uint32_t*)(((uint8_t *)s->buf)+(index>>3));
    
    ptr[0] |= be2me_32(value<<(32-n-(index&7) ));
    ptr[1] = 0;
//if(n>24) printf("%d %d\n", n, value);
    index+= n;
    s->index= index;
#endif
#endif //!ALIGNED_BITSTREAM_WRITER
}
#endif

#ifndef ALT_BITSTREAM_WRITER
/* for jpeg : escape 0xff with 0x00 after it */
static inline void jput_bits(PutBitContext *s, int n, unsigned int value)
{
    unsigned int bit_buf, b;
    int bit_left, i;
    
    assert(n == 32 || value < (1U << n));

    bit_buf = s->bit_buf;
    bit_left = s->bit_left;

    //printf("n=%d value=%x cnt=%d buf=%x\n", n, value, bit_cnt, bit_buf);
    /* XXX: optimize */
    if (n < bit_left) {
        bit_buf = (bit_buf<<n) | value;
        bit_left-=n;
    } else {
	bit_buf<<=bit_left;
        bit_buf |= value >> (n - bit_left);
        /* handle escape */
        for(i=0;i<4;i++) {
            b = (bit_buf >> 24);
            *(s->buf_ptr++) = b;
            if (b == 0xff)
                *(s->buf_ptr++) = 0;
            bit_buf <<= 8;
        }

	bit_left+= 32 - n;
        bit_buf = value;
    }
    
    s->bit_buf = bit_buf;
    s->bit_left = bit_left;
}
#endif


#ifdef ALT_BITSTREAM_WRITER
static inline void jput_bits(PutBitContext *s, int n, int value)
{
    int index= s->index;
    uint32_t *ptr= (uint32_t*)(((uint8_t *)s->buf)+(index>>3));
    int v= ptr[0];
//if(n>24) printf("%d %d\n", n, value);
    
    v |= be2me_32(value<<(32-n-(index&7) ));
    if(((v+0x01010101)^0xFFFFFFFF)&v&0x80808080)
    {
	/* handle idiotic (m)jpeg escapes */
	uint8_t *bPtr= (uint8_t*)ptr;
	int numChecked= ((index+n)>>3) - (index>>3);
	
	v= be2me_32(v);

	*(bPtr++)= v>>24;
	if((v&0xFF000000)==0xFF000000 && numChecked>0){
		*(bPtr++)= 0x00;
		index+=8;
	}
	*(bPtr++)= (v>>16)&0xFF;
	if((v&0x00FF0000)==0x00FF0000 && numChecked>1){
		*(bPtr++)= 0x00;
		index+=8;
	}
	*(bPtr++)= (v>>8)&0xFF;
	if((v&0x0000FF00)==0x0000FF00 && numChecked>2){
		*(bPtr++)= 0x00;
		index+=8;
	}
	*(bPtr++)= v&0xFF;
	if((v&0x000000FF)==0x000000FF && numChecked>3){
		*(bPtr++)= 0x00;
		index+=8;
	}
	*((uint32_t*)bPtr)= 0;
    }
    else
    {
	ptr[0] = v;
	ptr[1] = 0;
    }

    index+= n;
    s->index= index;
 }
#endif

static inline uint8_t* pbBufPtr(PutBitContext *s)
{
#ifdef ALT_BITSTREAM_WRITER
	return s->buf + (s->index>>3);
#else
	return s->buf_ptr;
#endif
}

/* Bitstream reader API docs:
name
    abritary name which is used as prefix for the internal variables

gb
    getbitcontext

OPEN_READER(name, gb)
    loads gb into local variables

CLOSE_READER(name, gb)
    stores local vars in gb

UPDATE_CACHE(name, gb)
    refills the internal cache from the bitstream
    after this call at least MIN_CACHE_BITS will be available,

GET_CACHE(name, gb)
    will output the contents of the internal cache, next bit is MSB of 32 or 64 bit (FIXME 64bit)

SHOW_UBITS(name, gb, num)
    will return the nest num bits

SHOW_SBITS(name, gb, num)
    will return the nest num bits and do sign extension

SKIP_BITS(name, gb, num)
    will skip over the next num bits
    note, this is equinvalent to SKIP_CACHE; SKIP_COUNTER

SKIP_CACHE(name, gb, num)
    will remove the next num bits from the cache (note SKIP_COUNTER MUST be called before UPDATE_CACHE / CLOSE_READER)

SKIP_COUNTER(name, gb, num)
    will increment the internal bit counter (see SKIP_CACHE & SKIP_BITS)

LAST_SKIP_CACHE(name, gb, num)
    will remove the next num bits from the cache if it is needed for UPDATE_CACHE otherwise it will do nothing

LAST_SKIP_BITS(name, gb, num)
    is equinvalent to SKIP_LAST_CACHE; SKIP_COUNTER

for examples see get_bits, show_bits, skip_bits, get_vlc
*/

#ifdef ALT_BITSTREAM_READER
#   define MIN_CACHE_BITS 25

#   define OPEN_READER(name, gb)\
        int name##_index= (gb)->index;\
        int name##_cache= 0;\

#   define CLOSE_READER(name, gb)\
        (gb)->index= name##_index;\

#   define UPDATE_CACHE(name, gb)\
        name##_cache= be2me_32( unaligned32( ((uint8_t *)(gb)->buffer)+(name##_index>>3) ) ) << (name##_index&0x07);\

#   define SKIP_CACHE(name, gb, num)\
        name##_cache <<= (num);\

// FIXME name?
#   define SKIP_COUNTER(name, gb, num)\
        name##_index += (num);\

#   define SKIP_BITS(name, gb, num)\
        {\
            SKIP_CACHE(name, gb, num)\
            SKIP_COUNTER(name, gb, num)\
        }\

#   define LAST_SKIP_BITS(name, gb, num) SKIP_COUNTER(name, gb, num)
#   define LAST_SKIP_CACHE(name, gb, num) ;

#   define SHOW_UBITS(name, gb, num)\
        NEG_USR32(name##_cache, num)

#   define SHOW_SBITS(name, gb, num)\
        NEG_SSR32(name##_cache, num)

#   define GET_CACHE(name, gb)\
        ((uint32_t)name##_cache)

static inline int get_bits_count(GetBitContext *s){
    return s->index;
}
#elif defined LIBMPEG2_BITSTREAM_READER
//libmpeg2 like reader

#   define MIN_CACHE_BITS 16

#   define OPEN_READER(name, gb)\
        int name##_bit_count=(gb)->bit_count;\
        int name##_cache= (gb)->cache;\
        uint8_t * name##_buffer_ptr=(gb)->buffer_ptr;\

#   define CLOSE_READER(name, gb)\
        (gb)->bit_count= name##_bit_count;\
        (gb)->cache= name##_cache;\
        (gb)->buffer_ptr= name##_buffer_ptr;\

#   define UPDATE_CACHE(name, gb)\
    if(name##_bit_count > 0){\
        name##_cache+= ((name##_buffer_ptr[0]<<8) + name##_buffer_ptr[1]) << name##_bit_count;\
        name##_buffer_ptr+=2;\
        name##_bit_count-= 16;\
    }\

#   define SKIP_CACHE(name, gb, num)\
        name##_cache <<= (num);\

#   define SKIP_COUNTER(name, gb, num)\
        name##_bit_count += (num);\

#   define SKIP_BITS(name, gb, num)\
        {\
            SKIP_CACHE(name, gb, num)\
            SKIP_COUNTER(name, gb, num)\
        }\

#   define LAST_SKIP_BITS(name, gb, num) SKIP_BITS(name, gb, num)
#   define LAST_SKIP_CACHE(name, gb, num) SKIP_CACHE(name, gb, num)

#   define SHOW_UBITS(name, gb, num)\
        NEG_USR32(name##_cache, num)

#   define SHOW_SBITS(name, gb, num)\
        NEG_SSR32(name##_cache, num)

#   define GET_CACHE(name, gb)\
        ((uint32_t)name##_cache)

static inline int get_bits_count(GetBitContext *s){
    return (s->buffer_ptr - s->buffer)*8 - 16 + s->bit_count;
}

#elif defined A32_BITSTREAM_READER

#   define MIN_CACHE_BITS 32

#   define OPEN_READER(name, gb)\
        int name##_bit_count=(gb)->bit_count;\
        uint32_t name##_cache0= (gb)->cache0;\
        uint32_t name##_cache1= (gb)->cache1;\
        uint32_t * name##_buffer_ptr=(gb)->buffer_ptr;\

#   define CLOSE_READER(name, gb)\
        (gb)->bit_count= name##_bit_count;\
        (gb)->cache0= name##_cache0;\
        (gb)->cache1= name##_cache1;\
        (gb)->buffer_ptr= name##_buffer_ptr;\

#   define UPDATE_CACHE(name, gb)\
    if(name##_bit_count > 0){\
        const uint32_t next= be2me_32( *name##_buffer_ptr );\
        name##_cache0 |= NEG_USR32(next,name##_bit_count);\
        name##_cache1 |= next<<name##_bit_count;\
        name##_buffer_ptr++;\
        name##_bit_count-= 32;\
    }\

#ifdef ARCH_X86
#   define SKIP_CACHE(name, gb, num)\
        asm(\
            "shldl %2, %1, %0		\n\t"\
            "shll %2, %1		\n\t"\
            : "+r" (name##_cache0), "+r" (name##_cache1)\
            : "Ic" ((uint8_t)num)\
           );
#else
#   define SKIP_CACHE(name, gb, num)\
        name##_cache0 <<= (num);\
        name##_cache0 |= NEG_USR32(name##_cache1,num);\
        name##_cache1 <<= (num);
#endif

#   define SKIP_COUNTER(name, gb, num)\
        name##_bit_count += (num);\

#   define SKIP_BITS(name, gb, num)\
        {\
            SKIP_CACHE(name, gb, num)\
            SKIP_COUNTER(name, gb, num)\
        }\

#   define LAST_SKIP_BITS(name, gb, num) SKIP_BITS(name, gb, num)
#   define LAST_SKIP_CACHE(name, gb, num) SKIP_CACHE(name, gb, num)

#   define SHOW_UBITS(name, gb, num)\
        NEG_USR32(name##_cache0, num)

#   define SHOW_SBITS(name, gb, num)\
        NEG_SSR32(name##_cache0, num)

#   define GET_CACHE(name, gb)\
        (name##_cache0)

static inline int get_bits_count(GetBitContext *s){
    return ((uint8_t*)s->buffer_ptr - s->buffer)*8 - 32 + s->bit_count;
}

#endif

static inline unsigned int get_bits(GetBitContext *s, int n){
    register int tmp;
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)
    tmp= SHOW_UBITS(re, s, n);
    LAST_SKIP_BITS(re, s, n)
    CLOSE_READER(re, s)
    return tmp;
}

static inline unsigned int show_bits(GetBitContext *s, int n){
    register int tmp;
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)
    tmp= SHOW_UBITS(re, s, n);
//    CLOSE_READER(re, s)
    return tmp;
}

static inline void skip_bits(GetBitContext *s, int n){
 //Note gcc seems to optimize this to s->index+=n for the ALT_READER :))
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)
    LAST_SKIP_BITS(re, s, n)
    CLOSE_READER(re, s)
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
    return get_bits(s, 1);
#endif
}

static inline unsigned int show_bits1(GetBitContext *s){
    return show_bits(s, 1);
}

static inline void skip_bits1(GetBitContext *s){
    skip_bits(s, 1);
}

void init_get_bits(GetBitContext *s,
                   UINT8 *buffer, int buffer_size);

int check_marker(GetBitContext *s, char *msg);
void align_get_bits(GetBitContext *s);
int init_vlc(VLC *vlc, int nb_bits, int nb_codes,
             const void *bits, int bits_wrap, int bits_size,
             const void *codes, int codes_wrap, int codes_size);
void free_vlc(VLC *vlc);

//note table will be trashed (pointer increased)
#define GET_VLC(code, name, gb, table, bits, max_depth)\
{\
    int n, index, nb_bits;\
\
    index= SHOW_UBITS(name, gb, bits);\
    code = table[index][0];\
    n    = table[index][1];\
\
    if(max_depth > 1 && n < 0){\
        LAST_SKIP_BITS(name, gb, bits)\
        UPDATE_CACHE(name, gb)\
\
        nb_bits = -n;\
        table += code;\
\
        index= SHOW_UBITS(name, gb, nb_bits);\
        code = table[index][0];\
        n    = table[index][1];\
        if(max_depth > 2 && n < 0){\
            LAST_SKIP_BITS(name, gb, nb_bits)\
            UPDATE_CACHE(name, gb)\
\
            nb_bits = -n;\
            table += code;\
\
            index= SHOW_UBITS(name, gb, nb_bits);\
            code = table[index][0];\
            n    = table[index][1];\
        }\
    }\
    SKIP_BITS(name, gb, n)\
}

// deprecated, dont use get_vlc for new code, use get_vlc2 instead or use GET_VLC directly
static inline int get_vlc(GetBitContext *s, VLC *vlc)
{
    int code;
    VLC_TYPE (*table)[2]= vlc->table;
    
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)

    GET_VLC(code, re, s, table, vlc->bits, 3)    

    CLOSE_READER(re, s)
    return code;
}

static inline int get_vlc2(GetBitContext *s, VLC_TYPE (*table)[2], int bits, int max_depth)
{
    int code;
    
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)

    GET_VLC(code, re, s, table, bits, max_depth)

    CLOSE_READER(re, s)
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

/* median of 3 */
static inline int mid_pred(int a, int b, int c)
{
    int vmin, vmax;
    vmax = vmin = a;
    if (b < vmin)
        vmin = b;
    else
	vmax = b;

    if (c < vmin)
        vmin = c;
    else if (c > vmax)
        vmax = c;

    return a + b + c - vmin - vmax;
}

static inline int clip(int a, int amin, int amax)
{
    if (a < amin)
        return amin;
    else if (a > amax)
        return amax;
    else
        return a;
}

/* math */
int ff_gcd(int a, int b);

static inline int ff_sqrt(int a)
{
    int ret=0;
    int s;
    int ret_sq=0;

    for(s=15; s>=0; s--){
        int b= ret_sq + (1<<(s*2)) + (ret<<s)*2;
        if(b<=a){
            ret_sq=b;
            ret+= 1<<s;
        }
    }
    return ret;
}
#if __CPU__ >= 686 && !defined(RUNTIME_CPUDETECT)
#define COPY3_IF_LT(x,y,a,b,c,d)\
asm volatile (\
    "cmpl %0, %3	\n\t"\
    "cmovl %3, %0	\n\t"\
    "cmovl %4, %1	\n\t"\
    "cmovl %5, %2	\n\t"\
    : "+r" (x), "+r" (a), "+r" (c)\
    : "r" (y), "r" (b), "r" (d)\
);
#else
#define COPY3_IF_LT(x,y,a,b,c,d)\
if((y)<(x)){\
     (x)=(y);\
     (a)=(b);\
     (c)=(d);\
}
#endif

#define CLAMP_TO_8BIT(d) ((d > 0xff) ? 0xff : (d < 0) ? 0 : d)

#endif /* HAVE_AV_CONFIG_H */

#endif /* COMMON_H */
