/*
 * Copyright (C) 2004 the ffmpeg project
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
 * @file vp3dsp_sse2.c
 * SSE2-optimized functions cribbed from the original VP3 source code.
 */

#include "../dsputil.h"
#include "mmx.h"

static DECLARE_ALIGNED_16(const unsigned short, SSE2_dequant_const[]) =
{
    0,65535,65535,0,0,0,0,0,    // 0x0000 0000 0000 0000 0000 FFFF FFFF 0000
    0,0,0,0,65535,65535,0,0,    // 0x0000 0000 FFFF FFFF 0000 0000 0000 0000
    65535,65535,65535,0,0,0,0,0,// 0x0000 0000 0000 0000 0000 FFFF FFFF FFFF
    0,0,0,65535,0,0,0,0,        // 0x0000 0000 0000 0000 FFFF 0000 0000 0000
    0,0,0,65535,65535,0,0,0,    // 0x0000 0000 0000 FFFF FFFF 0000 0000 0000
    65535,0,0,0,0,65535,0,0,    // 0x0000 0000 FFFF 0000 0000 0000 0000 FFFF
    0,0,65535,65535, 0,0,0,0    // 0x0000 0000 0000 0000 FFFF FFFF 0000 0000
};

static DECLARE_ALIGNED_16(const unsigned int, eight_data[]) =
{
    0x00080008,
    0x00080008,
    0x00080008,
    0x00080008
};

static DECLARE_ALIGNED_16(const unsigned short, SSE2_idct_data[7 * 8]) =
{
    64277,64277,64277,64277,64277,64277,64277,64277,
    60547,60547,60547,60547,60547,60547,60547,60547,
    54491,54491,54491,54491,54491,54491,54491,54491,
    46341,46341,46341,46341,46341,46341,46341,46341,
    36410,36410,36410,36410,36410,36410,36410,36410,
    25080,25080,25080,25080,25080,25080,25080,25080,
    12785,12785,12785,12785,12785,12785,12785,12785
};


