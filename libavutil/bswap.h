/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * byte swapping routines
 */

#ifndef AVUTIL_BSWAP_H
#define AVUTIL_BSWAP_H

#include <stdint.h>
#include "config.h"
#include "attributes.h"

#if   ARCH_ARM
#   include "arm/bswap.h"
#elif ARCH_AVR32
#   include "avr32/bswap.h"
#elif ARCH_BFIN
#   include "bfin/bswap.h"
#elif ARCH_SH4
#   include "sh4/bswap.h"
#elif ARCH_X86
#   include "x86/bswap.h"
#endif

#define AV_BSWAP16C(x) (((x) << 8 & 0xff00)  | ((x) >> 8 & 0x00ff))
#define AV_BSWAP32C(x) (AV_BSWAP16C(x) << 16 | AV_BSWAP16C((x) >> 16))
#define AV_BSWAP64C(x) (AV_BSWAP32C(x) << 32 | AV_BSWAP32C((x) >> 32))

#define AV_BSWAPC(s, x) AV_BSWAP##s##C(x)

#ifndef bswap_16
static av_always_inline av_const uint16_t bswap_16(uint16_t x)
{
    x= (x>>8) | (x<<8);
    return x;
}
#endif

#ifndef bswap_32
static av_always_inline av_const uint32_t bswap_32(uint32_t x)
{
    x= ((x<<8)&0xFF00FF00) | ((x>>8)&0x00FF00FF);
    x= (x>>16) | (x<<16);
    return x;
}
#endif

#ifndef bswap_64
static inline uint64_t av_const bswap_64(uint64_t x)
{
#if 0
    x= ((x<< 8)&0xFF00FF00FF00FF00ULL) | ((x>> 8)&0x00FF00FF00FF00FFULL);
    x= ((x<<16)&0xFFFF0000FFFF0000ULL) | ((x>>16)&0x0000FFFF0000FFFFULL);
    return (x>>32) | (x<<32);
#else
    union {
        uint64_t ll;
        uint32_t l[2];
    } w, r;
    w.ll = x;
    r.l[0] = bswap_32 (w.l[1]);
    r.l[1] = bswap_32 (w.l[0]);
    return r.ll;
#endif
}
#endif

// be2me ... big-endian to machine-endian
// le2me ... little-endian to machine-endian

#if HAVE_BIGENDIAN
#define be2me_16(x) (x)
#define be2me_32(x) (x)
#define be2me_64(x) (x)
#define le2me_16(x) bswap_16(x)
#define le2me_32(x) bswap_32(x)
#define le2me_64(x) bswap_64(x)
#define AV_BE2MEC(s, x) (x)
#define AV_LE2MEC(s, x) AV_BSWAPC(s, x)
#else
#define be2me_16(x) bswap_16(x)
#define be2me_32(x) bswap_32(x)
#define be2me_64(x) bswap_64(x)
#define le2me_16(x) (x)
#define le2me_32(x) (x)
#define le2me_64(x) (x)
#define AV_BE2MEC(s, x) AV_BSWAPC(s, x)
#define AV_LE2MEC(s, x) (x)
#endif

#define AV_BE2ME16C(x) AV_BE2MEC(16, x)
#define AV_BE2ME32C(x) AV_BE2MEC(32, x)
#define AV_BE2ME64C(x) AV_BE2MEC(64, x)
#define AV_LE2ME16C(x) AV_LE2MEC(16, x)
#define AV_LE2ME32C(x) AV_LE2MEC(32, x)
#define AV_LE2ME64C(x) AV_LE2MEC(64, x)

#endif /* AVUTIL_BSWAP_H */
