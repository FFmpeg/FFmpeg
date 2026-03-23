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

#define NB_CONTEXTS 2
#include "common.glsl"
#include "ffv1_common.glsl"

layout (set = 0, binding = 1, scalar) uniform crc_ieee_buf {
    uint32_t crc_ieee[256];
};

layout (set = 1, binding = 1, scalar) readonly buffer slice_offsets_buf {
    u32vec2 slice_offsets[];
};
layout (set = 1, binding = 2, scalar) writeonly buffer slice_status_buf {
    uint32_t slice_status[];
};

layout(set = 1, binding = 3) writeonly buffer fltmap_buf {
    uint fltmap[][4][65536];
};

shared uint hdr_sym[4 + 4 + 3];
const int nb_hdr_sym = 4 + codec_planes + 3;

uint get_usymbol(const uint ctx_off)
{
    if (get_rac(rc_state[ctx_off]))
        return 0;

    int e = 0;
    while (get_rac(rc_state[ctx_off + 1 + min(e, 9)])) // 1..10
        e++;

    uint a = 1;
    for (int i = e - 1; i >= 0; i--) {
        a <<= 1;
        a |= uint(get_rac(rc_state[ctx_off + 22 + min(i, 9)]));  // 22..31
    }

    return a;
}

void decode_remap(uint slice_idx, inout SliceContext sc)
{
    int end = rct_offset - 1;
    int flip = sc.remap == 2 ? (end >> 1) : 0;

    for (int p = 0; p < color_planes; p++) {
        int j = 0;
        int lu = 0;

        [[unroll]]
        for (int i = 0; i < NB_CONTEXTS; i++)
            rc_state[i] = uint8_t(128);

        get_usymbol(0);

        [[unroll]]
        for (int i = 0; i < NB_CONTEXTS*CONTEXT_SIZE; i++)
            rc_state[i] = uint8_t(128);

        for (uint i = 0; i <= end; ) {
            uint run = get_usymbol(lu*CONTEXT_SIZE);
            uint run0 = lu != 0 ? 0   : run;
            uint run1 = lu != 0 ? run : 1;

            i += run0;
            while (run1-- > 0) {
                if (i - 1 >= end)
                    break;
                fltmap[slice_idx][p][j++] = i ^ (((i & 0x8000) != 0) ? 0 : flip);
                i++;
            }
            if (lu > 0)
                i++;
            lu ^= int(run == 0);
        }
        sc.remap_count[p] = uint16_t(j);
    }
}

bool decode_slice_header(uint slice_idx, inout SliceContext sc)
{
    [[unroll]]
    for (int i = 0; i < CONTEXT_SIZE; i++)
        rc_state[i] = uint8_t(128);

    for (int i = 0; i < nb_hdr_sym; i++)
        hdr_sym[i] = get_usymbol(0);

    uint sx = hdr_sym[0];
    uint sy = hdr_sym[1];
    uint sw = hdr_sym[2] + 1;
    uint sh = hdr_sym[3] + 1;

    if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0 ||
        sx > (gl_NumWorkGroups.x - sw) || sy > (gl_NumWorkGroups.y - sh))
        return true;

    /* Set coordinates */
    uint sxs = slice_coord(img_size.x, sx     , gl_NumWorkGroups.x, chroma_shift.x);
    uint sxe = slice_coord(img_size.x, sx + sw, gl_NumWorkGroups.x, chroma_shift.x);
    uint sys = slice_coord(img_size.y, sy     , gl_NumWorkGroups.y, chroma_shift.y);
    uint sye = slice_coord(img_size.y, sy + sh, gl_NumWorkGroups.y, chroma_shift.y);

    sc.slice_pos = ivec2(sxs, sys);
    sc.slice_dim = ivec2(sxe - sxs, sye - sys);
    sc.slice_rct_coef = ivec2(1, 1);
    sc.slice_coding_mode = int(0);

    for (uint i = 0; i < codec_planes; i++) {
        uint idx = hdr_sym[4 + i];
        if (idx >= quant_table_count)
            return true;
        sc.quant_table_idx[i] = uint8_t(idx);
    }

    if (version >= 4) {
        sc.slice_reset_contexts = get_rac(rc_state[0]);
        sc.slice_coding_mode = get_usymbol(0);
        if (sc.slice_coding_mode != 1 && colorspace == 1) {
            sc.slice_rct_coef.g = int(get_usymbol(0));
            sc.slice_rct_coef.r = int(get_usymbol(0));
            if (sc.slice_rct_coef.g + sc.slice_rct_coef.r > 4)
                return true;
        }

        if (micro_version >= 4) {
            sc.remap = get_usymbol(0);
            if (sc.remap != 0)
                decode_remap(slice_idx, sc);
        }
    }

    return false;
}

void main(void)
{
    uint slice_idx = gl_WorkGroupID.y*gl_NumWorkGroups.x + gl_WorkGroupID.x;

    rac_init_dec(slice_offsets[slice_idx].x, slice_offsets[slice_idx].y);

    if (slice_idx == (gl_NumWorkGroups.x*gl_NumWorkGroups.y - 1))
        get_rac_equi();

    decode_slice_header(slice_idx, slice_ctx[slice_idx]);

    slice_ctx[slice_idx].c = rc;

    if (has_crc) {
        u8buf bs = u8buf(slice_data + slice_offsets[slice_idx].x);
        uint32_t slice_size = slice_offsets[slice_idx].y;

        uint32_t crc = crcref;
        for (int i = 0; i < slice_size; i++)
            crc = crc_ieee[(crc & 0xFF) ^ uint32_t(bs[i].v)] ^ (crc >> 8);

        slice_status[2*slice_idx + 0] = crc;
    }

    uint overread = 0;
    if (rc.bs_off >= (rc.bs_end + MAX_OVERREAD))
        overread = rc.bs_off - rc.bs_end;
    slice_status[2*slice_idx + 1] = overread;
}
