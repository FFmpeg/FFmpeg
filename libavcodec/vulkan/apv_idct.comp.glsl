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
#include "dct.glsl"

#define APV_MAX_NUM_COMP    4
#define APV_MAX_TILE_COLS   20
#define APV_MAX_TILE_ROWS   20
#define APV_MAX_TILE_COUNT  (APV_MAX_TILE_COLS * APV_MAX_TILE_ROWS)
#define APV_TR_SIZE         8
#define APV_BLOCKS_PER_WG   8

layout (set = 0, binding = 0) uniform uimage2D dst[];
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

    /* figure out the tile position */
    int tx = 0;
    while (tx + 1 < tile_count.x && int(tile_col[tx + 1]) <= luma_pos.x)
        tx++;
    int ty = 0;
    while (ty + 1 < tile_count.y && int(tile_row[ty + 1]) <= luma_pos.y)
        ty++;

    const int tile_idx = ty * tile_count.x + tx;
    const int qp = int(tile_qp[int(comp) * APV_MAX_TILE_COUNT + tile_idx]);
    const int level_scale = apv_level_scale[qp % 6];
    const int qp_shift = qp / 6;

    const int half_range = 1 << (bit_depth - 1);
    const int max_val = (1 << bit_depth) - 1;
    const float fact = float(half_range);
    const float norm = 1.0f / (1024.0f * fact); /* DCT normalization const */

    [[unroll]]
    for (uint y = 0u; y < 8u; y++) {
        /* load */
        int   raw   = int(imageLoad(dst[comp], pos + ivec2(col, y)).x);
        int   coeff = sign_extend(raw, 16);
        /* dequant + norm */
        int   qs    = level_scale * int(q_matrix[comp][col][y]) * (1 << qp_shift);
        float v     = float(coeff * qs) * norm;
        /* scale */
        blocks[block][y * 9u + col] = v * idct_scale[y * 8u + col];
    }
    barrier();

    idct8(block, col, 9);
    barrier();

    blocks[block][col * 9u] += 1.0f;

    idct8(block, col * 9u, 1);
    barrier();

    [[unroll]]
    for (int y = 0; y < 8; y++) {
        float v = round(blocks[block][y * 9u + col] * fact);
        imageStore(dst[comp], pos + ivec2(col, y),
                   uvec4(uint(clamp(int(v), 0, max_val))));
    }
}
