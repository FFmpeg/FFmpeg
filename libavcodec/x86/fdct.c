/*
 * SIMD-optimized forward DCT
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/macros.h"
#include "libavutil/mem_internal.h"
#include "libavutil/x86/asm.h"
#include "fdct.h"

#if HAVE_SSE2_INLINE

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

static const struct
{
 DECLARE_ALIGNED(16, const int32_t, fdct_r_row_sse2)[4];
} fdct_r_row_sse2 =
{{
 RND_FRW_ROW, RND_FRW_ROW, RND_FRW_ROW, RND_FRW_ROW
}};
//DECLARE_ALIGNED(16, static const long, fdct_r_row_sse2)[4] = {RND_FRW_ROW, RND_FRW_ROW, RND_FRW_ROW, RND_FRW_ROW};

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

void ff_fdct_sse2(int16_t *block)
{
    DECLARE_ALIGNED(16, int64_t, align_tmp)[16];
    int16_t * const block1= (int16_t*)align_tmp;

    fdct_col_sse2(block, block1, 0);
    fdct_row_sse2(block1, block);
}

#endif /* HAVE_SSE2_INLINE */
