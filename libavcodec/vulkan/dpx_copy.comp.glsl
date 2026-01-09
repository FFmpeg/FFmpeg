/*
 * Copyright (c) 2025 Lynne <dev@lynne.ee>
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

#version 460
#pragma shader_stage(compute)
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nontemporal_keyword : require

#include "common.comp"

layout (constant_id = 0) const bool big_endian = false;
layout (constant_id = 1) const int type_bits = 0;

layout (set = 0, binding = 0) uniform writeonly uimage2D dst[];
layout (set = 0, binding = 1, scalar) nontemporal readonly buffer data_buf8 {
    uint8_t data8[];
};
layout (set = 0, binding = 2, scalar) nontemporal readonly buffer data_buf16 {
    uint16_t data16[];
};
layout (set = 0, binding = 3, scalar) nontemporal readonly buffer data_buf32 {
    uint32_t data32[];
};

layout (push_constant, scalar) uniform pushConstants {
    int bits_per_comp;
    int nb_comp;
    int nb_images;
    int stride;
    int need_align;
    int padded_10bit;
    int shift;
};

#define READ_FN(bits, bytes)                          \
uint read_val##bits(uint off)                         \
{                                                     \
    if (big_endian)                                   \
        return uint(reverse##bytes(data##bits[off])); \
    return uint(data##bits[off]);                     \
}
READ_FN(16, 2)
READ_FN(32, 4)

uint read_data(uint off)
{
    if (type_bits == 8)
        return uint(data8[off]);
    else if (type_bits == 16)
        return read_val16(off);
    return read_val32(off);
}

void main(void)
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    uint linesize;
    linesize = align(imageSize(dst[0]).x*bits_per_comp*nb_comp, 32);

    uint offs = pos.y*linesize + pos.x*nb_comp*bits_per_comp;
    offs /= bits_per_comp;

    if (nb_images == 1) {
        uvec4 val;
        for (int i = 0; i < nb_comp; i++)
            val[i] = read_data(offs + i);
        val >>= shift;
        imageStore(dst[0], pos, val);
    } else {
        const ivec4 fmt_lut = ivec4(2, 0, 1, 3);
        for (int i = 0; i < nb_comp; i++) {
            uint32_t val = read_data(offs + i);
            val >>= shift;
            imageStore(dst[fmt_lut[i]], pos, uvec4(val));
        }
    }
}
