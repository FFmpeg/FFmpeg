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
#include "mathematics.h"
#include "rational.h"

int av_reduce(int *dst_nom, int *dst_den, int64_t nom, int64_t den, int64_t max){
    AVRational a0={0,1}, a1={1,0};
    int sign= (nom<0) ^ (den<0);
    int64_t gcd= ff_gcd(ABS(nom), ABS(den));

    nom = ABS(nom)/gcd;
    den = ABS(den)/gcd;
    if(nom<=max && den<=max){
        a1= (AVRational){nom, den};
        den=0;
    }
    
    while(den){
        int64_t x       = nom / den;
        int64_t next_den= nom - den*x;
        int64_t a2n= x*a1.num + a0.num;
        int64_t a2d= x*a1.den + a0.den;

        if(a2n > max || a2d > max) break;

        a0= a1;
        a1= (AVRational){a2n, a2d};
        nom= den;
        den= next_den;
    }
    assert(ff_gcd(a1.num, a1.den) == 1);
    
    *dst_nom = sign ? -a1.num : a1.num;
    *dst_den = a1.den;
    
    return den==0;
}

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
