/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin <yinshiyou-hf@loongson.cn>
 *                Xiwei  Gu  <guxiwei-hf@loongson.cn>
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

#ifndef AVCODEC_LOONGARCH_H264DSP_LASX_H
#define AVCODEC_LOONGARCH_H264DSP_LASX_H

#include "libavcodec/h264dec.h"

void ff_h264_h_lpf_luma_8_lasx(uint8_t *src, ptrdiff_t stride,
                               int alpha, int beta, int8_t *tc0);
void ff_h264_v_lpf_luma_8_lasx(uint8_t *src, ptrdiff_t stride,
                               int alpha, int beta, int8_t *tc0);
void ff_h264_h_lpf_luma_intra_8_lasx(uint8_t *src, ptrdiff_t stride,
                                     int alpha, int beta);
void ff_h264_v_lpf_luma_intra_8_lasx(uint8_t *src, ptrdiff_t stride,
                                     int alpha, int beta);
void ff_h264_h_lpf_chroma_8_lasx(uint8_t *src, ptrdiff_t stride,
                                 int alpha, int beta, int8_t *tc0);
void ff_h264_v_lpf_chroma_8_lasx(uint8_t *src, ptrdiff_t stride,
                                 int alpha, int beta, int8_t *tc0);
void ff_h264_h_lpf_chroma_intra_8_lasx(uint8_t *src, ptrdiff_t stride,
                                       int alpha, int beta);
void ff_h264_v_lpf_chroma_intra_8_lasx(uint8_t *src, ptrdiff_t stride,
                                       int alpha, int beta);
void ff_biweight_h264_pixels16_8_lasx(uint8_t *dst, uint8_t *src,
                                      ptrdiff_t stride, int height,
                                      int log2_denom, int weight_dst,
                                      int weight_src, int offset_in);
void ff_biweight_h264_pixels8_8_lasx(uint8_t *dst, uint8_t *src,
                                     ptrdiff_t stride, int height,
                                     int log2_denom, int weight_dst,
                                     int weight_src, int offset);
void ff_biweight_h264_pixels4_8_lasx(uint8_t *dst, uint8_t *src,
                                     ptrdiff_t stride, int height,
                                     int log2_denom, int weight_dst,
                                     int weight_src, int offset);
void ff_weight_h264_pixels16_8_lasx(uint8_t *src, ptrdiff_t stride,
                                    int height, int log2_denom,
                                    int weight_src, int offset_in);
void ff_weight_h264_pixels8_8_lasx(uint8_t *src, ptrdiff_t stride,
                                   int height, int log2_denom,
                                   int weight_src, int offset);
void ff_weight_h264_pixels4_8_lasx(uint8_t *src, ptrdiff_t stride,
                                   int height, int log2_denom,
                                   int weight_src, int offset);
void ff_h264_add_pixels4_8_lasx(uint8_t *_dst, int16_t *_src, int stride);

void ff_h264_add_pixels8_8_lasx(uint8_t *_dst, int16_t *_src, int stride);
#endif  // #ifndef AVCODEC_LOONGARCH_H264DSP_LASX_H
