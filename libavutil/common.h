/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file common.h
 * common internal and external API header
 */

#ifndef FFMPEG_COMMON_H
#define FFMPEG_COMMON_H

#include <inttypes.h>

#ifdef HAVE_AV_CONFIG_H
/* only include the following when compiling package */
#    include "config.h"

#    include <stdlib.h>
#    include <stdio.h>
#    include <string.h>
#    include <ctype.h>
#    include <limits.h>
#    include <errno.h>
#    include <math.h>
#endif /* HAVE_AV_CONFIG_H */

#ifndef av_always_inline
#if defined(__GNUC__) && (__GNUC__ > 3 || __GNUC__ == 3 && __GNUC_MINOR__ > 0)
#    define av_always_inline __attribute__((always_inline)) inline
#else
#    define av_always_inline inline
#endif
#endif

#ifndef av_noinline
#if defined(__GNUC__) && (__GNUC__ > 3 || __GNUC__ == 3 && __GNUC_MINOR__ > 0)
#    define av_noinline __attribute__((noinline))
#else
#    define av_noinline
#endif
#endif

#ifndef av_pure
#if defined(__GNUC__) && (__GNUC__ > 3 || __GNUC__ == 3 && __GNUC_MINOR__ > 0)
#    define av_pure __attribute__((pure))
#else
#    define av_pure
#endif
#endif

#ifndef av_const
#if defined(__GNUC__) && (__GNUC__ > 2 || __GNUC__ == 2 && __GNUC_MINOR__ > 5)
#    define av_const __attribute__((const))
#else
#    define av_const
#endif
#endif

#ifndef av_cold
#if defined(__GNUC__) && (__GNUC__ > 4 || __GNUC__ == 4 && __GNUC_MINOR__ > 2)
#    define av_cold __attribute__((cold))
#else
#    define av_cold
#endif
#endif

#ifdef HAVE_AV_CONFIG_H
#    include "internal.h"
#endif /* HAVE_AV_CONFIG_H */

#ifndef attribute_deprecated
#if defined(__GNUC__) && (__GNUC__ > 3 || __GNUC__ == 3 && __GNUC_MINOR__ > 0)
#    define attribute_deprecated __attribute__((deprecated))
#else
#    define attribute_deprecated
#endif
#endif

#ifndef av_unused
#if defined(__GNUC__)
#    define av_unused __attribute__((unused))
#else
#    define av_unused
#endif
#endif

#include "mem.h"

//rounded divison & shift
#define RSHIFT(a,b) ((a) > 0 ? ((a) + ((1<<(b))>>1))>>(b) : ((a) + ((1<<(b))>>1)-1)>>(b))
/* assume b>0 */
#define ROUNDED_DIV(a,b) (((a)>0 ? (a) + ((b)>>1) : (a) - ((b)>>1))/(b))
#define FFABS(a) ((a) >= 0 ? (a) : (-(a)))
#define FFSIGN(a) ((a) > 0 ? 1 : -1)

#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))

#define FFSWAP(type,a,b) do{type SWAP_tmp= b; b= a; a= SWAP_tmp;}while(0)

/* misc math functions */
extern const uint8_t ff_log2_tab[256];

static inline av_const int av_log2(unsigned int v)
{
    int n = 0;
    if (v & 0xffff0000) {
        v >>= 16;
        n += 16;
    }
    if (v & 0xff00) {
        v >>= 8;
        n += 8;
    }
    n += ff_log2_tab[v];

    return n;
}

static inline av_const int av_log2_16bit(unsigned int v)
{
    int n = 0;
    if (v & 0xff00) {
        v >>= 8;
        n += 8;
    }
    n += ff_log2_tab[v];

    return n;
}

/* median of 3 */
static inline av_const int mid_pred(int a, int b, int c)
{
#ifdef HAVE_CMOV
    int i=b;
    asm volatile(
        "cmp    %2, %1 \n\t"
        "cmovg  %1, %0 \n\t"
        "cmovg  %2, %1 \n\t"
        "cmp    %3, %1 \n\t"
        "cmovl  %3, %1 \n\t"
        "cmp    %1, %0 \n\t"
        "cmovg  %1, %0 \n\t"
        :"+&r"(i), "+&r"(a)
        :"r"(b), "r"(c)
    );
    return i;
#elif 0
    int t= (a-b)&((a-b)>>31);
    a-=t;
    b+=t;
    b-= (b-c)&((b-c)>>31);
    b+= (a-b)&((a-b)>>31);

    return b;
#else
    if(a>b){
        if(c>b){
            if(c>a) b=a;
            else    b=c;
        }
    }else{
        if(b>c){
            if(c>a) b=c;
            else    b=a;
        }
    }
    return b;
#endif
}

/**
 * clip a signed integer value into the amin-amax range
 * @param a value to clip
 * @param amin minimum value of the clip range
 * @param amax maximum value of the clip range
 * @return clipped value
 */
static inline av_const int av_clip(int a, int amin, int amax)
{
    if      (a < amin) return amin;
    else if (a > amax) return amax;
    else               return a;
}

/**
 * clip a signed integer value into the 0-255 range
 * @param a value to clip
 * @return clipped value
 */
static inline av_const uint8_t av_clip_uint8(int a)
{
    if (a&(~255)) return (-a)>>31;
    else          return a;
}

/**
 * clip a signed integer value into the -32768,32767 range
 * @param a value to clip
 * @return clipped value
 */
static inline av_const int16_t av_clip_int16(int a)
{
    if ((a+32768) & ~65535) return (a>>31) ^ 32767;
    else                    return a;
}

/* math */
int64_t av_const ff_gcd(int64_t a, int64_t b);

/**
 * converts fourcc string to int
 */
