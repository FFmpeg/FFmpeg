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
 * @file libavcodec/x86/vp3dsp_mmx.c
 * MMX-optimized functions cribbed from the original VP3 source code.
 */

#include "libavutil/x86_cpu.h"
#include "libavcodec/dsputil.h"
#include "dsputil_mmx.h"

extern const uint16_t ff_vp3_idct_data[];

// this is off by one or two for some cases when filter_limit is greater than 63
// in:  p0 in mm6, p1 in mm4, p2 in mm2, p3 in mm1
// out: p1 in mm4, p2 in mm3
#define VP3_LOOP_FILTER(flim) \
    "movq       %%mm6, %%mm7 \n\t" \
    "pand    "MANGLE(ff_pb_7 )", %%mm6 \n\t" /* p0&7 */ \
    "psrlw         $3, %%mm7 \n\t" \
    "pand    "MANGLE(ff_pb_1F)", %%mm7 \n\t" /* p0>>3 */ \
    "movq       %%mm2, %%mm3 \n\t" /* mm3 = p2 */ \
    "pxor       %%mm4, %%mm2 \n\t" \
    "pand    "MANGLE(ff_pb_1 )", %%mm2 \n\t" /* (p2^p1)&1 */ \
    "movq       %%mm2, %%mm5 \n\t" \
    "paddb      %%mm2, %%mm2 \n\t" \
    "paddb      %%mm5, %%mm2 \n\t" /* 3*(p2^p1)&1 */ \
    "paddb      %%mm6, %%mm2 \n\t" /* extra bits lost in shifts */ \
    "pcmpeqb    %%mm0, %%mm0 \n\t" \
    "pxor       %%mm0, %%mm1 \n\t" /* 255 - p3 */ \
    "pavgb      %%mm2, %%mm1 \n\t" /* (256 - p3 + extrabits) >> 1 */ \
    "pxor       %%mm4, %%mm0 \n\t" /* 255 - p1 */ \
    "pavgb      %%mm3, %%mm0 \n\t" /* (256 + p2-p1) >> 1 */ \
    "paddb   "MANGLE(ff_pb_3 )", %%mm1 \n\t" \
    "pavgb      %%mm0, %%mm1 \n\t" /* 128+2+(   p2-p1  - p3) >> 2 */ \
    "pavgb      %%mm0, %%mm1 \n\t" /* 128+1+(3*(p2-p1) - p3) >> 3 */ \
    "paddusb    %%mm1, %%mm7 \n\t" /* d+128+1 */ \
    "movq    "MANGLE(ff_pb_81)", %%mm6 \n\t" \
    "psubusb    %%mm7, %%mm6 \n\t" \
    "psubusb "MANGLE(ff_pb_81)", %%mm7 \n\t" \
\
    "movq     "#flim", %%mm5 \n\t" \
    "pminub     %%mm5, %%mm6 \n\t" \
    "pminub     %%mm5, %%mm7 \n\t" \
    "movq       %%mm6, %%mm0 \n\t" \
    "movq       %%mm7, %%mm1 \n\t" \
    "paddb      %%mm6, %%mm6 \n\t" \
    "paddb      %%mm7, %%mm7 \n\t" \
    "pminub     %%mm5, %%mm6 \n\t" \
    "pminub     %%mm5, %%mm7 \n\t" \
    "psubb      %%mm0, %%mm6 \n\t" \
    "psubb      %%mm1, %%mm7 \n\t" \
    "paddusb    %%mm7, %%mm4 \n\t" \
    "psubusb    %%mm6, %%mm4 \n\t" \
    "psubusb    %%mm7, %%mm3 \n\t" \
    "paddusb    %%mm6, %%mm3 \n\t"

#define STORE_4_WORDS(dst0, dst1, dst2, dst3, mm) \
    "movd "#mm", %0        \n\t" \
    "movw   %w0, -1"#dst0" \n\t" \
    "psrlq  $32, "#mm"     \n\t" \
    "shr    $16, %0        \n\t" \
    "movw   %w0, -1"#dst1" \n\t" \
    "movd "#mm", %0        \n\t" \
    "movw   %w0, -1"#dst2" \n\t" \
    "shr    $16, %0        \n\t" \
    "movw   %w0, -1"#dst3" \n\t"

