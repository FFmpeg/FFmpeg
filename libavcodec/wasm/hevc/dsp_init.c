/*
 * Copyright (c) 2024 Zhao Zhili
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

#include "libavutil/cpu_internal.h"
#include "libavcodec/hevc/dsp.h"
#include "libavcodec/wasm/hevc/idct.h"
#include "libavcodec/wasm/hevc/sao.h"

av_cold void ff_hevc_dsp_init_wasm(HEVCDSPContext *c, const int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (!CPUEXT(cpu_flags, SIMD128))
        return;

#if HAVE_SIMD128
    if (bit_depth == 8) {
        c->idct[0] = ff_hevc_idct_4x4_8_simd128;
        c->idct[1] = ff_hevc_idct_8x8_8_simd128;
        c->idct[2] = ff_hevc_idct_16x16_8_simd128;
        c->idct[3] = ff_hevc_idct_32x32_8_simd128;

        c->sao_band_filter[0] = ff_hevc_sao_band_filter_8x8_8_simd128;
        c->sao_band_filter[1] =
        c->sao_band_filter[2] =
        c->sao_band_filter[3] =
        c->sao_band_filter[4] = ff_hevc_sao_band_filter_16x16_8_simd128;

        c->sao_edge_filter[0] = ff_hevc_sao_edge_filter_8x8_8_simd128;
        c->sao_edge_filter[1] =
        c->sao_edge_filter[2] =
        c->sao_edge_filter[3] =
        c->sao_edge_filter[4] = ff_hevc_sao_edge_filter_16x16_8_simd128;
    } else if (bit_depth == 10) {
        c->idct[0] = ff_hevc_idct_4x4_10_simd128;
        c->idct[1] = ff_hevc_idct_8x8_10_simd128;
        c->idct[2] = ff_hevc_idct_16x16_10_simd128;
        c->idct[3] = ff_hevc_idct_32x32_10_simd128;
    }
#endif
}
