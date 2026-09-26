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
#ifndef GOLOMB
#define RC_SUBGROUP
#endif
#include "common.glsl"
#include "ffv1_common.glsl"

layout (set = 1, binding = 1, scalar) readonly buffer slice_order_buf {
    uint32_t slice_order[];
};
layout (set = 1, binding = 2, scalar) writeonly buffer slice_status_buf {
    uint32_t slice_status[];
};
layout (set = 1, binding = 4) coherent uniform uimage2D dec[];

#ifdef FLOAT
layout(set = 1, binding = 6) readonly buffer fltmap_buf {
    uint fltmap[][4][65536];
};
#endif

#ifndef GOLOMB

layout (set = 1, binding = 3, scalar) buffer slice_state_buf {
    uint8_t slice_rc_state[];
};

layout (constant_id = 20) const bool quant_ballot = false;

void decode_line_pcm(ivec2 sp, int w, int y, int p)
{
#ifndef RGB
    if (p > 0 && p < 3) {
        w = ceil_rshift(w, chroma_shift.x);
        sp >>= chroma_shift;
    }
#endif

    for (int x = 0; x < w; x++) {
        uint v = 0;

        for (uint i = (rct_offset >> 1); i > 0; i >>= 1)
            v |= get_rac_equi() ? i : 0;

        if (gl_LocalInvocationID.x == 0)
            imageStore(dec[p], sp + LADDR(ivec2(x, y)), uvec4(v));
    }
}

void decode_line(ivec2 sp, int w,
                 int y, int p, int bits, uint state_off,
                 uint8_t quant_table_idx, int run_index, bool ext)
{
#ifndef RGB
    if (p > 0 && p < 3) {
        w = ceil_rshift(w, chroma_shift.x);
        sp >>= chroma_shift;
    }
#endif

    linecache_load(dec[p], sp, y, 0);

    ivec3 top = subgroupBroadcast(get_pred_top(dec[p], sp, ivec2(0, y), 0, w,
                                               quant_table_idx, ext), 0u);
    ivec2 pr = get_pred_left(top, quant_table_idx, ext);
    int c = pr[0];
    int pred = pr[1];
    int sgn = c < 0 ? -1 : 1;
    int tl = top.y;
    int l = linecache[1];
    uint ctx = abs(c);
    uint sbase = state_off + gl_LocalInvocationID.x;
    uint soff = sbase + CONTEXT_SIZE*ctx;
    uint ld = slice_rc_state[soff];
    uint8_t adapted = uint8_t(0);
    bool same = false;
    uint row = 0;
    ivec2 qthr = quant_ballot ? quant_thresh[quant_table_idx][gl_LocalInvocationID.x] : ivec2(0);
    ivec2 qso = quant_ballot ? quant_scale_off[quant_table_idx] : ivec2(0);

    ivec4 tr = get_top(dec[p], sp, ivec2(min(1 + int(gl_LocalInvocationID.x), w - 1), y),
                       0, w, ext);
    for (int x = 0; x < w; x += 32) {
        ivec3 tn = get_pred_top_quant(tr, quant_table_idx, ext);
        tn.z += qso.y;
        tr = get_top(dec[p], sp, ivec2(min(x + 33 + int(gl_LocalInvocationID.x), w - 1), y),
                     0, w, ext);
        int gmin = min(tn.y - tn.x, 0);
        int gmax = max(tn.y - tn.x, 0);
        int n = min(w - x, 32);

        int j = 0;
        do {
            uint st = same ? uint(adapted) : ld;
            int base = subgroupBroadcast(tn.z, j);
            int t = subgroupBroadcast(tn.y, j);

            uint used, used_bits;
            int v = get_isymbol(st, pred, sgn, used, used_bits);
            uint vz = zero_extend(v, bits);
#ifdef FLOAT
            v = int(vz);
#endif

            if (quant_ballot) {
                uvec4 q0 = subgroupBallot(int(int8_t(v - tl)) >= qthr.x);
                uvec4 q3 = subgroupBallot(ext && int(int8_t(l - v)) >= qthr.y);
                c = base + int(subgroupBallotBitCount(q0)) + qso.x*int(subgroupBallotBitCount(q3));
            } else {
                c = base + quant_table[quant_table_idx][0][(v - tl) & MAX_QUANT_TABLE_MASK];
                if (ext)
                    c += quant_table[quant_table_idx][3][(l - v) & MAX_QUANT_TABLE_MASK];
            }
            uint ctx_prev = ctx;
            uint soff_prev = soff;
            ctx = abs(c);
            soff = sbase + CONTEXT_SIZE*ctx;
            same = ctx == ctx_prev;
            if (!same)
                ld = slice_rc_state[soff];
            rac_renorm();
            uint nst = zero_one_state[st + (subgroupInverseBallot(uvec4(used_bits, 0, 0, 0)) ? 256 : 0)];

            adapted = uint8_t(subgroupInverseBallot(uvec4(used, 0, 0, 0)) ? nst : st);
            slice_rc_state[soff_prev] = adapted;
            sgn = c < 0 ? -1 : 1;

            int vm = int(TYPE(vz));
            pred = subgroupBroadcast(clamp(tn.y, vm + gmin, vm + gmax), j);
            row = gl_LocalInvocationID.x == j ? vz : row;
            rac_check_window();

            l = v;
            tl = t;
        } while (++j < n);

        if (gl_LocalInvocationID.x < n)
            imageStore(dec[p], sp + LADDR(ivec2(x + int(gl_LocalInvocationID.x), y)), uvec4(row));
    }

    memoryBarrierImage();
    barrier();
}

