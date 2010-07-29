/*
 * idct_mmx.c
 * Copyright (C) 1999-2001 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 * See http://libmpeg2.sourceforge.net/ for updates.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mpeg2dec; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/common.h"
#include "libavcodec/dsputil.h"

#include "libavutil/x86_cpu.h"
#include "dsputil_mmx.h"

#define ROW_SHIFT 11
#define COL_SHIFT 6

#define round(bias) ((int)(((bias)+0.5) * (1<<ROW_SHIFT)))
#define rounder(bias) {round (bias), round (bias)}


#if 0
/* C row IDCT - it is just here to document the MMXEXT and MMX versions */
static inline void idct_row (int16_t * row, int offset,
                             int16_t * table, int32_t * rounder)
{
    int C1, C2, C3, C4, C5, C6, C7;
    int a0, a1, a2, a3, b0, b1, b2, b3;

    row += offset;

    C1 = table[1];
    C2 = table[2];
    C3 = table[3];
    C4 = table[4];
    C5 = table[5];
    C6 = table[6];
    C7 = table[7];

    a0 = C4*row[0] + C2*row[2] + C4*row[4] + C6*row[6] + *rounder;
    a1 = C4*row[0] + C6*row[2] - C4*row[4] - C2*row[6] + *rounder;
    a2 = C4*row[0] - C6*row[2] - C4*row[4] + C2*row[6] + *rounder;
    a3 = C4*row[0] - C2*row[2] + C4*row[4] - C6*row[6] + *rounder;

    b0 = C1*row[1] + C3*row[3] + C5*row[5] + C7*row[7];
    b1 = C3*row[1] - C7*row[3] - C1*row[5] - C5*row[7];
    b2 = C5*row[1] - C1*row[3] + C7*row[5] + C3*row[7];
    b3 = C7*row[1] - C5*row[3] + C3*row[5] - C1*row[7];

    row[0] = (a0 + b0) >> ROW_SHIFT;
    row[1] = (a1 + b1) >> ROW_SHIFT;
    row[2] = (a2 + b2) >> ROW_SHIFT;
    row[3] = (a3 + b3) >> ROW_SHIFT;
    row[4] = (a3 - b3) >> ROW_SHIFT;
    row[5] = (a2 - b2) >> ROW_SHIFT;
    row[6] = (a1 - b1) >> ROW_SHIFT;
    row[7] = (a0 - b0) >> ROW_SHIFT;
}
#endif


/* MMXEXT row IDCT */

#define mmxext_table(c1,c2,c3,c4,c5,c6,c7)      {  c4,  c2, -c4, -c2,   \
                                                   c4,  c6,  c4,  c6,   \
                                                   c1,  c3, -c1, -c5,   \
                                                   c5,  c7,  c3, -c7,   \
                                                   c4, -c6,  c4, -c6,   \
                                                  -c4,  c2,  c4, -c2,   \
                                                   c5, -c1,  c3, -c1,   \
                                                   c7,  c3,  c7, -c5 }

static inline void mmxext_row_head (int16_t * const row, const int offset,
                                    const int16_t * const table)
{
    __asm__ volatile(
        "movq     (%0), %%mm2        \n\t"  /* mm2 = x6 x4 x2 x0 */

        "movq    8(%0), %%mm5        \n\t"  /* mm5 = x7 x5 x3 x1 */
        "movq    %%mm2, %%mm0        \n\t"  /* mm0 = x6 x4 x2 x0 */

        "movq     (%1), %%mm3        \n\t"  /* mm3 = -C2 -C4 C2 C4 */
        "movq    %%mm5, %%mm6        \n\t"  /* mm6 = x7 x5 x3 x1 */

        "movq    8(%1), %%mm4        \n\t"  /* mm4 = C6 C4 C6 C4 */
        "pmaddwd %%mm0, %%mm3        \n\t"  /* mm3 = -C4*x4-C2*x6 C4*x0+C2*x2 */

        "pshufw  $0x4e, %%mm2, %%mm2 \n\t"  /* mm2 = x2 x0 x6 x4 */
        :: "r" ((row+offset)), "r" (table)
    );
}

