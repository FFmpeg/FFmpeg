/*
 * Copyright (c) 2015 James Almer
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

#ifndef AVUTIL_X86_INTMATH_H
#define AVUTIL_X86_INTMATH_H

#include <stdint.h>
#include "config.h"

#if HAVE_FAST_CLZ
#if defined(__INTEL_COMPILER)
#   define ff_log2(x) (_bit_scan_reverse((x)|1))
#   define ff_log2_16bit av_log2

#   define ff_ctz(v) _bit_scan_forward(v)

#   define ff_ctzll ff_ctzll_x86
static av_always_inline av_const int ff_ctzll_x86(long long v)
{
#   if ARCH_X86_64
    uint64_t c;
    __asm__("bsfq %1,%0" : "=r" (c) : "r" (v));
    return c;
#   else
    return ((uint32_t)v == 0) ? _bit_scan_forward((uint32_t)(v >> 32)) + 32 : _bit_scan_forward((uint32_t)v);
#   endif
}
#elif defined(_MSC_VER)
#   define ff_log2 ff_log2_x86
static av_always_inline av_const int ff_log2_x86(unsigned int v) {
    unsigned long n;
    _BitScanReverse(&n, v | 1);
    return n;
}
#   define ff_log2_16bit av_log2

#   define ff_ctz ff_ctz_x86
static av_always_inline av_const int ff_ctz_x86(int v) {
    unsigned long c;
    _BitScanForward(&c, v);
    return c;
}

#   define ff_ctzll ff_ctzll_x86
static av_always_inline av_const int ff_ctzll_x86(long long v)
{
    unsigned long c;
#   if ARCH_X86_64
    _BitScanForward64(&c, v);
#   else
    if ((uint32_t)v == 0) {
        _BitScanForward(&c, (uint32_t)(v >> 32));
        c += 32;
    } else {
        _BitScanForward(&c, (uint32_t)v);
    }
#   endif
    return c;
}

#endif /* __INTEL_COMPILER */

#endif /* HAVE_FAST_CLZ */

#if defined(__GNUC__)

/* Our generic version of av_popcount is faster than GCC's built-in on
 * CPUs that don't support the popcnt instruction.
 */
#if defined(__POPCNT__)
    #define av_popcount   __builtin_popcount
#if ARCH_X86_64
    #define av_popcount64 __builtin_popcountll
#endif

#endif /* __POPCNT__ */

#if defined(__BMI2__)

#if AV_GCC_VERSION_AT_LEAST(5,1)
#define av_mod_uintp2 __builtin_ia32_bzhi_si
#elif HAVE_INLINE_ASM
/* GCC releases before 5.1.0 have a broken bzhi builtin, so for those we
 * implement it using inline assembly
 */
#define av_mod_uintp2 av_mod_uintp2_bmi2
static av_always_inline av_const unsigned av_mod_uintp2_bmi2(unsigned a, unsigned p)
{
    if (av_builtin_constant_p(p))
        return a & ((1 << p) - 1);
    else {
        unsigned x;
        __asm__ ("bzhi %2, %1, %0 \n\t" : "=r"(x) : "rm"(a), "r"(p));
        return x;
    }
}
#endif /* AV_GCC_VERSION_AT_LEAST */

#endif /* __BMI2__ */

#endif /* __GNUC__ */

#endif /* AVUTIL_X86_INTMATH_H */