void ff_vp3_v_loop_filter_mmx2(uint8_t *src, int stride, int *bounding_values)
{
    __asm__ volatile(
        "movq          %0, %%mm6 \n\t"
        "movq          %1, %%mm4 \n\t"
        "movq          %2, %%mm2 \n\t"
        "movq          %3, %%mm1 \n\t"

        VP3_LOOP_FILTER(%4)

        "movq       %%mm4, %1    \n\t"
        "movq       %%mm3, %2    \n\t"

        : "+m" (*(uint64_t*)(src - 2*stride)),
          "+m" (*(uint64_t*)(src - 1*stride)),
          "+m" (*(uint64_t*)(src + 0*stride)),
          "+m" (*(uint64_t*)(src + 1*stride))
        : "m"(*(uint64_t*)(bounding_values+129))
    );
}

void ff_vp3_h_loop_filter_mmx2(uint8_t *src, int stride, int *bounding_values)
{
    x86_reg tmp;

    __asm__ volatile(
        "movd -2(%1),      %%mm6 \n\t"
        "movd -2(%1,%3),   %%mm0 \n\t"
        "movd -2(%1,%3,2), %%mm1 \n\t"
        "movd -2(%1,%4),   %%mm4 \n\t"

        TRANSPOSE8x4(%%mm6, %%mm0, %%mm1, %%mm4, -2(%2), -2(%2,%3), -2(%2,%3,2), -2(%2,%4), %%mm2)
        VP3_LOOP_FILTER(%5)
        SBUTTERFLY(%%mm4, %%mm3, %%mm5, bw, q)

        STORE_4_WORDS((%1), (%1,%3), (%1,%3,2), (%1,%4), %%mm4)
        STORE_4_WORDS((%2), (%2,%3), (%2,%3,2), (%2,%4), %%mm5)

        : "=&r"(tmp)
        : "r"(src), "r"(src+4*stride), "r"((x86_reg)stride), "r"((x86_reg)3*stride),
          "m"(*(uint64_t*)(bounding_values+129))
        : "memory"
    );
}

