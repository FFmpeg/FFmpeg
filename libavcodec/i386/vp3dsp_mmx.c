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
 * @file vp3dsp_mmx.c
 * MMX-optimized functions cribbed from the original VP3 source code.
 */

#include "../dsputil.h"
#include "mmx.h"

#define IdctAdjustBeforeShift 8

/* (12 * 4) 2-byte memory locations ( = 96 bytes total)
 * idct_constants[0..15] = Mask table (M(I))
 * idct_constants[16..43] = Cosine table (C(I))
 * idct_constants[44..47] = 8
 */
static uint16_t idct_constants[(4 + 7 + 1) * 4];
static const uint16_t idct_cosine_table[7] = {
    64277, 60547, 54491, 46341, 36410, 25080, 12785
};

#define r0 mm0
#define r1 mm1
#define r2 mm2
#define r3 mm3
#define r4 mm4
#define r5 mm5
#define r6 mm6
#define r7 mm7

/* from original comments: The Macro does IDct on 4 1-D Dcts */
#define BeginIDCT() { \
    movq_m2r(*I(3), r2); \
    movq_m2r(*C(3), r6); \
    movq_r2r(r2, r4); \
    movq_m2r(*J(5), r7); \
    pmulhw_r2r(r6, r4);       /* r4 = c3*i3 - i3 */ \
    movq_m2r(*C(5), r1); \
    pmulhw_r2r(r7, r6);       /* r6 = c3*i5 - i5 */ \
    movq_r2r(r1, r5); \
    pmulhw_r2r(r2, r1);       /* r1 = c5*i3 - i3 */ \
    movq_m2r(*I(1), r3); \
    pmulhw_r2r(r7, r5);       /* r5 = c5*i5 - i5 */ \
    movq_m2r(*C(1), r0);      /* (all registers are in use) */ \
    paddw_r2r(r2, r4);        /* r4 = c3*i3 */ \
    paddw_r2r(r7, r6);        /* r6 = c3*i5 */ \
    paddw_r2r(r1, r2);        /* r2 = c5*i3 */ \
    movq_m2r(*J(7), r1); \
    paddw_r2r(r5, r7);        /* r7 = c5*i5 */ \
    movq_r2r(r0, r5);         /* r5 = c1 */ \
    pmulhw_r2r(r3, r0);       /* r0 = c1*i1 - i1 */ \
    paddsw_r2r(r7, r4);       /* r4 = C = c3*i3 + c5*i5 */ \
    pmulhw_r2r(r1, r5);       /* r5 = c1*i7 - i7 */ \
    movq_m2r(*C(7), r7); \
    psubsw_r2r(r2, r6);       /* r6 = D = c3*i5 - c5*i3 */ \
    paddw_r2r(r3, r0);        /* r0 = c1*i1 */ \
    pmulhw_r2r(r7, r3);       /* r3 = c7*i1 */ \
    movq_m2r(*I(2), r2); \
    pmulhw_r2r(r1, r7);       /* r7 = c7*i7 */ \
    paddw_r2r(r1, r5);        /* r5 = c1*i7 */ \
    movq_r2r(r2, r1);         /* r1 = i2 */ \
    pmulhw_m2r(*C(2), r2);    /* r2 = c2*i2 - i2 */ \
    psubsw_r2r(r5, r3);       /* r3 = B = c7*i1 - c1*i7 */ \
    movq_m2r(*J(6), r5); \
    paddsw_r2r(r7, r0);       /* r0 = A = c1*i1 + c7*i7 */ \
    movq_r2r(r5, r7);         /* r7 = i6 */ \
    psubsw_r2r(r4, r0);       /* r0 = A - C */ \
    pmulhw_m2r(*C(2), r5);    /* r5 = c2*i6 - i6 */ \
    paddw_r2r(r1, r2);        /* r2 = c2*i2 */ \
    pmulhw_m2r(*C(6), r1);    /* r1 = c6*i2 */ \
    paddsw_r2r(r4, r4);       /* r4 = C + C */ \
    paddsw_r2r(r0, r4);       /* r4 = C. = A + C */ \
    psubsw_r2r(r6, r3);       /* r3 = B - D */ \
    paddw_r2r(r7, r5);        /* r5 = c2*i6 */ \
    paddsw_r2r(r6, r6);       /* r6 = D + D */ \
    pmulhw_m2r(*C(6), r7);    /* r7 = c6*i6 */ \
    paddsw_r2r(r3, r6);       /* r6 = D. = B + D */ \
    movq_r2m(r4, *I(1));      /* save C. at I(1) */ \
    psubsw_r2r(r5, r1);       /* r1 = H = c6*i2 - c2*i6 */ \
    movq_m2r(*C(4), r4); \
    movq_r2r(r3, r5);         /* r5 = B - D */ \
    pmulhw_r2r(r4, r3);       /* r3 = (c4 - 1) * (B - D) */ \
    paddsw_r2r(r2, r7);       /* r7 = G = c6*i6 + c2*i2 */ \
    movq_r2m(r6, *I(2));      /* save D. at I(2) */ \
    movq_r2r(r0, r2);         /* r2 = A - C */ \
    movq_m2r(*I(0), r6); \
    pmulhw_r2r(r4, r0);       /* r0 = (c4 - 1) * (A - C) */ \
    paddw_r2r(r3, r5);        /* r5 = B. = c4 * (B - D) */ \
    movq_m2r(*J(4), r3); \
    psubsw_r2r(r1, r5);       /* r5 = B.. = B. - H */ \
    paddw_r2r(r0, r2);        /* r0 = A. = c4 * (A - C) */ \
    psubsw_r2r(r3, r6);       /* r6 = i0 - i4 */ \
    movq_r2r(r6, r0); \
    pmulhw_r2r(r4, r6);       /* r6 = (c4 - 1) * (i0 - i4) */ \
    paddsw_r2r(r3, r3);       /* r3 = i4 + i4 */ \
    paddsw_r2r(r1, r1);       /* r1 = H + H */ \
    paddsw_r2r(r0, r3);       /* r3 = i0 + i4 */ \
    paddsw_r2r(r5, r1);       /* r1 = H. = B + H */ \
    pmulhw_r2r(r3, r4);       /* r4 = (c4 - 1) * (i0 + i4) */ \
    paddsw_r2r(r0, r6);       /* r6 = F = c4 * (i0 - i4) */ \
    psubsw_r2r(r2, r6);       /* r6 = F. = F - A. */ \
    paddsw_r2r(r2, r2);       /* r2 = A. + A. */ \
    movq_m2r(*I(1), r0);      /* r0 = C. */ \
    paddsw_r2r(r6, r2);       /* r2 = A.. = F + A. */ \
    paddw_r2r(r3, r4);        /* r4 = E = c4 * (i0 + i4) */ \
    psubsw_r2r(r1, r2);       /* r2 = R2 = A.. - H. */ \
}

