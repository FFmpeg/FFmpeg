/*
 * simple math operations
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at> et al
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

#ifndef AVCODEC_ARM_MATHOPS_H
#define AVCODEC_ARM_MATHOPS_H

#include <stdint.h>
#include "libavutil/common.h"

#   define MULL MULL
static inline av_const int MULL(int a, int b, unsigned shift)
{
    int lo, hi;
    __asm__("smull %0, %1, %2, %3     \n\t"
            "mov   %0, %0,     lsr %4 \n\t"
            "add   %1, %0, %1, lsl %5 \n\t"
            : "=&r"(lo), "=&r"(hi)
            : "r"(b), "r"(a), "ir"(shift), "ir"(32-shift));
    return hi;
}

#define MULH MULH
#if HAVE_ARMV6
static inline av_const int MULH(int a, int b)
{
    int r;
    __asm__ ("smmul %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}
#else
static inline av_const int MULH(int a, int b)
{
    int lo, hi;
    __asm__ ("smull %0, %1, %2, %3" : "=&r"(lo), "=&r"(hi) : "r"(b), "r"(a));
    return hi;
}
#endif

static inline av_const int64_t MUL64(int a, int b)
{
    union { uint64_t x; unsigned hl[2]; } x;
    __asm__ ("smull %0, %1, %2, %3"
             : "=r"(x.hl[0]), "=r"(x.hl[1]) : "r"(a), "r"(b));
    return x.x;
}
#define MUL64 MUL64

static inline av_const int64_t MAC64(int64_t d, int a, int b)
{
    union { uint64_t x; unsigned hl[2]; } x = { d };
    __asm__ ("smlal %0, %1, %2, %3"
             : "+r"(x.hl[0]), "+r"(x.hl[1]) : "r"(a), "r"(b));
    return x.x;
}
#define MAC64(d, a, b) ((d) = MAC64(d, a, b))
#define MLS64(d, a, b) MAC64(d, -(a), b)

#if HAVE_ARMV5TE

/* signed 16x16 -> 32 multiply add accumulate */
#   define MAC16(rt, ra, rb)                                            \
    __asm__ ("smlabb %0, %1, %2, %0" : "+r"(rt) : "r"(ra), "r"(rb));

/* signed 16x16 -> 32 multiply */
#   define MUL16 MUL16
static inline av_const MUL16(int ra, int rb)
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
    __asm__ volatile (
        "mov   %0, %2  \n\t"
        "cmp   %1, %2  \n\t"
        "movgt %0, %1  \n\t"
        "movgt %1, %2  \n\t"
        "cmp   %1, %3  \n\t"
        "movle %1, %3  \n\t"
        "cmp   %0, %1  \n\t"
        "movgt %0, %1  \n\t"
        : "=&r"(m), "+r"(a)
        : "r"(b), "r"(c));
    return m;
}

#endif /* AVCODEC_ARM_MATHOPS_H */
