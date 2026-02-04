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

#define FULL_RENORM
#include "common.comp"
#include "ffv1_common.glsl"

uint8_t state[CONTEXT_SIZE];

void init_slice(inout SliceContext sc, uint slice_idx)
{
    /* Set coordinates */
    uint sxs = slice_coord(img_size.x, gl_WorkGroupID.x + 0,
                           gl_NumWorkGroups.x, chroma_shift.x);
    uint sxe = slice_coord(img_size.x, gl_WorkGroupID.x + 1,
                           gl_NumWorkGroups.x, chroma_shift.x);
    uint sys = slice_coord(img_size.y, gl_WorkGroupID.y + 0,
                           gl_NumWorkGroups.y, chroma_shift.y);
    uint sye = slice_coord(img_size.y, gl_WorkGroupID.y + 1,
                           gl_NumWorkGroups.y, chroma_shift.y);

    sc.slice_pos = ivec2(sxs, sys);
    sc.slice_dim = ivec2(sxe - sxs, sye - sys);
    sc.slice_coding_mode = int(force_pcm);
    sc.slice_reset_contexts = sc.slice_coding_mode == 1;
    sc.quant_table_idx = u8vec3(context_model);

    if (!rct_search || (sc.slice_coding_mode == 1))
        sc.slice_rct_coef = ivec2(1, 1);

    rac_init(sc.c,
             OFFBUF(u8buf, slice_data, slice_idx * slice_size_max),
             slice_size_max);
}

void put_usymbol(inout RangeCoder c, uint v)
{
    bool is_nil = (v == 0);
    put_rac_direct(c, state[0], is_nil);
    if (is_nil)
        return;

    const int e = findMSB(v);

    for (int i = 0; i < e; i++)
        put_rac_direct(c, state[1 + min(i, 9)], true);
    put_rac_direct(c, state[1 + min(e, 9)], false);

    for (int i = e - 1; i >= 0; i--)
        put_rac_direct(c, state[22 + min(i, 9)], bool(bitfieldExtract(v, i, 1)));
}

void write_slice_header(inout SliceContext sc)
{
    [[unroll]]
    for (int i = 0; i < CONTEXT_SIZE; i++)
        state[i] = uint8_t(128);

    put_usymbol(sc.c, gl_WorkGroupID.x);
    put_usymbol(sc.c, gl_WorkGroupID.y);
    put_usymbol(sc.c, 0);
    put_usymbol(sc.c, 0);

    for (int i = 0; i < codec_planes; i++)
        put_usymbol(sc.c, sc.quant_table_idx[i]);

    put_usymbol(sc.c, pic_mode);
    put_usymbol(sc.c, sar.x);
    put_usymbol(sc.c, sar.y);

    if (version >= 4) {
        put_rac_direct(sc.c, state[0], sc.slice_reset_contexts);
        put_usymbol(sc.c, sc.slice_coding_mode);
        if (sc.slice_coding_mode != 1 && colorspace == 1) {
            put_usymbol(sc.c, sc.slice_rct_coef.y);
            put_usymbol(sc.c, sc.slice_rct_coef.x);
        }
    }
}

void write_frame_header(inout SliceContext sc)
{
    put_rac_equi(sc.c, bool(key_frame));
}

void main(void)
{
    const uint slice_idx = gl_WorkGroupID.y*gl_NumWorkGroups.x + gl_WorkGroupID.x;

    init_slice(slice_ctx[slice_idx], slice_idx);

    if (slice_idx == 0)
        write_frame_header(slice_ctx[slice_idx]);

    write_slice_header(slice_ctx[slice_idx]);
}
