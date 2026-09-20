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
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_shuffle : require

#define SB_QUALI readonly
#include "common.glsl"
#include "ffv1_common.glsl"

layout (set = 1, binding = 1) uniform uimage2D src[];

layout (set = 1, binding = 2, scalar) workgroupcoherent buffer fltmap_buf {
    u32vec2 fltmap[];
};

/* Per slice, fltmap_buf holds the sorted (val, ndx) pairs of each plane,
 * max_pixels_per_slice each, followed by the bitmaps of each plane, which
 * the setup shader fills after the sort, so they serve as its scratch. */

#define RADIX_BITS 4
#define RADIX_SIZE 16

shared uint cnt[RADIX_SIZE][gl_WorkGroupSize.x / 4];
shared uint hist[RADIX_SIZE];

uint slice_w, sxs, sys;

u32vec2 load_pixel(uint i, int p)
{
    uint y = i / slice_w;
    uint x = i - y*slice_w;
    uint v = imageLoad(src[p], ivec2(sxs + x, sys + y))[0];
    if (remap_mode == 2)
        v = ((v & 0x80000000u) != 0u) ? v : (v ^ 0x7FFFFFFFu);
    return u32vec2(v, i);
}

/* Lanes of the subgroup whose element has the digit d */
uvec4 digit_lanes(bool live, uint d)
{
    uvec4 lanes = subgroupBallot(live);
    [[unroll]] for (int k = 0; k < RADIX_BITS; k++) {
        uvec4 b = subgroupBallot(live && ((d >> k) & 1u) != 0u);
        lanes &= ((d >> k) & 1u) != 0u ? b : ~b;
    }
    return lanes;
}

/* LSD radix sort, 4 bits per pass, between the two buffers. Each subgroup
 * owns a block of rows of elements, one per lane, ranked with ballots. */
void radix_sort(int p, uint n, uint src, uint dst)
{
    const uint sg = gl_SubgroupID;
    const uint lane = gl_SubgroupInvocationID;
    const uint S = min(gl_SubgroupSize, gl_WorkGroupSize.x);
    const uint rows = (n + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;
    const uint block = sg*rows*S;

    for (uint shift = 0; shift < 32; shift += RADIX_BITS) {
        for (uint v = lane; v < RADIX_SIZE; v += S)
            cnt[v][sg] = 0;
        if (gl_LocalInvocationIndex < RADIX_SIZE)
            hist[gl_LocalInvocationIndex] = 0;
        barrier();

        /* Count of each digit per block and in total */
        for (uint m = 0; m < rows; m++) {
            const uint i = block + m*S + lane;
            const bool live = i < n;
            const u32vec2 e = !live ? u32vec2(0) : shift == 0 ?
                              load_pixel(i, p) : fltmap[src + i];
            const uint d = (e.x >> shift) & (RADIX_SIZE - 1);
            const uvec4 lanes = digit_lanes(live, d);
            if (live && subgroupBallotExclusiveBitCount(lanes) == 0) {
                atomicAdd(cnt[d][sg], subgroupBallotBitCount(lanes));
                atomicAdd(hist[d], subgroupBallotBitCount(lanes));
            }
        }
        barrier();

        /* Offset of each digit per block: the digit's base plus its count
         * in the blocks before */
        for (uint v = sg; v < RADIX_SIZE; v += gl_NumSubgroups) {
            uint carry = 0;
            for (uint w = 0; w < v; w++)
                carry += hist[w];
            for (uint c = 0; c < gl_NumSubgroups; c += S) {
                const bool has = c + lane < gl_NumSubgroups;
                const uint x = has ? cnt[v][c + lane] : 0;
                const uint ex = subgroupExclusiveAdd(x);
                if (has)
                    cnt[v][c + lane] = carry + ex;
                carry += subgroupAdd(x);
            }
        }
        barrier();

        /* Scatter each row from the offsets, advancing them */
        for (uint m = 0; m < rows; m++) {
            const uint i = block + m*S + lane;
            const bool live = i < n;
            const u32vec2 e = !live ? u32vec2(0) : shift == 0 ?
                              load_pixel(i, p) : fltmap[src + i];
            const uint d = (e.x >> shift) & (RADIX_SIZE - 1);
            const uvec4 lanes = digit_lanes(live, d);
            const uint before = subgroupBallotExclusiveBitCount(lanes);
            uint pos = 0;
            if (live && before == 0)
                pos = atomicAdd(cnt[d][sg], subgroupBallotBitCount(lanes));
            pos = subgroupShuffle(pos, subgroupBallotFindLSB(lanes)) + before;
            if (live)
                fltmap[dst + pos] = e;
        }
        controlBarrier(gl_ScopeWorkgroup, gl_ScopeWorkgroup,
                       gl_StorageSemanticsShared | gl_StorageSemanticsBuffer,
                       gl_SemanticsAcquireRelease);

        const uint t = src;
        src = dst;
        dst = t;
    }
}

void main(void)
{
    const uint slice_idx = gl_WorkGroupID.y*gl_NumWorkGroups.x + gl_WorkGroupID.x;
    uvec2 img_size = imageSize(src[0]);

    sxs = slice_coord(img_size.x, gl_WorkGroupID.x + 0,
                      gl_NumWorkGroups.x, 0);
    uint sxe = slice_coord(img_size.x, gl_WorkGroupID.x + 1,
                           gl_NumWorkGroups.x, 0);
    sys = slice_coord(img_size.y, gl_WorkGroupID.y + 0,
                      gl_NumWorkGroups.y, 0);
    uint sye = slice_coord(img_size.y, gl_WorkGroupID.y + 1,
                           gl_NumWorkGroups.y, 0);

    slice_w = sxe - sxs;
    uint slice_h = sye - sys;
    uint pixel_num = slice_w * slice_h;

    const uint slice_base = slice_idx*6u*max_pixels_per_slice;
    const uint scratch = slice_base + 4u*max_pixels_per_slice;

    for (int p = 0; p < color_planes; p++)
        radix_sort(p, pixel_num, slice_base + uint(p)*max_pixels_per_slice,
                   scratch);
}
