/*
 * MMX optimized forward DCT
 * The gcc porting is Copyright (c) 2001 Fabrice Bellard.
 *
 * from  fdctam32.c - AP922 MMX(3D-Now) forward-DCT
 * 
 *  Intel Application Note AP-922 - fast, precise implementation of DCT
 *        http://developer.intel.com/vtune/cbts/appnotes.htm
 */
#include "../common.h"
#include "mmx.h"

#define ATTR_ALIGN(align) __attribute__ ((__aligned__ (align)))

//////////////////////////////////////////////////////////////////////
//
// constants for the forward DCT
// -----------------------------
//
// Be sure to check that your compiler is aligning all constants to QWORD
// (8-byte) memory boundaries!  Otherwise the unaligned memory access will
// severely stall MMX execution.
//
//////////////////////////////////////////////////////////////////////

#define BITS_FRW_ACC	3 //; 2 or 3 for accuracy
#define SHIFT_FRW_COL	BITS_FRW_ACC
#define SHIFT_FRW_ROW	(BITS_FRW_ACC + 17 - 3)
//#define RND_FRW_ROW		(262144 * (BITS_FRW_ACC - 1)) //; 1 << (SHIFT_FRW_ROW-1)
#define RND_FRW_ROW		(1 << (SHIFT_FRW_ROW-1))
//#define RND_FRW_COL		(2 * (BITS_FRW_ACC - 1)) //; 1 << (SHIFT_FRW_COL-1)
#define RND_FRW_COL		(1 << (SHIFT_FRW_COL-1))

//concatenated table, for forward DCT transformation
static const int16_t fdct_tg_all_16[] ATTR_ALIGN(8) = {
    13036, 13036, 13036, 13036,		// tg * (2<<16) + 0.5
    27146, 27146, 27146, 27146,		// tg * (2<<16) + 0.5
    -21746, -21746, -21746, -21746,	// tg * (2<<16) + 0.5
};
static const int16_t cos_4_16[4] ATTR_ALIGN(8) = {
    -19195, -19195, -19195, -19195,	//cos * (2<<16) + 0.5
};

static const int16_t ocos_4_16[4] ATTR_ALIGN(8) = {
    23170, 23170, 23170, 23170,	//cos * (2<<15) + 0.5
};

static const long long  fdct_one_corr ATTR_ALIGN(8) = 0x0001000100010001LL;
static const long fdct_r_row[2] ATTR_ALIGN(8) = {RND_FRW_ROW, RND_FRW_ROW };

