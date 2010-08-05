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

#include "libavcodec/dsputil.h"
#include "libavcodec/h264dsp.h"

void ff_h264_v_loop_filter_luma_neon(uint8_t *pix, int stride, int alpha,
                                     int beta, int8_t *tc0);
void ff_h264_h_loop_filter_luma_neon(uint8_t *pix, int stride, int alpha,
                                     int beta, int8_t *tc0);
void ff_h264_v_loop_filter_chroma_neon(uint8_t *pix, int stride, int alpha,
                                       int beta, int8_t *tc0);
void ff_h264_h_loop_filter_chroma_neon(uint8_t *pix, int stride, int alpha,
                                       int beta, int8_t *tc0);

void ff_weight_h264_pixels_16x16_neon(uint8_t *ds, int stride, int log2_den,
                                      int weight, int offset);
void ff_weight_h264_pixels_16x8_neon(uint8_t *ds, int stride, int log2_den,
                                     int weight, int offset);
void ff_weight_h264_pixels_8x16_neon(uint8_t *ds, int stride, int log2_den,
                                     int weight, int offset);
void ff_weight_h264_pixels_8x8_neon(uint8_t *ds, int stride, int log2_den,
                                    int weight, int offset);
void ff_weight_h264_pixels_8x4_neon(uint8_t *ds, int stride, int log2_den,
                                    int weight, int offset);
void ff_weight_h264_pixels_4x8_neon(uint8_t *ds, int stride, int log2_den,
                                    int weight, int offset);
void ff_weight_h264_pixels_4x4_neon(uint8_t *ds, int stride, int log2_den,
                                    int weight, int offset);
void ff_weight_h264_pixels_4x2_neon(uint8_t *ds, int stride, int log2_den,
                                    int weight, int offset);

void ff_biweight_h264_pixels_16x16_neon(uint8_t *dst, uint8_t *src, int stride,
                                        int log2_den, int weightd, int weights,
                                        int offset);
void ff_biweight_h264_pixels_16x8_neon(uint8_t *dst, uint8_t *src, int stride,
                                       int log2_den, int weightd, int weights,
                                       int offset);
void ff_biweight_h264_pixels_8x16_neon(uint8_t *dst, uint8_t *src, int stride,
                                       int log2_den, int weightd, int weights,
                                       int offset);
void ff_biweight_h264_pixels_8x8_neon(uint8_t *dst, uint8_t *src, int stride,
                                      int log2_den, int weightd, int weights,
                                      int offset);
void ff_biweight_h264_pixels_8x4_neon(uint8_t *dst, uint8_t *src, int stride,
                                      int log2_den, int weightd, int weights,
                                      int offset);
void ff_biweight_h264_pixels_4x8_neon(uint8_t *dst, uint8_t *src, int stride,
                                      int log2_den, int weightd, int weights,
                                      int offset);
void ff_biweight_h264_pixels_4x4_neon(uint8_t *dst, uint8_t *src, int stride,
                                      int log2_den, int weightd, int weights,
                                      int offset);
void ff_biweight_h264_pixels_4x2_neon(uint8_t *dst, uint8_t *src, int stride,
                                      int log2_den, int weightd, int weights,
                                      int offset);

void ff_h264_idct_add_neon(uint8_t *dst, DCTELEM *block, int stride);
void ff_h264_idct_dc_add_neon(uint8_t *dst, DCTELEM *block, int stride);
void ff_h264_idct_add16_neon(uint8_t *dst, const int *block_offset,
                             DCTELEM *block, int stride,
                             const uint8_t nnzc[6*8]);
void ff_h264_idct_add16intra_neon(uint8_t *dst, const int *block_offset,
                                  DCTELEM *block, int stride,
                                  const uint8_t nnzc[6*8]);
void ff_h264_idct_add8_neon(uint8_t **dest, const int *block_offset,
                            DCTELEM *block, int stride,
                            const uint8_t nnzc[6*8]);

void ff_h264_idct8_add_neon(uint8_t *dst, DCTELEM *block, int stride);
void ff_h264_idct8_dc_add_neon(uint8_t *dst, DCTELEM *block, int stride);
void ff_h264_idct8_add4_neon(uint8_t *dst, const int *block_offset,
                             DCTELEM *block, int stride,
                             const uint8_t nnzc[6*8]);

static void ff_h264dsp_init_neon(H264DSPContext *c)
{
    c->h264_v_loop_filter_luma   = ff_h264_v_loop_filter_luma_neon;
    c->h264_h_loop_filter_luma   = ff_h264_h_loop_filter_luma_neon;
    c->h264_v_loop_filter_chroma = ff_h264_v_loop_filter_chroma_neon;
    c->h264_h_loop_filter_chroma = ff_h264_h_loop_filter_chroma_neon;

    c->weight_h264_pixels_tab[0] = ff_weight_h264_pixels_16x16_neon;
    c->weight_h264_pixels_tab[1] = ff_weight_h264_pixels_16x8_neon;
    c->weight_h264_pixels_tab[2] = ff_weight_h264_pixels_8x16_neon;
    c->weight_h264_pixels_tab[3] = ff_weight_h264_pixels_8x8_neon;
    c->weight_h264_pixels_tab[4] = ff_weight_h264_pixels_8x4_neon;
    c->weight_h264_pixels_tab[5] = ff_weight_h264_pixels_4x8_neon;
    c->weight_h264_pixels_tab[6] = ff_weight_h264_pixels_4x4_neon;
    c->weight_h264_pixels_tab[7] = ff_weight_h264_pixels_4x2_neon;

    c->biweight_h264_pixels_tab[0] = ff_biweight_h264_pixels_16x16_neon;
    c->biweight_h264_pixels_tab[1] = ff_biweight_h264_pixels_16x8_neon;
    c->biweight_h264_pixels_tab[2] = ff_biweight_h264_pixels_8x16_neon;
    c->biweight_h264_pixels_tab[3] = ff_biweight_h264_pixels_8x8_neon;
    c->biweight_h264_pixels_tab[4] = ff_biweight_h264_pixels_8x4_neon;
    c->biweight_h264_pixels_tab[5] = ff_biweight_h264_pixels_4x8_neon;
    c->biweight_h264_pixels_tab[6] = ff_biweight_h264_pixels_4x4_neon;
    c->biweight_h264_pixels_tab[7] = ff_biweight_h264_pixels_4x2_neon;

    c->h264_idct_add        = ff_h264_idct_add_neon;
    c->h264_idct_dc_add     = ff_h264_idct_dc_add_neon;
    c->h264_idct_add16      = ff_h264_idct_add16_neon;
    c->h264_idct_add16intra = ff_h264_idct_add16intra_neon;
    c->h264_idct_add8       = ff_h264_idct_add8_neon;
    c->h264_idct8_add       = ff_h264_idct8_add_neon;
    c->h264_idct8_dc_add    = ff_h264_idct8_dc_add_neon;
    c->h264_idct8_add4      = ff_h264_idct8_add4_neon;
}

void ff_h264dsp_init_arm(H264DSPContext *c)
{
    if (HAVE_NEON) ff_h264dsp_init_neon(c);
}
