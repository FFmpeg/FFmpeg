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

#include "libavcodec/dsputil.h"
#include "dsputil_mmx.h"
#include "mmx.h"

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
    movdqa_m2r(*I(3), xmm2);     /* xmm2 = i3 */ \
    movdqa_m2r(*C(3), xmm6);     /* xmm6 = c3 */ \
    \
    movdqa_r2r(xmm2, xmm4);      /* xmm4 = i3 */ \
    movdqa_m2r(*I(5), xmm7);     /* xmm7 = i5 */ \
    \
    pmulhw_r2r(xmm6, xmm4);      /* xmm4 = c3 * i3 - i3 */ \
    movdqa_m2r(*C(5), xmm1);     /* xmm1 = c5 */ \
    \
    pmulhw_r2r(xmm7, xmm6);      /* xmm6 = c3 * i5 - i5 */ \
    movdqa_r2r(xmm1, xmm5);      /* xmm5 = c5 */ \
    \
    pmulhw_r2r(xmm2, xmm1);      /* xmm1 = c5 * i3 - i3 */ \
    movdqa_m2r(*I(1), xmm3);     /* xmm3 = i1 */ \
    \
    pmulhw_r2r(xmm7, xmm5);      /* xmm5 = c5 * i5 - i5 */ \
    movdqa_m2r(*C(1), xmm0);     /* xmm0 = c1 */ \
    \
    /* all registers are in use */ \
    \
    paddw_r2r(xmm2, xmm4);       /* xmm4 = c3 * i3 */ \
    paddw_r2r(xmm7, xmm6);       /* xmm6 = c3 * i5 */ \
    \
    paddw_r2r(xmm1, xmm2);       /* xmm2 = c5 * i3 */ \
    movdqa_m2r(*I(7), xmm1);     /* xmm1 = i7 */ \
    \
    paddw_r2r(xmm5, xmm7);       /* xmm7 = c5 * i5 */ \
    movdqa_r2r(xmm0, xmm5);      /* xmm5 = c1 */ \
    \
    pmulhw_r2r(xmm3, xmm0);      /* xmm0 = c1 * i1 - i1 */ \
    paddsw_r2r(xmm7, xmm4);      /* xmm4 = c3 * i3 + c5 * i5 = C */ \
    \
    pmulhw_r2r(xmm1, xmm5);      /* xmm5 = c1 * i7 - i7 */ \
    movdqa_m2r(*C(7), xmm7);     /* xmm7 = c7 */ \
    \
    psubsw_r2r(xmm2, xmm6);      /* xmm6 = c3 * i5 - c5 * i3 = D */ \
    paddw_r2r(xmm3, xmm0);       /* xmm0 = c1 * i1 */ \
    \
    pmulhw_r2r(xmm7, xmm3);      /* xmm3 = c7 * i1 */ \
    movdqa_m2r(*I(2), xmm2);     /* xmm2 = i2 */ \
    \
    pmulhw_r2r(xmm1, xmm7);      /* xmm7 = c7 * i7 */ \
    paddw_r2r(xmm1, xmm5);       /* xmm5 = c1 * i7 */ \
    \
    movdqa_r2r(xmm2, xmm1);      /* xmm1 = i2 */ \
    pmulhw_m2r(*C(2), xmm2);     /* xmm2 = i2 * c2 -i2 */ \
    \
    psubsw_r2r(xmm5, xmm3);      /* xmm3 = c7 * i1 - c1 * i7 = B */ \
    movdqa_m2r(*I(6), xmm5);     /* xmm5 = i6 */ \
    \
    paddsw_r2r(xmm7, xmm0);      /* xmm0 = c1 * i1 + c7 * i7 = A */ \
    movdqa_r2r(xmm5, xmm7);      /* xmm7 = i6 */ \
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
    movdqa_r2m(xmm4, *I(1));     /* Save C. at I(1) */ \
    \
    psubsw_r2r(xmm5, xmm1);      /* xmm1 = c6 * i2 - c2 * i6 = H */ \
    movdqa_m2r(*C(4), xmm4);     /* xmm4 = c4 */ \
    \
    movdqa_r2r(xmm3, xmm5);      /* xmm5 = B - D */ \
    pmulhw_r2r(xmm4, xmm3);      /* xmm3 = ( c4 -1 ) * ( B - D ) */ \
    \
    paddsw_r2r(xmm2, xmm7);      /* xmm7 = c2 * i2 + c6 * i6 = G */ \
    movdqa_r2m(xmm6, *I(2));     /* Save D. at I(2) */ \
    \
    movdqa_r2r(xmm0, xmm2);      /* xmm2 = A - C */ \
    movdqa_m2r(*I(0), xmm6);     /* xmm6 = i0 */ \
    \
    pmulhw_r2r(xmm4, xmm0);      /* xmm0 = ( c4 - 1 ) * ( A - C ) = A. */ \
    paddw_r2r(xmm3, xmm5);       /* xmm5 = c4 * ( B - D ) = B. */ \
    \
    movdqa_m2r(*I(4), xmm3);     /* xmm3 = i4 */ \
    psubsw_r2r(xmm1, xmm5);      /* xmm5 = B. - H = B.. */ \
    \
    paddw_r2r(xmm0, xmm2);       /* xmm2 = c4 * ( A - C) = A. */ \
    psubsw_r2r(xmm3, xmm6);      /* xmm6 = i0 - i4 */ \
    \
    movdqa_r2r(xmm6, xmm0);      /* xmm0 = i0 - i4 */ \
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
    movdqa_m2r(*I(1), xmm0);     /* Load        C. from I(1) */ \
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
    movdqa_m2r(*I(2), xmm3);     /* Load D. from I(2) */ \
    paddsw_r2r(xmm7, xmm7);      /* xmm7 = G + G */ \
    \
    movdqa_r2m(xmm2, *O(2));     /* Write out op2 */ \
    paddsw_r2r(xmm4, xmm7);      /* xmm7 = E + G = G. */ \
    \
    movdqa_r2m(xmm1, *O(1));     /* Write out op1 */ \
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
    movdqa_r2m(xmm4, *O(4));     /* Write out op4 */ \
    psraw_i2r(4, xmm5);          /* xmm5 = op5 */ \
    \
    movdqa_r2m(xmm3, *O(3));     /* Write out op3 */ \
    psubsw_r2r(xmm0, xmm7);      /* xmm7 = G. - C. = R7 */ \
    \
    paddsw_m2r(*Eight, xmm7);    /* Adjust R7 and R0 before shifting */ \
    paddsw_r2r(xmm0, xmm0);      /* xmm0 = C. + C. */ \
    \
    paddsw_r2r(xmm7, xmm0);      /* xmm0 = G. + C. */ \
    psraw_i2r(4, xmm7);          /* xmm7 = op7 */ \
    \
    movdqa_r2m(xmm6, *O(6));     /* Write out op6 */ \
    psraw_i2r(4, xmm0);          /* xmm0 = op0 */ \
    \
    movdqa_r2m(xmm5, *O(5));     /* Write out op5 */ \
    movdqa_r2m(xmm7, *O(7));     /* Write out op7 */ \
    \
    movdqa_r2m(xmm0, *O(0));     /* Write out op0 */ \
    \
} /* End of SSE2_Column_IDCT macro */


