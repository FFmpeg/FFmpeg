/*
 * MMX optimized forward DCT
 * The gcc porting is Copyright (c) 2001 Fabrice Bellard.
 * cleanup/optimizations are Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * SSE2 optimization is Copyright (c) 2004 Denes Balatoni.
 *
 * from  fdctam32.c - AP922 MMX(3D-Now) forward-DCT
 * 
 *  Intel Application Note AP-922 - fast, precise implementation of DCT
 *        http://developer.intel.com/vtune/cbts/appnotes.htm
 *
 * Also of inspiration:
 * a page about fdct at http://www.geocities.com/ssavekar/dct.htm
 * Skal's fdct at http://skal.planet-d.net/coding/dct.html
 */
#include "../common.h"
#include "../dsputil.h"
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
#define RND_FRW_ROW		(1 << (SHIFT_FRW_ROW-1))
//#define RND_FRW_COL		(1 << (SHIFT_FRW_COL-1))

//concatenated table, for forward DCT transformation
static const int16_t fdct_tg_all_16[] ATTR_ALIGN(8) = {
    13036, 13036, 13036, 13036,		// tg * (2<<16) + 0.5
    27146, 27146, 27146, 27146,		// tg * (2<<16) + 0.5
    -21746, -21746, -21746, -21746,	// tg * (2<<16) + 0.5
};

static const int16_t ocos_4_16[4] ATTR_ALIGN(8) = {
    23170, 23170, 23170, 23170,	//cos * (2<<15) + 0.5
};

static const int64_t fdct_one_corr ATTR_ALIGN(8) = 0x0001000100010001LL;

static const int32_t fdct_r_row[2] ATTR_ALIGN(8) = {RND_FRW_ROW, RND_FRW_ROW };

struct 
{
 const int32_t fdct_r_row_sse2[4] ATTR_ALIGN(16);
} fdct_r_row_sse2 ATTR_ALIGN(16)=
{{
 RND_FRW_ROW, RND_FRW_ROW, RND_FRW_ROW, RND_FRW_ROW
}};
//static const long fdct_r_row_sse2[4] ATTR_ALIGN(16) = {RND_FRW_ROW, RND_FRW_ROW, RND_FRW_ROW, RND_FRW_ROW};

static const int16_t tab_frw_01234567[] ATTR_ALIGN(8) = {  // forward_dct coeff table
  16384,   16384,   22725,   19266, 
  16384,   16384,   12873,    4520, 
  21407,    8867,   19266,   -4520, 
  -8867,  -21407,  -22725,  -12873, 
  16384,  -16384,   12873,  -22725, 
 -16384,   16384,    4520,   19266, 
   8867,  -21407,    4520,  -12873, 
  21407,   -8867,   19266,  -22725, 

  22725,   22725,   31521,   26722, 
  22725,   22725,   17855,    6270, 
  29692,   12299,   26722,   -6270, 
 -12299,  -29692,  -31521,  -17855, 
  22725,  -22725,   17855,  -31521, 
 -22725,   22725,    6270,   26722, 
  12299,  -29692,    6270,  -17855, 
  29692,  -12299,   26722,  -31521, 

  21407,   21407,   29692,   25172, 
  21407,   21407,   16819,    5906, 
  27969,   11585,   25172,   -5906, 
 -11585,  -27969,  -29692,  -16819, 
  21407,  -21407,   16819,  -29692, 
 -21407,   21407,    5906,   25172, 
  11585,  -27969,    5906,  -16819, 
  27969,  -11585,   25172,  -29692, 

  19266,   19266,   26722,   22654, 
  19266,   19266,   15137,    5315, 
  25172,   10426,   22654,   -5315, 
 -10426,  -25172,  -26722,  -15137, 
  19266,  -19266,   15137,  -26722, 
 -19266,   19266,    5315,   22654, 
  10426,  -25172,    5315,  -15137, 
  25172,  -10426,   22654,  -26722, 

  16384,   16384,   22725,   19266, 
  16384,   16384,   12873,    4520, 
  21407,    8867,   19266,   -4520, 
  -8867,  -21407,  -22725,  -12873, 
  16384,  -16384,   12873,  -22725, 
 -16384,   16384,    4520,   19266, 
   8867,  -21407,    4520,  -12873, 
  21407,   -8867,   19266,  -22725, 

  19266,   19266,   26722,   22654, 
  19266,   19266,   15137,    5315, 
  25172,   10426,   22654,   -5315, 
 -10426,  -25172,  -26722,  -15137, 
  19266,  -19266,   15137,  -26722, 
 -19266,   19266,    5315,   22654, 
  10426,  -25172,    5315,  -15137, 
  25172,  -10426,   22654,  -26722, 

  21407,   21407,   29692,   25172, 
  21407,   21407,   16819,    5906, 
  27969,   11585,   25172,   -5906, 
 -11585,  -27969,  -29692,  -16819, 
  21407,  -21407,   16819,  -29692, 
 -21407,   21407,    5906,   25172, 
  11585,  -27969,    5906,  -16819, 
  27969,  -11585,   25172,  -29692, 

  22725,   22725,   31521,   26722, 
  22725,   22725,   17855,    6270, 
  29692,   12299,   26722,   -6270, 
 -12299,  -29692,  -31521,  -17855, 
  22725,  -22725,   17855,  -31521, 
 -22725,   22725,    6270,   26722, 
  12299,  -29692,    6270,  -17855, 
  29692,  -12299,   26722,  -31521, 
};

