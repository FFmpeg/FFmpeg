/*
 * Copyright (c) 2014 Seppo Tomperi <seppo.tomperi@vtt.fi>
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
#include "libavutil/arm/cpu.h"
#include "libavcodec/hevcdsp.h"

void ff_hevc_v_loop_filter_luma_neon(uint8_t *_pix, ptrdiff_t _stride, int _beta, int *_tc, uint8_t *_no_p, uint8_t *_no_q);
void ff_hevc_h_loop_filter_luma_neon(uint8_t *_pix, ptrdiff_t _stride, int _beta, int *_tc, uint8_t *_no_p, uint8_t *_no_q);
void ff_hevc_v_loop_filter_chroma_neon(uint8_t *_pix, ptrdiff_t _stride, int *_tc, uint8_t *_no_p, uint8_t *_no_q);
void ff_hevc_h_loop_filter_chroma_neon(uint8_t *_pix, ptrdiff_t _stride, int *_tc, uint8_t *_no_p, uint8_t *_no_q);
void ff_hevc_transform_4x4_neon_8(int16_t *coeffs, int col_limit);
void ff_hevc_transform_8x8_neon_8(int16_t *coeffs, int col_limit);
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

static av_cold void hevcdsp_init_neon(HEVCDSPContext *c, const int bit_depth)
{
#if HAVE_NEON
    if (bit_depth == 8) {
        c->hevc_v_loop_filter_luma     = ff_hevc_v_loop_filter_luma_neon;
        c->hevc_h_loop_filter_luma     = ff_hevc_h_loop_filter_luma_neon;
        c->hevc_v_loop_filter_chroma   = ff_hevc_v_loop_filter_chroma_neon;
        c->hevc_h_loop_filter_chroma   = ff_hevc_h_loop_filter_chroma_neon;
        c->idct[0]                     = ff_hevc_transform_4x4_neon_8;
        c->idct[1]                     = ff_hevc_transform_8x8_neon_8;
        c->idct_dc[0]                  = ff_hevc_idct_4x4_dc_neon_8;
        c->idct_dc[1]                  = ff_hevc_idct_8x8_dc_neon_8;
        c->idct_dc[2]                  = ff_hevc_idct_16x16_dc_neon_8;
        c->idct_dc[3]                  = ff_hevc_idct_32x32_dc_neon_8;
        c->transform_add[0]            = ff_hevc_transform_add_4x4_neon_8;
        c->transform_add[1]            = ff_hevc_transform_add_8x8_neon_8;
        c->transform_add[2]            = ff_hevc_transform_add_16x16_neon_8;
        c->transform_add[3]            = ff_hevc_transform_add_32x32_neon_8;
        c->idct_4x4_luma               = ff_hevc_transform_luma_4x4_neon_8;
    }
#endif // HAVE_NEON
}

void ff_hevcdsp_init_arm(HEVCDSPContext *c, const int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags))
        hevcdsp_init_neon(c, bit_depth);
}
