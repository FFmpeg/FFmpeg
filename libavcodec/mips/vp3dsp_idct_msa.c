/*
 * Copyright (c) 2018 gxw <guxiwei-hf@loongson.cn>
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

#include "vp3dsp_mips.h"
#include "libavutil/mips/generic_macros_msa.h"
#include "libavutil/intreadwrite.h"
#include "libavcodec/rnd_avg.h"

static void idct_msa(uint8_t *dst, int stride, int16_t *input, int type)
{
    v8i16 r0, r1, r2, r3, r4, r5, r6, r7, sign;
    v4i32 r0_r, r0_l, r1_r, r1_l, r2_r, r2_l, r3_r, r3_l,
          r4_r, r4_l, r5_r, r5_l, r6_r, r6_l, r7_r, r7_l;
    v4i32 A, B, C, D, Ad, Bd, Cd, Dd, E, F, G, H;
    v4i32 Ed, Gd, Add, Bdd, Fd, Hd;
    v16u8 sign_l;
    v16i8 d0, d1, d2, d3, d4, d5, d6, d7;
    v4i32 c0, c1, c2, c3, c4, c5, c6, c7;
    v4i32 f0, f1, f2, f3, f4, f5, f6, f7;
    v4i32 sign_t;
    v16i8 zero = {0};
    v16i8 mask = {0, 4, 8, 12, 16, 20, 24, 28, 0, 0, 0, 0, 0, 0, 0, 0};
    v4i32 cnst64277w = {64277, 64277, 64277, 64277};
    v4i32 cnst60547w = {60547, 60547, 60547, 60547};
    v4i32 cnst54491w = {54491, 54491, 54491, 54491};
    v4i32 cnst46341w = {46341, 46341, 46341, 46341};
    v4i32 cnst36410w = {36410, 36410, 36410, 36410};
    v4i32 cnst25080w = {25080, 25080, 25080, 25080};
    v4i32 cnst12785w = {12785, 12785, 12785, 12785};
    v4i32 cnst8w = {8, 8, 8, 8};
    v4i32 cnst2048w = {2048, 2048, 2048, 2048};
    v4i32 cnst128w = {128, 128, 128, 128};

    /* Extended input data */
    LD_SH8(input, 8, r0, r1, r2, r3, r4, r5, r6, r7);
    sign = __msa_clti_s_h(r0, 0);
    r0_r = (v4i32) __msa_ilvr_h(sign, r0);
    r0_l = (v4i32) __msa_ilvl_h(sign, r0);
    sign = __msa_clti_s_h(r1, 0);
    r1_r = (v4i32) __msa_ilvr_h(sign, r1);
    r1_l = (v4i32) __msa_ilvl_h(sign, r1);
    sign = __msa_clti_s_h(r2, 0);
    r2_r = (v4i32) __msa_ilvr_h(sign, r2);
    r2_l = (v4i32) __msa_ilvl_h(sign, r2);
    sign = __msa_clti_s_h(r3, 0);
    r3_r = (v4i32) __msa_ilvr_h(sign, r3);
    r3_l = (v4i32) __msa_ilvl_h(sign, r3);
    sign = __msa_clti_s_h(r4, 0);
    r4_r = (v4i32) __msa_ilvr_h(sign, r4);
    r4_l = (v4i32) __msa_ilvl_h(sign, r4);
    sign = __msa_clti_s_h(r5, 0);
    r5_r = (v4i32) __msa_ilvr_h(sign, r5);
    r5_l = (v4i32) __msa_ilvl_h(sign, r5);
    sign = __msa_clti_s_h(r6, 0);
    r6_r = (v4i32) __msa_ilvr_h(sign, r6);
    r6_l = (v4i32) __msa_ilvl_h(sign, r6);
    sign = __msa_clti_s_h(r7, 0);
    r7_r = (v4i32) __msa_ilvr_h(sign, r7);
    r7_l = (v4i32) __msa_ilvl_h(sign, r7);

    /* Right part */
    A = ((r1_r * cnst64277w) >> 16) + ((r7_r * cnst12785w) >> 16);
    B = ((r1_r * cnst12785w) >> 16) - ((r7_r * cnst64277w) >> 16);
    C = ((r3_r * cnst54491w) >> 16) + ((r5_r * cnst36410w) >> 16);
    D = ((r5_r * cnst54491w) >> 16) - ((r3_r * cnst36410w) >> 16);
    Ad = ((A - C) * cnst46341w) >> 16;
    Bd = ((B - D) * cnst46341w) >> 16;
    Cd = A + C;
    Dd = B + D;
    E = ((r0_r + r4_r) * cnst46341w) >> 16;
    F = ((r0_r - r4_r) * cnst46341w) >> 16;
    G = ((r2_r * cnst60547w) >> 16) + ((r6_r * cnst25080w) >> 16);
    H = ((r2_r * cnst25080w) >> 16) - ((r6_r * cnst60547w) >> 16);
    Ed = E - G;
    Gd = E + G;
    Add = F + Ad;
    Bdd = Bd - H;
    Fd = F - Ad;
    Hd = Bd + H;
    r0_r = Gd + Cd;
    r7_r = Gd - Cd;
    r1_r = Add + Hd;
    r2_r = Add - Hd;
    r3_r = Ed + Dd;
    r4_r = Ed - Dd;
    r5_r = Fd + Bdd;
    r6_r = Fd - Bdd;

    /* Left part */
    A = ((r1_l * cnst64277w) >> 16) + ((r7_l * cnst12785w) >> 16);
    B = ((r1_l * cnst12785w) >> 16) - ((r7_l * cnst64277w) >> 16);
    C = ((r3_l * cnst54491w) >> 16) + ((r5_l * cnst36410w) >> 16);
    D = ((r5_l * cnst54491w) >> 16) - ((r3_l * cnst36410w) >> 16);
    Ad = ((A - C) * cnst46341w) >> 16;
    Bd = ((B - D) * cnst46341w) >> 16;
    Cd = A + C;
    Dd = B + D;
    E = ((r0_l + r4_l) * cnst46341w) >> 16;
    F = ((r0_l - r4_l) * cnst46341w) >> 16;
    G = ((r2_l * cnst60547w) >> 16) + ((r6_l * cnst25080w) >> 16);
    H = ((r2_l * cnst25080w) >> 16) - ((r6_l * cnst60547w) >> 16);
    Ed = E - G;
    Gd = E + G;
    Add = F + Ad;
    Bdd = Bd - H;
    Fd = F - Ad;
    Hd = Bd + H;
    r0_l = Gd + Cd;
    r7_l = Gd - Cd;
    r1_l = Add + Hd;
    r2_l = Add - Hd;
    r3_l = Ed + Dd;
    r4_l = Ed - Dd;
    r5_l = Fd + Bdd;
    r6_l = Fd - Bdd;

    /* Row 0 to 3 */
    TRANSPOSE4x4_SW_SW(r0_r, r1_r, r2_r, r3_r,
                       r0_r, r1_r, r2_r, r3_r);
    TRANSPOSE4x4_SW_SW(r0_l, r1_l, r2_l, r3_l,
                       r0_l, r1_l, r2_l, r3_l);
    A = ((r1_r * cnst64277w) >> 16) + ((r3_l * cnst12785w) >> 16);
    B = ((r1_r * cnst12785w) >> 16) - ((r3_l * cnst64277w) >> 16);
    C = ((r3_r * cnst54491w) >> 16) + ((r1_l * cnst36410w) >> 16);
    D = ((r1_l * cnst54491w) >> 16) - ((r3_r * cnst36410w) >> 16);
    Ad = ((A - C) * cnst46341w) >> 16;
    Bd = ((B - D) * cnst46341w) >> 16;
    Cd = A + C;
    Dd = B + D;
    E = ((r0_r + r0_l) * cnst46341w) >> 16;
    E += cnst8w;
    F = ((r0_r - r0_l) * cnst46341w) >> 16;
    F += cnst8w;
    if (type == 1) { // HACK
        E += cnst2048w;
        F += cnst2048w;
    }
    G = ((r2_r * cnst60547w) >> 16) + ((r2_l * cnst25080w) >> 16);
    H = ((r2_r * cnst25080w) >> 16) - ((r2_l * cnst60547w) >> 16);
    Ed = E - G;
    Gd = E + G;
    Add = F + Ad;
    Bdd = Bd - H;
    Fd = F - Ad;
    Hd = Bd + H;
    A = (Gd + Cd) >> 4;
    B = (Gd - Cd) >> 4;
    C = (Add + Hd) >> 4;
    D = (Add - Hd) >> 4;
    E = (Ed + Dd) >> 4;
    F = (Ed - Dd) >> 4;
    G = (Fd + Bdd) >> 4;
    H = (Fd - Bdd) >> 4;
    if (type != 1) {
        LD_SB8(dst, stride, d0, d1, d2, d3, d4, d5, d6, d7);
        ILVR_B4_SW(zero, d0, zero, d1, zero, d2, zero, d3,
                   f0, f1, f2, f3);
        ILVR_B4_SW(zero, d4, zero, d5, zero, d6, zero, d7,
                   f4, f5, f6, f7);
        ILVR_H4_SW(zero, f0, zero, f1, zero, f2, zero, f3,
                   c0, c1, c2, c3);
        ILVR_H4_SW(zero, f4, zero, f5, zero, f6, zero, f7,
                   c4, c5, c6, c7);
        A += c0;
        B += c7;
        C += c1;
        D += c2;
        E += c3;
        F += c4;
        G += c5;
        H += c6;
    }
    A = CLIP_SW_0_255(A);
    B = CLIP_SW_0_255(B);
    C = CLIP_SW_0_255(C);
    D = CLIP_SW_0_255(D);
    E = CLIP_SW_0_255(E);
    F = CLIP_SW_0_255(F);
    G = CLIP_SW_0_255(G);
    H = CLIP_SW_0_255(H);
    sign_l = __msa_or_v((v16u8)r1_r, (v16u8)r2_r);
    sign_l = __msa_or_v(sign_l, (v16u8)r3_r);
    sign_l = __msa_or_v(sign_l, (v16u8)r0_l);
    sign_l = __msa_or_v(sign_l, (v16u8)r1_l);
    sign_l = __msa_or_v(sign_l, (v16u8)r2_l);
    sign_l = __msa_or_v(sign_l, (v16u8)r3_l);
    sign_t = __msa_ceqi_w((v4i32)sign_l, 0);
    Add = ((r0_r * cnst46341w) + (8 << 16)) >> 20;
    if (type == 1) {
        Bdd = Add + cnst128w;
        Bdd = CLIP_SW_0_255(Bdd);
        Ad = Bdd;
        Bd = Bdd;
        Cd = Bdd;
        Dd = Bdd;
        Ed = Bdd;
        Fd = Bdd;
        Gd = Bdd;
        Hd = Bdd;
    } else {
        Ad = Add + c0;
        Bd = Add + c1;
        Cd = Add + c2;
        Dd = Add + c3;
        Ed = Add + c4;
        Fd = Add + c5;
        Gd = Add + c6;
        Hd = Add + c7;
        Ad = CLIP_SW_0_255(Ad);
        Bd = CLIP_SW_0_255(Bd);
        Cd = CLIP_SW_0_255(Cd);
        Dd = CLIP_SW_0_255(Dd);
        Ed = CLIP_SW_0_255(Ed);
        Fd = CLIP_SW_0_255(Fd);
        Gd = CLIP_SW_0_255(Gd);
        Hd = CLIP_SW_0_255(Hd);
    }
    Ad = (v4i32)__msa_and_v((v16u8)Ad, (v16u8)sign_t);
    Bd = (v4i32)__msa_and_v((v16u8)Bd, (v16u8)sign_t);
    Cd = (v4i32)__msa_and_v((v16u8)Cd, (v16u8)sign_t);
    Dd = (v4i32)__msa_and_v((v16u8)Dd, (v16u8)sign_t);
    Ed = (v4i32)__msa_and_v((v16u8)Ed, (v16u8)sign_t);
    Fd = (v4i32)__msa_and_v((v16u8)Fd, (v16u8)sign_t);
    Gd = (v4i32)__msa_and_v((v16u8)Gd, (v16u8)sign_t);
    Hd = (v4i32)__msa_and_v((v16u8)Hd, (v16u8)sign_t);
    sign_t = __msa_ceqi_w(sign_t, 0);
    A = (v4i32)__msa_and_v((v16u8)A, (v16u8)sign_t);
    B = (v4i32)__msa_and_v((v16u8)B, (v16u8)sign_t);
    C = (v4i32)__msa_and_v((v16u8)C, (v16u8)sign_t);
    D = (v4i32)__msa_and_v((v16u8)D, (v16u8)sign_t);
    E = (v4i32)__msa_and_v((v16u8)E, (v16u8)sign_t);
    F = (v4i32)__msa_and_v((v16u8)F, (v16u8)sign_t);
    G = (v4i32)__msa_and_v((v16u8)G, (v16u8)sign_t);
    H = (v4i32)__msa_and_v((v16u8)H, (v16u8)sign_t);
    r0_r = Ad + A;
    r1_r = Bd + C;
    r2_r = Cd + D;
    r3_r = Dd + E;
    r0_l = Ed + F;
    r1_l = Fd + G;
    r2_l = Gd + H;
    r3_l = Hd + B;

    /* Row 4 to 7 */
    TRANSPOSE4x4_SW_SW(r4_r, r5_r, r6_r, r7_r,
                       r4_r, r5_r, r6_r, r7_r);
    TRANSPOSE4x4_SW_SW(r4_l, r5_l, r6_l, r7_l,
                       r4_l, r5_l, r6_l, r7_l);
    A = ((r5_r * cnst64277w) >> 16) + ((r7_l * cnst12785w) >> 16);
    B = ((r5_r * cnst12785w) >> 16) - ((r7_l * cnst64277w) >> 16);
    C = ((r7_r * cnst54491w) >> 16) + ((r5_l * cnst36410w) >> 16);
    D = ((r5_l * cnst54491w) >> 16) - ((r7_r * cnst36410w) >> 16);
    Ad = ((A - C) * cnst46341w) >> 16;
    Bd = ((B - D) * cnst46341w) >> 16;
    Cd = A + C;
    Dd = B + D;
    E = ((r4_r + r4_l) * cnst46341w) >> 16;
    E += cnst8w;
    F = ((r4_r - r4_l) * cnst46341w) >> 16;
    F += cnst8w;
    if (type == 1) { // HACK
        E += cnst2048w;
        F += cnst2048w;
    }
    G = ((r6_r * cnst60547w) >> 16) + ((r6_l * cnst25080w) >> 16);
    H = ((r6_r * cnst25080w) >> 16) - ((r6_l * cnst60547w) >> 16);
    Ed = E - G;
    Gd = E + G;
    Add = F + Ad;
    Bdd = Bd - H;
    Fd = F - Ad;
    Hd = Bd + H;
    A = (Gd + Cd) >> 4;
    B = (Gd - Cd) >> 4;
    C = (Add + Hd) >> 4;
    D = (Add - Hd) >> 4;
    E = (Ed + Dd) >> 4;
    F = (Ed - Dd) >> 4;
    G = (Fd + Bdd) >> 4;
    H = (Fd - Bdd) >> 4;
    if (type != 1) {
        ILVL_H4_SW(zero, f0, zero, f1, zero, f2, zero, f3,
                   c0, c1, c2, c3);
        ILVL_H4_SW(zero, f4, zero, f5, zero, f6, zero, f7,
                   c4, c5, c6, c7);
        A += c0;
        B += c7;
        C += c1;
        D += c2;
        E += c3;
        F += c4;
        G += c5;
        H += c6;
    }
    A = CLIP_SW_0_255(A);
    B = CLIP_SW_0_255(B);
    C = CLIP_SW_0_255(C);
    D = CLIP_SW_0_255(D);
    E = CLIP_SW_0_255(E);
    F = CLIP_SW_0_255(F);
    G = CLIP_SW_0_255(G);
    H = CLIP_SW_0_255(H);
    sign_l = __msa_or_v((v16u8)r5_r, (v16u8)r6_r);
    sign_l = __msa_or_v(sign_l, (v16u8)r7_r);
    sign_l = __msa_or_v(sign_l, (v16u8)r4_l);
    sign_l = __msa_or_v(sign_l, (v16u8)r5_l);
    sign_l = __msa_or_v(sign_l, (v16u8)r6_l);
    sign_l = __msa_or_v(sign_l, (v16u8)r7_l);
    sign_t = __msa_ceqi_w((v4i32)sign_l, 0);
    Add = ((r4_r * cnst46341w) + (8 << 16)) >> 20;
    if (type == 1) {
        Bdd = Add + cnst128w;
        Bdd = CLIP_SW_0_255(Bdd);
        Ad = Bdd;
        Bd = Bdd;
        Cd = Bdd;
        Dd = Bdd;
        Ed = Bdd;
        Fd = Bdd;
        Gd = Bdd;
        Hd = Bdd;
    } else {
        Ad = Add + c0;
        Bd = Add + c1;
        Cd = Add + c2;
        Dd = Add + c3;
        Ed = Add + c4;
        Fd = Add + c5;
        Gd = Add + c6;
        Hd = Add + c7;
        Ad = CLIP_SW_0_255(Ad);
        Bd = CLIP_SW_0_255(Bd);
        Cd = CLIP_SW_0_255(Cd);
        Dd = CLIP_SW_0_255(Dd);
        Ed = CLIP_SW_0_255(Ed);
        Fd = CLIP_SW_0_255(Fd);
        Gd = CLIP_SW_0_255(Gd);
        Hd = CLIP_SW_0_255(Hd);
    }
    Ad = (v4i32)__msa_and_v((v16u8)Ad, (v16u8)sign_t);
    Bd = (v4i32)__msa_and_v((v16u8)Bd, (v16u8)sign_t);
    Cd = (v4i32)__msa_and_v((v16u8)Cd, (v16u8)sign_t);
    Dd = (v4i32)__msa_and_v((v16u8)Dd, (v16u8)sign_t);
    Ed = (v4i32)__msa_and_v((v16u8)Ed, (v16u8)sign_t);
    Fd = (v4i32)__msa_and_v((v16u8)Fd, (v16u8)sign_t);
    Gd = (v4i32)__msa_and_v((v16u8)Gd, (v16u8)sign_t);
    Hd = (v4i32)__msa_and_v((v16u8)Hd, (v16u8)sign_t);
    sign_t = __msa_ceqi_w(sign_t, 0);
    A = (v4i32)__msa_and_v((v16u8)A, (v16u8)sign_t);
    B = (v4i32)__msa_and_v((v16u8)B, (v16u8)sign_t);
    C = (v4i32)__msa_and_v((v16u8)C, (v16u8)sign_t);
    D = (v4i32)__msa_and_v((v16u8)D, (v16u8)sign_t);
    E = (v4i32)__msa_and_v((v16u8)E, (v16u8)sign_t);
    F = (v4i32)__msa_and_v((v16u8)F, (v16u8)sign_t);
    G = (v4i32)__msa_and_v((v16u8)G, (v16u8)sign_t);
    H = (v4i32)__msa_and_v((v16u8)H, (v16u8)sign_t);
    r4_r = Ad + A;
    r5_r = Bd + C;
    r6_r = Cd + D;
    r7_r = Dd + E;
    r4_l = Ed + F;
    r5_l = Fd + G;
    r6_l = Gd + H;
    r7_l = Hd + B;
    VSHF_B2_SB(r0_r, r4_r, r1_r, r5_r, mask, mask, d0, d1);
    VSHF_B2_SB(r2_r, r6_r, r3_r, r7_r, mask, mask, d2, d3);
    VSHF_B2_SB(r0_l, r4_l, r1_l, r5_l, mask, mask, d4, d5);
    VSHF_B2_SB(r2_l, r6_l, r3_l, r7_l, mask, mask, d6, d7);

    /* Final sequence of operations over-write original dst */
    ST_D1(d0, 0, dst);
    ST_D1(d1, 0, dst + stride);
    ST_D1(d2, 0, dst + 2 * stride);
    ST_D1(d3, 0, dst + 3 * stride);
    ST_D1(d4, 0, dst + 4 * stride);
    ST_D1(d5, 0, dst + 5 * stride);
    ST_D1(d6, 0, dst + 6 * stride);
    ST_D1(d7, 0, dst + 7 * stride);
}

