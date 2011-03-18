/*
 * Originally provided by Intel at Application Note AP-922.
 *
 * Column code adapted from Peter Gubanov.
 * Copyright (c) 2000-2001 Peter Gubanov <peter@elecard.net.ru>
 * http://www.elecard.com/peter/idct.shtml
 * rounding trick copyright (c) 2000 Michel Lespinasse <walken@zoy.org>
 *
 * MMI port and (c) 2002 by Leon van Stuivenberg
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/common.h"
#include "libavcodec/dsputil.h"
#include "mmi.h"

#define BITS_INV_ACC    5       // 4 or 5 for IEEE
#define SHIFT_INV_ROW   (16 - BITS_INV_ACC)
#define SHIFT_INV_COL   (1 + BITS_INV_ACC)

#define TG1             6518
#define TG2             13573
#define TG3             21895
#define CS4             23170

#define ROUNDER_0       0
#define ROUNDER_1       16

#define TAB_i_04        (32+0)
#define TAB_i_17        (32+64)
#define TAB_i_26        (32+128)
#define TAB_i_35        (32+192)

#define TG_1_16         (32+256+0)
#define TG_2_16         (32+256+16)
#define TG_3_16         (32+256+32)
#define COS_4_16        (32+256+48)

#define CLIPMAX         (32+256+64+0)

static short consttable[] align16 = {
/* rounder 0*/  // assume SHIFT_INV_ROW == 11
 0x3ff, 1, 0x3ff, 1, 0x3ff, 1, 0x3ff, 1,
/* rounder 1*/
 0x3ff, 0, 0x3ff, 0, 0x3ff, 0, 0x3ff, 0,
/* row 0/4*/
 16384,  21407, -16384, -21407,  22725,  19266, -22725, -12873,
  8867,  16384,   8867,  16384,   4520,  12873,  -4520,  19266,
 16384,  -8867,  16384,  -8867,  12873, -22725,  19266, -22725,
 21407, -16384, -21407,  16384,  19266,   4520, -12873,   4520,
/* row 1/7*/
 22725,  29692, -22725, -29692,  31521,  26722, -31521, -17855,
 12299,  22725,  12299,  22725,   6270,  17855,  -6270,  26722,
 22725, -12299,  22725, -12299,  17855, -31521,  26722, -31521,
 29692, -22725, -29692,  22725,  26722,   6270, -17855,   6270,
/* row 2/6*/
 21407,  27969, -21407, -27969,  29692,  25172, -29692, -16819,
 11585,  21407,  11585,  21407,   5906,  16819,  -5906,  25172,
 21407, -11585,  21407, -11585,  16819, -29692,  25172, -29692,
 27969, -21407, -27969,  21407,  25172,   5906, -16819,   5906,
/*row 3/5*/
 19266,  25172, -19266, -25172,  26722,  22654, -26722, -15137,
 10426,  19266,  10426,  19266,   5315,  15137,  -5315,  22654,
 19266, -10426,  19266, -10426,  15137, -26722,  22654, -26722,
 25172, -19266, -25172,  19266,  22654,   5315, -15137,   5315,
/*column constants*/
 TG1, TG1, TG1, TG1, TG1, TG1, TG1, TG1,
 TG2, TG2, TG2, TG2, TG2, TG2, TG2, TG2,
 TG3, TG3, TG3, TG3, TG3, TG3, TG3, TG3,
 CS4, CS4, CS4, CS4, CS4, CS4, CS4, CS4,
/* clamp */
 255, 255, 255, 255, 255, 255, 255, 255
};