static inline void mmxext_row (const int16_t * const table,
                               const int32_t * const rounder)
{
    __asm__ volatile (
        "movq    16(%0), %%mm1         \n\t" /* mm1 = -C5 -C1 C3 C1 */
        "pmaddwd  %%mm2, %%mm4         \n\t" /* mm4 = C4*x0+C6*x2 C4*x4+C6*x6 */

        "pmaddwd 32(%0), %%mm0         \n\t" /* mm0 = C4*x4-C6*x6 C4*x0-C6*x2 */
        "pshufw   $0x4e, %%mm6, %%mm6  \n\t" /* mm6 = x3 x1 x7 x5 */

        "movq    24(%0), %%mm7         \n\t" /* mm7 = -C7 C3 C7 C5 */
        "pmaddwd  %%mm5, %%mm1         \n\t" /* mm1= -C1*x5-C5*x7 C1*x1+C3*x3 */

        "paddd     (%1), %%mm3         \n\t" /* mm3 += rounder */
        "pmaddwd  %%mm6, %%mm7         \n\t" /* mm7 = C3*x1-C7*x3 C5*x5+C7*x7 */

        "pmaddwd 40(%0), %%mm2         \n\t" /* mm2= C4*x0-C2*x2 -C4*x4+C2*x6 */
        "paddd    %%mm4, %%mm3         \n\t" /* mm3 = a1 a0 + rounder */

        "pmaddwd 48(%0), %%mm5         \n\t" /* mm5 = C3*x5-C1*x7 C5*x1-C1*x3 */
        "movq     %%mm3, %%mm4         \n\t" /* mm4 = a1 a0 + rounder */

        "pmaddwd 56(%0), %%mm6         \n\t" /* mm6 = C7*x1-C5*x3 C7*x5+C3*x7 */
        "paddd    %%mm7, %%mm1         \n\t" /* mm1 = b1 b0 */

        "paddd     (%1), %%mm0         \n\t" /* mm0 += rounder */
        "psubd    %%mm1, %%mm3         \n\t" /* mm3 = a1-b1 a0-b0 + rounder */

        "psrad $" AV_STRINGIFY(ROW_SHIFT) ", %%mm3         \n\t" /* mm3 = y6 y7 */
        "paddd    %%mm4, %%mm1         \n\t" /* mm1 = a1+b1 a0+b0 + rounder */

        "paddd    %%mm2, %%mm0         \n\t" /* mm0 = a3 a2 + rounder */
        "psrad $" AV_STRINGIFY(ROW_SHIFT) ", %%mm1         \n\t" /* mm1 = y1 y0 */

        "paddd    %%mm6, %%mm5         \n\t" /* mm5 = b3 b2 */
        "movq     %%mm0, %%mm4         \n\t" /* mm4 = a3 a2 + rounder */

        "paddd    %%mm5, %%mm0         \n\t" /* mm0 = a3+b3 a2+b2 + rounder */
        "psubd    %%mm5, %%mm4         \n\t" /* mm4 = a3-b3 a2-b2 + rounder */
        : : "r" (table), "r" (rounder));
}

static inline void mmxext_row_tail (int16_t * const row, const int store)
{
    __asm__ volatile (
        "psrad $" AV_STRINGIFY(ROW_SHIFT) ", %%mm0        \n\t"  /* mm0 = y3 y2 */

        "psrad $" AV_STRINGIFY(ROW_SHIFT) ", %%mm4  \n\t"  /* mm4 = y4 y5 */

        "packssdw %%mm0, %%mm1        \n\t"  /* mm1 = y3 y2 y1 y0 */

        "packssdw %%mm3, %%mm4        \n\t"  /* mm4 = y6 y7 y4 y5 */

        "movq     %%mm1, (%0)         \n\t"  /* save y3 y2 y1 y0 */
        "pshufw   $0xb1, %%mm4, %%mm4 \n\t"  /* mm4 = y7 y6 y5 y4 */

        /* slot */

        "movq     %%mm4, 8(%0)        \n\t"  /* save y7 y6 y5 y4 */
        :: "r" (row+store)
        );
}

