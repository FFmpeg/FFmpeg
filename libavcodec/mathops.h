/*
 * simple math operations
 * Copyright (c) 2001, 2002 Fabrice Bellard
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at> et al
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
#ifndef AVCODEC_MATHOPS_H
#define AVCODEC_MATHOPS_H

#include "libavutil/common.h"

#if   ARCH_X86

#include "x86/mathops.h"

#elif ARCH_ARM

#include "arm/mathops.h"

#elif ARCH_PPC

#include "ppc/mathops.h"

#elif ARCH_BFIN

#include "bfin/mathops.h"

#endif

/* generic implementation */

#ifndef MULL
#   define MULL(a,b,s) (((int64_t)(a) * (int64_t)(b)) >> (s))
#endif

#ifndef MULH
//gcc 3.4 creates an incredibly bloated mess out of this
//#    define MULH(a,b) (((int64_t)(a) * (int64_t)(b))>>32)

static av_always_inline int MULH(int a, int b){
    return ((int64_t)(a) * (int64_t)(b))>>32;
}
#endif

#ifndef MUL64
#   define MUL64(a,b) ((int64_t)(a) * (int64_t)(b))
#endif

#ifndef MAC64
#   define MAC64(d, a, b) ((d) += MUL64(a, b))
#endif

#ifndef MLS64
#   define MLS64(d, a, b) ((d) -= MUL64(a, b))
#endif

/* signed 16x16 -> 32 multiply add accumulate */
#ifndef MAC16
#   define MAC16(rt, ra, rb) rt += (ra) * (rb)
#endif

/* signed 16x16 -> 32 multiply */
#ifndef MUL16
#   define MUL16(ra, rb) ((ra) * (rb))
#endif

#ifndef MLS16
#   define MLS16(rt, ra, rb) ((rt) -= (ra) * (rb))
#endif

/* median of 3 */
#ifndef mid_pred
#define mid_pred mid_pred
static inline av_const int mid_pred(int a, int b, int c)
{
#if 0
    int t= (a-b)&((a-b)>>31);
    a-=t;
    b+=t;
    b-= (b-c)&((b-c)>>31);
    b+= (a-b)&((a-b)>>31);

    return b;
#else
    if(a>b){
        if(c>b){
            if(c>a) b=a;
            else    b=c;
        }
    }else{
        if(b>c){
            if(c>a) b=c;
            else    b=a;
        }
    }
    return b;
#endif
}
#endif

#ifndef sign_extend
static inline av_const int sign_extend(int val, unsigned bits)
{
    return (val << (INT_BIT - bits)) >> (INT_BIT - bits);
}
#endif

#endif /* AVCODEC_MATHOPS_H */

