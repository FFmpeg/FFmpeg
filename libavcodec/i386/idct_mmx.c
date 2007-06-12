/*
 * idct_mmx.c
 * Copyright (C) 1999-2001 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
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

#include "common.h"
#include "dsputil.h"

#include "mmx.h"

#define ATTR_ALIGN(align) __attribute__ ((__aligned__ (align)))

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

static inline void mmxext_row_head (int16_t * row, int offset, const int16_t * table)
{
    movq_m2r (*(row+offset), mm2);      // mm2 = x6 x4 x2 x0

    movq_m2r (*(row+offset+4), mm5);    // mm5 = x7 x5 x3 x1
    movq_r2r (mm2, mm0);                // mm0 = x6 x4 x2 x0

    movq_m2r (*table, mm3);             // mm3 = -C2 -C4 C2 C4
    movq_r2r (mm5, mm6);                // mm6 = x7 x5 x3 x1

    movq_m2r (*(table+4), mm4);         // mm4 = C6 C4 C6 C4
    pmaddwd_r2r (mm0, mm3);             // mm3 = -C4*x4-C2*x6 C4*x0+C2*x2

    pshufw_r2r (mm2, mm2, 0x4e);        // mm2 = x2 x0 x6 x4
}

static inline void mmxext_row (const int16_t * table, const int32_t * rounder)
{
    movq_m2r (*(table+8), mm1);         // mm1 = -C5 -C1 C3 C1
    pmaddwd_r2r (mm2, mm4);             // mm4 = C4*x0+C6*x2 C4*x4+C6*x6

    pmaddwd_m2r (*(table+16), mm0);     // mm0 = C4*x4-C6*x6 C4*x0-C6*x2
    pshufw_r2r (mm6, mm6, 0x4e);        // mm6 = x3 x1 x7 x5

    movq_m2r (*(table+12), mm7);        // mm7 = -C7 C3 C7 C5
    pmaddwd_r2r (mm5, mm1);             // mm1 = -C1*x5-C5*x7 C1*x1+C3*x3

    paddd_m2r (*rounder, mm3);          // mm3 += rounder
    pmaddwd_r2r (mm6, mm7);             // mm7 = C3*x1-C7*x3 C5*x5+C7*x7

    pmaddwd_m2r (*(table+20), mm2);     // mm2 = C4*x0-C2*x2 -C4*x4+C2*x6
    paddd_r2r (mm4, mm3);               // mm3 = a1 a0 + rounder

    pmaddwd_m2r (*(table+24), mm5);     // mm5 = C3*x5-C1*x7 C5*x1-C1*x3
    movq_r2r (mm3, mm4);                // mm4 = a1 a0 + rounder

    pmaddwd_m2r (*(table+28), mm6);     // mm6 = C7*x1-C5*x3 C7*x5+C3*x7
    paddd_r2r (mm7, mm1);               // mm1 = b1 b0

    paddd_m2r (*rounder, mm0);          // mm0 += rounder
    psubd_r2r (mm1, mm3);               // mm3 = a1-b1 a0-b0 + rounder

    psrad_i2r (ROW_SHIFT, mm3);         // mm3 = y6 y7
    paddd_r2r (mm4, mm1);               // mm1 = a1+b1 a0+b0 + rounder

    paddd_r2r (mm2, mm0);               // mm0 = a3 a2 + rounder
    psrad_i2r (ROW_SHIFT, mm1);         // mm1 = y1 y0

    paddd_r2r (mm6, mm5);               // mm5 = b3 b2
    movq_r2r (mm0, mm4);                // mm4 = a3 a2 + rounder

    paddd_r2r (mm5, mm0);               // mm0 = a3+b3 a2+b2 + rounder
    psubd_r2r (mm5, mm4);               // mm4 = a3-b3 a2-b2 + rounder
}

static inline void mmxext_row_tail (int16_t * row, int store)
{
    psrad_i2r (ROW_SHIFT, mm0);         // mm0 = y3 y2

    psrad_i2r (ROW_SHIFT, mm4);         // mm4 = y4 y5

    packssdw_r2r (mm0, mm1);            // mm1 = y3 y2 y1 y0

    packssdw_r2r (mm3, mm4);            // mm4 = y6 y7 y4 y5

    movq_r2m (mm1, *(row+store));       // save y3 y2 y1 y0
    pshufw_r2r (mm4, mm4, 0xb1);        // mm4 = y7 y6 y5 y4

    /* slot */

    movq_r2m (mm4, *(row+store+4));     // save y7 y6 y5 y4
}

