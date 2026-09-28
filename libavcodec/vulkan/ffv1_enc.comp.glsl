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

#define ENCODE
#ifndef GOLOMB
#define RC_SUBGROUP
#endif
/* Golomb slices start writing at rac_terminate()'s byte count, with no
 * alignment guarantee */
#define PB_UNALIGNED
#include "common.glsl"
#include "ffv1_common.glsl"
#extension GL_KHR_shader_subgroup_arithmetic : require

layout (set = 0, binding = 2, scalar) uniform crc_ieee_buf {
    uint32_t crc_ieee[256];
};

layout (set = 1, binding = 1, scalar) writeonly buffer slice_results_buf {
    uint32_t slice_results[];
};
/* Source images are bound as UINT (raw bits) regardless of the underlying
 * pixel format. Integer formats are passed through unchanged; for float
 * formats this avoids the fp16/fp32 conversion that would otherwise flush
 * denormals before we get to look at them. */
layout (set = 1, binding = 3) uniform uimage2D src[];
layout (set = 1, binding = 6, scalar) readonly buffer slice_order_buf {
    uint32_t slice_order[];
};
#ifdef FLOAT
layout (set = 1, binding = 5, scalar) readonly buffer fltmap_buf {
    uint fltmap[];
};
#endif

#ifndef GOLOMB

layout (set = 1, binding = 2, scalar) buffer slice_state_buf {
    uint8_t slice_rc_state[];
};

layout (constant_id = 19) const bool enc_ext = false;
shared uint crc_tab[has_crc ? 256 : 1];

void encode_line_pcm(in SliceContext sc, readonly uimage2D img,
                     ivec2 sp, int y, uint p, uint comp)
{
    int w = sc.slice_dim.x;
#ifdef BAYER
    w >>= 1;
#endif

#ifndef RGB
    if (p > 0 && p < 3) {
        w = ceil_rshift(w, chroma_shift.x);
        sp >>= chroma_shift;
    }
#endif

    for (int x = 0; x < w; x++) {
        uint v = subgroupBroadcastFirst(imageLoad(img, sp + LADDR(ivec2(x, y)))[comp]);

        for (uint i = (rct_offset >> 1); i > 0; i >>= 1)
            put_rac_equi(bool(v & i));
        if (rc_nev > 32)
            rac_emit();
    }
}

