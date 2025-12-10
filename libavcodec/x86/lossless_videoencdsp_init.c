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
#include "libavutil/x86/cpu.h"
#include "libavcodec/lossless_videoencdsp.h"

void ff_diff_bytes_sse2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                        intptr_t w);
void ff_diff_bytes_avx2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                        intptr_t w);

void ff_sub_median_pred_sse2(uint8_t *dst, const uint8_t *src1,
                             const uint8_t *src2, intptr_t w,
                             int *left, int *left_top);

void ff_sub_left_predict_avx(uint8_t *dst, const uint8_t *src,
                            ptrdiff_t stride, ptrdiff_t width, int height);

av_cold void ff_llvidencdsp_init_x86(LLVidEncDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->sub_median_pred = ff_sub_median_pred_sse2;
        c->diff_bytes = ff_diff_bytes_sse2;
    }

    if (EXTERNAL_AVX(cpu_flags)) {
        c->sub_left_predict = ff_sub_left_predict_avx;
    }

    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        c->diff_bytes = ff_diff_bytes_avx2;
    }
}
