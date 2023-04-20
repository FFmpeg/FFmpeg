/*
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

#ifndef AVUTIL_RISCV_INTMATH_H
#define AVUTIL_RISCV_INTMATH_H

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"

/*
 * The compiler is forced to sign-extend the result anyhow, so it is faster to
 * compute it explicitly and use it.
 */
#define av_clip_int8 av_clip_int8_rvi
static av_always_inline av_const int8_t av_clip_int8_rvi(int a)
{
    union { uint8_t u; int8_t s; } u = { .u = a };

    if (a != u.s)
        a = ((a >> 31) ^ 0x7F);
    return a;
}

#define av_clip_int16 av_clip_int16_rvi
static av_always_inline av_const int16_t av_clip_int16_rvi(int a)
{
    union { uint16_t u; int16_t s; } u = { .u = a };

    if (a != u.s)
        a = ((a >> 31) ^ 0x7FFF);
    return a;
}

#define av_clipl_int32 av_clipl_int32_rvi
static av_always_inline av_const int32_t av_clipl_int32_rvi(int64_t a)
{
    union { uint32_t u; int32_t s; } u = { .u = a };

    if (a != u.s)
        a = ((a >> 63) ^ 0x7FFFFFFF);
    return a;
}

#define av_clip_intp2 av_clip_intp2_rvi
static av_always_inline av_const int av_clip_intp2_rvi(int a, int p)
{
    const int shift = 31 - p;
    int b = ((int)(((unsigned)a) << shift)) >> shift;

    if (a != b)
        b = (a >> 31) ^ ((1 << p) - 1);
    return b;
}

#if defined (__GNUC__) || defined (__clang__)
#define av_popcount   __builtin_popcount
#if (__riscv_xlen >= 64)
#define av_popcount64 __builtin_popcountl
#else
#define av_popcount64 __builtin_popcountll
#endif
#endif

#endif /* AVUTIL_RISCV_INTMATH_H */