#define DCT_8_INV_ROW1(blk, rowoff, taboff, rnd, outreg) { \
        lq(blk, rowoff, $16);   /* r16 = x7  x5  x3  x1  x6  x4  x2  x0 */ \
        /*slot*/ \
        lq($24, 0+taboff, $17); /* r17 = w */ \
        /*delay slot $16*/ \
        lq($24, 16+taboff, $18);/* r18 = w */ \
        prevh($16, $2);         /* r2  = x1  x3  x5  x7  x0  x2  x4  x6 */ \
        lq($24, 32+taboff, $19);/* r19 = w */ \
        phmadh($17, $16, $17);  /* r17 = b1"b0'a1"a0' */ \
        lq($24, 48+taboff, $20);/* r20 = w */ \
        phmadh($18, $2, $18);   /* r18 = b1'b0"a1'a0" */ \
        phmadh($19, $16, $19);  /* r19 = b3"b2'a3"a2' */ \
        phmadh($20, $2, $20);   /* r20 = b3'b2"a3'a2" */ \
        paddw($17, $18, $17);   /* r17 = (b1)(b0)(a1)(a0) */ \
        paddw($19, $20, $19);   /* r19 = (b3)(b2)(a3)(a2) */ \
        pcpyld($19, $17, $18);  /* r18 = (a3)(a2)(a1)(a0) */ \
        pcpyud($17, $19, $20);  /* r20 = (b3)(b2)(b1)(b0) */ \
        paddw($18, rnd, $18);   /* r18 = (a3)(a2)(a1)(a0) */\
        paddw($18, $20, $17);   /* r17 = ()()()(a0+b0) */ \
        psubw($18, $20, $20);   /* r20 = ()()()(a0-b0) */ \
        psraw($17, SHIFT_INV_ROW, $17); /* r17 = (y3 y2 y1 y0) */ \
        psraw($20, SHIFT_INV_ROW, $20); /* r20 = (y4 y5 y6 y7) */ \
        ppach($20, $17, outreg);/* out = y4 y5 y6 y7 y3 y2 y1 y0  Note order */ \
\
        prevh(outreg, $2);        \
        pcpyud($2, $2, $2);        \
        pcpyld($2, outreg, outreg);        \
}


#define DCT_8_INV_COL8() \
\
        lq($24, TG_3_16, $2);   /* r2  = tn3 */         \
\
        pmulth($11, $2, $17);   /* r17 = x3 * tn3 (6420) */ \
        psraw($17, 15, $17);    \
        pmfhl_uw($3);           /* r3  = 7531 */        \
        psraw($3, 15, $3);      \
        pinteh($3, $17, $17);   /* r17 = x3 * tn3 */    \
        psubh($17, $13, $17);   /* r17 = tm35 */        \
\
        pmulth($13, $2, $18);   /* r18 = x5 * tn3 (6420) */ \
        psraw($18, 15, $18);    \
        pmfhl_uw($3);           /* r3  = 7531 */        \
        psraw($3, 15, $3);      \
        pinteh($3, $18, $18);   /* r18 = x5 * tn3 */    \
        paddh($18, $11, $18);   /* r18 = tp35 */        \
\
        lq($24, TG_1_16, $2);   /* r2  = tn1 */         \
\
        pmulth($15, $2, $19);   /* r19 = x7 * tn1 (6420) */ \
        psraw($19, 15, $19);    \
        pmfhl_uw($3);           /* r3  = 7531 */        \
        psraw($3, 15, $3);      \
        pinteh($3, $19, $19);   /* r19 = x7 * tn1 */    \
        paddh($19, $9, $19);    /* r19 = tp17 */        \
\
        pmulth($9, $2, $20);    /* r20 = x1 * tn1 (6420) */ \
        psraw($20, 15, $20);    \
        pmfhl_uw($3);           /* r3  = 7531 */        \
        psraw($3, 15, $3);      \
        pinteh($3, $20, $20);   /* r20 = x1 * tn1 */    \
        psubh($20, $15, $20);   /* r20 = tm17 */        \
\
        psubh($19, $18, $3);    /* r3  = t1 */          \
        paddh($20, $17, $16);   /* r16 = t2 */          \
        psubh($20, $17, $23);   /* r23 = b3 */          \
        paddh($19, $18, $20);   /* r20 = b0 */          \
\
        lq($24, COS_4_16, $2);  /* r2  = cs4 */         \
\
        paddh($3, $16, $21);    /* r21 = t1+t2 */       \
        psubh($3, $16, $22);    /* r22 = t1-t2 */       \
\
        pmulth($21, $2, $21);   /* r21 = cs4 * (t1+t2) 6420 */ \
        psraw($21, 15, $21);    \
        pmfhl_uw($3);           /* r3  = 7531 */        \
        psraw($3, 15, $3);      \
        pinteh($3, $21, $21);   /* r21 = b1 */          \
\
        pmulth($22, $2, $22);   /* r22 = cs4 * (t1-t2) 6420 */ \
        psraw($22, 15, $22);    \
        pmfhl_uw($3);           /* r3  = 7531 */        \
        psraw($3, 15, $3);      \
        pinteh($3, $22, $22);   /* r22 = b2 */          \
\
        lq($24, TG_2_16, $2);   /* r2  = tn2 */         \