static inline void mmxext_row_mid (int16_t * const row, const int store,
                                   const int offset,
                                   const int16_t * const table)
{
    __asm__ volatile (
        "movq     (%0,%1), %%mm2       \n\t" /* mm2 = x6 x4 x2 x0 */
        "psrad $" AV_STRINGIFY(ROW_SHIFT) ", %%mm0 \n\t"   /* mm0 = y3 y2 */

        "movq    8(%0,%1), %%mm5       \n\t" /* mm5 = x7 x5 x3 x1 */
        "psrad $" AV_STRINGIFY(ROW_SHIFT) ", %%mm4 \n\t" /* mm4 = y4 y5 */

        "packssdw   %%mm0, %%mm1       \n\t" /* mm1 = y3 y2 y1 y0 */
        "movq       %%mm5, %%mm6       \n\t" /* mm6 = x7 x5 x3 x1 */

        "packssdw   %%mm3, %%mm4       \n\t" /* mm4 = y6 y7 y4 y5 */
        "movq       %%mm2, %%mm0       \n\t" /* mm0 = x6 x4 x2 x0 */

        "movq       %%mm1, (%0,%2)     \n\t" /* save y3 y2 y1 y0 */
        "pshufw     $0xb1, %%mm4, %%mm4\n\t" /* mm4 = y7 y6 y5 y4 */

        "movq        (%3), %%mm3       \n\t" /* mm3 = -C2 -C4 C2 C4 */
        "movq       %%mm4, 8(%0,%2)    \n\t" /* save y7 y6 y5 y4 */

        "pmaddwd    %%mm0, %%mm3       \n\t" /* mm3= -C4*x4-C2*x6 C4*x0+C2*x2 */

        "movq       8(%3), %%mm4       \n\t" /* mm4 = C6 C4 C6 C4 */
        "pshufw     $0x4e, %%mm2, %%mm2\n\t" /* mm2 = x2 x0 x6 x4 */
        :: "r" (row), "r" ((x86_reg) (2*offset)), "r" ((x86_reg) (2*store)), "r" (table)
        );
}


/* MMX row IDCT */

#define mmx_table(c1,c2,c3,c4,c5,c6,c7) {  c4,  c2,  c4,  c6,   \
                                           c4,  c6, -c4, -c2,   \
                                           c1,  c3,  c3, -c7,   \
                                           c5,  c7, -c1, -c5,   \
                                           c4, -c6,  c4, -c2,   \
                                          -c4,  c2,  c4, -c6,   \
                                           c5, -c1,  c7, -c5,   \
                                           c7,  c3,  c3, -c1 }

static inline void mmx_row_head (int16_t * const row, const int offset,
                                 const int16_t * const table)
{
    __asm__ volatile (
        "movq (%0), %%mm2       \n\t"    /* mm2 = x6 x4 x2 x0 */

        "movq 8(%0), %%mm5      \n\t"    /* mm5 = x7 x5 x3 x1 */
        "movq %%mm2, %%mm0      \n\t"    /* mm0 = x6 x4 x2 x0 */

        "movq (%1), %%mm3       \n\t"    /* mm3 = C6 C4 C2 C4 */
        "movq %%mm5, %%mm6      \n\t"    /* mm6 = x7 x5 x3 x1 */

        "punpckldq %%mm0, %%mm0 \n\t"    /* mm0 = x2 x0 x2 x0 */

        "movq 8(%1), %%mm4      \n\t"    /* mm4 = -C2 -C4 C6 C4 */
        "pmaddwd %%mm0, %%mm3   \n\t"    /* mm3 = C4*x0+C6*x2 C4*x0+C2*x2 */

        "movq 16(%1), %%mm1     \n\t"    /* mm1 = -C7 C3 C3 C1 */
        "punpckhdq %%mm2, %%mm2 \n\t"    /* mm2 = x6 x4 x6 x4 */
        :: "r" ((row+offset)), "r" (table)
        );
}

