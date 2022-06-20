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

#include "config.h"
#include "../lossless_videodsp.h"
#include "libavutil/x86/cpu.h"

void ff_add_bytes_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t w);
void ff_add_bytes_avx2(uint8_t *dst, uint8_t *src, ptrdiff_t w);

void ff_add_median_pred_sse2(uint8_t *dst, const uint8_t *top,
                             const uint8_t *diff, ptrdiff_t w,
                             int *left, int *left_top);

int  ff_add_left_pred_ssse3(uint8_t *dst, const uint8_t *src,
                            ptrdiff_t w, int left);
int  ff_add_left_pred_unaligned_ssse3(uint8_t *dst, const uint8_t *src,
                                      ptrdiff_t w, int left);
int  ff_add_left_pred_unaligned_avx2(uint8_t *dst, const uint8_t *src,
                                     ptrdiff_t w, int left);

int ff_add_left_pred_int16_ssse3(uint16_t *dst, const uint16_t *src, unsigned mask, ptrdiff_t w, unsigned acc);
int ff_add_left_pred_int16_unaligned_ssse3(uint16_t *dst, const uint16_t *src, unsigned mask, ptrdiff_t w, unsigned acc);

void ff_add_gradient_pred_ssse3(uint8_t *src, const ptrdiff_t stride, const ptrdiff_t width);
void ff_add_gradient_pred_avx2(uint8_t *src, const ptrdiff_t stride, const ptrdiff_t width);

void ff_llviddsp_init_x86(LLVidDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->add_bytes       = ff_add_bytes_sse2;
        c->add_median_pred = ff_add_median_pred_sse2;
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        c->add_left_pred = ff_add_left_pred_ssse3;
        c->add_left_pred_int16 = ff_add_left_pred_int16_ssse3;
        c->add_gradient_pred   = ff_add_gradient_pred_ssse3;
    }

    if (EXTERNAL_SSSE3_FAST(cpu_flags)) {
        c->add_left_pred = ff_add_left_pred_unaligned_ssse3;
        c->add_left_pred_int16 = ff_add_left_pred_int16_unaligned_ssse3;
    }

    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        c->add_bytes       = ff_add_bytes_avx2;
        c->add_left_pred   = ff_add_left_pred_unaligned_avx2;
        c->add_gradient_pred = ff_add_gradient_pred_avx2;
    }
}
