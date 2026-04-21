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
#extension GL_EXT_nonuniform_qualifier : require

#define VLPF_OFF 0
#define VLPF_LIN 1
#define VLPF_CMP 2

layout (constant_id = 0) const int lowpass = 0;
layout (constant_id = 1) const int planes = 0;

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

layout (set = 0, binding = 0) uniform sampler2D top_field[];
layout (set = 0, binding = 1) uniform sampler2D bot_field[];
layout (set = 0, binding = 2) uniform writeonly image2D output_img[];

vec4 get_line(sampler2D tex, const vec2 pos)
{
    if (lowpass == VLPF_CMP) {
        return  0.75  * texture(tex, pos) +
                0.25  * texture(tex, pos - ivec2(0, 1)) +
                0.25  * texture(tex, pos + ivec2(0, 1)) +
               -0.125 * texture(tex, pos - ivec2(0, 2)) +
               -0.125 * texture(tex, pos + ivec2(0, 2));
    } else if (lowpass == VLPF_LIN) {
        return 0.50 * texture(tex, pos) +
               0.25 * texture(tex, pos - ivec2(0, 1)) +
               0.25 * texture(tex, pos + ivec2(0, 1));
    } else {
        return texture(tex, pos);
    }
}

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    vec2 ipos = pos + vec2(0.5);

    for (int i = 0; i < planes; i++) {
        ivec2 size = imageSize(output_img[i]);
        if (any(greaterThanEqual(pos, size)))
            return;

        vec4 res;
        if ((pos.y % 2) == 0)
            res = get_line(top_field[i], ipos);
        else
            res = get_line(bot_field[i], ipos);
        imageStore(output_img[i], pos, res);
    }
}
