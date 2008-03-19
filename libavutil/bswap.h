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
 * @file bswap.h
 * byte swapping routines
 */

#ifndef FFMPEG_BSWAP_H
#define FFMPEG_BSWAP_H

#include <stdint.h>
#include "config.h"
#include "common.h"

#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#else

static av_always_inline av_const uint16_t bswap_16(uint16_t x)
{
#if defined(ARCH_X86)
    asm("rorw $8, %0" : "+r"(x));
#elif defined(ARCH_SH4)
    asm("swap.b %0,%0" : "=r"(x) : "0"(x));
#else
    x= (x>>8) | (x<<8);
#endif
    return x;
}

static av_always_inline av_const uint32_t bswap_32(uint32_t x)
{
#if defined(ARCH_X86)
#ifdef HAVE_BSWAP
    asm("bswap   %0" : "+r" (x));
#else
    asm("rorw    $8,  %w0 \n\t"
        "rorl    $16, %0  \n\t"
        "rorw    $8,  %w0"
        : "+r"(x));
#endif
#elif defined(ARCH_SH4)
    asm("swap.b %0,%0\n"
        "swap.w %0,%0\n"
        "swap.b %0,%0\n"
        : "=r"(x) : "0"(x));
#elif defined(ARCH_ARM)
    uint32_t t;
    asm ("eor %1, %0, %0, ror #16 \n\t"
         "bic %1, %1, #0xFF0000   \n\t"
         "mov %0, %0, ror #8      \n\t"
         "eor %0, %0, %1, lsr #8  \n\t"
         : "+r"(x), "+r"(t));
#elif defined(ARCH_BFIN)
    unsigned tmp;
    asm("%1 = %0 >> 8 (V);      \n\t"
        "%0 = %0 << 8 (V);      \n\t"
        "%0 = %0 | %1;          \n\t"
        "%0 = PACK(%0.L, %0.H); \n\t"
        : "+d"(x), "=&d"(tmp));
#else
    x= ((x<<8)&0xFF00FF00) | ((x>>8)&0x00FF00FF);
    x= (x>>16) | (x<<16);
#endif
    return x;
}

static inline uint64_t av_const bswap_64(uint64_t x)
{
#if 0
    x= ((x<< 8)&0xFF00FF00FF00FF00ULL) | ((x>> 8)&0x00FF00FF00FF00FFULL);
    x= ((x<<16)&0xFFFF0000FFFF0000ULL) | ((x>>16)&0x0000FFFF0000FFFFULL);
    return (x>>32) | (x<<32);
#elif defined(ARCH_X86_64)
  asm("bswap  %0": "=r" (x) : "0" (x));
  return x;
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

#endif  /* !HAVE_BYTESWAP_H */

// be2me ... BigEndian to MachineEndian
// le2me ... LittleEndian to MachineEndian

#ifdef WORDS_BIGENDIAN
#define be2me_16(x) (x)
#define be2me_32(x) (x)
#define be2me_64(x) (x)
#define le2me_16(x) bswap_16(x)
#define le2me_32(x) bswap_32(x)
#define le2me_64(x) bswap_64(x)
#else
#define be2me_16(x) bswap_16(x)
#define be2me_32(x) bswap_32(x)
#define be2me_64(x) bswap_64(x)
#define le2me_16(x) (x)
#define le2me_32(x) (x)
#define le2me_64(x) (x)
#endif

#endif /* FFMPEG_BSWAP_H */