/* RowIDCT gets ready to transpose */
#define RowIDCT() { \
    \
    BeginIDCT(); \
    \
    movq_m2r(*I(2), r3);   /* r3 = D. */ \
    psubsw_r2r(r7, r4);    /* r4 = E. = E - G */ \
    paddsw_r2r(r1, r1);    /* r1 = H. + H. */ \
    paddsw_r2r(r7, r7);    /* r7 = G + G */ \
    paddsw_r2r(r2, r1);    /* r1 = R1 = A.. + H. */ \
    paddsw_r2r(r4, r7);    /* r7 = G. = E + G */ \
    psubsw_r2r(r3, r4);    /* r4 = R4 = E. - D. */ \
    paddsw_r2r(r3, r3); \
    psubsw_r2r(r5, r6);    /* r6 = R6 = F. - B.. */ \
    paddsw_r2r(r5, r5); \
    paddsw_r2r(r4, r3);    /* r3 = R3 = E. + D. */ \
    paddsw_r2r(r6, r5);    /* r5 = R5 = F. + B.. */ \
    psubsw_r2r(r0, r7);    /* r7 = R7 = G. - C. */ \
    paddsw_r2r(r0, r0); \
    movq_r2m(r1, *I(1));   /* save R1 */ \
    paddsw_r2r(r7, r0);    /* r0 = R0 = G. + C. */ \
}