static inline void mmx_row (const int16_t * const table,
                            const int32_t * const rounder)
{
    __asm__ volatile (
        "pmaddwd   %%mm2, %%mm4    \n\t"  /* mm4 = -C4*x4-C2*x6 C4*x4+C6*x6 */
        "punpckldq %%mm5, %%mm5    \n\t"  /* mm5 = x3 x1 x3 x1 */

        "pmaddwd  32(%0), %%mm0    \n\t"  /* mm0 = C4*x0-C2*x2 C4*x0-C6*x2 */
        "punpckhdq %%mm6, %%mm6    \n\t"  /* mm6 = x7 x5 x7 x5 */

        "movq     24(%0), %%mm7    \n\t"  /* mm7 = -C5 -C1 C7 C5 */
        "pmaddwd   %%mm5, %%mm1    \n\t"  /* mm1 = C3*x1-C7*x3 C1*x1+C3*x3 */

        "paddd      (%1), %%mm3    \n\t"  /* mm3 += rounder */
        "pmaddwd   %%mm6, %%mm7    \n\t"  /* mm7 = -C1*x5-C5*x7 C5*x5+C7*x7 */

        "pmaddwd  40(%0), %%mm2    \n\t"  /* mm2 = C4*x4-C6*x6 -C4*x4+C2*x6 */
        "paddd     %%mm4, %%mm3    \n\t"  /* mm3 = a1 a0 + rounder */

        "pmaddwd  48(%0), %%mm5    \n\t"  /* mm5 = C7*x1-C5*x3 C5*x1-C1*x3 */
        "movq      %%mm3, %%mm4    \n\t"  /* mm4 = a1 a0 + rounder */

        "pmaddwd  56(%0), %%mm6    \n\t"  /* mm6 = C3*x5-C1*x7 C7*x5+C3*x7 */
        "paddd     %%mm7, %%mm1    \n\t"  /* mm1 = b1 b0 */

        "paddd      (%1), %%mm0    \n\t"  /* mm0 += rounder */
        "psubd     %%mm1, %%mm3    \n\t"  /* mm3 = a1-b1 a0-b0 + rounder */

        "psrad $" AV_STRINGIFY(ROW_SHIFT) ", %%mm3    \n\t"  /* mm3 = y6 y7 */
        "paddd     %%mm4, %%mm1    \n\t"  /* mm1 = a1+b1 a0+b0 + rounder */

        "paddd     %%mm2, %%mm0    \n\t"  /* mm0 = a3 a2 + rounder */
        "psrad $" AV_STRINGIFY(ROW_SHIFT) ", %%mm1    \n\t"  /* mm1 = y1 y0 */

        "paddd     %%mm6, %%mm5    \n\t"  /* mm5 = b3 b2 */
        "movq      %%mm0, %%mm7    \n\t"  /* mm7 = a3 a2 + rounder */

        "paddd     %%mm5, %%mm0    \n\t"  /* mm0 = a3+b3 a2+b2 + rounder */
        "psubd     %%mm5, %%mm7    \n\t"  /* mm7 = a3-b3 a2-b2 + rounder */
        :: "r" (table), "r" (rounder)
        );
}

static inline void mmx_row_tail (int16_t * const row, const int store)
{
    __asm__ volatile (
        "psrad $" AV_STRINGIFY(ROW_SHIFT) ", %%mm0      \n\t" /* mm0 = y3 y2 */

        "psrad $" AV_STRINGIFY(ROW_SHIFT) ", %%mm7      \n\t" /* mm7 = y4 y5 */

        "packssdw %%mm0, %%mm1 \n\t" /* mm1 = y3 y2 y1 y0 */

        "packssdw %%mm3, %%mm7 \n\t" /* mm7 = y6 y7 y4 y5 */

        "movq %%mm1, (%0)      \n\t" /* save y3 y2 y1 y0 */
        "movq %%mm7, %%mm4     \n\t" /* mm4 = y6 y7 y4 y5 */

        "pslld $16, %%mm7      \n\t" /* mm7 = y7 0 y5 0 */

        "psrld $16, %%mm4      \n\t" /* mm4 = 0 y6 0 y4 */

        "por %%mm4, %%mm7      \n\t" /* mm7 = y7 y6 y5 y4 */

        /* slot */

        "movq %%mm7, 8(%0)     \n\t" /* save y7 y6 y5 y4 */
        :: "r" (row+store)
        );
}

