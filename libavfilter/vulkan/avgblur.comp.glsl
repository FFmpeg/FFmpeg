/*
 * Copyright (c) 2026 Lynne <dev@lynne.ee>
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

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

layout (set = 0, binding = 0) uniform readonly  image2D input_img[];
layout (set = 0, binding = 1) uniform writeonly image2D output_img[];

layout (push_constant, scalar) uniform pushConstants {
    vec4 filter_norm;
    ivec2 filter_len;
    uint planes;
};

void main()
{
    const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    ivec2 size = imageSize(output_img[nonuniformEXT(gl_LocalInvocationID.z)]);
    if (any(greaterThanEqual(pos, size)))
        return;

    if ((planes & (1 << gl_LocalInvocationID.z)) == 0) {
        imageStore(output_img[gl_LocalInvocationID.z], pos,
                   imageLoad(input_img[nonuniformEXT(gl_LocalInvocationID.z)],
                             pos));
        return;
    }

    vec4 sum = vec4(0);
    for (int y = -filter_len.y; y <= filter_len.y; y++)
        for (int x = -filter_len.x; x <= filter_len.x; x++)
            sum += imageLoad(input_img[nonuniformEXT(gl_LocalInvocationID.z)],
                             pos + ivec2(x, y));

    imageStore(output_img[nonuniformEXT(gl_LocalInvocationID.z)],
               pos, sum * filter_norm);
}