/* Column IDCT normalizes and stores final results */
#define ColumnIDCT() { \
    \
    BeginIDCT(); \
    \
    paddsw_m2r(*Eight, r2);    /* adjust R2 (and R1) for shift */ \
    paddsw_r2r(r1, r1);        /* r1 = H. + H. */ \
    paddsw_r2r(r2, r1);        /* r1 = R1 = A.. + H. */ \
    psraw_i2r(4, r2);          /* r2 = NR2 */ \
    psubsw_r2r(r7, r4);        /* r4 = E. = E - G */ \
    psraw_i2r(4, r1);          /* r1 = NR1 */ \
    movq_m2r(*I(2), r3);       /* r3 = D. */ \
    paddsw_r2r(r7, r7);        /* r7 = G + G */ \
    movq_r2m(r2, *I(2));       /* store NR2 at I2 */ \
    paddsw_r2r(r4, r7);        /* r7 = G. = E + G */ \
    movq_r2m(r1, *I(1));       /* store NR1 at I1 */ \
    psubsw_r2r(r3, r4);        /* r4 = R4 = E. - D. */ \
    paddsw_m2r(*Eight, r4);    /* adjust R4 (and R3) for shift */ \
    paddsw_r2r(r3, r3);        /* r3 = D. + D. */ \
    paddsw_r2r(r4, r3);        /* r3 = R3 = E. + D. */ \
    psraw_i2r(4, r4);          /* r4 = NR4 */ \
    psubsw_r2r(r5, r6);        /* r6 = R6 = F. - B.. */ \
    psraw_i2r(4, r3);          /* r3 = NR3 */ \
    paddsw_m2r(*Eight, r6);    /* adjust R6 (and R5) for shift */ \
    paddsw_r2r(r5, r5);        /* r5 = B.. + B.. */ \
    paddsw_r2r(r6, r5);        /* r5 = R5 = F. + B.. */ \
    psraw_i2r(4, r6);          /* r6 = NR6 */ \
    movq_r2m(r4, *J(4));       /* store NR4 at J4 */ \
    psraw_i2r(4, r5);          /* r5 = NR5 */ \
    movq_r2m(r3, *I(3));       /* store NR3 at I3 */ \
    psubsw_r2r(r0, r7);        /* r7 = R7 = G. - C. */ \
    paddsw_m2r(*Eight, r7);    /* adjust R7 (and R0) for shift */ \
    paddsw_r2r(r0, r0);        /* r0 = C. + C. */ \
    paddsw_r2r(r7, r0);        /* r0 = R0 = G. + C. */ \
    psraw_i2r(4, r7);          /* r7 = NR7 */ \
    movq_r2m(r6, *J(6));       /* store NR6 at J6 */ \
    psraw_i2r(4, r0);          /* r0 = NR0 */ \
    movq_r2m(r5, *J(5));       /* store NR5 at J5 */ \
    movq_r2m(r7, *J(7));       /* store NR7 at J7 */ \
    movq_r2m(r0, *I(0));       /* store NR0 at I0 */ \
}

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