void ff_vp3_idct_put_msa(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    idct_msa(dest, line_size, block, 1);
    memset(block, 0, sizeof(*block) * 64);
}

void ff_vp3_idct_add_msa(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    idct_msa(dest, line_size, block, 2);
    memset(block, 0, sizeof(*block) * 64);
}

void ff_vp3_idct_dc_add_msa(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int i = (block[0] + 15) >> 5;
    v4i32 dc = {i, i, i, i};
    v16i8 d0, d1, d2, d3, d4, d5, d6, d7;
    v4i32 c0, c1, c2, c3, c4, c5, c6, c7;
    v4i32 e0, e1, e2, e3, e4, e5, e6, e7;
    v4i32 r0, r1, r2, r3, r4, r5, r6, r7;
    v16i8 mask = {0, 4, 8, 12, 16, 20, 24, 28, 0, 0, 0, 0, 0, 0, 0, 0};
    v16i8 zero = {0};

    LD_SB8(dest, line_size, d0, d1, d2, d3, d4, d5, d6, d7);
    ILVR_B4_SW(zero, d0, zero, d1, zero, d2, zero, d3,
               c0, c1, c2, c3);
    ILVR_B4_SW(zero, d4, zero, d5, zero, d6, zero, d7,
               c4, c5, c6, c7);
    /* Right part */
    ILVR_H4_SW(zero, c0, zero, c1, zero, c2, zero, c3,
               e0, e1, e2, e3);
    ILVR_H4_SW(zero, c4, zero, c5, zero, c6, zero, c7,
               e4, e5, e6, e7);
    e0 += dc;
    e1 += dc;
    e2 += dc;
    e3 += dc;
    e4 += dc;
    e5 += dc;
    e6 += dc;
    e7 += dc;
    e0 = CLIP_SW_0_255(e0);
    e1 = CLIP_SW_0_255(e1);
    e2 = CLIP_SW_0_255(e2);
    e3 = CLIP_SW_0_255(e3);
    e4 = CLIP_SW_0_255(e4);
    e5 = CLIP_SW_0_255(e5);
    e6 = CLIP_SW_0_255(e6);
    e7 = CLIP_SW_0_255(e7);

    /* Left part */
    ILVL_H4_SW(zero, c0, zero, c1, zero, c2, zero, c3,
               r0, r1, r2, r3);
    ILVL_H4_SW(zero, c4, zero, c5, zero, c6, zero, c7,
               r4, r5, r6, r7);
    r0 += dc;
    r1 += dc;
    r2 += dc;
    r3 += dc;
    r4 += dc;
    r5 += dc;
    r6 += dc;
    r7 += dc;
    r0 = CLIP_SW_0_255(r0);
    r1 = CLIP_SW_0_255(r1);
    r2 = CLIP_SW_0_255(r2);
    r3 = CLIP_SW_0_255(r3);
    r4 = CLIP_SW_0_255(r4);
    r5 = CLIP_SW_0_255(r5);
    r6 = CLIP_SW_0_255(r6);
    r7 = CLIP_SW_0_255(r7);
    VSHF_B2_SB(e0, r0, e1, r1, mask, mask, d0, d1);
    VSHF_B2_SB(e2, r2, e3, r3, mask, mask, d2, d3);
    VSHF_B2_SB(e4, r4, e5, r5, mask, mask, d4, d5);
    VSHF_B2_SB(e6, r6, e7, r7, mask, mask, d6, d7);

    /* Final sequence of operations over-write original dst */
    ST_D1(d0, 0, dest);
    ST_D1(d1, 0, dest + line_size);
    ST_D1(d2, 0, dest + 2 * line_size);
    ST_D1(d3, 0, dest + 3 * line_size);
    ST_D1(d4, 0, dest + 4 * line_size);
    ST_D1(d5, 0, dest + 5 * line_size);
    ST_D1(d6, 0, dest + 6 * line_size);
    ST_D1(d7, 0, dest + 7 * line_size);

    block[0] = 0;
}