\
        pmulth($10, $2, $17);   /* r17 = x2 * tn2 (6420) */ \
        psraw($17, 15, $17);    \
        pmfhl_uw($3);           /* r3  = 7531 */        \
        psraw($3, 15, $3);      \
        pinteh($3, $17, $17);   /* r17 = x3 * tn3 */    \
        psubh($17, $14, $17);   /* r17 = tm26 */        \
\
        pmulth($14, $2, $18);   /* r18 = x6 * tn2 (6420) */ \
        psraw($18, 15, $18);    \
        pmfhl_uw($3);           /* r3  = 7531 */        \
        psraw($3, 15, $3);      \
        pinteh($3, $18, $18);   /* r18 = x6 * tn2 */    \
        paddh($18, $10, $18);   /* r18 = tp26 */        \
\
        paddh($8, $12, $2);     /* r2  = tp04 */        \
        psubh($8, $12, $3);     /* r3  = tm04 */        \
\
        paddh($2, $18, $16);    /* r16 = a0 */          \
        psubh($2, $18, $19);    /* r19 = a3 */          \
        psubh($3, $17, $18);    /* r18 = a2 */          \
        paddh($3, $17, $17);    /* r17 = a1 */


#define DCT_8_INV_COL8_STORE(blk) \
\
        paddh($16, $20, $2);    /* y0  a0+b0 */ \
        psubh($16, $20, $16);   /* y7  a0-b0 */ \
        psrah($2, SHIFT_INV_COL, $2);           \
        psrah($16, SHIFT_INV_COL, $16);         \
        sq($2, 0, blk);                         \
        sq($16, 112, blk);                      \
\
        paddh($17, $21, $3);    /* y1  a1+b1 */ \
        psubh($17, $21, $17);   /* y6  a1-b1 */ \
        psrah($3, SHIFT_INV_COL, $3);           \
        psrah($17, SHIFT_INV_COL, $17);         \
        sq($3, 16, blk);                        \
        sq($17, 96, blk);                       \
\
        paddh($18, $22, $2);    /* y2  a2+b2 */ \
        psubh($18, $22, $18);   /* y5  a2-b2 */ \
        psrah($2, SHIFT_INV_COL, $2);           \
        psrah($18, SHIFT_INV_COL, $18);         \
        sq($2, 32, blk);                        \
        sq($18, 80, blk);                       \
\
        paddh($19, $23, $3);    /* y3  a3+b3 */ \
        psubh($19, $23, $19);   /* y4  a3-b3 */ \
        psrah($3, SHIFT_INV_COL, $3);           \
        psrah($19, SHIFT_INV_COL, $19);         \
        sq($3, 48, blk);                        \
        sq($19, 64, blk);



#define DCT_8_INV_COL8_PMS() \
        paddh($16, $20, $2);    /* y0  a0+b0 */ \
        psubh($16, $20, $20);   /* y7  a0-b0 */ \
        psrah($2, SHIFT_INV_COL, $16);          \
        psrah($20, SHIFT_INV_COL, $20);         \
\
        paddh($17, $21, $3);    /* y1  a1+b1 */ \
        psubh($17, $21, $21);   /* y6  a1-b1 */ \
        psrah($3, SHIFT_INV_COL, $17);          \
        psrah($21, SHIFT_INV_COL, $21);         \
\
        paddh($18, $22, $2);    /* y2  a2+b2 */ \
        psubh($18, $22, $22);   /* y5  a2-b2 */ \
        psrah($2, SHIFT_INV_COL, $18);          \
        psrah($22, SHIFT_INV_COL, $22);         \
\
        paddh($19, $23, $3);    /* y3  a3+b3 */ \
        psubh($19, $23, $23);   /* y4  a3-b3 */ \
        psrah($3, SHIFT_INV_COL, $19);          \
        psrah($23, SHIFT_INV_COL, $23);

#define PUT(rs)                 \
        pminh(rs, $11, $2);     \
        pmaxh($2, $0, $2);      \
        ppacb($0, $2, $2);      \
        sd3(2, 0, 4);           \
        __asm__ volatile ("add $4, $5, $4");

#define DCT_8_INV_COL8_PUT() \
        PUT($16);        \
        PUT($17);        \
        PUT($18);        \
        PUT($19);        \
        PUT($23);        \
        PUT($22);        \
        PUT($21);        \
        PUT($20);

