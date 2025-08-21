/*
 * Copyright (c) 2025 Zhao Zhili <quinkblack@foxmail.com>
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

#include "libavutil/aarch64/cpu.h"
#include "libavfilter/vf_colordetect.h"

int ff_detect_alpha_full_neon(const uint8_t *color, ptrdiff_t color_stride,
                              const uint8_t *alpha, ptrdiff_t alpha_stride,
                              ptrdiff_t width, ptrdiff_t height,
                              int alpha_max, int mpeg_range, int offset);

int ff_detect_alpha16_full_neon(const uint8_t *color, ptrdiff_t color_stride,
                                const uint8_t *alpha, ptrdiff_t alpha_stride,
                                ptrdiff_t width, ptrdiff_t height,
                                int alpha_max, int mpeg_range, int offset);

int ff_detect_alpha_limited_neon(const uint8_t *color, ptrdiff_t color_stride,
                                 const uint8_t *alpha, ptrdiff_t alpha_stride,
                                 ptrdiff_t width, ptrdiff_t height,
                                 int alpha_max, int mpeg_range, int offset);

int ff_detect_alpha16_limited_neon(const uint8_t *color, ptrdiff_t color_stride,
                                   const uint8_t *alpha, ptrdiff_t alpha_stride,
                                   ptrdiff_t width, ptrdiff_t height,
                                   int alpha_max, int mpeg_range, int offset);

int ff_detect_range_neon(const uint8_t *data, ptrdiff_t stride,
                         ptrdiff_t width, ptrdiff_t height,
                         int mpeg_min, int mpeg_max);

int ff_detect_range16_neon(const uint8_t *data, ptrdiff_t stride,
                           ptrdiff_t width, ptrdiff_t height,
                           int mpeg_min, int mpeg_max);

av_cold void ff_color_detect_dsp_init_aarch64(FFColorDetectDSPContext *dsp, int depth,
                                          enum AVColorRange color_range)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        dsp->detect_range = depth > 8 ? ff_detect_range16_neon : ff_detect_range_neon;
        if (color_range == AVCOL_RANGE_JPEG)
            dsp->detect_alpha = depth > 8 ? ff_detect_alpha16_full_neon : ff_detect_alpha_full_neon;
        else
            dsp->detect_alpha = depth > 8 ? ff_detect_alpha16_limited_neon : ff_detect_alpha_limited_neon;
    }
}
