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
#define BeginIDCT() \
    movq_m2r(*I(3), r2); \
    movq_m2r(*C(3), r6); \
    movq_r2r(r2, r4); \
    movq_m2r(*J(5), r7); \
    pmulhw_r2r(r6, r4); \
    movq_m2r(*C(5), r1); \
    pmulhw_r2r(r7, r6); \
    movq_r2r(r1, r5); \
    pmulhw_r2r(r2, r1); \
    movq_m2r(*I(1), r3); \
    pmulhw_r2r(r7, r5); \
    movq_m2r(*C(1), r0); \
    paddw_r2r(r2, r4); \
    paddw_r2r(r7, r6); \
    paddw_r2r(r1, r2); \
    movq_m2r(*J(7), r1); \
    paddw_r2r(r5, r7); \
    movq_r2r(r0, r5); \
    pmulhw_r2r(r3, r0); \
    paddsw_r2r(r7, r4); \
    pmulhw_r2r(r1, r5); \
    movq_m2r(*C(7), r7); \
    psubsw_r2r(r2, r6); \
    paddw_r2r(r3, r0); \
    pmulhw_r2r(r7, r3); \
    movq_m2r(*I(2), r2); \
    pmulhw_r2r(r1, r7); \
    paddw_r2r(r1, r5); \
    movq_r2r(r2, r1); \
    pmulhw_m2r(*C(2), r2); \
    psubsw_r2r(r5, r3); \
    movq_m2r(*J(6), r5); \
    paddsw_r2r(r7, r0); \
    movq_r2r(r5, r7); \
    psubsw_r2r(r4, r0); \
    pmulhw_m2r(*C(2), r5); \
    paddw_r2r(r1, r2); \
    pmulhw_m2r(*C(6), r1); \
    paddsw_r2r(r4, r4); \
    paddsw_r2r(r0, r4); \
    psubsw_r2r(r6, r3); \
    paddw_r2r(r7, r5); \
    paddsw_r2r(r6, r6); \
    pmulhw_m2r(*C(6), r7); \
    paddsw_r2r(r3, r6); \
    movq_r2m(r4, *I(1)); \
    psubsw_r2r(r5, r1); \
    movq_m2r(*C(4), r4); \
    movq_r2r(r3, r5); \
    pmulhw_r2r(r4, r3); \
    paddsw_r2r(r2, r7); \
    movq_r2m(r6, *I(2)); \
    movq_r2r(r0, r2); \
    movq_m2r(*I(0), r6); \
    pmulhw_r2r(r4, r0); \
    paddw_r2r(r3, r5); \
    movq_m2r(*J(4), r3); \
    psubsw_r2r(r1, r5); \
    paddw_r2r(r0, r2); \
    psubsw_r2r(r3, r6); \
    movq_r2r(r6, r0); \
    pmulhw_r2r(r4, r6); \
    paddsw_r2r(r3, r3); \
    paddsw_r2r(r1, r1); \
    paddsw_r2r(r0, r3); \
    paddsw_r2r(r5, r1); \
    pmulhw_r2r(r3, r4); \
    paddsw_r2r(r0, r6); \
    psubsw_r2r(r2, r6); \
    paddsw_r2r(r2, r2); \
    movq_m2r(*I(1), r0); \
    paddsw_r2r(r6, r2); \
    paddw_r2r(r3, r4); \
    psubsw_r2r(r1, r2);

/* RowIDCT gets ready to transpose */
#define RowIDCT() \
    \
    BeginIDCT() \
    \
    movq_m2r(*I(2), r3); \
    psubsw_r2r(r7, r4); \
    paddsw_r2r(r1, r1); \
    paddsw_r2r(r7, r7); \
    paddsw_r2r(r2, r1); \
    paddsw_r2r(r4, r7); \
    psubsw_r2r(r3, r4); \
    psubsw_r2r(r5, r6); \
    paddsw_r2r(r5, r5); \
    paddsw_r2r(r4, r3); \
    paddsw_r2r(r6, r5); \
    psubsw_r2r(r0, r7); \
    paddsw_r2r(r0, r0); \
    movq_r2m(r1, *I(1)); \
    paddsw_r2r(r7, r0);