/* from original comments: The Macro does IDct on 4 1-D Dcts */
#define BeginIDCT() \
    "movq   "I(3)", %%mm2 \n\t" \
    "movq   "C(3)", %%mm6 \n\t" \
    "movq    %%mm2, %%mm4 \n\t" \
    "movq   "J(5)", %%mm7 \n\t" \
    "pmulhw  %%mm6, %%mm4 \n\t"    /* r4 = c3*i3 - i3 */ \
    "movq   "C(5)", %%mm1 \n\t" \
    "pmulhw  %%mm7, %%mm6 \n\t"    /* r6 = c3*i5 - i5 */ \
    "movq    %%mm1, %%mm5 \n\t" \
    "pmulhw  %%mm2, %%mm1 \n\t"    /* r1 = c5*i3 - i3 */ \
    "movq   "I(1)", %%mm3 \n\t" \
    "pmulhw  %%mm7, %%mm5 \n\t"    /* r5 = c5*i5 - i5 */ \
    "movq   "C(1)", %%mm0 \n\t" \
    "paddw   %%mm2, %%mm4 \n\t"    /* r4 = c3*i3 */ \
    "paddw   %%mm7, %%mm6 \n\t"    /* r6 = c3*i5 */ \
    "paddw   %%mm1, %%mm2 \n\t"    /* r2 = c5*i3 */ \
    "movq   "J(7)", %%mm1 \n\t" \
    "paddw   %%mm5, %%mm7 \n\t"    /* r7 = c5*i5 */ \
    "movq    %%mm0, %%mm5 \n\t"    /* r5 = c1 */ \
    "pmulhw  %%mm3, %%mm0 \n\t"    /* r0 = c1*i1 - i1 */ \
    "paddsw  %%mm7, %%mm4 \n\t"    /* r4 = C = c3*i3 + c5*i5 */ \
    "pmulhw  %%mm1, %%mm5 \n\t"    /* r5 = c1*i7 - i7 */ \
    "movq   "C(7)", %%mm7 \n\t" \
    "psubsw  %%mm2, %%mm6 \n\t"    /* r6 = D = c3*i5 - c5*i3 */ \
    "paddw   %%mm3, %%mm0 \n\t"    /* r0 = c1*i1 */ \
    "pmulhw  %%mm7, %%mm3 \n\t"    /* r3 = c7*i1 */ \
    "movq   "I(2)", %%mm2 \n\t" \
    "pmulhw  %%mm1, %%mm7 \n\t"    /* r7 = c7*i7 */ \
    "paddw   %%mm1, %%mm5 \n\t"    /* r5 = c1*i7 */ \
    "movq    %%mm2, %%mm1 \n\t"    /* r1 = i2 */ \
    "pmulhw "C(2)", %%mm2 \n\t"    /* r2 = c2*i2 - i2 */ \
    "psubsw  %%mm5, %%mm3 \n\t"    /* r3 = B = c7*i1 - c1*i7 */ \
    "movq   "J(6)", %%mm5 \n\t" \
    "paddsw  %%mm7, %%mm0 \n\t"    /* r0 = A = c1*i1 + c7*i7 */ \
    "movq    %%mm5, %%mm7 \n\t"    /* r7 = i6 */ \
    "psubsw  %%mm4, %%mm0 \n\t"    /* r0 = A - C */ \
    "pmulhw "C(2)", %%mm5 \n\t"    /* r5 = c2*i6 - i6 */ \
    "paddw   %%mm1, %%mm2 \n\t"    /* r2 = c2*i2 */ \
    "pmulhw "C(6)", %%mm1 \n\t"    /* r1 = c6*i2 */ \
    "paddsw  %%mm4, %%mm4 \n\t"    /* r4 = C + C */ \
    "paddsw  %%mm0, %%mm4 \n\t"    /* r4 = C. = A + C */ \
    "psubsw  %%mm6, %%mm3 \n\t"    /* r3 = B - D */ \
    "paddw   %%mm7, %%mm5 \n\t"    /* r5 = c2*i6 */ \
    "paddsw  %%mm6, %%mm6 \n\t"    /* r6 = D + D */ \
    "pmulhw "C(6)", %%mm7 \n\t"    /* r7 = c6*i6 */ \
    "paddsw  %%mm3, %%mm6 \n\t"    /* r6 = D. = B + D */ \
    "movq    %%mm4, "I(1)"\n\t"    /* save C. at I(1) */ \
    "psubsw  %%mm5, %%mm1 \n\t"    /* r1 = H = c6*i2 - c2*i6 */ \
    "movq   "C(4)", %%mm4 \n\t" \
    "movq    %%mm3, %%mm5 \n\t"    /* r5 = B - D */ \
    "pmulhw  %%mm4, %%mm3 \n\t"    /* r3 = (c4 - 1) * (B - D) */ \
    "paddsw  %%mm2, %%mm7 \n\t"    /* r3 = (c4 - 1) * (B - D) */ \
    "movq    %%mm6, "I(2)"\n\t"    /* save D. at I(2) */ \
    "movq    %%mm0, %%mm2 \n\t"    /* r2 = A - C */ \
    "movq   "I(0)", %%mm6 \n\t" \
    "pmulhw  %%mm4, %%mm0 \n\t"    /* r0 = (c4 - 1) * (A - C) */ \
    "paddw   %%mm3, %%mm5 \n\t"    /* r5 = B. = c4 * (B - D) */ \
    "movq   "J(4)", %%mm3 \n\t" \
    "psubsw  %%mm1, %%mm5 \n\t"    /* r5 = B.. = B. - H */ \
    "paddw   %%mm0, %%mm2 \n\t"    /* r0 = A. = c4 * (A - C) */ \
    "psubsw  %%mm3, %%mm6 \n\t"    /* r6 = i0 - i4 */ \
    "movq    %%mm6, %%mm0 \n\t" \
    "pmulhw  %%mm4, %%mm6 \n\t"    /* r6 = (c4 - 1) * (i0 - i4) */ \
    "paddsw  %%mm3, %%mm3 \n\t"    /* r3 = i4 + i4 */ \
    "paddsw  %%mm1, %%mm1 \n\t"    /* r1 = H + H */ \
    "paddsw  %%mm0, %%mm3 \n\t"    /* r3 = i0 + i4 */ \
    "paddsw  %%mm5, %%mm1 \n\t"    /* r1 = H. = B + H */ \
    "pmulhw  %%mm3, %%mm4 \n\t"    /* r4 = (c4 - 1) * (i0 + i4) */ \
    "paddsw  %%mm0, %%mm6 \n\t"    /* r6 = F = c4 * (i0 - i4) */ \
    "psubsw  %%mm2, %%mm6 \n\t"    /* r6 = F. = F - A. */ \
    "paddsw  %%mm2, %%mm2 \n\t"    /* r2 = A. + A. */ \
    "movq   "I(1)", %%mm0 \n\t"    /* r0 = C. */ \
    "paddsw  %%mm6, %%mm2 \n\t"    /* r2 = A.. = F + A. */ \
    "paddw   %%mm3, %%mm4 \n\t"    /* r4 = E = c4 * (i0 + i4) */ \
    "psubsw  %%mm1, %%mm2 \n\t"    /* r2 = R2 = A.. - H. */

