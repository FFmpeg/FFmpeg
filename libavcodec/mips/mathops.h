/*
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
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

#ifndef AVCODEC_MIPS_MATHOPS_H
#define AVCODEC_MIPS_MATHOPS_H

#include <stdint.h>
#include "config.h"
#include "libavutil/common.h"

#if HAVE_INLINE_ASM

#if HAVE_LOONGSON

static inline av_const int64_t MAC64(int64_t d, int a, int b)
{
    int64_t m;
    __asm__ ("dmult.g %1, %2, %3 \n\t"
             "daddu   %0, %0, %1 \n\t"
             : "+r"(d), "=&r"(m) : "r"(a), "r"(b));
    return d;
}
#define MAC64(d, a, b) ((d) = MAC64(d, a, b))

static inline av_const int64_t MLS64(int64_t d, int a, int b)
{
    int64_t m;
    __asm__ ("dmult.g %1, %2, %3 \n\t"
             "dsubu   %0, %0, %1 \n\t"
             : "+r"(d), "=&r"(m) : "r"(a), "r"(b));
    return d;
}
#define MLS64(d, a, b) ((d) = MLS64(d, a, b))

#elif ARCH_MIPS64

static inline av_const int64_t MAC64(int64_t d, int a, int b)
{
    int64_t m;
    __asm__ ("dmult %2, %3     \n\t"
             "mflo  %1         \n\t"
             "daddu %0, %0, %1 \n\t"
             : "+r"(d), "=&r"(m) : "r"(a), "r"(b)
             : "hi", "lo");
    return d;
}
#define MAC64(d, a, b) ((d) = MAC64(d, a, b))

static inline av_const int64_t MLS64(int64_t d, int a, int b)
{
    int64_t m;
    __asm__ ("dmult %2, %3     \n\t"
             "mflo  %1         \n\t"
             "dsubu %0, %0, %1 \n\t"
             : "+r"(d), "=&r"(m) : "r"(a), "r"(b)
             : "hi", "lo");
    return d;
}
#define MLS64(d, a, b) ((d) = MLS64(d, a, b))

#endif

#endif /* HAVE_INLINE_ASM */

#endif /* AVCODEC_MIPS_MATHOPS_H */
