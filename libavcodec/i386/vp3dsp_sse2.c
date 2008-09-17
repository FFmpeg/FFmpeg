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

DECLARE_ALIGNED_16(const uint16_t, ff_vp3_idct_data[7 * 8]) =
{
    64277,64277,64277,64277,64277,64277,64277,64277,
    60547,60547,60547,60547,60547,60547,60547,60547,
    54491,54491,54491,54491,54491,54491,54491,54491,
    46341,46341,46341,46341,46341,46341,46341,46341,
    36410,36410,36410,36410,36410,36410,36410,36410,
    25080,25080,25080,25080,25080,25080,25080,25080,
    12785,12785,12785,12785,12785,12785,12785,12785
};


#define SSE2_Column_IDCT() \
    "movdqa "I(3)", %%xmm2 \n\t"     /* xmm2 = i3 */ \
    "movdqa "C(3)", %%xmm6 \n\t"     /* xmm6 = c3 */ \
    "movdqa %%xmm2, %%xmm4 \n\t"     /* xmm4 = i3 */ \
    "movdqa "I(5)", %%xmm7 \n\t"     /* xmm7 = i5 */ \
    "pmulhw %%xmm6, %%xmm4 \n\t"     /* xmm4 = c3 * i3 - i3 */ \
    "movdqa "C(5)", %%xmm1 \n\t"     /* xmm1 = c5 */ \
    "pmulhw %%xmm7, %%xmm6 \n\t"     /* xmm6 = c3 * i5 - i5 */ \
    "movdqa %%xmm1, %%xmm5 \n\t"     /* xmm5 = c5 */ \
    "pmulhw %%xmm2, %%xmm1 \n\t"     /* xmm1 = c5 * i3 - i3 */ \
    "movdqa "I(1)", %%xmm3 \n\t"     /* xmm3 = i1 */ \
    "pmulhw %%xmm7, %%xmm5 \n\t"     /* xmm5 = c5 * i5 - i5 */ \
    "movdqa "C(1)", %%xmm0 \n\t"     /* xmm0 = c1 */ \
    "paddw  %%xmm2, %%xmm4 \n\t"     /* xmm4 = c3 * i3 */ \
    "paddw  %%xmm7, %%xmm6 \n\t"     /* xmm6 = c3 * i5 */ \
    "paddw  %%xmm1, %%xmm2 \n\t"     /* xmm2 = c5 * i3 */ \
    "movdqa "I(7)", %%xmm1 \n\t"     /* xmm1 = i7 */ \
    "paddw  %%xmm5, %%xmm7 \n\t"     /* xmm7 = c5 * i5 */ \
    "movdqa %%xmm0, %%xmm5 \n\t"     /* xmm5 = c1 */ \
    "pmulhw %%xmm3, %%xmm0 \n\t"     /* xmm0 = c1 * i1 - i1 */ \
    "paddsw %%xmm7, %%xmm4 \n\t"     /* xmm4 = c3 * i3 + c5 * i5 = C */ \
    "pmulhw %%xmm1, %%xmm5 \n\t"     /* xmm5 = c1 * i7 - i7 */ \
    "movdqa "C(7)", %%xmm7 \n\t"     /* xmm7 = c7 */ \
    "psubsw %%xmm2, %%xmm6 \n\t"     /* xmm6 = c3 * i5 - c5 * i3 = D */ \
    "paddw  %%xmm3, %%xmm0 \n\t"     /* xmm0 = c1 * i1 */ \
    "pmulhw %%xmm7, %%xmm3 \n\t"     /* xmm3 = c7 * i1 */ \
    "movdqa "I(2)", %%xmm2 \n\t"     /* xmm2 = i2 */ \
    "pmulhw %%xmm1, %%xmm7 \n\t"     /* xmm7 = c7 * i7 */ \
    "paddw  %%xmm1, %%xmm5 \n\t"     /* xmm5 = c1 * i7 */ \
    "movdqa %%xmm2, %%xmm1 \n\t"     /* xmm1 = i2 */ \
    "pmulhw "C(2)", %%xmm2 \n\t"     /* xmm2 = i2 * c2 -i2 */ \
    "psubsw %%xmm5, %%xmm3 \n\t"     /* xmm3 = c7 * i1 - c1 * i7 = B */ \
    "movdqa "I(6)", %%xmm5 \n\t"     /* xmm5 = i6 */ \
    "paddsw %%xmm7, %%xmm0 \n\t"     /* xmm0 = c1 * i1 + c7 * i7 = A */ \
    "movdqa %%xmm5, %%xmm7 \n\t"     /* xmm7 = i6 */ \
    "psubsw %%xmm4, %%xmm0 \n\t"     /* xmm0 = A - C */ \
    "pmulhw "C(2)", %%xmm5 \n\t"     /* xmm5 = c2 * i6 - i6 */ \
    "paddw  %%xmm1, %%xmm2 \n\t"     /* xmm2 = i2 * c2 */ \
    "pmulhw "C(6)", %%xmm1 \n\t"     /* xmm1 = c6 * i2 */ \
    "paddsw %%xmm4, %%xmm4 \n\t"     /* xmm4 = C + C */ \
    "paddsw %%xmm0, %%xmm4 \n\t"     /* xmm4 = A + C = C. */ \
    "psubsw %%xmm6, %%xmm3 \n\t"     /* xmm3 = B - D */ \
    "paddw  %%xmm7, %%xmm5 \n\t"     /* xmm5 = c2 * i6 */ \
    "paddsw %%xmm6, %%xmm6 \n\t"     /* xmm6 = D + D */ \
    "pmulhw "C(6)", %%xmm7 \n\t"     /* xmm7 = c6 * i6 */ \
    "paddsw %%xmm3, %%xmm6 \n\t"     /* xmm6 = B + D = D. */ \
    "movdqa %%xmm4, "I(1)" \n\t"     /* Save C. at I(1) */ \
    "psubsw %%xmm5, %%xmm1 \n\t"     /* xmm1 = c6 * i2 - c2 * i6 = H */ \
    "movdqa "C(4)", %%xmm4 \n\t"     /* xmm4 = c4 */ \
    "movdqa %%xmm3, %%xmm5 \n\t"     /* xmm5 = B - D */ \
    "pmulhw %%xmm4, %%xmm3 \n\t"     /* xmm3 = ( c4 -1 ) * ( B - D ) */ \
    "paddsw %%xmm2, %%xmm7 \n\t"     /* xmm7 = c2 * i2 + c6 * i6 = G */ \
    "movdqa %%xmm6, "I(2)" \n\t"     /* Save D. at I(2) */ \
    "movdqa %%xmm0, %%xmm2 \n\t"     /* xmm2 = A - C */ \
    "movdqa "I(0)", %%xmm6 \n\t"     /* xmm6 = i0 */ \
    "pmulhw %%xmm4, %%xmm0 \n\t"     /* xmm0 = ( c4 - 1 ) * ( A - C ) = A. */ \
    "paddw  %%xmm3, %%xmm5 \n\t"     /* xmm5 = c4 * ( B - D ) = B. */ \
    "movdqa "I(4)", %%xmm3 \n\t"     /* xmm3 = i4 */ \
    "psubsw %%xmm1, %%xmm5 \n\t"     /* xmm5 = B. - H = B.. */ \
    "paddw  %%xmm0, %%xmm2 \n\t"     /* xmm2 = c4 * ( A - C) = A. */ \
    "psubsw %%xmm3, %%xmm6 \n\t"     /* xmm6 = i0 - i4 */ \
    "movdqa %%xmm6, %%xmm0 \n\t"     /* xmm0 = i0 - i4 */ \
    "pmulhw %%xmm4, %%xmm6 \n\t"     /* xmm6 = (c4 - 1) * (i0 - i4) = F */ \
    "paddsw %%xmm3, %%xmm3 \n\t"     /* xmm3 = i4 + i4 */ \
    "paddsw %%xmm1, %%xmm1 \n\t"     /* xmm1 = H + H */ \
    "paddsw %%xmm0, %%xmm3 \n\t"     /* xmm3 = i0 + i4 */ \
    "paddsw %%xmm5, %%xmm1 \n\t"     /* xmm1 = B. + H = H. */ \
    "pmulhw %%xmm3, %%xmm4 \n\t"     /* xmm4 = ( c4 - 1 ) * ( i0 + i4 )  */ \
    "paddw  %%xmm0, %%xmm6 \n\t"     /* xmm6 = c4 * ( i0 - i4 ) */ \
    "psubsw %%xmm2, %%xmm6 \n\t"     /* xmm6 = F - A. = F. */ \
    "paddsw %%xmm2, %%xmm2 \n\t"     /* xmm2 = A. + A. */ \
    "movdqa "I(1)", %%xmm0 \n\t"     /* Load        C. from I(1) */ \
    "paddsw %%xmm6, %%xmm2 \n\t"     /* xmm2 = F + A. = A.. */ \
    "paddw  %%xmm3, %%xmm4 \n\t"     /* xmm4 = c4 * ( i0 + i4 ) = 3 */ \
    "psubsw %%xmm1, %%xmm2 \n\t"     /* xmm2 = A.. - H. = R2 */ \
    "paddsw "OC_8", %%xmm2 \n\t"     /* Adjust R2 and R1 before shifting */ \
    "paddsw %%xmm1, %%xmm1 \n\t"     /* xmm1 = H. + H. */ \
    "paddsw %%xmm2, %%xmm1 \n\t"     /* xmm1 = A.. + H. = R1 */ \
    "psraw      $4, %%xmm2 \n\t"     /* xmm2 = op2 */ \
    "psubsw %%xmm7, %%xmm4 \n\t"     /* xmm4 = E - G = E. */ \
    "psraw      $4, %%xmm1 \n\t"     /* xmm1 = op1 */ \
    "movdqa "I(2)", %%xmm3 \n\t"     /* Load D. from I(2) */ \
    "paddsw %%xmm7, %%xmm7 \n\t"     /* xmm7 = G + G */ \
    "movdqa %%xmm2, "O(2)" \n\t"     /* Write out op2 */ \
    "paddsw %%xmm4, %%xmm7 \n\t"     /* xmm7 = E + G = G. */ \
    "movdqa %%xmm1, "O(1)" \n\t"     /* Write out op1 */ \
    "psubsw %%xmm3, %%xmm4 \n\t"     /* xmm4 = E. - D. = R4 */ \
    "paddsw "OC_8", %%xmm4 \n\t"     /* Adjust R4 and R3 before shifting */ \
    "paddsw %%xmm3, %%xmm3 \n\t"     /* xmm3 = D. + D. */ \
    "paddsw %%xmm4, %%xmm3 \n\t"     /* xmm3 = E. + D. = R3 */ \
    "psraw      $4, %%xmm4 \n\t"     /* xmm4 = op4 */ \
    "psubsw %%xmm5, %%xmm6 \n\t"     /* xmm6 = F. - B..= R6 */ \
    "psraw      $4, %%xmm3 \n\t"     /* xmm3 = op3 */ \
    "paddsw "OC_8", %%xmm6 \n\t"     /* Adjust R6 and R5 before shifting */ \
    "paddsw %%xmm5, %%xmm5 \n\t"     /* xmm5 = B.. + B.. */ \
    "paddsw %%xmm6, %%xmm5 \n\t"     /* xmm5 = F. + B.. = R5 */ \
    "psraw      $4, %%xmm6 \n\t"     /* xmm6 = op6 */ \
    "movdqa %%xmm4, "O(4)" \n\t"     /* Write out op4 */ \
    "psraw      $4, %%xmm5 \n\t"     /* xmm5 = op5 */ \
    "movdqa %%xmm3, "O(3)" \n\t"     /* Write out op3 */ \
    "psubsw %%xmm0, %%xmm7 \n\t"     /* xmm7 = G. - C. = R7 */ \
    "paddsw "OC_8", %%xmm7 \n\t"     /* Adjust R7 and R0 before shifting */ \
    "paddsw %%xmm0, %%xmm0 \n\t"     /* xmm0 = C. + C. */ \
    "paddsw %%xmm7, %%xmm0 \n\t"     /* xmm0 = G. + C. */ \
    "psraw      $4, %%xmm7 \n\t"     /* xmm7 = op7 */ \
    "movdqa %%xmm6, "O(6)" \n\t"     /* Write out op6 */ \
    "psraw      $4, %%xmm0 \n\t"     /* xmm0 = op0 */ \
    "movdqa %%xmm5, "O(5)" \n\t"     /* Write out op5 */ \
    "movdqa %%xmm7, "O(7)" \n\t"     /* Write out op7 */ \
    "movdqa %%xmm0, "O(0)" \n\t"     /* Write out op0 */