static inline void mmx_row_mid (int16_t * const row, const int store,
                                const int offset, const int16_t * const table)
{

    __asm__ volatile (
        "movq    (%0,%1), %%mm2    \n\t" /* mm2 = x6 x4 x2 x0 */
        "psrad $" AV_STRINGIFY(ROW_SHIFT) ", %%mm0 \n\t" /* mm0 = y3 y2 */

        "movq   8(%0,%1), %%mm5    \n\t" /* mm5 = x7 x5 x3 x1 */
        "psrad $" AV_STRINGIFY(ROW_SHIFT) ", %%mm7 \n\t" /* mm7 = y4 y5 */

        "packssdw  %%mm0, %%mm1    \n\t" /* mm1 = y3 y2 y1 y0 */
        "movq      %%mm5, %%mm6    \n\t" /* mm6 = x7 x5 x3 x1 */

        "packssdw  %%mm3, %%mm7    \n\t" /* mm7 = y6 y7 y4 y5 */
        "movq      %%mm2, %%mm0    \n\t" /* mm0 = x6 x4 x2 x0 */

        "movq      %%mm1, (%0,%2)  \n\t" /* save y3 y2 y1 y0 */
        "movq      %%mm7, %%mm1    \n\t" /* mm1 = y6 y7 y4 y5 */

        "punpckldq %%mm0, %%mm0    \n\t" /* mm0 = x2 x0 x2 x0 */
        "psrld       $16, %%mm7    \n\t" /* mm7 = 0 y6 0 y4 */

        "movq       (%3), %%mm3    \n\t" /* mm3 = C6 C4 C2 C4 */
        "pslld       $16, %%mm1    \n\t" /* mm1 = y7 0 y5 0 */

        "movq      8(%3), %%mm4    \n\t" /* mm4 = -C2 -C4 C6 C4 */
        "por       %%mm1, %%mm7    \n\t" /* mm7 = y7 y6 y5 y4 */

        "movq     16(%3), %%mm1    \n\t" /* mm1 = -C7 C3 C3 C1 */
        "punpckhdq %%mm2, %%mm2    \n\t" /* mm2 = x6 x4 x6 x4 */

        "movq      %%mm7, 8(%0,%2) \n\t" /* save y7 y6 y5 y4 */
        "pmaddwd   %%mm0, %%mm3    \n\t" /* mm3 = C4*x0+C6*x2 C4*x0+C2*x2 */
        : : "r" (row), "r" ((x86_reg) (2*offset)), "r" ((x86_reg) (2*store)), "r" (table)
        );
}


#if 0
/* C column IDCT - it is just here to document the MMXEXT and MMX versions */
static inline void idct_col (int16_t * col, int offset)
{
/* multiplication - as implemented on mmx */
#define F(c,x) (((c) * (x)) >> 16)

/* saturation - it helps us handle torture test cases */
#define S(x) (((x)>32767) ? 32767 : ((x)<-32768) ? -32768 : (x))

    int16_t x0, x1, x2, x3, x4, x5, x6, x7;
    int16_t y0, y1, y2, y3, y4, y5, y6, y7;
    int16_t a0, a1, a2, a3, b0, b1, b2, b3;
    int16_t u04, v04, u26, v26, u17, v17, u35, v35, u12, v12;

    col += offset;

    x0 = col[0*8];
    x1 = col[1*8];
    x2 = col[2*8];
    x3 = col[3*8];
    x4 = col[4*8];
    x5 = col[5*8];
    x6 = col[6*8];
    x7 = col[7*8];

    u04 = S (x0 + x4);
    v04 = S (x0 - x4);
    u26 = S (F (T2, x6) + x2);
    v26 = S (F (T2, x2) - x6);

    a0 = S (u04 + u26);
    a1 = S (v04 + v26);
    a2 = S (v04 - v26);
    a3 = S (u04 - u26);

    u17 = S (F (T1, x7) + x1);
    v17 = S (F (T1, x1) - x7);
    u35 = S (F (T3, x5) + x3);
    v35 = S (F (T3, x3) - x5);

    b0 = S (u17 + u35);
    b3 = S (v17 - v35);
    u12 = S (u17 - u35);
    v12 = S (v17 + v35);
    u12 = S (2 * F (C4, u12));
    v12 = S (2 * F (C4, v12));
    b1 = S (u12 + v12);
    b2 = S (u12 - v12);

    y0 = S (a0 + b0) >> COL_SHIFT;
    y1 = S (a1 + b1) >> COL_SHIFT;
    y2 = S (a2 + b2) >> COL_SHIFT;
    y3 = S (a3 + b3) >> COL_SHIFT;

    y4 = S (a3 - b3) >> COL_SHIFT;
    y5 = S (a2 - b2) >> COL_SHIFT;
    y6 = S (a1 - b1) >> COL_SHIFT;
    y7 = S (a0 - b0) >> COL_SHIFT;

    col[0*8] = y0;
    col[1*8] = y1;
    col[2*8] = y2;
    col[3*8] = y3;
    col[4*8] = y4;
    col[5*8] = y5;
    col[6*8] = y6;
    col[7*8] = y7;
}
#endif


