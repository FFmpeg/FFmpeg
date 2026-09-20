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

#define NB_CONTEXTS 6
#define FULL_RENORM
#include "common.glsl"
#include "ffv1_common.glsl"

layout (set = 1, binding = 1, scalar) buffer fltmap_buf {
    uint fltmap[];
};

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

    if (!rct_search || force_pcm)
        sc.slice_rct_coef = ivec2(1, 1);

    rac_init(slice_idx*slice_size_max, slice_size_max);
}

void put_usymbol(uint v, uint ctx_off)
{
    bool is_nil = (v == 0);
    put_rac(rc_state[ctx_off], is_nil);
    if (is_nil)
        return;

    const int e = findMSB(v);

    for (int i = 0; i <= e; i++)
        put_rac(rc_state[ctx_off + 1 + min(i, 9)], i < e);

    for (int i = e - 1; i >= 0; i--)
        put_rac(rc_state[ctx_off + 22 + min(i, 9)],
                bool(bitfieldExtract(v, i, 1)));
}

shared uint hdr_sym[4 + 4 + 3];
const int nb_hdr_sym = 4 + codec_planes + 3;

void encode_histogram_remap(uint slice_idx, inout SliceContext sc)
{
    const int flip = (remap_mode == 2) ? 0x7FFF : 0;

    for (int p = 0; p < color_planes; p++) {
        const uint base = (slice_idx*4u + uint(p))*65536u;
        uint j = 0;
        uint lu = 0;
        int run = 0;

        for (int i = 0; i < NB_CONTEXTS*CONTEXT_SIZE; i++)
            rc_state[i] = uint8_t(128);

        put_usymbol(0, 0);

        for (int i = 0; i < NB_CONTEXTS*CONTEXT_SIZE; i++)
            rc_state[i] = uint8_t(128);

        int cnt = 0;
        for (int i = 0; i < rct_offset; i++) {
            int ri = i ^ (((i & 0x8000) != 0) ? 0 : flip);
            uint u = uint(fltmap[base + uint(ri)] != 0u);

            fltmap[base + uint(ri)] = j;
            j += u;

            if (lu == u) {
                run++;
            } else {
                put_usymbol(run, lu*CONTEXT_SIZE);
                if (run == 0)
                    lu = u;
                run = 0;
            }
        }

        if (run != 0)
            put_usymbol(run, lu*CONTEXT_SIZE);
        sc.remap_count[p] = int(j);
    }
}

/* The 32-bit float remap uses 6 contexts: state[lu][category][bit] with
 * lu = 0,1 and category = 0 (run/step-1), 1 (delta, unused here), 2 (mul). */
#define CTX_F32(lu, cat) ((uint(lu)*3u + uint(cat))*CONTEXT_SIZE)

void set_compact_index(uint plane_base, uint bitmap_base, uint pixel_num,
                       inout uint i, int ci)
{
    const uint u_val = fltmap[plane_base + 2u*i + 0u];

    /* Assign the index to every pixel with this value (sorted, so adjacent) */
    do {
        fltmap[bitmap_base + fltmap[plane_base + 2u*i + 1u]] = uint(ci);
        i++;
    } while (i < pixel_num && fltmap[plane_base + 2u*i + 0u] == u_val);
}

/* Codes the sorted unique values of each plane by mirroring the decoder's
 * state machine. The decoder walks a cursor over the 32-bit value range and
 * alternates between two modes:
 *   lu == 0: reads run, emits the single value (cursor + run), moves the
 *            cursor past it, and switches to lu == 1 if run was 0.
 *   lu == 1: reads run, emits run consecutive values starting at the cursor,
 *            skips one value, and switches to lu == 0 if run was 0.
 * It stops once the cursor is past 0xFFFFFFFF. A single multiplier of 1 is
 * used (mul_count == 1), so every value is coded with unit steps. */
