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

/* Forward (f0 -> f1) and backward (f1 -> f0) flow fields. The NV optical flow
 * engine writes signed fixed point (S10.5) vectors, sampled here as raw
 * integers; dividing by 32 (2^5) yields correct values. */
layout (set = 0, binding = 3) uniform isampler2D flow_fwd;
layout (set = 0, binding = 4) uniform isampler2D flow_bwd;

#define FLOW_FIXED_POINT_SCALE (1.0 / 32.0)

// Thresholds for the spurious-flow guard

/* STATIC_LUMA_SIGMA is the per-pixel luma tolerance for calling a region "static" */
#define STATIC_LUMA_SIGMA 0.03
/* WARP_NEAR_PX is the threshold below which a warp is small enough that it won't
   ever be considered "long range" */
#define WARP_NEAR_PX 5.0
/* WARP_FAR_PX is the threshold above which a warp is large enough that it will
   always be considered "long range" */
#define WARP_FAR_PX  20.0
/* WARP_REACH_SCALE is the factor by which a warp's reach is scaled before the
 * range test. Higher values will trigger the guard for smaller displacements. */
#define WARP_REACH_SCALE 1.0

/* These Picard values were established empirically on a couple of different
 * samples, but one could easily imagine reaching a different conclusion from
 * different data. */
#define PICARD_ITERS 6
#define PICARD_OMEGA 0.5

layout (push_constant, scalar) uniform pushConstants {
    float t;          /* interpolation position in [0, 1] between f0 and f1 */
    int planes;
    vec4 luma_weights[4]; /* per-plane RGB->Y weights, matching the grayscale pass */
    /* Visible extent of each plane. Neither the source textures nor the output
     * storage images need match it: a hardware decoder allocates its frames
     * padded up to its alignment, and the output frames context may be the
     * reused input context or a fresh visible sized one. Everything below works
     * in this visible space and converts on each access. */
    vec2 plane_size[4];
};

#define luma_size (plane_size[0]) /* what the flow vectors are expressed against */

/* Normalized forward/backward flow at a normalized position. The flow vectors
 * are relative to the original Luma, so must be converted to a resolution independent
 * displacement that can be applied to other planes with different dimensions. */
vec2 flow_at_fwd(vec2 p) { return vec2(texture(flow_fwd, p).xy) * FLOW_FIXED_POINT_SCALE / luma_size; }
vec2 flow_at_bwd(vec2 p) { return vec2(texture(flow_bwd, p).xy) * FLOW_FIXED_POINT_SCALE / luma_size; }

/* Convert a coordinate normalized against a plane's visible extent into the
 * source texture's own normalized space. The identity unless that source image
 * is padded. */
vec2 f0_uv(vec2 p, int i) { return p * (plane_size[i] / vec2(textureSize(f0_img[i], 0))); }
vec2 f1_uv(vec2 p, int i) { return p * (plane_size[i] / vec2(textureSize(f1_img[i], 0))); }

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    /* The warp positions and the guard weight only depend on plane size. So
     * we cache these values and re-use them if the next plane is the same
     * size. In practice, we won't see plane sizes change and then change back
     * to some previous dimension, so a single cached value is optimal. */
    vec2  cached_size = vec2(0.0);
    vec2  s0 = vec2(0.0);
    vec2  s1 = vec2(0.0);
    float guard = 0.0;

    for (int i = 0; i < planes; i++) {
        /* Bound the pass by the visible extent, not by either image's allocation:
         * a padded source would let invocations run past the end of a visible
         * sized output, and a padded output would have us synthesise pixels the
         * flow field does not cover. */
        vec2 size = plane_size[i];
        if (any(greaterThanEqual(vec2(pos), size)))
            continue;

        vec2 base = (vec2(pos) + 0.5) / size;

        if (size != cached_size) {
            cached_size = size;

            /* The flow is anchored on the f0/f1 grids, not the intermediate frame.
             * Recover the source position on each grid by Picard iteration. */
            s0 = base;
            s1 = base;
            for (int k = 0; k < PICARD_ITERS; k++) {
                s0 = mix(s0, base - t * flow_at_fwd(s0), PICARD_OMEGA);
                s1 = mix(s1, base - (1.0 - t) * flow_at_bwd(s1), PICARD_OMEGA);
            }

            /* Key staticness off the same weighted luma the grayscale pass feeds the
             * flow engine. */
            float l0 = 0.0, l1 = 0.0;
            for (int q = 0; q < planes; q++) {
                l0 += dot(texture(f0_img[q], f0_uv(base, q)), luma_weights[q]);
                l1 += dot(texture(f1_img[q], f1_uv(base, q)), luma_weights[q]);
            }
            float lz = abs(l0 - l1);
            float staticness = exp(-(lz * lz) / (STATIC_LUMA_SIGMA * STATIC_LUMA_SIGMA));

            float disp = max(length((s0 - base) * luma_size),
                             length((s1 - base) * luma_size));
            /* smoothstep is used to transition between the two thresholds and avoid
             * abrupt changes at the boundary. */
            float reach = smoothstep(WARP_NEAR_PX, WARP_FAR_PX, disp * WARP_REACH_SCALE);

            guard = staticness * reach;
        }

        vec4 c0 = texture(f0_img[i], f0_uv(s0, i));
        vec4 c1 = texture(f1_img[i], f1_uv(s1, i));
        vec4 warped = mix(c0, c1, t);

        /* Spurious-flow guard. In textureless regions the flow
         * engine invents bogus flows we need to ignore. */
        vec4 z0 = texture(f0_img[i], f0_uv(base, i));
        vec4 z1 = texture(f1_img[i], f1_uv(base, i));
        vec4 stat = mix(z0, z1, t);

        vec4 result = mix(warped, stat, guard);

        imageStore(out_img[i], pos, result);
    }
}