#define SSE2_Row_IDCT() \
    "movdqa "I(3)", %%xmm2 \n\t"     /* xmm2 = i3 */ \
    "movdqa "C(3)", %%xmm6 \n\t"     /* xmm6 = c3 */ \
    "movdqa %%xmm2, %%xmm4 \n\t"     /* xmm4 = i3 */ \
    "movdqa "I(5)", %%xmm7 \n\t"     /* xmm7 = i5 */ \
    "pmulhw %%xmm6, %%xmm4 \n\t"     /* xmm4 = c3 * i3 - i3 */ \
    "movdqa "C(5)", %%xmm1 \n\t"     /* xmm1 = c5 */ \
    "pmulhw %%xmm7, %%xmm6 \n\t"     /* xmm6 = c3 * i5 - i5 */ \
    "movdqa %%xmm1, %%xmm5 \n\t"     /* xmm5 = c5 */ \
    "pmulhw %%xmm2, %%xmm1 \n\t"     /* xmm1 = c5 * i3 - i3 */ \
    "movdqa "I(1)", %%xmm3 \n\t"     /* xmm3 = i1 */ \
    "pmulhw %%xmm7, %%xmm5 \n\t"     /* xmm5 = c5 * i5 - i5 */ \
    "movdqa "C(1)", %%xmm0 \n\t"     /* xmm0 = c1 */ \
    "paddw  %%xmm2, %%xmm4 \n\t"     /* xmm4 = c3 * i3 */ \
    "paddw  %%xmm7, %%xmm6 \n\t"     /* xmm6 = c3 * i5 */ \
    "paddw  %%xmm1, %%xmm2 \n\t"     /* xmm2 = c5 * i3 */ \
    "movdqa "I(7)", %%xmm1 \n\t"     /* xmm1 = i7 */ \
    "paddw  %%xmm5, %%xmm7 \n\t"     /* xmm7 = c5 * i5 */ \
    "movdqa %%xmm0, %%xmm5 \n\t"     /* xmm5 = c1 */ \
    "pmulhw %%xmm3, %%xmm0 \n\t"     /* xmm0 = c1 * i1 - i1 */ \
    "paddsw %%xmm7, %%xmm4 \n\t"     /* xmm4 = c3 * i3 + c5 * i5 = C */ \
    "pmulhw %%xmm1, %%xmm5 \n\t"     /* xmm5 = c1 * i7 - i7 */ \
    "movdqa "C(7)", %%xmm7 \n\t"     /* xmm7 = c7 */ \
    "psubsw %%xmm2, %%xmm6 \n\t"     /* xmm6 = c3 * i5 - c5 * i3 = D */ \
    "paddw  %%xmm3, %%xmm0 \n\t"     /* xmm0 = c1 * i1 */ \
    "pmulhw %%xmm7, %%xmm3 \n\t"     /* xmm3 = c7 * i1 */ \
    "movdqa "I(2)", %%xmm2 \n\t"     /* xmm2 = i2 */ \
    "pmulhw %%xmm1, %%xmm7 \n\t"     /* xmm7 = c7 * i7 */ \
    "paddw  %%xmm1, %%xmm5 \n\t"     /* xmm5 = c1 * i7 */ \
    "movdqa %%xmm2, %%xmm1 \n\t"     /* xmm1 = i2 */ \
    "pmulhw "C(2)", %%xmm2 \n\t"     /* xmm2 = i2 * c2 -i2 */ \
    "psubsw %%xmm5, %%xmm3 \n\t"     /* xmm3 = c7 * i1 - c1 * i7 = B */ \
    "movdqa "I(6)", %%xmm5 \n\t"     /* xmm5 = i6 */ \
    "paddsw %%xmm7, %%xmm0 \n\t"     /* xmm0 = c1 * i1 + c7 * i7        = A */ \
    "movdqa %%xmm5, %%xmm7 \n\t"     /* xmm7 = i6 */ \
    "psubsw %%xmm4, %%xmm0 \n\t"     /* xmm0 = A - C */ \
    "pmulhw "C(2)", %%xmm5 \n\t"     /* xmm5 = c2 * i6 - i6 */ \
    "paddw  %%xmm1, %%xmm2 \n\t"     /* xmm2 = i2 * c2 */ \
    "pmulhw "C(6)", %%xmm1 \n\t"     /* xmm1 = c6 * i2 */ \
    "paddsw %%xmm4, %%xmm4 \n\t"     /* xmm4 = C + C */ \
    "paddsw %%xmm0, %%xmm4 \n\t"     /* xmm4 = A + C = C. */ \
    "psubsw %%xmm6, %%xmm3 \n\t"     /* xmm3 = B - D */ \
    "paddw  %%xmm7, %%xmm5 \n\t"     /* xmm5 = c2 * i6 */ \
    "paddsw %%xmm6, %%xmm6 \n\t"     /* xmm6 = D + D */ \
    "pmulhw "C(6)", %%xmm7 \n\t"     /* xmm7 = c6 * i6 */ \
    "paddsw %%xmm3, %%xmm6 \n\t"     /* xmm6 = B + D = D. */ \
    "movdqa %%xmm4, "I(1)" \n\t"     /* Save C. at I(1)        */ \
    "psubsw %%xmm5, %%xmm1 \n\t"     /* xmm1 = c6 * i2 - c2 * i6 = H */ \
    "movdqa "C(4)", %%xmm4 \n\t"     /* xmm4 = c4 */ \
    "movdqa %%xmm3, %%xmm5 \n\t"     /* xmm5 = B - D */ \
    "pmulhw %%xmm4, %%xmm3 \n\t"     /* xmm3 = ( c4 -1 ) * ( B - D ) */ \
    "paddsw %%xmm2, %%xmm7 \n\t"     /* xmm7 = c2 * i2 + c6 * i6 = G */ \
    "movdqa %%xmm6, "I(2)" \n\t"     /* Save D. at I(2) */ \
    "movdqa %%xmm0, %%xmm2 \n\t"     /* xmm2 = A - C */ \
    "movdqa "I(0)", %%xmm6 \n\t"     /* xmm6 = i0 */ \
    "pmulhw %%xmm4, %%xmm0 \n\t"     /* xmm0 = ( c4 - 1 ) * ( A - C ) = A. */ \
    "paddw  %%xmm3, %%xmm5 \n\t"     /* xmm5 = c4 * ( B - D ) = B. */ \
    "movdqa "I(4)", %%xmm3 \n\t"     /* xmm3 = i4 */ \
    "psubsw %%xmm1, %%xmm5 \n\t"     /* xmm5 = B. - H = B.. */ \
    "paddw  %%xmm0, %%xmm2 \n\t"     /* xmm2 = c4 * ( A - C) = A. */ \
    "psubsw %%xmm3, %%xmm6 \n\t"     /* xmm6 = i0 - i4 */ \
    "movdqa %%xmm6, %%xmm0 \n\t"     /* xmm0 = i0 - i4 */ \
    "pmulhw %%xmm4, %%xmm6 \n\t"     /* xmm6 = ( c4 - 1 ) * ( i0 - i4 ) = F */ \
    "paddsw %%xmm3, %%xmm3 \n\t"     /* xmm3 = i4 + i4 */ \
    "paddsw %%xmm1, %%xmm1 \n\t"     /* xmm1 = H + H */ \
    "paddsw %%xmm0, %%xmm3 \n\t"     /* xmm3 = i0 + i4 */ \
    "paddsw %%xmm5, %%xmm1 \n\t"     /* xmm1 = B. + H = H. */ \
    "pmulhw %%xmm3, %%xmm4 \n\t"     /* xmm4 = ( c4 - 1 ) * ( i0 + i4 )  */ \
    "paddw  %%xmm0, %%xmm6 \n\t"     /* xmm6 = c4 * ( i0 - i4 ) */ \
    "psubsw %%xmm2, %%xmm6 \n\t"     /* xmm6 = F - A. = F. */ \
    "paddsw %%xmm2, %%xmm2 \n\t"     /* xmm2 = A. + A. */ \
    "movdqa "I(1)", %%xmm0 \n\t"     /* Load C. from I(1) */ \
    "paddsw %%xmm6, %%xmm2 \n\t"     /* xmm2 = F + A. = A.. */ \
    "paddw  %%xmm3, %%xmm4 \n\t"     /* xmm4 = c4 * ( i0 + i4 ) = 3 */ \
    "psubsw %%xmm1, %%xmm2 \n\t"     /* xmm2 = A.. - H. = R2 */ \
    "paddsw %%xmm1, %%xmm1 \n\t"     /* xmm1 = H. + H. */ \
    "paddsw %%xmm2, %%xmm1 \n\t"     /* xmm1 = A.. + H. = R1 */ \
    "psubsw %%xmm7, %%xmm4 \n\t"     /* xmm4 = E - G = E. */ \
    "movdqa "I(2)", %%xmm3 \n\t"     /* Load D. from I(2) */ \
    "paddsw %%xmm7, %%xmm7 \n\t"     /* xmm7 = G + G */ \
    "movdqa %%xmm2, "I(2)" \n\t"     /* Write out op2 */ \
    "paddsw %%xmm4, %%xmm7 \n\t"     /* xmm7 = E + G = G. */ \
    "movdqa %%xmm1, "I(1)" \n\t"     /* Write out op1 */ \
    "psubsw %%xmm3, %%xmm4 \n\t"     /* xmm4 = E. - D. = R4 */ \
    "paddsw %%xmm3, %%xmm3 \n\t"     /* xmm3 = D. + D. */ \
    "paddsw %%xmm4, %%xmm3 \n\t"     /* xmm3 = E. + D. = R3 */ \
    "psubsw %%xmm5, %%xmm6 \n\t"     /* xmm6 = F. - B..= R6 */ \
    "paddsw %%xmm5, %%xmm5 \n\t"     /* xmm5 = B.. + B.. */ \
    "paddsw %%xmm6, %%xmm5 \n\t"     /* xmm5 = F. + B.. = R5 */ \
    "movdqa %%xmm4, "I(4)" \n\t"     /* Write out op4 */ \
    "movdqa %%xmm3, "I(3)" \n\t"     /* Write out op3 */ \
    "psubsw %%xmm0, %%xmm7 \n\t"     /* xmm7 = G. - C. = R7 */ \
    "paddsw %%xmm0, %%xmm0 \n\t"     /* xmm0 = C. + C. */ \
    "paddsw %%xmm7, %%xmm0 \n\t"     /* xmm0 = G. + C. */ \
    "movdqa %%xmm6, "I(6)" \n\t"     /* Write out op6 */ \
    "movdqa %%xmm5, "I(5)" \n\t"     /* Write out op5 */ \
    "movdqa %%xmm7, "I(7)" \n\t"     /* Write out op7 */ \
    "movdqa %%xmm0, "I(0)" \n\t"     /* Write out op0 */

