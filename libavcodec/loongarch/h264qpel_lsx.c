/*
 * Loongson LSX optimized h264qpel
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 * Contributed by Hecai Yuan <yuanhecai@loongson.cn>
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

#include "h264qpel_loongarch.h"
#include "libavutil/loongarch/loongson_intrinsics.h"
#include "libavutil/attributes.h"

static void put_h264_qpel16_hv_lowpass_lsx(uint8_t *dst, const uint8_t *src,
                                           ptrdiff_t dstStride, ptrdiff_t srcStride)
{
    put_h264_qpel8_hv_lowpass_lsx(dst, src, dstStride, srcStride);
    put_h264_qpel8_hv_lowpass_lsx(dst + 8, src + 8, dstStride, srcStride);
    src += srcStride << 3;
    dst += dstStride << 3;
    put_h264_qpel8_hv_lowpass_lsx(dst, src, dstStride, srcStride);
    put_h264_qpel8_hv_lowpass_lsx(dst + 8, src + 8, dstStride, srcStride);
}

void ff_put_h264_qpel16_mc22_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    put_h264_qpel16_hv_lowpass_lsx(dst, src, stride, stride);
}

static void put_h264_qpel16_h_lowpass_lsx(uint8_t *dst, const uint8_t *src,
                                          int dstStride, int srcStride)
{
    put_h264_qpel8_h_lowpass_lsx(dst, src, dstStride, srcStride);
    put_h264_qpel8_h_lowpass_lsx(dst+8, src+8, dstStride, srcStride);
    src += srcStride << 3;
    dst += dstStride << 3;
    put_h264_qpel8_h_lowpass_lsx(dst, src, dstStride, srcStride);
    put_h264_qpel8_h_lowpass_lsx(dst+8, src+8, dstStride, srcStride);
}

static void put_h264_qpel16_v_lowpass_lsx(uint8_t *dst, const uint8_t *src,
                                           int dstStride, int srcStride)
{
    put_h264_qpel8_v_lowpass_lsx(dst, (uint8_t*)src, dstStride, srcStride);
    put_h264_qpel8_v_lowpass_lsx(dst+8, (uint8_t*)src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    put_h264_qpel8_v_lowpass_lsx(dst, (uint8_t*)src, dstStride, srcStride);
    put_h264_qpel8_v_lowpass_lsx(dst+8, (uint8_t*)src+8, dstStride, srcStride);
}

void ff_put_h264_qpel16_mc21_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 256;

    put_h264_qpel16_h_lowpass_lsx(halfH, src, 16, stride);
    put_h264_qpel16_hv_lowpass_lsx(halfHV, src, 16, stride);
    put_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_put_h264_qpel16_mc12_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 256;

    put_h264_qpel16_hv_lowpass_lsx(halfHV, src, 16, stride);
    put_h264_qpel16_v_lowpass_lsx(halfH, src, 16, stride);
    put_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_put_h264_qpel16_mc32_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 256;

    put_h264_qpel16_hv_lowpass_lsx(halfHV, src, 16, stride);
    put_h264_qpel16_v_lowpass_lsx(halfH, src + 1, 16, stride);
    put_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_put_h264_qpel16_mc23_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 256;

    put_h264_qpel16_h_lowpass_lsx(halfH, src + stride, 16, stride);
    put_h264_qpel16_hv_lowpass_lsx(halfHV, src, 16, stride);
    put_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

static void avg_h264_qpel16_v_lowpass_lsx(uint8_t *dst, const uint8_t *src,
                                          int dstStride, int srcStride)
{
    avg_h264_qpel8_v_lowpass_lsx(dst, (uint8_t*)src, dstStride, srcStride);
    avg_h264_qpel8_v_lowpass_lsx(dst+8, (uint8_t*)src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    avg_h264_qpel8_v_lowpass_lsx(dst, (uint8_t*)src, dstStride, srcStride);
    avg_h264_qpel8_v_lowpass_lsx(dst+8, (uint8_t*)src+8, dstStride, srcStride);
}

void ff_avg_h264_qpel16_mc02_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avg_h264_qpel16_v_lowpass_lsx(dst, src, stride, stride);
}

void ff_avg_h264_qpel16_mc03_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t half[256];

    put_h264_qpel16_v_lowpass_lsx(half, src, 16, stride);
    avg_pixels16_l2_8_lsx(dst, src + stride, half, stride, stride);
}

void ff_avg_h264_qpel16_mc23_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 256;

    put_h264_qpel16_h_lowpass_lsx(halfH, src + stride, 16, stride);
    put_h264_qpel16_hv_lowpass_lsx(halfHV, src, 16, stride);
    avg_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_avg_h264_qpel16_mc21_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 256;

    put_h264_qpel16_h_lowpass_lsx(halfH, src, 16, stride);
    put_h264_qpel16_hv_lowpass_lsx(halfHV, src, 16, stride);
    avg_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_avg_h264_qpel16_mc01_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t half[256];

    put_h264_qpel16_v_lowpass_lsx(half, src, 16, stride);
    avg_pixels16_l2_8_lsx(dst, src, half, stride, stride);
}

void ff_avg_h264_qpel16_mc32_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 256;

    put_h264_qpel16_hv_lowpass_lsx(halfHV, src, 16, stride);
    put_h264_qpel16_v_lowpass_lsx(halfH, src + 1, 16, stride);
    avg_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

void ff_avg_h264_qpel16_mc12_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t temp[512];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 256;

    put_h264_qpel16_hv_lowpass_lsx(halfHV, src, 16, stride);
    put_h264_qpel16_v_lowpass_lsx(halfH, src, 16, stride);
    avg_pixels16_l2_8_lsx(dst, halfH, halfHV, stride, 16);
}

static void avg_h264_qpel16_hv_lowpass_lsx(uint8_t *dst, const uint8_t *src,
                                           ptrdiff_t dstStride, ptrdiff_t srcStride)
{
    avg_h264_qpel8_hv_lowpass_lsx(dst, src, dstStride, srcStride);
    avg_h264_qpel8_hv_lowpass_lsx(dst + 8, src + 8, dstStride, srcStride);
    src += srcStride << 3;
    dst += dstStride << 3;
    avg_h264_qpel8_hv_lowpass_lsx(dst, src, dstStride, srcStride);
    avg_h264_qpel8_hv_lowpass_lsx(dst + 8, src + 8, dstStride, srcStride);
}

void ff_avg_h264_qpel16_mc22_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    avg_h264_qpel16_hv_lowpass_lsx(dst, src, stride, stride);
}

void ff_put_h264_qpel8_mc03_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t half[64];

    put_h264_qpel8_v_lowpass_lsx(half, (uint8_t*)src, 8, stride);
    put_pixels8_l2_8_lsx(dst, src + stride, half, stride, stride);
}

void ff_put_h264_qpel8_mc01_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t half[64];

    put_h264_qpel8_v_lowpass_lsx(half, (uint8_t*)src, 8, stride);
    put_pixels8_l2_8_lsx(dst, src, half, stride, stride);
}

void ff_put_h264_qpel8_mc30_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t half[64];

    put_h264_qpel8_h_lowpass_lsx(half, src, 8, stride);
    put_pixels8_l2_8_lsx(dst, src+1, half, stride, stride);
}

void ff_put_h264_qpel8_mc10_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t half[64];

    put_h264_qpel8_h_lowpass_lsx(half, src, 8, stride);
    put_pixels8_l2_8_lsx(dst, src, half, stride, stride);
}

void ff_put_h264_qpel8_mc33_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lsx(halfH, src + stride, 8, stride);
    put_h264_qpel8_v_lowpass_lsx(halfV, (uint8_t*)src + 1, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_put_h264_qpel8_mc13_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lsx(halfH, src + stride, 8, stride);
    put_h264_qpel8_v_lowpass_lsx(halfV, (uint8_t*)src, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_put_h264_qpel8_mc31_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lsx(halfH, src, 8, stride);
    put_h264_qpel8_v_lowpass_lsx(halfV, (uint8_t*)src + 1, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_put_h264_qpel8_mc11_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lsx(halfH, src, 8, stride);
    put_h264_qpel8_v_lowpass_lsx(halfV, (uint8_t*)src, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_put_h264_qpel8_mc32_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 64;

    put_h264_qpel8_hv_lowpass_lsx(halfHV, src, 8, stride);
    put_h264_qpel8_v_lowpass_lsx(halfH, (uint8_t*)src + 1, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_put_h264_qpel8_mc21_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 64;

    put_h264_qpel8_h_lowpass_lsx(halfH, src, 8, stride);
    put_h264_qpel8_hv_lowpass_lsx(halfHV, src, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_put_h264_qpel8_mc23_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 64;

    put_h264_qpel8_h_lowpass_lsx(halfH, src + stride, 8, stride);
    put_h264_qpel8_hv_lowpass_lsx(halfHV, src, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_put_h264_qpel8_mc12_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 64;

    put_h264_qpel8_hv_lowpass_lsx(halfHV, src, 8, stride);
    put_h264_qpel8_v_lowpass_lsx(halfH, (uint8_t*)src, 8, stride);
    put_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_put_h264_qpel8_mc02_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    put_h264_qpel8_v_lowpass_lsx(dst, (uint8_t*)src, stride, stride);
}

void ff_put_h264_qpel8_mc22_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    put_h264_qpel8_hv_lowpass_lsx(dst, src, stride, stride);
}

void ff_put_h264_qpel8_mc20_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    put_h264_qpel8_h_lowpass_lsx(dst, src, stride, stride);
}

void ff_avg_h264_qpel8_mc10_lsx(uint8_t *dst, const uint8_t *src,
                                 ptrdiff_t stride)
{
    uint8_t half[64];

    put_h264_qpel8_h_lowpass_lsx(half, src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, src, half, stride, stride);
}

void ff_avg_h264_qpel8_mc20_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avg_h264_qpel8_h_lowpass_lsx(dst, src, stride, stride);
}

void ff_avg_h264_qpel8_mc30_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t half[64];

    put_h264_qpel8_h_lowpass_lsx(half, src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, src+1, half, stride, stride);
}

void ff_avg_h264_qpel8_mc11_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lsx(halfH, src, 8, stride);
    put_h264_qpel8_v_lowpass_lsx(halfV, (uint8_t*)src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_avg_h264_qpel8_mc21_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 64;

    put_h264_qpel8_h_lowpass_lsx(halfH, src, 8, stride);
    put_h264_qpel8_hv_lowpass_lsx(halfHV, src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_avg_h264_qpel8_mc31_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lsx(halfH, src, 8, stride);
    put_h264_qpel8_v_lowpass_lsx(halfV, (uint8_t*)src + 1, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_avg_h264_qpel8_mc02_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avg_h264_qpel8_v_lowpass_lsx(dst, (uint8_t*)src, stride, stride);
}

void ff_avg_h264_qpel8_mc12_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 64;

    put_h264_qpel8_hv_lowpass_lsx(halfHV, src, 8, stride);
    put_h264_qpel8_v_lowpass_lsx(halfH, (uint8_t*)src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_avg_h264_qpel8_mc22_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    avg_h264_qpel8_hv_lowpass_lsx(dst, src, stride, stride);
}

void ff_avg_h264_qpel8_mc32_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfHV = temp;
    uint8_t *const halfH  = temp + 64;

    put_h264_qpel8_hv_lowpass_lsx(halfHV, src, 8, stride);
    put_h264_qpel8_v_lowpass_lsx(halfH, (uint8_t*)src + 1, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_avg_h264_qpel8_mc13_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lsx(halfH, src + stride, 8, stride);
    put_h264_qpel8_v_lowpass_lsx(halfV, (uint8_t*)src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}

void ff_avg_h264_qpel8_mc23_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t temp[128];
    uint8_t *const halfH  = temp;
    uint8_t *const halfHV = temp + 64;

    put_h264_qpel8_h_lowpass_lsx(halfH, src + stride, 8, stride);
    put_h264_qpel8_hv_lowpass_lsx(halfHV, src, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfHV, stride, 8);
}

void ff_avg_h264_qpel8_mc33_lsx(uint8_t *dst, const uint8_t *src,
                                ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfV[64];

    put_h264_qpel8_h_lowpass_lsx(halfH, src + stride, 8, stride);
    put_h264_qpel8_v_lowpass_lsx(halfV, (uint8_t*)src + 1, 8, stride);
    avg_pixels8_l2_8_lsx(dst, halfH, halfV, stride, 8);
}
