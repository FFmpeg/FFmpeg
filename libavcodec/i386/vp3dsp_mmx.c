/*
 * Copyright (C) 2004 the ffmpeg project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
static uint16_t idct_cosine_table[7] = {
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

void vp3_dsp_init_mmx(void)
{
    int j = 16;
    uint16_t *p;

    do {
        idct_constants[--j] = 0;
    } while (j);

    idct_constants[0]  = idct_constants[5] =
    idct_constants[10] = idct_constants[15] = 65535;

    j = 1;
    do {
        p = idct_constants + ((j + 3) << 2);
        p[0] = p[1] = p[2] = p[3] = idct_cosine_table[j - 1];
    } while (++j <= 7);

    idct_constants[44] = idct_constants[45] =
    idct_constants[46] = idct_constants[47] = IdctAdjustBeforeShift;
}

void vp3_idct_mmx(int16_t *input_data, int16_t *dequant_matrix,
    int coeff_count, int16_t *output_data)
{
    /* eax = quantized input
     * ebx = dequantizer matrix
     * ecx = IDCT constants
     *  M(I) = ecx + MaskOffset(0) + I * 8
     *  C(I) = ecx + CosineOffset(32) + (I-1) * 8
     * edx = output
     * r0..r7 = mm0..mm7
     */