void ff_vp3_v_loop_filter_msa(uint8_t *first_pixel, ptrdiff_t stride,
                              int *bounding_values)
{
    int nstride = -stride;
    v4i32 e0, e1, f0, f1, g0, g1;
    v16i8 zero = {0};
    v16i8 d0, d1, d2, d3;
    v8i16 c0, c1, c2, c3;
    v8i16 r0;
    v8i16 cnst3h = {3, 3, 3, 3, 3, 3, 3, 3},
          cnst4h = {4, 4, 4, 4, 4, 4, 4, 4};
    v16i8 mask = {0, 4, 8, 12, 16, 20, 24, 28, 0, 0, 0, 0, 0, 0, 0, 0};
    int16_t temp_16[8];
    int temp_32[8];

    LD_SB4(first_pixel + nstride * 2, stride, d0, d1, d2, d3);
    ILVR_B4_SH(zero, d0, zero, d1, zero, d2, zero, d3,
               c0, c1, c2, c3);
    r0 = (c0 - c3) + (c2 - c1) * cnst3h;
    r0 += cnst4h;
    r0 = r0 >> 3;
    /* Get filter_value from bounding_values one by one */
    ST_SH(r0, temp_16);
    for (int i = 0; i < 8; i++)
        temp_32[i] = bounding_values[temp_16[i]];
    LD_SW2(temp_32, 4, e0, e1);
    ILVR_H2_SW(zero, c1, zero, c2, f0, g0);
    ILVL_H2_SW(zero, c1, zero, c2, f1, g1);
    f0 += e0;
    f1 += e1;
    g0 -= e0;
    g1 -= e1;
    f0 = CLIP_SW_0_255(f0);
    f1 = CLIP_SW_0_255(f1);
    g0 = CLIP_SW_0_255(g0);
    g1 = CLIP_SW_0_255(g1);
    VSHF_B2_SB(f0, f1, g0, g1, mask, mask, d1, d2);

    /* Final move to first_pixel */
    ST_D1(d1, 0, first_pixel + nstride);
    ST_D1(d2, 0, first_pixel);
}

