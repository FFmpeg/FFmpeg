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

#extension GL_EXT_shader_image_load_formatted : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

layout (local_size_x_id = 253, local_size_y_id = 254, local_size_z_id = 255) in;

layout (constant_id = 0) const int alpha_bits = 0;
layout (constant_id = 1) const int slices_per_row = 0;
layout (constant_id = 2) const int width_in_mb = 0;
layout (constant_id = 3) const int max_mbs_per_slice = 0;

struct SliceData {
    uint mbs_per_slice;
    int16_t coeffs[4][8 * 256];
};

layout (set = 0, binding = 0, scalar) writeonly buffer SliceBuffer {
    SliceData slices[];
};
layout (set = 0, binding = 1) uniform readonly iimage2D plane;

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

void main()
{
    ivec2 coord = min(ivec2(gl_GlobalInvocationID.xy), imageSize(plane) - ivec2(1));
    uint16_t alpha = uint16_t(imageLoad(plane, coord).x);

    if (alpha_bits == 8)
        alpha >>= 2;
    else
        alpha = (alpha << 6) | (alpha >> 4);

    uint mbs_per_slice = max_mbs_per_slice;
    uint slices_width = width_in_mb / mbs_per_slice;
    uint mb_width = slices_width * mbs_per_slice;
    uint slice_x = gl_WorkGroupID.x / mbs_per_slice;
    uint slice_y = gl_WorkGroupID.y;
    uvec2 slice_base = uvec2(slice_x * mbs_per_slice * 16u, slice_y * 16u);

    /* Handle slice macroblock size reduction on edge slices */
    if (gl_WorkGroupID.x >= mb_width) {
        uint edge_mb = gl_WorkGroupID.x - mb_width;
        uvec3 table = edge_mps_table[width_in_mb - mb_width];
        uvec3 base = uvec3(0, table.x, table.x + table.y);
        uint edge_slice = edge_mb < base.y ? 0 : (edge_mb < base.z ? 1 : 2);
        slice_x += edge_slice;
        slice_base.x += base[edge_slice] * 16u;
        mbs_per_slice = table[edge_slice];
    }

    uint slice = slice_y * slices_per_row + slice_x;
    uvec2 coeff_coord = uvec2(coord) - slice_base;
    uint coeff = coeff_coord.y * (mbs_per_slice * 16u) + coeff_coord.x;
    slices[slice].coeffs[3][coeff] = int16_t(alpha);
}