void encode_float32_remap(uint slice_idx, inout SliceContext sc)
{
    const uint slice_w = uint(sc.slice_dim.x);
    const uint slice_h = uint(sc.slice_dim.y);
    const uint pixel_num = slice_w * slice_h;
    const uint slice_base = slice_idx*12u*max_pixels_per_slice;
    const int64_t end = int64_t(0xFFFFFFFFu);

    for (int p = 0; p < color_planes; p++) {
        /* Layout: per slice, the sorted units of each plane (max_pixels*8
         * bytes), then the bitmaps of each plane (max_pixels*4 bytes). The
         * units are read-only here, the bitmap is written. */
        const uint plane_base = slice_base + uint(p)*2u*max_pixels_per_slice;
        const uint bitmap_base = slice_base + (8u + uint(p))*max_pixels_per_slice;

        for (int i = 0; i < NB_CONTEXTS*CONTEXT_SIZE; i++)
            rc_state[i] = uint8_t(128);

        /* mul_count */
        put_usymbol(1, CTX_F32(0, 0));

        for (int i = 0; i < NB_CONTEXTS*CONTEXT_SIZE; i++)
            rc_state[i] = uint8_t(128);

        int64_t cursor = 0;
        uint lu = 0;
        int ci = -1;
        uint i = 0;

        while (cursor <= end) {
            if (i >= pixel_num) {
                /* No values left: move the decoder's cursor past the end */
                if (lu == 0u) {
                    put_usymbol(uint(end + 1 - cursor), CTX_F32(0, 0));
                    cursor = end + 1;
                } else {
                    put_usymbol(0, CTX_F32(1, 0));
                    cursor++;
                    lu = 0;
                }
                continue;
            }

            if (lu == 0u) {
                const int64_t val = int64_t(fltmap[plane_base + 2u*i + 0u]);
                const uint run = uint(val - cursor);

                put_usymbol(run, CTX_F32(0, 0));

                /* The decoder reads the multiplier right after the first
                 * value, unless that value is the last possible one */
                if (ci < 0 && val < end)
                    put_usymbol(1, CTX_F32(0, 2));

                set_compact_index(plane_base, bitmap_base, pixel_num, i, ++ci);

                cursor = val + 1;
                lu = uint(run == 0u);
            } else {
                uint run = 0;

                while (i < pixel_num &&
                       int64_t(fltmap[plane_base + 2u*i + 0u]) == cursor) {
                    set_compact_index(plane_base, bitmap_base, pixel_num, i, ++ci);
                    cursor++;
                    run++;
                }

                put_usymbol(run, CTX_F32(1, 0));

                /* The decoder skips one value after a run */
                cursor++;
                lu = uint(run != 0u);
            }
        }

        sc.remap_count[p] = ci + 1;
    }
}

void write_slice_header(uint slice_idx, inout SliceContext sc)
{
    [[unroll]]
    for (int i = 0; i < CONTEXT_SIZE; i++)
        rc_state[i] = uint8_t(128);

    hdr_sym[0] = gl_WorkGroupID.x;
    hdr_sym[1] = gl_WorkGroupID.y;
    hdr_sym[2] = 0;
    hdr_sym[3] = 0;

    [[unroll]]
    for (int i = 0; i < codec_planes; i++)
        hdr_sym[4 + i] = context_model;

    hdr_sym[nb_hdr_sym - 3] = pic_mode;
    hdr_sym[nb_hdr_sym - 2] = sar.x;
    hdr_sym[nb_hdr_sym - 1] = sar.y;

    for (int i = 0; i < nb_hdr_sym; i++)
        put_usymbol(hdr_sym[i], 0);

    if (version >= 4) {
        put_rac(rc_state[0], force_pcm);
        put_usymbol(uint(force_pcm), 0);
        if (!force_pcm && colorspace != 0) {
            put_usymbol(sc.slice_rct_coef.g, 0);
            put_usymbol(sc.slice_rct_coef.r, 0);
        }

        if (micro_version >= 4) {
            put_usymbol(remap_mode, 0);
            if (remap_mode != 0) {
                if (c_bits >= 32)
                    encode_float32_remap(slice_idx, sc);
                else
                    encode_histogram_remap(slice_idx, sc);
            }
        }
    }
}

void write_frame_header(inout SliceContext sc)
{
    put_rac_equi(bool(key_frame));
}

void main(void)
{
    const uint slice_idx = gl_WorkGroupID.y*gl_NumWorkGroups.x + gl_WorkGroupID.x;

    init_slice(slice_ctx[slice_idx], slice_idx);

    if (slice_idx == 0)
        write_frame_header(slice_ctx[slice_idx]);

    write_slice_header(slice_idx, slice_ctx[slice_idx]);

    slice_ctx[slice_idx].c = rc;
}
