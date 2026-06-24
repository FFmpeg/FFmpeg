/*
 * Copyright (C) 2026 Philip Langdale <philipl@overt.org>
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

#extension GL_EXT_scalar_block_layout : require

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

/* The two source frames, sampled with normalized coordinates. Only the first
 * plane (luma for YUV inputs) is bound. */
layout (set = 0, binding = 0) uniform sampler2D in_frames[2];

/* The single channel grayscale images consumed by the optical flow session. */
layout (set = 0, binding = 1, r8) uniform writeonly image2D out_gray[2];

/* Luma weights: (1,0,0,0) selects plane 0 for YUV (already luma); RGB->Y
 * coefficients for RGB. Only feeds the flow engine as a matching signal and is
 * never output, so the host passes fixed BT.709 without consulting metadata. */
layout (push_constant, scalar) uniform pushConstants {
    vec4 luma_weights;
};

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    for (int i = 0; i < 2; i++) {
        ivec2 size = imageSize(out_gray[i]);
        if (any(greaterThanEqual(pos, size)))
            continue;

        /* A source image may be allocated larger than the visible frame -
         * eg: 1920x1088 for 1080p video - so reads have to be normalised
         * against the physical texture size, not the output image size. */
        vec2 uv = (vec2(pos) + 0.5) / vec2(textureSize(in_frames[i], 0));
        float luma = dot(texture(in_frames[i], uv).xyz, luma_weights.xyz) + luma_weights.w;
        imageStore(out_gray[i], pos, vec4(luma));
    }
}
