/*
 * Copyright (c) 2020 Reimar Döffinger
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/hevcdsp.h"

void ff_hevc_add_residual_4x4_8_neon(uint8_t *_dst, int16_t *coeffs,
                                     ptrdiff_t stride);
void ff_hevc_add_residual_4x4_10_neon(uint8_t *_dst, int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_add_residual_8x8_8_neon(uint8_t *_dst, int16_t *coeffs,
                                     ptrdiff_t stride);
void ff_hevc_add_residual_8x8_10_neon(uint8_t *_dst, int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_add_residual_16x16_8_neon(uint8_t *_dst, int16_t *coeffs,
                                       ptrdiff_t stride);
void ff_hevc_add_residual_16x16_10_neon(uint8_t *_dst, int16_t *coeffs,
                                        ptrdiff_t stride);
void ff_hevc_add_residual_32x32_8_neon(uint8_t *_dst, int16_t *coeffs,
                                       ptrdiff_t stride);
void ff_hevc_add_residual_32x32_10_neon(uint8_t *_dst, int16_t *coeffs,
                                        ptrdiff_t stride);
void ff_hevc_idct_8x8_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_8x8_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_16x16_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_16x16_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_4x4_dc_8_neon(int16_t *coeffs);
void ff_hevc_idct_8x8_dc_8_neon(int16_t *coeffs);
void ff_hevc_idct_16x16_dc_8_neon(int16_t *coeffs);
void ff_hevc_idct_32x32_dc_8_neon(int16_t *coeffs);
void ff_hevc_idct_4x4_dc_10_neon(int16_t *coeffs);
void ff_hevc_idct_8x8_dc_10_neon(int16_t *coeffs);
void ff_hevc_idct_16x16_dc_10_neon(int16_t *coeffs);
void ff_hevc_idct_32x32_dc_10_neon(int16_t *coeffs);
void ff_hevc_sao_band_filter_8x8_8_neon(uint8_t *_dst, uint8_t *_src,
                                  ptrdiff_t stride_dst, ptrdiff_t stride_src,
                                  int16_t *sao_offset_val, int sao_left_class,
                                  int width, int height);



av_cold void ff_hevc_dsp_init_aarch64(HEVCDSPContext *c, const int bit_depth)
{
    if (!have_neon(av_get_cpu_flags())) return;

    if (bit_depth == 8) {
        c->add_residual[0]             = ff_hevc_add_residual_4x4_8_neon;
        c->add_residual[1]             = ff_hevc_add_residual_8x8_8_neon;
        c->add_residual[2]             = ff_hevc_add_residual_16x16_8_neon;
        c->add_residual[3]             = ff_hevc_add_residual_32x32_8_neon;
        c->idct[1]                     = ff_hevc_idct_8x8_8_neon;
        c->idct[2]                     = ff_hevc_idct_16x16_8_neon;
        c->idct_dc[0]                  = ff_hevc_idct_4x4_dc_8_neon;
        c->idct_dc[1]                  = ff_hevc_idct_8x8_dc_8_neon;
        c->idct_dc[2]                  = ff_hevc_idct_16x16_dc_8_neon;
        c->idct_dc[3]                  = ff_hevc_idct_32x32_dc_8_neon;
        // This function is disabled, as it doesn't handle widths that aren't
        // an even multiple of 8 correctly. fate-hevc doesn't exercise that
        // for the current size, but if enabled for bigger sizes, the cases
        // of non-multiple of 8 seem to arise.
//        c->sao_band_filter[0]          = ff_hevc_sao_band_filter_8x8_8_neon;
    }
    if (bit_depth == 10) {
        c->add_residual[0]             = ff_hevc_add_residual_4x4_10_neon;
        c->add_residual[1]             = ff_hevc_add_residual_8x8_10_neon;
        c->add_residual[2]             = ff_hevc_add_residual_16x16_10_neon;
        c->add_residual[3]             = ff_hevc_add_residual_32x32_10_neon;
        c->idct[1]                     = ff_hevc_idct_8x8_10_neon;
        c->idct[2]                     = ff_hevc_idct_16x16_10_neon;
        c->idct_dc[0]                  = ff_hevc_idct_4x4_dc_10_neon;
        c->idct_dc[1]                  = ff_hevc_idct_8x8_dc_10_neon;
        c->idct_dc[2]                  = ff_hevc_idct_16x16_dc_10_neon;
        c->idct_dc[3]                  = ff_hevc_idct_32x32_dc_10_neon;
    }
}
