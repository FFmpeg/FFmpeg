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
#define APV_MIN_TRANS_COEFF -32768
#define APV_MAX_TRANS_COEFF 32767
#define APV_TR_SIZE         8
#define APV_BLK_COEFFS      (APV_TR_SIZE * APV_TR_SIZE)
#define APV_MB_SIZE         (ivec2(16, 16))

layout (set = 0, binding = 0) uniform writeonly uimage2D dst[];
layout (set = 0, binding = 2, scalar) writeonly buffer coeffs_out_buf {
    int16_t coeffs_out[];
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

GetBitContext gb;

/*
 * Decode one VLC code from a single 32-bit window, branchlessly. The longest
 * legal APV code is 18 bits (3 + 2*5 + 5), so one show + one skip covers every
 * case. The three layouts:
 *   1xxx          len 1+k,    val = k bits after the 1
 *   00xxx         len 2+k,    val = (1<<k) + k bits
 *   01 0^n 1 xxx  len 3+2n+k, val = (((1<<n)+1)<<k) + (n+k bits)
 */
int apv_read_vlc(int k)
{
    /* Top 32 bits; the longest legal code is 31 bits */
    uint bits = show_bits(gb, 32);
    uint mask = (1u << k) - 1u;

    bool caseA = bits >= 0x80000000u;
    bool caseB = bits <  0x40000000u;

    int valA = int((bits >> (31 - k)) & mask);
    int valB = int((bits >> (30 - k)) & mask) + (1 << k);

    /* clamp guards corrupt streams: legal codes have n <= 14 and
     * 3 + 2n + k <= 31, so legal decodes are unaffected */
    uint suffix = bits << 2;
    int n = clamp(31 - findMSB(suffix), 0, 14);
    int valC = (((1 << n) + 1) << k) +
               int((bits >> max(29 - 2 * n - k, 0)) & ((1u << (n + k)) - 1u));
    if (suffix == 0u)
        valC = APV_MAX_TRANS_COEFF + 1;

    int lenA = 1 + k;
    int lenB = 2 + k;
    int lenC = min(3 + 2 * n + k, 32);

    int val = caseA ? valA : (caseB ? valB : valC);
    int len = caseA ? lenA : (caseB ? lenB : lenC);

    skip_bits(gb, len);
    return val;
}

/*
 * As above, with the trailing sign bit folded into the same window. The caller
 * says whether a sign bit is present (DC: when val != 0; AC levels: always);
 * sign is only valid when it is.
 */
int apv_read_vlc_sign(int k, bool sign_always, out bool sign)
{
    /* Top 32 bits; the longest legal code is 31 bits, +1 for the sign */
    uint bits = show_bits(gb, 32);
    uint mask = (1u << k) - 1u;

    bool caseA = bits >= 0x80000000u;
    bool caseB = bits <  0x40000000u;

    int valA = int((bits >> (31 - k)) & mask);
    int valB = int((bits >> (30 - k)) & mask) + (1 << k);

    uint suffix = bits << 2;
    int n = clamp(31 - findMSB(suffix), 0, 14);
    int valC = (((1 << n) + 1) << k) +
               int((bits >> max(29 - 2 * n - k, 0)) & ((1u << (n + k)) - 1u));
    bool badC = suffix == 0u;

    int lenA = 1 + k;
    int lenB = 2 + k;
    int lenC = min(3 + 2 * n + k, 31);

    int val = caseA ? valA : (caseB ? valB : valC);
    int len = caseA ? lenA : (caseB ? lenB : lenC);

    bool has_sign = sign_always || val != 0;
    sign = has_sign && bool((bits >> (31 - len)) & 1u);
    len += has_sign ? 1 : 0;

    skip_bits(gb, len);
    if (!caseA && !caseB && badC)
        val = APV_MAX_TRANS_COEFF + 1;
    return val;
}

/* ff_zigzag_direct, packed: each byte is the raster index (y*8 + x). */
const uint8_t zigzag[64] = {
    uint8_t( 0), uint8_t( 1), uint8_t( 8), uint8_t(16),
    uint8_t( 9), uint8_t( 2), uint8_t( 3), uint8_t(10),
    uint8_t(17), uint8_t(24), uint8_t(32), uint8_t(25),
    uint8_t(18), uint8_t(11), uint8_t( 4), uint8_t( 5),
    uint8_t(12), uint8_t(19), uint8_t(26), uint8_t(33),
    uint8_t(40), uint8_t(48), uint8_t(41), uint8_t(34),
    uint8_t(27), uint8_t(20), uint8_t(13), uint8_t( 6),
    uint8_t( 7), uint8_t(14), uint8_t(21), uint8_t(28),
    uint8_t(35), uint8_t(42), uint8_t(49), uint8_t(56),
    uint8_t(57), uint8_t(50), uint8_t(43), uint8_t(36),
    uint8_t(29), uint8_t(22), uint8_t(15), uint8_t(23),
    uint8_t(30), uint8_t(37), uint8_t(44), uint8_t(51),
    uint8_t(58), uint8_t(59), uint8_t(52), uint8_t(45),
    uint8_t(38), uint8_t(31), uint8_t(39), uint8_t(46),
    uint8_t(53), uint8_t(60), uint8_t(61), uint8_t(54),
    uint8_t(47), uint8_t(55), uint8_t(62), uint8_t(63),
};

int prev_dc;
int prev_k_dc;
int prev_1st_ac_level;

void decode_block(uint cbase, int cstride, ivec2 pos, uint comp)
{
    int dc_coeff;
    bool dc_sign;

    int abs_diff = apv_read_vlc_sign(prev_k_dc, false, dc_sign);

    dc_coeff = prev_dc + (dc_sign ? -abs_diff : abs_diff);

    if (dc_coeff < APV_MIN_TRANS_COEFF ||
        dc_coeff > APV_MAX_TRANS_COEFF)
        return;

    coeffs_out[cbase + uint(pos.y * cstride + pos.x)] = int16_t(dc_coeff);
    prev_dc   = dc_coeff;
    prev_k_dc = min(abs_diff >> 1, 5);

    /* ACs */
    int scan_pos   = 1;
    int first_ac   = 1;
    int prev_level = prev_1st_ac_level;
    int prev_run   = 0;

    do {
        int coeff_zero_run;

        int k_param = clamp(prev_run >> 2, 0, 2);
        coeff_zero_run = apv_read_vlc(k_param);

        if (coeff_zero_run > APV_BLK_COEFFS - scan_pos)
            return;

        /* the coefficient buffer was zero-filled, so runs need no writes */
        scan_pos += coeff_zero_run;
        prev_run = coeff_zero_run;

        if (scan_pos < APV_BLK_COEFFS) {
            int abs_ac_coeff_minus1;
            int level;
            bool sign_ac_coeff;

            k_param = clamp(prev_level >> 2, 0, 4);
            abs_ac_coeff_minus1 = apv_read_vlc_sign(k_param, true,
                                                    sign_ac_coeff);

            if (sign_ac_coeff)
                level = -abs_ac_coeff_minus1 - 1;
            else
                level = abs_ac_coeff_minus1 + 1;

            if (level < APV_MIN_TRANS_COEFF || level > APV_MAX_TRANS_COEFF)
                return;

            int zz = int(zigzag[scan_pos]);
            coeffs_out[cbase + uint((pos.y + (zz >> 3)) * cstride +
                                    pos.x + (zz & 7))] = int16_t(level);

            prev_level = abs_ac_coeff_minus1 + 1;
            if (first_ac != 0) {
                prev_1st_ac_level = prev_level;
                first_ac = 0;
            }

            scan_pos++;
        }
    } while (scan_pos < APV_BLK_COEFFS);
}

void main(void)
{
    const ivec2 tile_pos = ivec2(gl_WorkGroupID.xy);
    const uint comp_idx = uint(gl_WorkGroupID.z);

    /* EC state */
    prev_dc = 0;
    prev_k_dc = 5;
    prev_1st_ac_level = 0;

    const int num_tiles = tile_count.x * tile_count.y;
    const int tile_idx  = tile_pos.y * tile_count.x + tile_pos.x;
    const uvec2 tile_bs = tile_offset[int(comp_idx) * num_tiles + tile_idx];
    init_get_bits(gb, u8buf(tile_data + tile_bs.x), int(tile_bs.y));

    ivec2 sub_shift = comp_idx == 0 ? ivec2(0) : log2_chroma_sub;

    /* This component's plane inside the flat coefficient buffer. Plane
     * dims are the MB-aligned coded size (the closing entries of the tile
     * col/row tables), in component resolution. */
    const int cw0 = int(tile_col[tile_count.x]);
    const int ch0 = int(tile_row[tile_count.y]);
    uint cbase = 0u;
    for (uint i = 0u; i < comp_idx; i++) {
        ivec2 ss = i == 0u ? ivec2(0) : log2_chroma_sub;
        cbase += uint((cw0 >> ss.x) * (ch0 >> ss.y));
    }
    const int cstride = cw0 >> sub_shift.x;

    ivec2 tile_start = ivec2(tile_col[tile_pos.x], tile_row[tile_pos.y]);
    ivec2 tile_dim = ivec2(tile_col[tile_pos.x + 1],
                           tile_row[tile_pos.y + 1]) - tile_start;
    ivec2 tile_mb_dim = tile_dim / APV_MB_SIZE;
    ivec2 blk_mb_dim = ivec2(2, 2) >> sub_shift;

    ivec2 mb, blk;
    for (mb.y = 0; mb.y < tile_mb_dim.y; mb.y++) {
        for (mb.x = 0; mb.x < tile_mb_dim.x; mb.x++) {
            for (blk.y = 0; blk.y < blk_mb_dim.y; blk.y++) {
                for (blk.x = 0; blk.x < blk_mb_dim.x; blk.x++) {
                    ivec2 pos = (APV_MB_SIZE*mb +
                                 APV_TR_SIZE*blk + tile_start) >> sub_shift;

                    decode_block(cbase, cstride, pos, comp_idx);
                }
            }
        }
    }
}