#define SSE2_Row_IDCT() {        \
    \
    movdqa_m2r(*I(3), xmm2);     /* xmm2 = i3 */ \
    movdqa_m2r(*C(3), xmm6);     /* xmm6 = c3 */ \
    \
    movdqa_r2r(xmm2, xmm4);      /* xmm4 = i3 */ \
    movdqa_m2r(*I(5), xmm7);     /* xmm7 = i5 */ \
    \
    pmulhw_r2r(xmm6, xmm4);      /* xmm4 = c3 * i3 - i3 */ \
    movdqa_m2r(*C(5), xmm1);     /* xmm1 = c5 */ \
    \
    pmulhw_r2r(xmm7, xmm6);      /* xmm6 = c3 * i5 - i5 */ \
    movdqa_r2r(xmm1, xmm5);      /* xmm5 = c5 */ \
    \
    pmulhw_r2r(xmm2, xmm1);      /* xmm1 = c5 * i3 - i3 */ \
    movdqa_m2r(*I(1), xmm3);     /* xmm3 = i1 */ \
    \
    pmulhw_r2r(xmm7, xmm5);      /* xmm5 = c5 * i5 - i5 */ \
    movdqa_m2r(*C(1), xmm0);     /* xmm0 = c1 */ \
    \
    /* all registers are in use */ \
    \
    paddw_r2r(xmm2, xmm4);      /* xmm4 = c3 * i3 */ \
    paddw_r2r(xmm7, xmm6);      /* xmm6 = c3 * i5 */ \
    \
    paddw_r2r(xmm1, xmm2);      /* xmm2 = c5 * i3 */ \
    movdqa_m2r(*I(7), xmm1);    /* xmm1 = i7 */ \
    \
    paddw_r2r(xmm5, xmm7);      /* xmm7 = c5 * i5 */ \
    movdqa_r2r(xmm0, xmm5);     /* xmm5 = c1 */ \
    \
    pmulhw_r2r(xmm3, xmm0);     /* xmm0 = c1 * i1 - i1 */ \
    paddsw_r2r(xmm7, xmm4);     /* xmm4 = c3 * i3 + c5 * i5 = C */ \
    \
    pmulhw_r2r(xmm1, xmm5);     /* xmm5 = c1 * i7 - i7 */ \
    movdqa_m2r(*C(7), xmm7);    /* xmm7 = c7 */ \
    \
    psubsw_r2r(xmm2, xmm6);     /* xmm6 = c3 * i5 - c5 * i3 = D */ \
    paddw_r2r(xmm3, xmm0);      /* xmm0 = c1 * i1 */ \
    \
    pmulhw_r2r(xmm7, xmm3);     /* xmm3 = c7 * i1 */ \
    movdqa_m2r(*I(2), xmm2);    /* xmm2 = i2 */ \
    \
    pmulhw_r2r(xmm1, xmm7);     /* xmm7 = c7 * i7 */ \
    paddw_r2r(xmm1, xmm5);      /* xmm5 = c1 * i7 */ \
    \
    movdqa_r2r(xmm2, xmm1);     /* xmm1 = i2 */ \
    pmulhw_m2r(*C(2), xmm2);    /* xmm2 = i2 * c2 -i2 */ \
    \
    psubsw_r2r(xmm5, xmm3);     /* xmm3 = c7 * i1 - c1 * i7 = B */ \
    movdqa_m2r(*I(6), xmm5);    /* xmm5 = i6 */ \
    \
    paddsw_r2r(xmm7, xmm0);     /* xmm0 = c1 * i1 + c7 * i7        = A */ \
    movdqa_r2r(xmm5, xmm7);     /* xmm7 = i6 */ \
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
    movdqa_r2m(xmm4, *I(1));    /* Save C. at I(1)        */ \
    \
    psubsw_r2r(xmm5, xmm1);     /* xmm1 = c6 * i2 - c2 * i6 = H */ \
    movdqa_m2r(*C(4), xmm4);    /* xmm4 = c4 */ \
    \
    movdqa_r2r(xmm3, xmm5);     /* xmm5 = B - D */ \
    pmulhw_r2r(xmm4, xmm3);     /* xmm3 = ( c4 -1 ) * ( B - D ) */ \
    \
    paddsw_r2r(xmm2, xmm7);     /* xmm7 = c2 * i2 + c6 * i6 = G */ \
    movdqa_r2m(xmm6, *I(2));    /* Save D. at I(2) */ \
    \
    movdqa_r2r(xmm0, xmm2);     /* xmm2 = A - C */ \
    movdqa_m2r(*I(0), xmm6);    /* xmm6 = i0 */ \
    \
    pmulhw_r2r(xmm4, xmm0);     /* xmm0 = ( c4 - 1 ) * ( A - C ) = A. */ \
    paddw_r2r(xmm3, xmm5);      /* xmm5 = c4 * ( B - D ) = B. */ \
    \
    movdqa_m2r(*I(4), xmm3);    /* xmm3 = i4 */ \
    psubsw_r2r(xmm1, xmm5);     /* xmm5 = B. - H = B.. */ \
    \
    paddw_r2r(xmm0, xmm2);      /* xmm2 = c4 * ( A - C) = A. */ \
    psubsw_r2r(xmm3, xmm6);     /* xmm6 = i0 - i4 */ \
    \
    movdqa_r2r(xmm6, xmm0);     /* xmm0 = i0 - i4 */ \
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
    movdqa_m2r(*I(1), xmm0);    /* Load C. from I(1) */ \
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
    movdqa_m2r(*I(2), xmm3);    /* Load D. from I(2) */ \
    paddsw_r2r(xmm7, xmm7);     /* xmm7 = G + G */ \
    \
    movdqa_r2m(xmm2, *I(2));    /* Write out op2 */ \
    paddsw_r2r(xmm4, xmm7);     /* xmm7 = E + G = G. */ \
    \
    movdqa_r2m(xmm1, *I(1));    /* Write out op1 */ \
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
    movdqa_r2m(xmm4, *I(4));    /* Write out op4 */ \
    \
    movdqa_r2m(xmm3, *I(3));    /* Write out op3 */ \
    psubsw_r2r(xmm0, xmm7);     /* xmm7 = G. - C. = R7 */ \
    \
    paddsw_r2r(xmm0, xmm0);     /* xmm0 = C. + C. */ \
    \
    paddsw_r2r(xmm7, xmm0);     /* xmm0 = G. + C. */ \
    \
    movdqa_r2m(xmm6, *I(6));    /* Write out op6 */ \
    \
    movdqa_r2m(xmm5, *I(5));    /* Write out op5 */ \
    movdqa_r2m(xmm7, *I(7));    /* Write out op7 */ \
    \
    movdqa_r2m(xmm0, *I(0));    /* Write out op0 */ \
    \
} /* End of SSE2_Row_IDCT macro */