/* MMX column IDCT */
static inline void idct_col (int16_t * const col, const int offset)
{
#define T1 13036
#define T2 27146
#define T3 43790
#define C4 23170

    DECLARE_ALIGNED(8, static const short, t1_vector)[] = {
        T1,T1,T1,T1,
        T2,T2,T2,T2,
        T3,T3,T3,T3,
        C4,C4,C4,C4
    };

    /* column code adapted from Peter Gubanov */
    /* http://www.elecard.com/peter/idct.shtml */

    __asm__ volatile (
        "movq      (%0), %%mm0    \n\t" /* mm0 = T1 */

        "movq   2*8(%1), %%mm1    \n\t" /* mm1 = x1 */
        "movq     %%mm0, %%mm2    \n\t" /* mm2 = T1 */

        "movq 7*2*8(%1), %%mm4    \n\t" /* mm4 = x7 */
        "pmulhw   %%mm1, %%mm0    \n\t" /* mm0 = T1*x1 */

        "movq    16(%0), %%mm5    \n\t" /* mm5 = T3 */
        "pmulhw   %%mm4, %%mm2    \n\t" /* mm2 = T1*x7 */

        "movq 2*5*8(%1), %%mm6    \n\t" /* mm6 = x5 */
        "movq     %%mm5, %%mm7    \n\t" /* mm7 = T3-1 */

        "movq 3*8*2(%1), %%mm3    \n\t" /* mm3 = x3 */
        "psubsw   %%mm4, %%mm0    \n\t" /* mm0 = v17 */

        "movq     8(%0), %%mm4    \n\t" /* mm4 = T2 */
        "pmulhw   %%mm3, %%mm5    \n\t" /* mm5 = (T3-1)*x3 */

        "paddsw   %%mm2, %%mm1    \n\t" /* mm1 = u17 */
        "pmulhw   %%mm6, %%mm7    \n\t" /* mm7 = (T3-1)*x5 */

        /* slot */

        "movq     %%mm4, %%mm2    \n\t" /* mm2 = T2 */
        "paddsw   %%mm3, %%mm5    \n\t" /* mm5 = T3*x3 */

        "pmulhw 2*8*2(%1), %%mm4  \n\t" /* mm4 = T2*x2 */
        "paddsw   %%mm6, %%mm7    \n\t" /* mm7 = T3*x5 */

        "psubsw   %%mm6, %%mm5    \n\t" /* mm5 = v35 */
        "paddsw   %%mm3, %%mm7    \n\t" /* mm7 = u35 */

        "movq 6*8*2(%1), %%mm3    \n\t" /* mm3 = x6 */
        "movq     %%mm0, %%mm6    \n\t" /* mm6 = v17 */

        "pmulhw   %%mm3, %%mm2    \n\t" /* mm2 = T2*x6 */
        "psubsw   %%mm5, %%mm0    \n\t" /* mm0 = b3 */

        "psubsw   %%mm3, %%mm4    \n\t" /* mm4 = v26 */
        "paddsw   %%mm6, %%mm5    \n\t" /* mm5 = v12 */

        "movq     %%mm0, 3*8*2(%1)\n\t" /* save b3 in scratch0 */
        "movq     %%mm1, %%mm6    \n\t" /* mm6 = u17 */

        "paddsw 2*8*2(%1), %%mm2  \n\t" /* mm2 = u26 */
        "paddsw   %%mm7, %%mm6    \n\t" /* mm6 = b0 */

        "psubsw   %%mm7, %%mm1    \n\t" /* mm1 = u12 */
        "movq     %%mm1, %%mm7    \n\t" /* mm7 = u12 */

        "movq   0*8(%1), %%mm3    \n\t" /* mm3 = x0 */
        "paddsw   %%mm5, %%mm1    \n\t" /* mm1 = u12+v12 */

        "movq    24(%0), %%mm0    \n\t" /* mm0 = C4/2 */
        "psubsw   %%mm5, %%mm7    \n\t" /* mm7 = u12-v12 */

        "movq     %%mm6, 5*8*2(%1)\n\t" /* save b0 in scratch1 */
        "pmulhw   %%mm0, %%mm1    \n\t" /* mm1 = b1/2 */

        "movq     %%mm4, %%mm6    \n\t" /* mm6 = v26 */
        "pmulhw   %%mm0, %%mm7    \n\t" /* mm7 = b2/2 */

        "movq 4*8*2(%1), %%mm5    \n\t" /* mm5 = x4 */
        "movq     %%mm3, %%mm0    \n\t" /* mm0 = x0 */

        "psubsw   %%mm5, %%mm3    \n\t" /* mm3 = v04 */
        "paddsw   %%mm5, %%mm0    \n\t" /* mm0 = u04 */

        "paddsw   %%mm3, %%mm4    \n\t" /* mm4 = a1 */
        "movq     %%mm0, %%mm5    \n\t" /* mm5 = u04 */

        "psubsw   %%mm6, %%mm3    \n\t" /* mm3 = a2 */
        "paddsw   %%mm2, %%mm5    \n\t" /* mm5 = a0 */

        "paddsw   %%mm1, %%mm1    \n\t" /* mm1 = b1 */
        "psubsw   %%mm2, %%mm0    \n\t" /* mm0 = a3 */

        "paddsw   %%mm7, %%mm7    \n\t" /* mm7 = b2 */
        "movq     %%mm3, %%mm2    \n\t" /* mm2 = a2 */

        "movq     %%mm4, %%mm6    \n\t" /* mm6 = a1 */
        "paddsw   %%mm7, %%mm3    \n\t" /* mm3 = a2+b2 */

        "psraw $" AV_STRINGIFY(COL_SHIFT) ", %%mm3\n\t" /* mm3 = y2 */
        "paddsw   %%mm1, %%mm4\n\t" /* mm4 = a1+b1 */

        "psraw $" AV_STRINGIFY(COL_SHIFT) ", %%mm4\n\t" /* mm4 = y1 */
        "psubsw   %%mm1, %%mm6    \n\t" /* mm6 = a1-b1 */

        "movq 5*8*2(%1), %%mm1    \n\t" /* mm1 = b0 */
        "psubsw   %%mm7, %%mm2    \n\t" /* mm2 = a2-b2 */

        "psraw $" AV_STRINGIFY(COL_SHIFT) ", %%mm6\n\t" /* mm6 = y6 */
        "movq     %%mm5, %%mm7    \n\t" /* mm7 = a0 */

        "movq     %%mm4, 1*8*2(%1)\n\t" /* save y1 */
        "psraw $" AV_STRINGIFY(COL_SHIFT) ", %%mm2\n\t" /* mm2 = y5 */

        "movq     %%mm3, 2*8*2(%1)\n\t" /* save y2 */
        "paddsw   %%mm1, %%mm5    \n\t" /* mm5 = a0+b0 */

        "movq 3*8*2(%1), %%mm4    \n\t" /* mm4 = b3 */
        "psubsw   %%mm1, %%mm7    \n\t" /* mm7 = a0-b0 */

        "psraw $" AV_STRINGIFY(COL_SHIFT) ", %%mm5\n\t" /* mm5 = y0 */
        "movq     %%mm0, %%mm3    \n\t" /* mm3 = a3 */

        "movq     %%mm2, 5*8*2(%1)\n\t" /* save y5 */
        "psubsw   %%mm4, %%mm3    \n\t" /* mm3 = a3-b3 */

        "psraw $" AV_STRINGIFY(COL_SHIFT) ", %%mm7\n\t" /* mm7 = y7 */
        "paddsw   %%mm0, %%mm4    \n\t" /* mm4 = a3+b3 */

        "movq     %%mm5, 0*8*2(%1)\n\t" /* save y0 */
        "psraw $" AV_STRINGIFY(COL_SHIFT) ", %%mm3\n\t" /* mm3 = y4 */

        "movq     %%mm6, 6*8*2(%1)\n\t" /* save y6 */
        "psraw $" AV_STRINGIFY(COL_SHIFT) ", %%mm4\n\t" /* mm4 = y3 */

        "movq     %%mm7, 7*8*2(%1)\n\t" /* save y7 */

        "movq     %%mm3, 4*8*2(%1)\n\t" /* save y4 */

        "movq     %%mm4, 3*8*2(%1)\n\t" /* save y3 */
        :: "r" (t1_vector), "r" (col+offset)
        );

#undef T1
#undef T2
#undef T3
#undef C4
}


