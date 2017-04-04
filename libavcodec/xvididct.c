/*
 * Xvid MPEG-4 IDCT
 *
 * Copyright (C) 2006-2011 Xvid Solutions GmbH
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
 * Walken IDCT
 * Alternative IDCT implementation for decoding compatibility.
 *
 * @author Skal
 * @note This C version is not the original IDCT, but a modified one that
 *       yields the same error profile as the MMX/MMXEXT/SSE2 versions.
 */

#include "config.h"
#include "libavutil/attributes.h"
#include "avcodec.h"
#include "idctdsp.h"
#include "xvididct.h"

#define ROW_SHIFT 11
#define COL_SHIFT  6

// #define FIX(x)   (int)((x) * (1 << ROW_SHIFT))
#define RND0 65536 // 1 << (COL_SHIFT + ROW_SHIFT - 1);
#define RND1 3597  // FIX (1.75683487303);
#define RND2 2260  // FIX (1.10355339059);
#define RND3 1203  // FIX (0.587788325588);
#define RND4 0
#define RND5 120   // FIX (0.058658283817);
#define RND6 512   // FIX (0.25);
#define RND7 512   // FIX (0.25);

static const int TAB04[] = { 22725, 21407, 19266, 16384, 12873,  8867, 4520 };
static const int TAB17[] = { 31521, 29692, 26722, 22725, 17855, 12299, 6270 };
static const int TAB26[] = { 29692, 27969, 25172, 21407, 16819, 11585, 5906 };
static const int TAB35[] = { 26722, 25172, 22654, 19266, 15137, 10426, 5315 };

static int idct_row(short *in, const int *const tab, int rnd)
{
    const int c1 = tab[0];
    const int c2 = tab[1];
    const int c3 = tab[2];
    const int c4 = tab[3];
    const int c5 = tab[4];
    const int c6 = tab[5];
    const int c7 = tab[6];

    const int right = in[5] | in[6] | in[7];
    const int left  = in[1] | in[2] | in[3];
    if (!(right | in[4])) {
        const int k = c4 * in[0] + rnd;
        if (left) {
            const int a0 = k + c2 * in[2];
            const int a1 = k + c6 * in[2];
            const int a2 = k - c6 * in[2];
            const int a3 = k - c2 * in[2];

            const int b0 = c1 * in[1] + c3 * in[3];
            const int b1 = c3 * in[1] - c7 * in[3];
            const int b2 = c5 * in[1] - c1 * in[3];
            const int b3 = c7 * in[1] - c5 * in[3];

            in[0] = (a0 + b0) >> ROW_SHIFT;
            in[1] = (a1 + b1) >> ROW_SHIFT;
            in[2] = (a2 + b2) >> ROW_SHIFT;
            in[3] = (a3 + b3) >> ROW_SHIFT;
            in[4] = (a3 - b3) >> ROW_SHIFT;
            in[5] = (a2 - b2) >> ROW_SHIFT;
            in[6] = (a1 - b1) >> ROW_SHIFT;
            in[7] = (a0 - b0) >> ROW_SHIFT;
        } else {
            const int a0 = k >> ROW_SHIFT;
            if (a0) {
                in[0] =
                in[1] =
                in[2] =
                in[3] =
                in[4] =
                in[5] =
                in[6] =
                in[7] = a0;
            } else
                return 0;
        }
    } else if (!(left | right)) {
        const int a0 = (rnd + c4 * (in[0] + in[4])) >> ROW_SHIFT;
        const int a1 = (rnd + c4 * (in[0] - in[4])) >> ROW_SHIFT;

        in[0] = a0;
        in[3] = a0;
        in[4] = a0;
        in[7] = a0;
        in[1] = a1;
        in[2] = a1;
        in[5] = a1;
        in[6] = a1;
    } else {
        const int k  = c4 * in[0] + rnd;
        const int a0 = k + c2 * in[2] + c4 * in[4] + c6 * in[6];
        const int a1 = k + c6 * in[2] - c4 * in[4] - c2 * in[6];
        const int a2 = k - c6 * in[2] - c4 * in[4] + c2 * in[6];
        const int a3 = k - c2 * in[2] + c4 * in[4] - c6 * in[6];

        const int b0 = c1 * in[1] + c3 * in[3] + c5 * in[5] + c7 * in[7];
        const int b1 = c3 * in[1] - c7 * in[3] - c1 * in[5] - c5 * in[7];
        const int b2 = c5 * in[1] - c1 * in[3] + c7 * in[5] + c3 * in[7];
        const int b3 = c7 * in[1] - c5 * in[3] + c3 * in[5] - c1 * in[7];

        in[0] = (a0 + b0) >> ROW_SHIFT;
        in[1] = (a1 + b1) >> ROW_SHIFT;
        in[2] = (a2 + b2) >> ROW_SHIFT;
        in[3] = (a3 + b3) >> ROW_SHIFT;
        in[4] = (a3 - b3) >> ROW_SHIFT;
        in[5] = (a2 - b2) >> ROW_SHIFT;
        in[6] = (a1 - b1) >> ROW_SHIFT;
        in[7] = (a0 - b0) >> ROW_SHIFT;
    }
    return 1;
}