static const int16_t tab_frw_01234567[] ATTR_ALIGN(8) = {  // forward_dct coeff table
    //row0
    16384, 16384, 21407, -8867,     //    w09 w01 w08 w00
    16384, 16384, 8867, -21407,     //    w13 w05 w12 w04
    16384, -16384, 8867, 21407,     //    w11 w03 w10 w02
    -16384, 16384, -21407, -8867,   //    w15 w07 w14 w06
    22725, 12873, 19266, -22725,    //    w22 w20 w18 w16
    19266, 4520, -4520, -12873,     //    w23 w21 w19 w17
    12873, 4520, 4520, 19266,       //    w30 w28 w26 w24
    -22725, 19266, -12873, -22725,  //    w31 w29 w27 w25

    //row1
    22725, 22725, 29692, -12299,    //    w09 w01 w08 w00
    22725, 22725, 12299, -29692,    //    w13 w05 w12 w04
    22725, -22725, 12299, 29692,    //    w11 w03 w10 w02
    -22725, 22725, -29692, -12299,  //    w15 w07 w14 w06
    31521, 17855, 26722, -31521,    //    w22 w20 w18 w16
    26722, 6270, -6270, -17855,     //    w23 w21 w19 w17
    17855, 6270, 6270, 26722,       //    w30 w28 w26 w24
    -31521, 26722, -17855, -31521,  //    w31 w29 w27 w25

    //row2
    21407, 21407, 27969, -11585,    //    w09 w01 w08 w00
    21407, 21407, 11585, -27969,    //    w13 w05 w12 w04
    21407, -21407, 11585, 27969,    //    w11 w03 w10 w02
    -21407, 21407, -27969, -11585,  //    w15 w07 w14 w06
    29692, 16819, 25172, -29692,    //    w22 w20 w18 w16
    25172, 5906, -5906, -16819,     //    w23 w21 w19 w17
    16819, 5906, 5906, 25172,       //    w30 w28 w26 w24
    -29692, 25172, -16819, -29692,  //    w31 w29 w27 w25

    //row3
    19266, 19266, 25172, -10426,    //    w09 w01 w08 w00
    19266, 19266, 10426, -25172,    //    w13 w05 w12 w04
    19266, -19266, 10426, 25172,    //    w11 w03 w10 w02
    -19266, 19266, -25172, -10426,  //    w15 w07 w14 w06, 
    26722, 15137, 22654, -26722,    //    w22 w20 w18 w16
    22654, 5315, -5315, -15137,     //    w23 w21 w19 w17
    15137, 5315, 5315, 22654,       //    w30 w28 w26 w24
    -26722, 22654, -15137, -26722,  //    w31 w29 w27 w25, 

    //row4
    16384, 16384, 21407, -8867,     //    w09 w01 w08 w00
    16384, 16384, 8867, -21407,     //    w13 w05 w12 w04
    16384, -16384, 8867, 21407,     //    w11 w03 w10 w02
    -16384, 16384, -21407, -8867,   //    w15 w07 w14 w06
    22725, 12873, 19266, -22725,    //    w22 w20 w18 w16
    19266, 4520, -4520, -12873,     //    w23 w21 w19 w17
    12873, 4520, 4520, 19266,       //    w30 w28 w26 w24
    -22725, 19266, -12873, -22725,  //    w31 w29 w27 w25 

    //row5
    19266, 19266, 25172, -10426,    //    w09 w01 w08 w00
    19266, 19266, 10426, -25172,    //    w13 w05 w12 w04
    19266, -19266, 10426, 25172,    //    w11 w03 w10 w02
    -19266, 19266, -25172, -10426,  //    w15 w07 w14 w06
    26722, 15137, 22654, -26722,    //    w22 w20 w18 w16
    22654, 5315, -5315, -15137,     //    w23 w21 w19 w17
    15137, 5315, 5315, 22654,       //    w30 w28 w26 w24
    -26722, 22654, -15137, -26722,  //    w31 w29 w27 w25

    //row6
    21407, 21407, 27969, -11585,    //    w09 w01 w08 w00
    21407, 21407, 11585, -27969,    //    w13 w05 w12 w04
    21407, -21407, 11585, 27969,    //    w11 w03 w10 w02
    -21407, 21407, -27969, -11585,  //    w15 w07 w14 w06, 
    29692, 16819, 25172, -29692,    //    w22 w20 w18 w16
    25172, 5906, -5906, -16819,     //    w23 w21 w19 w17
    16819, 5906, 5906, 25172,       //    w30 w28 w26 w24
    -29692, 25172, -16819, -29692,  //    w31 w29 w27 w25, 

    //row7
    22725, 22725, 29692, -12299,    //    w09 w01 w08 w00
    22725, 22725, 12299, -29692,    //    w13 w05 w12 w04
    22725, -22725, 12299, 29692,    //    w11 w03 w10 w02
    -22725, 22725, -29692, -12299,  //    w15 w07 w14 w06, 
    31521, 17855, 26722, -31521,    //    w22 w20 w18 w16
    26722, 6270, -6270, -17855,     //    w23 w21 w19 w17
    17855, 6270, 6270, 26722,       //    w30 w28 w26 w24
    -31521, 26722, -17855, -31521   //    w31 w29 w27 w25
};


