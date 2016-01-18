/*
 *  ARM NEON optimised HEVC for armv8 instruct functions
 *  Copyright (c) 2015 Junhai ZHANG <243186085@qq.com>
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
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/hevcdsp.h"
#include "hevcdsp_aarch64.h"

void ff_hevc_transform_4x4_neon_8(int16_t *coeffs, int col_limit);
void ff_hevc_transform_8x8_neon_8(int16_t *coeffs, int col_limit);
void ff_hevc_transform_16x16_neon_8(int16_t *coeffs, int col_limit);
void ff_hevc_transform_32x32_neon_8(int16_t *coeffs, int col_limit);
void ff_hevc_idct_4x4_dc_neon_8(int16_t *coeffs);
void ff_hevc_idct_8x8_dc_neon_8(int16_t *coeffs);
void ff_hevc_idct_16x16_dc_neon_8(int16_t *coeffs);
void ff_hevc_idct_32x32_dc_neon_8(int16_t *coeffs);
void ff_hevc_transform_luma_4x4_neon_8(int16_t *coeffs);
void ff_hevc_transform_add_4x4_neon_8(uint8_t *_dst, int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_transform_add_8x8_neon_8(uint8_t *_dst, int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_transform_add_16x16_neon_8(uint8_t *_dst, int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_transform_add_32x32_neon_8(uint8_t *_dst, int16_t *coeffs,
                                      ptrdiff_t stride);


av_cold void ff_hevcdsp_init_neon(HEVCDSPContext *c, const int bit_depth)
{
    if (bit_depth == 8) {
        int x;
        c->idct[0]                          = ff_hevc_transform_4x4_neon_8;
        c->idct[1]                          = ff_hevc_transform_8x8_neon_8;
        c->idct[2]                          = ff_hevc_transform_16x16_neon_8;
        c->idct[3]                          = ff_hevc_transform_32x32_neon_8;
        c->idct_dc[0]                       = ff_hevc_idct_4x4_dc_neon_8;
        c->idct_dc[1]                       = ff_hevc_idct_8x8_dc_neon_8;
        c->idct_dc[2]                       = ff_hevc_idct_16x16_dc_neon_8;
        c->idct_dc[3]                       = ff_hevc_idct_32x32_dc_neon_8;
        c->transform_add[0]                 = ff_hevc_transform_add_4x4_neon_8;
        c->transform_add[1]                 = ff_hevc_transform_add_8x8_neon_8;
        c->transform_add[2]                 = ff_hevc_transform_add_16x16_neon_8;
        c->transform_add[3]                 = ff_hevc_transform_add_32x32_neon_8;
        c->idct_4x4_luma                    = ff_hevc_transform_luma_4x4_neon_8;
    }
}
