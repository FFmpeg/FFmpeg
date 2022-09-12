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

#ifndef AVUTIL_RISCV_BSWAP_H
#define AVUTIL_RISCV_BSWAP_H

#include <stdint.h>
#include "config.h"
#include "libavutil/attributes.h"

#if defined (__riscv_zbb) && (__riscv_zbb > 0) && HAVE_INLINE_ASM

static av_always_inline av_const uintptr_t av_bswap_xlen(uintptr_t x)
{
    uintptr_t y;

    __asm__("rev8 %0, %1" : "=r" (y) : "r" (x));
    return y;
}

#define av_bswap16 av_bswap16

static av_always_inline av_const uint_fast16_t av_bswap16(uint_fast16_t x)
{
    return av_bswap_xlen(x) >> (__riscv_xlen - 16);
}

#if (__riscv_xlen == 32)
#define av_bswap32 av_bswap_xlen
#define av_bswap64 av_bswap64

static av_always_inline av_const uint64_t av_bswap64(uint64_t x)
{
    return (((uint64_t)av_bswap32(x)) << 32) | av_bswap32(x >> 32);
}

#else
#define av_bswap32 av_bswap32

static av_always_inline av_const uint_fast32_t av_bswap32(uint_fast32_t x)
{
    return av_bswap_xlen(x) >> (__riscv_xlen - 32);
}

#if (__riscv_xlen == 64)
#define av_bswap64 av_bswap_xlen

#else
#define av_bswap64 av_bswap64

static av_always_inline av_const uint_fast64_t av_bswap64(uint_fast64_t x)
{
    return av_bswap_xlen(x) >> (__riscv_xlen - 64);
}

#endif /* __riscv_xlen > 64 */
#endif /* __riscv_xlen > 32 */
#endif /* __riscv_zbb */
#endif /* AVUTIL_RISCV_BSWAP_H */
