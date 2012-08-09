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

#include "libavutil/common.h"
#include "libavutil/x86/asm.h"
#include "libavcodec/dsputil.h"

#if HAVE_INLINE_ASM

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

#define BITS_FRW_ACC   3 //; 2 or 3 for accuracy
#define SHIFT_FRW_COL  BITS_FRW_ACC
#define SHIFT_FRW_ROW  (BITS_FRW_ACC + 17 - 3)
#define RND_FRW_ROW    (1 << (SHIFT_FRW_ROW-1))
//#define RND_FRW_COL    (1 << (SHIFT_FRW_COL-1))

#define X8(x) x,x,x,x,x,x,x,x

//concatenated table, for forward DCT transformation
DECLARE_ALIGNED(16, static const int16_t, fdct_tg_all_16)[24] = {
    X8(13036),  // tg * (2<<16) + 0.5
    X8(27146),  // tg * (2<<16) + 0.5
    X8(-21746)  // tg * (2<<16) + 0.5
};

DECLARE_ALIGNED(16, static const int16_t, ocos_4_16)[8] = {
    X8(23170)   //cos * (2<<15) + 0.5
};

DECLARE_ALIGNED(16, static const int16_t, fdct_one_corr)[8] = { X8(1) };

DECLARE_ALIGNED(8, static const int32_t, fdct_r_row)[2] = {RND_FRW_ROW, RND_FRW_ROW };

static const struct
{
 DECLARE_ALIGNED(16, const int32_t, fdct_r_row_sse2)[4];
} fdct_r_row_sse2 =
{{
 RND_FRW_ROW, RND_FRW_ROW, RND_FRW_ROW, RND_FRW_ROW
}};
//DECLARE_ALIGNED(16, static const long, fdct_r_row_sse2)[4] = {RND_FRW_ROW, RND_FRW_ROW, RND_FRW_ROW, RND_FRW_ROW};

