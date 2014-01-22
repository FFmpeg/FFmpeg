/*
 * Lossless video DSP utils
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

#include "../lossless_videodsp.h"
#include "libavutil/x86/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavcodec/mathops.h"

void ff_add_int16_mmx(uint16_t *dst, const uint16_t *src, unsigned mask, int w);
void ff_add_int16_sse2(uint16_t *dst, const uint16_t *src, unsigned mask, int w);
void ff_diff_int16_mmx (uint16_t *dst, const uint16_t *src1, const uint16_t *src2, unsigned mask, int w);
void ff_diff_int16_sse2(uint16_t *dst, const uint16_t *src1, const uint16_t *src2, unsigned mask, int w);
int ff_add_hfyu_left_prediction_int16_ssse3(uint16_t *dst, const uint16_t *src, unsigned mask, int w, int acc);
int ff_add_hfyu_left_prediction_int16_sse4(uint16_t *dst, const uint16_t *src, unsigned mask, int w, int acc);
void ff_add_hfyu_median_prediction_int16_mmxext(uint16_t *dst, const uint16_t *top, const uint16_t *diff, unsigned mask, int w, int *left, int *left_top);

static void sub_hfyu_median_prediction_int16_mmxext(uint16_t *dst, const uint16_t *src1,
                                                    const uint16_t *src2, unsigned mask, int w,
                                                    int *left, int *left_top)
{
    x86_reg i=0;
    uint16_t l, lt;

    __asm__ volatile(
        "movd %5, %%mm7                 \n\t"
        "pshufw $0, %%mm7, %%mm7        \n\t"
        "movq  (%1, %0), %%mm0          \n\t" // LT
        "psllq $16, %%mm0                \n\t"
        "1:                             \n\t"
        "movq  (%1, %0), %%mm1          \n\t" // T
        "movq  -2(%2, %0), %%mm2        \n\t" // L
        "movq  (%2, %0), %%mm3          \n\t" // X
        "movq %%mm2, %%mm4              \n\t" // L
        "psubw %%mm0, %%mm2             \n\t"
        "paddw %%mm1, %%mm2             \n\t" // L + T - LT
        "pand %%mm7, %%mm2              \n\t"
        "movq %%mm4, %%mm5              \n\t" // L
        "pmaxsw %%mm1, %%mm4            \n\t" // max(T, L)
        "pminsw %%mm5, %%mm1            \n\t" // min(T, L)
        "pminsw %%mm2, %%mm4            \n\t"
        "pmaxsw %%mm1, %%mm4            \n\t"
        "psubw %%mm4, %%mm3             \n\t" // dst - pred
        "pand %%mm7, %%mm3              \n\t"
        "movq %%mm3, (%3, %0)           \n\t"
        "add $8, %0                     \n\t"
        "movq -2(%1, %0), %%mm0         \n\t" // LT
        "cmp %4, %0                     \n\t"
        " jb 1b                         \n\t"
        : "+r" (i)
        : "r"(src1), "r"(src2), "r"(dst), "r"((x86_reg)2*w), "rm"(mask)
    );

    l= *left;
    lt= *left_top;

    dst[0]= src2[0] - mid_pred(l, src1[0], (l + src1[0] - lt)&mask);

    *left_top= src1[w-1];
    *left    = src2[w-1];
}

void ff_llviddsp_init_x86(LLVidDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMX(cpu_flags)) {
        c->add_int16 = ff_add_int16_mmx;
        c->diff_int16 = ff_diff_int16_mmx;
    }

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        c->add_hfyu_median_prediction_int16 = ff_add_hfyu_median_prediction_int16_mmxext;
        c->sub_hfyu_median_prediction_int16 = sub_hfyu_median_prediction_int16_mmxext;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->add_int16 = ff_add_int16_sse2;
        c->diff_int16 = ff_diff_int16_sse2;
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        c->add_hfyu_left_prediction_int16 = ff_add_hfyu_left_prediction_int16_ssse3;
    }

    if (EXTERNAL_SSE4(cpu_flags)) {
        c->add_hfyu_left_prediction_int16 = ff_add_hfyu_left_prediction_int16_sse4;
    }
}
