/*
  Originally provided by Intel at AP-922
  http://developer.intel.com/vtune/cbts/strmsimd/922down.htm
  (See more app notes at http://developer.intel.com/vtune/cbts/strmsimd/appnotes.htm)
  but in a limited edition.

  column code adapted from peter gubanov
  Copyright (c) 2000-2001 Peter Gubanov <peter@elecard.net.ru>
  http://www.elecard.com/peter/idct.shtml
  Rounding trick Copyright (c) 2000 Michel Lespinasse <walken@zoy.org>

  mmi port by leonvs@iae.nl
*/  
#include "../common.h"
#include "mmi.h"

#define BITS_INV_ACC	5	// 4 or 5 for IEEE
#define SHIFT_INV_ROW	(16 - BITS_INV_ACC)
#define SHIFT_INV_COL   (BITS_INV_ACC)	//(1 + BITS_INV_ACC)  no, FP15 is used

#define Rounder_0	0
#define Rounder_1	16
#define Rounder_2	32
#define Rounder_3	48
#define Rounder_4	64
#define Rounder_5	80
#define Rounder_6	96
#define Rounder_7	112

// assume SHIFT_INV_ROW == 11
static int roundertable[8][4] align16 = {
    {65535, 65535, 65535, 65535},
    { 1023,  1023,  1023,  1023},
    { 1023,  1023,  1023,  1023},
    { 1023,  1023,  1023,  1023},
    {    0,     0,     0,     0},
    { 1023,  1023,  1023,  1023},
    { 1023,  1023,  1023,  1023},
    { 1023,  1023,  1023,  1023}
};


#define TAB_i_04	0
#define TAB_i_17	64
#define TAB_i_26	128
#define TAB_i_35	192

static short rowtable[4][32] align16 = {
    {
     16384, 16384, 22725, 12873, 21407, 8867, 19266, 4520,
     16384, -16383, 19266, -22724, 8867, -21406, -4519, -12872,
     16384, -16383, 12873, 4520, -8866, 21407, -22724, 19266,
     16384, 16384, 4520, 19266, -21406, -8866, -12872, -22724},
    {
     22725, 22725, 31521, 17855, 29692, 12299, 26722, 6270,
     22725, -22724, 26722, -31520, 12299, -29691, -6269, -17854,
     22725, -22724, 17855, 6270, -12298, 29692, -31520, 26722,
     22725, 22725, 6270, 26722, -29691, -12298, -17854, -31520},
    {
     21407, 21407, 29692, 16819, 27969, 11585, 25172, 5906,
     21407, -21406, 25172, -29691, 11585, -27968, -5905, -16818,
     21407, -21406, 16819, 5906, -11584, 27969, -29691, 25172,
     21407, 21407, 5906, 25172, -27968, -11584, -16818, -29691},
    {
     19266, 19266, 26722, 15137, 25172, 10426, 22654, 5315,
     19266, -19265, 22654, -26721, 10426, -25171, -5314, -15136,
     19266, -19265, 15137, 5315, -10425, 25172, -26721, 22654,
     19266, 19266, 5315, 22654, -25171, -10425, -15136, -26721}
};


#define TG_3_16_minus_one	0
#define ONE_plus_tg_3_16	16
#define ONE_plus_tg_1_16	32
#define TG_1_16_minus_one	48
#define TG_2_16_minus_one	64
#define ONE_plus_tg_2_16	80
#define ZERO_ocos_4_16		96

#define TG1	6518
#define TG2	13573
#define TG3	21895
#define MN1	-32768
#define PL1	32768
#define CS4	23170

static short coltable[7][8] align16 = {
    { MN1,  TG3,  MN1,  TG3,  MN1,  TG3,  MN1,  TG3},
    {-TG3, -PL1, -TG3, -PL1, -TG3, -PL1, -TG3, -PL1},
    {-TG1, -PL1, -TG1, -PL1, -TG1, -PL1, -TG1, -PL1},
    { MN1,  TG1,  MN1,  TG1,  MN1,  TG1,  MN1,  TG1},
    { MN1,  TG2,  MN1,  TG2,  MN1,  TG2,  MN1,  TG2},
    {-TG2, -PL1, -TG2, -PL1, -TG2, -PL1, -TG2, -PL1},
    { CS4,    0,  CS4,    0,  CS4,    0,  CS4,    0}
};

#define	noprevh(rt, rd)


