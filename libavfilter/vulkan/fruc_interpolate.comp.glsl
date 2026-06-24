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

#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

layout (set = 0, binding = 0) uniform sampler2D f0_img[];
layout (set = 0, binding = 1) uniform sampler2D f1_img[];
layout (set = 0, binding = 2) uniform writeonly image2D out_img[];

/* Forward (f0 -> f1) and backward (f1 -> f0) flow fields. These are bound for
 * descriptor-set compatibility with the C code but are not consulted yet: this
 * initial implementation performs a plain temporal blend. Motion-compensated
 * warping using these fields is introduced in a subsequent change. */
layout (set = 0, binding = 3) uniform isampler2D flow_fwd;
layout (set = 0, binding = 4) uniform isampler2D flow_bwd;

layout (push_constant, scalar) uniform pushConstants {
    float t;          /* interpolation position in [0, 1] between f0 and f1 */
    int planes;
    /* Visible extent of each plane. Neither the source textures nor the output
     * storage images need match it: a hardware decoder allocates its frames
     * padded up to its alignment, and the output frames context may be the
     * reused input context or a fresh visible sized one. Everything below works
     * in this visible space and converts on each access. */
    vec2 plane_size[4];
};

#define luma_size (plane_size[0]) /* what the flow vectors are expressed against */

/* Convert a coordinate normalized against a plane's visible extent into the
 * source texture's own normalized space. The identity unless that source image
 * is padded. */
vec2 f0_uv(vec2 p, int i) { return p * (plane_size[i] / vec2(textureSize(f0_img[i], 0))); }
vec2 f1_uv(vec2 p, int i) { return p * (plane_size[i] / vec2(textureSize(f1_img[i], 0))); }

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    for (int i = 0; i < planes; i++) {
        /* Bound the pass by the visible extent, not by either image's allocation:
         * a padded source would let invocations run past the end of a visible
         * sized output, and a padded output would have us synthesise pixels the
         * flow field does not cover. */
        vec2 size = plane_size[i];
        if (any(greaterThanEqual(vec2(pos), size)))
            continue;

        vec2 base = (vec2(pos) + 0.5) / size;

        vec4 c0 = texture(f0_img[i], f0_uv(base, i));
        vec4 c1 = texture(f1_img[i], f1_uv(base, i));

        imageStore(out_img[i], pos, mix(c0, c1, t));
    }
}
