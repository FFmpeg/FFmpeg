/*
 * Copyright (c) 2021 Wu Jianhua <jianhua.wu@intel.com>
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

#define TRANSPOSE_CCLOCK_FLIP 0
#define TRANSPOSE_CLOCK 1
#define TRANSPOSE_CCLOCK 2
#define TRANSPOSE_CLOCK_FLIP 3
#define TRANSPOSE_REVERSAL 4
#define TRANSPOSE_HFLIP 5
#define TRANSPOSE_VFLIP 6

layout (push_constant, scalar) uniform pushConstants {
    int dir;
};

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (dir == TRANSPOSE_CCLOCK || dir == TRANSPOSE_CLOCK || dir == TRANSPOSE_CLOCK_FLIP)
        pos = pos.yx;

    ivec2 size = imageSize(input_img[nonuniformEXT(gl_LocalInvocationID.z)]);
    if (any(greaterThanEqual(pos, size)))
        return;

    ivec2 dst;
    switch (dir) {
    case TRANSPOSE_CCLOCK:     dst = ivec2(size.y - pos.y, pos.x); break;
    case TRANSPOSE_CLOCK:      pos = ivec2(pos.x, size.y - pos.y); /* fall */
    case TRANSPOSE_CLOCK_FLIP: dst = ivec2(size.yx - pos.yx);      break;
    default:                   dst = pos.yx;                       break;
    }

    vec4 res = imageLoad(input_img[nonuniformEXT(gl_LocalInvocationID.z)], pos);

    imageStore(output_img[nonuniformEXT(gl_LocalInvocationID.z)], dst, res);
}