static inline av_pure int ff_get_fourcc(const char *s){
#ifdef HAVE_AV_CONFIG_H
    assert( strlen(s)==4 );
#endif

    return (s[0]) + (s[1]<<8) + (s[2]<<16) + (s[3]<<24);
}

#define MKTAG(a,b,c,d) (a | (b << 8) | (c << 16) | (d << 24))
#define MKBETAG(a,b,c,d) (d | (c << 8) | (b << 16) | (a << 24))

/*!
 * \def GET_UTF8(val, GET_BYTE, ERROR)
 * converts a UTF-8 character (up to 4 bytes long) to its 32-bit UCS-4 encoded form
 * \param val is the output and should be of type uint32_t. It holds the converted
 * UCS-4 character and should be a left value.
 * \param GET_BYTE gets UTF-8 encoded bytes from any proper source. It can be
 * a function or a statement whose return value or evaluated value is of type
 * uint8_t. It will be executed up to 4 times for values in the valid UTF-8 range,
 * and up to 7 times in the general case.
 * \param ERROR action that should be taken when an invalid UTF-8 byte is returned
 * from GET_BYTE. It should be a statement that jumps out of the macro,
 * like exit(), goto, return, break, or continue.
 */
#define GET_UTF8(val, GET_BYTE, ERROR)\
    val= GET_BYTE;\
    {\
        int ones= 7 - av_log2(val ^ 255);\
        if(ones==1)\
            ERROR\
        val&= 127>>ones;\
        while(--ones > 0){\
            int tmp= GET_BYTE - 128;\
            if(tmp>>6)\
                ERROR\
            val= (val<<6) + tmp;\
        }\
    }

/*!
 * \def PUT_UTF8(val, tmp, PUT_BYTE)
 * converts a 32-bit unicode character to its UTF-8 encoded form (up to 4 bytes long).
 * \param val is an input only argument and should be of type uint32_t. It holds
 * a ucs4 encoded unicode character that is to be converted to UTF-8. If
 * val is given as a function it's executed only once.
 * \param tmp is a temporary variable and should be of type uint8_t. It
 * represents an intermediate value during conversion that is to be
 * outputted by PUT_BYTE.
 * \param PUT_BYTE writes the converted UTF-8 bytes to any proper destination.
 * It could be a function or a statement, and uses tmp as the input byte.
 * For example, PUT_BYTE could be "*output++ = tmp;" PUT_BYTE will be
 * executed up to 4 times for values in the valid UTF-8 range and up to
 * 7 times in the general case, depending on the length of the converted
 * unicode character.
 */
#define PUT_UTF8(val, tmp, PUT_BYTE)\
    {\
        int bytes, shift;\
        uint32_t in = val;\
        if (in < 0x80) {\
            tmp = in;\
            PUT_BYTE\
        } else {\
            bytes = (av_log2(in) + 4) / 5;\
            shift = (bytes - 1) * 6;\
            tmp = (256 - (256 >> bytes)) | (in >> shift);\
            PUT_BYTE\
            while (shift >= 6) {\
                shift -= 6;\
                tmp = 0x80 | ((in >> shift) & 0x3f);\
                PUT_BYTE\
            }\
        }\
    }

#if defined(ARCH_X86) || defined(ARCH_POWERPC) || defined(ARCH_BFIN)
#define AV_READ_TIME read_time
#if defined(ARCH_X86_64)
static inline uint64_t read_time(void)
{
    uint64_t a, d;
    asm volatile("rdtsc\n\t"
                 : "=a" (a), "=d" (d));
    return (d << 32) | (a & 0xffffffff);
}
#elif defined(ARCH_X86_32)
static inline long long read_time(void)
{
    long long l;
    asm volatile("rdtsc\n\t"
                 : "=A" (l));
    return l;
}
#elif ARCH_BFIN
static inline uint64_t read_time(void)
{
    union {
        struct {
            unsigned lo;
            unsigned hi;
        } p;
        unsigned long long c;
    } t;
    asm volatile ("%0=cycles; %1=cycles2;" : "=d" (t.p.lo), "=d" (t.p.hi));
    return t.c;
}
#else //FIXME check ppc64
static inline uint64_t read_time(void)
{
    uint32_t tbu, tbl, temp;

     /* from section 2.2.1 of the 32-bit PowerPC PEM */
     __asm__ __volatile__(
         "1:\n"
         "mftbu  %2\n"
         "mftb   %0\n"
         "mftbu  %1\n"
         "cmpw   %2,%1\n"
         "bne    1b\n"
     : "=r"(tbl), "=r"(tbu), "=r"(temp)
     :
     : "cc");

     return (((uint64_t)tbu)<<32) | (uint64_t)tbl;
}
#endif
#elif defined(HAVE_GETHRTIME)
#define AV_READ_TIME gethrtime
#endif

#ifdef AV_READ_TIME
#define START_TIMER \
uint64_t tend;\
uint64_t tstart= AV_READ_TIME();\

#define STOP_TIMER(id) \
tend= AV_READ_TIME();\
{\
    static uint64_t tsum=0;\
    static int tcount=0;\
    static int tskip_count=0;\
    if(tcount<2 || tend - tstart < FFMAX(8*tsum/tcount, 2000)){\
        tsum+= tend - tstart;\
        tcount++;\
    }else\
        tskip_count++;\
    if(((tcount+tskip_count)&(tcount+tskip_count-1))==0){\
        av_log(NULL, AV_LOG_DEBUG, "%"PRIu64" dezicycles in %s, %d runs, %d skips\n",\
               tsum*10/tcount, id, tcount, tskip_count);\
    }\
}
#else
#define START_TIMER
#define STOP_TIMER(id) {}
#endif

#endif /* FFMPEG_COMMON_H */