/* RowIDCT gets ready to transpose */
#define RowIDCT() \
    BeginIDCT() \
    "movq   "I(2)", %%mm3 \n\t"    /* r3 = D. */ \
    "psubsw  %%mm7, %%mm4 \n\t"    /* r4 = E. = E - G */ \
    "paddsw  %%mm1, %%mm1 \n\t"    /* r1 = H. + H. */ \
    "paddsw  %%mm7, %%mm7 \n\t"    /* r7 = G + G */ \
    "paddsw  %%mm2, %%mm1 \n\t"    /* r1 = R1 = A.. + H. */ \
    "paddsw  %%mm4, %%mm7 \n\t"    /* r1 = R1 = A.. + H. */ \
    "psubsw  %%mm3, %%mm4 \n\t"    /* r4 = R4 = E. - D. */ \
    "paddsw  %%mm3, %%mm3 \n\t" \
    "psubsw  %%mm5, %%mm6 \n\t"    /* r6 = R6 = F. - B.. */ \
    "paddsw  %%mm5, %%mm5 \n\t" \
    "paddsw  %%mm4, %%mm3 \n\t"    /* r3 = R3 = E. + D. */ \
    "paddsw  %%mm6, %%mm5 \n\t"    /* r5 = R5 = F. + B.. */ \
    "psubsw  %%mm0, %%mm7 \n\t"    /* r7 = R7 = G. - C. */ \
    "paddsw  %%mm0, %%mm0 \n\t" \
    "movq    %%mm1, "I(1)"\n\t"    /* save R1 */ \
    "paddsw  %%mm7, %%mm0 \n\t"    /* r0 = R0 = G. + C. */

/* Column IDCT normalizes and stores final results */
#define ColumnIDCT() \
    BeginIDCT() \
    "paddsw "OC_8", %%mm2 \n\t"    /* adjust R2 (and R1) for shift */ \
    "paddsw  %%mm1, %%mm1 \n\t"    /* r1 = H. + H. */ \
    "paddsw  %%mm2, %%mm1 \n\t"    /* r1 = R1 = A.. + H. */ \
    "psraw      $4, %%mm2 \n\t"    /* r2 = NR2 */ \
    "psubsw  %%mm7, %%mm4 \n\t"    /* r4 = E. = E - G */ \
    "psraw      $4, %%mm1 \n\t"    /* r1 = NR1 */ \
    "movq   "I(2)", %%mm3 \n\t"    /* r3 = D. */ \
    "paddsw  %%mm7, %%mm7 \n\t"    /* r7 = G + G */ \
    "movq    %%mm2, "I(2)"\n\t"    /* store NR2 at I2 */ \
    "paddsw  %%mm4, %%mm7 \n\t"    /* r7 = G. = E + G */ \
    "movq    %%mm1, "I(1)"\n\t"    /* store NR1 at I1 */ \
    "psubsw  %%mm3, %%mm4 \n\t"    /* r4 = R4 = E. - D. */ \
    "paddsw "OC_8", %%mm4 \n\t"    /* adjust R4 (and R3) for shift */ \
    "paddsw  %%mm3, %%mm3 \n\t"    /* r3 = D. + D. */ \
    "paddsw  %%mm4, %%mm3 \n\t"    /* r3 = R3 = E. + D. */ \
    "psraw      $4, %%mm4 \n\t"    /* r4 = NR4 */ \
    "psubsw  %%mm5, %%mm6 \n\t"    /* r6 = R6 = F. - B.. */ \
    "psraw      $4, %%mm3 \n\t"    /* r3 = NR3 */ \
    "paddsw "OC_8", %%mm6 \n\t"    /* adjust R6 (and R5) for shift */ \
    "paddsw  %%mm5, %%mm5 \n\t"    /* r5 = B.. + B.. */ \
    "paddsw  %%mm6, %%mm5 \n\t"    /* r5 = R5 = F. + B.. */ \
    "psraw      $4, %%mm6 \n\t"    /* r6 = NR6 */ \
    "movq    %%mm4, "J(4)"\n\t"    /* store NR4 at J4 */ \
    "psraw      $4, %%mm5 \n\t"    /* r5 = NR5 */ \
    "movq    %%mm3, "I(3)"\n\t"    /* store NR3 at I3 */ \
    "psubsw  %%mm0, %%mm7 \n\t"    /* r7 = R7 = G. - C. */ \
    "paddsw "OC_8", %%mm7 \n\t"    /* adjust R7 (and R0) for shift */ \
    "paddsw  %%mm0, %%mm0 \n\t"    /* r0 = C. + C. */ \
    "paddsw  %%mm7, %%mm0 \n\t"    /* r0 = R0 = G. + C. */ \
    "psraw      $4, %%mm7 \n\t"    /* r7 = NR7 */ \
    "movq    %%mm6, "J(6)"\n\t"    /* store NR6 at J6 */ \
    "psraw      $4, %%mm0 \n\t"    /* r0 = NR0 */ \
    "movq    %%mm5, "J(5)"\n\t"    /* store NR5 at J5 */ \
    "movq    %%mm7, "J(7)"\n\t"    /* store NR7 at J7 */ \
    "movq    %%mm0, "I(0)"\n\t"    /* store NR0 at I0 */

