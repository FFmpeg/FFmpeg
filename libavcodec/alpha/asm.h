/*
 * Alpha optimized DSP utils
 * Copyright (c) 2002 Falk Hueffner <falk@debian.org>
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

#ifndef AVCODEC_ALPHA_ASM_H
#define AVCODEC_ALPHA_ASM_H

#include <inttypes.h>

#include "libavutil/common.h"

#if AV_GCC_VERSION_AT_LEAST(2,96)
# define likely(x)      __builtin_expect((x) != 0, 1)
# define unlikely(x)    __builtin_expect((x) != 0, 0)
#else
# define likely(x)      (x)
# define unlikely(x)    (x)
#endif

#define AMASK_BWX (1 << 0)
#define AMASK_FIX (1 << 1)
#define AMASK_CIX (1 << 2)
#define AMASK_MVI (1 << 8)

static inline uint64_t BYTE_VEC(uint64_t x)
{
    x |= x <<  8;
    x |= x << 16;
    x |= x << 32;
    return x;
}
static inline uint64_t WORD_VEC(uint64_t x)
{
    x |= x << 16;
    x |= x << 32;
    return x;
}

#define sextw(x) ((int16_t) (x))

#ifdef __GNUC__
#define ldq(p)                                                  \
    (((const union {                                            \
        uint64_t __l;                                           \
        __typeof__(*(p)) __s[sizeof (uint64_t) / sizeof *(p)];  \
    } *) (p))->__l)
#define ldl(p)                                                  \
    (((const union {                                            \
        int32_t __l;                                            \
        __typeof__(*(p)) __s[sizeof (int32_t) / sizeof *(p)];   \
    } *) (p))->__l)
#define stq(l, p)                                                       \
    do {                                                                \
        (((union {                                                      \
            uint64_t __l;                                               \
            __typeof__(*(p)) __s[sizeof (uint64_t) / sizeof *(p)];      \
        } *) (p))->__l) = l;                                            \
    } while (0)
#define stl(l, p)                                                       \
    do {                                                                \
        (((union {                                                      \
            int32_t __l;                                                \
            __typeof__(*(p)) __s[sizeof (int32_t) / sizeof *(p)];       \
        } *) (p))->__l) = l;                                            \
    } while (0)
struct unaligned_long { uint64_t l; } __attribute__((packed));
#define ldq_u(p)        (*(const uint64_t *) (((uint64_t) (p)) & ~7ul))
#define uldq(a)         (((const struct unaligned_long *) (a))->l)

#if AV_GCC_VERSION_AT_LEAST(3,3)
#define prefetch(p)     __builtin_prefetch((p), 0, 1)
#define prefetch_en(p)  __builtin_prefetch((p), 0, 0)
#define prefetch_m(p)   __builtin_prefetch((p), 1, 1)
#define prefetch_men(p) __builtin_prefetch((p), 1, 0)
#define cmpbge          __builtin_alpha_cmpbge
/* Avoid warnings.  */
#define extql(a, b)     __builtin_alpha_extql(a, (uint64_t) (b))
#define extwl(a, b)     __builtin_alpha_extwl(a, (uint64_t) (b))
#define extqh(a, b)     __builtin_alpha_extqh(a, (uint64_t) (b))
#define zap             __builtin_alpha_zap
#define zapnot          __builtin_alpha_zapnot
#define amask           __builtin_alpha_amask
#define implver         __builtin_alpha_implver
#define rpcc            __builtin_alpha_rpcc
#else
#define prefetch(p)     __asm__ volatile("ldl $31,%0"  : : "m"(*(const char *) (p)) : "memory")
#define prefetch_en(p)  __asm__ volatile("ldq $31,%0"  : : "m"(*(const char *) (p)) : "memory")
#define prefetch_m(p)   __asm__ volatile("lds $f31,%0" : : "m"(*(const char *) (p)) : "memory")
#define prefetch_men(p) __asm__ volatile("ldt $f31,%0" : : "m"(*(const char *) (p)) : "memory")
#define cmpbge(a, b) ({ uint64_t __r; __asm__ ("cmpbge  %r1,%2,%0"  : "=r" (__r) : "rJ"  (a), "rI" (b)); __r; })
#define extql(a, b)  ({ uint64_t __r; __asm__ ("extql   %r1,%2,%0"  : "=r" (__r) : "rJ"  (a), "rI" (b)); __r; })
#define extwl(a, b)  ({ uint64_t __r; __asm__ ("extwl   %r1,%2,%0"  : "=r" (__r) : "rJ"  (a), "rI" (b)); __r; })
#define extqh(a, b)  ({ uint64_t __r; __asm__ ("extqh   %r1,%2,%0"  : "=r" (__r) : "rJ"  (a), "rI" (b)); __r; })
#define zap(a, b)    ({ uint64_t __r; __asm__ ("zap     %r1,%2,%0"  : "=r" (__r) : "rJ"  (a), "rI" (b)); __r; })
#define zapnot(a, b) ({ uint64_t __r; __asm__ ("zapnot  %r1,%2,%0"  : "=r" (__r) : "rJ"  (a), "rI" (b)); __r; })
#define amask(a)     ({ uint64_t __r; __asm__ ("amask   %1,%0"      : "=r" (__r) : "rI"  (a));           __r; })
#define implver()    ({ uint64_t __r; __asm__ ("implver %0"         : "=r" (__r));                       __r; })
#define rpcc()       ({ uint64_t __r; __asm__ volatile ("rpcc %0"   : "=r" (__r));                       __r; })
#endif
#define wh64(p) __asm__ volatile("wh64 (%0)" : : "r"(p) : "memory")

