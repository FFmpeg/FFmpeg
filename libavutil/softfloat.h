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
 *
 */

#define MIN_EXP -126
#define MAX_EXP  126
#define ONE_BITS 29

typedef struct SoftFloat{
    int32_t  exp;
    int32_t mant;
}SoftFloat;

static SoftFloat av_normalize_sf(SoftFloat a){
    if(a.mant){
#if 1
        while((a.mant + 0x20000000U)<0x40000000U){
            a.mant += a.mant;
            a.exp  -= 1;
        }
#else
        int s=ONE_BITS + 1 - av_log2(a.mant ^ (a.mant<<1));
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

static inline SoftFloat av_normalize1_sf(SoftFloat a){
#if 1
    if(a.mant + 0x40000000 < 0){
        a.exp++;
        a.mant>>=1;
    }
    return a;
#elif 1
    int t= a.mant + 0x40000000 < 0;
    return (SoftFloat){a.exp+t, a.mant>>t};
#else
    int t= (a.mant + 0x40000000U)>>31;
    return (SoftFloat){a.exp+t, a.mant>>t};
#endif
}

/**
 *
 * @return will not be more denormalized then a+b, so if either input is
 *         normalized then the output wont be worse then the other input
 *         if both are normalized then the output will be normalized
 */
static inline SoftFloat av_mul_sf(SoftFloat a, SoftFloat b){
    a.exp += b.exp;
    a.mant = (a.mant * (int64_t)b.mant) >> ONE_BITS;
    return av_normalize1_sf(a);
}

/**
 *
 * b has to be normalized and not zero
 * @return will not be more denormalized then a
 */
static SoftFloat av_div_sf(SoftFloat a, SoftFloat b){
    a.exp -= b.exp+1;
    a.mant = ((int64_t)a.mant<<(ONE_BITS+1)) / b.mant;
    return av_normalize1_sf(a);
}

static inline int av_cmp_sf(SoftFloat a, SoftFloat b){
    int t= a.exp - b.exp;
    if(t<0) return (a.mant >> (-t)) -  b.mant      ;
    else    return  a.mant          - (b.mant >> t);
}

static inline SoftFloat av_add_sf(SoftFloat a, SoftFloat b){
    int t= a.exp - b.exp;
    if(t<0) return av_normalize1_sf((SoftFloat){b.exp, b.mant + (a.mant >> (-t))});
    else    return av_normalize1_sf((SoftFloat){a.exp, a.mant + (b.mant >>   t )});
}

static inline SoftFloat av_sub_sf(SoftFloat a, SoftFloat b){
    return av_add_sf(a, (SoftFloat){b.exp, -b.mant});
}

//FIXME sqrt, log, exp, pow, sin, cos

static inline SoftFloat av_int2sf(int v, int frac_bits){
    return av_normalize_sf((SoftFloat){ONE_BITS-frac_bits, v});
}

/**
 *
 * rounding is to -inf
 */
static inline int av_sf2int(SoftFloat v, int frac_bits){
    v.exp += frac_bits - ONE_BITS;
    if(v.exp >= 0) return v.mant <<  v.exp ;
    else           return v.mant >>(-v.exp);
}
