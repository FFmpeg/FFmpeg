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

layout (constant_id = 0) const uint planes = 0;
layout (constant_id = 1) const bool has_alpha = false;

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

layout (set = 0, binding = 0) uniform readonly image2D main_img[];
layout (set = 0, binding = 1) uniform readonly image2D overlay_img[];
layout (set = 0, binding = 2) uniform writeonly image2D output_img[];

layout (push_constant, scalar) uniform pushConstants {
    ivec2 o_offset[4];
    ivec2 o_size[4];
};

void overlay_noalpha(uint i, ivec2 pos)
{
    if ((o_offset[i].x <= pos.x) && (o_offset[i].y <= pos.y) &&
        (pos.x < (o_offset[i].x + o_size[i].x)) &&
        (pos.y < (o_offset[i].y + o_size[i].y))) {
        vec4 res = imageLoad(overlay_img[i], pos - o_offset[i]);
        imageStore(output_img[i], pos, res);
    } else {
        vec4 res = imageLoad(main_img[i], pos);
        imageStore(output_img[i], pos, res);
    }
}

void overlay_alpha_opaque(uint i, ivec2 pos)
{
    vec4 res = imageLoad(main_img[i], pos);
    if ((o_offset[i].x <= pos.x) && (o_offset[i].y <= pos.y) &&
        (pos.x < (o_offset[i].x + o_size[i].x)) &&
        (pos.y < (o_offset[i].y + o_size[i].y))) {
        vec4 ovr = imageLoad(overlay_img[i], pos - o_offset[i]);
        res = ovr * ovr.a + res * (1.0f - ovr.a);
        res.a = 1.0f;
        imageStore(output_img[i], pos, res);
    }
    imageStore(output_img[i], pos, res);
}

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    for (uint i = 0; i < planes; i++) {
        if (any(greaterThanEqual(pos, imageSize(output_img[i]))))
            return;

        if (has_alpha)
            overlay_alpha_opaque(i, pos);
        else
            overlay_noalpha(i, pos);
    }
}
