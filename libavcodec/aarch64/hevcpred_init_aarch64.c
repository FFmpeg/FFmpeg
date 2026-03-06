/*
 * HEVC Intra Prediction NEON initialization
 *
 * Copyright (c) 2026 Jun Zhao <barryjzhao@tencent.com>
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
#include "libavutil/avassert.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/hevc/pred.h"

// DC prediction
void ff_hevc_pred_dc_4x4_8_neon(uint8_t *src, const uint8_t *top,
                               const uint8_t *left, ptrdiff_t stride,
                               int c_idx);
void ff_hevc_pred_dc_8x8_8_neon(uint8_t *src, const uint8_t *top,
                               const uint8_t *left, ptrdiff_t stride,
                               int c_idx);
void ff_hevc_pred_dc_16x16_8_neon(uint8_t *src, const uint8_t *top,
                                const uint8_t *left, ptrdiff_t stride,
                                int c_idx);
void ff_hevc_pred_dc_32x32_8_neon(uint8_t *src, const uint8_t *top,
                                const uint8_t *left, ptrdiff_t stride,
                                int c_idx);

// Planar prediction
void ff_hevc_pred_planar_4x4_8_neon(uint8_t *src, const uint8_t *top,
                                   const uint8_t *left, ptrdiff_t stride);
void ff_hevc_pred_planar_8x8_8_neon(uint8_t *src, const uint8_t *top,
                                   const uint8_t *left, ptrdiff_t stride);
void ff_hevc_pred_planar_16x16_8_neon(uint8_t *src, const uint8_t *top,
                                    const uint8_t *left, ptrdiff_t stride);
void ff_hevc_pred_planar_32x32_8_neon(uint8_t *src, const uint8_t *top,
                                    const uint8_t *left, ptrdiff_t stride);

static void pred_dc_neon(uint8_t *src, const uint8_t *top,
                         const uint8_t *left, ptrdiff_t stride,
                         int log2_size, int c_idx)
{
    switch (log2_size) {
    case 2:
        ff_hevc_pred_dc_4x4_8_neon(src, top, left, stride, c_idx);
        break;
    case 3:
        ff_hevc_pred_dc_8x8_8_neon(src, top, left, stride, c_idx);
        break;
    case 4:
        ff_hevc_pred_dc_16x16_8_neon(src, top, left, stride, c_idx);
        break;
    case 5:
        ff_hevc_pred_dc_32x32_8_neon(src, top, left, stride, c_idx);
        break;
    default:
        av_unreachable("log2_size must be 2, 3, 4 or 5");
    }
}

av_cold void ff_hevc_pred_init_aarch64(HEVCPredContext *hpc, int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (!have_neon(cpu_flags))
        return;

    if (bit_depth == 8) {
        hpc->pred_dc        = pred_dc_neon;
        hpc->pred_planar[0] = ff_hevc_pred_planar_4x4_8_neon;
        hpc->pred_planar[1] = ff_hevc_pred_planar_8x8_8_neon;
        hpc->pred_planar[2] = ff_hevc_pred_planar_16x16_8_neon;
        hpc->pred_planar[3] = ff_hevc_pred_planar_32x32_8_neon;
    }
}
