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

#define CONTEXT_SIZE 32
#define MAX_OVERREAD 2

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

#ifdef DECODE
uint rc_win;
uint rc_dist;
uint rc_next;
uint rc_pos;

void rac_load_window(void)
{
    rc.bs_off += rc_pos;
    rc_pos &= ~31u;
    uint o = rc.bs_off + gl_SubgroupInvocationID;
    rc_win = uint(~(o < rc.bs_end ? u8buf(uint64_t(slice_data) + o).v : uint8_t(0)));
}

void rac_init_dec(in RangeCoder c)
{
    for (uint i = gl_SubgroupInvocationID; i < 512; i += gl_SubgroupSize)
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
    rc_next = subgroupBroadcast(rc_win, ++rc_pos);
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

const int AVERROR_INVALIDDATA = -0x41444E49;

int get_isymbol_esc(inout uint st, uint sx, int pred, int sgn, out uint read, out uint bits)
{
    bool esc = c_bits > 10;
    int n = 11;
    if (esc) {
        uint s10 = subgroupBroadcast(st, 10);
        bool one;
        [[dont_unroll]] do {
            s10 = rangecoder_state[s10 + 256];
            n++;
            rac_check_window();
            one = get_rac(s10 << 24);
        } while (one && n < 33);
        st = gl_SubgroupInvocationID == 10 ? s10 : st;

        if (one) {
            read = 0x7FFu;
            bits = 0x7FEu;
            return pred + sgn*AVERROR_INVALIDDATA;
        }
    }

    uint s31 = subgroupBroadcast(st, 31);
    rac_check_window();
    bool b = get_rac(s31 << 24);
    uint a = b ? 0x3 : 0x2;
    for (n -= 2; n >= 11; n--) {
        s31 = rangecoder_state[s31 + (b ? 256 : 0)];
        rac_check_window();
        b = get_rac(s31 << 24);
        a = (a << 1) | uint(b);
    }
    st = gl_SubgroupInvocationID == 31 ? s31 : st;

    rac_check_window();
    [[unroll]] for (int k = 8; k >= 0; k--)
        a = (a << 1) | uint(get_rac(subgroupBroadcast(sx, 22 + k)));

    bool neg = get_rac(subgroupBroadcast(sx, 21));
    int sa = int(a)*sgn;
    read = 0xFFE007FFu;
    bits = ((esc ? 0x1FFu : 0x3FFu) << 1) | ((a & 0x3FFu) << 22) | (uint(neg) << 21);
    return neg ? pred - sa : pred + sa;
}

int get_isymbol(inout uint st, int pred, int sgn, out uint read, out uint bits)
{
    uint st24 = st << 24;
    if (get_rac(subgroupBroadcast(st24, 0))) {
        read = 1u;
        bits = 1u;
        return pred;
    }

    int e = 1;
    while (e < 11 && get_rac(subgroupBroadcast(st24, e)))
        e++;
    if (e == 11)
        return get_isymbol_esc(st, st24, pred, sgn, read, bits);

    uint a = 1u;
    for (int k = e - 2; k >= 0; k--)
        a = (a << 1) | uint(get_rac(subgroupBroadcast(st24, 22 + k)));
    bool neg = get_rac(subgroupBroadcast(st24, 10 + e));

    read = (2u << e) - 1u + (((1u << (e - 1)) - 1u) << 22) + (1u << (10 + e));
    bits = (a << 22) + (1u << e) - 2u - (1u << (21 + e)) + (neg ? 1u << (10 + e) : 0u);
    int sa = int(a)*sgn;
    return neg ? pred - sa : pred + sa;
}
#endif

#endif /* VULKAN_RANGECODER_SUBGROUP_H */