static inline void fdct_col(const int16_t *in, int16_t *out, int offset)
{
    movq_m2r(*(in + offset + 1 * 8), mm0);
    movq_m2r(*(in + offset + 6 * 8), mm1);
    movq_r2r(mm0, mm2);
    movq_m2r(*(in + offset + 2 * 8), mm3);
    paddsw_r2r(mm1, mm0);
    movq_m2r(*(in + offset + 5 * 8), mm4);
    psllw_i2r(SHIFT_FRW_COL, mm0);
    movq_m2r(*(in + offset + 0 * 8), mm5);
    paddsw_r2r(mm3, mm4);
    paddsw_m2r(*(in + offset + 7 * 8), mm5);
    psllw_i2r(SHIFT_FRW_COL, mm4);
    movq_r2r(mm0, mm6);
    psubsw_r2r(mm1, mm2);
    movq_m2r(*(fdct_tg_all_16 + 4), mm1);
    psubsw_r2r(mm4, mm0);
    movq_m2r(*(in + offset + 3 * 8), mm7);
    pmulhw_r2r(mm0, mm1);
    paddsw_m2r(*(in + offset + 4 * 8), mm7);
    psllw_i2r(SHIFT_FRW_COL, mm5);
    paddsw_r2r(mm4, mm6);
    psllw_i2r(SHIFT_FRW_COL, mm7);
    movq_r2r(mm5, mm4);
    psubsw_r2r(mm7, mm5);
    paddsw_r2r(mm5, mm1);
    paddsw_r2r(mm7, mm4);
    por_m2r(fdct_one_corr, mm1);
    psllw_i2r(SHIFT_FRW_COL + 1, mm2);
    pmulhw_m2r(*(fdct_tg_all_16 + 4), mm5);
    movq_r2r(mm4, mm7);
    psubsw_m2r(*(in + offset + 5 * 8), mm3);
    psubsw_r2r(mm6, mm4);
    movq_r2m(mm1, *(out + offset + 2 * 8));
    paddsw_r2r(mm6, mm7);
    movq_m2r(*(in + offset + 3 * 8), mm1);
    psllw_i2r(SHIFT_FRW_COL + 1, mm3);
    psubsw_m2r(*(in + offset + 4 * 8), mm1);
    movq_r2r(mm2, mm6);
    movq_r2m(mm4, *(out + offset + 4 * 8));
    paddsw_r2r(mm3, mm2);
    pmulhw_m2r(*ocos_4_16, mm2);
    psubsw_r2r(mm3, mm6);
    pmulhw_m2r(*ocos_4_16, mm6);
    psubsw_r2r(mm0, mm5);
    por_m2r(fdct_one_corr, mm5);
    psllw_i2r(SHIFT_FRW_COL, mm1);
    por_m2r(fdct_one_corr, mm2);
    movq_r2r(mm1, mm4);
    movq_m2r(*(in + offset + 0 * 8), mm3);
    paddsw_r2r(mm6, mm1);
    psubsw_m2r(*(in + offset + 7 * 8), mm3);
    psubsw_r2r(mm6, mm4);
    movq_m2r(*(fdct_tg_all_16 + 0), mm0);
    psllw_i2r(SHIFT_FRW_COL, mm3);
    movq_m2r(*(fdct_tg_all_16 + 8), mm6);
    pmulhw_r2r(mm1, mm0);
    movq_r2m(mm7, *(out + offset + 0 * 8));
    pmulhw_r2r(mm4, mm6);
    movq_r2m(mm5, *(out + offset + 6 * 8));
    movq_r2r(mm3, mm7);
    movq_m2r(*(fdct_tg_all_16 + 8), mm5);
    psubsw_r2r(mm2, mm7);
    paddsw_r2r(mm2, mm3);
    pmulhw_r2r(mm7, mm5);
    paddsw_r2r(mm3, mm0);
    paddsw_r2r(mm4, mm6);
    pmulhw_m2r(*(fdct_tg_all_16 + 0), mm3);
    por_m2r(fdct_one_corr, mm0);
    paddsw_r2r(mm7, mm5);
    psubsw_r2r(mm6, mm7);
    movq_r2m(mm0, *(out + offset + 1 * 8));
    paddsw_r2r(mm4, mm5);
    movq_r2m(mm7, *(out + offset + 3 * 8));
    psubsw_r2r(mm1, mm3);
    movq_r2m(mm5, *(out + offset + 5 * 8));
    movq_r2m(mm3, *(out + offset + 7 * 8));
}

