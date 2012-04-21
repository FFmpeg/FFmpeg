/*
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "libavutil/arm/cpu.h"
#include "libavcodec/h264pred.h"

void ff_pred16x16_vert_neon(uint8_t *src, int stride);
void ff_pred16x16_hor_neon(uint8_t *src, int stride);
void ff_pred16x16_plane_neon(uint8_t *src, int stride);
void ff_pred16x16_dc_neon(uint8_t *src, int stride);
void ff_pred16x16_128_dc_neon(uint8_t *src, int stride);
void ff_pred16x16_left_dc_neon(uint8_t *src, int stride);
void ff_pred16x16_top_dc_neon(uint8_t *src, int stride);

void ff_pred8x8_vert_neon(uint8_t *src, int stride);
void ff_pred8x8_hor_neon(uint8_t *src, int stride);
void ff_pred8x8_plane_neon(uint8_t *src, int stride);
void ff_pred8x8_dc_neon(uint8_t *src, int stride);
void ff_pred8x8_128_dc_neon(uint8_t *src, int stride);
void ff_pred8x8_left_dc_neon(uint8_t *src, int stride);
void ff_pred8x8_top_dc_neon(uint8_t *src, int stride);
void ff_pred8x8_l0t_dc_neon(uint8_t *src, int stride);
void ff_pred8x8_0lt_dc_neon(uint8_t *src, int stride);
void ff_pred8x8_l00_dc_neon(uint8_t *src, int stride);
void ff_pred8x8_0l0_dc_neon(uint8_t *src, int stride);

static void ff_h264_pred_init_neon(H264PredContext *h, int codec_id, const int bit_depth, const int chroma_format_idc)
{
    const int high_depth = bit_depth > 8;

    if (high_depth)
        return;

    h->pred8x8[VERT_PRED8x8     ] = ff_pred8x8_vert_neon;
    h->pred8x8[HOR_PRED8x8      ] = ff_pred8x8_hor_neon;
    if (codec_id != CODEC_ID_VP8)
        h->pred8x8[PLANE_PRED8x8] = ff_pred8x8_plane_neon;
    h->pred8x8[DC_128_PRED8x8   ] = ff_pred8x8_128_dc_neon;
    if (codec_id != CODEC_ID_RV40 && codec_id != CODEC_ID_VP8) {
        h->pred8x8[DC_PRED8x8     ] = ff_pred8x8_dc_neon;
        h->pred8x8[LEFT_DC_PRED8x8] = ff_pred8x8_left_dc_neon;
        h->pred8x8[TOP_DC_PRED8x8 ] = ff_pred8x8_top_dc_neon;
        h->pred8x8[ALZHEIMER_DC_L0T_PRED8x8] = ff_pred8x8_l0t_dc_neon;
        h->pred8x8[ALZHEIMER_DC_0LT_PRED8x8] = ff_pred8x8_0lt_dc_neon;
        h->pred8x8[ALZHEIMER_DC_L00_PRED8x8] = ff_pred8x8_l00_dc_neon;
        h->pred8x8[ALZHEIMER_DC_0L0_PRED8x8] = ff_pred8x8_0l0_dc_neon;
    }

    h->pred16x16[DC_PRED8x8     ] = ff_pred16x16_dc_neon;
    h->pred16x16[VERT_PRED8x8   ] = ff_pred16x16_vert_neon;
    h->pred16x16[HOR_PRED8x8    ] = ff_pred16x16_hor_neon;
    h->pred16x16[LEFT_DC_PRED8x8] = ff_pred16x16_left_dc_neon;
    h->pred16x16[TOP_DC_PRED8x8 ] = ff_pred16x16_top_dc_neon;
    h->pred16x16[DC_128_PRED8x8 ] = ff_pred16x16_128_dc_neon;
    if (codec_id != CODEC_ID_SVQ3 && codec_id != CODEC_ID_RV40 && codec_id != CODEC_ID_VP8)
        h->pred16x16[PLANE_PRED8x8  ] = ff_pred16x16_plane_neon;
}

void ff_h264_pred_init_arm(H264PredContext *h, int codec_id, int bit_depth, const int chroma_format_idc)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags))
        ff_h264_pred_init_neon(h, codec_id, bit_depth, chroma_format_idc);
}