void encode_line(in SliceContext sc, readonly uimage2D img, uint state_off,
                 ivec2 sp, int y, uint p, uint comp, int bits,
                 uint8_t quant_table_idx, in int run_index)
{
    int w = sc.slice_dim.x;
#ifdef BAYER
    w >>= 1;
#endif

#ifndef RGB
    if (p > 0 && p < 3) {
        w = ceil_rshift(w, chroma_shift.x);
        sp >>= chroma_shift;
    }
#elif defined(FLOAT)
    if (bits == 0)
        return;
#endif

    int lane = int(gl_SubgroupInvocationID);
    uint last_off = ~0u;
    uint adapted[RC_K];
    [[unroll]] for (uint k = 0; k < RC_K; k++)
        adapted[k] = 0;

    ivec2 pc = ivec2(min(lane, w - 1), y);
    uvec4 rcur, rT, rTR, rL, rTL, rLL = uvec4(0), rTT = uvec4(0);
#ifdef RGB
    rcur = imageLoad(img, sp + LADDR(pc));
    rT   = imageLoad(img, sp + LADDR(pc + ivec2(0, -1)));
    rTR  = imageLoad(img, sp + LADDR(ivec2(min(pc.x + 1, w - 1), y - 1)));
    rL   = imageLoad(img, sp + LADDR(pc.x > 0 ? pc + ivec2(-1, 0) : ivec2(0, y - 1)));
    rTL  = imageLoad(img, sp + LADDR(pc.x > 0 ? pc + ivec2(-1, -1) : ivec2(0, y - 2)));
    if (enc_ext) {
        rLL = pc.x > 0 ? imageLoad(img, sp + LADDR(pc.x > 1 ? pc + ivec2(-2, 0) : ivec2(0, y - 1))) : uvec4(0);
        rTT = imageLoad(img, sp + LADDR(pc + ivec2(0, -2)));
    }
#else
    int y1 = max(y - 1, 0);
    int y2 = max(y - 2, 0);
    rcur = imageLoad(img, sp + pc);
    rT   = imageLoad(img, sp + ivec2(pc.x, y1));
    rTR  = imageLoad(img, sp + ivec2(min(pc.x + 1, w - 1), y1));
    rL   = imageLoad(img, sp + (pc.x > 0 ? pc + ivec2(-1, 0) : ivec2(0, y1)));
    rTL  = imageLoad(img, sp + (pc.x > 0 ? ivec2(pc.x - 1, y1) : ivec2(0, y2)));
    if (enc_ext) {
        rLL = pc.x > 0 && (pc.x > 1 || y > 0) ?
              imageLoad(img, sp + (pc.x > 1 ? pc + ivec2(-2, 0) : ivec2(0, y1))) : uvec4(0);
        rTT = y > 1 ? imageLoad(img, sp + ivec2(pc.x, y2)) : uvec4(0);
    }
#endif

    for (int x = 0; x < w; x += int(RC_LANES)) {
        int px = min(x + lane, w - 1);
        int cur = int(TYPE(rcur[comp]));
        int T   = int(TYPE(rT[comp]));
        int TR  = int(TYPE(rTR[comp]));
        int L   = int(TYPE(rL[comp]));
        int TL  = int(TYPE(rTL[comp]));
        int LL  = int(TYPE(rLL[comp]));
        int TT  = int(TYPE(rTT[comp]));
#ifndef RGB
        if (y < 1) {
            T = 0;
            TR = 0;
            TL = 0;
            L = px > 0 ? L : 0;
        } else if (y < 2 && px == 0) {
            TL = 0;
        }
#endif

        int c = quant_table[quant_table_idx][0][(L - TL) & MAX_QUANT_TABLE_MASK] +
                quant_table[quant_table_idx][1][(TL - T) & MAX_QUANT_TABLE_MASK] +
                quant_table[quant_table_idx][2][(T - TR) & MAX_QUANT_TABLE_MASK];
        if (enc_ext)
            c += quant_table[quant_table_idx][3][(LL - L) & MAX_QUANT_TABLE_MASK] +
                 quant_table[quant_table_idx][4][(TT - T) & MAX_QUANT_TABLE_MASK];

        int d = cur - predict(L, ivec2(TL, T));
        if (c < 0) {
            c = -c;
            d = -d;
        }
        d = fold(d, bits);
        uint soff = state_off + CONTEXT_SIZE*uint(c);

        uint ad = abs(d);
        int e = findMSB(ad);
        int ec = clamp(e, 0, 9);
        int es = 11 + min(e, 10);
        uint used = d == 0 ? 1u : 1u | ((4u << ec) - 2u) | (((1u << ec) - 1u) << 22) | (1u << es);
        uint ones = d == 0 ? 1u : ((2u << ec) - 2u) | ((ad & ((1u << ec) - 1u)) << 22) |
                                  (uint(d < 0) << es);

        int n = min(w - x, int(RC_LANES));
        uint so_n = subgroupBroadcast(soff, 0);
        int v_n = subgroupBroadcast(d, 0);
        uint used_n = subgroupBroadcast(used, 0);
        uint ones_n = subgroupBroadcast(ones, 0);
        RCStates st;
        [[unroll]] for (uint k = 0; k < RC_K; k++) {
            st.v[k] = uint(slice_rc_state[so_n + lane + k*RC_LANES]);
            st.v[k] = so_n == last_off ? adapted[k] : st.v[k];
        }

        if (x + int(RC_LANES) < w) {
            pc = ivec2(min(x + int(RC_LANES) + lane, w - 1), y);
#ifdef RGB
            rcur = imageLoad(img, sp + LADDR(pc));
            rT   = imageLoad(img, sp + LADDR(pc + ivec2(0, -1)));
            rTR  = imageLoad(img, sp + LADDR(ivec2(min(pc.x + 1, w - 1), y - 1)));
            rL   = imageLoad(img, sp + LADDR(pc + ivec2(-1, 0)));
            rTL  = imageLoad(img, sp + LADDR(pc + ivec2(-1, -1)));
            if (enc_ext) {
                rLL = imageLoad(img, sp + LADDR(RC_LANES > 1 || pc.x > 1 ? pc + ivec2(-2, 0) :
                                                                       ivec2(0, y - 1)));
                rTT = imageLoad(img, sp + LADDR(pc + ivec2(0, -2)));
            }
#else
            rcur = imageLoad(img, sp + pc);
            rT   = imageLoad(img, sp + ivec2(pc.x, y1));
            rTR  = imageLoad(img, sp + ivec2(min(pc.x + 1, w - 1), y1));
            rL   = imageLoad(img, sp + pc + ivec2(-1, 0));
            rTL  = imageLoad(img, sp + ivec2(pc.x - 1, y1));
            if (enc_ext) {
                rLL = RC_LANES > 1 || pc.x > 1 ? imageLoad(img, sp + pc + ivec2(-2, 0)) :
                      y > 0 ? imageLoad(img, sp + ivec2(0, y1)) : uvec4(0);
                rTT = y > 1 ? imageLoad(img, sp + ivec2(pc.x, y2)) : uvec4(0);
            }
#endif
        }

        int j = 0;
        while (true) {
            do {
                uint so = so_n;
                int v = v_n;
                uint used_j = used_n;
                uint ones_j = ones_n;
                uint nst[RC_K];
                [[unroll]] for (uint k = 0; k < RC_K; k++) {
                    uint one = bitfieldExtract(ones_j, lane + int(k*RC_LANES), 1);
                    nst[k] = zero_one_state[(one << 8) + st.v[k]];
                }

                int jn = min(j + 1, n - 1);
                so_n = subgroupBroadcast(soff, jn);
                v_n = subgroupBroadcast(d, jn);
                used_n = subgroupBroadcast(used, jn);
                ones_n = subgroupBroadcast(ones, jn);
                uint ld[RC_K];
                [[unroll]] for (uint k = 0; k < RC_K; k++)
                    ld[k] = uint(slice_rc_state[so_n + lane + k*RC_LANES]);

                uint s10, s31;
                put_isymbol(st, v, s10, s31);

                [[unroll]] for (uint k = 0; k < RC_K; k++) {
                    int l = lane + int(k*RC_LANES);
                    adapted[k] = bitfieldExtract(used_j, l, 1) != 0 ? nst[k] : st.v[k];
                    if (abs(v) >= 1024)
                        adapted[k] = l == 10 ? s10 : l == 31 ? s31 : adapted[k];

                    slice_rc_state[so + uint(l)] = uint8_t(adapted[k]);
                    st.v[k] = so_n == so ? adapted[k] : ld[k];
                }
                last_off = so;
            } while (++j < n && rc_nev <= 42);
            if (rc_nev > 42)
                rac_emit();
            if (j >= n)
                break;
        }
    }
}

