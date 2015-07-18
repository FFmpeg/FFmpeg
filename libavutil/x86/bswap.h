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
 * @file
 * byte swapping routines
 */

#ifndef AVUTIL_X86_BSWAP_H
#define AVUTIL_X86_BSWAP_H

#include <stdint.h>
#include "config.h"
#include "libavutil/attributes.h"

#if HAVE_INLINE_ASM

#if !AV_GCC_VERSION_AT_LEAST(4,1)
#define av_bswap16 av_bswap16
static av_always_inline av_const unsigned av_bswap16(unsigned x)
{
    __asm__("rorw $8, %w0" : "+r"(x));
    return x;
}
#endif /* !AV_GCC_VERSION_AT_LEAST(4,1) */

#if !AV_GCC_VERSION_AT_LEAST(4,5) || defined(__INTEL_COMPILER)
#define av_bswap32 av_bswap32
static av_always_inline av_const uint32_t av_bswap32(uint32_t x)
{
    __asm__("bswap   %0" : "+r" (x));
    return x;
}

#if ARCH_X86_64
#define av_bswap64 av_bswap64
static inline uint64_t av_const av_bswap64(uint64_t x)
{
    __asm__("bswap  %0": "=r" (x) : "0" (x));
    return x;
}
#endif
#endif /* !AV_GCC_VERSION_AT_LEAST(4,5) */

#endif /* HAVE_INLINE_ASM */
#endif /* AVUTIL_X86_BSWAP_H */
