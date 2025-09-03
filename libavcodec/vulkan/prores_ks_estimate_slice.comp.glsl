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
#extension GL_KHR_shader_subgroup_clustered : require
#extension GL_KHR_shader_subgroup_shuffle : require
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout (constant_id = 0) const int max_mbs_per_slice = 8;
layout (constant_id = 1) const int chroma_factor = 0;
layout (constant_id = 2) const int alpha_bits = 0;
layout (constant_id = 3) const int num_planes = 0;
layout (constant_id = 4) const int slices_per_picture = 0;
layout (constant_id = 5) const int min_quant = 0;
layout (constant_id = 6) const int max_quant = 0;
layout (constant_id = 7) const int bits_per_mb = 0;

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

layout (set = 0, binding = 0, scalar) readonly buffer SliceBuffer {
    SliceData slices[];
};
layout (set = 0, binding = 1, scalar) writeonly buffer SliceScores {
    SliceScore scores[];
};
layout (set = 0, binding = 2, scalar) uniform ProresDataTables {
    int16_t qmat[128][64];
    int16_t qmat_chroma[128][64];
};

#define CFACTOR_Y444 3

#define GET_SIGN(x)  ((x) >> 31)
#define MAKE_CODE(x) (((x) * 2) ^ GET_SIGN(x))

int estimate_vlc(uint codebook, int val)
{
    /* number of prefix bits to switch between Rice and expGolomb */
    uint switch_bits = (codebook & 3) + 1;
    uint rice_order  =  codebook >> 5;       /* rice code order */
    uint exp_order   = (codebook >> 2) & 7;  /* exp golomb code order */

    uint switch_val  = switch_bits << rice_order;

    if (val >= switch_val) {
        val -= int(switch_val - (1 << exp_order));
        int exponent = findMSB(val);
        return int(exponent * 2 - exp_order + switch_bits + 1);
    } else {
        return int((val >> rice_order) + rice_order + 1);
    }
}

#define FIRST_DC_CB 0xB8 // rice_order = 5, exp_golomb_order = 6, switch_bits = 0

int estimate_dcs(inout int error, uint slice, uint plane, uint q)
{
    const uint8_t dc_codebook[7] = { U8(0x04), U8(0x28), U8(0x28), U8(0x4D), U8(0x4D), U8(0x70), U8(0x70) };

    uint blocks_per_mb = plane != 0 && chroma_factor != CFACTOR_Y444 ? 2 : 4;
    uint blocks_per_slice = slices[slice].mbs_per_slice * blocks_per_mb;
    int codebook = 5;
    int coeff = slices[slice].coeffs[plane][0];
    int scale = plane != 0 ? qmat_chroma[q][0] : qmat[q][0];
    int prev_dc = (coeff - 0x4000) / scale;
    int bits = estimate_vlc(FIRST_DC_CB, MAKE_CODE(prev_dc));
    int sign = 0;

    for (int i = 1; i < blocks_per_slice; ++i) {
        coeff = slices[slice].coeffs[plane][i];
        int dc = (coeff - 0x4000) / scale;
        error += abs(coeff - 0x4000) % scale;
        int delta = dc - prev_dc;
        int new_sign = GET_SIGN(delta);
        delta = (delta ^ sign) - sign;
        int code = MAKE_CODE(delta);
        bits += estimate_vlc(dc_codebook[codebook], code);
        codebook = min(code, 6);
        sign = new_sign;
        prev_dc = dc;
    }

    return bits;
}

#define FFALIGN(x, a) (((x)+(a)-1)&~((a)-1))
#define SCORE_LIMIT   1073741823

int estimate_acs(inout int error, uint slice, uint plane, uint q)
{
    const uint8_t run_to_cb[16] = { U8(0x06), U8(0x06), U8(0x05), U8(0x05), U8(0x04), U8(0x29),
                                    U8(0x29), U8(0x29), U8(0x29), U8(0x28), U8(0x28), U8(0x28),
                                    U8(0x28), U8(0x28), U8(0x28), U8(0x4C) };

    const uint8_t level_to_cb[10] = { U8(0x04), U8(0x0A), U8(0x05), U8(0x06), U8(0x04), U8(0x28),
                                      U8(0x28), U8(0x28), U8(0x28), U8(0x4C) };

    uint blocks_per_mb = plane != 0 && chroma_factor != CFACTOR_Y444 ? 2 : 4;
    uint blocks_per_slice = slices[slice].mbs_per_slice * blocks_per_mb;
    uint max_coeffs = blocks_per_slice << 6;
    int prev_run = 4;
    int prev_level = 2;
    int bits = 0;
    int run = 0;

    for (uint i = 1; i < 64; i++) {
        int quant = plane != 0 ? qmat_chroma[q][i] : qmat[q][i];
        for (uint j = 0; j < blocks_per_slice; j++) {
            uint idx = i * blocks_per_slice + j;
            int coeff = slices[slice].coeffs[plane][idx];
            int level = coeff / quant;
            error += abs(coeff) % quant;
            if (level != 0) {
                int abs_level = abs(level);
                bits += estimate_vlc(run_to_cb[prev_run], run);
                bits += estimate_vlc(level_to_cb[prev_level], abs_level - 1) + 1;
                prev_run = min(run, 15);
                prev_level = min(abs_level, 9);
                run = 0;
            } else {
                run++;
            }
        }
    }

    return bits;
}