void ff_vp3_h_loop_filter_msa(uint8_t *first_pixel, ptrdiff_t stride,
                              int *bounding_values)
{
    v16i8 d0, d1, d2, d3, d4, d5, d6, d7;
    v8i16 c0, c1, c2, c3, c4, c5, c6, c7;
    v8i16 r0;
    v4i32 e0, e1, f0, f1, g0, g1;
    v16i8 zero = {0};
    v8i16 cnst3h = {3, 3, 3, 3, 3, 3, 3, 3},
          cnst4h = {4, 4, 4, 4, 4, 4, 4, 4};
    v16i8 mask = {0, 16, 4, 20, 8, 24, 12, 28, 0, 0, 0, 0, 0, 0, 0, 0};
    int16_t temp_16[8];
    int temp_32[8];

    LD_SB8(first_pixel - 2, stride, d0, d1, d2, d3, d4, d5, d6, d7);
    ILVR_B4_SH(zero, d0, zero, d1, zero, d2, zero, d3,
               c0, c1, c2, c3);
    ILVR_B4_SH(zero, d4, zero, d5, zero, d6, zero, d7,
               c4, c5, c6, c7);
    TRANSPOSE8x8_SH_SH(c0, c1, c2, c3, c4, c5, c6, c7,
                       c0, c1, c2, c3, c4, c5, c6, c7);
    r0 = (c0 - c3) + (c2 - c1) * cnst3h;
    r0 += cnst4h;
    r0 = r0 >> 3;

    /* Get filter_value from bounding_values one by one */
    ST_SH(r0, temp_16);
    for (int i = 0; i < 8; i++)
        temp_32[i] = bounding_values[temp_16[i]];
    LD_SW2(temp_32, 4, e0, e1);
    ILVR_H2_SW(zero, c1, zero, c2, f0, g0);
    ILVL_H2_SW(zero, c1, zero, c2, f1, g1);
    f0 += e0;
    f1 += e1;
    g0 -= e0;
    g1 -= e1;
    f0 = CLIP_SW_0_255(f0);
    f1 = CLIP_SW_0_255(f1);
    g0 = CLIP_SW_0_255(g0);
    g1 = CLIP_SW_0_255(g1);
    VSHF_B2_SB(f0, g0, f1, g1, mask, mask, d1, d2);
    /* Final move to first_pixel */
    ST_H4(d1, 0, 1, 2, 3, first_pixel - 1, stride);
    ST_H4(d2, 0, 1, 2, 3, first_pixel - 1 + 4 * stride, stride);
}

