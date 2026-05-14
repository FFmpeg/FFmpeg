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
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_ARB_gpu_shader_int64 : require

/* Must be kept in sync with the definitions in vf_nlmeans_vulkan.c */
#define DTYPE vec4
#define T_ALIGN 16
#define TYPE_ELEMS 4

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

layout (buffer_reference, buffer_reference_align = T_ALIGN, scalar) buffer DataBuffer {
    DTYPE v[];
};

layout (push_constant, scalar) uniform pushConstants {
    uvec4 width;
    uvec4 height;
    uvec4 ws_offset;
    uvec4 ws_stride;
    ivec4 patch_size;
    vec4 strength;
    uvec4 comp_off;
    uvec4 comp_plane;
    DataBuffer integral_base;
    uint64_t integral_size;
    uint64_t int_stride;
    uint xyoffs_start;
    uint ws_count;
    uint nb_components;
};

layout (set = 0, binding = 0) uniform readonly image2D input_img[];

layout (set = 0, binding = 1, scalar) buffer weights_buffer {
    float weights[];
};

layout (set = 0, binding = 2, scalar) buffer sums_buffer {
    float sums[];
};

layout (set = 1, binding = 0, scalar) readonly buffer xyoffsets_buffer {
    ivec2 xyoffsets[];
};

void main()
{
    uint64_t offset;
    DataBuffer dst;
    uvec2 size;
    ivec2 pos;
    ivec2 pos_off;
    int p;
    float s;

    DataBuffer integral_data;
    ivec2 offs[TYPE_ELEMS];

    uint c_off;
    uint c_plane;
    uint ws_off;

    pos = ivec2(gl_GlobalInvocationID.xy);
    uint comp_idx = uint(gl_WorkGroupID.z) % nb_components;
    uint invoc_idx = uint(gl_WorkGroupID.z) / nb_components;

    c_off = comp_off[comp_idx];
    c_plane = comp_plane[comp_idx];
    p = patch_size[comp_idx];
    s = strength[comp_idx];
    if (s == 0.0 || pos.x < p || pos.y < p || pos.x >= width[c_plane] - p || pos.y >= height[c_plane] - p)
        return;

    offset = integral_size * (invoc_idx * nb_components + comp_idx);
    integral_data = DataBuffer(uint64_t(integral_base) + offset);
    for (uint i = 0; i < TYPE_ELEMS; i++)
        offs[i] = xyoffsets[xyoffs_start + TYPE_ELEMS*invoc_idx + i];

    ws_off = ws_count * invoc_idx + ws_offset[comp_idx] + pos.y * ws_stride[comp_idx] + pos.x;
    size = imageSize(input_img[c_plane]);

    DTYPE a;
    DTYPE b;
    DTYPE c;
    DTYPE d;

    DTYPE patch_diff;
    vec4 src;
    vec4 w;
    float w_sum;
    float sum;

    for (int i = 0; i < 4; i++) {
        pos_off = pos + offs[i];
        if (any(greaterThanEqual(uvec2(pos_off), size)))
            src[i] = imageLoad(input_img[c_plane], pos)[c_off];
        else
            src[i] = imageLoad(input_img[c_plane], pos_off)[c_off];
    }

    offset = int_stride * uint64_t(pos.y - p);
    dst = DataBuffer(uint64_t(integral_data) + offset);
    a = dst.v[pos.x - p];
    c = dst.v[pos.x + p];
    offset = int_stride * uint64_t(pos.y + p);
    dst = DataBuffer(uint64_t(integral_data) + offset);
    b = dst.v[pos.x - p];
    d = dst.v[pos.x + p];

    patch_diff = d + a - b - c;
    w = exp(patch_diff * s);
    w_sum = w[0] + w[1] + w[2] + w[3];
    sum = dot(w, src * 255);

    weights[ws_off] += w_sum;
    sums[ws_off] += sum;
}