#define TAN1  0x32EC
#define TAN2  0x6A0A
#define TAN3  0xAB0E
#define SQRT2 0x5A82

#define MULT(c, x, n)  (((c) * (x)) >> (n))
// 12b version => #define MULT(c,x, n)  ((((c) >> 3) * (x)) >> ((n) - 3))
// 12b zero-testing version:

#define BUTTERFLY(a, b, tmp)     \
    (tmp) = (a) + (b);           \
    (b)   = (a) - (b);           \
    (a)   = (tmp)

#define LOAD_BUTTERFLY(m1, m2, a, b, tmp, s)   \
    (m1) = (s)[(a)] + (s)[(b)];                \
    (m2) = (s)[(a)] - (s)[(b)]

static void idct_col_8(short *const in)
{
    int mm0, mm1, mm2, mm3, mm4, mm5, mm6, mm7, spill;

    // odd

    mm4 = (int) in[7 * 8];
    mm5 = (int) in[5 * 8];
    mm6 = (int) in[3 * 8];
    mm7 = (int) in[1 * 8];

    mm0 = MULT(TAN1, mm4, 16) + mm7;
    mm1 = MULT(TAN1, mm7, 16) - mm4;
    mm2 = MULT(TAN3, mm5, 16) + mm6;
    mm3 = MULT(TAN3, mm6, 16) - mm5;

    mm7 = mm0 + mm2;
    mm4 = mm1 - mm3;
    mm0 = mm0 - mm2;
    mm1 = mm1 + mm3;
    mm6 = mm0 + mm1;
    mm5 = mm0 - mm1;
    mm5 = 2 * MULT(SQRT2, mm5, 16); // 2*sqrt2
    mm6 = 2 * MULT(SQRT2, mm6, 16); // Watch out: precision loss but done to match
                                    // the pmulhw used in MMX/MMXEXT/SSE2 versions

    // even

    mm1 = (int) in[2 * 8];
    mm2 = (int) in[6 * 8];
    mm3 = MULT(TAN2, mm2, 16) + mm1;
    mm2 = MULT(TAN2, mm1, 16) - mm2;

    LOAD_BUTTERFLY(mm0, mm1, 0 * 8, 4 * 8, spill, in);

    BUTTERFLY(mm0, mm3, spill);
    BUTTERFLY(mm0, mm7, spill);
    in[8 * 0] = (int16_t) (mm0 >> COL_SHIFT);
    in[8 * 7] = (int16_t) (mm7 >> COL_SHIFT);
    BUTTERFLY(mm3, mm4, mm0);
    in[8 * 3] = (int16_t) (mm3 >> COL_SHIFT);
    in[8 * 4] = (int16_t) (mm4 >> COL_SHIFT);

    BUTTERFLY(mm1, mm2, mm0);
    BUTTERFLY(mm1, mm6, mm0);
    in[8 * 1] = (int16_t) (mm1 >> COL_SHIFT);
    in[8 * 6] = (int16_t) (mm6 >> COL_SHIFT);
    BUTTERFLY(mm2, mm5, mm0);
    in[8 * 2] = (int16_t) (mm2 >> COL_SHIFT);
    in[8 * 5] = (int16_t) (mm5 >> COL_SHIFT);
}

static void idct_col_4(short *const in)
{
    int mm0, mm1, mm2, mm3, mm4, mm5, mm6, mm7, spill;

    // odd

    mm0 = (int) in[1 * 8];
    mm2 = (int) in[3 * 8];

    mm1 = MULT(TAN1, mm0, 16);
    mm3 = MULT(TAN3, mm2, 16);

    mm7 = mm0 + mm2;
    mm4 = mm1 - mm3;
    mm0 = mm0 - mm2;
    mm1 = mm1 + mm3;
    mm6 = mm0 + mm1;
    mm5 = mm0 - mm1;
    mm6 = 2 * MULT(SQRT2, mm6, 16); // 2*sqrt2
    mm5 = 2 * MULT(SQRT2, mm5, 16);

    // even

    mm0 = mm1 = (int) in[0 * 8];
    mm3 = (int) in[2 * 8];
    mm2 = MULT(TAN2, mm3, 16);

    BUTTERFLY(mm0, mm3, spill);
    BUTTERFLY(mm0, mm7, spill);
    in[8 * 0] = (int16_t) (mm0 >> COL_SHIFT);
    in[8 * 7] = (int16_t) (mm7 >> COL_SHIFT);
    BUTTERFLY(mm3, mm4, mm0);
    in[8 * 3] = (int16_t) (mm3 >> COL_SHIFT);
    in[8 * 4] = (int16_t) (mm4 >> COL_SHIFT);

    BUTTERFLY(mm1, mm2, mm0);
    BUTTERFLY(mm1, mm6, mm0);
    in[8 * 1] = (int16_t) (mm1 >> COL_SHIFT);
    in[8 * 6] = (int16_t) (mm6 >> COL_SHIFT);
    BUTTERFLY(mm2, mm5, mm0);
    in[8 * 2] = (int16_t) (mm2 >> COL_SHIFT);
    in[8 * 5] = (int16_t) (mm5 >> COL_SHIFT);
}