struct 
{
 const int16_t tab_frw_01234567_sse2[256] ATTR_ALIGN(16);
} tab_frw_01234567_sse2 ATTR_ALIGN(16) =
{{
//static const int16_t tab_frw_01234567_sse2[] ATTR_ALIGN(16) = {  // forward_dct coeff table  
#define TABLE_SSE2 C4,  C4,  C1,  C3, -C6, -C2, -C1, -C5, \
                   C4,  C4,  C5,  C7,  C2,  C6,  C3, -C7, \
                  -C4,  C4,  C7,  C3,  C6, -C2,  C7, -C5, \
                   C4, -C4,  C5, -C1,  C2, -C6,  C3, -C1, 
// c1..c7 * cos(pi/4) * 2^15 
#define C1 22725
#define C2 21407
#define C3 19266
#define C4 16384
#define C5 12873
#define C6 8867
#define C7 4520
TABLE_SSE2

#undef C1
#undef C2
#undef C3
#undef C4
#undef C5
#undef C6
#undef C7
#define C1 31521
#define C2 29692
#define C3 26722
#define C4 22725
#define C5 17855
#define C6 12299
#define C7 6270
TABLE_SSE2

#undef C1
#undef C2
#undef C3
#undef C4
#undef C5
#undef C6
#undef C7
#define C1 29692
#define C2 27969
#define C3 25172
#define C4 21407
#define C5 16819
#define C6 11585
#define C7 5906
TABLE_SSE2

#undef C1
#undef C2
#undef C3
#undef C4
#undef C5
#undef C6
#undef C7
#define C1 26722
#define C2 25172
#define C3 22654
#define C4 19266
#define C5 15137
#define C6 10426
#define C7 5315
TABLE_SSE2

#undef C1
#undef C2
#undef C3
#undef C4
#undef C5
#undef C6
#undef C7
#define C1 22725
#define C2 21407
#define C3 19266
#define C4 16384
#define C5 12873
#define C6 8867
#define C7 4520
TABLE_SSE2

#undef C1
#undef C2
#undef C3
#undef C4
#undef C5
#undef C6
#undef C7
#define C1 26722
#define C2 25172
#define C3 22654
#define C4 19266
#define C5 15137
#define C6 10426
#define C7 5315
TABLE_SSE2

#undef C1
#undef C2
#undef C3
#undef C4
#undef C5
#undef C6
#undef C7
#define C1 29692
#define C2 27969
#define C3 25172
#define C4 21407
#define C5 16819
#define C6 11585
#define C7 5906
TABLE_SSE2

#undef C1
#undef C2
#undef C3
#undef C4
#undef C5
#undef C6
#undef C7
#define C1 31521
#define C2 29692
#define C3 26722
#define C4 22725
#define C5 17855
#define C6 12299
#define C7 6270
TABLE_SSE2
}};


static always_inline void fdct_col(const int16_t *in, int16_t *out, int offset)
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