#define Transpose() { \
    movq_r2r(r4, r1);         /* r1 = e3 e2 e1 e0 */ \
    punpcklwd_r2r(r5, r4);    /* r4 = f1 e1 f0 e0 */ \
    movq_r2m(r0, *I(0));      /* save a3 a2 a1 a0 */ \
    punpckhwd_r2r(r5, r1);    /* r1 = f3 e3 f2 e2 */ \
    movq_r2r(r6, r0);         /* r0 = g3 g2 g1 g0 */ \
    punpcklwd_r2r(r7, r6);    /* r6 = h1 g1 h0 g0 */ \
    movq_r2r(r4, r5);         /* r5 = f1 e1 f0 e0 */ \
    punpckldq_r2r(r6, r4);    /* r4 = h0 g0 f0 e0 = R4 */ \
    punpckhdq_r2r(r6, r5);    /* r5 = h1 g1 f1 e1 = R5 */ \
    movq_r2r(r1, r6);         /* r6 = f3 e3 f2 e2 */ \
    movq_r2m(r4, *J(4)); \
    punpckhwd_r2r(r7, r0);    /* r0 = h3 g3 h2 g2 */ \
    movq_r2m(r5, *J(5)); \
    punpckhdq_r2r(r0, r6);    /* r6 = h3 g3 f3 e3 = R7 */ \
    movq_m2r(*I(0), r4);      /* r4 = a3 a2 a1 a0 */ \
    punpckldq_r2r(r0, r1);    /* r1 = h2 g2 f2 e2 = R6 */ \
    movq_m2r(*I(1), r5);      /* r5 = b3 b2 b1 b0 */ \
    movq_r2r(r4, r0);         /* r0 = a3 a2 a1 a0 */ \
    movq_r2m(r6, *J(7)); \
    punpcklwd_r2r(r5, r0);    /* r0 = b1 a1 b0 a0 */ \
    movq_r2m(r1, *J(6)); \
    punpckhwd_r2r(r5, r4);    /* r4 = b3 a3 b2 a2 */ \
    movq_r2r(r2, r5);         /* r5 = c3 c2 c1 c0 */ \
    punpcklwd_r2r(r3, r2);    /* r2 = d1 c1 d0 c0 */ \
    movq_r2r(r0, r1);         /* r1 = b1 a1 b0 a0 */ \
    punpckldq_r2r(r2, r0);    /* r0 = d0 c0 b0 a0 = R0 */ \
    punpckhdq_r2r(r2, r1);    /* r1 = d1 c1 b1 a1 = R1 */ \
    movq_r2r(r4, r2);         /* r2 = b3 a3 b2 a2 */ \
    movq_r2m(r0, *I(0)); \
    punpckhwd_r2r(r3, r5);    /* r5 = d3 c3 d2 c2 */ \
    movq_r2m(r1, *I(1)); \
    punpckhdq_r2r(r5, r4);    /* r4 = d3 c3 b3 a3 = R3 */ \
    punpckldq_r2r(r5, r2);    /* r2 = d2 c2 b2 a2 = R2 */ \
    movq_r2m(r4, *I(3)); \
    movq_r2m(r2, *I(2)); \
}

void ff_vp3_dsp_init_mmx(void)
{
    int j = 16;
    uint16_t *p;

    j = 1;
    do {
        p = idct_constants + ((j + 3) << 2);
        p[0] = p[1] = p[2] = p[3] = idct_cosine_table[j - 1];
    } while (++j <= 7);

    idct_constants[44] = idct_constants[45] =
    idct_constants[46] = idct_constants[47] = IdctAdjustBeforeShift;
}

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

#define C(x) (idct_constants + 16 + (x - 1) * 4)
#define Eight (idct_constants + 44)

    /* at this point, function has completed dequantization + dezigzag +
     * partial transposition; now do the idct itself */
#define I(K) (output_data + K * 8)
#define J(K) (output_data + ((K - 4) * 8) + 4)

    RowIDCT();
    Transpose();

#undef I
#undef J
#define I(K) (output_data + (K * 8) + 32)
#define J(K) (output_data + ((K - 4) * 8) + 36)

    RowIDCT();
    Transpose();

#undef I
#undef J
#define I(K) (output_data + K * 8)
#define J(K) (output_data + K * 8)

    ColumnIDCT();

#undef I
#undef J
#define I(K) (output_data + (K * 8) + 4)
#define J(K) (output_data + (K * 8) + 4)

    ColumnIDCT();

#undef I
#undef J

}