DECLARE_ALIGNED(8, static const int32_t, rounder0)[] =
    rounder ((1 << (COL_SHIFT - 1)) - 0.5);
DECLARE_ALIGNED(8, static const int32_t, rounder4)[] = rounder (0);
DECLARE_ALIGNED(8, static const int32_t, rounder1)[] =
    rounder (1.25683487303);        /* C1*(C1/C4+C1+C7)/2 */
DECLARE_ALIGNED(8, static const int32_t, rounder7)[] =
    rounder (-0.25);                /* C1*(C7/C4+C7-C1)/2 */
DECLARE_ALIGNED(8, static const int32_t, rounder2)[] =
    rounder (0.60355339059);        /* C2 * (C6+C2)/2 */
DECLARE_ALIGNED(8, static const int32_t, rounder6)[] =
    rounder (-0.25);                /* C2 * (C6-C2)/2 */
DECLARE_ALIGNED(8, static const int32_t, rounder3)[] =
    rounder (0.087788325588);       /* C3*(-C3/C4+C3+C5)/2 */
DECLARE_ALIGNED(8, static const int32_t, rounder5)[] =
    rounder (-0.441341716183);      /* C3*(-C5/C4+C5-C3)/2 */

#undef COL_SHIFT
#undef ROW_SHIFT