#else /* GOLOMB */

layout (set = 1, binding = 2, scalar) buffer slice_state_buf {
    VlcState slice_vlc_state[];
};

uint hdr_len = 0;
PutBitContext pb;

void init_golomb(void)
{
    hdr_len = rac_terminate();
    init_put_bits(pb, OFFBUF(u8buf, slice_data, rc.bs_start + hdr_len),
                  slice_size_max - hdr_len);
}

void encode_line(in SliceContext sc, readonly uimage2D img, uint state_off,
                 ivec2 sp, int y, uint p, uint comp, int bits,
                 uint8_t quant_table_idx, inout int run_index)
{
    int w = sc.slice_dim.x;
#ifdef BAYER
    w >>= 1;
#endif

#ifndef RGB
    if (p > 0 && p < 3) {
        w = ceil_rshift(w, chroma_shift.x);
        sp >>= chroma_shift;
    }
#elif defined(FLOAT)
    if (bits == 0)
        return;
#endif

    linecache_load(img, sp, y, comp);

    int run_count = 0;
    bool run_mode = false;

    for (int x = 0; x < w; x++) {
        ivec2 d = get_pred(img, sp, ivec2(x, y), comp, w,
                           quant_table_idx, extend_lookup[quant_table_idx]);
        TYPE cur = TYPE(imageLoad(img, sp + LADDR(ivec2(x, y)))[comp]);
        d[1] = int(cur) - d[1];
        linecache_next(cur);

        if (d[0] < 0)
            d = -d;

        d[1] = fold(d[1], bits);

        if (d[0] == 0)
            run_mode = true;

        if (run_mode) {
            if (d[1] != 0) {
                /* A very unlikely loop */
                while (run_count >= 1 << log2_run[run_index]) {
                    run_count -= 1 << log2_run[run_index];
                    run_index++;
                    put_bits(pb, 1, 1);
                }

                put_bits(pb, 1 + log2_run[run_index], run_count);
                if (run_index != 0)
                    run_index--;
                run_count = 0;
                run_mode  = false;
                if (d[1] > 0)
                    d[1]--;
            } else {
                run_count++;
            }
        }

        if (!run_mode) {
            Symbol sym = get_vlc_symbol(slice_vlc_state[state_off + d[0]],
                                        d[1], bits);
            put_bits(pb, sym.bits, sym.val);
        }
    }

    if (run_mode) {
        while (run_count >= (1 << log2_run[run_index])) {
            run_count -= 1 << log2_run[run_index];
            run_index++;
            put_bits(pb, 1, 1);
        }

        if (run_count > 0)
            put_bits(pb, 1, 1);
    }
}
#endif