#define SSE2_Column_IDCT() {        \
    \
    movdqu_m2r(*I(3), xmm2);     /* xmm2 = i3 */ \
    movdqu_m2r(*C(3), xmm6);     /* xmm6 = c3 */ \
    \
    movdqu_r2r(xmm2, xmm4);      /* xmm4 = i3 */ \
    movdqu_m2r(*I(5), xmm7);     /* xmm7 = i5 */ \
    \
    pmulhw_r2r(xmm6, xmm4);      /* xmm4 = c3 * i3 - i3 */ \
    movdqu_m2r(*C(5), xmm1);     /* xmm1 = c5 */ \
    \
    pmulhw_r2r(xmm7, xmm6);      /* xmm6 = c3 * i5 - i5 */ \
    movdqu_r2r(xmm1, xmm5);      /* xmm5 = c5 */ \
    \
    pmulhw_r2r(xmm2, xmm1);      /* xmm1 = c5 * i3 - i3 */ \
    movdqu_m2r(*I(1), xmm3);     /* xmm3 = i1 */ \
    \
    pmulhw_r2r(xmm7, xmm5);      /* xmm5 = c5 * i5 - i5 */ \
    movdqu_m2r(*C(1), xmm0);     /* xmm0 = c1 */ \
    \
    /* all registers are in use */ \
    \
    paddw_r2r(xmm2, xmm4);       /* xmm4 = c3 * i3 */ \
    paddw_r2r(xmm7, xmm6);       /* xmm6 = c3 * i5 */ \
    \
    paddw_r2r(xmm1, xmm2);       /* xmm2 = c5 * i3 */ \
    movdqu_m2r(*I(7), xmm1);     /* xmm1 = i7 */ \
    \
    paddw_r2r(xmm5, xmm7);       /* xmm7 = c5 * i5 */ \
    movdqu_r2r(xmm0, xmm5);      /* xmm5 = c1 */ \
    \
    pmulhw_r2r(xmm3, xmm0);      /* xmm0 = c1 * i1 - i1 */ \
    paddsw_r2r(xmm7, xmm4);      /* xmm4 = c3 * i3 + c5 * i5 = C */ \
    \
    pmulhw_r2r(xmm1, xmm5);      /* xmm5 = c1 * i7 - i7 */ \
    movdqu_m2r(*C(7), xmm7);     /* xmm7 = c7 */ \
    \
    psubsw_r2r(xmm2, xmm6);      /* xmm6 = c3 * i5 - c5 * i3 = D */ \
    paddw_r2r(xmm3, xmm0);       /* xmm0 = c1 * i1 */ \
    \
    pmulhw_r2r(xmm7, xmm3);      /* xmm3 = c7 * i1 */ \
    movdqu_m2r(*I(2), xmm2);     /* xmm2 = i2 */ \
    \
    pmulhw_r2r(xmm1, xmm7);      /* xmm7 = c7 * i7 */ \
    paddw_r2r(xmm1, xmm5);       /* xmm5 = c1 * i7 */ \
    \
    movdqu_r2r(xmm2, xmm1);      /* xmm1 = i2 */ \
    pmulhw_m2r(*C(2), xmm2);     /* xmm2 = i2 * c2 -i2 */ \
    \
    psubsw_r2r(xmm5, xmm3);      /* xmm3 = c7 * i1 - c1 * i7 = B */ \
    movdqu_m2r(*I(6), xmm5);     /* xmm5 = i6 */ \
    \
    paddsw_r2r(xmm7, xmm0);      /* xmm0 = c1 * i1 + c7 * i7 = A */ \
    movdqu_r2r(xmm5, xmm7);      /* xmm7 = i6 */ \
    \
    psubsw_r2r(xmm4, xmm0);      /* xmm0 = A - C */ \
    pmulhw_m2r(*C(2), xmm5);     /* xmm5 = c2 * i6 - i6 */ \
    \
    paddw_r2r(xmm1, xmm2);       /* xmm2 = i2 * c2 */ \
    pmulhw_m2r(*C(6), xmm1);     /* xmm1 = c6 * i2 */ \
    \
    paddsw_r2r(xmm4, xmm4);      /* xmm4 = C + C */ \
    paddsw_r2r(xmm0, xmm4);      /* xmm4 = A + C = C. */ \
    \
    psubsw_r2r(xmm6, xmm3);      /* xmm3 = B - D */ \
    paddw_r2r(xmm7, xmm5);       /* xmm5 = c2 * i6 */ \
    \
    paddsw_r2r(xmm6, xmm6);      /* xmm6 = D + D */ \
    pmulhw_m2r(*C(6), xmm7);     /* xmm7 = c6 * i6 */ \
    \
    paddsw_r2r(xmm3, xmm6);      /* xmm6 = B + D = D. */ \
    movdqu_r2m(xmm4, *I(1));     /* Save C. at I(1) */ \
    \
    psubsw_r2r(xmm5, xmm1);      /* xmm1 = c6 * i2 - c2 * i6 = H */ \
    movdqu_m2r(*C(4), xmm4);     /* xmm4 = c4 */ \
    \
    movdqu_r2r(xmm3, xmm5);      /* xmm5 = B - D */ \
    pmulhw_r2r(xmm4, xmm3);      /* xmm3 = ( c4 -1 ) * ( B - D ) */ \
    \
    paddsw_r2r(xmm2, xmm7);      /* xmm7 = c2 * i2 + c6 * i6 = G */ \
    movdqu_r2m(xmm6, *I(2));     /* Save D. at I(2) */ \
    \
    movdqu_r2r(xmm0, xmm2);      /* xmm2 = A - C */ \
    movdqu_m2r(*I(0), xmm6);     /* xmm6 = i0 */ \
    \
    pmulhw_r2r(xmm4, xmm0);      /* xmm0 = ( c4 - 1 ) * ( A - C ) = A. */ \
    paddw_r2r(xmm3, xmm5);       /* xmm5 = c4 * ( B - D ) = B. */ \
    \
    movdqu_m2r(*I(4), xmm3);     /* xmm3 = i4 */ \
    psubsw_r2r(xmm1, xmm5);      /* xmm5 = B. - H = B.. */ \
    \
    paddw_r2r(xmm0, xmm2);       /* xmm2 = c4 * ( A - C) = A. */ \
    psubsw_r2r(xmm3, xmm6);      /* xmm6 = i0 - i4 */ \
    \
    movdqu_r2r(xmm6, xmm0);      /* xmm0 = i0 - i4 */ \
    pmulhw_r2r(xmm4, xmm6);      /* xmm6 = (c4 - 1) * (i0 - i4) = F */ \
    \
    paddsw_r2r(xmm3, xmm3);      /* xmm3 = i4 + i4 */ \
    paddsw_r2r(xmm1, xmm1);      /* xmm1 = H + H */ \
    \
    paddsw_r2r(xmm0, xmm3);      /* xmm3 = i0 + i4 */ \
    paddsw_r2r(xmm5, xmm1);      /* xmm1 = B. + H = H. */ \
    \
    pmulhw_r2r(xmm3, xmm4);      /* xmm4 = ( c4 - 1 ) * ( i0 + i4 )  */ \
    paddw_r2r(xmm0, xmm6);       /* xmm6 = c4 * ( i0 - i4 ) */ \
    \
    psubsw_r2r(xmm2, xmm6);      /* xmm6 = F - A. = F. */ \
    paddsw_r2r(xmm2, xmm2);      /* xmm2 = A. + A. */ \
    \
    movdqu_m2r(*I(1), xmm0);     /* Load        C. from I(1) */ \
    paddsw_r2r(xmm6, xmm2);      /* xmm2 = F + A. = A.. */ \
    \
    paddw_r2r(xmm3, xmm4);       /* xmm4 = c4 * ( i0 + i4 ) = 3 */ \
    psubsw_r2r(xmm1, xmm2);      /* xmm2 = A.. - H. = R2 */ \
    \
    paddsw_m2r(*Eight, xmm2);    /* Adjust R2 and R1 before shifting */ \
    paddsw_r2r(xmm1, xmm1);      /* xmm1 = H. + H. */ \
    \
    paddsw_r2r(xmm2, xmm1);      /* xmm1 = A.. + H. = R1 */ \
    psraw_i2r(4, xmm2);          /* xmm2 = op2 */ \
    \
    psubsw_r2r(xmm7, xmm4);      /* xmm4 = E - G = E. */ \
    psraw_i2r(4, xmm1);          /* xmm1 = op1 */ \
    \
    movdqu_m2r(*I(2), xmm3);     /* Load D. from I(2) */ \
    paddsw_r2r(xmm7, xmm7);      /* xmm7 = G + G */ \
    \
    movdqu_r2m(xmm2, *O(2));     /* Write out op2 */ \
    paddsw_r2r(xmm4, xmm7);      /* xmm7 = E + G = G. */ \
    \
    movdqu_r2m(xmm1, *O(1));     /* Write out op1 */ \
    psubsw_r2r(xmm3, xmm4);      /* xmm4 = E. - D. = R4 */ \
    \
    paddsw_m2r(*Eight, xmm4);    /* Adjust R4 and R3 before shifting */ \
    paddsw_r2r(xmm3, xmm3);      /* xmm3 = D. + D. */ \
    \
    paddsw_r2r(xmm4, xmm3);      /* xmm3 = E. + D. = R3 */ \
    psraw_i2r(4, xmm4);          /* xmm4 = op4 */ \
    \
    psubsw_r2r(xmm5, xmm6);      /* xmm6 = F. - B..= R6 */ \
    psraw_i2r(4, xmm3);          /* xmm3 = op3 */ \
    \
    paddsw_m2r(*Eight, xmm6);    /* Adjust R6 and R5 before shifting */ \
    paddsw_r2r(xmm5, xmm5);      /* xmm5 = B.. + B.. */ \
    \
    paddsw_r2r(xmm6, xmm5);      /* xmm5 = F. + B.. = R5 */ \
    psraw_i2r(4, xmm6);          /* xmm6 = op6 */ \
    \
    movdqu_r2m(xmm4, *O(4));     /* Write out op4 */ \
    psraw_i2r(4, xmm5);          /* xmm5 = op5 */ \
    \
    movdqu_r2m(xmm3, *O(3));     /* Write out op3 */ \
    psubsw_r2r(xmm0, xmm7);      /* xmm7 = G. - C. = R7 */ \
    \
    paddsw_m2r(*Eight, xmm7);    /* Adjust R7 and R0 before shifting */ \
    paddsw_r2r(xmm0, xmm0);      /* xmm0 = C. + C. */ \
    \
    paddsw_r2r(xmm7, xmm0);      /* xmm0 = G. + C. */ \
    psraw_i2r(4, xmm7);          /* xmm7 = op7 */ \
    \
    movdqu_r2m(xmm6, *O(6));     /* Write out op6 */ \
    psraw_i2r(4, xmm0);          /* xmm0 = op0 */ \
    \
    movdqu_r2m(xmm5, *O(5));     /* Write out op5 */ \
    movdqu_r2m(xmm7, *O(7));     /* Write out op7 */ \
    \
    movdqu_r2m(xmm0, *O(0));     /* Write out op0 */ \
    \
} /* End of SSE2_Column_IDCT macro */