#else /* GOLOMB */

layout (set = 1, binding = 3, scalar) buffer slice_state_buf {
    VlcState slice_vlc_state[];
};

GetBitContext gb;

void golomb_init(void)
{
    if (version == 3 && micro_version > 1 || version > 3)
        get_rac_internal((rc.range * 129) >> 8);

    uint64_t ac_byte_count = rc.bs_off - rc.bs_start - 1;
    init_get_bits(gb, u8buf(slice_data + rc.bs_start + ac_byte_count),
                  int(rc.bs_end - rc.bs_start - ac_byte_count));
}

void decode_line(ivec2 sp, int w,
                 int y, int p, int bits, uint state_off,
                 uint8_t quant_table_idx, inout int run_index, bool ext)
{
#ifndef RGB
    if (p > 0 && p < 3) {
        w = ceil_rshift(w, chroma_shift.x);
        sp >>= chroma_shift;
    }
#endif

    linecache_load(dec[p], sp, y, 0);

    int run_count = 0;
    int run_mode  = 0;

    for (int x = 0; x < w; x++) {
        ivec2 pos = sp + ivec2(x, y);
        int diff;
        ivec2 pr = get_pred(dec[p], sp, ivec2(x, y), 0, w,
                            quant_table_idx, ext);

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
        linecache_next(TYPE(v));
    }
}
#endif

void decode_plane_line(ivec2 sp, int w, int y, int p, int bits, uint state_off,
                       uint8_t quant_table_idx, inout int run_index)
{
    if (has_extend_lookup && extend_lookup[quant_table_idx])
        decode_line(sp, w, y, p, bits, state_off, quant_table_idx, run_index, true);
    else
        decode_line(sp, w, y, p, bits, state_off, quant_table_idx, run_index, false);
}

#ifdef BAYER
void writeout_bayer(uint slice_idx, in SliceContext sc, ivec2 sp, int w, int y)
{
    memoryBarrierImage();
    barrier();

    int offset = rct_offset;

    for (uint x = gl_LocalInvocationID.x; x < w; x += gl_WorkGroupSize.x) {
        ivec2 lpos = sp + LADDR(ivec2(x, y));
        ivec2 pos  = sc.slice_pos + ivec2(int(x) << 1, y << 1);

        int g_r = int(imageLoad(dec[0], lpos)[0]);
        int g_b = int(imageLoad(dec[1], lpos)[0]);
        int b   = int(imageLoad(dec[2], lpos)[0]);
        int r   = int(imageLoad(dec[3], lpos)[0]);

        if (sc.slice_coding_mode != 1) {
            b -= offset;
            r -= offset;
            g_r -= (b*sc.slice_rct_coef.g + r*sc.slice_rct_coef.r) >> 2;
            b += g_r;
            r += g_r;

            int gd = g_b - offset;
            g_b = g_r - (gd >> 1);
            g_r = g_b + gd;
        }

        imageStore(dst[0], pos + ivec2(0, 0), uvec4(r));
        imageStore(dst[0], pos + ivec2(1, 0), uvec4(g_r));
        imageStore(dst[0], pos + ivec2(0, 1), uvec4(g_b));
        imageStore(dst[0], pos + ivec2(1, 1), uvec4(b));
    }
}
#endif

#ifdef RGB
ivec4 transform_sample(ivec4 pix, ivec2 rct_coef, int offset)
{
    pix.b -= offset;
    pix.r -= offset;
    pix.g -= (pix.b*rct_coef.g + pix.r*rct_coef.r) >> 2;
    pix.b += pix.g;
    pix.r += pix.g;
    return pix;
}

void writeout_rgb(uint slice_idx, in SliceContext sc, ivec2 sp, int w, int y,
                  bool apply_rct)
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

        if (apply_rct)
#ifdef FLOAT
            pix = transform_sample(pix, sc.slice_rct_coef, sc.remap_count[0]);
#else
            pix = transform_sample(pix, sc.slice_rct_coef, rct_offset);
#endif

#ifdef FLOAT
        pix = pix.gbra;
        vec4 pd;
        for (int i = 0; i < color_planes; i++) {
            uint mask = (1u << ceil_log2(sc.remap_count[i])) - 1u;
            uint v = fltmap[slice_idx][i][uint(pix[i]) & mask];
            if (c_bits >= 32)
                pd[i] = uintBitsToFloat(v);
            else
                pd[i] = float(uint16BitsToFloat16(uint16_t(v)));
        }
        pd = pd.brga;

        pd = vec4(pd[fmt_lut[0]], pd[fmt_lut[1]],
                  pd[fmt_lut[2]], pd[fmt_lut[3]]);