static inline void mmxext_row_mid (int16_t * row, int store,
                                   int offset, const int16_t * table)
{
    movq_m2r (*(row+offset), mm2);      // mm2 = x6 x4 x2 x0
    psrad_i2r (ROW_SHIFT, mm0);         // mm0 = y3 y2

    movq_m2r (*(row+offset+4), mm5);    // mm5 = x7 x5 x3 x1
    psrad_i2r (ROW_SHIFT, mm4);         // mm4 = y4 y5

    packssdw_r2r (mm0, mm1);            // mm1 = y3 y2 y1 y0
    movq_r2r (mm5, mm6);                // mm6 = x7 x5 x3 x1

    packssdw_r2r (mm3, mm4);            // mm4 = y6 y7 y4 y5
    movq_r2r (mm2, mm0);                // mm0 = x6 x4 x2 x0

    movq_r2m (mm1, *(row+store));       // save y3 y2 y1 y0
    pshufw_r2r (mm4, mm4, 0xb1);        // mm4 = y7 y6 y5 y4

    movq_m2r (*table, mm3);             // mm3 = -C2 -C4 C2 C4
    movq_r2m (mm4, *(row+store+4));     // save y7 y6 y5 y4

    pmaddwd_r2r (mm0, mm3);             // mm3 = -C4*x4-C2*x6 C4*x0+C2*x2

    movq_m2r (*(table+4), mm4);         // mm4 = C6 C4 C6 C4
    pshufw_r2r (mm2, mm2, 0x4e);        // mm2 = x2 x0 x6 x4
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

static inline void mmx_row_head (int16_t * row, int offset, const int16_t * table)
{
    movq_m2r (*(row+offset), mm2);      // mm2 = x6 x4 x2 x0

    movq_m2r (*(row+offset+4), mm5);    // mm5 = x7 x5 x3 x1
    movq_r2r (mm2, mm0);                // mm0 = x6 x4 x2 x0

    movq_m2r (*table, mm3);             // mm3 = C6 C4 C2 C4
    movq_r2r (mm5, mm6);                // mm6 = x7 x5 x3 x1

    punpckldq_r2r (mm0, mm0);           // mm0 = x2 x0 x2 x0

    movq_m2r (*(table+4), mm4);         // mm4 = -C2 -C4 C6 C4
    pmaddwd_r2r (mm0, mm3);             // mm3 = C4*x0+C6*x2 C4*x0+C2*x2

    movq_m2r (*(table+8), mm1);         // mm1 = -C7 C3 C3 C1
    punpckhdq_r2r (mm2, mm2);           // mm2 = x6 x4 x6 x4
}

static inline void mmx_row (const int16_t * table, const int32_t * rounder)
{
    pmaddwd_r2r (mm2, mm4);             // mm4 = -C4*x4-C2*x6 C4*x4+C6*x6
    punpckldq_r2r (mm5, mm5);           // mm5 = x3 x1 x3 x1

    pmaddwd_m2r (*(table+16), mm0);     // mm0 = C4*x0-C2*x2 C4*x0-C6*x2
    punpckhdq_r2r (mm6, mm6);           // mm6 = x7 x5 x7 x5

    movq_m2r (*(table+12), mm7);        // mm7 = -C5 -C1 C7 C5
    pmaddwd_r2r (mm5, mm1);             // mm1 = C3*x1-C7*x3 C1*x1+C3*x3

    paddd_m2r (*rounder, mm3);          // mm3 += rounder
    pmaddwd_r2r (mm6, mm7);             // mm7 = -C1*x5-C5*x7 C5*x5+C7*x7

    pmaddwd_m2r (*(table+20), mm2);     // mm2 = C4*x4-C6*x6 -C4*x4+C2*x6
    paddd_r2r (mm4, mm3);               // mm3 = a1 a0 + rounder

    pmaddwd_m2r (*(table+24), mm5);     // mm5 = C7*x1-C5*x3 C5*x1-C1*x3
    movq_r2r (mm3, mm4);                // mm4 = a1 a0 + rounder

    pmaddwd_m2r (*(table+28), mm6);     // mm6 = C3*x5-C1*x7 C7*x5+C3*x7
    paddd_r2r (mm7, mm1);               // mm1 = b1 b0

    paddd_m2r (*rounder, mm0);          // mm0 += rounder
    psubd_r2r (mm1, mm3);               // mm3 = a1-b1 a0-b0 + rounder

    psrad_i2r (ROW_SHIFT, mm3);         // mm3 = y6 y7
    paddd_r2r (mm4, mm1);               // mm1 = a1+b1 a0+b0 + rounder

    paddd_r2r (mm2, mm0);               // mm0 = a3 a2 + rounder
    psrad_i2r (ROW_SHIFT, mm1);         // mm1 = y1 y0

    paddd_r2r (mm6, mm5);               // mm5 = b3 b2
    movq_r2r (mm0, mm7);                // mm7 = a3 a2 + rounder

    paddd_r2r (mm5, mm0);               // mm0 = a3+b3 a2+b2 + rounder
    psubd_r2r (mm5, mm7);               // mm7 = a3-b3 a2-b2 + rounder
}

static inline void mmx_row_tail (int16_t * row, int store)
{
    psrad_i2r (ROW_SHIFT, mm0);         // mm0 = y3 y2

    psrad_i2r (ROW_SHIFT, mm7);         // mm7 = y4 y5

    packssdw_r2r (mm0, mm1);            // mm1 = y3 y2 y1 y0

    packssdw_r2r (mm3, mm7);            // mm7 = y6 y7 y4 y5

    movq_r2m (mm1, *(row+store));       // save y3 y2 y1 y0
    movq_r2r (mm7, mm4);                // mm4 = y6 y7 y4 y5

    pslld_i2r (16, mm7);                // mm7 = y7 0 y5 0

    psrld_i2r (16, mm4);                // mm4 = 0 y6 0 y4

    por_r2r (mm4, mm7);                 // mm7 = y7 y6 y5 y4

    /* slot */

    movq_r2m (mm7, *(row+store+4));     // save y7 y6 y5 y4
}

static inline void mmx_row_mid (int16_t * row, int store,
                                int offset, const int16_t * table)
{
    movq_m2r (*(row+offset), mm2);      // mm2 = x6 x4 x2 x0
    psrad_i2r (ROW_SHIFT, mm0);         // mm0 = y3 y2

    movq_m2r (*(row+offset+4), mm5);    // mm5 = x7 x5 x3 x1
    psrad_i2r (ROW_SHIFT, mm7);         // mm7 = y4 y5

    packssdw_r2r (mm0, mm1);            // mm1 = y3 y2 y1 y0
    movq_r2r (mm5, mm6);                // mm6 = x7 x5 x3 x1

    packssdw_r2r (mm3, mm7);            // mm7 = y6 y7 y4 y5
    movq_r2r (mm2, mm0);                // mm0 = x6 x4 x2 x0

    movq_r2m (mm1, *(row+store));       // save y3 y2 y1 y0
    movq_r2r (mm7, mm1);                // mm1 = y6 y7 y4 y5

    punpckldq_r2r (mm0, mm0);           // mm0 = x2 x0 x2 x0
    psrld_i2r (16, mm7);                // mm7 = 0 y6 0 y4

    movq_m2r (*table, mm3);             // mm3 = C6 C4 C2 C4
    pslld_i2r (16, mm1);                // mm1 = y7 0 y5 0

    movq_m2r (*(table+4), mm4);         // mm4 = -C2 -C4 C6 C4
    por_r2r (mm1, mm7);                 // mm7 = y7 y6 y5 y4

    movq_m2r (*(table+8), mm1);         // mm1 = -C7 C3 C3 C1
    punpckhdq_r2r (mm2, mm2);           // mm2 = x6 x4 x6 x4

    movq_r2m (mm7, *(row+store+4));     // save y7 y6 y5 y4
    pmaddwd_r2r (mm0, mm3);             // mm3 = C4*x0+C6*x2 C4*x0+C2*x2
}


#if 0
// C column IDCT - it is just here to document the MMXEXT and MMX versions
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


// MMX column IDCT
static inline void idct_col (int16_t * col, int offset)
{
#define T1 13036
#define T2 27146
#define T3 43790
#define C4 23170

    static const short _T1[] ATTR_ALIGN(8) = {T1,T1,T1,T1};
    static const short _T2[] ATTR_ALIGN(8) = {T2,T2,T2,T2};
    static const short _T3[] ATTR_ALIGN(8) = {T3,T3,T3,T3};
    static const short _C4[] ATTR_ALIGN(8) = {C4,C4,C4,C4};

    /* column code adapted from peter gubanov */
    /* http://www.elecard.com/peter/idct.shtml */

    movq_m2r (*_T1, mm0);               // mm0 = T1

    movq_m2r (*(col+offset+1*8), mm1);  // mm1 = x1
    movq_r2r (mm0, mm2);                // mm2 = T1

    movq_m2r (*(col+offset+7*8), mm4);  // mm4 = x7
    pmulhw_r2r (mm1, mm0);              // mm0 = T1*x1

    movq_m2r (*_T3, mm5);               // mm5 = T3
    pmulhw_r2r (mm4, mm2);              // mm2 = T1*x7

    movq_m2r (*(col+offset+5*8), mm6);  // mm6 = x5
    movq_r2r (mm5, mm7);                // mm7 = T3-1

    movq_m2r (*(col+offset+3*8), mm3);  // mm3 = x3
    psubsw_r2r (mm4, mm0);              // mm0 = v17

    movq_m2r (*_T2, mm4);               // mm4 = T2
    pmulhw_r2r (mm3, mm5);              // mm5 = (T3-1)*x3

    paddsw_r2r (mm2, mm1);              // mm1 = u17
    pmulhw_r2r (mm6, mm7);              // mm7 = (T3-1)*x5

    /* slot */

    movq_r2r (mm4, mm2);                // mm2 = T2
    paddsw_r2r (mm3, mm5);              // mm5 = T3*x3

    pmulhw_m2r (*(col+offset+2*8), mm4);// mm4 = T2*x2
    paddsw_r2r (mm6, mm7);              // mm7 = T3*x5

    psubsw_r2r (mm6, mm5);              // mm5 = v35
    paddsw_r2r (mm3, mm7);              // mm7 = u35

    movq_m2r (*(col+offset+6*8), mm3);  // mm3 = x6
    movq_r2r (mm0, mm6);                // mm6 = v17

    pmulhw_r2r (mm3, mm2);              // mm2 = T2*x6
    psubsw_r2r (mm5, mm0);              // mm0 = b3

    psubsw_r2r (mm3, mm4);              // mm4 = v26
    paddsw_r2r (mm6, mm5);              // mm5 = v12

    movq_r2m (mm0, *(col+offset+3*8));  // save b3 in scratch0
    movq_r2r (mm1, mm6);                // mm6 = u17

    paddsw_m2r (*(col+offset+2*8), mm2);// mm2 = u26
    paddsw_r2r (mm7, mm6);              // mm6 = b0

    psubsw_r2r (mm7, mm1);              // mm1 = u12
    movq_r2r (mm1, mm7);                // mm7 = u12

    movq_m2r (*(col+offset+0*8), mm3);  // mm3 = x0
    paddsw_r2r (mm5, mm1);              // mm1 = u12+v12

    movq_m2r (*_C4, mm0);               // mm0 = C4/2
    psubsw_r2r (mm5, mm7);              // mm7 = u12-v12

    movq_r2m (mm6, *(col+offset+5*8));  // save b0 in scratch1
    pmulhw_r2r (mm0, mm1);              // mm1 = b1/2

    movq_r2r (mm4, mm6);                // mm6 = v26
    pmulhw_r2r (mm0, mm7);              // mm7 = b2/2

    movq_m2r (*(col+offset+4*8), mm5);  // mm5 = x4
    movq_r2r (mm3, mm0);                // mm0 = x0

    psubsw_r2r (mm5, mm3);              // mm3 = v04
    paddsw_r2r (mm5, mm0);              // mm0 = u04

    paddsw_r2r (mm3, mm4);              // mm4 = a1
    movq_r2r (mm0, mm5);                // mm5 = u04

    psubsw_r2r (mm6, mm3);              // mm3 = a2
    paddsw_r2r (mm2, mm5);              // mm5 = a0

    paddsw_r2r (mm1, mm1);              // mm1 = b1
    psubsw_r2r (mm2, mm0);              // mm0 = a3

    paddsw_r2r (mm7, mm7);              // mm7 = b2
    movq_r2r (mm3, mm2);                // mm2 = a2

    movq_r2r (mm4, mm6);                // mm6 = a1
    paddsw_r2r (mm7, mm3);              // mm3 = a2+b2

    psraw_i2r (COL_SHIFT, mm3);         // mm3 = y2
    paddsw_r2r (mm1, mm4);              // mm4 = a1+b1

    psraw_i2r (COL_SHIFT, mm4);         // mm4 = y1
    psubsw_r2r (mm1, mm6);              // mm6 = a1-b1

    movq_m2r (*(col+offset+5*8), mm1);  // mm1 = b0
    psubsw_r2r (mm7, mm2);              // mm2 = a2-b2

    psraw_i2r (COL_SHIFT, mm6);         // mm6 = y6
    movq_r2r (mm5, mm7);                // mm7 = a0

    movq_r2m (mm4, *(col+offset+1*8));  // save y1
    psraw_i2r (COL_SHIFT, mm2);         // mm2 = y5

    movq_r2m (mm3, *(col+offset+2*8));  // save y2
    paddsw_r2r (mm1, mm5);              // mm5 = a0+b0

    movq_m2r (*(col+offset+3*8), mm4);  // mm4 = b3
    psubsw_r2r (mm1, mm7);              // mm7 = a0-b0

    psraw_i2r (COL_SHIFT, mm5);         // mm5 = y0
    movq_r2r (mm0, mm3);                // mm3 = a3

    movq_r2m (mm2, *(col+offset+5*8));  // save y5
    psubsw_r2r (mm4, mm3);              // mm3 = a3-b3

    psraw_i2r (COL_SHIFT, mm7);         // mm7 = y7
    paddsw_r2r (mm0, mm4);              // mm4 = a3+b3

    movq_r2m (mm5, *(col+offset+0*8));  // save y0
    psraw_i2r (COL_SHIFT, mm3);         // mm3 = y4

    movq_r2m (mm6, *(col+offset+6*8));  // save y6
    psraw_i2r (COL_SHIFT, mm4);         // mm4 = y3

    movq_r2m (mm7, *(col+offset+7*8));  // save y7

    movq_r2m (mm3, *(col+offset+4*8));  // save y4

    movq_r2m (mm4, *(col+offset+3*8));  // save y3

#undef T1
#undef T2
#undef T3
#undef C4
}

static const int32_t rounder0[] ATTR_ALIGN(8) =
    rounder ((1 << (COL_SHIFT - 1)) - 0.5);
static const int32_t rounder4[] ATTR_ALIGN(8) = rounder (0);
static const int32_t rounder1[] ATTR_ALIGN(8) =
    rounder (1.25683487303);        /* C1*(C1/C4+C1+C7)/2 */
static const int32_t rounder7[] ATTR_ALIGN(8) =
    rounder (-0.25);                /* C1*(C7/C4+C7-C1)/2 */
static const int32_t rounder2[] ATTR_ALIGN(8) =
    rounder (0.60355339059);        /* C2 * (C6+C2)/2 */
static const int32_t rounder6[] ATTR_ALIGN(8) =
    rounder (-0.25);                /* C2 * (C6-C2)/2 */
static const int32_t rounder3[] ATTR_ALIGN(8) =
    rounder (0.087788325588);       /* C3*(-C3/C4+C3+C5)/2 */
static const int32_t rounder5[] ATTR_ALIGN(8) =
    rounder (-0.441341716183);      /* C3*(-C5/C4+C5-C3)/2 */

#undef COL_SHIFT
#undef ROW_SHIFT

#define declare_idct(idct,table,idct_row_head,idct_row,idct_row_tail,idct_row_mid) \
void idct (int16_t * block)                                             \
{                                                                       \
    static const int16_t table04[] ATTR_ALIGN(16) =                     \
        table (22725, 21407, 19266, 16384, 12873,  8867, 4520);         \
    static const int16_t table17[] ATTR_ALIGN(16) =                     \
        table (31521, 29692, 26722, 22725, 17855, 12299, 6270);         \
    static const int16_t table26[] ATTR_ALIGN(16) =                     \
        table (29692, 27969, 25172, 21407, 16819, 11585, 5906);         \
    static const int16_t table35[] ATTR_ALIGN(16) =                     \
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

void ff_mmx_idct(DCTELEM *block);
void ff_mmxext_idct(DCTELEM *block);

declare_idct (ff_mmxext_idct, mmxext_table,
              mmxext_row_head, mmxext_row, mmxext_row_tail, mmxext_row_mid)

declare_idct (ff_mmx_idct, mmx_table,
              mmx_row_head, mmx_row, mmx_row_tail, mmx_row_mid)

