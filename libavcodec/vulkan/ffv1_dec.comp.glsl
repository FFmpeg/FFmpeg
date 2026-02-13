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

#define DECODE
#include "common.glsl"
#include "ffv1_common.glsl"

layout (set = 1, binding = 1, scalar) readonly buffer slice_offsets_buf {
    u32vec2 slice_offsets[];
};
layout (set = 1, binding = 2, scalar) writeonly buffer slice_status_buf {
    uint32_t slice_status[];
};
layout (set = 1, binding = 4) uniform uimage2D dec[];

#ifndef GOLOMB

layout (set = 1, binding = 3, scalar) buffer slice_state_buf {
    uint8_t slice_rc_state[];
};

#define READ(idx) get_rac_state(idx)
int get_isymbol(void)
{
    if (READ(0))
        return 0;

    int e = 1;
    for (; e < 11; e++) {
        if (!READ(e))
            break;
    }

    int a = 1;
    int i = e;

    if (bits > 8 && e == 11) {
        do {
            rc_state[10] = zero_one_state[rc_state[10] + 256];
            e++;
        } while (READ(10));

        e--;
        i = e - 1;

        a <<= 1;
        a |= int(READ(31));
        for (; i >= 11; i--) {
            rc_state[31] = zero_one_state[rc_state[31] +
                                          (rc_data[31] ? 256 : 0)];
            a <<= 1;
            a |= int(READ(31));
        }
    }

    a <<= i - 1;
    i -= 2;
    for (; i >= 0; i--)
        a |= int(READ(i + 22)) << i;

    return READ(min(e + 10, 21)) ? -a : a;
}

void decode_line_pcm(ivec2 sp, int w, int y, int p)
{
    if (gl_LocalInvocationID.x > 0)
        return;

#ifndef RGB
    if (p > 0 && p < 3) {
        w = ceil_rshift(w, chroma_shift.x);
        sp >>= chroma_shift;
    }
#endif

    for (int x = 0; x < w; x++) {
        uint v = 0;

        [[unroll]]
        for (uint i = (rct_offset >> 1); i > 0; i >>= 1)
            v |= get_rac_equi() ? i : 0;

        imageStore(dec[p], sp + LADDR(ivec2(x, y)), uvec4(v));
    }
}

void decode_line(ivec2 sp, int w,
                 int y, int p, uint state_off,
                 uint8_t quant_table_idx, int run_index)
{
#ifndef RGB
    if (p > 0 && p < 3) {
        w = ceil_rshift(w, chroma_shift.x);
        sp >>= chroma_shift;
    }
#endif

    for (int x = 0; x < w; x++) {
        ivec2 pr = get_pred(dec[p], sp, ivec2(x, y), 0, w,
                            quant_table_idx, extend_lookup[quant_table_idx]);

        uint rc_off = state_off + CONTEXT_SIZE*abs(pr[0]) + gl_LocalInvocationID.x;

        rc_state[gl_LocalInvocationID.x] = slice_rc_state[rc_off];
        rc_dec[gl_LocalInvocationID.x] = false;
        barrier();

        if (gl_LocalInvocationID.x == 0) {
            int diff = get_isymbol();
            if (pr[0] < 0)
                diff = -diff;

            uint v = zero_extend(pr[1] + diff, bits);
            imageStore(dec[p], sp + LADDR(ivec2(x, y)), uvec4(v));
        }

        /* Image write now visible to other invocs */
        memoryBarrierImage();
        barrier();
        if (rc_dec[gl_LocalInvocationID.x])
            slice_rc_state[rc_off] =
                zero_one_state[rc_state[gl_LocalInvocationID.x] +
                               (rc_data[gl_LocalInvocationID.x] ? 256 : 0)];
    }
}

#else /* GOLOMB */

layout (set = 1, binding = 3, scalar) buffer slice_state_buf {
    VlcState slice_vlc_state[];
};

GetBitContext gb;

void golomb_init(void)
{
    if (version == 3 && micro_version > 1 || version > 3)
        get_rac_internal(rc.range * 129 >> 8);

    uint64_t ac_byte_count = rc.bs_off - rc.bs_start - 1;
    init_get_bits(gb, u8buf(rc.bs_start + ac_byte_count),
                  int(rc.bs_end - rc.bs_start - ac_byte_count));
}

void decode_line(ivec2 sp, int w,
                 int y, int p, uint state_off,
                 uint8_t quant_table_idx, inout int run_index)
{
#ifndef RGB
    if (p > 0 && p < 3) {
        w = ceil_rshift(w, chroma_shift.x);
        sp >>= chroma_shift;
    }
#endif

    int run_count = 0;
    int run_mode  = 0;

    for (int x = 0; x < w; x++) {
        ivec2 pos = sp + ivec2(x, y);
        int diff;
        ivec2 pr = get_pred(dec[p], sp, ivec2(x, y), 0, w,
                            quant_table_idx, extend_lookup[quant_table_idx]);

        uint vlc_off = state_off + abs(pr[0]);

        if (pr[0] == 0 && run_mode == 0)
            run_mode = 1;

        if (run_mode != 0) {
            if (run_count == 0 && run_mode == 1) {
                int tmp_idx = int(log2_run[run_index]);
                if (get_bit(gb)) {
                    run_count = 1 << tmp_idx;
                    if (x + run_count <= w)
                        run_index++;
                } else {
                    if (tmp_idx != 0) {
                        run_count = int(get_bits(gb, tmp_idx));
                    } else
                        run_count = 0;

                    if (run_index != 0)
                        run_index--;
                    run_mode = 2;
                }
            }

            run_count--;
            if (run_count < 0) {
                run_mode  = 0;
                run_count = 0;
                diff = read_vlc_symbol(gb, slice_vlc_state[vlc_off], bits);
                if (diff >= 0)
                    diff++;
            } else {
                diff = 0;
            }
        } else {
            diff = read_vlc_symbol(gb, slice_vlc_state[vlc_off], bits);
        }

        if (pr[0] < 0)
            diff = -diff;

        uint v = zero_extend(pr[1] + diff, bits);
        imageStore(dec[p], sp + LADDR(ivec2(x, y)), uvec4(v));
    }
}
#endif