#ifdef RGB
const uvec4 rgb_plane_order = { 1, 2, 0, 3 };

ivec4 load_components(uint slice_idx, in SliceContext sc, ivec2 pos)
{
    ivec4 pix;
#ifdef FLOAT
    if (c_bits >= 32) {
        /* 32-bit float: per-pixel-position bitmap lookup. The bitmaps of
         * the planes follow the units of the planes in the same buffer. */
        ivec2 rel = pos - sc.slice_pos;
        uint pixel_idx = uint(rel.x + sc.slice_dim.x*rel.y);
        uint base = (slice_idx*12u + 8u)*max_pixels_per_slice;
        for (int i = 0; i < color_planes; i++)
            pix[i] = int(fltmap[base + uint(i)*max_pixels_per_slice + pixel_idx]);
    } else {
        /* 16-bit float: value-indexed lookup. Source view is r16_uint so
         * imageLoad returns the raw fp16 bit pattern in .x. */
        for (int i = 0; i < color_planes; i++) {
            uint iv = imageLoad(src[i], pos)[0] & 0xFFFFu;
            pix[i] = int(fltmap[(slice_idx*4u + uint(i))*65536u + iv]);
        }
    }
#else
    pix = ivec4(imageLoad(src[0], pos));
    if (planar_rgb)
        for (int i = 1; i < (3 + int(transparency)); i++)
            pix[i] = int(imageLoad(src[i], pos)[0]);
#endif

    return ivec4(pix[fmt_lut[0]], pix[fmt_lut[1]],
                 pix[fmt_lut[2]], pix[fmt_lut[3]]);
}

void transform_sample(inout ivec4 pix, ivec2 rct_coef, int offset)
{
    pix.b -= pix.g;
    pix.r -= pix.g;
    pix.g += (pix.b*rct_coef.g + pix.r*rct_coef.r) >> 2;
    pix.b += offset;
    pix.r += offset;
}

void preload_rgb(uint slice_idx, in SliceContext sc, ivec2 sp, int w, int y,
                 bool apply_rct)
{
    for (uint x0 = 0; x0 < w; x0 += 4*gl_WorkGroupSize.x) {
        ivec4 pix[4];
        [[unroll]] for (uint k = 0; k < 4; k++) {
            uint x = min(x0 + k*gl_WorkGroupSize.x + gl_LocalInvocationID.x, w - 1);
            pix[k] = load_components(slice_idx, sc, sc.slice_pos + ivec2(x, y));
        }

        [[unroll]] for (uint k = 0; k < 4; k++) {
            uint x = x0 + k*gl_WorkGroupSize.x + gl_LocalInvocationID.x;
            if (x < w) {
#ifdef FLOAT
                if (apply_rct)
                    transform_sample(pix[k], sc.slice_rct_coef, sc.remap_count[0]);
#else
                if (apply_rct)
                    transform_sample(pix[k], sc.slice_rct_coef, rct_offset);
#endif
                imageStore(tmp, sp + LADDR(ivec2(x, y)), pix[k]);
            }
        }
    }

    memoryBarrierImage();
    barrier();
}

#ifdef BAYER
void preload_bayer(in SliceContext sc, ivec2 sp, int w, int y, bool apply_rct)
{
    int offset = rct_offset;

    for (uint x = gl_LocalInvocationID.x; x < w; x += gl_WorkGroupSize.x) {
        ivec2 lpos = sp + LADDR(ivec2(x, y));
        ivec2 src_pos = sc.slice_pos + ivec2(int(x) << 1, y << 1);

        int r  = int(imageLoad(src[0], src_pos + ivec2(0, 0))[0]);
        int gr = int(imageLoad(src[0], src_pos + ivec2(1, 0))[0]);
        int gb = int(imageLoad(src[0], src_pos + ivec2(0, 1))[0]);
        int b  = int(imageLoad(src[0], src_pos + ivec2(1, 1))[0]);

        if (apply_rct) {
            int gd = gr - gb;
            int gm = gb + (gd >> 1);
            b -= gm;
            r -= gm;
            gm += (b*sc.slice_rct_coef.g + r*sc.slice_rct_coef.r) >> 2;
            b += offset;
            r += offset;
            gd += offset;
            gr = gm;
            gb = gd;
        }

        imageStore(tmp, lpos, ivec4(gr, gb, b, r));
    }

    memoryBarrierImage();
    barrier();
}
#endif
#endif

