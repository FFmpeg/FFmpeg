/*
 * Rational numbers
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
 * @file rational.h
 * Rational numbers.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef FFMPEG_RATIONAL_H
#define FFMPEG_RATIONAL_H

#include <stdint.h>
#include "common.h"

/**
 * Rational number num/den.
 */
typedef struct AVRational{
    int num; ///< numerator
    int den; ///< denominator
} AVRational;

/**
 * Compare two rationals.
 * @param a first rational
 * @param b second rational
 * @return 0 if a==b, 1 if a>b and -1 if a<b.
 */
static inline int av_cmp_q(AVRational a, AVRational b){
    const int64_t tmp= a.num * (int64_t)b.den - b.num * (int64_t)a.den;

    if(tmp) return (tmp>>63)|1;
    else    return 0;
}

/**
 * Rational to double conversion.
 * @param a rational to convert
 * @return (double) a
 */
static inline double av_q2d(AVRational a){
    return a.num / (double) a.den;
}

/**
 * Reduce a fraction.
 * This is useful for framerate calculations.
 * @param dst_nom destination numerator
 * @param dst_den destination denominator
 * @param nom source numerator
 * @param den source denominator
 * @param max the maximum allowed for dst_nom & dst_den
 * @return 1 if exact, 0 otherwise
 */
int av_reduce(int *dst_nom, int *dst_den, int64_t nom, int64_t den, int64_t max);

/**
 * Multiplies two rationals.
 * @param b first rational.
 * @param c second rational.
 * @return b*c.
 */
AVRational av_mul_q(AVRational b, AVRational c) av_const;

/**
 * Divides one rational by another.
 * @param b first rational.
 * @param c second rational.
 * @return b/c.
 */
AVRational av_div_q(AVRational b, AVRational c) av_const;

/**
 * Adds two rationals.
 * @param b first rational.
 * @param c second rational.
 * @return b+c.
 */
AVRational av_add_q(AVRational b, AVRational c) av_const;

/**
 * Subtracts one rational from another.
 * @param b first rational.
 * @param c second rational.
 * @return b-c.
 */
AVRational av_sub_q(AVRational b, AVRational c) av_const;

/**
 * Converts a double precision floating point number to a rational.
 * @param d double to convert
 * @param max the maximum allowed numerator and denominator
 * @return (AVRational) d.
 */
AVRational av_d2q(double d, int max) av_const;

#endif /* FFMPEG_RATIONAL_H */
