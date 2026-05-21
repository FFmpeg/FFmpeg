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
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_EXT_null_initializer : require

layout (constant_id = 0) const uint plane = 0;
layout (constant_id = 1) const uint slices = 0;

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

layout (set = 0, binding = 0) uniform readonly image2D input_img[];
layout (set = 0, binding = 1, scalar) buffer sum_buffer {
    uint slice_sum[];
};

layout (push_constant, scalar) uniform pushConstants {
    float threshold;
};

shared uint wg_sum = { };

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    /* oob invocs still must reach the barrier, but mustn't
     * get counted in, threshold is positive, so the fake value of 0.0 would
     * otherwise be counted as black */
    bool in_bounds = all(lessThan(pos, imageSize(input_img[plane])));
    float value = 0.0f;
    if (in_bounds)
        value = imageLoad(input_img[plane], pos).x;

    uvec4 isblack = subgroupBallot(in_bounds && value <= threshold);
    if (subgroupElect())
        atomicAdd(wg_sum, subgroupBallotBitCount(isblack));

    barrier();
    if (gl_LocalInvocationIndex == 0)
        atomicAdd(slice_sum[gl_WorkGroupID.x % slices], wg_sum);
}
