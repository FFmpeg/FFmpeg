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
 * @ingroup lavu_math_rational
 * 64-bit extension of AVRational.
 * @author Niklas Haas
 */

#ifndef SWSCALE_RATIONAL64_H
#define SWSCALE_RATIONAL64_H

#include <stdint.h>
#include <limits.h>

#include "libavutil/attributes.h"

/**
 * @defgroup AVRational64
 * 64-bit extension of AVRational
 *
 * Offers a 64-bit extended version of AVRational. This is less efficient (and
 * may revolve around emulated 128-bit multiplications internally), but allows
 * to represent a much larger range of rational numbers without overflow.
 *
 * @{
 */

/**
 * 64-bit Rational number (pair of numerator and denominator).
 */
typedef struct AVRational64 {
    int64_t num; ///< Numerator
    int64_t den; ///< Denominator
} AVRational64;

/**
 * Create an AVRational64.
 *
 * Useful for compilers that do not support compound literals.
 *
 * @note The return value is not reduced.
 */
static inline AVRational64 av_make_q64(int64_t num, int64_t den)
{
    AVRational64 r = { num, den };
    return r;
}

/**
 * Compare two 64-bit rationals.
 *
 * @param a First rational
 * @param b Second rational
 *
 * @return One of the following values:
 *         - 0 if `a == b`
 *         - 1 if `a > b`
 *         - -1 if `a < b`
 *         - `INT_MIN` if one of the values is of the form `0 / 0`
 */
int av_cmp_q64(AVRational64 a, AVRational64 b);

/**
 * Convert an AVRational64 to a `double`.
 * @param a AVRational64 to convert
 * @return `a` in floating-point form
 * @see av_d2q()
 */
static inline double av_q2d_64(AVRational64 a){
    return a.num / (double) a.den;
}

/**
 * Multiply two 64-bit rationals.
 * @param b First multiplicant
 * @param c Second multiplicant
 * @return b*c
 */
AVRational64 av_mul_q64(AVRational64 b, AVRational64 c) av_const;

/**
 * Divide one 64-bit rational by another.
 * @param b Dividend
 * @param c Divisor
 * @return b/c
 */
AVRational64 av_div_q64(AVRational64 b, AVRational64 c) av_const;

/**
 * Add two 64-bit rationals.
 * @param b First addend
 * @param c Second addend
 * @return b+c
 */
AVRational64 av_add_q64(AVRational64 b, AVRational64 c) av_const;

/**
 * Subtract one 64-bit rational from another.
 * @param b Minuend
 * @param c Subtrahend
 * @return b-c
 */
AVRational64 av_sub_q64(AVRational64 b, AVRational64 c) av_const;

/**
 * Invert a 64-bit rational.
 * @param q value
 * @return 1 / q
 */
static av_always_inline AVRational64 av_inv_q64(AVRational64 q)
{
    AVRational64 r = { q.den, q.num };
    return r;
}

/**
 * Return the best rational so that a and b are multiple of it.
 * If the resulting denominator is larger than max_den, return def.
 */
AVRational64 av_gcd_q64(AVRational64 a, AVRational64 b, int max_den, AVRational64 def);

/**
 * @}
 */

#endif /* SWSCALE_RATIONAL64_H */
