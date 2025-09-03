/*
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

#version 460
#pragma shader_stage(compute)

#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shared_memory_block : require
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"
#include "dct.glsl"

layout (constant_id = 0) const int max_mbs_per_slice = 8;
layout (constant_id = 1) const int blocks_per_mb = 0;
layout (constant_id = 2) const int width_in_mb = 0;
layout (constant_id = 3) const int pictures_per_frame = 0;

layout(push_constant, scalar) uniform SliceDataInfo {
   int plane;
   int line_add;
   int bits_per_sample;
};

struct SliceData {
    uint32_t mbs_per_slice;
    i16vec4 rows[4][8 * 32][2];
};

layout (set = 0, binding = 0, scalar) writeonly buffer SliceBuffer {
    SliceData slices[];
};
layout (set = 0, binding = 1) uniform readonly iimage2D planes[3];

/* Table of possible edge slice configurations */
const uvec3 edge_mps_table[8] = uvec3[](
    uvec3(0, 0, 0),
    uvec3(1, 0, 0),
    uvec3(2, 0, 0),
    uvec3(2, 1, 0),
    uvec3(4, 0, 0),
    uvec3(4, 1, 0),
    uvec3(4, 2, 0),
    uvec3(4, 2, 1)
);

const u8vec2 progressive_scan[64] = {
	u8vec2(0, 0), u8vec2(1, 0), u8vec2(0, 1), u8vec2(1, 1),
    u8vec2(2, 0), u8vec2(3, 0), u8vec2(2, 1), u8vec2(3, 1),
	u8vec2(0, 2), u8vec2(1, 2), u8vec2(0, 3), u8vec2(1, 3),
    u8vec2(2, 2), u8vec2(3, 2), u8vec2(2, 3), u8vec2(3, 3),
	u8vec2(4, 0), u8vec2(5, 0), u8vec2(4, 1), u8vec2(4, 2),
    u8vec2(5, 1), u8vec2(6, 0), u8vec2(7, 0), u8vec2(6, 1),
	u8vec2(5, 2), u8vec2(4, 3), u8vec2(5, 3), u8vec2(6, 2),
    u8vec2(7, 1), u8vec2(7, 2), u8vec2(6, 3), u8vec2(7, 3),
	u8vec2(0, 4), u8vec2(1, 4), u8vec2(0, 5), u8vec2(0, 6),
    u8vec2(1, 5), u8vec2(2, 4), u8vec2(3, 4), u8vec2(2, 5),
	u8vec2(1, 6), u8vec2(0, 7), u8vec2(1, 7), u8vec2(2, 6),
    u8vec2(3, 5), u8vec2(4, 4), u8vec2(5, 4), u8vec2(4, 5),
	u8vec2(3, 6), u8vec2(2, 7), u8vec2(3, 7), u8vec2(4, 6),
    u8vec2(5, 5), u8vec2(6, 4), u8vec2(7, 4), u8vec2(6, 5),
	u8vec2(5, 6), u8vec2(4, 7), u8vec2(5, 7), u8vec2(6, 6),
    u8vec2(7, 5), u8vec2(7, 6), u8vec2(6, 7), u8vec2(7, 7),
};

const u8vec2 interlaced_scan[64] = {
	u8vec2(0, 0), u8vec2(0, 1), u8vec2(1, 0), u8vec2(1, 1),
    u8vec2(0, 2), u8vec2(0, 3), u8vec2(1, 2), u8vec2(1, 3),
	u8vec2(2, 0), u8vec2(2, 1), u8vec2(3, 0), u8vec2(3, 1),
    u8vec2(2, 2), u8vec2(2, 3), u8vec2(3, 2), u8vec2(3, 3),
	u8vec2(0, 4), u8vec2(0, 5), u8vec2(1, 4), u8vec2(2, 4),
    u8vec2(1, 5), u8vec2(0, 6), u8vec2(0, 7), u8vec2(1, 6),
	u8vec2(2, 5), u8vec2(3, 4), u8vec2(3, 5), u8vec2(2, 6),
    u8vec2(1, 7), u8vec2(2, 7), u8vec2(3, 6), u8vec2(3, 7),
	u8vec2(4, 0), u8vec2(4, 1), u8vec2(5, 0), u8vec2(6, 0),
    u8vec2(5, 1), u8vec2(4, 2), u8vec2(4, 3), u8vec2(5, 2),
	u8vec2(6, 1), u8vec2(7, 0), u8vec2(7, 1), u8vec2(6, 2),
    u8vec2(5, 3), u8vec2(4, 4), u8vec2(4, 5), u8vec2(5, 4),
	u8vec2(6, 3), u8vec2(7, 2), u8vec2(7, 3), u8vec2(6, 4),
    u8vec2(5, 5), u8vec2(4, 6), u8vec2(4, 7), u8vec2(5, 6),
	u8vec2(6, 5), u8vec2(7, 4), u8vec2(7, 5), u8vec2(6, 6),
    u8vec2(5, 7), u8vec2(6, 7), u8vec2(7, 6), u8vec2(7, 7),
};