#define CAST(x) vec4(x)
#else
#define CAST(x) ivec4(x)
        ivec4 pd = ivec4(pix[fmt_lut[0]], pix[fmt_lut[1]],
                         pix[fmt_lut[2]], pix[fmt_lut[3]]);
#endif

        imageStore(dst[0], pos, pd);
        if (planar_rgb) {
            for (int i = 1; i < color_planes; i++)
                imageStore(dst[i], pos, CAST(pd[i]));
        }
    }
}
#endif

void decode_slice(in SliceContext sc, uint slice_idx)
{
    int w = sc.slice_dim.x;
    ivec2 sp = sc.slice_pos;
    u16vec4 bits = get_slice_bits(sc);

#ifdef BAYER
    /* Bayer logical dims: 2x2 blocks at half resolution */
    w >>= 1;
    int bayer_h = sc.slice_dim.y >> 1;
    sp.x >>= 1;
    sp.y = int(slice_idx / gl_NumWorkGroups.x)*rgb_linecache;
    /* c_bits = bps + 1 (the +1 is for is_rgb). For PCM mode, all planes use
     * raw bps. For non-PCM, gm uses bps (bps+1 before 4.8, which coded an
     * extra bit); gd/b-gm/r-gm use bps+1. */
    if (sc.slice_coding_mode == 0) {
        bits = u16vec4(c_bits - 1, c_bits, c_bits, c_bits);
        if (version == 4 && micro_version < 8)
            bits[0] = uint16_t(c_bits);
    } else
        bits = u16vec4(c_bits - 1, c_bits - 1, c_bits - 1, c_bits - 1);
#elif defined(RGB)
    sp.y = int(slice_idx / gl_NumWorkGroups.x)*rgb_linecache;
#endif

#ifndef GOLOMB
    /* PCM coding */
    if (sc.slice_coding_mode == 1) {
#ifdef BAYER
        for (int y = 0; y < bayer_h; y++) {
            for (int p = 0; p < 4; p++)
                decode_line_pcm(sp, w, y, p);
            writeout_bayer(slice_idx, sc, sp, w, y);
        }
#elif defined(RGB)
        for (int y = 0; y < sc.slice_dim.y; y++) {
            for (int p = 0; p < color_planes; p++)
                decode_line_pcm(sp, w, y, p);

            writeout_rgb(slice_idx, sc, sp, w, y, false);
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

#ifdef BAYER
    u8vec4 quant_table_idx = sc.quant_table_idx.xzyy;
    u32vec4 slice_state_off = (slice_idx*codec_planes +
                               uvec4(0, 2, 1, 1))*plane_state_size;
#else
    u8vec4 quant_table_idx = sc.quant_table_idx.xyyz;
    u32vec4 slice_state_off = (slice_idx*codec_planes +
                               uvec4(0, 1, 1, 2))*plane_state_size;
#endif

#ifdef GOLOMB
    slice_state_off >>= 3; // division by VLC_STATE_SIZE
    golomb_init();
#endif

#ifdef BAYER
    int run_index = 0;
    for (int y = 0; y < bayer_h; y++) {
        for (int p = 0; p < 4; p++)
            decode_plane_line(sp, w, y, p, bits[p], slice_state_off[p],
                              quant_table_idx[p], run_index);

        writeout_bayer(slice_idx, sc, sp, w, y);
    }
#elif defined(RGB)
    int run_index = 0;
    for (int y = 0; y < sc.slice_dim.y; y++) {
        for (int p = 0; p < color_planes; p++)
            decode_plane_line(sp, w, y, p, bits[p], slice_state_off[p],
                              quant_table_idx[p], run_index);

        writeout_rgb(slice_idx, sc, sp, w, y, true);
    }
#else
    for (int p = 0; p < planes; p++) {
        int h = sc.slice_dim.y;
        if (p > 0 && p < 3)
            h = ceil_rshift(h, chroma_shift.y);

        int run_index = 0;
        for (int y = 0; y < h; y++)
            decode_plane_line(sp, w, y, p, bits[p], slice_state_off[p],
                              quant_table_idx[p], run_index);
    }
#endif
}

void main(void)
{
    uint slice_idx = slice_order[gl_WorkGroupID.y*gl_NumWorkGroups.x + gl_WorkGroupID.x];

#ifdef GOLOMB
    rc = slice_ctx[slice_idx].c;
#else
    rac_init_dec(slice_ctx[slice_idx].c);
#endif

    decode_slice(slice_ctx[slice_idx], slice_idx);

    if (gl_LocalInvocationID.x == 0) {
#ifndef GOLOMB
        rc.bs_off += rc_pos;
#endif
        uint overread = 0;
        if (rc.bs_off >= (rc.bs_end + MAX_OVERREAD))
            overread = rc.bs_off - rc.bs_end;
        slice_status[2*slice_idx + 1] = overread;
    }
}