#if AV_GCC_VERSION_AT_LEAST(3,3) && defined(__alpha_max__)
#define minub8  __builtin_alpha_minub8
#define minsb8  __builtin_alpha_minsb8
#define minuw4  __builtin_alpha_minuw4
#define minsw4  __builtin_alpha_minsw4
#define maxub8  __builtin_alpha_maxub8
#define maxsb8  __builtin_alpha_maxsb8
#define maxuw4  __builtin_alpha_maxuw4
#define maxsw4  __builtin_alpha_maxsw4
#define perr    __builtin_alpha_perr
#define pklb    __builtin_alpha_pklb
#define pkwb    __builtin_alpha_pkwb
#define unpkbl  __builtin_alpha_unpkbl
#define unpkbw  __builtin_alpha_unpkbw
#else
#define minub8(a, b) ({ uint64_t __r; __asm__ (".arch ev6; minub8  %r1,%2,%0"  : "=r" (__r) : "%rJ" (a), "rI" (b)); __r; })
#define minsb8(a, b) ({ uint64_t __r; __asm__ (".arch ev6; minsb8  %r1,%2,%0"  : "=r" (__r) : "%rJ" (a), "rI" (b)); __r; })
#define minuw4(a, b) ({ uint64_t __r; __asm__ (".arch ev6; minuw4  %r1,%2,%0"  : "=r" (__r) : "%rJ" (a), "rI" (b)); __r; })
#define minsw4(a, b) ({ uint64_t __r; __asm__ (".arch ev6; minsw4  %r1,%2,%0"  : "=r" (__r) : "%rJ" (a), "rI" (b)); __r; })
#define maxub8(a, b) ({ uint64_t __r; __asm__ (".arch ev6; maxub8  %r1,%2,%0"  : "=r" (__r) : "%rJ" (a), "rI" (b)); __r; })
#define maxsb8(a, b) ({ uint64_t __r; __asm__ (".arch ev6; maxsb8  %r1,%2,%0"  : "=r" (__r) : "%rJ" (a), "rI" (b)); __r; })
#define maxuw4(a, b) ({ uint64_t __r; __asm__ (".arch ev6; maxuw4  %r1,%2,%0"  : "=r" (__r) : "%rJ" (a), "rI" (b)); __r; })
#define maxsw4(a, b) ({ uint64_t __r; __asm__ (".arch ev6; maxsw4  %r1,%2,%0"  : "=r" (__r) : "%rJ" (a), "rI" (b)); __r; })
#define perr(a, b)   ({ uint64_t __r; __asm__ (".arch ev6; perr    %r1,%r2,%0" : "=r" (__r) : "%rJ" (a), "rJ" (b)); __r; })
#define pklb(a)      ({ uint64_t __r; __asm__ (".arch ev6; pklb    %r1,%0"     : "=r" (__r) : "rJ"  (a));           __r; })
#define pkwb(a)      ({ uint64_t __r; __asm__ (".arch ev6; pkwb    %r1,%0"     : "=r" (__r) : "rJ"  (a));           __r; })
#define unpkbl(a)    ({ uint64_t __r; __asm__ (".arch ev6; unpkbl  %r1,%0"     : "=r" (__r) : "rJ"  (a));           __r; })
#define unpkbw(a)    ({ uint64_t __r; __asm__ (".arch ev6; unpkbw  %r1,%0"     : "=r" (__r) : "rJ"  (a));           __r; })
#endif