/* Following macro does two 4x4 transposes in place.

  At entry (we assume):

    r0 = a3 a2 a1 a0
    I(1) = b3 b2 b1 b0
    r2 = c3 c2 c1 c0
    r3 = d3 d2 d1 d0

    r4 = e3 e2 e1 e0
    r5 = f3 f2 f1 f0
    r6 = g3 g2 g1 g0
    r7 = h3 h2 h1 h0

  At exit, we have:

    I(0) = d0 c0 b0 a0
    I(1) = d1 c1 b1 a1
    I(2) = d2 c2 b2 a2
    I(3) = d3 c3 b3 a3

    J(4) = h0 g0 f0 e0
    J(5) = h1 g1 f1 e1
    J(6) = h2 g2 f2 e2
    J(7) = h3 g3 f3 e3

   I(0) I(1) I(2) I(3)  is the transpose of r0 I(1) r2 r3.
   J(4) J(5) J(6) J(7)  is the transpose of r4 r5 r6 r7.

   Since r1 is free at entry, we calculate the Js first. */
#define Transpose() \
    "movq       %%mm4, %%mm1 \n\t"    /* r1 = e3 e2 e1 e0 */ \
    "punpcklwd  %%mm5, %%mm4 \n\t"    /* r4 = f1 e1 f0 e0 */ \
    "movq       %%mm0, "I(0)"\n\t"    /* save a3 a2 a1 a0 */ \
    "punpckhwd  %%mm5, %%mm1 \n\t"    /* r1 = f3 e3 f2 e2 */ \
    "movq       %%mm6, %%mm0 \n\t"    /* r0 = g3 g2 g1 g0 */ \
    "punpcklwd  %%mm7, %%mm6 \n\t"    /* r6 = h1 g1 h0 g0 */ \
    "movq       %%mm4, %%mm5 \n\t"    /* r5 = f1 e1 f0 e0 */ \
    "punpckldq  %%mm6, %%mm4 \n\t"    /* r4 = h0 g0 f0 e0 = R4 */ \
    "punpckhdq  %%mm6, %%mm5 \n\t"    /* r5 = h1 g1 f1 e1 = R5 */ \
    "movq       %%mm1, %%mm6 \n\t"    /* r6 = f3 e3 f2 e2 */ \
    "movq       %%mm4, "J(4)"\n\t" \
    "punpckhwd  %%mm7, %%mm0 \n\t"    /* r0 = h3 g3 h2 g2 */ \
    "movq       %%mm5, "J(5)"\n\t" \
    "punpckhdq  %%mm0, %%mm6 \n\t"    /* r6 = h3 g3 f3 e3 = R7 */ \
    "movq      "I(0)", %%mm4 \n\t"    /* r4 = a3 a2 a1 a0 */ \
    "punpckldq  %%mm0, %%mm1 \n\t"    /* r1 = h2 g2 f2 e2 = R6 */ \
    "movq      "I(1)", %%mm5 \n\t"    /* r5 = b3 b2 b1 b0 */ \
    "movq       %%mm4, %%mm0 \n\t"    /* r0 = a3 a2 a1 a0 */ \
    "movq       %%mm6, "J(7)"\n\t" \
    "punpcklwd  %%mm5, %%mm0 \n\t"    /* r0 = b1 a1 b0 a0 */ \
    "movq       %%mm1, "J(6)"\n\t" \
    "punpckhwd  %%mm5, %%mm4 \n\t"    /* r4 = b3 a3 b2 a2 */ \
    "movq       %%mm2, %%mm5 \n\t"    /* r5 = c3 c2 c1 c0 */ \
    "punpcklwd  %%mm3, %%mm2 \n\t"    /* r2 = d1 c1 d0 c0 */ \
    "movq       %%mm0, %%mm1 \n\t"    /* r1 = b1 a1 b0 a0 */ \
    "punpckldq  %%mm2, %%mm0 \n\t"    /* r0 = d0 c0 b0 a0 = R0 */ \
    "punpckhdq  %%mm2, %%mm1 \n\t"    /* r1 = d1 c1 b1 a1 = R1 */ \
    "movq       %%mm4, %%mm2 \n\t"    /* r2 = b3 a3 b2 a2 */ \
    "movq       %%mm0, "I(0)"\n\t" \
    "punpckhwd  %%mm3, %%mm5 \n\t"    /* r5 = d3 c3 d2 c2 */ \
    "movq       %%mm1, "I(1)"\n\t" \
    "punpckhdq  %%mm5, %%mm4 \n\t"    /* r4 = d3 c3 b3 a3 = R3 */ \
    "punpckldq  %%mm5, %%mm2 \n\t"    /* r2 = d2 c2 b2 a2 = R2 */ \
    "movq       %%mm4, "I(3)"\n\t" \
    "movq       %%mm2, "I(2)"\n\t"

