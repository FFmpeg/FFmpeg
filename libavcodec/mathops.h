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
#include "config.h"

#if   ARCH_ARM
#   include "arm/mathops.h"
#elif ARCH_AVR32
#   include "avr32/mathops.h"
#elif ARCH_BFIN
#   include "bfin/mathops.h"
#elif ARCH_MIPS
#   include "mips/mathops.h"
#elif ARCH_PPC
#   include "ppc/mathops.h"
#elif ARCH_X86
#   include "x86/mathops.h"
#endif

/* generic implementation */

#ifndef MUL64
#   define MUL64(a,b) ((int64_t)(a) * (int64_t)(b))
#endif

#ifndef MULL
#   define MULL(a,b,s) (MUL64(a, b) >> (s))
#endif

#ifndef MULH
static av_always_inline int MULH(int a, int b){
    return MUL64(a, b) >> 32;
}
#endif

#ifndef UMULH
static av_always_inline unsigned UMULH(unsigned a, unsigned b){
    return ((uint64_t)(a) * (uint64_t)(b))>>32;
}
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
    unsigned shift = 8 * sizeof(int) - bits;
    union { unsigned u; int s; } v = { (unsigned) val << shift };
    return v.s >> shift;
}
#endif

#ifndef zero_extend
static inline av_const unsigned zero_extend(unsigned val, unsigned bits)
{
    return (val << ((8 * sizeof(int)) - bits)) >> ((8 * sizeof(int)) - bits);
}
#endif

#ifndef COPY3_IF_LT
#define COPY3_IF_LT(x, y, a, b, c, d)\
if ((y) < (x)) {\
    (x) = (y);\
    (a) = (b);\
    (c) = (d);\
}
#endif

#ifndef NEG_SSR32
#   define NEG_SSR32(a,s) ((( int32_t)(a))>>(32-(s)))
#endif

#ifndef NEG_USR32
#   define NEG_USR32(a,s) (((uint32_t)(a))>>(32-(s)))
#endif

#if HAVE_BIGENDIAN
# ifndef PACK_2U8
#   define PACK_2U8(a,b)     (((a) <<  8) | (b))
# endif
# ifndef PACK_4U8
#   define PACK_4U8(a,b,c,d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
# endif
# ifndef PACK_2U16
#   define PACK_2U16(a,b)    (((a) << 16) | (b))
# endif
#else
# ifndef PACK_2U8
#   define PACK_2U8(a,b)     (((b) <<  8) | (a))
# endif
# ifndef PACK_4U2
#   define PACK_4U8(a,b,c,d) (((d) << 24) | ((c) << 16) | ((b) << 8) | (a))
# endif
# ifndef PACK_2U16
#   define PACK_2U16(a,b)    (((b) << 16) | (a))
# endif
#endif

#ifndef PACK_2S8
#   define PACK_2S8(a,b)     PACK_2U8((a)&255, (b)&255)
#endif
#ifndef PACK_4S8
#   define PACK_4S8(a,b,c,d) PACK_4U8((a)&255, (b)&255, (c)&255, (d)&255)
#endif
#ifndef PACK_2S16
#   define PACK_2S16(a,b)    PACK_2U16((a)&0xffff, (b)&0xffff)
#endif

#endif /* AVCODEC_MATHOPS_H */
