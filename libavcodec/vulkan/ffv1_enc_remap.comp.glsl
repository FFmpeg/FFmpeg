/*
 * FFv1 codec
 *
 * Copyright (c) 2024 Lynne <dev@lynne.ee>
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

layout (set = 1, binding = 1) uniform image2D src[];

layout (set = 1, binding = 2) buffer fltmap_buf {
    uint fltmap[][4][65536];
};

void load_fltmap(uint slice_idx, uint p)
{
    uvec2 img_size = imageSize(src[0]);
    uint sxs = slice_coord(img_size.x, gl_WorkGroupID.x + 0,
                           gl_NumWorkGroups.x, 0);
    uint sxe = slice_coord(img_size.x, gl_WorkGroupID.x + 1,
                           gl_NumWorkGroups.x, 0);
    uint sys = slice_coord(img_size.y, gl_WorkGroupID.y + 0,
                           gl_NumWorkGroups.y, 0);
    uint sye = slice_coord(img_size.y, gl_WorkGroupID.y + 1,
                           gl_NumWorkGroups.y, 0);

    for (uint i = gl_LocalInvocationIndex; i < 32768;
         i += (gl_WorkGroupSize.x * gl_WorkGroupSize.y))
        fltmap[slice_idx][p][i] = 0;

    barrier();

    for (uint y = sys + gl_LocalInvocationID.y; y < sye; y += gl_WorkGroupSize.y) {
        for (uint x = sxs + gl_LocalInvocationID.x; x < sxe; x += gl_WorkGroupSize.x) {
            vec4 pix = imageLoad(src[p], ivec2(x, y));
            uint16_t pix_idx = float16BitsToUint16(float16_t(pix[0]));
            atomicOr(fltmap[slice_idx][p][pix_idx], 1);
        }
    }
}

void main(void)
{
    const uint slice_idx = gl_WorkGroupID.y*gl_NumWorkGroups.x + gl_WorkGroupID.x;

    for (int i = 0; i < color_planes; i++)
        load_fltmap(slice_idx, i);
}