void ff_put_no_rnd_pixels_l2_msa(uint8_t *dst, const uint8_t *src1,
                                 const uint8_t *src2, ptrdiff_t stride, int h)
{
    if (h == 8) {
        v16i8 d0, d1, d2, d3, d4, d5, d6, d7;
        v16i8 c0, c1, c2, c3;
        v4i32 a0, a1, a2, a3, b0, b1, b2, b3;
        v4i32 e0, e1, e2;
        v4i32 f0, f1, f2;
        v4u32 t0, t1, t2, t3;
        v16i8 mask = {0, 1, 2, 3, 16, 17, 18, 19, 4, 5, 6, 7, 20, 21, 22, 23};
        int32_t value = 0xfefefefe;
        v4i32 fmask = {value, value, value, value};

        LD_SB8(src1, stride, d0, d1, d2, d3, d4, d5, d6, d7);
        VSHF_B2_SB(d0, d1, d2, d3, mask, mask, c0, c1);
        VSHF_B2_SB(d4, d5, d6, d7, mask, mask, c2, c3);
        a0 = (v4i32) __msa_pckev_d((v2i64)c1, (v2i64)c0);
        a2 = (v4i32) __msa_pckod_d((v2i64)c1, (v2i64)c0);
        a1 = (v4i32) __msa_pckev_d((v2i64)c3, (v2i64)c2);
        a3 = (v4i32) __msa_pckod_d((v2i64)c3, (v2i64)c2);

        LD_SB8(src2, stride, d0, d1, d2, d3, d4, d5, d6, d7);
        VSHF_B2_SB(d0, d1, d2, d3, mask, mask, c0, c1);
        VSHF_B2_SB(d4, d5, d6, d7, mask, mask, c2, c3);
        b0 = (v4i32) __msa_pckev_d((v2i64)c1, (v2i64)c0);
        b2 = (v4i32) __msa_pckod_d((v2i64)c1, (v2i64)c0);
        b1 = (v4i32) __msa_pckev_d((v2i64)c3, (v2i64)c2);
        b3 = (v4i32) __msa_pckod_d((v2i64)c3, (v2i64)c2);

        e0 = (v4i32) __msa_xor_v((v16u8)a0, (v16u8)b0);
        e0 = (v4i32) __msa_and_v((v16u8)e0, (v16u8)fmask);
        t0 = ((v4u32)e0) >> 1;
        e2 = (v4i32) __msa_and_v((v16u8)a0, (v16u8)b0);
        t0 = t0 + (v4u32)e2;

        e1 = (v4i32) __msa_xor_v((v16u8)a1, (v16u8)b1);
        e1 = (v4i32) __msa_and_v((v16u8)e1, (v16u8)fmask);
        t1 = ((v4u32)e1) >> 1;
        e2 = (v4i32) __msa_and_v((v16u8)a1, (v16u8)b1);
        t1 = t1 + (v4u32)e2;

        f0 = (v4i32) __msa_xor_v((v16u8)a2, (v16u8)b2);
        f0 = (v4i32) __msa_and_v((v16u8)f0, (v16u8)fmask);
        t2 = ((v4u32)f0) >> 1;
        f2 = (v4i32) __msa_and_v((v16u8)a2, (v16u8)b2);
        t2 = t2 + (v4u32)f2;

        f1 = (v4i32) __msa_xor_v((v16u8)a3, (v16u8)b3);
        f1 = (v4i32) __msa_and_v((v16u8)f1, (v16u8)fmask);
        t3 = ((v4u32)f1) >> 1;
        f2 = (v4i32) __msa_and_v((v16u8)a3, (v16u8)b3);
        t3 = t3 + (v4u32)f2;

        ST_W8(t0, t1, 0, 1, 2, 3, 0, 1, 2, 3, dst, stride);
        ST_W8(t2, t3, 0, 1, 2, 3, 0, 1, 2, 3, dst + 4, stride);
    } else {
        int i;

        for (i = 0; i < h; i++) {
            uint32_t a, b;

            a = AV_RN32(&src1[i * stride]);
            b = AV_RN32(&src2[i * stride]);
            AV_WN32A(&dst[i * stride], no_rnd_avg32(a, b));
            a = AV_RN32(&src1[i * stride + 4]);
            b = AV_RN32(&src2[i * stride + 4]);
            AV_WN32A(&dst[i * stride + 4], no_rnd_avg32(a, b));
        }
    }
}