void encode_slice(in SliceContext sc, uint slice_idx)
{
    ivec2 sp = sc.slice_pos;
    u16vec4 bits = get_slice_bits(sc);

#ifdef BAYER
    int bayer_w = sc.slice_dim.x >> 1;
    int bayer_h = sc.slice_dim.y >> 1;
    sp.x >>= 1;
    sp.y = int(slice_idx / gl_NumWorkGroups.x)*rgb_linecache;
    /* c_bits = bps + 1 for is_rgb pixfmts (Bayer is treated as RGB). gm uses
     * raw bps; gd/b-gm/r-gm need an extra bit for the RCT difference. PCM
     * stores raw samples so all planes use bps. */
    if (sc.slice_coding_mode == 0)
        bits = u16vec4(c_bits - 1, c_bits, c_bits, c_bits);
    else
        bits = u16vec4(c_bits - 1, c_bits - 1, c_bits - 1, c_bits - 1);
#elif defined(RGB)
    sp.y = int(slice_idx / gl_NumWorkGroups.x)*rgb_linecache;
#endif

#ifndef GOLOMB
    if (force_pcm) {
#ifdef BAYER
        for (int y = 0; y < bayer_h; y++) {
            preload_bayer(sc, sp, bayer_w, y, false);

            for (uint c = 0; c < 4; c++)
                encode_line_pcm(sc, tmp, sp, y, 0, c);
        }
#elif defined(RGB)
        for (int y = 0; y < sc.slice_dim.y; y++) {
            preload_rgb(slice_idx, sc, sp, sc.slice_dim.x, y, false);

            for (uint c = 0; c < color_planes; c++)
                encode_line_pcm(sc, tmp, sp, y, 0, rgb_plane_order[c]);
        }
#else
        for (int c = 0; c < color_planes; c++) {

            int h = sc.slice_dim.y;
            if (c > 0 && c < 3)
                h = ceil_rshift(h, chroma_shift.y);

            /* Takes into account dual-plane YUV formats */
            int p = min(c, planes - 1);
            int comp = c - p;

            for (int y = 0; y < h; y++)
                encode_line_pcm(sc, src[p], sp, y, p, comp);
        }
#endif
        return;
    }
#endif

#ifdef BAYER
    u32vec4 slice_state_off = (slice_idx*codec_planes +
                               uvec4(0, 2, 1, 1))*plane_state_size;
#else
    u32vec4 slice_state_off = (slice_idx*codec_planes +
                               uvec4(0, 1, 1, 2))*plane_state_size;
#endif

#ifdef GOLOMB
    slice_state_off >>= 3;
    init_golomb();
#endif

#ifdef BAYER
    int run_index = 0;
    for (int y = 0; y < bayer_h; y++) {
        preload_bayer(sc, sp, bayer_w, y, true);

        for (uint c = 0; c < 4; c++)
            encode_line(sc, tmp, slice_state_off[c],
                        sp, y, 0, c, bits[c],
                        U8(context_model), run_index);
    }
#elif defined(RGB)
    int run_index = 0;
    for (int y = 0; y < sc.slice_dim.y; y++) {
        preload_rgb(slice_idx, sc, sp, sc.slice_dim.x, y, true);

        for (uint c = 0; c < color_planes; c++)
            encode_line(sc, tmp, slice_state_off[c],
                        sp, y, 0, rgb_plane_order[c], bits[c],
                        U8(context_model), run_index);
    }
#else
    for (uint c = 0; c < color_planes; c++) {
        int run_index = 0;

        int h = sc.slice_dim.y;
        if (c > 0 && c < 3)
            h = ceil_rshift(h, chroma_shift.y);

        uint p = min(c, planes - 1);
        uint comp = c - p;

        for (int y = 0; y < h; y++)
            encode_line(sc, src[p], slice_state_off[c], sp, y, p,
                        comp, bits[c], U8(context_model), run_index);
    }
#endif
}

