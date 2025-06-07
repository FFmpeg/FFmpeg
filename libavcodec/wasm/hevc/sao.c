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

