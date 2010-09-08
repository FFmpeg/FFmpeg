/*
 * Copyright (c) 2010 Jason Garrett-Glaser
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

#include "libavutil/cpu.h"
#include "libavcodec/h264pred.h"

void ff_pred16x16_vertical_mmx     (uint8_t *src, int stride);
void ff_pred16x16_vertical_sse     (uint8_t *src, int stride);
void ff_pred16x16_horizontal_mmx   (uint8_t *src, int stride);
void ff_pred16x16_horizontal_mmxext(uint8_t *src, int stride);
void ff_pred16x16_horizontal_ssse3 (uint8_t *src, int stride);
void ff_pred16x16_dc_mmxext        (uint8_t *src, int stride);
void ff_pred16x16_dc_sse2          (uint8_t *src, int stride);
void ff_pred16x16_dc_ssse3         (uint8_t *src, int stride);
void ff_pred16x16_tm_vp8_mmx       (uint8_t *src, int stride);
void ff_pred16x16_tm_vp8_mmxext    (uint8_t *src, int stride);
void ff_pred16x16_tm_vp8_sse2      (uint8_t *src, int stride);
void ff_pred8x8_dc_rv40_mmxext     (uint8_t *src, int stride);
void ff_pred8x8_vertical_mmx       (uint8_t *src, int stride);
void ff_pred8x8_horizontal_mmx     (uint8_t *src, int stride);
void ff_pred8x8_horizontal_mmxext  (uint8_t *src, int stride);
void ff_pred8x8_horizontal_ssse3   (uint8_t *src, int stride);
void ff_pred8x8_tm_vp8_mmx         (uint8_t *src, int stride);
void ff_pred8x8_tm_vp8_mmxext      (uint8_t *src, int stride);
void ff_pred8x8_tm_vp8_sse2        (uint8_t *src, int stride);
void ff_pred8x8_tm_vp8_ssse3       (uint8_t *src, int stride);
void ff_pred4x4_dc_mmxext          (uint8_t *src, const uint8_t *topright, int stride);
void ff_pred4x4_tm_vp8_mmx         (uint8_t *src, const uint8_t *topright, int stride);
void ff_pred4x4_tm_vp8_mmxext      (uint8_t *src, const uint8_t *topright, int stride);
void ff_pred4x4_tm_vp8_ssse3       (uint8_t *src, const uint8_t *topright, int stride);
void ff_pred4x4_vertical_vp8_mmxext(uint8_t *src, const uint8_t *topright, int stride);

void ff_h264_pred_init_x86(H264PredContext *h, int codec_id)
{
    int mm_flags = av_get_cpu_flags();

#if HAVE_YASM
    if (mm_flags & AV_CPU_FLAG_MMX) {
        h->pred16x16[VERT_PRED8x8] = ff_pred16x16_vertical_mmx;
        h->pred16x16[HOR_PRED8x8 ] = ff_pred16x16_horizontal_mmx;
        h->pred8x8  [VERT_PRED8x8] = ff_pred8x8_vertical_mmx;
        h->pred8x8  [HOR_PRED8x8 ] = ff_pred8x8_horizontal_mmx;
        if (codec_id == CODEC_ID_VP8) {
            h->pred16x16[PLANE_PRED8x8] = ff_pred16x16_tm_vp8_mmx;
            h->pred8x8  [PLANE_PRED8x8] = ff_pred8x8_tm_vp8_mmx;
            h->pred4x4  [TM_VP8_PRED  ] = ff_pred4x4_tm_vp8_mmx;
        }
    }

    if (mm_flags & AV_CPU_FLAG_MMX2) {
        h->pred16x16[HOR_PRED8x8 ] = ff_pred16x16_horizontal_mmxext;
        h->pred16x16[DC_PRED8x8  ] = ff_pred16x16_dc_mmxext;
        h->pred8x8  [HOR_PRED8x8 ] = ff_pred8x8_horizontal_mmxext;
        h->pred4x4  [DC_PRED     ] = ff_pred4x4_dc_mmxext;
        if (codec_id == CODEC_ID_VP8) {
            h->pred16x16[PLANE_PRED8x8] = ff_pred16x16_tm_vp8_mmxext;
            h->pred8x8  [DC_PRED8x8   ] = ff_pred8x8_dc_rv40_mmxext;
            h->pred8x8  [PLANE_PRED8x8] = ff_pred8x8_tm_vp8_mmxext;
            h->pred4x4  [TM_VP8_PRED  ] = ff_pred4x4_tm_vp8_mmxext;
            h->pred4x4  [VERT_PRED    ] = ff_pred4x4_vertical_vp8_mmxext;
        }
    }

    if (mm_flags & AV_CPU_FLAG_SSE) {
        h->pred16x16[VERT_PRED8x8] = ff_pred16x16_vertical_sse;
    }

    if (mm_flags & AV_CPU_FLAG_SSE2) {
        h->pred16x16[DC_PRED8x8  ] = ff_pred16x16_dc_sse2;
        if (codec_id == CODEC_ID_VP8) {
            h->pred16x16[PLANE_PRED8x8] = ff_pred16x16_tm_vp8_sse2;
            h->pred8x8  [PLANE_PRED8x8] = ff_pred8x8_tm_vp8_sse2;
        }
    }

    if (mm_flags & AV_CPU_FLAG_SSSE3) {
        h->pred16x16[HOR_PRED8x8 ] = ff_pred16x16_horizontal_ssse3;
        h->pred16x16[DC_PRED8x8  ] = ff_pred16x16_dc_ssse3;
        h->pred8x8  [HOR_PRED8x8 ] = ff_pred8x8_horizontal_ssse3;
        if (codec_id == CODEC_ID_VP8) {
            h->pred8x8  [PLANE_PRED8x8] = ff_pred8x8_tm_vp8_ssse3;
            h->pred4x4  [TM_VP8_PRED  ] = ff_pred4x4_tm_vp8_ssse3;
        }
    }
#endif
}