static always_inline void fdct_row_sse2(const int16_t *in, int16_t *out)
{
    asm volatile(
        ".macro FDCT_ROW_SSE2_H1 i t   \n\t"
	"movq      \\i(%0), %%xmm2     \n\t"
	"movq      \\i+8(%0), %%xmm0   \n\t"
	"movdqa    \\t+32(%1), %%xmm3  \n\t"
	"movdqa    \\t+48(%1), %%xmm7  \n\t"	
	"movdqa    \\t(%1), %%xmm4     \n\t"
	"movdqa    \\t+16(%1), %%xmm5  \n\t"	
	".endm                         \n\t"
        ".macro FDCT_ROW_SSE2_H2 i t   \n\t"
	"movq      \\i(%0), %%xmm2     \n\t"
	"movq      \\i+8(%0), %%xmm0   \n\t"
	"movdqa    \\t+32(%1), %%xmm3  \n\t"
	"movdqa    \\t+48(%1), %%xmm7  \n\t"	
	".endm                         \n\t"
	".macro FDCT_ROW_SSE2 i        \n\t"	
	"movq      %%xmm2, %%xmm1      \n\t"
	"pshuflw   $27, %%xmm0, %%xmm0 \n\t"
	"paddsw    %%xmm0, %%xmm1      \n\t"
	"psubsw    %%xmm0, %%xmm2      \n\t"
	"punpckldq %%xmm2, %%xmm1      \n\t"
	"pshufd    $78, %%xmm1, %%xmm2 \n\t"
	"pmaddwd   %%xmm2, %%xmm3      \n\t"
	"pmaddwd   %%xmm1, %%xmm7      \n\t"
	"pmaddwd   %%xmm5, %%xmm2      \n\t"
	"pmaddwd   %%xmm4, %%xmm1      \n\t"
	"paddd     %%xmm7, %%xmm3      \n\t"	
	"paddd     %%xmm2, %%xmm1      \n\t"
	"paddd     %%xmm6, %%xmm3      \n\t"
	"paddd     %%xmm6, %%xmm1      \n\t"
	"psrad     %3, %%xmm3          \n\t"
	"psrad     %3, %%xmm1          \n\t"
	"packssdw  %%xmm3, %%xmm1      \n\t"
	"movdqa    %%xmm1, \\i(%4)     \n\t"
	".endm                         \n\t"	
	"movdqa    (%2), %%xmm6        \n\t"		
	"FDCT_ROW_SSE2_H1 0 0 \n\t"
	"FDCT_ROW_SSE2 0 \n\t"
	"FDCT_ROW_SSE2_H2 64 0 \n\t"
	"FDCT_ROW_SSE2 64 \n\t"

	"FDCT_ROW_SSE2_H1 16 64 \n\t"
	"FDCT_ROW_SSE2 16 \n\t"
	"FDCT_ROW_SSE2_H2 112 64 \n\t"
	"FDCT_ROW_SSE2 112 \n\t"

	"FDCT_ROW_SSE2_H1 32 128 \n\t"
	"FDCT_ROW_SSE2 32 \n\t"
	"FDCT_ROW_SSE2_H2 96 128 \n\t"
	"FDCT_ROW_SSE2 96 \n\t"

	"FDCT_ROW_SSE2_H1 48 192 \n\t"
	"FDCT_ROW_SSE2 48 \n\t"
	"FDCT_ROW_SSE2_H2 80 192 \n\t"
	"FDCT_ROW_SSE2 80 \n\t"
	:
	: "r" (in), "r" (tab_frw_01234567_sse2.tab_frw_01234567_sse2), "r" (fdct_r_row_sse2.fdct_r_row_sse2), "i" (SHIFT_FRW_ROW), "r" (out)
    );
}