#define SSE2_Row_IDCT() {        \
    \
    movdqu_m2r(*I(3), xmm2);     /* xmm2 = i3 */ \
    movdqu_m2r(*C(3), xmm6);     /* xmm6 = c3 */ \
    \
    movdqu_r2r(xmm2, xmm4);      /* xmm4 = i3 */ \
    movdqu_m2r(*I(5), xmm7);     /* xmm7 = i5 */ \
    \
    pmulhw_r2r(xmm6, xmm4);      /* xmm4 = c3 * i3 - i3 */ \
    movdqu_m2r(*C(5), xmm1);     /* xmm1 = c5 */ \
    \
    pmulhw_r2r(xmm7, xmm6);      /* xmm6 = c3 * i5 - i5 */ \
    movdqu_r2r(xmm1, xmm5);      /* xmm5 = c5 */ \
    \
    pmulhw_r2r(xmm2, xmm1);      /* xmm1 = c5 * i3 - i3 */ \
    movdqu_m2r(*I(1), xmm3);     /* xmm3 = i1 */ \
    \
    pmulhw_r2r(xmm7, xmm5);      /* xmm5 = c5 * i5 - i5 */ \
    movdqu_m2r(*C(1), xmm0);     /* xmm0 = c1 */ \
    \
    /* all registers are in use */ \
    \
    paddw_r2r(xmm2, xmm4);      /* xmm4 = c3 * i3 */ \
    paddw_r2r(xmm7, xmm6);      /* xmm6 = c3 * i5 */ \
    \
    paddw_r2r(xmm1, xmm2);      /* xmm2 = c5 * i3 */ \
    movdqu_m2r(*I(7), xmm1);    /* xmm1 = i7 */ \
    \
    paddw_r2r(xmm5, xmm7);      /* xmm7 = c5 * i5 */ \
    movdqu_r2r(xmm0, xmm5);     /* xmm5 = c1 */ \
    \
    pmulhw_r2r(xmm3, xmm0);     /* xmm0 = c1 * i1 - i1 */ \
    paddsw_r2r(xmm7, xmm4);     /* xmm4 = c3 * i3 + c5 * i5 = C */ \
    \
    pmulhw_r2r(xmm1, xmm5);     /* xmm5 = c1 * i7 - i7 */ \
    movdqu_m2r(*C(7), xmm7);    /* xmm7 = c7 */ \
    \
    psubsw_r2r(xmm2, xmm6);     /* xmm6 = c3 * i5 - c5 * i3 = D */ \
    paddw_r2r(xmm3, xmm0);      /* xmm0 = c1 * i1 */ \
    \
    pmulhw_r2r(xmm7, xmm3);     /* xmm3 = c7 * i1 */ \
    movdqu_m2r(*I(2), xmm2);    /* xmm2 = i2 */ \
    \
    pmulhw_r2r(xmm1, xmm7);     /* xmm7 = c7 * i7 */ \
    paddw_r2r(xmm1, xmm5);      /* xmm5 = c1 * i7 */ \
    \
    movdqu_r2r(xmm2, xmm1);     /* xmm1 = i2 */ \
    pmulhw_m2r(*C(2), xmm2);    /* xmm2 = i2 * c2 -i2 */ \
    \
    psubsw_r2r(xmm5, xmm3);     /* xmm3 = c7 * i1 - c1 * i7 = B */ \
    movdqu_m2r(*I(6), xmm5);    /* xmm5 = i6 */ \
    \
    paddsw_r2r(xmm7, xmm0);     /* xmm0 = c1 * i1 + c7 * i7        = A */ \
    movdqu_r2r(xmm5, xmm7);     /* xmm7 = i6 */ \
    \
    psubsw_r2r(xmm4, xmm0);     /* xmm0 = A - C */ \
    pmulhw_m2r(*C(2), xmm5);    /* xmm5 = c2 * i6 - i6 */ \
    \
    paddw_r2r(xmm1, xmm2);      /* xmm2 = i2 * c2 */ \
    pmulhw_m2r(*C(6), xmm1);    /* xmm1 = c6 * i2 */ \
    \
    paddsw_r2r(xmm4, xmm4);     /* xmm4 = C + C */ \
    paddsw_r2r(xmm0, xmm4);     /* xmm4 = A + C = C. */ \
    \
    psubsw_r2r(xmm6, xmm3);     /* xmm3 = B - D */ \
    paddw_r2r(xmm7, xmm5);      /* xmm5 = c2 * i6 */ \
    \
    paddsw_r2r(xmm6, xmm6);     /* xmm6 = D + D */ \
    pmulhw_m2r(*C(6), xmm7);    /* xmm7 = c6 * i6 */ \
    \
    paddsw_r2r(xmm3, xmm6);     /* xmm6 = B + D = D. */ \
    movdqu_r2m(xmm4, *I(1));    /* Save C. at I(1)        */ \
    \
    psubsw_r2r(xmm5, xmm1);     /* xmm1 = c6 * i2 - c2 * i6 = H */ \
    movdqu_m2r(*C(4), xmm4);    /* xmm4 = c4 */ \
    \
    movdqu_r2r(xmm3, xmm5);     /* xmm5 = B - D */ \
    pmulhw_r2r(xmm4, xmm3);     /* xmm3 = ( c4 -1 ) * ( B - D ) */ \
    \
    paddsw_r2r(xmm2, xmm7);     /* xmm7 = c2 * i2 + c6 * i6 = G */ \
    movdqu_r2m(xmm6, *I(2));    /* Save D. at I(2) */ \
    \
    movdqu_r2r(xmm0, xmm2);     /* xmm2 = A - C */ \
    movdqu_m2r(*I(0), xmm6);    /* xmm6 = i0 */ \
    \
    pmulhw_r2r(xmm4, xmm0);     /* xmm0 = ( c4 - 1 ) * ( A - C ) = A. */ \
    paddw_r2r(xmm3, xmm5);      /* xmm5 = c4 * ( B - D ) = B. */ \
    \
    movdqu_m2r(*I(4), xmm3);    /* xmm3 = i4 */ \
    psubsw_r2r(xmm1, xmm5);     /* xmm5 = B. - H = B.. */ \
    \
    paddw_r2r(xmm0, xmm2);      /* xmm2 = c4 * ( A - C) = A. */ \
    psubsw_r2r(xmm3, xmm6);     /* xmm6 = i0 - i4 */ \
    \
    movdqu_r2r(xmm6, xmm0);     /* xmm0 = i0 - i4 */ \
    pmulhw_r2r(xmm4, xmm6);     /* xmm6 = ( c4 - 1 ) * ( i0 - i4 ) = F */ \
    \
    paddsw_r2r(xmm3, xmm3);     /* xmm3 = i4 + i4 */ \
    paddsw_r2r(xmm1, xmm1);     /* xmm1 = H + H */ \
    \
    paddsw_r2r(xmm0, xmm3);     /* xmm3 = i0 + i4 */ \
    paddsw_r2r(xmm5, xmm1);     /* xmm1 = B. + H = H. */ \
    \
    pmulhw_r2r(xmm3, xmm4);     /* xmm4 = ( c4 - 1 ) * ( i0 + i4 )  */ \
    paddw_r2r(xmm0, xmm6);      /* xmm6 = c4 * ( i0 - i4 ) */ \
    \
    psubsw_r2r(xmm2, xmm6);     /* xmm6 = F - A. = F. */ \
    paddsw_r2r(xmm2, xmm2);     /* xmm2 = A. + A. */ \
    \
    movdqu_m2r(*I(1), xmm0);    /* Load C. from I(1) */ \
    paddsw_r2r(xmm6, xmm2);     /* xmm2 = F + A. = A.. */ \
    \
    paddw_r2r(xmm3, xmm4);      /* xmm4 = c4 * ( i0 + i4 ) = 3 */ \
    psubsw_r2r(xmm1, xmm2);     /* xmm2 = A.. - H. = R2 */ \
    \
    paddsw_r2r(xmm1, xmm1);     /* xmm1 = H. + H. */ \
    paddsw_r2r(xmm2, xmm1);     /* xmm1 = A.. + H. = R1 */ \
    \
    psubsw_r2r(xmm7, xmm4);     /* xmm4 = E - G = E. */ \
    \
    movdqu_m2r(*I(2), xmm3);    /* Load D. from I(2) */ \
    paddsw_r2r(xmm7, xmm7);     /* xmm7 = G + G */ \
    \
    movdqu_r2m(xmm2, *I(2));    /* Write out op2 */ \
    paddsw_r2r(xmm4, xmm7);     /* xmm7 = E + G = G. */ \
    \
    movdqu_r2m(xmm1, *I(1));    /* Write out op1 */ \
    psubsw_r2r(xmm3, xmm4);     /* xmm4 = E. - D. = R4 */ \
    \
    paddsw_r2r(xmm3, xmm3);     /* xmm3 = D. + D. */ \
    \
    paddsw_r2r(xmm4, xmm3);     /* xmm3 = E. + D. = R3 */ \
    \
    psubsw_r2r(xmm5, xmm6);     /* xmm6 = F. - B..= R6 */ \
    \
    paddsw_r2r(xmm5, xmm5);     /* xmm5 = B.. + B.. */ \
    \
    paddsw_r2r(xmm6, xmm5);     /* xmm5 = F. + B.. = R5 */ \
    \
    movdqu_r2m(xmm4, *I(4));    /* Write out op4 */ \
    \
    movdqu_r2m(xmm3, *I(3));    /* Write out op3 */ \
    psubsw_r2r(xmm0, xmm7);     /* xmm7 = G. - C. = R7 */ \
    \
    paddsw_r2r(xmm0, xmm0);     /* xmm0 = C. + C. */ \
    \
    paddsw_r2r(xmm7, xmm0);     /* xmm0 = G. + C. */ \
    \
    movdqu_r2m(xmm6, *I(6));    /* Write out op6 */ \
    \
    movdqu_r2m(xmm5, *I(5));    /* Write out op5 */ \
    movdqu_r2m(xmm7, *I(7));    /* Write out op7 */ \
    \
    movdqu_r2m(xmm0, *I(0));    /* Write out op0 */ \
    \
} /* End of SSE2_Row_IDCT macro */


