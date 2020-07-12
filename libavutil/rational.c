/*
 * rational numbers
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
 * rational numbers
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "avassert.h"
#include <limits.h>

#include "common.h"
#include "mathematics.h"
#include "rational.h"

int av_reduce(int *dst_num, int *dst_den,
              int64_t num, int64_t den, int64_t max)
{
    AVRational a0 = { 0, 1 }, a1 = { 1, 0 };
    int sign = (num < 0) ^ (den < 0);
    int64_t gcd = av_gcd(FFABS(num), FFABS(den));

    if (gcd) {
        num = FFABS(num) / gcd;
        den = FFABS(den) / gcd;
    }
    if (num <= max && den <= max) {
        a1 = (AVRational) { num, den };
        den = 0;
    }

    while (den) {
        uint64_t x        = num / den;
        int64_t next_den  = num - den * x;
        int64_t a2n       = x * a1.num + a0.num;
        int64_t a2d       = x * a1.den + a0.den;

        if (a2n > max || a2d > max) {
            if (a1.num) x =          (max - a0.num) / a1.num;
            if (a1.den) x = FFMIN(x, (max - a0.den) / a1.den);

            if (den * (2 * x * a1.den + a0.den) > num * a1.den)
                a1 = (AVRational) { x * a1.num + a0.num, x * a1.den + a0.den };
            break;
        }

        a0  = a1;
        a1  = (AVRational) { a2n, a2d };
        num = den;
        den = next_den;
    }
    av_assert2(av_gcd(a1.num, a1.den) <= 1U);
    av_assert2(a1.num <= max && a1.den <= max);

    *dst_num = sign ? -a1.num : a1.num;
    *dst_den = a1.den;

    return den == 0;
}

AVRational av_mul_q(AVRational b, AVRational c)
{
    av_reduce(&b.num, &b.den,
               b.num * (int64_t) c.num,
               b.den * (int64_t) c.den, INT_MAX);
    return b;
}

AVRational av_div_q(AVRational b, AVRational c)
{
    return av_mul_q(b, (AVRational) { c.den, c.num });
}

AVRational av_add_q(AVRational b, AVRational c) {
    av_reduce(&b.num, &b.den,
               b.num * (int64_t) c.den +
               c.num * (int64_t) b.den,
               b.den * (int64_t) c.den, INT_MAX);
    return b;
}

AVRational av_sub_q(AVRational b, AVRational c)
{
    return av_add_q(b, (AVRational) { -c.num, c.den });
}

AVRational av_d2q(double d, int max)
{
    AVRational a;
    int exponent;
    int64_t den;
    if (isnan(d))
        return (AVRational) { 0,0 };
    if (fabs(d) > INT_MAX + 3LL)
        return (AVRational) { d < 0 ? -1 : 1, 0 };
    frexp(d, &exponent);
    exponent = FFMAX(exponent-1, 0);
    den = 1LL << (61 - exponent);
    // (int64_t)rint() and llrint() do not work with gcc on ia64 and sparc64,
    // see Ticket2713 for affected gcc/glibc versions
    av_reduce(&a.num, &a.den, floor(d * den + 0.5), den, max);
    if ((!a.num || !a.den) && d && max>0 && max<INT_MAX)
        av_reduce(&a.num, &a.den, floor(d * den + 0.5), den, INT_MAX);

    return a;
}

int av_nearer_q(AVRational q, AVRational q1, AVRational q2)
{
    /* n/d is q, a/b is the median between q1 and q2 */
    int64_t a = q1.num * (int64_t)q2.den + q2.num * (int64_t)q1.den;
    int64_t b = 2 * (int64_t)q1.den * q2.den;

    /* rnd_up(a*d/b) > n => a*d/b > n */
    int64_t x_up = av_rescale_rnd(a, q.den, b, AV_ROUND_UP);

    /* rnd_down(a*d/b) < n => a*d/b < n */
    int64_t x_down = av_rescale_rnd(a, q.den, b, AV_ROUND_DOWN);

    return ((x_up > q.num) - (x_down < q.num)) * av_cmp_q(q2, q1);
}

int av_find_nearest_q_idx(AVRational q, const AVRational* q_list)
{
    int i, nearest_q_idx = 0;
    for (i = 0; q_list[i].den; i++)
        if (av_nearer_q(q, q_list[i], q_list[nearest_q_idx]) > 0)
            nearest_q_idx = i;

    return nearest_q_idx;
}

uint32_t av_q2intfloat(AVRational q) {
    int64_t n;
    int shift;
    int sign = 0;

    if (q.den < 0) {
        q.den *= -1;
        q.num *= -1;
    }
    if (q.num < 0) {
        q.num *= -1;
        sign = 1;
    }

    if (!q.num && !q.den) return 0xFFC00000;
    if (!q.num) return 0;
    if (!q.den) return 0x7F800000 | (q.num & 0x80000000);

    shift = 23 + av_log2(q.den) - av_log2(q.num);
    if (shift >= 0) n = av_rescale(q.num, 1LL<<shift, q.den);
    else            n = av_rescale(q.num, 1, ((int64_t)q.den) << -shift);

    shift -= n >= (1<<24);
    shift += n <  (1<<23);

    if (shift >= 0) n = av_rescale(q.num, 1LL<<shift, q.den);
    else            n = av_rescale(q.num, 1, ((int64_t)q.den) << -shift);

    av_assert1(n <  (1<<24));
    av_assert1(n >= (1<<23));

    return sign<<31 | (150-shift)<<23 | (n - (1<<23));
}

AVRational av_gcd_q(AVRational a, AVRational b, int max_den, AVRational def)
{
    int64_t gcd, lcm;

    gcd = av_gcd(a.den, b.den);
    lcm = (a.den / gcd) * b.den;
    return lcm < max_den ? av_make_q(av_gcd(a.num, b.num), lcm) : def;
}