/* Column IDCT normalizes and stores final results */
#define ColumnIDCT() \
    \
    BeginIDCT() \
    \
    paddsw_m2r(*Eight, r2); \
    paddsw_r2r(r1, r1); \
    paddsw_r2r(r2, r1); \
    psraw_i2r(4, r2); \
    psubsw_r2r(r7, r4); \
    psraw_i2r(4, r1); \
    movq_m2r(*I(2), r3); \
    paddsw_r2r(r7, r7); \
    movq_r2m(r2, *I(2)); \
    paddsw_r2r(r4, r7); \
    movq_r2m(r1, *I(1)); \
    psubsw_r2r(r3, r4); \
    paddsw_m2r(*Eight, r4); \
    paddsw_r2r(r3, r3); \
    paddsw_r2r(r4, r3); \
    psraw_i2r(4, r4); \
    psubsw_r2r(r5, r6); \
    psraw_i2r(4, r3); \
    paddsw_m2r(*Eight, r6); \
    paddsw_r2r(r5, r5); \
    paddsw_r2r(r6, r5); \
    psraw_i2r(4, r6); \
    movq_r2m(r4, *J(4)); \
    psraw_i2r(4, r5); \
    movq_r2m(r3, *I(3)); \
    psubsw_r2r(r0, r7); \
    paddsw_m2r(*Eight, r7); \
    paddsw_r2r(r0, r0); \
    paddsw_r2r(r7, r0); \
    psraw_i2r(4, r7); \
    movq_r2m(r6, *J(6)); \
    psraw_i2r(4, r0); \
    movq_r2m(r5, *J(5)); \
    movq_r2m(r7, *J(7)); \
    movq_r2m(r0, *I(0));


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
    movq_r2r(r4, r1); \
    punpcklwd_r2r(r5, r4); \
    movq_r2m(r0, *I(0)); \
    punpckhwd_r2r(r5, r1); \
    movq_r2r(r6, r0); \
    punpcklwd_r2r(r7, r6); \
    movq_r2r(r4, r5); \
    punpckldq_r2r(r6, r4); \
    punpckhdq_r2r(r6, r5); \
    movq_r2r(r1, r6); \
    movq_r2m(r4, *J(4)); \
    punpckhwd_r2r(r7, r0); \
    movq_r2m(r5, *J(5)); \
    punpckhdq_r2r(r0, r6); \
    movq_m2r(*I(0), r4); \
    punpckldq_r2r(r0, r1); \
    movq_m2r(*I(1), r5); \
    movq_r2r(r4, r0); \
    movq_r2m(r6, *J(7)); \
    punpcklwd_r2r(r5, r0); \
    movq_r2m(r1, *J(6)); \
    punpckhwd_r2r(r5, r4); \
    movq_r2r(r2, r5); \
    punpcklwd_r2r(r3, r2); \
    movq_r2r(r0, r1); \
    punpckldq_r2r(r2, r0); \
    punpckhdq_r2r(r2, r1); \
    movq_r2r(r4, r2); \
    movq_r2m(r0, *I(0)); \
    punpckhwd_r2r(r3, r5); \
    movq_r2m(r1, *I(1)); \
    punpckhdq_r2r(r5, r4); \
    punpckldq_r2r(r5, r2); \
    movq_r2m(r4, *I(3)); \
    movq_r2m(r2, *I(2));


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

