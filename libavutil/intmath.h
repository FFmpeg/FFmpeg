/*
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
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

#ifndef AVUTIL_INTMATH_H
#define AVUTIL_INTMATH_H

#include <stdint.h>

#include "config.h"
#include "attributes.h"

#if ARCH_ARM
#   include "arm/intmath.h"
#endif
#if ARCH_X86
#   include "x86/intmath.h"
#endif

#if HAVE_FAST_CLZ
#if defined( __INTEL_COMPILER )
#ifndef ff_log2
#   define ff_log2(x) (_bit_scan_reverse((x)|1))
#   ifndef ff_log2_16bit
#      define ff_log2_16bit av_log2
#   endif
#endif /* ff_log2 */
#elif AV_GCC_VERSION_AT_LEAST(3,4)
#ifndef ff_log2
#   define ff_log2(x) (31 - __builtin_clz((x)|1))
#   ifndef ff_log2_16bit
#      define ff_log2_16bit av_log2
#   endif
#endif /* ff_log2 */
#endif /* AV_GCC_VERSION_AT_LEAST(3,4) */
#endif

extern const uint8_t ff_log2_tab[256];

#ifndef ff_log2
#define ff_log2 ff_log2_c
#if !defined( _MSC_VER )
static av_always_inline av_const int ff_log2_c(unsigned int v)
{
    int n = 0;
    if (v & 0xffff0000) {
        v >>= 16;
        n += 16;
    }
    if (v & 0xff00) {
        v >>= 8;
        n += 8;
    }
    n += ff_log2_tab[v];

    return n;
}
#else
static av_always_inline av_const int ff_log2_c(unsigned int v)
{
    unsigned long n;
    _BitScanReverse(&n, v|1);
    return n;
}
#define ff_log2_16bit av_log2
#endif
#endif

#ifndef ff_log2_16bit
#define ff_log2_16bit ff_log2_16bit_c
static av_always_inline av_const int ff_log2_16bit_c(unsigned int v)
{
    int n = 0;
    if (v & 0xff00) {
        v >>= 8;
        n += 8;
    }
    n += ff_log2_tab[v];

    return n;
}
#endif

#define av_log2       ff_log2
#define av_log2_16bit ff_log2_16bit

/**
 * @addtogroup lavu_math
 * @{
 */

#if HAVE_FAST_CLZ
#if defined( __INTEL_COMPILER )
#ifndef ff_ctz
#define ff_ctz(v) _bit_scan_forward(v)
#endif
#elif AV_GCC_VERSION_AT_LEAST(3,4)
#ifndef ff_ctz
#define ff_ctz(v) __builtin_ctz(v)
#endif
#endif
#endif

#ifndef ff_ctz
#define ff_ctz ff_ctz_c
#if !defined( _MSC_VER )
static av_always_inline av_const int ff_ctz_c(int v)
{
    int c;

    if (v & 0x1)
        return 0;

    c = 1;
    if (!(v & 0xffff)) {
        v >>= 16;
        c += 16;
    }
    if (!(v & 0xff)) {
        v >>= 8;
        c += 8;
    }
    if (!(v & 0xf)) {
        v >>= 4;
        c += 4;
    }
    if (!(v & 0x3)) {
        v >>= 2;
        c += 2;
    }
    c -= v & 0x1;

    return c;
}
#else
static av_always_inline av_const int ff_ctz_c( int v )
{
    unsigned long c;
    _BitScanForward(&c, v);
    return c;
}
#endif
#endif

/**
 * Trailing zero bit count.
 *
 * @param v  input value. If v is 0, the result is undefined.
 * @return   the number of trailing 0-bits
 */
int av_ctz(int v);

/**
 * @}
 */
#endif /* AVUTIL_INTMATH_H */
