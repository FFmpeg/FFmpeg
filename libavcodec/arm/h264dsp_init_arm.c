/*
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
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
#include "libavutil/arm/cpu.h"
#include "libavcodec/h264dsp.h"

int ff_startcode_find_candidate_armv6(const uint8_t *buf, int size);

void ff_h264_v_loop_filter_luma_neon(uint8_t *pix, int stride, int alpha,
                                     int beta, int8_t *tc0);
void ff_h264_h_loop_filter_luma_neon(uint8_t *pix, int stride, int alpha,
                                     int beta, int8_t *tc0);
void ff_h264_v_loop_filter_chroma_neon(uint8_t *pix, int stride, int alpha,
                                       int beta, int8_t *tc0);
void ff_h264_h_loop_filter_chroma_neon(uint8_t *pix, int stride, int alpha,
                                       int beta, int8_t *tc0);

void ff_weight_h264_pixels_16_neon(uint8_t *dst, int stride, int height,
                                   int log2_den, int weight, int offset);
void ff_weight_h264_pixels_8_neon(uint8_t *dst, int stride, int height,
                                  int log2_den, int weight, int offset);
void ff_weight_h264_pixels_4_neon(uint8_t *dst, int stride, int height,
                                  int log2_den, int weight, int offset);

void ff_biweight_h264_pixels_16_neon(uint8_t *dst, uint8_t *src, int stride,
                                     int height, int log2_den, int weightd,
                                     int weights, int offset);
void ff_biweight_h264_pixels_8_neon(uint8_t *dst, uint8_t *src, int stride,
                                    int height, int log2_den, int weightd,
                                    int weights, int offset);
void ff_biweight_h264_pixels_4_neon(uint8_t *dst, uint8_t *src, int stride,
                                    int height, int log2_den, int weightd,
                                    int weights, int offset);

void ff_h264_idct_add_neon(uint8_t *dst, int16_t *block, int stride);
void ff_h264_idct_dc_add_neon(uint8_t *dst, int16_t *block, int stride);
void ff_h264_idct_add16_neon(uint8_t *dst, const int *block_offset,
                             int16_t *block, int stride,
                             const uint8_t nnzc[6*8]);
void ff_h264_idct_add16intra_neon(uint8_t *dst, const int *block_offset,
                                  int16_t *block, int stride,
                                  const uint8_t nnzc[6*8]);
void ff_h264_idct_add8_neon(uint8_t **dest, const int *block_offset,
                            int16_t *block, int stride,
                            const uint8_t nnzc[6*8]);

void ff_h264_idct8_add_neon(uint8_t *dst, int16_t *block, int stride);
void ff_h264_idct8_dc_add_neon(uint8_t *dst, int16_t *block, int stride);
void ff_h264_idct8_add4_neon(uint8_t *dst, const int *block_offset,
                             int16_t *block, int stride,
                             const uint8_t nnzc[6*8]);

static av_cold void h264dsp_init_neon(H264DSPContext *c, const int bit_depth,
                                      const int chroma_format_idc)
{
#if HAVE_NEON
    if (bit_depth == 8) {
        c->h264_v_loop_filter_luma   = ff_h264_v_loop_filter_luma_neon;
        c->h264_h_loop_filter_luma   = ff_h264_h_loop_filter_luma_neon;
        if(chroma_format_idc == 1){
        c->h264_v_loop_filter_chroma = ff_h264_v_loop_filter_chroma_neon;
        c->h264_h_loop_filter_chroma = ff_h264_h_loop_filter_chroma_neon;
        }

        c->weight_h264_pixels_tab[0] = ff_weight_h264_pixels_16_neon;
        c->weight_h264_pixels_tab[1] = ff_weight_h264_pixels_8_neon;
        c->weight_h264_pixels_tab[2] = ff_weight_h264_pixels_4_neon;

        c->biweight_h264_pixels_tab[0] = ff_biweight_h264_pixels_16_neon;
        c->biweight_h264_pixels_tab[1] = ff_biweight_h264_pixels_8_neon;
        c->biweight_h264_pixels_tab[2] = ff_biweight_h264_pixels_4_neon;

        c->h264_idct_add        = ff_h264_idct_add_neon;
        c->h264_idct_dc_add     = ff_h264_idct_dc_add_neon;
        c->h264_idct_add16      = ff_h264_idct_add16_neon;
        c->h264_idct_add16intra = ff_h264_idct_add16intra_neon;
        if (chroma_format_idc <= 1)
            c->h264_idct_add8   = ff_h264_idct_add8_neon;
        c->h264_idct8_add       = ff_h264_idct8_add_neon;
        c->h264_idct8_dc_add    = ff_h264_idct8_dc_add_neon;
        c->h264_idct8_add4      = ff_h264_idct8_add4_neon;
    }
#endif // HAVE_NEON
}

av_cold void ff_h264dsp_init_arm(H264DSPContext *c, const int bit_depth,
                                 const int chroma_format_idc)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_armv6(cpu_flags) && !(have_vfpv3(cpu_flags) || have_neon(cpu_flags))) {
        // This function uses the 'setend' instruction which is deprecated
        // on ARMv8. This instruction is serializing on some ARMv7 cores as
        // well. Therefore, only use the function on ARMv6.
        c->h264_find_start_code_candidate = ff_startcode_find_candidate_armv6;
    }
    if (have_neon(cpu_flags))
        h264dsp_init_neon(c, bit_depth, chroma_format_idc);
}