static void vp3_idct_mmx(int16_t *input_data, int16_t *dequant_matrix,
     int16_t *output_data)
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

    movq_m2r(*input_data, r0);
    pmullw_m2r(*dequant_matrix, r0);
    movq_m2r(*(input_data + 8), r1);
    pmullw_m2r(*(dequant_matrix + 8), r1);
    movq_m2r(*M(0), r2);
    movq_r2r(r0, r3);
    movq_m2r(*(input_data + 4), r4);
    psrlq_i2r(16, r0);
    pmullw_m2r(*(dequant_matrix + 4), r4);
    pand_r2r(r2, r3);
    movq_r2r(r0, r5);
    movq_r2r(r1, r6);
    pand_r2r(r2, r5);
    psllq_i2r(32, r6);
    movq_m2r(*M(3), r7);
    pxor_r2r(r5, r0);
    pand_r2r(r6, r7);
    por_r2r(r3, r0);
    pxor_r2r(r7, r6);
    por_r2r(r7, r0);
    movq_m2r(*M(3), r7);
    movq_r2r(r4, r3);
    movq_r2m(r0, *output_data);

    pand_r2r(r2, r3);
    movq_m2r(*(input_data + 16), r0);
    psllq_i2r(16, r3);
    pmullw_m2r(*(dequant_matrix + 16), r0);
    pand_r2r(r1, r7);
    por_r2r(r3, r5);
    por_r2r(r6, r7);
    movq_m2r(*(input_data + 12), r3);
    por_r2r(r5, r7);
    pmullw_m2r(*(dequant_matrix + 12), r3);
    psrlq_i2r(16, r4);
    movq_r2m(r7, *(output_data + 8));

    movq_r2r(r4, r5);
    movq_r2r(r0, r7);
    psrlq_i2r(16, r4);
    psrlq_i2r(48, r7);
    movq_r2r(r2, r6);
    pand_r2r(r2, r5);
    pand_r2r(r4, r6);
    movq_r2m(r7, *(output_data + 40));

    pxor_r2r(r6, r4);
    psrlq_i2r(32, r1);
    por_r2r(r5, r4);
    movq_m2r(*M(3), r7);
    pand_r2r(r2, r1);
    movq_m2r(*(input_data + 24), r5);
    psllq_i2r(16, r0);
    pmullw_m2r(*(dequant_matrix + 24), r5);
    pand_r2r(r0, r7);
    movq_r2m(r1, *(output_data + 32));

    por_r2r(r4, r7);
    movq_r2r(r3, r4);
    pand_r2r(r2, r3);
    movq_m2r(*M(2), r1);
    psllq_i2r(32, r3);
    por_r2r(r3, r7);
    movq_r2r(r5, r3);
    psllq_i2r(48, r3);
    pand_r2r(r0, r1);
    movq_r2m(r7, *(output_data + 16));

    por_r2r(r3, r6);
    movq_m2r(*M(1), r7);
    por_r2r(r1, r6);
    movq_m2r(*(input_data + 28), r1);
    pand_r2r(r4, r7);
    pmullw_m2r(*(dequant_matrix + 28), r1);
    por_r2r(r6, r7);
    pand_m2r(*M(1), r0);
    psrlq_i2r(32, r4);
    movq_r2m(r7, *(output_data + 24));

    movq_r2r(r4, r6);
    movq_m2r(*M(3), r7);
    pand_r2r(r2, r4);
    movq_m2r(*M(1), r3);
    pand_r2r(r1, r7);
    pand_r2r(r5, r3);
    por_r2r(r4, r0);
    psllq_i2r(16, r3);
    por_r2r(r0, r7);
    movq_m2r(*M(2), r4);
    por_r2r(r3, r7);
    movq_m2r(*(input_data + 40), r0);
    movq_r2r(r4, r3);
    pmullw_m2r(*(dequant_matrix + 40), r0);
    pand_r2r(r5, r4);
    movq_r2m(r7, *(output_data + 4));

    por_r2r(r4, r6);
    movq_r2r(r3, r4);
    psrlq_i2r(16, r6);
    movq_r2r(r0, r7);
    pand_r2r(r1, r4);
    psllq_i2r(48, r7);
    por_r2r(r4, r6);
    movq_m2r(*(input_data + 44), r4);
    por_r2r(r6, r7);
    pmullw_m2r(*(dequant_matrix + 44), r4);
    psrlq_i2r(16, r3);
    movq_r2m(r7, *(output_data + 12));

    pand_r2r(r1, r3);
    psrlq_i2r(48, r5);
    pand_r2r(r2, r1);
    movq_m2r(*(input_data + 52), r6);
    por_r2r(r3, r5);
    pmullw_m2r(*(input_data + 52), r6);
    psrlq_i2r(16, r0);
    movq_r2r(r4, r7);
    movq_r2r(r2, r3);
    psllq_i2r(48, r7);
    pand_r2r(r0, r3);
    pxor_r2r(r3, r0);
    psllq_i2r(32, r3);
    por_r2r(r5, r7);
    movq_r2r(r6, r5);
    pand_m2r(*M(1), r6);
    por_r2r(r3, r7);
    psllq_i2r(32, r6);
    por_r2r(r1, r0);
    movq_r2m(r7, *(output_data + 20));

    por_r2r(r6, r0);
    movq_m2r(*(input_data + 60), r7);
    movq_r2r(r5, r6);
    pmullw_m2r(*(input_data + 60), r7);
    psrlq_i2r(32, r5);
    pand_r2r(r2, r6);
    movq_r2r(r5, r1);
    movq_r2m(r0, *(output_data + 28));

    pand_r2r(r2, r1);
    movq_m2r(*(input_data + 56), r0);
    movq_r2r(r7, r3);
    pmullw_m2r(*(dequant_matrix + 56), r0);
    psllq_i2r(16, r3);
    pand_m2r(*M(3), r7);
    pxor_r2r(r1, r5);
    por_r2r(r5, r6);
    movq_r2r(r3, r5);
    pand_m2r(*M(3), r5);
    por_r2r(r1, r7);
    movq_m2r(*(input_data + 48), r1);
    pxor_r2r(r5, r3);
    pmullw_m2r(*(dequant_matrix + 48), r1);
    por_r2r(r3, r7);
    por_r2r(r5, r6);
    movq_r2r(r0, r5);
    movq_r2m(r7, *(output_data + 60));

    psrlq_i2r(16, r5);
    pand_m2r(*M(2), r5);
    movq_r2r(r0, r7);
    por_r2r(r5, r6);
    pand_r2r(r2, r0);
    pxor_r2r(r0, r7);
    psllq_i2r(32, r0);
    movq_r2m(r6, *(output_data + 52));

    psrlq_i2r(16, r4);
    movq_m2r(*(input_data + 36), r5);
    psllq_i2r(16, r7);
    pmullw_m2r(*(dequant_matrix + 36), r5);
    movq_r2r(r7, r6);
    movq_m2r(*M(2), r3);
    psllq_i2r(16, r6);
    pand_m2r(*M(3), r7);
    pand_r2r(r1, r3);
    por_r2r(r0, r7);
    movq_r2r(r1, r0);
    pand_m2r(*M(3), r1);
    por_r2r(r3, r6);
    movq_r2r(r4, r3);
    psrlq_i2r(32, r1);
    pand_r2r(r2, r3);
    por_r2r(r1, r7);
    por_r2r(r3, r7);
    movq_r2r(r4, r3);
    pand_m2r(*M(1), r3);
    movq_r2r(r5, r1);
    movq_r2m(r7, *(output_data + 44));

    psrlq_i2r(48, r5);
    movq_m2r(*(input_data + 32), r7);
    por_r2r(r3, r6);
    pmullw_m2r(*(dequant_matrix + 32), r7);
    por_r2r(r5, r6);
    pand_m2r(*M(2), r4);
    psllq_i2r(32, r0);
    movq_r2m(r6, *(output_data + 36));

    movq_r2r(r0, r6);
    pand_m2r(*M(3), r0);
    psllq_i2r(16, r6);
    movq_m2r(*(input_data + 20), r5);
    movq_r2r(r1, r3);
    pmullw_m2r(*(dequant_matrix + 40), r5);
    psrlq_i2r(16, r1);
    pand_m2r(*M(1), r1);
    por_r2r(r4, r0);
    pand_r2r(r7, r2);
    por_r2r(r1, r0);
    por_r2r(r2, r0);
    psllq_i2r(16, r3);
    movq_r2r(r3, r4);
    movq_r2r(r5, r2);
    movq_r2m(r0, *(output_data + 56));

    psrlq_i2r(48, r2);
    pand_m2r(*M(2), r4);
    por_r2r(r2, r6);
    movq_m2r(*M(1), r2);
    por_r2r(r4, r6);
    pand_r2r(r7, r2);
    psllq_i2r(32, r3);
    por_m2r(*(output_data + 40), r3);

    por_r2r(r2, r6);
    movq_m2r(*M(3), r2);
    psllq_i2r(16, r5);
    movq_r2m(r6, *(output_data + 48));

    pand_r2r(r5, r2);
    movq_m2r(*M(2), r6);
    pxor_r2r(r2, r5);
    pand_r2r(r7, r6);
    psrlq_i2r(32, r2);
    pand_m2r(*M(3), r7);
    por_r2r(r2, r3);
    por_m2r(*(output_data + 32), r7);

    por_r2r(r3, r6);
    por_r2r(r5, r7);
    movq_r2m(r6, *(output_data + 40));
    movq_r2m(r7, *(output_data + 32));


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

