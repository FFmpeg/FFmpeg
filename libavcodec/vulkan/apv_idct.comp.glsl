/*
 * Copyright (c) 2025 Lynne <dev@lynne.ee>
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

#version 460
#pragma shader_stage(compute)
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

#define APV_MAX_NUM_COMP    4
#define APV_MAX_TILE_COLS   20
#define APV_MAX_TILE_ROWS   20
#define APV_MAX_TILE_COUNT  (APV_MAX_TILE_COLS * APV_MAX_TILE_ROWS)
#define APV_TR_SIZE         8
#define APV_BLOCKS_PER_WG   8

layout (set = 0, binding = 0) uniform uimage2D dst[];
layout (set = 0, binding = 2, scalar) readonly buffer coeffs_in_buf {
    int16_t coeffs_in[];
};
layout (set = 0, binding = 1, scalar) readonly buffer frame_data_buf {
    uvec2 tile_offset[APV_MAX_NUM_COMP * APV_MAX_TILE_COUNT];
    uint8_t q_matrix[APV_MAX_NUM_COMP][8][8];
    uint8_t tile_qp[APV_MAX_NUM_COMP * APV_MAX_TILE_COUNT];
    uint16_t tile_col[APV_MAX_TILE_COLS + 1];
    uint16_t tile_row[APV_MAX_TILE_ROWS + 1];
};

layout (push_constant, scalar) uniform pushConstants {
    u8buf tile_data;
    ivec2 tile_count;
    ivec2 log2_chroma_sub;
    int components;
    int bit_depth;
};

const int apv_level_scale[6] = { 40, 45, 51, 57, 64, 71 };

/* Rows are padded to avoid bank conflicts */
shared int blocks[APV_BLOCKS_PER_WG][8*(8 + 1)];

/* Inverse of the APV integer transform, split into its even and odd halves
 * like the HEVC one, then rounded and shifted down. */
void apv_idct8(uint block, uint offset, uint stride, int shift)
{
    int s0 = blocks[block][0*stride + offset];
    int s1 = blocks[block][1*stride + offset];
    int s2 = blocks[block][2*stride + offset];
    int s3 = blocks[block][3*stride + offset];
    int s4 = blocks[block][4*stride + offset];
    int s5 = blocks[block][5*stride + offset];
    int s6 = blocks[block][6*stride + offset];
    int s7 = blocks[block][7*stride + offset];

    /* Even half, with the rounding offset */
    int ee0 = 64*(s0 + s4) + (1 << (shift - 1));
    int ee1 = 64*(s0 - s4) + (1 << (shift - 1));
    int eo0 = 84*s2 + 35*s6;
    int eo1 = 35*s2 - 84*s6;
    int e0 = ee0 + eo0, e1 = ee1 + eo1, e2 = ee1 - eo1, e3 = ee0 - eo0;

    /* Odd half */
    int o0 = 89*s1 + 75*s3 + 50*s5 + 18*s7;
    int o1 = 75*s1 - 18*s3 - 89*s5 - 50*s7;
    int o2 = 50*s1 - 89*s3 + 18*s5 + 75*s7;
    int o3 = 18*s1 - 50*s3 + 75*s5 - 89*s7;

    blocks[block][0*stride + offset] = (e0 + o0) >> shift;
    blocks[block][1*stride + offset] = (e1 + o1) >> shift;
    blocks[block][2*stride + offset] = (e2 + o2) >> shift;
    blocks[block][3*stride + offset] = (e3 + o3) >> shift;
    blocks[block][4*stride + offset] = (e3 - o3) >> shift;
    blocks[block][5*stride + offset] = (e2 - o2) >> shift;
    blocks[block][6*stride + offset] = (e1 - o1) >> shift;
    blocks[block][7*stride + offset] = (e0 - o0) >> shift;
}

void main(void)
{
    const uvec3 wgid = gl_WorkGroupID;
    const uint comp = wgid.z;

    const uvec3 lid = gl_LocalInvocationID;
    const uint  block = (lid.y << 2) | (lid.x >> 3); /* 0..7 block in chunk */
    const uint  col = lid.x & 0x7u;                  /* 0..7 column in block */

    /* one workgroup handles eight horizontally neighbouring blocks */
    const int blk_x = int(wgid.x) * APV_BLOCKS_PER_WG + int(block);
    const int blk_y = int(wgid.y);
    const ivec2 pos = ivec2(blk_x, blk_y) * APV_TR_SIZE;

    /* note: some oddness happens on tile-boundaries */
    const ivec2 sub_shift = (comp == 0u) ? ivec2(0) : log2_chroma_sub;
    const ivec2 luma_pos  = pos << sub_shift;

    /* Uniform tile grid with a remainder tail, so the tile position is a
     * division, not a search. Single-column/row grids have no step; index 0. */
    int tx = 0, ty = 0;
    if (tile_count.x > 1)
        tx = min(luma_pos.x / int(tile_col[1] - tile_col[0]),
                 tile_count.x - 1);
    if (tile_count.y > 1)
        ty = min(luma_pos.y / int(tile_row[1] - tile_row[0]),
                 tile_count.y - 1);

    const int tile_idx = ty * tile_count.x + tx;
    const int qp = int(tile_qp[int(comp) * APV_MAX_TILE_COUNT + tile_idx]);
    const int level_scale = apv_level_scale[qp % 6];
    const int qp_shift = qp / 6;

    const int half_range = 1 << (bit_depth - 1);
    const int max_val = (1 << bit_depth) - 1;
    const int dequant_shift = bit_depth - 2;
    /* Scaled coefficients beyond this saturate either way, clamping to it
     * first keeps the dequantization within 32 bits */
    const int dequant_max = 1 << (16 + dequant_shift - qp_shift);

    /* This component's plane inside the flat coefficient buffer */
    const int cw0 = int(tile_col[tile_count.x]);
    const int ch0 = int(tile_row[tile_count.y]);
    uint cbase = 0u;
    for (uint i = 0u; i < comp; i++) {
        ivec2 ss = i == 0u ? ivec2(0) : log2_chroma_sub;
        cbase += uint((cw0 >> ss.x) * (ch0 >> ss.y));
    }
    const int cstride = cw0 >> sub_shift.x;
    const int cheight = ch0 >> sub_shift.y;

    /* blocks fully outside the coded area have nothing stored for them */
    const bool oob = pos.x >= cstride || pos.y >= cheight;

    [[unroll]]
    for (uint y = 0u; y < 8u; y++) {
        /* load */
        int   coeff = oob ? 0
                    : int(coeffs_in[cbase + uint((pos.y + int(y)) * cstride +
                                                 pos.x + int(col))]);
        /* dequant */
        int   qs    = level_scale * int(q_matrix[comp][col][y]);
        int   v     = clamp(coeff * qs, -dequant_max, dequant_max) << qp_shift;
        v = (v + (1 << (dequant_shift - 1))) >> dequant_shift;
        blocks[block][y * 9u + col] = clamp(v, -32768, 32767);
    }
    barrier();

    apv_idct8(block, col, 9, 7);
    barrier();

    apv_idct8(block, col * 9u, 1, 20 - bit_depth);
    barrier();

    [[unroll]]
    for (int y = 0; y < 8; y++) {
        int v = blocks[block][y * 9u + col] + half_range;
        imageStore(dst[comp], pos + ivec2(col, y),
                   uvec4(uint(clamp(v, 0, max_val))));
    }
}
