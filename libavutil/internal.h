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
 * @file internal.h
 * common internal api header.
 */

#ifndef INTERNAL_H
#define INTERNAL_H

#ifndef attribute_used
#if defined(__GNUC__) && (__GNUC__ > 3 || __GNUC__ == 3 && __GNUC_MINOR__ > 0)
#    define attribute_used __attribute__((used))
#else
#    define attribute_used
#endif
#endif

#ifndef attribute_unused
#if defined(__GNUC__)
#    define attribute_unused __attribute__((unused))
#else
#    define attribute_unused
#endif
#endif

#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif

#ifndef INT16_MIN
#define INT16_MIN       (-0x7fff-1)
#endif

#ifndef INT16_MAX
#define INT16_MAX       0x7fff
#endif

#ifndef INT32_MIN
#define INT32_MIN       (-0x7fffffff-1)
#endif

#ifndef INT32_MAX
#define INT32_MAX       0x7fffffff
#endif

#ifndef UINT32_MAX
#define UINT32_MAX      0xffffffff
#endif

#ifndef INT64_MIN
#define INT64_MIN       (-0x7fffffffffffffffLL-1)
#endif

#ifndef INT64_MAX
#define INT64_MAX INT64_C(9223372036854775807)
#endif

#ifndef UINT64_MAX
#define UINT64_MAX UINT64_C(0xFFFFFFFFFFFFFFFF)
#endif

#ifndef INT_BIT
#    if INT_MAX != 2147483647
#        define INT_BIT 64
#    else
#        define INT_BIT 32
#    endif
#endif

#if ( defined(__PIC__) || defined(__pic__) ) && ! defined(PIC)
#    define PIC
#endif

#include "intreadwrite.h"
#include "bswap.h"

#include <stddef.h>
#ifndef offsetof
#    define offsetof(T,F) ((unsigned int)((char *)&((T *)0)->F))
#endif

#ifdef __MINGW32__
#    ifdef _DEBUG
#        define DEBUG
#    endif

#    define snprintf _snprintf
#    define vsnprintf _vsnprintf

#    ifdef CONFIG_WINCE
#        define perror(a)
#        define abort()
#    endif

/* __MINGW32__ end */
#elif defined (CONFIG_OS2)
/* OS/2 EMX */

#    include <float.h>

#endif /* !__MINGW32__ && CONFIG_OS2 */

#ifdef USE_FASTMEMCPY
#    include "libvo/fastmemcpy.h"
#endif

// Use rip-relative addressing if compiling PIC code on x86-64.
#if defined(__MINGW32__) || defined(__CYGWIN__) || \
    defined(__OS2__) || (defined (__OpenBSD__) && !defined(__ELF__))
#    if defined(ARCH_X86_64) && defined(PIC)
#        define MANGLE(a) "_" #a"(%%rip)"
#    else
#        define MANGLE(a) "_" #a
#    endif
#else
#    if defined(ARCH_X86_64) && defined(PIC)
#        define MANGLE(a) #a"(%%rip)"
#    elif defined(CONFIG_DARWIN)
#        define MANGLE(a) "_" #a
#    else
#        define MANGLE(a) #a
#    endif
#endif

/* debug stuff */

#if !defined(DEBUG) && !defined(NDEBUG)
#    define NDEBUG
#endif
#include <assert.h>

/* dprintf macros */
#ifdef DEBUG
#    define dprintf(pctx, ...) av_log(pctx, AV_LOG_DEBUG, __VA_ARGS__)
#else
#    define dprintf(pctx, ...)
#endif

#define av_abort()      do { av_log(NULL, AV_LOG_ERROR, "Abort at %s:%d\n", __FILE__, __LINE__); abort(); } while (0)

/* math */

extern const uint32_t ff_inverse[256];

#if defined(ARCH_X86)
#    define FASTDIV(a,b) \
    ({\
        int ret,dmy;\
        asm volatile(\
            "mull %3"\
            :"=d"(ret),"=a"(dmy)\
            :"1"(a),"g"(ff_inverse[b])\
            );\
        ret;\
    })
#elif defined(ARCH_ARMV4L)
#    define FASTDIV(a,b) \
    ({\
        int ret,dmy;\
        asm volatile(\
            "umull %1, %0, %2, %3"\
            :"=&r"(ret),"=&r"(dmy)\
            :"r"(a),"r"(ff_inverse[b])\
            );\
        ret;\
    })
#elif defined(CONFIG_FASTDIV)
#    define FASTDIV(a,b)   ((uint32_t)((((uint64_t)a)*ff_inverse[b])>>32))
#else
#    define FASTDIV(a,b)   ((a)/(b))
#endif

extern const uint8_t ff_sqrt_tab[128];

static inline int ff_sqrt(int a)
{
    int ret=0;
    int s;
    int ret_sq=0;

    if(a<128) return ff_sqrt_tab[a];

    for(s=15; s>=0; s--){
        int b= ret_sq + (1<<(s*2)) + (ret<<s)*2;
        if(b<=a){
            ret_sq=b;
            ret+= 1<<s;
        }
    }
    return ret;
}

#if defined(ARCH_X86)
#define MASK_ABS(mask, level)\
            asm volatile(\
                "cdq                    \n\t"\
                "xorl %1, %0            \n\t"\
                "subl %1, %0            \n\t"\
                : "+a" (level), "=&d" (mask)\
            );
#else
#define MASK_ABS(mask, level)\
            mask= level>>31;\
            level= (level^mask)-mask;
#endif

#ifdef HAVE_CMOV
#define COPY3_IF_LT(x,y,a,b,c,d)\
asm volatile (\
    "cmpl %0, %3        \n\t"\
    "cmovl %3, %0       \n\t"\
    "cmovl %4, %1       \n\t"\
    "cmovl %5, %2       \n\t"\
    : "+&r" (x), "+&r" (a), "+r" (c)\
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

/* avoid usage of various functions */
#define malloc please_use_av_malloc
#define free please_use_av_free
#define realloc please_use_av_realloc
#define time time_is_forbidden_due_to_security_issues
#define rand rand_is_forbidden_due_to_state_trashing
#define srand srand_is_forbidden_due_to_state_trashing
#define sprintf sprintf_is_forbidden_due_to_security_issues_use_snprintf
#define strcat strcat_is_forbidden_due_to_security_issues_use_pstrcat
#define exit exit_is_forbidden
#if !(defined(LIBAVFORMAT_BUILD) || defined(_FRAMEHOOK_H))
#define printf please_use_av_log
#define fprintf please_use_av_log
#endif

#define CHECKED_ALLOCZ(p, size)\
{\
    p= av_mallocz(size);\
    if(p==NULL && (size)!=0){\
        perror("malloc");\
        goto fail;\
    }\
}

#ifndef HAVE_LRINTF
/* XXX: add ISOC specific test to avoid specific BSD testing. */
/* better than nothing implementation. */
/* btw, rintf() is existing on fbsd too -- alex */
static av_always_inline long int lrintf(float x)
{
#ifdef __MINGW32__
#  ifdef ARCH_X86_32
    int32_t i;
    asm volatile(
        "fistpl %0\n\t"
        : "=m" (i) : "t" (x) : "st"
    );
    return i;
#  else
    /* XXX: incorrect, but make it compile */
    return (int)(x + (x < 0 ? -0.5 : 0.5));
#  endif /* ARCH_X86_32 */
#else
    return (int)(rint(x));
#endif /* __MINGW32__ */
}
#endif /* HAVE_LRINTF */

#endif /* INTERNAL_H */