#define declare_idct(idct,table,idct_row_head,idct_row,idct_row_tail,idct_row_mid) \
void idct (int16_t * const block)                                       \
{                                                                       \
    DECLARE_ALIGNED(16, static const int16_t, table04)[] =              \
        table (22725, 21407, 19266, 16384, 12873,  8867, 4520);         \
    DECLARE_ALIGNED(16, static const int16_t, table17)[] =              \
        table (31521, 29692, 26722, 22725, 17855, 12299, 6270);         \
    DECLARE_ALIGNED(16, static const int16_t, table26)[] =              \
        table (29692, 27969, 25172, 21407, 16819, 11585, 5906);         \
    DECLARE_ALIGNED(16, static const int16_t, table35)[] =              \
        table (26722, 25172, 22654, 19266, 15137, 10426, 5315);         \
                                                                        \
    idct_row_head (block, 0*8, table04);                                \
    idct_row (table04, rounder0);                                       \
    idct_row_mid (block, 0*8, 4*8, table04);                            \
    idct_row (table04, rounder4);                                       \
    idct_row_mid (block, 4*8, 1*8, table17);                            \
    idct_row (table17, rounder1);                                       \
    idct_row_mid (block, 1*8, 7*8, table17);                            \
    idct_row (table17, rounder7);                                       \
    idct_row_mid (block, 7*8, 2*8, table26);                            \
    idct_row (table26, rounder2);                                       \
    idct_row_mid (block, 2*8, 6*8, table26);                            \
    idct_row (table26, rounder6);                                       \
    idct_row_mid (block, 6*8, 3*8, table35);                            \
    idct_row (table35, rounder3);                                       \
    idct_row_mid (block, 3*8, 5*8, table35);                            \
    idct_row (table35, rounder5);                                       \
    idct_row_tail (block, 5*8);                                         \
                                                                        \
    idct_col (block, 0);                                                \
    idct_col (block, 4);                                                \
}

declare_idct (ff_mmxext_idct, mmxext_table,
              mmxext_row_head, mmxext_row, mmxext_row_tail, mmxext_row_mid)

declare_idct (ff_mmx_idct, mmx_table,
              mmx_row_head, mmx_row, mmx_row_tail, mmx_row_mid)

