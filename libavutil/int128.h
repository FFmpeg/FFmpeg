/*
 * 128-bit integers
 * Copyright (c) 2026 Niklas Haas
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
 * 128-bit integers, falling back to integer.h if necessary
 * @author Niklas Haas
 */

#ifndef AVUTIL_INT128_H
#define AVUTIL_INT128_H

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#include "config.h"
#include "integer.h"

#if HAVE_INT128
#define AV_INT128_NATIVE 1
typedef unsigned __int128 av_uint128;
typedef          __int128 av_int128;
#elif defined(BITINT_MAXWIDTH) && BITINT_MAXWIDTH >= 128
#define AV_INT128_NATIVE 1
typedef unsigned _BitInt(128) av_uint128;
typedef          _BitInt(128) av_int128;
#elif AV_INTEGER_SIZE >= 8
#define AV_INT128_NATIVE 0
typedef AVInteger av_uint128;
typedef AVInteger av_int128;
#else
#error "128-bit integer type not available"
#endif

#if AV_INT128_NATIVE
#  define av_add128(a, b)   ((a) + (b))
#  define av_sub128(a, b)   ((a) - (b))
#  define av_mul128(a, b)   ((a) * (b))
#  define av_div128(a, b)   ((a) / (b))
#  define av_cmp128(a, b)   ((a) < (b) ? -1 : (a) > (b) ? 1 : 0)
#  define av_min128(a, b)   ((a) > (b) ? (b) : (a))
#  define av_max128(a, b)   ((a) > (b) ? (a) : (b))
#  define av_eq128(a, b)    ((a) == (b))
#  define av_mod128(a, b)   ((a) % (b))
#  define av_shr128(a, b)   ((a) >> (b))
#  define av_to128u(a)      ((av_uint128) (a))
#  define av_to128i(a)      ((av_int128) (a))
#  define av_from128i(a)    ((int64_t) (a))
#  define av_from128u(a)    ((uint64_t) (a))
#  define av_test128(a)     (!!(a))
#else
#  define av_add128(a, b)   av_add_i(a, b)
#  define av_sub128(a, b)   av_sub_i(a, b)
#  define av_mul128(a, b)   av_mul_i(a, b)
#  define av_div128(a, b)   av_div_i(a, b)
#  define av_cmp128(a, b)   av_cmp_i(a, b)
#  define av_min128(a, b)   (av_cmp_i(a, b) > 0 ? (b) : (a))
#  define av_max128(a, b)   (av_cmp_i(a, b) > 0 ? (a) : (b))
#  define av_eq128(a, b)    (av_cmp_i(a, b) == 0)
#  define av_mod128(a, b)   av_mod_i(NULL, a, b)
#  define av_shr128(a, b)   av_shr_i(a, b)
#  define av_to128i(a)      av_int2i(a)
#  define av_from128i(a)    av_i2int(a)
#  define av_from128u(a)    ((uint64_t) av_i2int(a))
#  define av_test128(a)     (!av_eq128(a, av_to128u(0)))

static av_always_inline av_uint128 av_to128u(uint64_t a)
{
    return (AVInteger) {{ a, a >> 16, a >> 32, a >> 48 }};
}
#endif

#endif /* AVUTIL_INT128_H */
