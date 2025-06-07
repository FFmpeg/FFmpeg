/*
 * Copyright (c) 2025 Zhao Zhili
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

#ifndef AVCODEC_WASM_HEVC_SAO_H
#define AVCODEC_WASM_HEVC_SAO_H

#include <stddef.h>
#include <stdint.h>

void ff_hevc_sao_band_filter_8x8_8_simd128(uint8_t *_dst, const uint8_t *_src,
                                           ptrdiff_t _stride_dst,
                                           ptrdiff_t _stride_src,
                                           const int16_t *sao_offset_val,
                                           int sao_left_class, int width,
                                           int height);

void ff_hevc_sao_band_filter_16x16_8_simd128(uint8_t *_dst, const uint8_t *_src,
                                             ptrdiff_t _stride_dst,
                                             ptrdiff_t _stride_src,
                                             const int16_t *sao_offset_val,
                                             int sao_left_class, int width,
                                             int height);

void ff_hevc_sao_edge_filter_8x8_8_simd128(uint8_t *_dst, const uint8_t *_src,
                                       ptrdiff_t stride_dst,
                                       const int16_t *sao_offset_val,
                                       int eo, int width, int height);

void ff_hevc_sao_edge_filter_16x16_8_simd128(uint8_t *_dst, const uint8_t *_src,
                                             ptrdiff_t stride_dst,
                                             const int16_t *sao_offset_val,
                                             int eo, int width, int height);

#endif

