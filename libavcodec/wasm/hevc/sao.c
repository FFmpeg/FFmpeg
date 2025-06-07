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

#include "sao.h"

#include <wasm_simd128.h>

#include "libavcodec/defs.h"

#define HEVC_MAX_PB_SIZE 64

void ff_hevc_sao_band_filter_8x8_8_simd128(uint8_t *dst, const uint8_t *src,
                                       ptrdiff_t stride_dst,
                                       ptrdiff_t stride_src,
                                       const int16_t *sao_offset_val,
                                       int sao_left_class, int width,
                                       int height)
{
    int8_t offset_table[32] = {0};
    v128_t offset_low, offset_high;

    for (int k = 0; k < 4; k++)
        offset_table[(k + sao_left_class) & 31] = (int8_t)sao_offset_val[k + 1];

    offset_low = wasm_v128_load(offset_table);
    offset_high = wasm_v128_load(&offset_table[16]);

    for (int y = height; y > 0; y -= 2) {
        v128_t src_v, src_high;
        v128_t v0, v1;

        src_v = wasm_v128_load64_zero(src);
        src += stride_src;
        src_v = wasm_v128_load64_lane(src, src_v, 1);
        src += stride_src;

        v0 = wasm_u8x16_shr(src_v, 3);
        v1 = wasm_i8x16_sub(v0, wasm_i8x16_const_splat(16));
        v0 = wasm_i8x16_swizzle(offset_low, v0);
        v1 = wasm_i8x16_swizzle(offset_high, v1);
        v0 = wasm_v128_or(v0, v1);
        src_high = wasm_u16x8_extend_high_u8x16(src_v);
        v1 = wasm_i16x8_extend_high_i8x16(v0);
        src_v = wasm_u16x8_extend_low_u8x16(src_v);
        v0 = wasm_i16x8_extend_low_i8x16(v0);

        v0 = wasm_i16x8_add_sat(src_v, v0);
        v1 = wasm_i16x8_add_sat(src_high, v1);
        v0 = wasm_u8x16_narrow_i16x8(v0, v1);

        wasm_v128_store64_lane(dst, v0, 0);
        dst += stride_dst;
        wasm_v128_store64_lane(dst, v0, 1);
        dst += stride_dst;
    }
}

void ff_hevc_sao_band_filter_16x16_8_simd128(uint8_t *dst, const uint8_t *src,
                                           ptrdiff_t stride_dst,
                                           ptrdiff_t stride_src,
                                           const int16_t *sao_offset_val,
                                           int sao_left_class, int width,
                                           int height)
{
    int8_t offset_table[32] = {0};
    v128_t offset_low, offset_high;

    for (int k = 0; k < 4; k++)
        offset_table[(k + sao_left_class) & 31] = (int8_t)sao_offset_val[k + 1];

    offset_low = wasm_v128_load(offset_table);
    offset_high = wasm_v128_load(&offset_table[16]);

    for (int y = height; y > 0; y--) {
        for (int x = 0; x < width; x += 16) {
            v128_t src_v, src_high;
            v128_t v0, v1;

            src_v = wasm_v128_load(&src[x]);

            v0 = wasm_u8x16_shr(src_v, 3);
            v1 = wasm_i8x16_sub(v0, wasm_i8x16_const_splat(16));
            v0 = wasm_i8x16_swizzle(offset_low, v0);
            v1 = wasm_i8x16_swizzle(offset_high, v1);
            v0 = wasm_v128_or(v0, v1);
            src_high = wasm_u16x8_extend_high_u8x16(src_v);
            v1 = wasm_i16x8_extend_high_i8x16(v0);
            src_v = wasm_u16x8_extend_low_u8x16(src_v);
            v0 = wasm_i16x8_extend_low_i8x16(v0);

            v0 = wasm_i16x8_add_sat(src_v, v0);
            v1 = wasm_i16x8_add_sat(src_high, v1);
            v0 = wasm_u8x16_narrow_i16x8(v0, v1);
            wasm_v128_store(&dst[x], v0);
        }

        dst += stride_dst;
        src += stride_src;
    }
}

