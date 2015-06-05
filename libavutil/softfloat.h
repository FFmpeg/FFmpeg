/*
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVUTIL_SOFTFLOAT_H
#define AVUTIL_SOFTFLOAT_H

#include <stdint.h>
#include "common.h"

#include "avassert.h"
#include "softfloat_tables.h"

#define MIN_EXP -126
#define MAX_EXP  126
#define ONE_BITS 29

typedef struct SoftFloat{
    int32_t mant;
    int32_t  exp;
}SoftFloat;

static av_const SoftFloat av_normalize_sf(SoftFloat a){
    if(a.mant){
#if 1
        while((a.mant + 0x1FFFFFFFU)<0x3FFFFFFFU){
            a.mant += a.mant;
            a.exp  -= 1;
        }
#else
        int s=ONE_BITS - av_log2(FFABS(a.mant));
        a.exp   -= s;
        a.mant <<= s;
#endif
        if(a.exp < MIN_EXP){
            a.exp = MIN_EXP;
            a.mant= 0;
        }
    }else{
        a.exp= MIN_EXP;
    }
    return a;
}

static inline av_const SoftFloat av_normalize1_sf(SoftFloat a){
#if 1
    if((int32_t)(a.mant + 0x40000000U) <= 0){
        a.exp++;
        a.mant>>=1;
    }
    av_assert2(a.mant < 0x40000000 && a.mant > -0x40000000);
    return a;
#elif 1
    int t= a.mant + 0x40000000 < 0;
    return (SoftFloat){ a.mant>>t, a.exp+t};
#else
    int t= (a.mant + 0x3FFFFFFFU)>>31;
    return (SoftFloat){a.mant>>t, a.exp+t};
#endif
}

/**
 * @return Will not be more denormalized than a+b. So if either input is
 *         normalized, then the output will not be worse then the other input.
 *         If both are normalized, then the output will be normalized.
 */
static inline av_const SoftFloat av_mul_sf(SoftFloat a, SoftFloat b){
    a.exp += b.exp;
    av_assert2((int32_t)((a.mant * (int64_t)b.mant) >> ONE_BITS) == (a.mant * (int64_t)b.mant) >> ONE_BITS);
    a.mant = (a.mant * (int64_t)b.mant) >> ONE_BITS;
    return av_normalize1_sf((SoftFloat){a.mant, a.exp - 1});
}

/**
 * b has to be normalized and not zero.
 * @return Will not be more denormalized than a.
 */
static av_const SoftFloat av_div_sf(SoftFloat a, SoftFloat b){
    a.exp -= b.exp;
    a.mant = ((int64_t)a.mant<<(ONE_BITS+1)) / b.mant;
    return av_normalize1_sf(a);
}

static inline av_const int av_cmp_sf(SoftFloat a, SoftFloat b){
    int t= a.exp - b.exp;
    if(t<0) return (a.mant >> (-t)) -  b.mant      ;
    else    return  a.mant          - (b.mant >> t);
}

static inline av_const int av_gt_sf(SoftFloat a, SoftFloat b)
{
    int t= a.exp - b.exp;
    if(t<0) return (a.mant >> (-t)) >  b.mant      ;
    else    return  a.mant          > (b.mant >> t);
}

static inline av_const SoftFloat av_add_sf(SoftFloat a, SoftFloat b){
    int t= a.exp - b.exp;
    if      (t <-31) return b;
    else if (t <  0) return av_normalize_sf(av_normalize1_sf((SoftFloat){ b.mant + (a.mant >> (-t)), b.exp}));
    else if (t < 32) return av_normalize_sf(av_normalize1_sf((SoftFloat){ a.mant + (b.mant >>   t ), a.exp}));
    else             return a;
}

static inline av_const SoftFloat av_sub_sf(SoftFloat a, SoftFloat b){
    return av_add_sf(a, (SoftFloat){ -b.mant, b.exp});
}

//FIXME log, exp, pow

/**
 * Converts a mantisse and exponent to a SoftFloat
 * @returns a SoftFloat with value v * 2^frac_bits
 */
static inline av_const SoftFloat av_int2sf(int v, int frac_bits){
    return av_normalize_sf((SoftFloat){v, ONE_BITS + 1 - frac_bits});
}

/**
 * Rounding is to -inf.
 */
static inline av_const int av_sf2int(SoftFloat v, int frac_bits){
    v.exp += frac_bits - (ONE_BITS + 1);
    if(v.exp >= 0) return v.mant <<  v.exp ;
    else           return v.mant >>(-v.exp);
}

/**
 * Rounding-to-nearest used.
 */
static av_always_inline SoftFloat av_sqrt_sf(SoftFloat val)
{
    int tabIndex, rem;

    if (val.mant == 0)
        val.exp = 0;
    else
    {
        tabIndex = (val.mant - 0x20000000) >> 20;

        rem = val.mant & 0xFFFFF;
        val.mant  = (int)(((int64_t)av_sqrttbl_sf[tabIndex] * (0x100000 - rem) +
                           (int64_t)av_sqrttbl_sf[tabIndex + 1] * rem +
                           0x80000) >> 20);
        val.mant = (int)(((int64_t)av_sqr_exp_multbl_sf[val.exp & 1] * val.mant +
                          0x10000000) >> 29);

        if (val.mant < 0x40000000)
            val.exp -= 2;
        else
            val.mant >>= 1;

        val.exp = (val.exp >> 1) + 1;
    }

    return val;
}

/**
 * Rounding-to-nearest used.
 */
void av_sincos_sf(int a, int *s, int *c);

#endif /* AVUTIL_SOFTFLOAT_H */
