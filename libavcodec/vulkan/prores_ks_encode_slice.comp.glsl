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
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_GOOGLE_include_directive : require

#define PB_UNALIGNED
#include "common.glsl"

layout (constant_id = 0) const int max_mbs_per_slice = 8;
layout (constant_id = 1) const int chroma_factor = 0;
layout (constant_id = 2) const int alpha_bits = 0;
layout (constant_id = 3) const int num_planes = 0;
layout (constant_id = 4) const int slices_per_picture = 0;
layout (constant_id = 5) const int max_quant = 0;

struct SliceData {
    uint32_t mbs_per_slice;
    int16_t coeffs[4][8 * 256];
};

struct SliceScore {
    ivec4 bits[16];
    ivec4 score[16];
    int total_bits[16];
    int total_score[16];
    int overquant;
    int buf_start;
    int quant;
};

layout(push_constant, scalar) uniform EncodeSliceInfo {
    u8buf bytestream;
    u8vec2buf seek_table;
};

layout (set = 0, binding = 0, scalar) readonly buffer SliceBuffer {
    SliceData slices[];
};
layout (set = 0, binding = 1, scalar) readonly buffer SliceScores {
    SliceScore scores[];
};
layout (set = 0, binding = 2, scalar) uniform ProresDataTables {
    int16_t qmat[128][64]; int16_t qmat_chroma[128][64];
};

#define CFACTOR_Y444 3

void encode_vlc_codeword(inout PutBitContext pb, uint codebook, int val)
{
    /* number of prefix bits to switch between Rice and expGolomb */
    uint switch_bits = (codebook & 3) + 1;
    uint rice_order  =  codebook >> 5;       /* rice code order */
    uint exp_order   = (codebook >> 2) & 7;  /* exp golomb code order */

    uint switch_val  = switch_bits << rice_order;

    if (val >= switch_val) {
        val -= int(switch_val - (1 << exp_order));
        int exponent = findMSB(val);

        put_bits(pb, exponent - exp_order + switch_bits, 0);
        put_bits(pb, exponent + 1, val);
    } else {
        int exponent = val >> rice_order;
        if (exponent != 0)
            put_bits(pb, exponent, 0);
        put_bits(pb, 1, 1);
        if (rice_order != 0)
            put_bits(pb, rice_order, zero_extend(val, rice_order));
    }
}

#define GET_SIGN(x)  ((x) >> 31)
#define MAKE_CODE(x) (((x) * 2) ^ GET_SIGN(x))

#define FIRST_DC_CB 0xB8 // rice_order = 5, exp_golomb_order = 6, switch_bits = 0

void encode_dcs(inout PutBitContext pb, bool is_chroma, int q)
{
    const uint8_t dc_codebook[7] = { U8(0x04), U8(0x28), U8(0x28), U8(0x4D), U8(0x4D), U8(0x70), U8(0x70) };

    uint slice = gl_GlobalInvocationID.x;
    uint plane = gl_GlobalInvocationID.y;
    uint blocks_per_mb = is_chroma && chroma_factor != CFACTOR_Y444 ? 2 : 4;
    uint blocks_per_slice = slices[slice].mbs_per_slice * blocks_per_mb;
    int codebook = 5;
    int scale = is_chroma ? qmat_chroma[q][0] : qmat[q][0];
    int coeff = slices[slice].coeffs[plane][0];
    int prev_dc = (coeff - 0x4000) / scale;
    encode_vlc_codeword(pb, FIRST_DC_CB, MAKE_CODE(prev_dc));
    int sign = 0;
    for (int i = 1; i < blocks_per_slice; i++) {
        coeff = slices[slice].coeffs[plane][i];
        int dc = (coeff - 0x4000) / scale;
        int delta = dc - prev_dc;
        int new_sign = GET_SIGN(delta);
        delta = (delta ^ sign) - sign;
        int code = MAKE_CODE(delta);
        encode_vlc_codeword(pb, dc_codebook[codebook], code);
        codebook = min(code, 6);
        sign = new_sign;
        prev_dc = dc;
    }
}

