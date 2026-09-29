/*
 * 64-bit rational numbers
 * Copyright (c) 2025 Niklas Haas
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * 64-bit rational numbers
 * @author Niklas Haas
 */

#include <limits.h>

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/int128.h"
#include "libavutil/intmath.h"
#include "rational64.h"

static av_always_inline uint64_t high64(av_int128 a)
{
    return av_from128u(av_shr128(a, 64));
}

/* unsigned version of av_gcd(); see libavutil/mathematics.c */
static av_always_inline uint64_t gcd_u64(uint64_t a, uint64_t b)
{
    if (!a)
        return b;
    if (!b)
        return a;

    const int k = ff_ctzll(a | b);
    a >>= ff_ctzll(a);
    do {
        b >>= ff_ctzll(b);
        if (a > b)
            FFSWAP(uint64_t, a, b);
        b -= a;
    } while (b);
    return a << k;
}

static av_int128 gcd128(av_int128 a, av_int128 b)
{
    while (av_test128(b)) {
        av_int128 tmp = b;
        b = av_mod128(a, b);
        a = tmp;
    }
    return a;
}

static AVRational64 reduce64(av_int128 num, av_int128 den)
{
    const av_int128 zero = av_to128i(0);
    const av_int128 max  = av_to128i(INT64_MAX);
    const int num_sign = av_cmp128(num, zero) < 0;
    const int den_sign = av_cmp128(den, zero) < 0;
    if (num_sign)
        num = av_sub128(zero, num);
    if (den_sign)
        den = av_sub128(zero, den);

    if (!high64(num) && !high64(den)) {
        /* Fast path: both values already fit into 64 bits */
        uint64_t n = av_from128u(num), d = av_from128u(den);
        const uint64_t gcd = gcd_u64(n, d);
        if (gcd) {
            n /= gcd;
            d /= gcd;
        }

        if (!((n | d) >> 63)) {
            AVRational64 res = { n, d };
            if (num_sign ^ den_sign)
                res.num = -res.num;
            return res;
        }

        /* Fall back to 128-bit arithmetic for values > INT64_MAX */
        num = av_to128u(n);
        den = av_to128u(d);
    }

    const av_int128 gcd = gcd128(num, den);
    if (av_test128(gcd)) {
        num = av_div128(num, gcd);
        den = av_div128(den, gcd);
    }

    av_uint128 a0n = av_to128u(0), a0d = av_to128u(1);
    av_uint128 a1n = av_to128u(1), a1d = av_to128u(0);
    if (av_cmp128(num, max) <= 0 && av_cmp128(den, max) <= 0) {
        a1n = num;
        a1d = den;
        goto done;
    }

    while (av_test128(den)) {
        av_int128 x        = av_div128(num, den);
        av_int128 next_den = av_sub128(num, av_mul128(den, x));
        av_uint128 a2n     = av_add128(av_mul128(x, a1n), a0n);
        av_uint128 a2d     = av_add128(av_mul128(x, a1d), a0d);

        if (av_cmp128(a2n, max) > 0 || av_cmp128(a2d, max) > 0) {
            if (av_test128(a1n))
                x = av_div128(av_sub128(max, a0n), a1n);
            if (av_test128(a1d)) {
                av_uint128 tmp = av_div128(av_sub128(max, a0d), a1d);
                x = av_min128(x, tmp);
            }

            av_uint128 x1d = av_mul128(x, a1d);
            av_uint128 a = av_mul128(den, av_add128(av_add128(x1d, x1d), a0d));
            av_uint128 b = av_mul128(num, a1d);
            if (av_cmp128(a, b) > 0) {
                a1n = av_add128(av_mul128(x, a1n), a0n);
                a1d = av_add128(x1d, a0d);
            }
            break;
        }

        a0n = a1n;
        a0d = a1d;
        a1n = a2n;
        a1d = a2d;
        num = den;
        den = next_den;
    }

done:;
    AVRational64 res = { av_from128i(a1n), av_from128i(a1d) };
    if (num_sign ^ den_sign)
        res.num = -res.num;
    return res;
}

static av_always_inline int fits31(int64_t x)
{
    return x >= -INT32_MAX && x <= INT32_MAX;
}

static av_always_inline int fits31_q(AVRational64 b, AVRational64 c)
{
    return fits31(b.num) && fits31(b.den) && fits31(c.num) && fits31(c.den);
}

/* Simplified version of reduce64() for small values */
static AVRational64 reduce64_fast(int64_t num, int64_t den)
{
    const int sign = (num < 0) ^ (den < 0);
    uint64_t n = num < 0 ? -(uint64_t) num : num;
    uint64_t d = den < 0 ? -(uint64_t) den : den;
    const uint64_t gcd = gcd_u64(n, d);
    if (gcd) {
        n /= gcd;
        d /= gcd;
    }

    AVRational64 res = { n, d };
    if (sign)
        res.num = -res.num;
    return res;
}


int ff_cmp_q64(AVRational64 a, AVRational64 b)
{
    int test;
    if (fits31_q(a, b)) {
        const int64_t p = a.num * b.den;
        const int64_t q = b.num * a.den;
        test = (p > q) - (p < q);
    } else {
        const av_int128 p = av_mul128(av_to128i(a.num), av_to128i(b.den));
        const av_int128 q = av_mul128(av_to128i(b.num), av_to128i(a.den));
        test = av_cmp128(p, q);
    }

    if (test)
        return (a.den < 0) ^ (b.den < 0) ? -test : test;
    else if (b.den && a.den)
        return 0;
    else if (a.num && b.num)
        return (a.num >> 63) - (b.num >> 63);
    else
        return INT_MIN;
}

AVRational64 ff_mul_q64(AVRational64 b, AVRational64 c)
{
    if (fits31_q(b, c))
        return reduce64_fast(b.num * c.num, b.den * c.den);

    return reduce64(av_mul128(av_to128i(b.num), av_to128i(c.num)),
                    av_mul128(av_to128i(b.den), av_to128i(c.den)));
}

AVRational64 ff_div_q64(AVRational64 b, AVRational64 c)
{
    return ff_mul_q64(b, ff_inv_q64(c));
}

AVRational64 ff_add_q64(AVRational64 b, AVRational64 c)
{
    if (fits31_q(b, c))
        return reduce64_fast(b.num * c.den + c.num * b.den, b.den * c.den);

    return reduce64(av_add128(av_mul128(av_to128i(b.num), av_to128i(c.den)),
                              av_mul128(av_to128i(c.num), av_to128i(b.den))),
                    av_mul128(av_to128i(b.den), av_to128i(c.den)));
}

AVRational64 ff_sub_q64(AVRational64 b, AVRational64 c)
{
    if (fits31_q(b, c))
        return reduce64_fast(b.num * c.den - c.num * b.den, b.den * c.den);

    return reduce64(av_sub128(av_mul128(av_to128i(b.num), av_to128i(c.den)),
                              av_mul128(av_to128i(c.num), av_to128i(b.den))),
                    av_mul128(av_to128i(b.den), av_to128i(c.den)));
}
