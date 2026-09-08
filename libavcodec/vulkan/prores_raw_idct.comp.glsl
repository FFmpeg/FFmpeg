/*
 * ProRes RAW decoder
 *
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

#pragma shader_stage(compute)
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"
#include "dct.glsl"

struct TileData {
   ivec2 pos;
   uint offset;
   uint size;
   uint log2_nb_blocks;
};

layout (set = 0, binding = 0, r16ui) uniform uimage2D dst;
layout (set = 0, binding = 1, scalar) readonly buffer frame_data_buf {
    TileData tile_data[];
};

layout (push_constant, scalar) uniform pushConstants {
   u8buf pkt_data;
   uint8_t qmat[64];
   uint16_t lin_curve[8];
};

#define COMP_ID (gl_LocalInvocationID.z)
#define BLOCK_ID (gl_LocalInvocationID.y)
#define ROW_ID (gl_LocalInvocationID.x)

const u8vec2 scan[64] = {
    u8vec2( 0,  0), u8vec2( 4,  0), u8vec2( 0,  2), u8vec2( 4,  2),
    u8vec2( 0,  8), u8vec2( 4,  8), u8vec2( 6,  8), u8vec2( 2, 10),
    u8vec2( 2,  0), u8vec2( 6,  0), u8vec2( 2,  2), u8vec2( 6,  2),
    u8vec2( 2,  8), u8vec2( 8,  8), u8vec2( 0, 10), u8vec2( 4, 10),
    u8vec2( 8,  0), u8vec2(12,  0), u8vec2( 8,  2), u8vec2(12,  2),
    u8vec2(10,  8), u8vec2(14,  8), u8vec2( 6, 10), u8vec2( 2, 12),
    u8vec2(10,  0), u8vec2(14,  0), u8vec2(10,  2), u8vec2(14,  2),
    u8vec2(12,  8), u8vec2( 8, 10), u8vec2( 0, 12), u8vec2( 4, 12),
    u8vec2( 0,  4), u8vec2( 4,  4), u8vec2( 6,  4), u8vec2( 2,  6),
    u8vec2(10, 10), u8vec2(14, 10), u8vec2( 6, 12), u8vec2( 2, 14),
    u8vec2( 2,  4), u8vec2( 8,  4), u8vec2( 0,  6), u8vec2( 4,  6),
    u8vec2(12, 10), u8vec2( 8, 12), u8vec2( 0, 14), u8vec2( 4, 14),
    u8vec2(10,  4), u8vec2(14,  4), u8vec2( 6,  6), u8vec2(12,  6),
    u8vec2(10, 12), u8vec2(14, 12), u8vec2( 6, 14), u8vec2(12, 14),
    u8vec2(12,  4), u8vec2( 8,  6), u8vec2(10,  6), u8vec2(14,  6),
    u8vec2(12, 12), u8vec2( 8, 14), u8vec2(10, 14), u8vec2(14, 14),
};

shared uint8_t qmat_buf[64];
shared uint lin_curve_buf[8];

void main(void)
{
    const uint tile_idx = gl_WorkGroupID.y*gl_NumWorkGroups.x + gl_WorkGroupID.x;
    TileData td = tile_data[tile_idx];

    uint64_t pkt_offset = uint64_t(pkt_data) + td.offset;
    u8vec2buf hdr_data = u8vec2buf(pkt_offset);
    int qscale = int(hdr_data[0].v.y);

    const ivec2 offs = td.pos + ivec2(COMP_ID & 1, COMP_ID >> 1);
    const uint nb_blocks = 1 << td.log2_nb_blocks;

    if (gl_LocalInvocationIndex == 0) {
        [[unroll]] for (uint i = 0; i < 64; i++) qmat_buf[i]      = qmat[i];
        [[unroll]] for (uint i = 0; i < 8;  i++) lin_curve_buf[i] = uint(lin_curve[i]);
    }
    barrier();

    const float norm = 8.0; /* DCT normalization const */

    /* Loop-invariant column scale */
    const float col_scale = float(qscale) * norm * idct_scale[ROW_ID];

    [[unroll]]
    for (uint y = 0; y < 8; y++) {
        uint block_off = y*8 + ROW_ID;
        int v = int(imageLoad(dst, offs + 2*ivec2(BLOCK_ID*8, 0) + scan[block_off])[0]);
        /* Dequantize (coeff * qmat * qscale), matching the reference decoder */
        float vf = float(sign_extend(v, 16)) * float(qmat_buf[block_off]) * col_scale;
        blocks[BLOCK_ID][COMP_ID*72 + y*9 + ROW_ID] = vf * idct_scale[y];
    }

    /* Column-wise iDCT */
    idct8(BLOCK_ID, COMP_ID*72 + ROW_ID, 9);
    barrier();

    /* Row-wise iDCT */
    idct8(BLOCK_ID, COMP_ID*72 + ROW_ID * 9, 1);
    barrier();

    /* Border tile check */
    if (BLOCK_ID >= nb_blocks)
        return;

    [[unroll]]
    for (uint y = 0; y < 8; y++) {
        /* Bias the signed iDCT output into the reference's unsigned 16-bit space */
        int u = clamp(int(round(blocks[BLOCK_ID][COMP_ID*72 + y*9 + ROW_ID])) + 32768,
                      0, 65535);

        /* 8-point combined linearization curve (inv. transfer fn +
         * encoder-defined shaping). cp1 - cp0 is the segment slope; for the
         * final segment cp[8] == 0. */
        uint seg  = uint(u) >> 13;
        uint frac = uint(u) & 0x1FFFu;
        uint cp0  = lin_curve_buf[seg];
        uint cp1  = seg < 7u ? lin_curve_buf[seg + 1u] : 0u;
        uint outv = (cp0 * 8192u + ((cp1 - cp0) & 0xFFFFu) * frac + 4096u) >> 13u;
        outv = min(outv, 0xFFFFu);

        imageStore(dst,
                   offs + 2*ivec2(BLOCK_ID*8 + ROW_ID, y),
                   ivec4(outv));
    }
}