DECLARE_ALIGNED(8, static const int16_t, tab_frw_01234567)[] = {  // forward_dct coeff table
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

static const struct
{
 DECLARE_ALIGNED(16, const int16_t, tab_frw_01234567_sse2)[256];
} tab_frw_01234567_sse2 =
{{
//DECLARE_ALIGNED(16, static const int16_t, tab_frw_01234567_sse2)[] = {  // forward_dct coeff table
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

#define S(s) AV_TOSTRING(s) //AV_STRINGIFY is too long

#define FDCT_COL(cpu, mm, mov)\
static av_always_inline void fdct_col_##cpu(const int16_t *in, int16_t *out, int offset)\
{\
    __asm__ volatile (\
        #mov"      16(%0),  %%"#mm"0 \n\t" \
        #mov"      96(%0),  %%"#mm"1 \n\t" \
        #mov"    %%"#mm"0,  %%"#mm"2 \n\t" \
        #mov"      32(%0),  %%"#mm"3 \n\t" \
        "paddsw  %%"#mm"1,  %%"#mm"0 \n\t" \
        #mov"      80(%0),  %%"#mm"4 \n\t" \
        "psllw  $"S(SHIFT_FRW_COL)", %%"#mm"0 \n\t" \
        #mov"        (%0),  %%"#mm"5 \n\t" \
        "paddsw  %%"#mm"3,  %%"#mm"4 \n\t" \
        "paddsw   112(%0),  %%"#mm"5 \n\t" \
        "psllw  $"S(SHIFT_FRW_COL)", %%"#mm"4 \n\t" \
        #mov"    %%"#mm"0,  %%"#mm"6 \n\t" \
        "psubsw  %%"#mm"1,  %%"#mm"2 \n\t" \
        #mov"      16(%1),  %%"#mm"1 \n\t" \
        "psubsw  %%"#mm"4,  %%"#mm"0 \n\t" \
        #mov"      48(%0),  %%"#mm"7 \n\t" \
        "pmulhw  %%"#mm"0,  %%"#mm"1 \n\t" \
        "paddsw    64(%0),  %%"#mm"7 \n\t" \
        "psllw  $"S(SHIFT_FRW_COL)", %%"#mm"5 \n\t" \
        "paddsw  %%"#mm"4,  %%"#mm"6 \n\t" \
        "psllw  $"S(SHIFT_FRW_COL)", %%"#mm"7 \n\t" \
        #mov"    %%"#mm"5,  %%"#mm"4 \n\t" \
        "psubsw  %%"#mm"7,  %%"#mm"5 \n\t" \
        "paddsw  %%"#mm"5,  %%"#mm"1 \n\t" \
        "paddsw  %%"#mm"7,  %%"#mm"4 \n\t" \
        "por         (%2),  %%"#mm"1 \n\t" \
        "psllw  $"S(SHIFT_FRW_COL)"+1, %%"#mm"2 \n\t" \
        "pmulhw    16(%1),  %%"#mm"5 \n\t" \
        #mov"    %%"#mm"4,  %%"#mm"7 \n\t" \
        "psubsw    80(%0),  %%"#mm"3 \n\t" \
        "psubsw  %%"#mm"6,  %%"#mm"4 \n\t" \
        #mov"    %%"#mm"1,    32(%3) \n\t" \
        "paddsw  %%"#mm"6,  %%"#mm"7 \n\t" \
        #mov"      48(%0),  %%"#mm"1 \n\t" \
        "psllw  $"S(SHIFT_FRW_COL)"+1, %%"#mm"3 \n\t" \
        "psubsw    64(%0),  %%"#mm"1 \n\t" \
        #mov"    %%"#mm"2,  %%"#mm"6 \n\t" \
        #mov"    %%"#mm"4,    64(%3) \n\t" \
        "paddsw  %%"#mm"3,  %%"#mm"2 \n\t" \
        "pmulhw      (%4),  %%"#mm"2 \n\t" \
        "psubsw  %%"#mm"3,  %%"#mm"6 \n\t" \
        "pmulhw      (%4),  %%"#mm"6 \n\t" \
        "psubsw  %%"#mm"0,  %%"#mm"5 \n\t" \
        "por         (%2),  %%"#mm"5 \n\t" \
        "psllw  $"S(SHIFT_FRW_COL)", %%"#mm"1 \n\t" \
        "por         (%2),  %%"#mm"2 \n\t" \
        #mov"    %%"#mm"1,  %%"#mm"4 \n\t" \
        #mov"        (%0),  %%"#mm"3 \n\t" \
        "paddsw  %%"#mm"6,  %%"#mm"1 \n\t" \
        "psubsw   112(%0),  %%"#mm"3 \n\t" \
        "psubsw  %%"#mm"6,  %%"#mm"4 \n\t" \
        #mov"        (%1),  %%"#mm"0 \n\t" \
        "psllw  $"S(SHIFT_FRW_COL)", %%"#mm"3 \n\t" \
        #mov"      32(%1),  %%"#mm"6 \n\t" \
        "pmulhw  %%"#mm"1,  %%"#mm"0 \n\t" \
        #mov"    %%"#mm"7,      (%3) \n\t" \
        "pmulhw  %%"#mm"4,  %%"#mm"6 \n\t" \
        #mov"    %%"#mm"5,    96(%3) \n\t" \
        #mov"    %%"#mm"3,  %%"#mm"7 \n\t" \
        #mov"      32(%1),  %%"#mm"5 \n\t" \
        "psubsw  %%"#mm"2,  %%"#mm"7 \n\t" \
        "paddsw  %%"#mm"2,  %%"#mm"3 \n\t" \
        "pmulhw  %%"#mm"7,  %%"#mm"5 \n\t" \
        "paddsw  %%"#mm"3,  %%"#mm"0 \n\t" \
        "paddsw  %%"#mm"4,  %%"#mm"6 \n\t" \
        "pmulhw      (%1),  %%"#mm"3 \n\t" \
        "por         (%2),  %%"#mm"0 \n\t" \
        "paddsw  %%"#mm"7,  %%"#mm"5 \n\t" \
        "psubsw  %%"#mm"6,  %%"#mm"7 \n\t" \
        #mov"    %%"#mm"0,    16(%3) \n\t" \
        "paddsw  %%"#mm"4,  %%"#mm"5 \n\t" \
        #mov"    %%"#mm"7,    48(%3) \n\t" \
        "psubsw  %%"#mm"1,  %%"#mm"3 \n\t" \
        #mov"    %%"#mm"5,    80(%3) \n\t" \
        #mov"    %%"#mm"3,   112(%3) \n\t" \
        : \
        : "r" (in  + offset), "r" (fdct_tg_all_16), "r" (fdct_one_corr), \
          "r" (out + offset), "r" (ocos_4_16)); \
}

FDCT_COL(mmx, mm, movq)
FDCT_COL(sse2, xmm, movdqa)

static av_always_inline void fdct_row_sse2(const int16_t *in, int16_t *out)
{
    __asm__ volatile(
#define FDCT_ROW_SSE2_H1(i,t)                    \
        "movq      " #i "(%0), %%xmm2      \n\t" \
        "movq      " #i "+8(%0), %%xmm0    \n\t" \
        "movdqa    " #t "+32(%1), %%xmm3   \n\t" \
        "movdqa    " #t "+48(%1), %%xmm7   \n\t" \
        "movdqa    " #t "(%1), %%xmm4      \n\t" \
        "movdqa    " #t "+16(%1), %%xmm5   \n\t"

#define FDCT_ROW_SSE2_H2(i,t)                    \
        "movq      " #i "(%0), %%xmm2      \n\t" \
        "movq      " #i "+8(%0), %%xmm0    \n\t" \
        "movdqa    " #t "+32(%1), %%xmm3   \n\t" \
        "movdqa    " #t "+48(%1), %%xmm7   \n\t"

#define FDCT_ROW_SSE2(i)                      \
        "movq      %%xmm2, %%xmm1       \n\t" \
        "pshuflw   $27, %%xmm0, %%xmm0  \n\t" \
        "paddsw    %%xmm0, %%xmm1       \n\t" \
        "psubsw    %%xmm0, %%xmm2       \n\t" \
        "punpckldq %%xmm2, %%xmm1       \n\t" \
        "pshufd    $78, %%xmm1, %%xmm2  \n\t" \
        "pmaddwd   %%xmm2, %%xmm3       \n\t" \
        "pmaddwd   %%xmm1, %%xmm7       \n\t" \
        "pmaddwd   %%xmm5, %%xmm2       \n\t" \
        "pmaddwd   %%xmm4, %%xmm1       \n\t" \
        "paddd     %%xmm7, %%xmm3       \n\t" \
        "paddd     %%xmm2, %%xmm1       \n\t" \
        "paddd     %%xmm6, %%xmm3       \n\t" \
        "paddd     %%xmm6, %%xmm1       \n\t" \
        "psrad     %3, %%xmm3           \n\t" \
        "psrad     %3, %%xmm1           \n\t" \
        "packssdw  %%xmm3, %%xmm1       \n\t" \
        "movdqa    %%xmm1, " #i "(%4)   \n\t"

        "movdqa    (%2), %%xmm6         \n\t"
        FDCT_ROW_SSE2_H1(0,0)
        FDCT_ROW_SSE2(0)
        FDCT_ROW_SSE2_H2(64,0)
        FDCT_ROW_SSE2(64)

        FDCT_ROW_SSE2_H1(16,64)
        FDCT_ROW_SSE2(16)
        FDCT_ROW_SSE2_H2(112,64)
        FDCT_ROW_SSE2(112)

        FDCT_ROW_SSE2_H1(32,128)
        FDCT_ROW_SSE2(32)
        FDCT_ROW_SSE2_H2(96,128)
        FDCT_ROW_SSE2(96)

        FDCT_ROW_SSE2_H1(48,192)
        FDCT_ROW_SSE2(48)
        FDCT_ROW_SSE2_H2(80,192)
        FDCT_ROW_SSE2(80)
        :
        : "r" (in), "r" (tab_frw_01234567_sse2.tab_frw_01234567_sse2),
          "r" (fdct_r_row_sse2.fdct_r_row_sse2), "i" (SHIFT_FRW_ROW), "r" (out)
          XMM_CLOBBERS_ONLY("%xmm0", "%xmm1", "%xmm2", "%xmm3",
                            "%xmm4", "%xmm5", "%xmm6", "%xmm7")
    );
}

static av_always_inline void fdct_row_mmx2(const int16_t *in, int16_t *out, const int16_t *table)
{
    __asm__ volatile (
        "pshufw    $0x1B, 8(%0), %%mm5 \n\t"
        "movq       (%0), %%mm0 \n\t"
        "movq      %%mm0, %%mm1 \n\t"
        "paddsw    %%mm5, %%mm0 \n\t"
        "psubsw    %%mm5, %%mm1 \n\t"
        "movq      %%mm0, %%mm2 \n\t"
        "punpckldq %%mm1, %%mm0 \n\t"
        "punpckhdq %%mm1, %%mm2 \n\t"
        "movq       (%1), %%mm1 \n\t"
        "movq      8(%1), %%mm3 \n\t"
        "movq     16(%1), %%mm4 \n\t"
        "movq     24(%1), %%mm5 \n\t"
        "movq     32(%1), %%mm6 \n\t"
        "movq     40(%1), %%mm7 \n\t"
        "pmaddwd   %%mm0, %%mm1 \n\t"
        "pmaddwd   %%mm2, %%mm3 \n\t"
        "pmaddwd   %%mm0, %%mm4 \n\t"
        "pmaddwd   %%mm2, %%mm5 \n\t"
        "pmaddwd   %%mm0, %%mm6 \n\t"
        "pmaddwd   %%mm2, %%mm7 \n\t"
        "pmaddwd  48(%1), %%mm0 \n\t"
        "pmaddwd  56(%1), %%mm2 \n\t"
        "paddd     %%mm1, %%mm3 \n\t"
        "paddd     %%mm4, %%mm5 \n\t"
        "paddd     %%mm6, %%mm7 \n\t"
        "paddd     %%mm0, %%mm2 \n\t"
        "movq       (%2), %%mm0 \n\t"
        "paddd     %%mm0, %%mm3 \n\t"
        "paddd     %%mm0, %%mm5 \n\t"
        "paddd     %%mm0, %%mm7 \n\t"
        "paddd     %%mm0, %%mm2 \n\t"
        "psrad $"S(SHIFT_FRW_ROW)", %%mm3 \n\t"
        "psrad $"S(SHIFT_FRW_ROW)", %%mm5 \n\t"
        "psrad $"S(SHIFT_FRW_ROW)", %%mm7 \n\t"
        "psrad $"S(SHIFT_FRW_ROW)", %%mm2 \n\t"
        "packssdw  %%mm5, %%mm3 \n\t"
        "packssdw  %%mm2, %%mm7 \n\t"
        "movq      %%mm3,  (%3) \n\t"
        "movq      %%mm7, 8(%3) \n\t"
        :
        : "r" (in), "r" (table), "r" (fdct_r_row), "r" (out));
}

static av_always_inline void fdct_row_mmx(const int16_t *in, int16_t *out, const int16_t *table)
{
    //FIXME reorder (I do not have an old MMX-only CPU here to benchmark ...)
    __asm__ volatile(
        "movd     12(%0), %%mm1 \n\t"
        "punpcklwd 8(%0), %%mm1 \n\t"
        "movq      %%mm1, %%mm2 \n\t"
        "psrlq     $0x20, %%mm1 \n\t"
        "movq      0(%0), %%mm0 \n\t"
        "punpcklwd %%mm2, %%mm1 \n\t"
        "movq      %%mm0, %%mm5 \n\t"
        "paddsw    %%mm1, %%mm0 \n\t"
        "psubsw    %%mm1, %%mm5 \n\t"
        "movq      %%mm0, %%mm2 \n\t"
        "punpckldq %%mm5, %%mm0 \n\t"
        "punpckhdq %%mm5, %%mm2 \n\t"
        "movq      0(%1), %%mm1 \n\t"
        "movq      8(%1), %%mm3 \n\t"
        "movq     16(%1), %%mm4 \n\t"
        "movq     24(%1), %%mm5 \n\t"
        "movq     32(%1), %%mm6 \n\t"
        "movq     40(%1), %%mm7 \n\t"
        "pmaddwd   %%mm0, %%mm1 \n\t"
        "pmaddwd   %%mm2, %%mm3 \n\t"
        "pmaddwd   %%mm0, %%mm4 \n\t"
        "pmaddwd   %%mm2, %%mm5 \n\t"
        "pmaddwd   %%mm0, %%mm6 \n\t"
        "pmaddwd   %%mm2, %%mm7 \n\t"
        "pmaddwd  48(%1), %%mm0 \n\t"
        "pmaddwd  56(%1), %%mm2 \n\t"
        "paddd     %%mm1, %%mm3 \n\t"
        "paddd     %%mm4, %%mm5 \n\t"
        "paddd     %%mm6, %%mm7 \n\t"
        "paddd     %%mm0, %%mm2 \n\t"
        "movq       (%2), %%mm0 \n\t"
        "paddd     %%mm0, %%mm3 \n\t"
        "paddd     %%mm0, %%mm5 \n\t"
        "paddd     %%mm0, %%mm7 \n\t"
        "paddd     %%mm0, %%mm2 \n\t"
        "psrad $"S(SHIFT_FRW_ROW)", %%mm3 \n\t"
        "psrad $"S(SHIFT_FRW_ROW)", %%mm5 \n\t"
        "psrad $"S(SHIFT_FRW_ROW)", %%mm7 \n\t"
        "psrad $"S(SHIFT_FRW_ROW)", %%mm2 \n\t"
        "packssdw  %%mm5, %%mm3 \n\t"
        "packssdw  %%mm2, %%mm7 \n\t"
        "movq      %%mm3, 0(%3) \n\t"
        "movq      %%mm7, 8(%3) \n\t"
        :
        : "r" (in), "r" (table), "r" (fdct_r_row), "r" (out));
}

void ff_fdct_mmx(int16_t *block)
{
    DECLARE_ALIGNED(8, int64_t, align_tmp)[16];
    int16_t * block1= (int16_t*)align_tmp;
    const int16_t *table= tab_frw_01234567;
    int i;

    fdct_col_mmx(block, block1, 0);
    fdct_col_mmx(block, block1, 4);

    for(i=8;i>0;i--) {
        fdct_row_mmx(block1, block, table);
        block1 += 8;
        table += 32;
        block += 8;
    }
}

void ff_fdct_mmx2(int16_t *block)
{
    DECLARE_ALIGNED(8, int64_t, align_tmp)[16];
    int16_t *block1= (int16_t*)align_tmp;
    const int16_t *table= tab_frw_01234567;
    int i;

    fdct_col_mmx(block, block1, 0);
    fdct_col_mmx(block, block1, 4);

    for(i=8;i>0;i--) {
        fdct_row_mmx2(block1, block, table);
        block1 += 8;
        table += 32;
        block += 8;
    }
}

void ff_fdct_sse2(int16_t *block)
{
    DECLARE_ALIGNED(16, int64_t, align_tmp)[16];
    int16_t * const block1= (int16_t*)align_tmp;

    fdct_col_sse2(block, block1, 0);
    fdct_row_sse2(block1, block);
}

#endif /* HAVE_INLINE_ASM */