#define SSE2_Transpose() \
    "movdqa     "I(4)", %%xmm4 \n\t"     /* xmm4=e7e6e5e4e3e2e1e0 */ \
    "movdqa     "I(5)", %%xmm0 \n\t"     /* xmm4=f7f6f5f4f3f2f1f0 */ \
    "movdqa     %%xmm4, %%xmm5 \n\t"     /* make a copy */ \
    "punpcklwd  %%xmm0, %%xmm4 \n\t"     /* xmm4=f3e3f2e2f1e1f0e0 */ \
    "punpckhwd  %%xmm0, %%xmm5 \n\t"     /* xmm5=f7e7f6e6f5e5f4e4 */ \
    "movdqa     "I(6)", %%xmm6 \n\t"     /* xmm6=g7g6g5g4g3g2g1g0 */ \
    "movdqa     "I(7)", %%xmm0 \n\t"     /* xmm0=h7h6h5h4h3h2h1h0 */ \
    "movdqa     %%xmm6, %%xmm7 \n\t"     /* make a copy */ \
    "punpcklwd  %%xmm0, %%xmm6 \n\t"     /* xmm6=h3g3h3g2h1g1h0g0 */ \
    "punpckhwd  %%xmm0, %%xmm7 \n\t"     /* xmm7=h7g7h6g6h5g5h4g4 */ \
    "movdqa     %%xmm4, %%xmm3 \n\t"     /* make a copy */ \
    "punpckldq  %%xmm6, %%xmm4 \n\t"     /* xmm4=h1g1f1e1h0g0f0e0 */ \
    "punpckhdq  %%xmm6, %%xmm3 \n\t"     /* xmm3=h3g3g3e3h2g2f2e2 */ \
    "movdqa     %%xmm3, "I(6)" \n\t"     /* save h3g3g3e3h2g2f2e2 */ \
    "movdqa     %%xmm5, %%xmm6 \n\t"     /* make a copy */ \
    "punpckldq  %%xmm7, %%xmm5 \n\t"     /* xmm5=h5g5f5e5h4g4f4e4 */ \
    "punpckhdq  %%xmm7, %%xmm6 \n\t"     /* xmm6=h7g7f7e7h6g6f6e6 */ \
    "movdqa     "I(0)", %%xmm0 \n\t"     /* xmm0=a7a6a5a4a3a2a1a0 */ \
    "movdqa     "I(1)", %%xmm1 \n\t"     /* xmm1=b7b6b5b4b3b2b1b0 */ \
    "movdqa     %%xmm0, %%xmm7 \n\t"     /* make a copy */ \
    "punpcklwd  %%xmm1, %%xmm0 \n\t"     /* xmm0=b3a3b2a2b1a1b0a0 */ \
    "punpckhwd  %%xmm1, %%xmm7 \n\t"     /* xmm7=b7a7b6a6b5a5b4a4 */ \
    "movdqa     "I(2)", %%xmm2 \n\t"     /* xmm2=c7c6c5c4c3c2c1c0 */ \
    "movdqa     "I(3)", %%xmm3 \n\t"     /* xmm3=d7d6d5d4d3d2d1d0 */ \
    "movdqa     %%xmm2, %%xmm1 \n\t"     /* make a copy */ \
    "punpcklwd  %%xmm3, %%xmm2 \n\t"     /* xmm2=d3c3d2c2d1c1d0c0 */ \
    "punpckhwd  %%xmm3, %%xmm1 \n\t"     /* xmm1=d7c7d6c6d5c5d4c4 */ \
    "movdqa     %%xmm0, %%xmm3 \n\t"     /* make a copy        */ \
    "punpckldq  %%xmm2, %%xmm0 \n\t"     /* xmm0=d1c1b1a1d0c0b0a0 */ \
    "punpckhdq  %%xmm2, %%xmm3 \n\t"     /* xmm3=d3c3b3a3d2c2b2a2 */ \
    "movdqa     %%xmm7, %%xmm2 \n\t"     /* make a copy */ \
    "punpckldq  %%xmm1, %%xmm2 \n\t"     /* xmm2=d5c5b5a5d4c4b4a4 */ \
    "punpckhdq  %%xmm1, %%xmm7 \n\t"     /* xmm7=d7c7b7a7d6c6b6a6 */ \
    "movdqa     %%xmm0, %%xmm1 \n\t"     /* make a copy */ \
    "punpcklqdq %%xmm4, %%xmm0 \n\t"     /* xmm0=h0g0f0e0d0c0b0a0 */ \
    "punpckhqdq %%xmm4, %%xmm1 \n\t"     /* xmm1=h1g1g1e1d1c1b1a1 */ \
    "movdqa     %%xmm0, "I(0)" \n\t"     /* save I(0) */ \
    "movdqa     %%xmm1, "I(1)" \n\t"     /* save I(1) */ \
    "movdqa     "I(6)", %%xmm0 \n\t"     /* load h3g3g3e3h2g2f2e2 */ \
    "movdqa     %%xmm3, %%xmm1 \n\t"     /* make a copy */ \
    "punpcklqdq %%xmm0, %%xmm1 \n\t"     /* xmm1=h2g2f2e2d2c2b2a2 */ \
    "punpckhqdq %%xmm0, %%xmm3 \n\t"     /* xmm3=h3g3f3e3d3c3b3a3 */ \
    "movdqa     %%xmm2, %%xmm4 \n\t"     /* make a copy */ \
    "punpcklqdq %%xmm5, %%xmm4 \n\t"     /* xmm4=h4g4f4e4d4c4b4a4 */ \
    "punpckhqdq %%xmm5, %%xmm2 \n\t"     /* xmm2=h5g5f5e5d5c5b5a5 */ \
    "movdqa     %%xmm1, "I(2)" \n\t"     /* save I(2) */ \
    "movdqa     %%xmm3, "I(3)" \n\t"     /* save I(3) */ \
    "movdqa     %%xmm4, "I(4)" \n\t"     /* save I(4) */ \
    "movdqa     %%xmm2, "I(5)" \n\t"     /* save I(5) */ \
    "movdqa     %%xmm7, %%xmm5 \n\t"     /* make a copy */ \
    "punpcklqdq %%xmm6, %%xmm5 \n\t"     /* xmm5=h6g6f6e6d6c6b6a6 */ \
    "punpckhqdq %%xmm6, %%xmm7 \n\t"     /* xmm7=h7g7f7e7d7c7b7a7 */ \
    "movdqa     %%xmm5, "I(6)" \n\t"     /* save I(6) */ \
    "movdqa     %%xmm7, "I(7)" \n\t"     /* save I(7) */

void ff_vp3_idct_sse2(int16_t *input_data)
{
#define OC_8 "%2"

#define I(x) AV_STRINGIFY(16*x)"(%0)"
#define O(x) I(x)
#define C(x) AV_STRINGIFY(16*(x-1))"(%1)"

    asm volatile (
        SSE2_Row_IDCT()

        SSE2_Transpose()

        SSE2_Column_IDCT()
        :: "r"(input_data), "r"(ff_vp3_idct_data), "m"(ff_pw_8)
    );
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