int estimate_slice_plane(inout int error, uint slice, uint plane, uint q)
{
    int bits = 0;
    bits += estimate_dcs(error, slice, plane, q);
    bits += estimate_acs(error, slice, plane, q);
    return FFALIGN(bits, 8);
}

int est_alpha_diff(int cur, int prev)
{
    const int dbits = (alpha_bits == 8) ? 4 : 7;
    const int dsize = 1 << dbits - 1;
    int diff = cur - prev;

    diff = zero_extend(diff, alpha_bits);
    if (diff >= (1 << alpha_bits) - dsize)
        diff -= 1 << alpha_bits;
    if (diff < -dsize || diff > dsize || diff == 0)
        return alpha_bits + 1;
    else
        return dbits + 1;
}

int estimate_alpha_plane(uint slice)
{
    const int mask  = (1 << alpha_bits) - 1;
    const int num_coeffs = int(slices[slice].mbs_per_slice) * 256;
    int prev = mask, cur;
    int idx = 0;
    int run = 0;
    int bits;

    cur = slices[slice].coeffs[3][idx++];
    bits = est_alpha_diff(cur, prev);
    prev = cur;
    do {
        cur = slices[slice].coeffs[3][idx++];
        if (cur != prev) {
            if (run == 0)
                bits++;
            else if (run < 0x10)
                bits += 5;
            else
                bits += 16;
            bits += est_alpha_diff(cur, prev);
            prev = cur;
            run  = 0;
        } else {
            run++;
        }
    } while (idx < num_coeffs);

    if (run != 0) {
        if (run < 0x10)
            bits += 5;
        else
            bits += 16;
    } else {
        bits++;
    }

    return bits;
}

int sum_of_planes(int value)
{
    if (num_planes == 3) {
        uint base = (gl_SubgroupInvocationID / 3) * 3;
        return subgroupShuffle(value, base) + subgroupShuffle(value, base + 1) + subgroupShuffle(value, base + 2);
    } else
        return subgroupClusteredAdd(value, 4);
}

void main()
{
    uint slice = gl_GlobalInvocationID.x / num_planes;
    uint plane = gl_LocalInvocationID.x % num_planes;
    uint q = min_quant + gl_GlobalInvocationID.y;
    if (slice >= slices_per_picture)
        return;

    /* Estimate slice bits and error for specified quantizer and plane */
    int error = 0;
    int bits = 0;
    if (plane == 3)
        bits = estimate_alpha_plane(slice);
    else
        bits = estimate_slice_plane(error, slice, plane, q);

    /* Write results to score buffer */
    scores[slice].bits[q][plane] = FFALIGN(bits, 8);
    scores[slice].score[q][plane] = error;

    /* Accumulate total bits and error of all planes */
    int total_bits = sum_of_planes(bits);
    int total_score = sum_of_planes(error);
    if (total_bits > 65000 * 8)
        total_score = SCORE_LIMIT;
    scores[slice].total_bits[q] = total_bits;
    scores[slice].total_score[q] = total_score;

    if (q != max_quant)
        return;

    /* Task threads that computed max_quant to also compute overquant if necessary */
    uint mbs_per_slice = slices[slice].mbs_per_slice;
    if (total_bits <= bits_per_mb * mbs_per_slice) {
        /* Overquant isn't needed for this slice */
        scores[slice].total_bits[max_quant + 1] = total_bits;
        scores[slice].total_score[max_quant + 1] = total_score + 1;
        scores[slice].bits[max_quant + 1][plane] = FFALIGN(bits, 8);
        scores[slice].score[max_quant + 1][plane] = error;
        scores[slice].overquant = int(max_quant);
    } else {
        /* Keep searching until an encoding fits our budget */
        for (q = max_quant + 1; q < 128; ++q) {
            /* Estimate slice bits and error for specified quantizer and plane */
            error = 0;
            bits = 0;
            if (plane == 3)
                bits = estimate_alpha_plane(slice);
            else
                bits = estimate_slice_plane(error, slice, plane, q);

            /* Accumulate total bits and error of all planes */
            total_bits = sum_of_planes(bits);
            total_score = sum_of_planes(error);

            /* If estimated bits fit within budget, we are done */
            if (total_bits <= bits_per_mb * mbs_per_slice)
                break;
        }

        scores[slice].bits[max_quant + 1][plane] = bits;
        scores[slice].score[max_quant + 1][plane] = error;
        scores[slice].total_bits[max_quant + 1] = total_bits;
        scores[slice].total_score[max_quant + 1] = total_score;
        scores[slice].overquant = int(q);
    }
}
