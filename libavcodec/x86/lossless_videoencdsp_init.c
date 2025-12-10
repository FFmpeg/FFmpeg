/*
 * SIMD-optimized lossless video encoding functions
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/lossless_videoencdsp.h"

void ff_diff_bytes_sse2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                        intptr_t w);
void ff_diff_bytes_avx2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                        intptr_t w);

void ff_sub_left_predict_avx(uint8_t *dst, const uint8_t *src,
                            ptrdiff_t stride, ptrdiff_t width, int height);

#if HAVE_SSE2_INLINE && HAVE_7REGS

static void sub_median_pred_sse2(uint8_t *dst, const uint8_t *src1,
                                 const uint8_t *src2, intptr_t w,
                                 int *left, int *left_top)
{
    x86_reg i = 0;

    __asm__ volatile (
        "movdqu  (%1, %0), %%xmm0    \n\t" // LT
        "movdqu  (%2, %0), %%xmm2    \n\t" // L
        "movd        (%6), %%xmm1    \n\t" // LT
        "movd        (%5), %%xmm3    \n\t" // L
        "pslldq        $1, %%xmm0    \n\t"
        "pslldq        $1, %%xmm2    \n\t"
        "por       %%xmm1, %%xmm0    \n\t" // LT
        "por       %%xmm3, %%xmm2    \n\t" // L
        "jmp 2f                      \n\t"
        "1:                          \n\t"
        "movdqu -1(%2, %0), %%xmm2   \n\t" // L
        "movdqu -1(%1, %0), %%xmm0   \n\t" // LT
        "2:                          \n\t"
        "movdqu  (%1, %0), %%xmm1    \n\t" // T
        "movdqu  (%2, %0), %%xmm3    \n\t" // X
        "movdqa    %%xmm2, %%xmm4    \n\t" // L
        "psubb     %%xmm0, %%xmm2    \n\t"
        "paddb     %%xmm1, %%xmm2    \n\t" // L + T - LT
        "movdqa    %%xmm4, %%xmm5    \n\t" // L
        "pmaxub    %%xmm1, %%xmm4    \n\t" // max(T, L)
        "pminub    %%xmm5, %%xmm1    \n\t" // min(T, L)
        "pminub    %%xmm2, %%xmm4    \n\t"
        "pmaxub    %%xmm1, %%xmm4    \n\t"
        "psubb     %%xmm4, %%xmm3    \n\t" // dst - pred
        "movdqu    %%xmm3, (%3, %0)  \n\t"
        "add          $16, %0        \n\t"
        "cmp           %4, %0        \n\t"
        " jb 1b                      \n\t"
        : "+r" (i)
        : "r" (src1), "r" (src2), "r" (dst), "r" ((x86_reg) w), "r" (left), "r" (left_top)
        : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5",) "memory"
    );

    *left_top = src1[w - 1];
    *left     = src2[w - 1];
}

#endif /* HAVE_INLINE_ASM */

av_cold void ff_llvidencdsp_init_x86(LLVidEncDSPContext *c)
{
    av_unused int cpu_flags = av_get_cpu_flags();

#if HAVE_SSE2_INLINE && HAVE_7REGS
    if (INLINE_SSE2(cpu_flags)) {
        c->sub_median_pred = sub_median_pred_sse2;
    }
#endif /* HAVE_SSE2_INLINE */

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->diff_bytes = ff_diff_bytes_sse2;
    }

    if (EXTERNAL_AVX(cpu_flags)) {
        c->sub_left_predict = ff_sub_left_predict_avx;
    }

    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        c->diff_bytes = ff_diff_bytes_avx2;
    }
}