void vp3_idct_put_mmx(int16_t *input_data, int16_t *dequant_matrix,
    int coeff_count, uint8_t *dest, int stride)
{
    int16_t transformed_data[64];
    int16_t *op;
    int i, j;
    uint8_t vector128[8] = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };

    vp3_idct_mmx(input_data, dequant_matrix, transformed_data);

    /* place in final output */
    op = transformed_data;
    movq_m2r(*vector128, mm0);
    for (i = 0; i < 8; i++) {
#if 1
        for (j = 0; j < 8; j++) {
            if (*op < -128)
                *dest = 0;
            else if (*op > 127)
                *dest = 255;
            else
                *dest = (uint8_t)(*op + 128);
            op++;
            dest++;
        }
        dest += (stride - 8);
#else
/* prototype optimization */
        pxor_r2r(mm1, mm1);
        packsswb_m2r(*(op + 4), mm1);
        movq_r2r(mm1, mm2);
        psrlq_i2r(32, mm2);
        packsswb_m2r(*(op + 0), mm1);
        op += 8;
        por_r2r(mm2, mm1);
        paddb_r2r(mm0, mm1);
        movq_r2m(mm1, *dest);
        dest += stride;
#endif
    }

    /* be a good MMX citizen */
    emms();
}

void vp3_idct_add_mmx(int16_t *input_data, int16_t *dequant_matrix,
    int coeff_count, uint8_t *dest, int stride)
{
    int16_t transformed_data[64];
    int16_t *op;
    int i, j;
    int16_t sample;

    vp3_idct_mmx(input_data, dequant_matrix, transformed_data);

    /* place in final output */
    op = transformed_data;
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            sample = *dest + *op;
            if (sample < 0)
                *dest = 0;
            else if (sample > 255)
                *dest = 255;
            else
                *dest = (uint8_t)(sample & 0xFF);
            op++;
            dest++;
        }
        dest += (stride - 8);
    }

    /* be a good MMX citizen */
    emms();
}