static inline void fdct_row(const int16_t *in, int16_t *out, const int16_t *table)
{
    movd_m2r(*(in + 6), mm5);
    punpcklwd_m2r(*(in + 4), mm5);
    movq_r2r(mm5, mm2);
    psrlq_i2r(0x20, mm5);
    movq_m2r(*(in + 0), mm0);
    punpcklwd_r2r(mm2, mm5);
    movq_r2r(mm0, mm1);
    paddsw_r2r(mm5, mm0);
    psubsw_r2r(mm5, mm1);
    movq_r2r(mm0, mm2);
    punpcklwd_r2r(mm1, mm0);
    punpckhwd_r2r(mm1, mm2);
    movq_r2r(mm2, mm1);
    movq_r2r(mm0, mm2);
    movq_m2r(*(table + 0), mm3);
    punpcklwd_r2r(mm1, mm0);
    movq_r2r(mm0, mm5);
    punpckldq_r2r(mm0, mm0);
    movq_m2r(*(table + 4), mm4);
    punpckhwd_r2r(mm1, mm2);
    pmaddwd_r2r(mm0, mm3);
    movq_r2r(mm2, mm6);
    movq_m2r(*(table + 16), mm1);
    punpckldq_r2r(mm2, mm2);
    pmaddwd_r2r(mm2, mm4);
    punpckhdq_r2r(mm5, mm5);
    pmaddwd_m2r(*(table + 8), mm0);
    punpckhdq_r2r(mm6, mm6);
    movq_m2r(*(table + 20), mm7);
    pmaddwd_r2r(mm5, mm1);
    paddd_m2r(*fdct_r_row, mm3);
    pmaddwd_r2r(mm6, mm7);
    pmaddwd_m2r(*(table + 12), mm2);
    paddd_r2r(mm4, mm3);
    pmaddwd_m2r(*(table + 24), mm5);
    pmaddwd_m2r(*(table + 28), mm6);
    paddd_r2r(mm7, mm1);
    paddd_m2r(*fdct_r_row, mm0);
    psrad_i2r(SHIFT_FRW_ROW, mm3);
    paddd_m2r(*fdct_r_row, mm1);
    paddd_r2r(mm2, mm0);
    paddd_m2r(*fdct_r_row, mm5);
    psrad_i2r(SHIFT_FRW_ROW, mm1);
    paddd_r2r(mm6, mm5);
    psrad_i2r(SHIFT_FRW_ROW, mm0);
    psrad_i2r(SHIFT_FRW_ROW, mm5);
    packssdw_r2r(mm0, mm3);
    packssdw_r2r(mm5, mm1);
    movq_r2r(mm3, mm6);
    punpcklwd_r2r(mm1, mm3);
    punpckhwd_r2r(mm1, mm6);
    movq_r2m(mm3, *(out + 0));
    movq_r2m(mm6, *(out + 4));
}

void ff_fdct_mmx(int16_t *block)
{
    int64_t align_tmp[16] ATTR_ALIGN(8);
    int16_t * const block_tmp= (int16_t*)align_tmp;
    int16_t *block1, *out;
    const int16_t *table;
    int i;

    block1 = block_tmp;
    fdct_col(block, block1, 0);
    fdct_col(block, block1, 4);

    block1 = block_tmp;
    table = tab_frw_01234567;
    out = block;
    for(i=8;i>0;i--) {
        fdct_row(block1, out, table);
        block1 += 8;
        table += 32;
        out += 8;
    }
}
