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

layout (set = 0, binding = 0) uniform sampler2D input_img[];
layout (set = 0, binding = 1) uniform writeonly image2D output_img[];

layout (push_constant, scalar) uniform pushConstants {
    vec2 dist;
    bool single_plane;
};

void distort_rgba(ivec2 pos, ivec2 size)
{
    const vec2 p = ((vec2(pos)/vec2(size)) - 0.5f)*2.0f;
    const vec2 o = p * (dist - 1.0f);

    vec4 res;
    res.r = texture(input_img[0], ((p - o)/2.0f) + 0.5f).r;
    res.g = texture(input_img[0], ((p    )/2.0f) + 0.5f).g;
    res.b = texture(input_img[0], ((p + o)/2.0f) + 0.5f).b;
    res.a = texture(input_img[0], ((p    )/2.0f) + 0.5f).a;
    imageStore(output_img[0], pos, res);
}

void distort_plane(ivec2 pos, ivec2 size)
{
    vec2 p = ((vec2(pos)/vec2(size)) - 0.5f)*2.0f;
    float d = sqrt(p.x*p.x + p.y*p.y);
    p *= d / (d*dist);
    vec4 res = texture(input_img[nonuniformEXT(gl_WorkGroupID.z)], (p/2.0f) + 0.5f);
    imageStore(output_img[nonuniformEXT(gl_WorkGroupID.z)], pos, res);
}

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(output_img[nonuniformEXT(gl_WorkGroupID.z)]);
    if (any(greaterThanEqual(pos, size)))
        return;

    if (single_plane) {
        distort_rgba(pos, pos);
    } else {
        vec2 npos = vec2(pos)/vec2(size);
        vec4 res = texture(input_img[0], npos);
        if (gl_WorkGroupID.z == 0)
            imageStore(output_img[0], pos, res);
        else
            distort_plane(pos, size);
    }
}