void ff_vp3_idct_mmx(int16_t *output_data)
{
    /* eax = quantized input
     * ebx = dequantizer matrix
     * ecx = IDCT constants
     *  M(I) = ecx + MaskOffset(0) + I * 8
     *  C(I) = ecx + CosineOffset(32) + (I-1) * 8
     * edx = output
     * r0..r7 = mm0..mm7
     */

#define C(x) AV_STRINGIFY(16*(x-1))"(%1)"
#define OC_8 "%2"

    /* at this point, function has completed dequantization + dezigzag +
     * partial transposition; now do the idct itself */
#define I(x) AV_STRINGIFY(16* x       )"(%0)"
#define J(x) AV_STRINGIFY(16*(x-4) + 8)"(%0)"

    __asm__ volatile (
        RowIDCT()
        Transpose()

#undef I
#undef J
#define I(x) AV_STRINGIFY(16* x    + 64)"(%0)"
#define J(x) AV_STRINGIFY(16*(x-4) + 72)"(%0)"

        RowIDCT()
        Transpose()

#undef I
#undef J
#define I(x) AV_STRINGIFY(16*x)"(%0)"
#define J(x) AV_STRINGIFY(16*x)"(%0)"

        ColumnIDCT()

#undef I
#undef J
#define I(x) AV_STRINGIFY(16*x + 8)"(%0)"
#define J(x) AV_STRINGIFY(16*x + 8)"(%0)"

        ColumnIDCT()
        :: "r"(output_data), "r"(ff_vp3_idct_data), "m"(ff_pw_8)
    );
#undef I
#undef J

}

void ff_vp3_idct_put_mmx(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_vp3_idct_mmx(block);
    put_signed_pixels_clamped_mmx(block, dest, line_size);
}

void ff_vp3_idct_add_mmx(uint8_t *dest, int line_size, DCTELEM *block)
{
    ff_vp3_idct_mmx(block);
    add_pixels_clamped_mmx(block, dest, line_size);
}
