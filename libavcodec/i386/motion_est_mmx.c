/*
 * MMX optimized motion estimation
 * Copyright (c) 2001 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include "../dsputil.h"
#include "mmx.h"

static const unsigned long long int mm_wone __attribute__ ((aligned(8))) = 0x0001000100010001;
static const unsigned long long int mm_wtwo __attribute__ ((aligned(8))) = 0x0002000200020002;

/* mm7 is accumulator, mm6 is zero */
static inline void sad_add(const UINT8 *p1, const UINT8 *p2)
{
    movq_m2r(*p1, mm0);
    movq_m2r(*p2, mm1);
    movq_r2r(mm0, mm2);
    psubusb_r2r(mm1, mm0);
    psubusb_r2r(mm2, mm1);
    por_r2r(mm1, mm0); /* mm0 is absolute value */

    movq_r2r(mm0, mm1);
    punpcklbw_r2r(mm6, mm0);
    punpckhbw_r2r(mm6, mm1);
    paddusw_r2r(mm0, mm7);
    paddusw_r2r(mm1, mm7);
}

/* convert mm7 to value */
static inline int sad_end(void)
{
    int res;

    movq_r2r(mm7, mm0);
    psrlq_i2r(32, mm7);
    paddusw_r2r(mm0, mm7);

    movq_r2r(mm7, mm0);
    psrlq_i2r(16, mm7);
    paddusw_r2r(mm0, mm7);
    __asm __volatile ("movd %%mm7, %0" : "=a" (res));
    return res & 0xffff;
}

int pix_abs16x16_mmx(UINT8 *blk1, UINT8 *blk2, int lx, int h)
{
    const UINT8 *p1, *p2;

    h >>= 1;
    p1 = blk1;
    p2 = blk2;
    pxor_r2r(mm7, mm7); /* mm7 is accumulator */
    pxor_r2r(mm6, mm6); /* mm7 is zero constant */
    do {
        sad_add(p1, p2);
        sad_add(p1 + 8, p2 + 8);
        p1 += lx;
        p2 += lx;
        sad_add(p1, p2);
        sad_add(p1 + 8, p2 + 8);
        p1 += lx;
        p2 += lx;
    } while (--h);
    return sad_end();
}

/* please test it ! */
static inline void sad_add_sse(const UINT8 *p1, const UINT8 *p2)
{
    movq_m2r(*(p1 + 0), mm0);
    movq_m2r(*(p1 + 8), mm1);
    psadbw_m2r(*(p2 + 0), mm0);
    psadbw_m2r(*(p2 + 8), mm1);
    paddusw_r2r(mm0, mm7);
    paddusw_r2r(mm1, mm7);
}

int pix_abs16x16_sse(UINT8 *blk1, UINT8 *blk2, int lx, int h)
{
    const UINT8 *p1, *p2;

    h >>= 1;
    p1 = blk1;
    p2 = blk2;
    pxor_r2r(mm7, mm7); /* mm7 is accumulator */
    do {
        sad_add_sse(p1, p2);
        p1 += lx;
        p2 += lx;
        sad_add_sse(p1, p2);
        p1 += lx;
        p2 += lx;
    } while (--h);
    return sad_end();
}

#define DUMP(reg) { mmx_t tmp; movq_r2m(reg, tmp); printf(#reg "=%016Lx\n", tmp.uq); }

/* mm7 is accumulator, mm6 is zero */
static inline void sad_add_x2(const UINT8 *p1, const UINT8 *p2, const UINT8 *p3)
{
    movq_m2r(*(p2 + 0), mm0);
    movq_m2r(*(p3 + 0), mm1);
    movq_r2r(mm0, mm2);
    movq_r2r(mm1, mm3);
    punpcklbw_r2r(mm6, mm0); /* extract 4 bytes low */
    punpcklbw_r2r(mm6, mm1);
    punpckhbw_r2r(mm6, mm2); /* high */
    punpckhbw_r2r(mm6, mm3); 
    paddusw_r2r(mm1, mm0);
    paddusw_r2r(mm3, mm2);
    movq_m2r(*(p1 + 0), mm1); /* mm1 : other value */
    paddusw_r2r(mm5, mm0); /* + 1 */
    paddusw_r2r(mm5, mm2); /* + 1 */
    psrlw_i2r(1, mm0);
    psrlw_i2r(1, mm2);
    packuswb_r2r(mm2, mm0); /* average is in mm0 */

    movq_r2r(mm1, mm2); 
    psubusb_r2r(mm0, mm1);
    psubusb_r2r(mm2, mm0);
    por_r2r(mm1, mm0); /* mm0 is absolute value */

    movq_r2r(mm0, mm1);
    punpcklbw_r2r(mm6, mm0);
    punpckhbw_r2r(mm6, mm1);
    paddusw_r2r(mm0, mm7);
    paddusw_r2r(mm1, mm7);
}

