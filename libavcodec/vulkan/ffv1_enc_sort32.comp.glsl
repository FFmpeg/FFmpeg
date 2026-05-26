/*
 * FFv1 codec
 *
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
#extension GL_GOOGLE_include_directive : require

#define SB_QUALI readonly
#include "common.glsl"
#include "ffv1_common.glsl"

layout (set = 1, binding = 1) uniform uimage2D src[];

layout (set = 1, binding = 2, scalar) buffer fltmap_buf {
    uint fltmap[];
};

/* The shared fltmap_buf is laid out per (slice, plane) as a
 * max_pixels_per_slice*3 uint block, where the first
 * max_pixels_per_slice*2 entries hold interleaved (val, ndx) pairs and
 * the trailing [max_pixels_per_slice] entries are the bitmap region used
 * by the setup/encode shaders. Padding past pixel_num is the sentinel
 * (UINT32_MAX, UINT32_MAX) so it sorts at the end. */

/* Per-workgroup bitonic-sort buffer. Limits a slice's pow2 size; large
 * slices fall back to working in global memory */
shared u32vec2 smem[8192];

void main(void)
{
    const uint slice_idx = gl_WorkGroupID.y*gl_NumWorkGroups.x + gl_WorkGroupID.x;
    uvec2 img_size = imageSize(src[0]);

    uint sxs = slice_coord(img_size.x, gl_WorkGroupID.x + 0,
                           gl_NumWorkGroups.x, 0);
    uint sxe = slice_coord(img_size.x, gl_WorkGroupID.x + 1,
                           gl_NumWorkGroups.x, 0);
    uint sys = slice_coord(img_size.y, gl_WorkGroupID.y + 0,
                           gl_NumWorkGroups.y, 0);
    uint sye = slice_coord(img_size.y, gl_WorkGroupID.y + 1,
                           gl_NumWorkGroups.y, 0);

    uint slice_w = sxe - sxs;
    uint slice_h = sye - sys;
    uint pixel_num = slice_w * slice_h;

    /* Round up to next pow2 for bitonic sort */
    uint N = 1;
    while (N < pixel_num)
        N <<= 1;
    N = max(N, 2);
    if (N > max_pixels_per_slice)
        N = max_pixels_per_slice;

    const uint plane_stride = max_pixels_per_slice*3u;
    const bool use_smem = N <= 8192u;

    for (int p = 0; p < color_planes; p++) {
        uint base = (slice_idx*4u + uint(p))*plane_stride;

        /* Load pixels */
        for (uint i = gl_LocalInvocationIndex; i < N;
             i += gl_WorkGroupSize.x * gl_WorkGroupSize.y) {
            uint v, ndx;
            if (i < pixel_num) {
                uint y = i / slice_w;
                uint x = i - y*slice_w;
                v = imageLoad(src[p], ivec2(sxs + x, sys + y))[0];
                if (remap_mode == 2)
                    v = ((v & 0x80000000u) != 0u) ? v : (v ^ 0x7FFFFFFFu);
                ndx = i;
            } else {
                v = 0xFFFFFFFFu;
                ndx = 0xFFFFFFFFu;
            }
            if (use_smem) {
                smem[i] = u32vec2(v, ndx);
            } else {
                fltmap[base + 2u*i + 0u] = v;
                fltmap[base + 2u*i + 1u] = ndx;
            }
        }
        barrier();
        if (!use_smem) memoryBarrierBuffer();

        /* Bitonic sort of the (val, ndx) pairs. */
        for (uint k = 2; k <= N; k <<= 1) {
            for (uint j = k >> 1; j > 0; j >>= 1) {
                for (uint i = gl_LocalInvocationIndex; i < N;
                     i += gl_WorkGroupSize.x * gl_WorkGroupSize.y) {
                    uint partner = i ^ j;
                    if (partner > i) {
                        bool ascending = (i & k) == 0;
                        u32vec2 a, b;
                        if (use_smem) {
                            a = smem[i];
                            b = smem[partner];
                        } else {
                            a = u32vec2(fltmap[base + 2u*i + 0u],
                                        fltmap[base + 2u*i + 1u]);
                            b = u32vec2(fltmap[base + 2u*partner + 0u],
                                        fltmap[base + 2u*partner + 1u]);
                        }
                        bool a_gt_b = (a.x > b.x) ||
                                      (a.x == b.x && a.y > b.y);
                        if (a_gt_b == ascending) {
                            if (use_smem) {
                                smem[i] = b;
                                smem[partner] = a;
                            } else {
                                fltmap[base + 2u*i + 0u] = b.x;
                                fltmap[base + 2u*i + 1u] = b.y;
                                fltmap[base + 2u*partner + 0u] = a.x;
                                fltmap[base + 2u*partner + 1u] = a.y;
                            }
                        }
                    }
                }
                barrier();
                if (!use_smem) memoryBarrierBuffer();
            }
        }

        /* Write sorted pairs back to global */
        if (use_smem) {
            for (uint i = gl_LocalInvocationIndex; i < N;
                 i += gl_WorkGroupSize.x * gl_WorkGroupSize.y) {
                u32vec2 u = smem[i];
                fltmap[base + 2u*i + 0u] = u.x;
                fltmap[base + 2u*i + 1u] = u.y;
            }
            barrier();
        }
    }
}
