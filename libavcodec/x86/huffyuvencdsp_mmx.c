/*
 * SIMD-optimized HuffYUV encoding functions
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
#include "libavcodec/huffyuvencdsp.h"
#include "libavcodec/mathops.h"

void ff_diff_bytes_mmx(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                       intptr_t w);
void ff_diff_bytes_sse2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                        intptr_t w);
void ff_diff_bytes_avx2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                        intptr_t w);

#if HAVE_INLINE_ASM

static void sub_hfyu_median_pred_mmxext(uint8_t *dst, const uint8_t *src1,
                                        const uint8_t *src2, intptr_t w,
                                        int *left, int *left_top)
{
    x86_reg i = 0;
    uint8_t l, lt;

    __asm__ volatile (
        "movq  (%1, %0), %%mm0          \n\t" // LT
        "psllq $8, %%mm0                \n\t"
        "1:                             \n\t"
        "movq  (%1, %0), %%mm1          \n\t" // T
        "movq  -1(%2, %0), %%mm2        \n\t" // L
        "movq  (%2, %0), %%mm3          \n\t" // X
        "movq %%mm2, %%mm4              \n\t" // L
        "psubb %%mm0, %%mm2             \n\t"
        "paddb %%mm1, %%mm2             \n\t" // L + T - LT
        "movq %%mm4, %%mm5              \n\t" // L
        "pmaxub %%mm1, %%mm4            \n\t" // max(T, L)
        "pminub %%mm5, %%mm1            \n\t" // min(T, L)
        "pminub %%mm2, %%mm4            \n\t"
        "pmaxub %%mm1, %%mm4            \n\t"
        "psubb %%mm4, %%mm3             \n\t" // dst - pred
        "movq %%mm3, (%3, %0)           \n\t"
        "add $8, %0                     \n\t"
        "movq -1(%1, %0), %%mm0         \n\t" // LT
        "cmp %4, %0                     \n\t"
        " jb 1b                         \n\t"
        : "+r" (i)
        : "r" (src1), "r" (src2), "r" (dst), "r" ((x86_reg) w));

    l  = *left;
    lt = *left_top;

    dst[0] = src2[0] - mid_pred(l, src1[0], (l + src1[0] - lt) & 0xFF);

    *left_top = src1[w - 1];
    *left     = src2[w - 1];
}

#endif /* HAVE_INLINE_ASM */

av_cold void ff_huffyuvencdsp_init_x86(HuffYUVEncDSPContext *c)
{
    av_unused int cpu_flags = av_get_cpu_flags();

    if (ARCH_X86_32 && EXTERNAL_MMX(cpu_flags)) {
        c->diff_bytes = ff_diff_bytes_mmx;
    }

#if HAVE_INLINE_ASM
    if (INLINE_MMXEXT(cpu_flags)) {
        c->sub_hfyu_median_pred = sub_hfyu_median_pred_mmxext;
    }
#endif /* HAVE_INLINE_ASM */

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->diff_bytes = ff_diff_bytes_sse2;
    }

    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        c->diff_bytes = ff_diff_bytes_avx2;
    }
}
