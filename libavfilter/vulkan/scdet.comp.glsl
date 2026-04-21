/*
 * Copyright 2025 (c) Niklas Haas
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
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_EXT_null_initializer : require

layout (constant_id = 0) const uint planes = 0;
layout (constant_id = 1) const uint slices = 0;

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

layout (set = 0, binding = 0) uniform readonly uimage2D prev_img[];
layout (set = 0, binding = 1) uniform readonly uimage2D cur_img[];
layout (set = 0, binding = 2, scalar) buffer sad_buffer {
    uint frame_sad[];
};

shared uint wg_sum = { };

void main()
{
    const uint slice = gl_WorkGroupID.x % slices;
    const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    for (uint i = 0; i < planes; i++) {
        const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
        if (all(lessThan(pos, imageSize(cur_img[i])))) {
            uvec4 prev = imageLoad(prev_img[i], pos);
            uvec4 cur  = imageLoad(cur_img[i],  pos);
            uvec4 sad = abs(ivec4(cur) - ivec4(prev));
            uint sum = subgroupAdd(sad.x + sad.y + sad.z);
            if (subgroupElect())
                atomicAdd(wg_sum, sum);
        }
    }

    barrier();
    if (gl_LocalInvocationIndex == 0)
        atomicAdd(frame_sad[slice], wg_sum);
}