void finalize_slice(in uint slice_idx)
{
#ifdef GOLOMB
    uint32_t enc_len = hdr_len + flush_put_bits(pb);

    u8buf bs = u8buf(slice_data + rc.bs_start);

    /* Append slice length */
    u8vec4 enc_len_p = unpack8(enc_len);
    bs[enc_len + 0].v = enc_len_p.z;
    bs[enc_len + 1].v = enc_len_p.y;
    bs[enc_len + 2].v = enc_len_p.x;
    enc_len += 3;

    /* Calculate and write CRC */
    if (has_crc) {
        bs[enc_len].v = uint8_t(0);
        enc_len++;

        uint32_t crc = crcref;
        for (int i = 0; i < enc_len; i++)
            crc = crc_ieee[(crc & 0xFF) ^ uint32_t(bs[i].v)] ^ (crc >> 8);

        if (crcref != 0x00000000)
            crc ^= 0x8CD88196;

        u8vec4 crc_p = unpack8(crc);
        bs[enc_len + 0].v = crc_p.x;
        bs[enc_len + 1].v = crc_p.y;
        bs[enc_len + 2].v = crc_p.z;
        bs[enc_len + 3].v = crc_p.w;
        enc_len += 4;
    }

    slice_results[slice_idx] = enc_len;
#else
    uint enc_len = rac_terminate();
    uint lane = gl_SubgroupInvocationID;
    u8buf bs = u8buf(slice_data + rc.bs_start);

    for (uint i = lane; i < 3 + uint(has_crc); i += RC_LANES)
        bs[enc_len + i].v = uint8_t(i < 3 ? enc_len >> (16 - 8*i) : 0);
    enc_len += 3 + uint(has_crc);

    if (has_crc) {
        controlBarrier(gl_ScopeWorkgroup, gl_ScopeWorkgroup,
                       gl_StorageSemanticsBuffer, gl_SemanticsAcquireRelease);

        uint seg = enc_len / RC_LANES;
        uint len0 = enc_len - (RC_LANES - 1)*seg;
        uint start = lane == 0 ? 0 : len0 + (lane - 1)*seg;
        uint len = lane == 0 ? len0 : seg;
        uint crc = lane == 0 ? crcref : 0;
        uint z[RC_K];
        [[unroll]] for (uint c = 0; c < RC_K; c++)
            z[c] = 1u << (lane + c*RC_LANES);
        for (uint i = 0; i < len0; i += 8) {
            uint b[8];
            [[unroll]] for (uint k = 0; k < 8; k++)
                b[k] = i + k < len ? uint(bs[start + i + k].v) : 0;
            [[unroll]] for (uint k = 0; k < 8; k++) {
                if (i + k < len)
                    crc = crc_tab[(crc ^ b[k]) & 0xFF] ^ (crc >> 8);
                if (i + k < seg)
                    [[unroll]] for (uint c = 0; c < RC_K; c++)
                        z[c] = crc_tab[z[c] & 0xFF] ^ (z[c] >> 8);
            }
        }

        uint acc = subgroupBroadcast(crc, 0);
        for (uint i = 1; i < RC_LANES; i++) {
            uint m = 0;
            [[unroll]] for (uint c = 0; c < RC_K; c++)
                m ^= bitfieldExtract(acc, int(lane + c*RC_LANES), 1) != 0 ? z[c] : 0;
            acc = subgroupXor(m) ^ subgroupBroadcast(crc, i);
        }
        if (crcref != 0x00000000)
            acc ^= 0x8CD88196;

        for (uint i = lane; i < 4; i += RC_LANES)
            bs[enc_len + i].v = uint8_t(acc >> (8*i));
        enc_len += 4;
    }

    if (lane == 0)
        slice_results[slice_idx] = enc_len;
#endif
}

void main(void)
{
    uint slice_idx = slice_order[gl_WorkGroupID.y*gl_NumWorkGroups.x + gl_WorkGroupID.x];

#ifdef GOLOMB
    if (gl_LocalInvocationID.x == 0)
        rc = slice_ctx[slice_idx].c;
    barrier();
#else
    if (has_crc)
        for (uint i = gl_LocalInvocationID.x; i < 256; i += gl_WorkGroupSize.x)
            crc_tab[i] = crc_ieee[i];
    rac_init_enc(slice_ctx[slice_idx].c);
#endif

    encode_slice(slice_ctx[slice_idx], slice_idx);

#ifdef GOLOMB
    if (gl_LocalInvocationID.x == 0)
#endif
        finalize_slice(slice_idx);
}