void ff_hevc_sao_edge_filter_8x8_8_simd128(uint8_t *dst, const uint8_t *src,
                                           ptrdiff_t stride_dst,
                                           const int16_t *sao_offset_val,
                                           int eo, int width, int height)
{
    static const int8_t pos[4][2][2] = {
            { { -1,  0 }, {  1, 0 } }, // horizontal
            { {  0, -1 }, {  0, 1 } }, // vertical
            { { -1, -1 }, {  1, 1 } }, // 45 degree
            { {  1, -1 }, { -1, 1 } }, // 135 degree
    };
    int a_stride, b_stride;
    ptrdiff_t stride_src = (2 * HEVC_MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    const v128_t edge_idx = wasm_u8x16_make(1, 2, 0, 3,
                                            4, 0, 0, 0,
                                            0, 0, 0, 0,
                                            0, 0, 0, 0);
    v128_t sao_offset = wasm_v128_load(sao_offset_val);
    v128_t one = wasm_i8x16_const_splat(1);
    v128_t two = wasm_i8x16_const_splat(2);

    a_stride = pos[eo][0][0] + pos[eo][0][1] * stride_src;
    b_stride = pos[eo][1][0] + pos[eo][1][1] * stride_src;
    for (int y = height; y > 0; y -= 2) {
        v128_t v0, v1, v2;
        v128_t diff0, diff1;

        v0 = wasm_v128_load64_zero(src);
        v1 = wasm_v128_load64_zero(src + a_stride);
        v2 = wasm_v128_load64_zero(src + b_stride);
        src += stride_src;
        v0 = wasm_v128_load64_lane(src, v0, 1);
        v1 = wasm_v128_load64_lane(src + a_stride, v1, 1);
        v2 = wasm_v128_load64_lane(src + b_stride, v2, 1);
        src += stride_src;

        diff0 = wasm_u8x16_gt(v0, v1);
        v1 = wasm_u8x16_lt(v0, v1);
        diff0 = wasm_i8x16_sub(v1, diff0);

        diff1 = wasm_u8x16_gt(v0, v2);
        v2 = wasm_u8x16_lt(v0, v2);
        diff1 = wasm_i8x16_sub(v2, diff1);

        v1 = wasm_i8x16_add(diff0, two);
        v1 = wasm_i8x16_add(v1, diff1);

        v2 = wasm_i8x16_swizzle(edge_idx, v1);  // offset_val
        v1 = wasm_i8x16_shl(v2, 1);             // Access int16_t
        v2 = wasm_i8x16_add(v1, one);           // Access upper half of int16_t
        diff0 = wasm_i8x16_shuffle(v1, v2, 0, 16, 1, 17, 2, 18, 3, 19, 4,
                                   20, 5, 21, 6, 22, 7, 23);
        diff1 = wasm_i8x16_shuffle(v1, v2, 8, 24, 9, 25, 10, 26, 11, 27,
                                   12, 28, 13, 29, 14, 30, 15, 31);
        v1 = wasm_u16x8_extend_high_u8x16(v0);
        v0 = wasm_u16x8_extend_low_u8x16(v0);
        diff0 = wasm_i8x16_swizzle(sao_offset, diff0);
        diff1 = wasm_i8x16_swizzle(sao_offset, diff1);

        v0 = wasm_i16x8_add_sat(v0, diff0);
        v1 = wasm_i16x8_add_sat(v1, diff1);
        v0 = wasm_u8x16_narrow_i16x8(v0, v1);

        wasm_v128_store64_lane(dst, v0, 0);
        dst += stride_dst;
        wasm_v128_store64_lane(dst, v0, 1);
        dst += stride_dst;
    }
}

void ff_hevc_sao_edge_filter_16x16_8_simd128(uint8_t *dst, const uint8_t *src,
                                           ptrdiff_t stride_dst,
                                           const int16_t *sao_offset_val,
                                           int eo, int width, int height)
{
    static const int8_t pos[4][2][2] = {
            { { -1,  0 }, {  1, 0 } }, // horizontal
            { {  0, -1 }, {  0, 1 } }, // vertical
            { { -1, -1 }, {  1, 1 } }, // 45 degree
            { {  1, -1 }, { -1, 1 } }, // 135 degree
    };
    int a_stride, b_stride;
    ptrdiff_t stride_src = (2 * HEVC_MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    const v128_t edge_idx = wasm_u8x16_make(1, 2, 0, 3,
                                            4, 0, 0, 0,
                                            0, 0, 0, 0,
                                            0, 0, 0, 0);
    v128_t sao_offset = wasm_v128_load(sao_offset_val);
    v128_t one = wasm_i8x16_const_splat(1);
    v128_t two = wasm_i8x16_const_splat(2);

    a_stride = pos[eo][0][0] + pos[eo][0][1] * stride_src;
    b_stride = pos[eo][1][0] + pos[eo][1][1] * stride_src;
    for (int y = height; y > 0; y--) {
        for (int x = 0; x < width; x += 16) {
            v128_t v0, v1, v2;
            v128_t diff0, diff1;

            v0 = wasm_v128_load(&src[x]);
            v1 = wasm_v128_load(&src[x + a_stride]);
            v2 = wasm_v128_load(&src[x + b_stride]);

            diff0 = wasm_u8x16_gt(v0, v1);
            v1 = wasm_u8x16_lt(v0, v1);
            diff0 = wasm_i8x16_sub(v1, diff0);

            diff1 = wasm_u8x16_gt(v0, v2);
            v2 = wasm_u8x16_lt(v0, v2);
            diff1 = wasm_i8x16_sub(v2, diff1);

            v1 = wasm_i8x16_add(diff0, two);
            v1 = wasm_i8x16_add(v1, diff1);

            v2 = wasm_i8x16_swizzle(edge_idx, v1);  // offset_val
            v1 = wasm_i8x16_shl(v2, 1);             // Access int16_t
            v2 = wasm_i8x16_add(v1, one);           // Access upper half of int16_t
            diff0 = wasm_i8x16_shuffle(v1, v2, 0, 16, 1, 17, 2, 18, 3, 19, 4,
                                       20, 5, 21, 6, 22, 7, 23);
            diff1 = wasm_i8x16_shuffle(v1, v2, 8, 24, 9, 25, 10, 26, 11, 27,
                                       12, 28, 13, 29, 14, 30, 15, 31);
            v1 = wasm_u16x8_extend_high_u8x16(v0);
            v0 = wasm_u16x8_extend_low_u8x16(v0);
            diff0 = wasm_i8x16_swizzle(sao_offset, diff0);
            diff1 = wasm_i8x16_swizzle(sao_offset, diff1);

            v0 = wasm_i16x8_add_sat(v0, diff0);
            v1 = wasm_i16x8_add_sat(v1, diff1);
            v0 = wasm_u8x16_narrow_i16x8(v0, v1);
            wasm_v128_store(&dst[x], v0);
        }

        src += stride_src;
        dst += stride_dst;
    }
}