#define M(x) (idct_constants + x * 4)
#define C(x) (idct_constants + 16 + (x - 1) * 4)
#define Eight (idct_constants + 44)

    unsigned char *input_bytes = (unsigned char *)input_data;
    unsigned char *dequant_matrix_bytes = (unsigned char *)dequant_matrix;
    unsigned char *output_data_bytes = (unsigned char *)output_data;

    movq_m2r(*(input_bytes), r0);
    pmullw_m2r(*(dequant_matrix_bytes), r0);       /* r0 = 03 02 01 00 */
    movq_m2r(*(input_bytes+16), r1);
    pmullw_m2r(*(dequant_matrix_bytes+16), r1);    /* r1 = 13 12 11 10 */
    movq_m2r(*M(0), r2);                           /* r2 = __ __ __ FF */
    movq_r2r(r0, r3);                              /* r3 = 03 02 01 00 */
    movq_m2r(*(input_bytes+8), r4);
    psrlq_i2r(16, r0);                             /* r0 = __ 03 02 01 */
    pmullw_m2r(*(dequant_matrix_bytes+8), r4);     /* r4 = 07 06 05 04 */
    pand_r2r(r2, r3);                              /* r3 = __ __ __ 00 */
    movq_r2r(r0, r5);                              /* r5 = __ 03 02 01 */
    movq_r2r(r1, r6);                              /* r6 = 13 12 11 10 */
    pand_r2r(r2, r5);                              /* r5 = __ __ __ 01 */
    psllq_i2r(32, r6);                             /* r6 = 11 10 __ __ */
    movq_m2r(*M(3), r7);                           /* r7 = FF __ __ __ */
    pxor_r2r(r5, r0);                              /* r0 = __ 03 02 __ */
    pand_r2r(r6, r7);                              /* r7 = 11 __ __ __ */
    por_r2r(r3, r0);                               /* r0 = __ 03 02 00 */
    pxor_r2r(r7, r6);                              /* r6 = __ 10 __ __ */
    por_r2r(r7, r0);                               /* r0 = 11 03 02 00 = R0 */
    movq_m2r(*M(3), r7);                           /* r7 = FF __ __ __ */
    movq_r2r(r4, r3);                              /* r3 = 07 06 05 04 */
    movq_r2m(r0, *(output_data_bytes));            /* write R0 = r0 */
    pand_r2r(r2, r3);                              /* r3 = __ __ __ 04 */
    movq_m2r(*(input_bytes+32), r0);
    psllq_i2r(16, r3);                             /* r3 = __ __ 04 __ */
    pmullw_m2r(*(dequant_matrix_bytes+32), r0);    /* r0 = 23 22 21 20 */
    pand_r2r(r1, r7);                              /* r7 = 13 __ __ __ */
    por_r2r(r3, r5);                               /* r5 = __ __ 04 01 */
    por_r2r(r6, r7);                               /* r7 = 13 10 __ __ */
    movq_m2r(*(input_bytes+24), r3);
    por_r2r(r5, r7);                               /* r7 = 13 10 04 01 = R1 */
    pmullw_m2r(*(dequant_matrix_bytes+24), r3);    /* r3 = 17 16 15 14 */
    psrlq_i2r(16, r4);                             /* r4 = __ 07 06 05 */
    movq_r2m(r7, *(output_data_bytes+16));         /* write R1 = r7 */
    movq_r2r(r4, r5);                              /* r5 = __ 07 06 05 */
    movq_r2r(r0, r7);                              /* r7 = 23 22 21 20 */
    psrlq_i2r(16, r4);                             /* r4 = __ __ 07 06 */
    psrlq_i2r(48, r7);                             /* r7 = __ __ __ 23 */
    movq_r2r(r2, r6);                              /* r6 = __ __ __ FF */
    pand_r2r(r2, r5);                              /* r5 = __ __ __ 05 */
    pand_r2r(r4, r6);                              /* r6 = __ __ __ 06 */
    movq_r2m(r7, *(output_data_bytes+80));      /* partial R9 = __ __ __ 23 */
    pxor_r2r(r6, r4);                              /* r4 = __ __ 07 __ */
    psrlq_i2r(32, r1);                             /* r1 = __ __ 13 12 */
    por_r2r(r5, r4);                               /* r4 = __ __ 07 05 */
    movq_m2r(*M(3), r7);                           /* r7 = FF __ __ __ */
    pand_r2r(r2, r1);                              /* r1 = __ __ __ 12 */
    movq_m2r(*(input_bytes+48), r5);
    psllq_i2r(16, r0);                             /* r0 = 22 21 20 __ */
    pmullw_m2r(*(dequant_matrix_bytes+48), r5);    /* r5 = 33 32 31 30 */
    pand_r2r(r0, r7);                              /* r7 = 22 __ __ __ */
    movq_r2m(r1, *(output_data_bytes+64));      /* partial R8 = __ __ __ 12 */
    por_r2r(r4, r7);                               /* r7 = 22 __ 07 05 */
    movq_r2r(r3, r4);                              /* r4 = 17 16 15 14 */
    pand_r2r(r2, r3);                              /* r3 = __ __ __ 14 */
    movq_m2r(*M(2), r1);                           /* r1 = __ FF __ __ */
    psllq_i2r(32, r3);                             /* r3 = __ 14 __ __ */
    por_r2r(r3, r7);                               /* r7 = 22 14 07 05 = R2 */
    movq_r2r(r5, r3);                              /* r3 = 33 32 31 30 */
    psllq_i2r(48, r3);                             /* r3 = 30 __ __ __ */
    pand_r2r(r0, r1);                              /* r1 = __ 21 __ __ */
    movq_r2m(r7, *(output_data_bytes+32));         /* write R2 = r7 */
    por_r2r(r3, r6);                               /* r6 = 30 __ __ 06 */
    movq_m2r(*M(1), r7);                           /* r7 = __ __ FF __ */
    por_r2r(r1, r6);                               /* r6 = 30 21 __ 06 */
    movq_m2r(*(input_bytes+56), r1);
    pand_r2r(r4, r7);                              /* r7 = __ __ 15 __ */
    pmullw_m2r(*(dequant_matrix_bytes+56), r1);    /* r1 = 37 36 35 34 */
    por_r2r(r6, r7);                               /* r7 = 30 21 15 06 = R3 */
    pand_m2r(*M(1), r0);                           /* r0 = __ __ 20 __ */
    psrlq_i2r(32, r4);                             /* r4 = __ __ 17 16 */
    movq_r2m(r7, *(output_data_bytes+48));         /* write R3 = r7 */
    movq_r2r(r4, r6);                              /* r6 = __ __ 17 16 */
    movq_m2r(*M(3), r7);                           /* r7 = FF __ __ __ */
    pand_r2r(r2, r4);                              /* r4 = __ __ __ 16 */
    movq_m2r(*M(1), r3);                           /* r3 = __ __ FF __ */
    pand_r2r(r1, r7);                              /* r7 = 37 __ __ __ */
    pand_r2r(r5, r3);                              /* r3 = __ __ 31 __ */
    por_r2r(r4, r0);                               /* r0 = __ __ 20 16 */
    psllq_i2r(16, r3);                             /* r3 = __ 31 __ __ */
    por_r2r(r0, r7);                               /* r7 = 37 __ 20 16 */
    movq_m2r(*M(2), r4);                           /* r4 = __ FF __ __ */
    por_r2r(r3, r7);                               /* r7 = 37 31 20 16 = R4 */
    movq_m2r(*(input_bytes+80), r0);
    movq_r2r(r4, r3);                              /* r3 = __ __ FF __ */
    pmullw_m2r(*(dequant_matrix_bytes+80), r0);    /* r0 = 53 52 51 50 */
    pand_r2r(r5, r4);                              /* r4 = __ 32 __ __ */
    movq_r2m(r7, *(output_data_bytes+8));          /* write R4 = r7 */
    por_r2r(r4, r6);                               /* r6 = __ 32 17 16 */
    movq_r2r(r3, r4);                              /* r4 = __ FF __ __ */
    psrlq_i2r(16, r6);                             /* r6 = __ __ 32 17 */
    movq_r2r(r0, r7);                              /* r7 = 53 52 51 50 */
    pand_r2r(r1, r4);                              /* r4 = __ 36 __ __ */
    psllq_i2r(48, r7);                             /* r7 = 50 __ __ __ */
    por_r2r(r4, r6);                               /* r6 = __ 36 32 17 */
    movq_m2r(*(input_bytes+88), r4);
    por_r2r(r6, r7);                               /* r7 = 50 36 32 17 = R5 */
    pmullw_m2r(*(dequant_matrix_bytes+88), r4);    /* r4 = 57 56 55 54 */
    psrlq_i2r(16, r3);                             /* r3 = __ __ FF __ */
    movq_r2m(r7, *(output_data_bytes+24));         /* write R5 = r7 */
    pand_r2r(r1, r3);                              /* r3 = __ __ 35 __ */
    psrlq_i2r(48, r5);                             /* r5 = __ __ __ 33 */
    pand_r2r(r2, r1);                              /* r1 = __ __ __ 34 */
    movq_m2r(*(input_bytes+104), r6);
    por_r2r(r3, r5);                               /* r5 = __ __ 35 33 */
    pmullw_m2r(*(dequant_matrix_bytes+104), r6);   /* r6 = 67 66 65 64 */
    psrlq_i2r(16, r0);                             /* r0 = __ 53 52 51 */
    movq_r2r(r4, r7);                              /* r7 = 57 56 55 54 */
    movq_r2r(r2, r3);                              /* r3 = __ __ __ FF */
    psllq_i2r(48, r7);                             /* r7 = 54 __ __ __ */
    pand_r2r(r0, r3);                              /* r3 = __ __ __ 51 */
    pxor_r2r(r3, r0);                              /* r0 = __ 53 52 __ */
    psllq_i2r(32, r3);                             /* r3 = __ 51 __ __ */
    por_r2r(r5, r7);                               /* r7 = 54 __ 35 33 */
    movq_r2r(r6, r5);                              /* r5 = 67 66 65 64 */
    pand_m2r(*M(1), r6);                           /* r6 = __ __ 65 __ */
    por_r2r(r3, r7);                               /* r7 = 54 51 35 33 = R6 */
    psllq_i2r(32, r6);                             /* r6 = 65 __ __ __ */
    por_r2r(r1, r0);                               /* r0 = __ 53 52 34 */
    movq_r2m(r7, *(output_data_bytes+40));         /* write R6 = r7 */
    por_r2r(r6, r0);                               /* r0 = 65 53 52 34 = R7 */
    movq_m2r(*(input_bytes+120), r7);
    movq_r2r(r5, r6);                              /* r6 = 67 66 65 64 */
    pmullw_m2r(*(dequant_matrix_bytes+120), r7);   /* r7 = 77 76 75 74 */
    psrlq_i2r(32, r5);                             /* r5 = __ __ 67 66 */
    pand_r2r(r2, r6);                              /* r6 = __ __ __ 64 */
    movq_r2r(r5, r1);                              /* r1 = __ __ 67 66 */
    movq_r2m(r0, *(output_data_bytes+56));         /* write R7 = r0 */
    pand_r2r(r2, r1);                              /* r1 = __ __ __ 66 */
    movq_m2r(*(input_bytes+112), r0);
    movq_r2r(r7, r3);                              /* r3 = 77 76 75 74 */
    pmullw_m2r(*(dequant_matrix_bytes+112), r0);   /* r0 = 73 72 71 70 */
    psllq_i2r(16, r3);                             /* r3 = 76 75 74 __ */
    pand_m2r(*M(3), r7);                           /* r7 = 77 __ __ __ */
    pxor_r2r(r1, r5);                              /* r5 = __ __ 67 __ */
    por_r2r(r5, r6);                               /* r6 = __ __ 67 64 */
    movq_r2r(r3, r5);                              /* r5 = 76 75 74 __ */
    pand_m2r(*M(3), r5);                           /* r5 = 76 __ __ __ */
    por_r2r(r1, r7);                               /* r7 = 77 __ __ 66 */
    movq_m2r(*(input_bytes+96), r1);
    pxor_r2r(r5, r3);                              /* r3 = __ 75 74 __ */
    pmullw_m2r(*(dequant_matrix_bytes+96), r1);    /* r1 = 63 62 61 60 */
    por_r2r(r3, r7);                               /* r7 = 77 75 74 66 = R15 */
    por_r2r(r5, r6);                               /* r6 = 76 __ 67 64 */
    movq_r2r(r0, r5);                              /* r5 = 73 72 71 70 */
    movq_r2m(r7, *(output_data_bytes+120));        /* store R15 = r7 */
    psrlq_i2r(16, r5);                             /* r5 = __ 73 72 71 */
    pand_m2r(*M(2), r5);                           /* r5 = __ 73 __ __ */
    movq_r2r(r0, r7);                              /* r7 = 73 72 71 70 */
    por_r2r(r5, r6);                               /* r6 = 76 73 67 64 = R14 */
    pand_r2r(r2, r0);                              /* r0 = __ __ __ 70 */
    pxor_r2r(r0, r7);                              /* r7 = 73 72 71 __ */
    psllq_i2r(32, r0);                             /* r0 = __ 70 __ __ */
    movq_r2m(r6, *(output_data_bytes+104));        /* write R14 = r6 */
    psrlq_i2r(16, r4);                             /* r4 = __ 57 56 55 */
    movq_m2r(*(input_bytes+72), r5);
    psllq_i2r(16, r7);                             /* r7 = 72 71 __ __ */
    pmullw_m2r(*(dequant_matrix_bytes+72), r5);    /* r5 = 47 46 45 44 */
    movq_r2r(r7, r6);                              /* r6 = 72 71 __ __ */
    movq_m2r(*M(2), r3);                           /* r3 = __ FF __ __ */
    psllq_i2r(16, r6);                             /* r6 = 71 __ __ __ */
    pand_m2r(*M(3), r7);                           /* r7 = 72 __ __ __ */
    pand_r2r(r1, r3);                              /* r3 = __ 62 __ __ */
    por_r2r(r0, r7);                               /* r7 = 72 70 __ __ */
    movq_r2r(r1, r0);                              /* r0 = 63 62 61 60 */
    pand_m2r(*M(3), r1);                           /* r1 = 63 __ __ __ */
    por_r2r(r3, r6);                               /* r6 = 71 62 __ __ */
    movq_r2r(r4, r3);                              /* r3 = __ 57 56 55 */
    psrlq_i2r(32, r1);                             /* r1 = __ __ 63 __ */
    pand_r2r(r2, r3);                              /* r3 = __ __ __ 55 */
    por_r2r(r1, r7);                               /* r7 = 72 70 63 __ */
    por_r2r(r3, r7);                               /* r7 = 72 70 63 55 = R13 */
    movq_r2r(r4, r3);                              /* r3 = __ 57 56 55 */
    pand_m2r(*M(1), r3);                           /* r3 = __ __ 56 __ */
    movq_r2r(r5, r1);                              /* r1 = 47 46 45 44 */
    movq_r2m(r7, *(output_data_bytes+88));         /* write R13 = r7 */
    psrlq_i2r(48, r5);                             /* r5 = __ __ __ 47 */
    movq_m2r(*(input_bytes+64), r7);
    por_r2r(r3, r6);                               /* r6 = 71 62 56 __ */
    pmullw_m2r(*(dequant_matrix_bytes+64), r7);    /* r7 = 43 42 41 40 */
    por_r2r(r5, r6);                               /* r6 = 71 62 56 47 = R12 */
    pand_m2r(*M(2), r4);                           /* r4 = __ 57 __ __ */
    psllq_i2r(32, r0);                             /* r0 = 61 60 __ __ */
    movq_r2m(r6, *(output_data_bytes+72));         /* write R12 = r6 */
    movq_r2r(r0, r6);                              /* r6 = 61 60 __ __ */
    pand_m2r(*M(3), r0);                           /* r0 = 61 __ __ __ */
    psllq_i2r(16, r6);                             /* r6 = 60 __ __ __ */
    movq_m2r(*(input_bytes+40), r5);
    movq_r2r(r1, r3);                              /* r3 = 47 46 45 44 */
    pmullw_m2r(*(dequant_matrix_bytes+40), r5);    /* r5 = 27 26 25 24 */
    psrlq_i2r(16, r1);                             /* r1 = __ 47 46 45 */
    pand_m2r(*M(1), r1);                           /* r1 = __ __ 46 __ */
    por_r2r(r4, r0);                               /* r0 = 61 57 __ __ */
    pand_r2r(r7, r2);                              /* r2 = __ __ __ 40 */
    por_r2r(r1, r0);                               /* r0 = 61 57 46 __ */
    por_r2r(r2, r0);                               /* r0 = 61 57 46 40 = R11 */
    psllq_i2r(16, r3);                             /* r3 = 46 45 44 __ */
    movq_r2r(r3, r4);                              /* r4 = 46 45 44 __ */
    movq_r2r(r5, r2);                              /* r2 = 27 26 25 24 */
    movq_r2m(r0, *(output_data_bytes+112));        /* write R11 = r0 */
    psrlq_i2r(48, r2);                             /* r2 = __ __ __ 27 */
    pand_m2r(*M(2), r4);                           /* r4 = __ 45 __ __ */
    por_r2r(r2, r6);                               /* r6 = 60 __ __ 27 */
    movq_m2r(*M(1), r2);                           /* r2 = __ __ FF __ */
    por_r2r(r4, r6);                               /* r6 = 60 45 __ 27 */
    pand_r2r(r7, r2);                              /* r2 = __ __ 41 __ */
    psllq_i2r(32, r3);                             /* r3 = 44 __ __ __ */
    por_m2r(*(output_data_bytes+80), r3);          /* r3 = 44 __ __ 23 */
    por_r2r(r2, r6);                               /* r6 = 60 45 41 27 = R10 */
    movq_m2r(*M(3), r2);                           /* r2 = FF __ __ __ */
    psllq_i2r(16, r5);                             /* r5 = 26 25 24 __ */
    movq_r2m(r6, *(output_data_bytes+96));         /* store R10 = r6 */
    pand_r2r(r5, r2);                              /* r2 = 26 __ __ __ */
    movq_m2r(*M(2), r6);                           /* r6 = __ FF __ __ */
    pxor_r2r(r2, r5);                              /* r5 = __ 25 24 __ */
    pand_r2r(r7, r6);                              /* r6 = __ 42 __ __ */
    psrlq_i2r(32, r2);                             /* r2 = __ __ 26 __ */
    pand_m2r(*M(3), r7);                           /* r7 = 43 __ __ __ */
    por_r2r(r2, r3);                               /* r3 = 44 __ 26 23 */
    por_m2r(*(output_data_bytes+64), r7);          /* r7 = 43 __ __ 12 */
    por_r2r(r3, r6);                               /* r6 = 44 42 26 23 = R9 */
    por_r2r(r5, r7);                               /* r7 = 43 25 24 12 = R8 */
    movq_r2m(r6, *(output_data_bytes+80));         /* store R9 = r6 */
    movq_r2m(r7, *(output_data_bytes+64));         /* store R8 = r7 */


#undef M

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
