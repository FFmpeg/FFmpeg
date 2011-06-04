/*
 * simple math operations
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at> et al
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_ARM_MATHOPS_H
#define AVCODEC_ARM_MATHOPS_H

#include <stdint.h>
#include "config.h"
#include "libavutil/common.h"

#if HAVE_INLINE_ASM

#define MULH MULH
#define MUL64 MUL64

#if HAVE_ARMV6
static inline av_const int MULH(int a, int b)
{
    int r;
    __asm__ ("smmul %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

static inline av_const int64_t MUL64(int a, int b)
{
    int64_t x;
    __asm__ ("smull %Q0, %R0, %1, %2" : "=r"(x) : "r"(a), "r"(b));
    return x;
}
#else
static inline av_const int MULH(int a, int b)
{
    int lo, hi;
    __asm__ ("smull %0, %1, %2, %3" : "=&r"(lo), "=&r"(hi) : "r"(b), "r"(a));
    return hi;
}

static inline av_const int64_t MUL64(int a, int b)
{
    int64_t x;
    __asm__ ("smull %Q0, %R0, %1, %2" : "=&r"(x) : "r"(a), "r"(b));
    return x;
}
#endif

static inline av_const int64_t MAC64(int64_t d, int a, int b)
{
    __asm__ ("smlal %Q0, %R0, %1, %2" : "+r"(d) : "r"(a), "r"(b));
    return d;
}
#define MAC64(d, a, b) ((d) = MAC64(d, a, b))
#define MLS64(d, a, b) MAC64(d, -(a), b)

#if HAVE_ARMV5TE

/* signed 16x16 -> 32 multiply add accumulate */
#   define MAC16(rt, ra, rb)                                            \
    __asm__ ("smlabb %0, %1, %2, %0" : "+r"(rt) : "r"(ra), "r"(rb));

/* signed 16x16 -> 32 multiply */
#   define MUL16 MUL16
static inline av_const int MUL16(int ra, int rb)
{
    int rt;
    __asm__ ("smulbb %0, %1, %2" : "=r"(rt) : "r"(ra), "r"(rb));
    return rt;
}

#endif

#define mid_pred mid_pred
static inline av_const int mid_pred(int a, int b, int c)
{
    int m;
    __asm__ (
        "mov   %0, %2  \n\t"
        "cmp   %1, %2  \n\t"
        "movgt %0, %1  \n\t"
        "movgt %1, %2  \n\t"
        "cmp   %1, %3  \n\t"
        "movle %1, %3  \n\t"
        "cmp   %0, %1  \n\t"
        "movgt %0, %1  \n\t"
        : "=&r"(m), "+r"(a)
        : "r"(b), "r"(c)
        : "cc");
    return m;
}

#endif /* HAVE_INLINE_ASM */

#endif /* AVCODEC_ARM_MATHOPS_H */
