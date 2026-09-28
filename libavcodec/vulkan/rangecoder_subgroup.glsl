/*
 * FFv1 codec
 *
 * Copyright (c) 2026 Lynne <dev@lynne.ee>
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

#ifndef VULKAN_RANGECODER_SUBGROUP_H
#define VULKAN_RANGECODER_SUBGROUP_H

#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_KHR_shader_subgroup_shuffle : require
#extension GL_KHR_shader_subgroup_shuffle_relative : require

#define CONTEXT_SIZE 32
#define MAX_OVERREAD 2

#define RC_LANES gl_WorkGroupSize.x
#define RC_K (CONTEXT_SIZE/RC_LANES)

struct RCStates {
    uint v[RC_K];
};

#ifdef DECODE
#define RC_BTYPE readonly buffer
#else
#define RC_BTYPE uniform
#endif

layout (set = 0, binding = 0, scalar) RC_BTYPE rangecoder_buf {
    uint8_t rangecoder_state[512];
};

shared uint8_t zero_one_state[512];

struct RangeCoder {
    uint     bs_start;
    uint     bs_off;
    uint     bs_end;
    uint     low;
    uint     range;
    uint16_t outstanding_count;
    int16_t  outstanding_byte;
};

RangeCoder rc;

uint rac_range1(uint range, uint state24)
{
    uint hi, lo;
    umulExtended(range, state24, hi, lo);
    return hi;
}

#ifdef ENCODE
uint rc_top;
uint rc_oc;
int rc_ob;
uint rc_nev;
uint rc_ev[64/RC_LANES];

void rac_init_enc(in RangeCoder c)
{
    for (uint i = gl_SubgroupInvocationID; i < 512; i += RC_LANES)
        zero_one_state[i] = rangecoder_state[i];
    barrier();

    rc = c;
    rc_top = rc.low + rc.range;
    rc_oc = uint(rc.outstanding_count);
    rc_ob = int(rc.outstanding_byte);
    rc_nev = 0;
    [[unroll]] for (uint k = 0; k < 64/RC_LANES; k++)
        rc_ev[k] = 0;
}

void rac_emit_block(uint low, uint cnt)
{
    uint lane = gl_SubgroupInvocationID;
    bool deferred = lane < cnt && low - 0xFF01u < 0xFFu;
    if (rc_ob >= 0 && rc_oc == 0 && subgroupBallot(deferred).x == 0) {
        uint digit = (low >> 8) & 0xFFu;
        uint prev = subgroupShuffleUp(digit, 1);
        uint b = ((lane == 0 ? uint(rc_ob) : prev) + (low >> 16)) & 0xFFu;
        if (lane < cnt)
            slice_data[rc.bs_off + lane].v = uint8_t(b);
        rc.bs_off += cnt;
        rc_ob = int(subgroupBroadcast(digit, cnt - 1));
    } else {
        for (uint i = 0; i < cnt; i++) {
            uint l = subgroupBroadcast(low, i);
            if (rc_ob < 0) {
                rc_ob = int(l >> 8);
            } else if (l - 0xFF01u < 0xFFu) {
                rc_oc++;
            } else {
                uint carry = l >> 16;
                if (lane == 0)
                    slice_data[rc.bs_off].v = uint8_t(uint(rc_ob) + carry);
                for (uint k = 0; k < rc_oc; k += RC_LANES)
                    if (lane < rc_oc - k)
                        slice_data[rc.bs_off + 1 + k + lane].v = uint8_t(carry - 1u);
                rc.bs_off += 1 + rc_oc;
                rc_oc = 0;
                rc_ob = int((l >> 8) & 0xFFu);
            }
        }
    }
}

void rac_emit(void)
{
    [[unroll]] for (uint k = 0; k < 64/RC_LANES; k++)
        if (rc_nev > k*RC_LANES)
            rac_emit_block(rc_ev[k], min(rc_nev - k*RC_LANES, RC_LANES));
    rc_nev = 0;
}

void rac_renorm_enc(void)
{
    uint low = rc_top - rc.range;
    rc.range <<= 8;
    rc_top = ((low & 0xFFu) << 8) + rc.range;
    [[unroll]] for (uint k = 0; k < 64/RC_LANES; k++)
        rc_ev[k] = gl_SubgroupInvocationID + k*RC_LANES == rc_nev ? low : rc_ev[k];
    rc_nev++;
}

void put_rac_range1(uint range1, bool bit)
{
    rc_top = bit ? rc_top : rc_top - range1;
    rc.range = bit ? range1 : rc.range - range1;
    [[dont_flatten]] if (rc.range < 0x100)
        rac_renorm_enc();
}

void put_rac(uint state24, bool bit)
{
    put_rac_range1(rac_range1(rc.range, state24), bit);
}

void put_rac_equi(bool bit)
{
    put_rac_range1(rc.range >> 1, bit);
}

uint rac_terminate(void)
{
    rac_emit();

    uint range1 = (rc.range * 129) >> 8;
    rc.range -= range1;
    rc_top -= range1;
    if (rc.range < 0x100)
        rac_renorm_enc();

    rc_top = rc_top - rc.range + 0x1FEu;
    rc.range = 0xFFu;
    rac_renorm_enc();
    rc_top = rc_top - rc.range + 0xFFu;
    rc.range = 0xFFu;
    rac_renorm_enc();
    rac_emit();

    rc.low = rc_top - rc.range;
    rc.outstanding_count = uint16_t(rc_oc);
    rc.outstanding_byte = int16_t(rc_ob);
    return rc.bs_off - rc.bs_start;
}

void put_isymbol_tail(int e, RCStates st24, uint a, bool neg)
{
    uint s[21];
    [[unroll]] for (uint i = 0; i < 11; i++)
        if (i <= e + 1)
            s[i] = subgroupBroadcast(st24.v[i/RC_LANES], i % RC_LANES);
    [[unroll]] for (uint i = 0; i < 9; i++)
        if (i < e)
            s[11 + i] = subgroupBroadcast(st24.v[(22 + i)/RC_LANES], (22 + i) % RC_LANES);
    s[20] = subgroupBroadcast(st24.v[(11 + e)/RC_LANES], (11 + e) % RC_LANES);

    uint range = rc.range;
    uint top = rc_top;
    uint r = rac_range1(rc.range, s[0]);
    rc.range -= r;
    rc_top -= r;
    [[unroll]] for (int i = 0; i < 9; i++)
        if (i < e)
            rc.range = rac_range1(rc.range, s[1 + i]);
    r = rac_range1(rc.range, s[1 + e]);
    rc.range -= r;
    rc_top -= r;
    [[dont_flatten]] if (rc.range < 0x100) {
        rc.range = range;
        rc_top = top;
        put_rac(s[0], false);
        [[unroll]] for (int i = 0; i < 9; i++)
            if (i < e)
                put_rac(s[1 + i], true);
        put_rac(s[1 + e], false);
    }
    [[unroll]] for (int i = 8; i >= 0; i--)
        if (i < e)
            put_rac(s[11 + i], bitfieldExtract(a, i, 1) != 0);
    put_rac(s[20], neg);
}

void put_isymbol_esc(int e, RCStates st, RCStates st24, uint a, bool neg,
                     out uint s10, out uint s31)
{
    uint s[10];
    [[unroll]] for (uint i = 0; i < 10; i++)
        s[i] = subgroupBroadcast(st24.v[i/RC_LANES], i % RC_LANES);
    s10 = subgroupBroadcast(st.v[10/RC_LANES], 10 % RC_LANES);
    s31 = subgroupBroadcast(st.v[31/RC_LANES], 31 % RC_LANES);
    put_rac(s[0], false);
    [[unroll]] for (int i = 1; i < 10; i++)
        put_rac(s[i], true);
    if (rc_nev > 40)
        rac_emit();

    for (int i = 9; i < e; i++) {
        uint nx = rangecoder_state[256 + s10];
        put_rac(s10 << 24, true);
        if (rc_nev > 60)
            rac_emit();
        s10 = nx;
    }
    uint nx = rangecoder_state[s10];
    put_rac(s10 << 24, false);
    s10 = nx;

    for (int i = e - 1; i >= 9; i--) {
        bool b = bitfieldExtract(a, i, 1) != 0;
        uint n0 = rangecoder_state[s31];
        uint n1 = rangecoder_state[256 + s31];
        put_rac(s31 << 24, b);
        if (rc_nev > 60)
            rac_emit();
        s31 = b ? n1 : n0;
    }
    if (rc_nev > 50)
        rac_emit();

    [[unroll]] for (int i = 8; i >= 0; i--)
        s[i] = subgroupBroadcast(st24.v[(22 + i)/RC_LANES], (22 + i) % RC_LANES);
    uint ss = subgroupBroadcast(st24.v[21/RC_LANES], 21 % RC_LANES);
    [[unroll]] for (int i = 8; i >= 0; i--)
        put_rac(s[i], bitfieldExtract(a, i, 1) != 0);
    put_rac(ss, neg);
}

void put_isymbol(RCStates st, int v, out uint s10, out uint s31)
{
    RCStates st24;
    [[unroll]] for (uint k = 0; k < RC_K; k++)
        st24.v[k] = st.v[k] << 24;
    uint a = abs(v);
    int e = findMSB(a);
    bool neg = v < 0;
    s10 = 0;
    s31 = 0;

    if (v != 0) {
        if (e < 4) {
            if (e < 2) {
                if (e == 1)
                    put_isymbol_tail(1, st24, a, neg);
                else
                    put_isymbol_tail(0, st24, a, neg);
            } else if (e == 3) {
                put_isymbol_tail(3, st24, a, neg);
            } else {
                put_isymbol_tail(2, st24, a, neg);
            }
        } else if (e < 7) {
            if (e == 4)
                put_isymbol_tail(4, st24, a, neg);
            else if (e == 5)
                put_isymbol_tail(5, st24, a, neg);
            else
                put_isymbol_tail(6, st24, a, neg);
        } else if (e < 10) {
            if (e == 7)
                put_isymbol_tail(7, st24, a, neg);
            else if (e == 8)
                put_isymbol_tail(8, st24, a, neg);
            else
                put_isymbol_tail(9, st24, a, neg);
        } else {
            put_isymbol_esc(e, st, st24, a, neg, s10, s31);
        }
    } else {
        put_rac(subgroupBroadcast(st24.v[0], 0), true);
    }
}
#endif

#ifdef DECODE
uint rc_win;
uint rc_winx[RC_K];
uint rc_dist;
uint rc_next;
uint rc_pos;

void rac_load_window(void)
{
    rc.bs_off += rc_pos;
    rc_pos &= ~31u;
    uint o = rc.bs_off + gl_SubgroupInvocationID;
    rc_win = uint(~(o < rc.bs_end ? u8buf(uint64_t(slice_data) + o).v : uint8_t(0)));
    [[unroll]] for (uint k = 1; k < RC_K; k++) {
        o += RC_LANES;
        rc_winx[k] = uint(~(o < rc.bs_end ? u8buf(uint64_t(slice_data) + o).v : uint8_t(0)));
    }
}

void rac_init_dec(in RangeCoder c)
{
    for (uint i = gl_SubgroupInvocationID; i < 512; i += RC_LANES)
        zero_one_state[i] = rangecoder_state[i];
    barrier();

    rc = c;
    rc_dist = rc.range - rc.low - 1;
    rc_pos = 0;
    rac_load_window();
    rc_next = subgroupBroadcast(rc_win, 0);
}

void rac_check_window(void)
{
    if (rc_pos > 10)
        rac_load_window();
}

void refill(void)
{
    rc.range <<= 8;
    rc_dist = (rc_dist << 8) | rc_next;
    if (RC_K == 1)
        rc_next = subgroupBroadcast(rc_win, ++rc_pos);
    else if (++rc_pos < RC_LANES)
        rc_next = subgroupBroadcast(rc_win, rc_pos);
    else
        rc_next = subgroupBroadcast(rc_winx[rc_pos/RC_LANES], rc_pos % RC_LANES);
}

void rac_renorm(void)
{
    if (expectEXT(rc.range < 0x100, false))
        refill();
}

bool get_rac_internal(uint range1)
{
    bool bit = rc_dist < range1;
    uint ranged = rc.range - range1;
    uint distd = rc_dist - range1;
    rc.range = bit ? range1 : ranged;
    rc_dist = bit ? rc_dist : distd;
    return bit;
}

bool get_rac(uint state24)
{
    bool bit = get_rac_internal(rac_range1(rc.range, state24));
    rac_renorm();
    return bit;
}

bool get_rac_equi(void)
{
    rac_check_window();
    bool bit = get_rac_internal(rc.range >> 1);
    rac_renorm();
    return bit;
}

int get_isymbol_tail(int e, uint range, uint range1, RCStates sx, int pred, int sgn,
                     out uint read, out uint bits)
{
    uint m[9];
    [[unroll]] for (int k = 8; k >= 0; k--)
        if (k + 1 < e)
            m[k] = subgroupBroadcast(sx.v[(22 + k)/RC_LANES], (22 + k) % RC_LANES);
    uint ss = subgroupBroadcast(sx.v[(10 + e)/RC_LANES], (10 + e) % RC_LANES);

    rc.range = range - range1;
    rc_dist -= range1;
    rac_renorm();

    uint a = 0;
    [[unroll]] for (int k = 8; k >= 0; k--) {
        if (k + 1 < e) {
            a = (a << 1) + uint(get_rac_internal(rac_range1(rc.range, m[k])));
            rac_renorm();
        }
    }

    a += 1u << (e - 1);
    int sa = int(a)*sgn;
    int vp = pred + sa;
    int vn = vp - 2*sa;
    bool neg = get_rac_internal(rac_range1(rc.range, ss));
    int v = neg ? vn : vp;
    read = (2u << e) - 1u + (((1u << (e - 1)) - 1u) << 22) + (1u << (10 + e));
    bits = (a << 22) + (1u << e) - 2u - (1u << (21 + e)) + (neg ? 1u << (10 + e) : 0u);
    return v;
}

const int AVERROR_INVALIDDATA = -0x41444E49;

int get_isymbol_esc(inout RCStates st, RCStates sx, int pred, int sgn,
                    out uint read, out uint bits)
{
    bool esc = c_bits > 10;
    int n = 11;
    if (esc) {
        uint s10 = subgroupBroadcast(st.v[10/RC_LANES], 10 % RC_LANES);
        bool one;
        [[dont_unroll]] do {
            s10 = rangecoder_state[s10 + 256];
            n++;
            rac_check_window();
            one = get_rac(s10 << 24);
        } while (one && n < 33);
        st.v[10/RC_LANES] = gl_SubgroupInvocationID == 10 % RC_LANES ? s10 : st.v[10/RC_LANES];

        if (one) {
            read = 0x7FFu;
            bits = 0x7FEu;
            return pred + sgn*AVERROR_INVALIDDATA;
        }
    }

    uint s31 = subgroupBroadcast(st.v[31/RC_LANES], 31 % RC_LANES);
    rac_check_window();
    bool b = get_rac(s31 << 24);
    uint a = b ? 0x3 : 0x2;
    for (n -= 2; n >= 11; n--) {
        s31 = rangecoder_state[s31 + (b ? 256 : 0)];
        rac_check_window();
        b = get_rac(s31 << 24);
        a = (a << 1) | uint(b);
    }
    st.v[31/RC_LANES] = gl_SubgroupInvocationID == 31 % RC_LANES ? s31 : st.v[31/RC_LANES];

    rac_check_window();
    [[unroll]] for (int k = 8; k >= 0; k--) {
        uint sk = subgroupBroadcast(sx.v[(22 + k)/RC_LANES], (22 + k) % RC_LANES);
        a = (a << 1) | uint(get_rac(sk));
    }

    uint ss = subgroupBroadcast(sx.v[21/RC_LANES], 21 % RC_LANES);
    bool neg = get_rac_internal(rac_range1(rc.range, ss));
    int sa = int(a)*sgn;
    read = 0xFFE007FFu;
    bits = ((esc ? 0x1FFu : 0x3FFu) << 1) | ((a & 0x3FFu) << 22) | (uint(neg) << 21);
    return neg ? pred - sa : pred + sa;
}

int get_isymbol(inout RCStates st, int pred, int sgn, out uint read, out uint bits)
{
    RCStates sx;
    [[unroll]] for (uint k = 0; k < RC_K; k++)
        sx.v[k] = st.v[k] << 24;
    uint s[11];
    [[unroll]] for (uint i = 0; i < 11; i++)
        s[i] = subgroupBroadcast(sx.v[i/RC_LANES], i % RC_LANES);

    read = 1u;
    bits = 1u;

    uint range = rc.range;
    uint dist = rc_dist;
    uint range0 = rac_range1(range, s[0]);
    uint r[11];
    r[0] = range - range0;
    r[1] = rac_range1(r[0], s[1]);
    rc.range = r[0];
    rc_dist = dist - range0;
    uint lim = max(rc_dist, 0xffu);

    int v;
    while (true) {
        uint skip;
        [[unroll]] for (int i = 2; i < 6; i++)
            r[i] = rac_range1(r[i - 1], s[i]);

        if (r[5] > lim) {
            [[unroll]] for (int i = 6; i < 11; i++)
                r[i] = rac_range1(r[i - 1], s[i]);
            if (r[6] > lim) {
                if (r[7] > lim) {
                    if (r[8] > lim) {
                        if (r[9] > lim) {
                            if (r[10] > lim) {
                                rc.range = r[10];
                                v = get_isymbol_esc(st, sx, pred, sgn, read, bits);
                                break;
                            } else if (r[10] <= rc_dist) {
                                v = get_isymbol_tail(10, r[9], r[10], sx, pred, sgn, read, bits);
                                break;
                            } else {
                                skip = 10;
                                rc.range = r[10];
                            }
                        } else if (r[9] <= rc_dist) {
                            v = get_isymbol_tail(9, r[8], r[9], sx, pred, sgn, read, bits);
                            break;
                        } else {
                            skip = 9;
                            rc.range = r[9];
                        }
                    } else if (r[8] <= rc_dist) {
                        v = get_isymbol_tail(8, r[7], r[8], sx, pred, sgn, read, bits);
                        break;
                    } else {
                        skip = 8;
                        rc.range = r[8];
                    }
                } else if (r[7] <= rc_dist) {
                    v = get_isymbol_tail(7, r[6], r[7], sx, pred, sgn, read, bits);
                    break;
                } else {
                    skip = 7;
                    rc.range = r[7];
                }
            } else if (r[6] <= rc_dist) {
                v = get_isymbol_tail(6, r[5], r[6], sx, pred, sgn, read, bits);
                break;
            } else {
                skip = 6;
                rc.range = r[6];
            }
        } else if (r[4] > lim) {
            if (r[5] <= rc_dist) {
                v = get_isymbol_tail(5, r[4], r[5], sx, pred, sgn, read, bits);
                break;
            } else {
                skip = 5;
                rc.range = r[5];
            }
        } else if (r[3] > lim) {
            if (r[4] <= rc_dist) {
                v = get_isymbol_tail(4, r[3], r[4], sx, pred, sgn, read, bits);
                break;
            } else {
                skip = 4;
                rc.range = r[4];
            }
        } else if (r[2] > lim) {
            if (r[3] <= rc_dist) {
                v = get_isymbol_tail(3, r[2], r[3], sx, pred, sgn, read, bits);
                break;
            } else {
                skip = 3;
                rc.range = r[3];
            }
        } else if (r[1] > lim) {
            if (r[2] <= rc_dist) {
                v = get_isymbol_tail(2, r[1], r[2], sx, pred, sgn, read, bits);
                break;
            } else {
                skip = 2;
                rc.range = r[2];
            }
        } else if (range0 > min(dist, range - 0x100)) {
            if (dist < range0) {
                rc.range = range0;
                rc_dist = dist;
                v = pred;
                break;
            }
            skip = 0;
            rc.range = r[0];
        } else if (r[1] <= rc_dist) {
            v = get_isymbol_tail(1, r[0], r[1], sx, pred, sgn, read, bits);
            break;
        } else {
            skip = 1;
            rc.range = r[1];
        }

        refill();
        lim = max(rc_dist, 0xffu);
        r[0] = rc.range;
        r[1] = skip > 0 ? rc.range + skip - 1 : rac_range1(rc.range, s[1]);
        range0 = 0;
        uint skipped = (2u << skip) - 2u;
        [[unroll]] for (uint k = 0; k < RC_K; k++)
            sx.v[k] = subgroupInverseBallot(uvec4(skipped >> (k*RC_LANES), 0, 0, 0)) ? ~0u : sx.v[k];
        [[unroll]] for (uint i = 2; i < 11; i++)
            s[i] = subgroupBroadcast(sx.v[i/RC_LANES], i % RC_LANES);
    }

    return v;
}
#endif

#endif /* VULKAN_RANGECODER_SUBGROUP_H */
