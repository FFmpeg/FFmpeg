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

/**
 * @file libavutil/x86/bswap.h
 * byte swapping routines
 */

#ifndef AVUTIL_X86_BSWAP_H
#define AVUTIL_X86_BSWAP_H

#include <stdint.h>
#include "config.h"
#include "libavutil/common.h"

#define bswap_16 bswap_16
static av_always_inline av_const uint16_t bswap_16(uint16_t x)
{
    __asm__("rorw $8, %0" : "+r"(x));
    return x;
}

#define bswap_32 bswap_32
static av_always_inline av_const uint32_t bswap_32(uint32_t x)
{
#if HAVE_BSWAP
    __asm__("bswap   %0" : "+r" (x));
#else
    __asm__("rorw    $8,  %w0 \n\t"
            "rorl    $16, %0  \n\t"
            "rorw    $8,  %w0"
            : "+r"(x));
#endif
    return x;
}

#if ARCH_X86_64
#define bswap_64 bswap_64
static inline uint64_t av_const bswap_64(uint64_t x)
{
    __asm__("bswap  %0": "=r" (x) : "0" (x));
    return x;
}
#endif

#endif /* AVUTIL_X86_BSWAP_H */