#define SSE2_Transpose() {    \
    \
    movdqa_m2r(*I(4), xmm4);    /* xmm4=e7e6e5e4e3e2e1e0 */ \
    movdqa_m2r(*I(5), xmm0);    /* xmm4=f7f6f5f4f3f2f1f0 */ \
    \
    movdqa_r2r(xmm4, xmm5);     /* make a copy */ \
    punpcklwd_r2r(xmm0, xmm4);  /* xmm4=f3e3f2e2f1e1f0e0 */ \
    \
    punpckhwd_r2r(xmm0, xmm5);  /* xmm5=f7e7f6e6f5e5f4e4 */ \
    movdqa_m2r(*I(6), xmm6);    /* xmm6=g7g6g5g4g3g2g1g0 */ \
    \
    movdqa_m2r(*I(7), xmm0);    /* xmm0=h7h6h5h4h3h2h1h0 */ \
    movdqa_r2r(xmm6, xmm7);     /* make a copy */ \
    \
    punpcklwd_r2r(xmm0, xmm6);  /* xmm6=h3g3h3g2h1g1h0g0 */ \
    punpckhwd_r2r(xmm0, xmm7);  /* xmm7=h7g7h6g6h5g5h4g4 */ \
    \
    movdqa_r2r(xmm4, xmm3);     /* make a copy */ \
    punpckldq_r2r(xmm6, xmm4);  /* xmm4=h1g1f1e1h0g0f0e0 */ \
    \
    punpckhdq_r2r(xmm6, xmm3);  /* xmm3=h3g3g3e3h2g2f2e2 */ \
    movdqa_r2m(xmm3, *I(6));    /* save h3g3g3e3h2g2f2e2 */ \
    /* Free xmm6 */ \
    movdqa_r2r(xmm5, xmm6);     /* make a copy */ \
    punpckldq_r2r(xmm7, xmm5);  /* xmm5=h5g5f5e5h4g4f4e4 */ \
    \
    punpckhdq_r2r(xmm7, xmm6);  /* xmm6=h7g7f7e7h6g6f6e6 */ \
    movdqa_m2r(*I(0), xmm0);    /* xmm0=a7a6a5a4a3a2a1a0 */ \
    /* Free xmm7 */ \
    movdqa_m2r(*I(1), xmm1);    /* xmm1=b7b6b5b4b3b2b1b0 */ \
    movdqa_r2r(xmm0, xmm7);     /* make a copy */ \
    \
    punpcklwd_r2r(xmm1, xmm0);  /* xmm0=b3a3b2a2b1a1b0a0 */ \
    punpckhwd_r2r(xmm1, xmm7);  /* xmm7=b7a7b6a6b5a5b4a4 */ \
    /* Free xmm1 */ \
    movdqa_m2r(*I(2), xmm2);    /* xmm2=c7c6c5c4c3c2c1c0 */ \
    movdqa_m2r(*I(3), xmm3);    /* xmm3=d7d6d5d4d3d2d1d0 */ \
    \
    movdqa_r2r(xmm2, xmm1);     /* make a copy */ \
    punpcklwd_r2r(xmm3, xmm2);  /* xmm2=d3c3d2c2d1c1d0c0 */ \
    \
    punpckhwd_r2r(xmm3, xmm1);  /* xmm1=d7c7d6c6d5c5d4c4 */ \
    movdqa_r2r(xmm0, xmm3);     /* make a copy        */ \
    \
    punpckldq_r2r(xmm2, xmm0);  /* xmm0=d1c1b1a1d0c0b0a0 */ \
    punpckhdq_r2r(xmm2, xmm3);  /* xmm3=d3c3b3a3d2c2b2a2 */ \
    /* Free xmm2 */ \
    movdqa_r2r(xmm7, xmm2);     /* make a copy */ \
    punpckldq_r2r(xmm1, xmm2);  /* xmm2=d5c5b5a5d4c4b4a4 */ \
    \
    punpckhdq_r2r(xmm1, xmm7);  /* xmm7=d7c7b7a7d6c6b6a6 */ \
    movdqa_r2r(xmm0, xmm1);     /* make a copy */ \
    \
    punpcklqdq_r2r(xmm4, xmm0); /* xmm0=h0g0f0e0d0c0b0a0 */ \
    punpckhqdq_r2r(xmm4, xmm1); /* xmm1=h1g1g1e1d1c1b1a1 */ \
    \
    movdqa_r2m(xmm0, *I(0));    /* save I(0) */ \
    movdqa_r2m(xmm1, *I(1));    /* save I(1) */ \
    \
    movdqa_m2r(*I(6), xmm0);    /* load h3g3g3e3h2g2f2e2 */ \
    movdqa_r2r(xmm3, xmm1);     /* make a copy */ \
    \
    punpcklqdq_r2r(xmm0, xmm1); /* xmm1=h2g2f2e2d2c2b2a2 */ \
    punpckhqdq_r2r(xmm0, xmm3); /* xmm3=h3g3f3e3d3c3b3a3 */ \
    \
    movdqa_r2r(xmm2, xmm4);     /* make a copy */ \
    punpcklqdq_r2r(xmm5, xmm4); /* xmm4=h4g4f4e4d4c4b4a4 */ \
    \
    punpckhqdq_r2r(xmm5, xmm2); /* xmm2=h5g5f5e5d5c5b5a5 */ \
    movdqa_r2m(xmm1, *I(2));    /* save I(2) */ \
    \
    movdqa_r2m(xmm3, *I(3));    /* save I(3) */ \
    movdqa_r2m(xmm4, *I(4));    /* save I(4) */ \
    \
    movdqa_r2m(xmm2, *I(5));    /* save I(5) */ \
    movdqa_r2r(xmm7, xmm5);     /* make a copy */ \
    \
    punpcklqdq_r2r(xmm6, xmm5); /* xmm5=h6g6f6e6d6c6b6a6 */ \
    punpckhqdq_r2r(xmm6, xmm7); /* xmm7=h7g7f7e7d7c7b7a7 */ \
    \
    movdqa_r2m(xmm5, *I(6));    /* save I(6) */ \
    movdqa_r2m(xmm7, *I(7));    /* save I(7) */ \
    \
} /* End of Transpose Macro */

void ff_vp3_idct_sse2(int16_t *input_data)
{
    unsigned char *input_bytes = (unsigned char *)input_data;
    unsigned char *output_data_bytes = (unsigned char *)input_data;
    const unsigned char *idct_data_bytes = (const unsigned char *)SSE2_idct_data;
    const unsigned char *Eight = (const unsigned char *)&ff_pw_8;

#define eax input_bytes
#define edx idct_data_bytes

#define I(i) (eax + 16 * i)
#define O(i) (ebx + 16 * i)
#define C(i) (edx + 16 * (i-1))

#define ebx output_data_bytes

    SSE2_Row_IDCT();

    SSE2_Transpose();

    SSE2_Column_IDCT();
}

void ff_vp3_idct_put_sse2(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_vp3_idct_sse2(block);
    put_signed_pixels_clamped_mmx(block, dest, line_size);
}

void ff_vp3_idct_add_sse2(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_vp3_idct_sse2(block);
    add_pixels_clamped_mmx(block, dest, line_size);
}
