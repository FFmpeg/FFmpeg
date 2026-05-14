/*
 * Copyright (c) Lynne
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

#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

layout (push_constant, scalar) uniform pushConstants {
    uvec4 comp_off;
    uvec4 comp_plane;
    uvec4 ws_offset;
    uvec4 ws_stride;
    uint32_t ws_count;
    uint32_t t;
    uint32_t nb_components;
};

layout (set = 0, binding = 0) uniform readonly  image2D input_img[];
layout (set = 0, binding = 1) uniform writeonly image2D output_img[];

layout (set = 1, binding = 0, scalar) readonly buffer weights_buffer {
    float weights[];
};

layout (set = 1, binding = 1, scalar) readonly buffer sums_buffer {
    float sums[];
};

void main()
{
    const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    const uint plane = uint(gl_WorkGroupID.z);
    const ivec2 size = imageSize(output_img[plane]);

    uint c_off;
    uint c_plane;
    uint ws_off;

    float w_sum;
    float sum;
    vec4 src;
    vec4 r;
    uint invoc_idx;
    uint comp_idx;

    if (any(greaterThanEqual(pos, size)))
        return;

    src = imageLoad(input_img[plane], pos);
    for (comp_idx = 0; comp_idx < nb_components; comp_idx++) {
        if (plane == comp_plane[comp_idx]) {
            w_sum = 0.0;
            sum = 0.0;
            for (invoc_idx = 0; invoc_idx < t; invoc_idx++) {
                ws_off = ws_count * invoc_idx + ws_offset[comp_idx] + pos.y * ws_stride[comp_idx] + pos.x;
                w_sum += weights[ws_off];
                sum += sums[ws_off];
            }
            c_off = comp_off[comp_idx];
            r[c_off] = (sum + src[c_off] * 255) / (1.0 + w_sum) / 255;
        }
    }
    imageStore(output_img[plane], pos, r);
}