#define DCT_8_INV_ROW1(rowoff, taboff, rounder, outreg) { \
\
	lq($4, rowoff, $16);	/* r16 = x7  x6  x5  x4  x3  x2  x1  x0 */ \
	lq($24, 0+taboff, $17);	/* r17 = w19 w17 w3  w1  w18 w16 w2  w0 */ \
	pinth($16, $16, $16);	/* r16 = x7  x3  x6  x2  x5  x1  x4  x0 */ \
	phmadh($17, $16, $17);	/* r17 = (x7*w19+x3*w17)b0'' (x6*w3+x2*w1)a0'' (x5*w18+x1*w16)b0' (x4*w2+x0*w0)a0' */ \
	lq($24, 16+taboff, $18);/* r18 = w23 w21 w7  w5  w22 w20 w6  w4 */ \
	lq($24, 32+taboff, $19);/* r19 = w27 w25 w11 w9  w26 w24 w10 w8 */ \
	lq($24, 48+taboff, $20);/* r20 = w31 w29 w15 w13 w30 w28 w14 w12 */ \
	phmadh($18, $16, $18);	/* r18 = (b1'')(a1'')(b1')(a1') */ \
	pcpyud($17, $17, $21);	/* r21 = (b0'')(a0'')(b0'')(a0'') */ \
	paddw($17, $21, $17);	/* r17 = (--)(--)(b0)(a0) */ \
	phmadh($19, $16, $19);	/* r19 = (b2'')(a2'')(b2')(a2') */ \
	pcpyud($18, $18, $21);	/* r21 = (b1'')(a1'')(b1'')(a1'') */ \
	paddw($18, $21, $18);	/* r18 = (--)(--)(b1)(a1) */ \
	pcpyud($19, $19, $21);	\
	phmadh($20, $16, $20);	/* r12 = (b3'')(a3'')(b3')(a3') */ \
	lq($7, rounder, $22);	/* r22 = rounder */ \
	paddw($19, $21, $19);	/* r19 = (--)(--)(b2)(a2) */ \
	pextlw($19, $17, $16);	/* r16 = (b2)(b0)(a2)(a0) */ \
	pcpyud($20, $20, $21);	\
	paddw($20, $21, $20);	/* r20 = (--)(--)(b3)(a3) */ \
	pextlw($20, $18, $17);	/* r17 = (b3)(b1)(a3)(a1) */ \
	pextlw($17, $16, $20);	/* r20 = (a3)(a2)(a1)(a0)" */ \
	pextuw($17, $16, $21);	/* r21 = (b3)(b2)(b1)(b0) */ \
	paddw($20, $22, $20);	/* r20 = (a3)(a2)(a1)(a0) */\
	paddw($20, $21, $17);	/* r17 = ()()()(a0+b0) */ \
	psubw($20, $21, $18);	/* r18 = ()()()(a0-b0) */ \
	psraw($17, SHIFT_INV_ROW, $17); /* r17 = (y3 y2 y1 y0) */ \
	psraw($18, SHIFT_INV_ROW, $18);	/* r18 = (y4 y5 y6 y7) */ \
	ppach($18, $17, outreg);/* out = y4 y5 y6 y7 y3 y2 y1 y0  Note order */ \
}


#define DCT_8_INV_COL4(pextop, blkoff, revop) { \
	lq($24, TG_3_16_minus_one, $2);	/* r2  = (tn3)(-1) x 4 */	\
	pextop($11, $13, $3);		/* r3  = (x3)(x5) x 4 */	\
	lq($24, ONE_plus_tg_3_16, $16);	/* r16 = -((+1)(tn3)) x 4 */	\
	phmadh($3, $2, $17);		/* r17 = (tm35) x 4 */		\
	lq($24, ONE_plus_tg_1_16, $2);	/* r2  = -((+1)(tn1)) x 4 */	\
	phmadh($3, $16, $18);		/* r18 = -(tp35) x 4 */		\
	lq($24, TG_1_16_minus_one, $16);/* r16 = (tn1)(-1) x 4 */	\
	pextop($9, $15, $3);		/* r3  = (x1)(x7) x 4 */	\
	phmadh($3, $2, $19);						\
	lq($24, ZERO_ocos_4_16, $2);	/* r2  = (0)(cos4) x 4 */	\
	phmadh($3, $16, $20);		/* r20 = (tm17) x 4 */		\
	psubw($0, $19, $19);		/* r19 = (tp17) x 4 */		\
	paddw($19, $18, $3);		/* r3  = t1 */			\
	paddw($20, $17, $16);		/* r16 = t2 */			\
	psubw($20, $17, $23);		/* r23 = b3 */			\
	psubw($19, $18, $20);		/* r20 = b0 */			\
	paddw($3, $16, $17);		/* (t1+t2) */			\
	psubw($3, $16, $18);		/* (t1-t2) */			\
	psraw($17, 15, $17);						\
	lq($24, TG_2_16_minus_one, $3);	/* r3  = (tn2)(-1) x 4 */	\
	pmulth($17, $2, $21);		/* r21 = b1 */			\
	psraw($18, 15, $18);						\
	lq($24, ONE_plus_tg_2_16, $16);	/* r16 = -((+1)(tn2)) x 4 */	\
	pmulth($18, $2, $22);		/* r22 = b2 */			\
