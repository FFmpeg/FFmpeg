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

#ifndef AVUTIL_ARM_BSWAP_H
#define AVUTIL_ARM_BSWAP_H

#include <stdint.h>
#include "config.h"
#include "libavutil/common.h"

#ifdef __ARMCC_VERSION

#if HAVE_ARMV6
#define bswap_16 bswap_16
static av_always_inline av_const uint16_t bswap_16(uint16_t x)
{
    __asm { rev16 x, x }
    return x;
}

#define bswap_32 bswap_32
static av_always_inline av_const uint32_t bswap_32(uint32_t x)
{
    return __rev(x);
}
#endif /* HAVE_ARMV6 */

#else /* __ARMCC_VERSION */

#if HAVE_ARMV6
#define bswap_16 bswap_16
static av_always_inline av_const uint16_t bswap_16(uint16_t x)
{
    __asm__("rev16 %0, %0" : "+r"(x));
    return x;
}
#endif

#define bswap_32 bswap_32
static av_always_inline av_const uint32_t bswap_32(uint32_t x)
{
#if HAVE_ARMV6
    __asm__("rev %0, %0" : "+r"(x));
#else
    uint32_t t;
    __asm__ ("eor %1, %0, %0, ror #16 \n\t"
             "bic %1, %1, #0xFF0000   \n\t"
             "mov %0, %0, ror #8      \n\t"
             "eor %0, %0, %1, lsr #8  \n\t"
             : "+r"(x), "=&r"(t));
#endif /* HAVE_ARMV6 */
    return x;
}

#endif /* __ARMCC_VERSION */

#endif /* AVUTIL_ARM_BSWAP_H */