static always_inline void fdct_row_mmx2(const int16_t *in, int16_t *out, const int16_t *table)
{ 
    pshufw_m2r(*(in + 4), mm5, 0x1B);
    movq_m2r(*(in + 0), mm0);
    movq_r2r(mm0, mm1);
    paddsw_r2r(mm5, mm0);
    psubsw_r2r(mm5, mm1);
    movq_r2r(mm0, mm2);
    punpckldq_r2r(mm1, mm0);
    punpckhdq_r2r(mm1, mm2);
    movq_m2r(*(table + 0), mm1);
    movq_m2r(*(table + 4), mm3);
    movq_m2r(*(table + 8), mm4);
    movq_m2r(*(table + 12), mm5);
    movq_m2r(*(table + 16), mm6);
    movq_m2r(*(table + 20), mm7);
    pmaddwd_r2r(mm0, mm1);
    pmaddwd_r2r(mm2, mm3);
    pmaddwd_r2r(mm0, mm4);
    pmaddwd_r2r(mm2, mm5);
    pmaddwd_r2r(mm0, mm6);
    pmaddwd_r2r(mm2, mm7);
    pmaddwd_m2r(*(table + 24), mm0);
    pmaddwd_m2r(*(table + 28), mm2);
    paddd_r2r(mm1, mm3);
    paddd_r2r(mm4, mm5);
    paddd_r2r(mm6, mm7);
    paddd_r2r(mm0, mm2);
    movq_m2r(*fdct_r_row, mm0);
    paddd_r2r(mm0, mm3);
    paddd_r2r(mm0, mm5);
    paddd_r2r(mm0, mm7);
    paddd_r2r(mm0, mm2);
    psrad_i2r(SHIFT_FRW_ROW, mm3);
    psrad_i2r(SHIFT_FRW_ROW, mm5);
    psrad_i2r(SHIFT_FRW_ROW, mm7);
    psrad_i2r(SHIFT_FRW_ROW, mm2);
    packssdw_r2r(mm5, mm3);
    packssdw_r2r(mm2, mm7);
    movq_r2m(mm3, *(out + 0));
    movq_r2m(mm7, *(out + 4));
}

static always_inline void fdct_row_mmx(const int16_t *in, int16_t *out, const int16_t *table)
{ 
//FIXME reorder (i dont have a old mmx only cpu here to benchmark ...)
    movd_m2r(*(in + 6), mm1);
    punpcklwd_m2r(*(in + 4), mm1);
    movq_r2r(mm1, mm2);
    psrlq_i2r(0x20, mm1);
    movq_m2r(*(in + 0), mm0);
    punpcklwd_r2r(mm2, mm1);
    movq_r2r(mm0, mm5);
    paddsw_r2r(mm1, mm0);
    psubsw_r2r(mm1, mm5);
    movq_r2r(mm0, mm2);
    punpckldq_r2r(mm5, mm0);
    punpckhdq_r2r(mm5, mm2);
    movq_m2r(*(table + 0), mm1);
    movq_m2r(*(table + 4), mm3);
    movq_m2r(*(table + 8), mm4);
    movq_m2r(*(table + 12), mm5);
    movq_m2r(*(table + 16), mm6);
    movq_m2r(*(table + 20), mm7);
    pmaddwd_r2r(mm0, mm1);
    pmaddwd_r2r(mm2, mm3);
    pmaddwd_r2r(mm0, mm4);
    pmaddwd_r2r(mm2, mm5);
    pmaddwd_r2r(mm0, mm6);
    pmaddwd_r2r(mm2, mm7);
    pmaddwd_m2r(*(table + 24), mm0);
    pmaddwd_m2r(*(table + 28), mm2);
    paddd_r2r(mm1, mm3);
    paddd_r2r(mm4, mm5);
    paddd_r2r(mm6, mm7);
    paddd_r2r(mm0, mm2);
    movq_m2r(*fdct_r_row, mm0);
    paddd_r2r(mm0, mm3);
    paddd_r2r(mm0, mm5);
    paddd_r2r(mm0, mm7);
    paddd_r2r(mm0, mm2);
    psrad_i2r(SHIFT_FRW_ROW, mm3);
    psrad_i2r(SHIFT_FRW_ROW, mm5);
    psrad_i2r(SHIFT_FRW_ROW, mm7);
    psrad_i2r(SHIFT_FRW_ROW, mm2);
    packssdw_r2r(mm5, mm3);
    packssdw_r2r(mm2, mm7);
    movq_r2m(mm3, *(out + 0));
    movq_r2m(mm7, *(out + 4));
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
        fdct_row_mmx(block1, out, table);
        block1 += 8;
        table += 32;
        out += 8;
    }
}

void ff_fdct_mmx2(int16_t *block)
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
        fdct_row_mmx2(block1, out, table);
        block1 += 8;
        table += 32;
        out += 8;
    }
}

void ff_fdct_sse2(int16_t *block) 
{
    int64_t align_tmp[16] ATTR_ALIGN(8);
    int16_t * const block_tmp= (int16_t*)align_tmp;
    int16_t *block1;

    block1 = block_tmp;
    fdct_col(block, block1, 0);
    fdct_col(block, block1, 4);

    fdct_row_sse2(block1, block);
}

