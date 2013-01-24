/*
 * Electronic Arts TGQ/TQI/MAD IDCT algorithm
 * Copyright (c) 2007-2008 Peter Ross <pross@xvid.org>
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
 * Electronic Arts TGQ/TQI/MAD IDCT algorithm
 * @author Peter Ross <pross@xvid.org>
 */

#include "dsputil.h"
#include "eaidct.h"
#include "libavutil/common.h"

#define ASQRT 181 /* (1/sqrt(2))<<8 */
#define A4    669 /* cos(pi/8)*sqrt(2)<<9 */
#define A2    277 /* sin(pi/8)*sqrt(2)<<9 */
#define A5    196 /* sin(pi/8)<<9 */

#define IDCT_TRANSFORM(dest,s0,s1,s2,s3,s4,s5,s6,s7,d0,d1,d2,d3,d4,d5,d6,d7,munge,src) {\
    const int a1 = (src)[s1] + (src)[s7]; \
    const int a7 = (src)[s1] - (src)[s7]; \
    const int a5 = (src)[s5] + (src)[s3]; \
    const int a3 = (src)[s5] - (src)[s3]; \
    const int a2 = (src)[s2] + (src)[s6]; \
    const int a6 = (ASQRT*((src)[s2] - (src)[s6]))>>8; \
    const int a0 = (src)[s0] + (src)[s4]; \
    const int a4 = (src)[s0] - (src)[s4]; \
    const int b0 = (((A4-A5)*a7 - A5*a3)>>9) + a1+a5; \
    const int b1 = (((A4-A5)*a7 - A5*a3)>>9) + ((ASQRT*(a1-a5))>>8); \
    const int b2 = (((A2+A5)*a3 + A5*a7)>>9) + ((ASQRT*(a1-a5))>>8); \
    const int b3 =  ((A2+A5)*a3 + A5*a7)>>9; \
    (dest)[d0] = munge(a0+a2+a6+b0); \
    (dest)[d1] = munge(a4+a6   +b1); \
    (dest)[d2] = munge(a4-a6   +b2); \
    (dest)[d3] = munge(a0-a2-a6+b3); \
    (dest)[d4] = munge(a0-a2-a6-b3); \
    (dest)[d5] = munge(a4-a6   -b2); \
    (dest)[d6] = munge(a4+a6   -b1); \
    (dest)[d7] = munge(a0+a2+a6-b0); \
}
/* end IDCT_TRANSFORM macro */

#define MUNGE_NONE(x) (x)
#define IDCT_COL(dest,src) IDCT_TRANSFORM(dest,0,8,16,24,32,40,48,56,0,8,16,24,32,40,48,56,MUNGE_NONE,src)

#define MUNGE_8BIT(x) av_clip_uint8((x)>>4)
#define IDCT_ROW(dest,src) IDCT_TRANSFORM(dest,0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,MUNGE_8BIT,src)

static inline void ea_idct_col(int16_t *dest, const int16_t *src) {
    if ((src[8]|src[16]|src[24]|src[32]|src[40]|src[48]|src[56])==0) {
        dest[0]  =
        dest[8]  =
        dest[16] =
        dest[24] =
        dest[32] =
        dest[40] =
        dest[48] =
        dest[56] = src[0];
    }else
        IDCT_COL(dest, src);
}

void ff_ea_idct_put_c(uint8_t *dest, int linesize, int16_t *block) {
    int i;
    int16_t temp[64];
    block[0] += 4;
    for (i=0; i<8; i++)
        ea_idct_col(&temp[i], &block[i]);
    for (i=0; i<8; i++)
        IDCT_ROW( (&dest[i*linesize]), (&temp[8*i]) );
}
