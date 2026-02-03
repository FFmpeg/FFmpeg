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

#pragma shader_stage(compute)
#extension GL_GOOGLE_include_directive : require

#include "common.comp"

layout (constant_id = 0) const bool big_endian = false;
layout (constant_id = 1) const bool packed_10bit = false;

layout (set = 0, binding = 0) uniform writeonly uimage2D dst[];
layout (set = 0, binding = 1, scalar) readonly buffer data_buf {
    uint32_t data[];
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

uint32_t read_data(uint off)
{
    if (big_endian)
        return reverse4(data[off]);
    return data[off];
}

i16vec4 parse_packed10_in_32(ivec2 pos, int stride)
{
    uint32_t d = read_data(pos.y*stride + pos.x);
    i16vec4 v;
    d = d << 10 | d >> 22 & 0x3FFFFF;
    v[0] = int16_t(d & 0x3FF);
    d = d << 10 | d >> 22 & 0x3FFFFF;
    v[1] = int16_t(d & 0x3FF);
    d = d << 10 | d >> 22 & 0x3FFFFF;
    v[2] = int16_t(d & 0x3FF);
    v[3] = int16_t(0);
    return v;
}

i16vec4 parse_packed_in_32(ivec2 pos, int stride)
{
    uint line_size = stride*bits_per_comp*nb_comp;
    line_size += line_size & 31;
    line_size += need_align << 3;

    uint line_off = pos.y*line_size;
    uint pix_off = pos.x*bits_per_comp*nb_comp;

    uint off = (line_off + pix_off) >> 5;
    uint bit = pix_off & 0x1f;

    uint32_t d0 = read_data(off + 0);
    uint32_t d1 = read_data(off + 1);

    uint64_t combined = (uint64_t(d1) << 32) | d0;
    combined >>= bit;

    return i16vec4(combined,
                   combined >> (bits_per_comp*1),
                   combined >> (bits_per_comp*2),
                   combined >> (bits_per_comp*3)) &
           int16_t((1 << bits_per_comp) - 1);
}

void main(void)
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pos, imageSize(dst[0]))))
        return;

    i16vec4 p;
    if (packed_10bit)
        p = parse_packed10_in_32(pos, imageSize(dst[0]).x);
    else
        p = parse_packed_in_32(pos, imageSize(dst[0]).x);

    if (nb_images == 1) {
        imageStore(dst[0], pos, p);
    } else {
        const ivec4 fmt_lut = ivec4(2, 0, 1, 3);
        for (uint i = 0; i < nb_comp; i++)
            imageStore(dst[fmt_lut[i]], pos, i16vec4(p[i]));
    }
}
