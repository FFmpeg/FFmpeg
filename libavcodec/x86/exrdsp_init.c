/*
 * OpenEXR (.exr) image decoder
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
#include "libavutil/x86/cpu.h"
#include "libavcodec/exrdsp.h"

void ff_reorder_pixels_sse2(uint8_t *dst, const uint8_t *src, ptrdiff_t size);

void ff_reorder_pixels_avx2(uint8_t *dst, const uint8_t *src, ptrdiff_t size);

void ff_predictor_ssse3(uint8_t *src, ptrdiff_t size);

void ff_predictor_avx(uint8_t *src, ptrdiff_t size);

void ff_predictor_avx2(uint8_t *src, ptrdiff_t size);

av_cold void ff_exrdsp_init_x86(ExrDSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        dsp->reorder_pixels = ff_reorder_pixels_sse2;
    }
    if (EXTERNAL_SSSE3(cpu_flags)) {
        dsp->predictor = ff_predictor_ssse3;
    }
    if (EXTERNAL_AVX(cpu_flags)) {
        dsp->predictor = ff_predictor_avx;
    }
    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        dsp->reorder_pixels = ff_reorder_pixels_avx2;
        dsp->predictor      = ff_predictor_avx2;
    }
}