#elif defined(__DECC)           /* Digital/Compaq/hp "ccc" compiler */

#include <c_asm.h>
#define ldq(p) (*(const uint64_t *) (p))
#define ldl(p) (*(const int32_t *)  (p))
#define stq(l, p) do { *(uint64_t *) (p) = (l); } while (0)
#define stl(l, p) do { *(int32_t *)  (p) = (l); } while (0)
#define ldq_u(a)     asm ("ldq_u   %v0,0(%a0)", a)
#define uldq(a)      (*(const __unaligned uint64_t *) (a))
#define cmpbge(a, b) asm ("cmpbge  %a0,%a1,%v0", a, b)
#define extql(a, b)  asm ("extql   %a0,%a1,%v0", a, b)
#define extwl(a, b)  asm ("extwl   %a0,%a1,%v0", a, b)
#define extqh(a, b)  asm ("extqh   %a0,%a1,%v0", a, b)
#define zap(a, b)    asm ("zap     %a0,%a1,%v0", a, b)
#define zapnot(a, b) asm ("zapnot  %a0,%a1,%v0", a, b)
#define amask(a)     asm ("amask   %a0,%v0", a)
#define implver()    asm ("implver %v0")
#define rpcc()       asm ("rpcc           %v0")
#define minub8(a, b) asm ("minub8  %a0,%a1,%v0", a, b)
#define minsb8(a, b) asm ("minsb8  %a0,%a1,%v0", a, b)
#define minuw4(a, b) asm ("minuw4  %a0,%a1,%v0", a, b)
#define minsw4(a, b) asm ("minsw4  %a0,%a1,%v0", a, b)
#define maxub8(a, b) asm ("maxub8  %a0,%a1,%v0", a, b)
#define maxsb8(a, b) asm ("maxsb8  %a0,%a1,%v0", a, b)
#define maxuw4(a, b) asm ("maxuw4  %a0,%a1,%v0", a, b)
#define maxsw4(a, b) asm ("maxsw4  %a0,%a1,%v0", a, b)
#define perr(a, b)   asm ("perr    %a0,%a1,%v0", a, b)
#define pklb(a)      asm ("pklb    %a0,%v0", a)
#define pkwb(a)      asm ("pkwb    %a0,%v0", a)
#define unpkbl(a)    asm ("unpkbl  %a0,%v0", a)
#define unpkbw(a)    asm ("unpkbw  %a0,%v0", a)
#define wh64(a)      asm ("wh64    %a0", a)

#else
#error "Unknown compiler!"
#endif

#endif /* AVCODEC_ALPHA_ASM_H */
