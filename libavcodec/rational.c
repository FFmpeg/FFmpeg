/*
 * Rational numbers
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
 
/**
 * @file rational.c
 * Rational numbers
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

//#include <math.h>
#include <limits.h>
 
#include "common.h"
#include "avcodec.h"
#include "rational.h"

/**
 * returns b*c.
 */
AVRational av_mul_q(AVRational b, AVRational c){
    av_reduce(&b.num, &b.den, b.num * (int64_t)c.num, b.den * (int64_t)c.den, INT_MAX);
    return b;
}

/**
 * returns b/c.
 */
AVRational av_div_q(AVRational b, AVRational c){
    av_reduce(&b.num, &b.den, b.num * (int64_t)c.den, b.den * (int64_t)c.num, INT_MAX);
    return b;
}

/**
 * returns b+c.
 */
AVRational av_add_q(AVRational b, AVRational c){
    av_reduce(&b.num, &b.den, b.num * (int64_t)c.den + c.num * (int64_t)b.den, b.den * (int64_t)c.den, INT_MAX);
    return b;
}

/**
 * returns b-c.
 */
AVRational av_sub_q(AVRational b, AVRational c){
    av_reduce(&b.num, &b.den, b.num * (int64_t)c.den - c.num * (int64_t)b.den, b.den * (int64_t)c.den, INT_MAX);
    return b;
}

/**
 * Converts a double precission floating point number to a AVRational.
 * @param max the maximum allowed numerator and denominator
 */
AVRational av_d2q(double d, int max){
    AVRational a;
    int exponent= FFMAX( (int)(log(ABS(d) + 1e-20)/log(2)), 0);
    int64_t den= 1LL << (61 - exponent);
    av_reduce(&a.num, &a.den, (int64_t)(d * den + 0.5), den, max);

    return a;
}