\
	pextop($10, $14, $2);		/* r2  = (x2)(x6) x 4 */	\
	phmadh($2, $3, $18);		/* r18 = (tm26) x 4 */		\
	phmadh($2, $16, $19);		/* r19 = -(tp26) x 4 */		\
	pextop($8, $0, $17);		/* r17 = (x0)(0) x 4 */		\
	psraw($17, 1, $17);						\
	pextop($12, $0, $16);		/* r16 = (x4)(0) x 4 */		\
	psraw($16, 1, $16);						\
	paddw($17, $16, $2);		/* r2  = tp04 */		\
	psubw($17, $16, $3);		/* r3  = tm04 */		\
	psubw($2, $19, $16);		/* r16 = a0 */			\
	paddw($3, $18, $17);		/* r17 = a1 */			\
	psubw($3, $18, $18);		/* r18 = a2 */			\
	paddw($2, $19, $19);		/* r19 = a3 */			\
\
	paddw($16, $20, $2);	/* y0  a0+b0 */		\
	psubw($16, $20, $16);	/* y7  a0-b0 */		\
	psraw($2, SHIFT_INV_COL+16, $2);		\
	psraw($16, SHIFT_INV_COL+16, $16);		\
	ppach($0, $2, $2);				\
	ppach($0, $16, $16);				\
	revop($2, $2);					\
	revop($16, $16);				\
	sd3(2, 0+blkoff, 4); 				\
	sd3(16, 112+blkoff, 4); 			\
\
	paddw($17, $21, $3);	/* y1  a1+b1 */		\
	psubw($17, $21, $17);	/* y6  a1-b1 */		\
	psraw($3, SHIFT_INV_COL+16, $3);		\
	psraw($17, SHIFT_INV_COL+16, $17);		\
	ppach($0, $3, $3);				\
	ppach($0, $17, $17);			\
	revop($3, $3);				\
	revop($17, $17);			\
	sd3(3, 16+blkoff, 4);			\
	sd3(17, 96+blkoff, 4);			\
\
	paddw($18, $22, $2);	/* y2  a2+b2 */	\
	psubw($18, $22, $18);	/* y5  a2-b2 */	\
	psraw($2, SHIFT_INV_COL+16, $2);	\
	psraw($18, SHIFT_INV_COL+16, $18);	\
	ppach($0, $2, $2);			\
	ppach($0, $18, $18);			\
	revop($2, $2);				\
	revop($18, $18);			\
	sd3(2, 32+blkoff, 4);			\
	sd3(18, 80+blkoff, 4);			\
\
	paddw($19, $23, $3);	/* y3  a3+b3 */	\
	psubw($19, $23, $19);	/* y4  a3-b3 */	\
	psraw($3, SHIFT_INV_COL+16, $3);	\
	psraw($19, SHIFT_INV_COL+16, $19);	\
	ppach($0, $3, $3);			\
	ppach($0, $19, $19);			\
	revop($3, $3);				\
	revop($19, $19);			\
	sd3(3, 48+blkoff, 4);			\
	sd3(19, 64+blkoff, 4);			\
}


void ff_mmi_idct(int16_t * block)
{
    /* $4 = block */
    __asm__ __volatile__("la $24, %0"::"m"(rowtable[0][0]));
    __asm__ __volatile__("la $7, %0"::"m"(roundertable[0][0]));
    DCT_8_INV_ROW1(0, TAB_i_04, Rounder_0, $8);
    DCT_8_INV_ROW1(16, TAB_i_17, Rounder_1, $9);
    DCT_8_INV_ROW1(32, TAB_i_26, Rounder_2, $10);
    DCT_8_INV_ROW1(48, TAB_i_35, Rounder_3, $11);
    DCT_8_INV_ROW1(64, TAB_i_04, Rounder_4, $12);
    DCT_8_INV_ROW1(80, TAB_i_35, Rounder_5, $13);
    DCT_8_INV_ROW1(96, TAB_i_26, Rounder_6, $14);
    DCT_8_INV_ROW1(112, TAB_i_17, Rounder_7, $15);

    __asm__ __volatile__("la $24, %0"::"m"(coltable[0][0]));
    DCT_8_INV_COL4(pextlh, 0, noprevh);
    DCT_8_INV_COL4(pextuh, 8, prevh);

    //let savedtemp regs be saved
    __asm__ __volatile__(" ":::"$16", "$17", "$18", "$19", "$20", "$21",
			 "$22", "$23");
}