#define SSE2_Transpose() {    \
    \
    movdqu_m2r(*I(4), xmm4);    /* xmm4=e7e6e5e4e3e2e1e0 */ \
    movdqu_m2r(*I(5), xmm0);    /* xmm4=f7f6f5f4f3f2f1f0 */ \
    \
    movdqu_r2r(xmm4, xmm5);     /* make a copy */ \
    punpcklwd_r2r(xmm0, xmm4);  /* xmm4=f3e3f2e2f1e1f0e0 */ \
    \
    punpckhwd_r2r(xmm0, xmm5);  /* xmm5=f7e7f6e6f5e5f4e4 */ \
    movdqu_m2r(*I(6), xmm6);    /* xmm6=g7g6g5g4g3g2g1g0 */ \
    \
    movdqu_m2r(*I(7), xmm0);    /* xmm0=h7h6h5h4h3h2h1h0 */ \
    movdqu_r2r(xmm6, xmm7);     /* make a copy */ \
    \
    punpcklwd_r2r(xmm0, xmm6);  /* xmm6=h3g3h3g2h1g1h0g0 */ \
    punpckhwd_r2r(xmm0, xmm7);  /* xmm7=h7g7h6g6h5g5h4g4 */ \
    \
    movdqu_r2r(xmm4, xmm3);     /* make a copy */ \
    punpckldq_r2r(xmm6, xmm4);  /* xmm4=h1g1f1e1h0g0f0e0 */ \
    \
    punpckhdq_r2r(xmm6, xmm3);  /* xmm3=h3g3g3e3h2g2f2e2 */ \
    movdqu_r2m(xmm3, *I(6));    /* save h3g3g3e3h2g2f2e2 */ \
    /* Free xmm6 */ \
    movdqu_r2r(xmm5, xmm6);     /* make a copy */ \
    punpckldq_r2r(xmm7, xmm5);  /* xmm5=h5g5f5e5h4g4f4e4 */ \
    \
    punpckhdq_r2r(xmm7, xmm6);  /* xmm6=h7g7f7e7h6g6f6e6 */ \
    movdqu_m2r(*I(0), xmm0);    /* xmm0=a7a6a5a4a3a2a1a0 */ \
    /* Free xmm7 */ \
    movdqu_m2r(*I(1), xmm1);    /* xmm1=b7b6b5b4b3b2b1b0 */ \
    movdqu_r2r(xmm0, xmm7);     /* make a copy */ \
    \
    punpcklwd_r2r(xmm1, xmm0);  /* xmm0=b3a3b2a2b1a1b0a0 */ \
    punpckhwd_r2r(xmm1, xmm7);  /* xmm7=b7a7b6a6b5a5b4a4 */ \
    /* Free xmm1 */ \
    movdqu_m2r(*I(2), xmm2);    /* xmm2=c7c6c5c4c3c2c1c0 */ \
    movdqu_m2r(*I(3), xmm3);    /* xmm3=d7d6d5d4d3d2d1d0 */ \
    \
    movdqu_r2r(xmm2, xmm1);     /* make a copy */ \
    punpcklwd_r2r(xmm3, xmm2);  /* xmm2=d3c3d2c2d1c1d0c0 */ \
    \
    punpckhwd_r2r(xmm3, xmm1);  /* xmm1=d7c7d6c6d5c5d4c4 */ \
    movdqu_r2r(xmm0, xmm3);     /* make a copy        */ \
    \
    punpckldq_r2r(xmm2, xmm0);  /* xmm0=d1c1b1a1d0c0b0a0 */ \
    punpckhdq_r2r(xmm2, xmm3);  /* xmm3=d3c3b3a3d2c2b2a2 */ \
    /* Free xmm2 */ \
    movdqu_r2r(xmm7, xmm2);     /* make a copy */ \
    punpckldq_r2r(xmm1, xmm2);  /* xmm2=d5c5b5a5d4c4b4a4 */ \
    \
    punpckhdq_r2r(xmm1, xmm7);  /* xmm7=d7c7b7a7d6c6b6a6 */ \
    movdqu_r2r(xmm0, xmm1);     /* make a copy */ \
    \
    punpcklqdq_r2r(xmm4, xmm0); /* xmm0=h0g0f0e0d0c0b0a0 */ \
    punpckhqdq_r2r(xmm4, xmm1); /* xmm1=h1g1g1e1d1c1b1a1 */ \
    \
    movdqu_r2m(xmm0, *I(0));    /* save I(0) */ \
    movdqu_r2m(xmm1, *I(1));    /* save I(1) */ \
    \
    movdqu_m2r(*I(6), xmm0);    /* load h3g3g3e3h2g2f2e2 */ \
    movdqu_r2r(xmm3, xmm1);     /* make a copy */ \
    \
    punpcklqdq_r2r(xmm0, xmm1); /* xmm1=h2g2f2e2d2c2b2a2 */ \
    punpckhqdq_r2r(xmm0, xmm3); /* xmm3=h3g3f3e3d3c3b3a3 */ \
    \
    movdqu_r2r(xmm2, xmm4);     /* make a copy */ \
    punpcklqdq_r2r(xmm5, xmm4); /* xmm4=h4g4f4e4d4c4b4a4 */ \
    \
    punpckhqdq_r2r(xmm5, xmm2); /* xmm2=h5g5f5e5d5c5b5a5 */ \
    movdqu_r2m(xmm1, *I(2));    /* save I(2) */ \
    \
    movdqu_r2m(xmm3, *I(3));    /* save I(3) */ \
    movdqu_r2m(xmm4, *I(4));    /* save I(4) */ \
    \
    movdqu_r2m(xmm2, *I(5));    /* save I(5) */ \
    movdqu_r2r(xmm7, xmm5);     /* make a copy */ \
    \
    punpcklqdq_r2r(xmm6, xmm5); /* xmm5=h6g6f6e6d6c6b6a6 */ \
    punpckhqdq_r2r(xmm6, xmm7); /* xmm7=h7g7f7e7d7c7b7a7 */ \
    \
    movdqu_r2m(xmm5, *I(6));    /* save I(6) */ \
    movdqu_r2m(xmm7, *I(7));    /* save I(7) */ \
    \
} /* End of Transpose Macro */


