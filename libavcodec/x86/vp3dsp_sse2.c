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
 * @file
 * SSE2-optimized functions cribbed from the original VP3 source code.
 */

#include "libavcodec/dsputil.h"
#include "dsputil_mmx.h"
#include "vp3dsp_sse2.h"

DECLARE_ALIGNED(16, const uint16_t, ff_vp3_idct_data)[7 * 8] =
{
    64277,64277,64277,64277,64277,64277,64277,64277,
    60547,60547,60547,60547,60547,60547,60547,60547,
    54491,54491,54491,54491,54491,54491,54491,54491,
    46341,46341,46341,46341,46341,46341,46341,46341,
    36410,36410,36410,36410,36410,36410,36410,36410,
    25080,25080,25080,25080,25080,25080,25080,25080,
    12785,12785,12785,12785,12785,12785,12785,12785
};


#define VP3_1D_IDCT_SSE2(ADD, SHIFT) \
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
    ADD(%%xmm2)                      /* Adjust R2 and R1 before shifting */ \
    "paddsw %%xmm1, %%xmm1 \n\t"     /* xmm1 = H. + H. */ \
    "paddsw %%xmm2, %%xmm1 \n\t"     /* xmm1 = A.. + H. = R1 */ \
    SHIFT(%%xmm2)                    /* xmm2 = op2 */ \
    "psubsw %%xmm7, %%xmm4 \n\t"     /* xmm4 = E - G = E. */ \
    SHIFT(%%xmm1)                    /* xmm1 = op1 */ \
    "movdqa "I(2)", %%xmm3 \n\t"     /* Load D. from I(2) */ \
    "paddsw %%xmm7, %%xmm7 \n\t"     /* xmm7 = G + G */ \
    "paddsw %%xmm4, %%xmm7 \n\t"     /* xmm7 = E + G = G. */ \
    "psubsw %%xmm3, %%xmm4 \n\t"     /* xmm4 = E. - D. = R4 */ \
    ADD(%%xmm4)                      /* Adjust R4 and R3 before shifting */ \
    "paddsw %%xmm3, %%xmm3 \n\t"     /* xmm3 = D. + D. */ \
    "paddsw %%xmm4, %%xmm3 \n\t"     /* xmm3 = E. + D. = R3 */ \
    SHIFT(%%xmm4)                    /* xmm4 = op4 */ \
    "psubsw %%xmm5, %%xmm6 \n\t"     /* xmm6 = F. - B..= R6 */ \
    SHIFT(%%xmm3)                    /* xmm3 = op3 */ \
    ADD(%%xmm6)                      /* Adjust R6 and R5 before shifting */ \
    "paddsw %%xmm5, %%xmm5 \n\t"     /* xmm5 = B.. + B.. */ \
    "paddsw %%xmm6, %%xmm5 \n\t"     /* xmm5 = F. + B.. = R5 */ \
    SHIFT(%%xmm6)                    /* xmm6 = op6 */ \
    SHIFT(%%xmm5)                    /* xmm5 = op5 */ \
    "psubsw %%xmm0, %%xmm7 \n\t"     /* xmm7 = G. - C. = R7 */ \
    ADD(%%xmm7)                      /* Adjust R7 and R0 before shifting */ \
    "paddsw %%xmm0, %%xmm0 \n\t"     /* xmm0 = C. + C. */ \
    "paddsw %%xmm7, %%xmm0 \n\t"     /* xmm0 = G. + C. */ \
    SHIFT(%%xmm7)                    /* xmm7 = op7 */ \
    SHIFT(%%xmm0)                    /* xmm0 = op0 */

#define PUT_BLOCK(r0, r1, r2, r3, r4, r5, r6, r7) \
    "movdqa " #r0 ", " O(0) "\n\t" \
    "movdqa " #r1 ", " O(1) "\n\t" \
    "movdqa " #r2 ", " O(2) "\n\t" \
    "movdqa " #r3 ", " O(3) "\n\t" \
    "movdqa " #r4 ", " O(4) "\n\t" \
    "movdqa " #r5 ", " O(5) "\n\t" \
    "movdqa " #r6 ", " O(6) "\n\t" \
    "movdqa " #r7 ", " O(7) "\n\t"

#define NOP(xmm)
#define SHIFT4(xmm) "psraw  $4, "#xmm"\n\t"
#define ADD8(xmm)   "paddsw %2, "#xmm"\n\t"

void ff_vp3_idct_sse2(int16_t *input_data)
{
#define I(x) AV_STRINGIFY(16*x)"(%0)"
#define O(x) I(x)
#define C(x) AV_STRINGIFY(16*(x-1))"(%1)"

    __asm__ volatile (
        VP3_1D_IDCT_SSE2(NOP, NOP)

        TRANSPOSE8(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm6, %%xmm7, (%0))
        PUT_BLOCK(%%xmm0, %%xmm5, %%xmm7, %%xmm3, %%xmm6, %%xmm4, %%xmm2, %%xmm1)

        VP3_1D_IDCT_SSE2(ADD8, SHIFT4)
        PUT_BLOCK(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm6, %%xmm7)
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