static void idct_col_3(short *const in)
{
    int mm0, mm1, mm2, mm3, mm4, mm5, mm6, mm7, spill;

    // odd

    mm7 = (int) in[1 * 8];
    mm4 = MULT(TAN1, mm7, 16);

    mm6 = mm7 + mm4;
    mm5 = mm7 - mm4;
    mm6 = 2 * MULT(SQRT2, mm6, 16); // 2*sqrt2
    mm5 = 2 * MULT(SQRT2, mm5, 16);

    // even

    mm0 = mm1 = (int) in[0 * 8];
    mm3 = (int) in[2 * 8];
    mm2 = MULT(TAN2, mm3, 16);

    BUTTERFLY(mm0, mm3, spill);
    BUTTERFLY(mm0, mm7, spill);
    in[8 * 0] = (int16_t) (mm0 >> COL_SHIFT);
    in[8 * 7] = (int16_t) (mm7 >> COL_SHIFT);
    BUTTERFLY(mm3, mm4, mm0);
    in[8 * 3] = (int16_t) (mm3 >> COL_SHIFT);
    in[8 * 4] = (int16_t) (mm4 >> COL_SHIFT);

    BUTTERFLY(mm1, mm2, mm0);
    BUTTERFLY(mm1, mm6, mm0);
    in[8 * 1] = (int16_t) (mm1 >> COL_SHIFT);
    in[8 * 6] = (int16_t) (mm6 >> COL_SHIFT);
    BUTTERFLY(mm2, mm5, mm0);
    in[8 * 2] = (int16_t) (mm2 >> COL_SHIFT);
    in[8 * 5] = (int16_t) (mm5 >> COL_SHIFT);
}

void ff_xvid_idct(int16_t *const in)
{
    int i, rows = 0x07;

    idct_row(in + 0 * 8, TAB04, RND0);
    idct_row(in + 1 * 8, TAB17, RND1);
    idct_row(in + 2 * 8, TAB26, RND2);
    if (idct_row(in + 3 * 8, TAB35, RND3))
        rows |= 0x08;
    if (idct_row(in + 4 * 8, TAB04, RND4))
        rows |= 0x10;
    if (idct_row(in + 5 * 8, TAB35, RND5))
        rows |= 0x20;
    if (idct_row(in + 6 * 8, TAB26, RND6))
        rows |= 0x40;
    if (idct_row(in + 7 * 8, TAB17, RND7))
        rows |= 0x80;

    if (rows & 0xF0) {
        for (i = 0; i < 8; i++)
            idct_col_8(in + i);
    } else if (rows & 0x08) {
        for (i = 0; i < 8; i++)
            idct_col_4(in + i);
    } else {
        for (i = 0; i < 8; i++)
            idct_col_3(in + i);
    }
}

static void xvid_idct_put(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    ff_xvid_idct(block);
    ff_put_pixels_clamped_c(block, dest, line_size);
}

static void xvid_idct_add(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    ff_xvid_idct(block);
    ff_add_pixels_clamped_c(block, dest, line_size);
}

av_cold void ff_xvid_idct_init(IDCTDSPContext *c, AVCodecContext *avctx)
{
    const unsigned high_bit_depth = avctx->bits_per_raw_sample > 8;

    if (high_bit_depth || avctx->lowres ||
        !(avctx->idct_algo == FF_IDCT_AUTO ||
          avctx->idct_algo == FF_IDCT_XVID))
        return;

    if (avctx->idct_algo == FF_IDCT_XVID) {
        c->idct_put  = xvid_idct_put;
        c->idct_add  = xvid_idct_add;
        c->idct      = ff_xvid_idct;
        c->perm_type = FF_IDCT_PERM_NONE;
    }

    if (ARCH_X86)
        ff_xvid_idct_init_x86(c, avctx, high_bit_depth);
    if (ARCH_MIPS)
        ff_xvid_idct_init_mips(c, avctx, high_bit_depth);

    ff_init_scantable_permutation(c->idct_permutation, c->perm_type);
}