#define DCTSIZE 8

int16_t get_swizzled_coeff(uint blocks_per_slice, uint slice_row, uint idx)
{
    uint coeff = slice_row * DCTSIZE + idx;
    u8vec2 coord = pictures_per_frame == 1 ? progressive_scan[coeff / blocks_per_slice]
                                           : interlaced_scan[coeff / blocks_per_slice];
    uint block = coeff % blocks_per_slice;
    float v = blocks[block][coord.y * 9 + coord.x];
    return int16_t(v * float(1 << 11));
}

void main()
{
    uint row = gl_LocalInvocationID.x;
    uint block = gl_LocalInvocationID.y;
    uint macroblock = gl_LocalInvocationID.z;
    uint slice_x = gl_WorkGroupID.x;
    uint slice_block = macroblock * blocks_per_mb + block;
    uint slice = gl_WorkGroupID.y * gl_NumWorkGroups.x + slice_x;

    /* Calculate the current thread coordinate in input plane */
    uint mbs_per_slice = max_mbs_per_slice;
    uint mb_width = 4u * blocks_per_mb;
    uint slices_width = width_in_mb / max_mbs_per_slice;
    uvec2 slice_base = gl_WorkGroupID.xy * uvec2(max_mbs_per_slice * mb_width, DCTSIZE * 2u);

    /* Handle slice macroblock size reduction on edge slices */
    if (slice_x >= slices_width) {
        uint edge_slice = slice_x - slices_width;
        uvec3 table = edge_mps_table[width_in_mb - slices_width * max_mbs_per_slice];
        uvec3 base = uvec3(0u, table.x, table.x + table.y);
        slice_base.x = (max_mbs_per_slice * slices_width + base[edge_slice]) * mb_width;
        mbs_per_slice = table[edge_slice];
    }

    uvec2 mb_base = slice_base + uvec2(macroblock * mb_width, 0u);
    uvec2 block_coord = plane != 0 ? uvec2(block >> 1u, block & 1u) : uvec2(block & 1u, block >> 1u);
    ivec2 coord = ivec2(mb_base + block_coord * DCTSIZE + uvec2(0u, row));
    coord.y = coord.y * pictures_per_frame + line_add;
    coord = min(coord, imageSize(planes[plane]) - ivec2(1));

    /* Load and normalize coefficients to [-1, 1] for increased precision during the DCT. */
    [[unroll]] for (int i = 0; i < 8; i++) {
        int c = imageLoad(planes[plane], coord + ivec2(i, 0)).x;
        blocks[slice_block][row * 9 + i] = float(c) / (1 << (bits_per_sample - 1));
    }

    /* Row-wise DCT */
    fdct8(slice_block, row, 9);
    barrier();

    /* Column-wise DCT */
    fdct8(slice_block, row*9, 1);
    barrier();

    uint slice_row = slice_block * DCTSIZE + row;
    uint blocks_per_slice = mbs_per_slice * blocks_per_mb;

    /**
     * Swizzle coefficients in morton order before storing to output buffer.
     * This allows for more cache friendly and coalesced coefficient loads.
     */
    i16vec4 dst_low;
    dst_low.x = get_swizzled_coeff(blocks_per_slice, slice_row, 0);
    dst_low.y = get_swizzled_coeff(blocks_per_slice, slice_row, 1);
    dst_low.z = get_swizzled_coeff(blocks_per_slice, slice_row, 2);
    dst_low.w = get_swizzled_coeff(blocks_per_slice, slice_row, 3);

    i16vec4 dst_hi;
    dst_hi.x = get_swizzled_coeff(blocks_per_slice, slice_row, 4);
    dst_hi.y = get_swizzled_coeff(blocks_per_slice, slice_row, 5);
    dst_hi.z = get_swizzled_coeff(blocks_per_slice, slice_row, 6);
    dst_hi.w = get_swizzled_coeff(blocks_per_slice, slice_row, 7);

    /* Store DCT result to slice buffer */
    slices[slice].mbs_per_slice = mbs_per_slice;
    slices[slice].rows[plane][slice_row][0] = dst_low;
    slices[slice].rows[plane][slice_row][1] = dst_hi;
}