#define ADD(rs)          \
        ld3(4, 0, 2);        \
        pextlb($0, $2, $2);  \
        paddh($2, rs, $2);   \
        pminh($2, $11, $2);  \
        pmaxh($2, $0, $2);   \
        ppacb($0, $2, $2);   \
        sd3(2, 0, 4); \
        __asm__ volatile ("add $4, $5, $4");

/*fixme: schedule*/
#define DCT_8_INV_COL8_ADD() \
        ADD($16);        \
        ADD($17);        \
        ADD($18);        \
        ADD($19);        \
        ADD($23);        \
        ADD($22);        \
        ADD($21);        \
        ADD($20);


void ff_mmi_idct(int16_t * block)
{
        /* $4 = block */
        __asm__ volatile("la $24, %0"::"m"(consttable[0]));
        lq($24, ROUNDER_0, $8);
        lq($24, ROUNDER_1, $7);
        DCT_8_INV_ROW1($4, 0, TAB_i_04, $8, $8);
        DCT_8_INV_ROW1($4, 16, TAB_i_17, $7, $9);
        DCT_8_INV_ROW1($4, 32, TAB_i_26, $7, $10);
        DCT_8_INV_ROW1($4, 48, TAB_i_35, $7, $11);
        DCT_8_INV_ROW1($4, 64, TAB_i_04, $7, $12);
        DCT_8_INV_ROW1($4, 80, TAB_i_35, $7, $13);
        DCT_8_INV_ROW1($4, 96, TAB_i_26, $7, $14);
        DCT_8_INV_ROW1($4, 112, TAB_i_17, $7, $15);
        DCT_8_INV_COL8();
        DCT_8_INV_COL8_STORE($4);

        //let savedtemp regs be saved
        __asm__ volatile(" ":::"$16", "$17", "$18", "$19", "$20", "$21", "$22", "$23");
}


void ff_mmi_idct_put(uint8_t *dest, int line_size, DCTELEM *block)
{
        /* $4 = dest, $5 = line_size, $6 = block */
        __asm__ volatile("la $24, %0"::"m"(consttable[0]));
        lq($24, ROUNDER_0, $8);
        lq($24, ROUNDER_1, $7);
        DCT_8_INV_ROW1($6, 0, TAB_i_04, $8, $8);
        DCT_8_INV_ROW1($6, 16, TAB_i_17, $7, $9);
        DCT_8_INV_ROW1($6, 32, TAB_i_26, $7, $10);
        DCT_8_INV_ROW1($6, 48, TAB_i_35, $7, $11);
        DCT_8_INV_ROW1($6, 64, TAB_i_04, $7, $12);
        DCT_8_INV_ROW1($6, 80, TAB_i_35, $7, $13);
        DCT_8_INV_ROW1($6, 96, TAB_i_26, $7, $14);
        DCT_8_INV_ROW1($6, 112, TAB_i_17, $7, $15);
        DCT_8_INV_COL8();
        lq($24, CLIPMAX, $11);
        DCT_8_INV_COL8_PMS();
        DCT_8_INV_COL8_PUT();

        //let savedtemp regs be saved
        __asm__ volatile(" ":::"$16", "$17", "$18", "$19", "$20", "$21", "$22", "$23");
}


void ff_mmi_idct_add(uint8_t *dest, int line_size, DCTELEM *block)
{
        /* $4 = dest, $5 = line_size, $6 = block */
        __asm__ volatile("la $24, %0"::"m"(consttable[0]));
        lq($24, ROUNDER_0, $8);
        lq($24, ROUNDER_1, $7);
        DCT_8_INV_ROW1($6, 0, TAB_i_04, $8, $8);
        DCT_8_INV_ROW1($6, 16, TAB_i_17, $7, $9);
        DCT_8_INV_ROW1($6, 32, TAB_i_26, $7, $10);
        DCT_8_INV_ROW1($6, 48, TAB_i_35, $7, $11);
        DCT_8_INV_ROW1($6, 64, TAB_i_04, $7, $12);
        DCT_8_INV_ROW1($6, 80, TAB_i_35, $7, $13);
        DCT_8_INV_ROW1($6, 96, TAB_i_26, $7, $14);
        DCT_8_INV_ROW1($6, 112, TAB_i_17, $7, $15);
        DCT_8_INV_COL8();
        lq($24, CLIPMAX, $11);
        DCT_8_INV_COL8_PMS();
        DCT_8_INV_COL8_ADD();

        //let savedtemp regs be saved
        __asm__ volatile(" ":::"$16", "$17", "$18", "$19", "$20", "$21", "$22", "$23");
}