#ifdef RGB
ivec4 transform_sample(ivec4 pix, ivec2 rct_coef)
{
    pix.b -= rct_offset;
    pix.r -= rct_offset;
    pix.g -= (pix.b*rct_coef.y + pix.r*rct_coef.x) >> 2;
    pix.b += pix.g;
    pix.r += pix.g;
    return ivec4(pix[fmt_lut[0]], pix[fmt_lut[1]],
                 pix[fmt_lut[2]], pix[fmt_lut[3]]);
}

void writeout_rgb(in SliceContext sc, ivec2 sp, int w, int y, bool apply_rct)
{
    memoryBarrierImage();
    barrier();

    for (uint x = gl_LocalInvocationID.x; x < w; x += gl_WorkGroupSize.x) {
        ivec2 lpos = sp + LADDR(ivec2(x, y));
        ivec2 pos = sc.slice_pos + ivec2(x, y);

        ivec4 pix;
        pix.r = int(imageLoad(dec[2], lpos)[0]);
        pix.g = int(imageLoad(dec[0], lpos)[0]);
        pix.b = int(imageLoad(dec[1], lpos)[0]);
        if (transparency)
            pix.a = int(imageLoad(dec[3], lpos)[0]);

        if (expectEXT(apply_rct, true))
            pix = transform_sample(pix, sc.slice_rct_coef);
        else
            pix = ivec4(pix[fmt_lut[0]], pix[fmt_lut[1]],
                        pix[fmt_lut[2]], pix[fmt_lut[3]]);

        imageStore(dst[0], pos, pix);
        if (planar_rgb) {
            for (int i = 1; i < color_planes; i++)
                imageStore(dst[i], pos, ivec4(pix[i]));
        }
    }
}
#endif

void decode_slice(in SliceContext sc, uint slice_idx)
{
    int w = sc.slice_dim.x;
    ivec2 sp = sc.slice_pos;

#ifdef RGB
    sp.y = int(gl_WorkGroupID.y)*rgb_linecache;
#endif

#ifndef GOLOMB
    /* PCM coding */
    if (sc.slice_coding_mode == 1) {
#ifdef RGB
        for (int y = 0; y < sc.slice_dim.y; y++) {
            for (int p = 0; p < color_planes; p++)
                decode_line_pcm(sp, w, y, p);

            writeout_rgb(sc, sp, w, y, false);
        }
#else
        for (int p = 0; p < planes; p++) {
            int h = sc.slice_dim.y;
            if (p > 0 && p < 3)
                h = ceil_rshift(h, chroma_shift.y);

            for (int y = 0; y < h; y++)
                decode_line_pcm(sp, w, y, p);
        }
#endif
        return;
    }
#endif

    u8vec4 quant_table_idx = sc.quant_table_idx.xyyz;
    u32vec4 slice_state_off = (slice_idx*codec_planes +
                               uvec4(0, 1, 1, 2))*plane_state_size;

#ifdef GOLOMB
    slice_state_off >>= 3; // division by VLC_STATE_SIZE
    golomb_init();
#endif

#ifdef RGB
    int run_index = 0;
    for (int y = 0; y < sc.slice_dim.y; y++) {
        for (int p = 0; p < color_planes; p++)
            decode_line(sp, w, y, p,
                        slice_state_off[p], quant_table_idx[p], run_index);

        writeout_rgb(sc, sp, w, y, true);
    }
#else
    for (int p = 0; p < planes; p++) {
        int h = sc.slice_dim.y;
        if (p > 0 && p < 3)
            h = ceil_rshift(h, chroma_shift.y);

        int run_index = 0;
        for (int y = 0; y < h; y++)
            decode_line(sp, w, y, p,
                        slice_state_off[p], quant_table_idx[p], run_index);
    }
#endif
}

void main(void)
{
    uint slice_idx = gl_WorkGroupID.y*gl_NumWorkGroups.x + gl_WorkGroupID.x;

    if (gl_LocalInvocationID.x == 0)
        rc = slice_ctx[slice_idx].c;
    barrier();

    decode_slice(slice_ctx[slice_idx], slice_idx);

    if (gl_LocalInvocationID.x == 0) {
        uint overread = 0;
        if (rc.bs_off >= (rc.bs_end + MAX_OVERREAD))
            overread = rc.bs_off - rc.bs_end;
        slice_status[2*slice_idx + 1] = overread;
    }
}