void encode_acs(inout PutBitContext pb, bool is_chroma, int q)
{
    const uint8_t run_to_cb[16] = { U8(0x06), U8(0x06), U8(0x05), U8(0x05), U8(0x04), U8(0x29),
                                    U8(0x29), U8(0x29), U8(0x29), U8(0x28), U8(0x28), U8(0x28),
                                    U8(0x28), U8(0x28), U8(0x28), U8(0x4C) };

    const uint8_t level_to_cb[10] = { U8(0x04), U8(0x0A), U8(0x05), U8(0x06), U8(0x04), U8(0x28),
                                      U8(0x28), U8(0x28), U8(0x28), U8(0x4C) };

    uint slice = gl_GlobalInvocationID.x;
    uint plane = gl_GlobalInvocationID.y;
    uint blocks_per_mb = is_chroma && chroma_factor != CFACTOR_Y444 ? 2 : 4;
    uint blocks_per_slice = slices[slice].mbs_per_slice * blocks_per_mb;
    int prev_run = 4;
    int prev_level = 2;
    int run = 0;

    for (uint i = 1; i < 64; i++) {
        int quant = is_chroma ? qmat_chroma[q][i] : qmat[q][i];
        for (uint j = 0; j < blocks_per_slice; j++) {
            uint idx = i * blocks_per_slice + j;
            int coeff = slices[slice].coeffs[plane][idx];
            int level = coeff / quant;
            if (level != 0) {
                int abs_level = abs(level);
                encode_vlc_codeword(pb, run_to_cb[prev_run], run);
                encode_vlc_codeword(pb, level_to_cb[prev_level], abs_level - 1);
                put_bits(pb, 1, zero_extend(GET_SIGN(level), 1));
                prev_run = min(run, 15);
                prev_level = min(abs_level, 9);
                run = 0;
            } else {
                run++;
            }
        }
    }
}

void encode_slice_plane(inout PutBitContext pb, int q)
{
    uint plane = gl_GlobalInvocationID.y;
    bool is_chroma = plane == 1 || plane == 2;
    encode_dcs(pb, is_chroma, q);
    encode_acs(pb, is_chroma, q);
}

void put_alpha_diff(inout PutBitContext pb, int cur, int prev)
{
    const int dbits = (alpha_bits == 8) ? 4 : 7;
    const int dsize = 1 << dbits - 1;
    int diff = cur - prev;

    diff = zero_extend(diff, alpha_bits);
    if (diff >= (1 << alpha_bits) - dsize)
        diff -= 1 << alpha_bits;
    if (diff < -dsize || diff > dsize || diff == 0) {
        put_bits(pb, 1, 1);
        put_bits(pb, alpha_bits, diff);
    } else {
        put_bits(pb, 1, 0);
        put_bits(pb, dbits - 1, abs(diff) - 1);
        put_bits(pb, 1, int(diff < 0));
    }
}

void put_alpha_run(inout PutBitContext pb, int run)
{
    if (run != 0) {
        put_bits(pb, 1, 0);
        if (run < 0x10)
            put_bits(pb, 4, run);
        else
            put_bits(pb, 15, run);
    } else {
        put_bits(pb, 1, 1);
    }
}

void encode_alpha_plane(inout PutBitContext pb)
{
    uint slice = gl_GlobalInvocationID.x;
    const int mask = (1 << alpha_bits) - 1;
    const int num_coeffs = int(slices[slice].mbs_per_slice) * 256;
    int prev = mask, cur;
    int idx = 0;
    int run = 0;

    cur = slices[slice].coeffs[3][idx++];
    put_alpha_diff(pb, cur, prev);
    prev = cur;
    do {
        cur = slices[slice].coeffs[3][idx++];
        if (cur != prev) {
            put_alpha_run(pb, run);
            put_alpha_diff(pb, cur, prev);
            prev = cur;
            run  = 0;
        } else {
            run++;
        }
    } while (idx < num_coeffs);
    put_alpha_run(pb, run);
}

u8vec2 byteswap16(int value)
{
    return unpack8(uint16_t(value)).yx;
}

void main()
{
    uint slice = gl_GlobalInvocationID.x;
    if (slice >= slices_per_picture)
        return;

    uint plane = gl_GlobalInvocationID.y;
    int q = scores[slice].quant;
    int q_idx = min(q, max_quant + 1);
    ivec4 bits = scores[slice].bits[q_idx];
    int slice_hdr_size = 2 * num_planes;
    int slice_size = slice_hdr_size + ((bits.x + bits.y + bits.z + bits.w) / 8);
    int buf_start = scores[slice].buf_start;
    u8buf buf = OFFBUF(u8buf, bytestream, buf_start);

    /* Write slice header */
    if (plane == 0) {
        buf[0].v = uint8_t(slice_hdr_size * 8);
        buf[1].v = uint8_t(q);
        u8vec2buf slice_hdr = OFFBUF(u8vec2buf, buf, 2);
        for (int i = 0; i < num_planes - 1; i++) {
            slice_hdr[i].v = byteswap16(bits[i] / 8);
        }
        seek_table[slice].v = byteswap16(slice_size);
    }

    int plane_offset = 0;
    for (int i = 0; i < plane; ++i)
        plane_offset += bits[i] / 8;

    /* Encode slice plane */
    PutBitContext pb;
    init_put_bits(pb, OFFBUF(u8buf, buf, slice_hdr_size + plane_offset), 0);
    if (plane == 3)
        encode_alpha_plane(pb);
    else
        encode_slice_plane(pb, q);
    flush_put_bits(pb);
}
