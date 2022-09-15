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

#if defined (__riscv_zbb) && (__riscv_zbb > 0) && HAVE_INLINE_ASM

#define av_popcount av_popcount_rvb
static av_always_inline av_const int av_popcount_rvb(uint32_t x)
{
    int ret;

#if (__riscv_xlen >= 64)
    __asm__ ("cpopw %0, %1\n" : "=r" (ret) : "r" (x));
#else
    __asm__ ("cpop %0, %1\n" : "=r" (ret) : "r" (x));
#endif
    return ret;
}

#if (__riscv_xlen >= 64)
#define av_popcount64 av_popcount64_rvb
static av_always_inline av_const int av_popcount64_rvb(uint64_t x)
{
    int ret;

#if (__riscv_xlen >= 128)
    __asm__ ("cpopd %0, %1\n" : "=r" (ret) : "r" (x));
#else
    __asm__ ("cpop %0, %1\n" : "=r" (ret) : "r" (x));
#endif
    return ret;
}
#endif /* __riscv_xlen >= 64 */
#endif /* __riscv_zbb */

#endif /* AVUTIL_RISCV_INTMATH_H */