int pix_abs16x16_x2_mmx(UINT8 *blk1, UINT8 *blk2, int lx, int h)
{
    const UINT8 *p1, *p2;

    p1 = blk1;
    p2 = blk2;
    pxor_r2r(mm7, mm7); /* mm7 is accumulator */
    pxor_r2r(mm6, mm6); /* mm7 is zero constant */
    movq_m2r(mm_wone, mm5); /* one constant */
    do {
        sad_add_x2(p1, p2, p2 + 1);
        sad_add_x2(p1 + 8, p2 + 8, p2 + 9);
        p1 += lx;
        p2 += lx;
    } while (--h);
    return sad_end();
}

int pix_abs16x16_y2_mmx(UINT8 *blk1, UINT8 *blk2, int lx, int h)
{
    const UINT8 *p1, *p2;

    p1 = blk1;
    p2 = blk2;
    pxor_r2r(mm7, mm7); /* mm7 is accumulator */
    pxor_r2r(mm6, mm6); /* mm7 is zero constant */
    movq_m2r(mm_wone, mm5); /* one constant */
    do {
        sad_add_x2(p1, p2, p2 + lx);
        sad_add_x2(p1 + 8, p2 + 8, p2 + 8 + lx);
        p1 += lx;
        p2 += lx;
    } while (--h);
    return sad_end();
}

/* mm7 is accumulator, mm6 is zero */
static inline void sad_add_xy2(const UINT8 *p1, const UINT8 *p2, const UINT8 *p3)
{
    movq_m2r(*(p2 + 0), mm0);
    movq_m2r(*(p3 + 0), mm1);
    movq_r2r(mm0, mm2);
    movq_r2r(mm1, mm3);
    punpcklbw_r2r(mm6, mm0); /* extract 4 bytes low */
    punpcklbw_r2r(mm6, mm1);
    punpckhbw_r2r(mm6, mm2); /* high */
    punpckhbw_r2r(mm6, mm3); 
    paddusw_r2r(mm1, mm0);
    paddusw_r2r(mm3, mm2);

    movq_m2r(*(p2 + 1), mm1);
    movq_m2r(*(p3 + 1), mm3);
    movq_r2r(mm1, mm4);
    punpcklbw_r2r(mm6, mm1); /* low */
    punpckhbw_r2r(mm6, mm4); /* high */
    paddusw_r2r(mm1, mm0);
    paddusw_r2r(mm4, mm2);
    movq_r2r(mm3, mm4);
    punpcklbw_r2r(mm6, mm3); /* low */
    punpckhbw_r2r(mm6, mm4); /* high */
    paddusw_r2r(mm3, mm0);
    paddusw_r2r(mm4, mm2);
    
    movq_m2r(*(p1 + 0), mm1); /* mm1 : other value */
    paddusw_r2r(mm5, mm0); /* + 2 */
    paddusw_r2r(mm5, mm2); /* + 2 */
    psrlw_i2r(2, mm0);
    psrlw_i2r(2, mm2);
    packuswb_r2r(mm2, mm0); /* average is in mm0 */

    movq_r2r(mm1, mm2); 
    psubusb_r2r(mm0, mm1);
    psubusb_r2r(mm2, mm0);
    por_r2r(mm1, mm0); /* mm0 is absolute value */

    movq_r2r(mm0, mm1);
    punpcklbw_r2r(mm6, mm0);
    punpckhbw_r2r(mm6, mm1);
    paddusw_r2r(mm0, mm7);
    paddusw_r2r(mm1, mm7);
}

int pix_abs16x16_xy2_mmx(UINT8 *blk1, UINT8 *blk2, int lx, int h)
{
    const UINT8 *p1, *p2, *p3;

    p1 = blk1;
    p2 = blk2;
    p3 = blk2 + lx;
    pxor_r2r(mm7, mm7); /* mm7 is accumulator */
    pxor_r2r(mm6, mm6); /* mm7 is zero constant */
    movq_m2r(mm_wtwo, mm5); /* one constant */
    do {
        sad_add_xy2(p1, p2, p2 + lx);
        sad_add_xy2(p1 + 8, p2 + 8, p2 + 8 + lx);
        p1 += lx;
        p2 += lx;
    } while (--h);
    return sad_end();
}