#define SSE2_Dequantize() {        \
    movdqu_m2r(*(eax), xmm0);     \
    \
    pmullw_m2r(*(ebx), xmm0);          /* xmm0 = 07 06 05 04 03 02 01 00 */ \
    movdqu_m2r(*(eax + 16), xmm1);     \
    \
    pmullw_m2r(*(ebx + 16), xmm1);     /* xmm1 = 17 16 15 14 13 12 11 10 */ \
    pshuflw_r2r(xmm0, xmm3, 0x078);    /* xmm3 = 07 06 05 04 01 03 02 00 */ \
    \
    movdqu_r2r(xmm1, xmm2);            /* xmm2 = 17 16 15 14 13 12 11 10 */ \
    movdqu_m2r(*(ecx), xmm7);          /* xmm7 = -- -- -- -- -- FF FF -- */ \
    \
    movdqu_m2r(*(eax + 32), xmm4);     \
    movdqu_m2r(*(eax + 64), xmm5);     \
    \
    pmullw_m2r(*(ebx + 32), xmm4);     /* xmm4 = 27 26 25 24 23 22 21 20 */ \
    pmullw_m2r(*(ebx + 64), xmm5);     /* xmm5 = 47 46 45 44 43 42 41 40 */ \
    \
    movdqu_m2r(*(ecx + 16), xmm6);     /* xmm6 = -- -- FF FF -- -- -- -- */ \
    pand_r2r(xmm2, xmm7);              /* xmm7 = -- -- -- -- -- 12 11 -- */ \
    \
    pand_r2r(xmm4, xmm6);              /* xmm6 = -- -- 25 24 -- -- -- -- */ \
    pxor_r2r(xmm7, xmm2);              /* xmm2 = 17 16 15 14 13 -- -- 10 */ \
    \
    pxor_r2r(xmm6, xmm4);              /* xmm4 = 27 26 -- -- 23 22 21 20 */ \
    pslldq_i2r(4, xmm7);               /* xmm7 = -- -- -- 12 11 -- -- -- */ \
    \
    pslldq_i2r(2, xmm6);               /* xmm6 = -- 25 24 -- -- -- -- -- */ \
    por_r2r(xmm6, xmm7);               /* xmm7 = -- 25 24 12 11 -- -- -- */ \
    \
    movdqu_m2r(*(ecx + 32), xmm0);     /* xmm0 = -- -- -- -- -- FF FF FF */ \
    movdqu_m2r(*(ecx + 48), xmm6);     /* xmm6 = -- -- -- -- FF -- -- -- */ \
    \
    pand_r2r(xmm3, xmm0);              /* xmm0 = -- -- -- -- -- 03 02 00 */ \
    pand_r2r(xmm5, xmm6);              /* xmm6 = -- -- -- -- 43 -- -- -- */ \
    \
    pxor_r2r(xmm0, xmm3);              /* xmm3 = 07 06 05 04 01 -- -- -- */ \
    pxor_r2r(xmm6, xmm5);              /* xmm5 = 47 46 45 44 -- 42 41 40 */ \
    \
    por_r2r(xmm7, xmm0);               /* xmm0 = -- 25 24 12 11 03 02 00 */ \
    pslldq_i2r(8, xmm6);               /* xmm6 = 43 -- -- -- -- -- -- -- */ \
    \
    por_r2r(xmm6, xmm0);               /* xmm0 = 43 25 24 12 11 03 02 00 */ \
    /* 02345 in use */ \
    \
    movdqu_m2r(*(ecx + 64 ), xmm1);    /* xmm1 = -- -- -- FF FF -- -- -- */ \
    pshuflw_r2r(xmm5, xmm5, 0x0B4);    /* xmm5 = 47 46 45 44 42 -- 41 40 */ \
    \
    movdqu_r2r(xmm1, xmm7);            /* xmm7 = -- -- -- FF FF -- -- -- */ \
    movdqu_r2r(xmm1, xmm6);            /* xmm6 = -- -- -- FF FF -- -- -- */ \
    \
    movdqu_r2m(xmm0, *(eax));          /* write  43 25 24 12 11 03 02 00 */ \
    pshufhw_r2r(xmm4, xmm4, 0x0C2);    /* xmm4 = 27 -- -- 26 23 22 21 20 */ \
    \
    pand_r2r(xmm4, xmm7);              /* xmm7 = -- -- -- 26 23 -- -- -- */ \
    pand_r2r(xmm5, xmm1);              /* xmm1 = -- -- -- 44 42 -- -- -- */ \
    \
    pxor_r2r(xmm7, xmm4);              /* xmm4 = 27 -- -- -- -- 22 21 20 */ \
    pxor_r2r(xmm1, xmm5);              /* xmm5 = 47 46 45 -- -- -- 41 40 */ \
    \
    pshuflw_r2r(xmm2, xmm2, 0x0C6);    /* xmm2 = 17 16 15 14 13 10 -- -- */ \
    movdqu_r2r(xmm6, xmm0);            /* xmm0 = -- -- -- FF FF -- -- -- */ \
    \
    pslldq_i2r(2, xmm7);               /* xmm7 = -- -- 26 23 -- -- -- -- */ \
    pslldq_i2r(6, xmm1);               /* xmm1 = 44 42 -- -- -- -- -- -- */ \
    \
    psrldq_i2r(2, xmm0);               /* xmm0 = -- -- -- -- FF FF -- -- */ \
    pand_r2r(xmm3, xmm6);              /* xmm6 = -- -- -- 04 01 -- -- -- */ \
    \
    pand_r2r(xmm2, xmm0);              /* xmm0 = -- -- -- -- 13 10 -- -- */ \
    pxor_r2r(xmm6, xmm3);              /* xmm3 = 07 06 05 -- -- -- -- -- */ \
    \
    pxor_r2r(xmm0, xmm2);              /* xmm2 = 17 16 15 14 -- -- -- -- */ \
    psrldq_i2r(6, xmm6);               /* xmm0 = -- -- -- -- -- -- 04 01 */ \
    \
    por_r2r(xmm7, xmm1);               /* xmm1 = 44 42 26 23 -- -- -- -- */ \
    por_r2r(xmm6, xmm0);               /* xmm1 = -- -- -- -- 13 10 04 01 */ \
    /* 12345 in use */    \
    por_r2r(xmm0, xmm1);               /* xmm1 = 44 42 26 23 13 10 04 01 */ \
    pshuflw_r2r(xmm4, xmm4, 0x093);    /* xmm4 = 27 -- -- -- 22 21 20 -- */ \
    \
    pshufhw_r2r(xmm4, xmm4, 0x093);    /* xmm4 = -- -- -- 27 22 21 20 -- */ \
    movdqu_r2m(xmm1, *(eax + 16));     /* write  44 42 26 23 13 10 04 01 */ \
    \
    pshufhw_r2r(xmm3, xmm3, 0x0D2);    /* xmm3 = 07 05 -- 06 -- -- -- -- */ \
    movdqu_m2r(*(ecx + 64), xmm0);     /* xmm0 = -- -- -- FF FF -- -- -- */ \
    \
    pand_r2r(xmm3, xmm0);              /* xmm0 = -- -- -- 06 -- -- -- -- */ \
    psrldq_i2r(12, xmm3);              /* xmm3 = -- -- -- -- -- -- 07 05 */ \
    \
    psrldq_i2r(8, xmm0);               /* xmm0 = -- -- -- -- -- -- -- 06 */ \
    \
    movdqu_m2r(*(ecx + 64), xmm6);     /* xmm6 = -- -- -- FF FF -- -- -- */ \
    movdqu_m2r(*(ecx + 96), xmm7);     /* xmm7 = -- -- -- -- FF FF -- -- */ \
    \
    pand_r2r(xmm4, xmm6);              /* xmm6 = -- -- -- 27 22 -- -- -- */ \
    pxor_r2r(xmm6, xmm4);              /* xmm4 = -- -- -- -- -- 21 20 -- */ \
    \
    por_r2r(xmm6, xmm3);               /* xmm3 = -- -- -- 27 22 -- 07 05 */ \
    pand_r2r(xmm4, xmm7);              /* xmm7 = -- -- -- -- -- 21 -- -- */ \
    \
    por_r2r(xmm7, xmm0);               /* xmm0 = -- -- -- -- -- 21 -- 06 */ \
    pxor_r2r(xmm7, xmm4);              /* xmm4 = -- -- -- -- -- -- 20 -- */ \
    \
    movdqu_m2r(*(ecx + 16 ), xmm6);    /* xmm6 = -- -- FF FF -- -- -- -- */ \
    movdqu_m2r(*(ecx + 64 ), xmm1);    /* xmm1 = -- -- -- FF FF -- -- -- */ \
    \
    pand_r2r(xmm2, xmm6);              /* xmm6 = -- -- 15 14 -- -- -- -- */ \
    pand_r2r(xmm6, xmm1);              /* xmm1 = -- -- -- 14 -- -- -- -- */ \
    \
    pxor_r2r(xmm6, xmm2);              /* xmm2 = 17 16 -- -- -- -- -- -- */ \
    pxor_r2r(xmm1, xmm6);              /* xmm6 = -- -- 15 -- -- -- -- -- */ \
    \
    psrldq_i2r(4, xmm1);               /* xmm1 = -- -- -- -- -- 14 -- -- */ \
    \
    psrldq_i2r(8, xmm6);               /* xmm6 = -- -- -- -- -- -- 15 -- */ \
    por_r2r(xmm1, xmm3);               /* xmm3 = -- -- -- 27 22 14 07 05 */ \
    \
    por_r2r(xmm6, xmm0);               /* xmm0 = -- -- -- -- -- 21 15 06 */ \
    pshufhw_r2r(xmm5, xmm5, 0x0E1);    /* xmm5 = 47 46 -- 45 -- -- 41 40 */ \
    \
    movdqu_m2r(*(ecx + 64), xmm1);     /* xmm1 = -- -- -- FF FF -- -- -- */ \
    pshuflw_r2r(xmm5, xmm5, 0x072);    /* xmm5 = 47 46 -- 45 41 -- 40 -- */ \
    \
    movdqu_r2r(xmm1, xmm6);            /* xmm6 = -- -- -- FF FF -- -- -- */ \
    pand_r2r(xmm5, xmm1);              /* xmm1 = -- -- -- 45 41 -- -- -- */ \
    \
    pxor_r2r(xmm1, xmm5);              /* xmm5 = 47 46 -- -- -- -- 40 -- */ \
    pslldq_i2r(4, xmm1);               /* xmm1 = -- 45 41 -- -- -- -- -- */ \
    \
    pshufd_r2r(xmm5, xmm5, 0x09C);     /* xmm5 = -- -- -- -- 47 46 40 -- */ \
    por_r2r(xmm1, xmm3);               /* xmm3 = -- 45 41 27 22 14 07 05 */ \
    \
    movdqu_m2r(*(eax + 96), xmm1);     /* xmm1 = 67 66 65 64 63 62 61 60 */ \
    pmullw_m2r(*(ebx + 96), xmm1);     \
    \
    movdqu_m2r(*(ecx), xmm7);          /* xmm7 = -- -- -- -- -- FF FF -- */ \
    \
    psrldq_i2r(8, xmm6);               /* xmm6 = -- -- -- -- -- -- -- FF */ \
    pand_r2r(xmm5, xmm7);              /* xmm7 = -- -- -- -- -- 46 40 -- */ \
    \
    pand_r2r(xmm1, xmm6);              /* xmm6 = -- -- -- -- -- -- -- 60 */ \
    pxor_r2r(xmm7, xmm5);              /* xmm5 = -- -- -- -- 47 -- -- -- */ \
    \
    pxor_r2r(xmm6, xmm1);              /* xmm1 = 67 66 65 64 63 62 61 -- */ \
    pslldq_i2r(2, xmm5);               /* xmm5 = -- -- -- 47 -- -- -- -- */ \
    \
    pslldq_i2r(14, xmm6);              /* xmm6 = 60 -- -- -- -- -- -- -- */ \
    por_r2r(xmm5, xmm4);               /* xmm4 = -- -- -- 47 -- -- 20 -- */ \
    \
    por_r2r(xmm6, xmm3);               /* xmm3 = 60 45 41 27 22 14 07 05 */ \
    pslldq_i2r(6, xmm7);               /* xmm7 = -- -- 46 40 -- -- -- -- */ \
    \
    movdqu_r2m(xmm3, *(eax+32));       /* write  60 45 41 27 22 14 07 05 */ \
    por_r2r(xmm7, xmm0);               /* xmm0 = -- -- 46 40 -- 21 15 06 */ \
    /* 0, 1, 2, 4 in use */    \
    movdqu_m2r(*(eax + 48), xmm3);     /* xmm3 = 37 36 35 34 33 32 31 30 */ \
    movdqu_m2r(*(eax + 80), xmm5);     /* xmm5 = 57 56 55 54 53 52 51 50 */ \
    \
    pmullw_m2r(*(ebx + 48), xmm3);     \
    pmullw_m2r(*(ebx + 80), xmm5);     \
    \
    movdqu_m2r(*(ecx + 64), xmm6);     /* xmm6 = -- -- -- FF FF -- -- -- */ \
    movdqu_m2r(*(ecx + 64), xmm7);     /* xmm7 = -- -- -- FF FF -- -- -- */ \
    \
    psrldq_i2r(8, xmm6);               /* xmm6 = -- -- -- -- -- -- -- FF */ \
    pslldq_i2r(8, xmm7);               /* xmm7 = FF -- -- -- -- -- -- -- */ \
    \
    pand_r2r(xmm3, xmm6);              /* xmm6 = -- -- -- -- -- -- -- 30 */ \
    pand_r2r(xmm5, xmm7);              /* xmm7 = 57 -- -- -- -- -- -- -- */ \
    \
    pxor_r2r(xmm6, xmm3);              /* xmm3 = 37 36 35 34 33 32 31 -- */ \
    pxor_r2r(xmm7, xmm5);              /* xmm5 = __ 56 55 54 53 52 51 50 */ \
    \
    pslldq_i2r(6, xmm6);               /* xmm6 = -- -- -- -- 30 -- -- -- */ \
    psrldq_i2r(2, xmm7);               /* xmm7 = -- 57 -- -- -- -- -- -- */ \
    \
    por_r2r(xmm7, xmm6);               /* xmm6 = -- 57 -- -- 30 -- -- -- */ \
    movdqu_m2r(*(ecx), xmm7);          /* xmm7 = -- -- -- -- -- FF FF -- */ \
    \
    por_r2r(xmm6, xmm0);               /* xmm0 = -- 57 46 40 30 21 15 06 */ \
    psrldq_i2r(2, xmm7);               /* xmm7 = -- -- -- -- -- -- FF FF */ \
    \
    movdqu_r2r(xmm2, xmm6);            /* xmm6 = 17 16 -- -- -- -- -- -- */ \
    pand_r2r(xmm1, xmm7);              /* xmm7 = -- -- -- -- -- -- 61 -- */ \
    \
    pslldq_i2r(2, xmm6);               /* xmm6 = 16 -- -- -- -- -- -- -- */ \
    psrldq_i2r(14, xmm2);              /* xmm2 = -- -- -- -- -- -- -- 17 */ \
    \
    pxor_r2r(xmm7, xmm1);              /* xmm1 = 67 66 65 64 63 62 -- -- */ \
    pslldq_i2r(12, xmm7);              /* xmm7 = 61 -- -- -- -- -- -- -- */ \
    \
    psrldq_i2r(14, xmm6);              /* xmm6 = -- -- -- -- -- -- -- 16 */ \
    por_r2r(xmm6, xmm4);               /* xmm4 = -- -- -- 47 -- -- 20 16 */ \
    \
    por_r2r(xmm7, xmm0);               /* xmm0 = 61 57 46 40 30 21 15 06 */ \
    movdqu_m2r(*(ecx), xmm6);          /* xmm6 = -- -- -- -- -- FF FF -- */ \
    \
    psrldq_i2r(2, xmm6);               /* xmm6 = -- -- -- -- -- -- FF FF */ \
    movdqu_r2m(xmm0, *(eax+48));       /* write  61 57 46 40 30 21 15 06 */ \
    /* 1, 2, 3, 4, 5 in use */\
    movdqu_m2r(*(ecx), xmm0);          /* xmm0 = -- -- -- -- -- FF FF -- */ \
    pand_r2r(xmm3, xmm6);              /* xmm6 = -- -- -- -- -- -- 31 -- */ \
    \
    movdqu_r2r(xmm3, xmm7);            /* xmm7 = 37 36 35 34 33 32 31 -- */ \
    pxor_r2r(xmm6, xmm3);              /* xmm3 = 37 36 35 34 33 32 -- -- */ \
    \
    pslldq_i2r(2, xmm3);               /* xmm3 = 36 35 34 33 32 -- -- -- */ \
    pand_r2r(xmm1, xmm0);              /* xmm0 = -- -- -- -- -- 62 -- -- */ \
    \
    psrldq_i2r(14, xmm7);              /* xmm7 = -- -- -- -- -- -- -- 37 */ \
    pxor_r2r(xmm0, xmm1);              /* xmm1 = 67 66 65 64 63 -- -- -- */ \
    \
    por_r2r(xmm7, xmm6);               /* xmm6 = -- -- -- -- -- -- 31 37 */ \
    movdqu_m2r(*(ecx + 64), xmm7);     /* xmm7 = -- -- -- FF FF -- -- -- */ \
    \
    pshuflw_r2r(xmm6, xmm6, 0x01E);    /* xmm6 = -- -- -- -- 37 31 -- -- */ \
    pslldq_i2r(6, xmm7);               /* xmm7 = FF FF -- -- -- -- -- -- */ \
    \
    por_r2r(xmm6, xmm4);               /* xmm4 = -- -- -- 47 37 31 20 16 */ \
    pand_r2r(xmm5, xmm7);              /* xmm7 = -- 56 -- -- -- -- -- -- */ \
    \
    pslldq_i2r(8, xmm0);               /* xmm0 = -- 62 -- -- -- -- -- -- */ \
    pxor_r2r(xmm7, xmm5);              /* xmm5 = -- -- 55 54 53 52 51 50 */ \
    \
    psrldq_i2r(2, xmm7);               /* xmm7 = -- -- 56 -- -- -- -- -- */ \
    \
    pshufhw_r2r(xmm3, xmm3, 0x087);    /* xmm3 = 35 33 34 36 32 -- -- -- */ \
    por_r2r(xmm7, xmm0);               /* xmm0 = -- 62 56 -- -- -- -- -- */ \
    \
    movdqu_m2r(*(eax + 112), xmm7);    /* xmm7 = 77 76 75 74 73 72 71 70 */ \
    pmullw_m2r(*(ebx + 112), xmm7);     \
    \
    movdqu_m2r(*(ecx + 64), xmm6);     /* xmm6 = -- -- -- FF FF -- -- -- */ \
    por_r2r(xmm0, xmm4);               /* xmm4 = -- 62 56 47 37 31 20 16 */ \
    \
    pshuflw_r2r(xmm7, xmm7, 0x0E1);    /* xmm7 = 77 76 75 74 73 72 70 71 */ \
    psrldq_i2r(8, xmm6);               /* xmm6 = -- -- -- -- -- -- -- FF */ \
    \
    movdqu_m2r(*(ecx + 64), xmm0);     /* xmm0 = -- -- -- FF FF -- -- -- */ \
    pand_r2r(xmm7, xmm6);              /* xmm6 = -- -- -- -- -- -- -- 71 */ \
    \
    pand_r2r(xmm3, xmm0);              /* xmm0 = -- -- -- 36 32 -- -- -- */ \
    pxor_r2r(xmm6, xmm7);              /* xmm7 = 77 76 75 74 73 72 70 -- */ \
    \
    pxor_r2r(xmm0, xmm3);              /* xmm3 = 35 33 34 -- -- -- -- -- */ \
    pslldq_i2r(14, xmm6);              /* xmm6 = 71 -- -- -- -- -- -- -- */ \
    \
    psrldq_i2r(4, xmm0);               /* xmm0 = -- -- -- -- -- 36 32 -- */ \
    por_r2r(xmm6, xmm4);               /* xmm4 = 71 62 56 47 37 31 20 16 */ \
    \
    por_r2r(xmm0, xmm2);               /* xmm2 = -- -- -- -- -- 36 32 17 */ \
    movdqu_r2m(xmm4, *(eax + 64));     /* write  71 62 56 47 37 31 20 16 */ \
    /* 1, 2, 3, 5, 7 in use */ \
    movdqu_m2r(*(ecx + 80), xmm6);     /* xmm6 = -- -- FF -- -- -- -- FF */ \
    pshufhw_r2r(xmm7, xmm7, 0x0D2);    /* xmm7 = 77 75 74 76 73 72 70 __ */ \
    \
    movdqu_m2r(*(ecx), xmm4);          /* xmm4 = -- -- -- -- -- FF FF -- */ \
    movdqu_m2r(*(ecx+48), xmm0);       /* xmm0 = -- -- -- -- FF -- -- -- */ \
    \
    pand_r2r(xmm5, xmm6);              /* xmm6 = -- -- 55 -- -- -- -- 50 */ \
    pand_r2r(xmm7, xmm4);              /* xmm4 = -- -- -- -- -- 72 70 -- */ \
    \
    pand_r2r(xmm1, xmm0);              /* xmm0 = -- -- -- -- 63 -- -- -- */ \
    pxor_r2r(xmm6, xmm5);              /* xmm5 = -- -- -- 54 53 52 51 -- */ \
    \
    pxor_r2r(xmm4, xmm7);              /* xmm7 = 77 75 74 76 73 -- -- -- */ \
    pxor_r2r(xmm0, xmm1);              /* xmm1 = 67 66 65 64 -- -- -- -- */ \
    \
    pshuflw_r2r(xmm6, xmm6, 0x02B);    /* xmm6 = -- -- 55 -- 50 -- -- -- */ \
    pslldq_i2r(10, xmm4);              /* xmm4 = 72 20 -- -- -- -- -- -- */ \
    \
    pshufhw_r2r(xmm6, xmm6, 0x0B1);    /* xmm6 = -- -- -- 55 50 -- -- -- */ \
    pslldq_i2r(4, xmm0);               /* xmm0 = -- -- 63 -- -- -- -- -- */ \
    \
    por_r2r(xmm4, xmm6);               /* xmm6 = 72 70 -- 55 50 -- -- -- */ \
    por_r2r(xmm0, xmm2);               /* xmm2 = -- -- 63 -- -- 36 32 17 */ \
    \
    por_r2r(xmm6, xmm2);               /* xmm2 = 72 70 64 55 50 36 32 17 */ \
    pshufhw_r2r(xmm1, xmm1, 0x0C9);    /* xmm1 = 67 64 66 65 -- -- -- -- */ \
    \
    movdqu_r2r(xmm3, xmm6);            /* xmm6 = 35 33 34 -- -- -- -- -- */ \
    movdqu_r2m(xmm2, *(eax+80));       /* write  72 70 64 55 50 36 32 17 */ \
    \
    psrldq_i2r(12, xmm6);              /* xmm6 = -- -- -- -- -- -- 35 33 */ \
    pslldq_i2r(4, xmm3);               /* xmm3 = 34 -- -- -- -- -- -- -- */ \
    \
    pshuflw_r2r(xmm5, xmm5, 0x04E);    /* xmm5 = -- -- -- 54 51 -- 53 52 */ \
    movdqu_r2r(xmm7, xmm4);            /* xmm4 = 77 75 74 76 73 -- -- -- */ \
    \
    movdqu_r2r(xmm5, xmm2);            /* xmm2 = -- -- -- 54 51 -- 53 52 */ \
    psrldq_i2r(10, xmm7);              /* xmm7 = -- -- -- -- -- 77 75 74 */ \
    \
    pslldq_i2r(6, xmm4);               /* xmm4 = 76 73 -- -- -- -- -- -- */ \
    pslldq_i2r(12, xmm2);              /* xmm2 = 53 52 -- -- -- -- -- -- */ \
    \
    movdqu_r2r(xmm1, xmm0);            /* xmm0 = 67 64 66 65 -- -- -- -- */ \
    psrldq_i2r(12, xmm1);              /* xmm1 = -- -- -- -- -- -- 67 64 */ \
    \
    psrldq_i2r(6, xmm5);               /* xmm5 = -- -- -- -- -- -- 54 51 */ \
    psrldq_i2r(14, xmm3);              /* xmm3 = -- -- -- -- -- -- -- 34 */ \
    \
    pslldq_i2r(10, xmm7);              /* xmm7 = 77 75 74 -- -- -- -- -- */ \
    por_r2r(xmm6, xmm4);               /* xmm4 = 76 73 -- -- -- -- 35 33 */ \
    \
    psrldq_i2r(10, xmm2);              /* xmm2 = -- -- -- -- -- 53 52 -- */ \
    pslldq_i2r(4, xmm0);               /* xmm0 = 66 65 -- -- -- -- -- -- */ \
    \
    pslldq_i2r(8, xmm1);               /* xmm1 = -- -- 67 64 -- -- -- -- */ \
    por_r2r(xmm7, xmm3);               /* xmm3 = 77 75 74 -- -- -- -- 34 */ \
    \
    psrldq_i2r(6, xmm0);               /* xmm0 = -- -- -- 66 65 -- -- -- */ \
    pslldq_i2r(4, xmm5);               /* xmm5 = -- -- -- -- 54 51 -- -- */ \
    \
    por_r2r(xmm1, xmm4);               /* xmm4 = 76 73 67 64 -- -- 35 33 */ \
    por_r2r(xmm2, xmm3);               /* xmm3 = 77 75 74 -- -- 53 52 34 */ \
    \
    por_r2r(xmm5, xmm4);               /* xmm4 = 76 73 67 64 54 51 35 33 */ \
    por_r2r(xmm0, xmm3);               /* xmm3 = 77 75 74 66 65 53 52 34 */ \
    \
    movdqu_r2m(xmm4, *(eax+96));       /* write  76 73 67 64 54 51 35 33 */ \
    movdqu_r2m(xmm3, *(eax+112));      /* write  77 75 74 66 65 53 52 34 */ \
    \
} /* end of SSE2_Dequantize Macro */


void ff_vp3_idct_sse2(int16_t *input_data)
{
    unsigned char *input_bytes = (unsigned char *)input_data;
    unsigned char *output_data_bytes = (unsigned char *)input_data;
    unsigned char *idct_data_bytes = (unsigned char *)SSE2_idct_data;
    unsigned char *Eight = (unsigned char *)eight_data;

#define eax input_bytes
//#define ebx dequant_matrix_bytes
#define ecx dequant_const_bytes
#define edx idct_data_bytes

#define I(i) (eax + 16 * i)
#define O(i) (ebx + 16 * i)
#define C(i) (edx + 16 * (i-1))

 //   SSE2_Dequantize();

#undef ebx
#define ebx output_data_bytes

    SSE2_Row_IDCT();

    SSE2_Transpose();

    SSE2_Column_IDCT();
}
