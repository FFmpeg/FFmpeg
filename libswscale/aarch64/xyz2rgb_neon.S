/*
 * Copyright (c) 2025 Arpad Panyik <Arpad.Panyik@arm.com>
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

#include "libavutil/aarch64/asm.S"
#include "asm-offsets.h"

#define JUMP_ALIGN 2
#define LOOP_ALIGN 2

function ff_xyz12Torgb48le_neon_asm, export=1
// x0  const SwsColorXform *c
// x1  uint8_t *dst
// w2  int dst_stride
// x3  const uint8_t *src
// w4  int src_stride
// w5  int w
// w6  int h

        ldp             x7,  x8, [x0, #(SCX_GAMMA_IN)]  // gamma.in, gamma.out
        ldr             q6,  [x0, #(SCX_MAT_00)]        // mat[0][0]..[2][1]
        ldr             h7,  [x0, #(SCX_MAT_22)]        // mat[2][2]; > 0

        add             w9,  w5,  w5, lsl #1        // w * 3
        add             x17, x3,  w4, sxtw          // sr2 = src + src_stride
        add             x16, x1,  w2, sxtw          // ds2 = dst + dst_stride
        sub             w4,  w4,  w9                // src_stride - w * 3
        sub             w2,  w2,  w9                // dst_stride - w * 3
        abs             v6.8h,  v6.8h               // abs(mat[0][0]..[2][1])
        sbfiz           x4,  x4,  #1, #32           // src_stride * 2 - w * 6
        sbfiz           x2,  x2,  #1, #32           // dst_stride * 2 - w * 6

        subs            w6,  w6,  #2
        b.lt            6f                          // h < 2

        stp             x19, x20, [sp, #-64]!
        stp             x21, x22, [sp, #16]
        stp             x23, x24, [sp, #32]
        str             x25, [sp, #48]

        .align LOOP_ALIGN
1:      // yp loop for 2x4 pixels
        subs            w0,  w5,  #4
        b.lt            3f                          // w < 4

        .align LOOP_ALIGN
2:      // xp loop for 2x4 pixels: XYZ0[0..3], XYZ1[0..3]
        ldp             x9,  x10, [x3]              // x9  = X0[0] Y0[0] Z0[0] X0[1], x10 = Y0[1] Z0[1] X0[2] Y0[2]
        ldr             x11, [x3, #16]              // x11 = Z0[2] X0[3] Y0[3] Z0[3]
        add             x3,  x3,  #24
        ubfx            x12, x9,  #4,  #12          // X0[0] >> 4
        lsr             x13, x9,  #52               // X0[1] >> 4
        ubfx            x14, x10, #36, #12          // X0[2] >> 4
        ubfx            x15, x11, #20, #12          // X0[3] >> 4

        ldp             x19, x20, [x17]             // x19 = X1[0] Y1[0] Z1[0] X1[1], x20 = Y1[1] Z1[1] X1[2] Y1[2]
        ldr             x21, [x17, #16]             // x21 = Z1[2] X1[3] Y1[3] Z1[3]
        add             x17, x17, #24
        ubfx            x22, x19, #4, #12           // X1[0] >> 4
        lsr             x23, x19, #52               // X1[1] >> 4
        ubfx            x24, x20, #36, #12          // X1[2] >> 4
        ubfx            x25, x21, #20, #12          // X1[3] >> 4

        ldr             h0,  [x7, x12, lsl #1]      // gamma.in[X0[0] >> 4]
        ubfx            x12, x9,  #20, #12          // Y0[0] >> 4
        ldr             h16, [x7, x13, lsl #1]      // gamma.in[X0[1] >> 4]
        ubfx            x13, x10, #4, #12           // Y0[1] >> 4
        ldr             h17, [x7, x14, lsl #1]      // gamma.in[X0[2] >> 4]
        lsr             x14, x10, #52               // Y0[2] >> 4
        ldr             h18, [x7, x15, lsl #1]      // gamma.in[X0[3] >> 4]
        ubfx            x15, x11, #36, #12          // Y0[3] >> 4

        ldr             h20, [x7, x22, lsl #1]      // gamma.in[X1[0] >> 4]
        ubfx            x22, x19, #20, #12          // Y1[0] >> 4
        ldr             h26, [x7, x23, lsl #1]      // gamma.in[X1[1] >> 4]
        ubfx            x23, x20, #4,  #12          // Y1[1] >> 4
        ldr             h27, [x7, x24, lsl #1]      // gamma.in[X1[2] >> 4]
        lsr             x24, x20, #52               // Y1[2] >> 4
        ldr             h28, [x7, x25, lsl #1]      // gamma.in[X1[3] >> 4]
        ubfx            x25, x21, #36, #12          // Y1[3] >> 4

        mov             v0.h[1],  v16.h[0]          // v0.4h  = gamma.in[X0[0..1] >> 4]
        mov             v17.h[1], v18.h[0]          // v17.4h = gamma.in[X0[2..3] >> 4]
        mov             v0.s[1],  v17.s[0]          // v0.4h  = gamma.in[X0[0..3] >> 4]
        ldr             h1,  [x7, x12, lsl #1]      // gamma.in[Y0[0] >> 4]
        umull           v3.4s, v0.4h, v6.h[0]       // R0[0..3] = gamma.in[X0[0..3] >> 4] * mat[0][0]
        umull           v5.4s, v0.4h, v6.h[6]       // B0[0..3] = gamma.in[X0[0..3] >> 4] * mat[2][0]
        ubfx            x12, x9,  #36, #12          // Z0[0] >> 4
        ldr             h16, [x7, x13, lsl #1]      // gamma.in[Y0[1] >> 4]

        mov             v20.h[1], v26.h[0]          // v20.4h = gamma.in[X1[0..1] >> 4]
        mov             v27.h[1], v28.h[0]          // v27.4h = gamma.in[X1[2..3] >> 4]
        mov             v20.s[1], v27.s[0]          // v20.4h = gamma.in[X1[0..3] >> 4]
        ldr             h21, [x7, x22, lsl #1]      // gamma.in[Y1[0] >> 4]
        umull           v23.4s, v20.4h, v6.h[0]     // R1[0..3] = gamma.in[X1[0..3] >> 4] * mat[0][0]
        umull           v25.4s, v20.4h, v6.h[6]     // B1[0..3] = gamma.in[X1[0..3] >> 4] * mat[2][0]
        ubfx            x22, x19, #36, #12          // Z1[0] >> 4
        ldr             h26, [x7, x23, lsl #1]      // gamma.in[Y1[1] >> 4]

        ubfx            x13, x10, #20, #12          // Z0[1] >> 4
        ldr             h17, [x7, x14, lsl #1]      // gamma.in[Y0[2] >> 4]
        ubfx            x14, x11, #4,  #12          // Z0[2] >> 4
        ldr             h18, [x7, x15, lsl #1]      // gamma.in[Y0[3] >> 4]
        lsr             x15, x11, #52               // Z0[3] >> 4
        mov             v1.h[1],  v16.h[0]          // v1.4h  = gamma.in[Y0[0..1] >> 4]
        mov             v17.h[1], v18.h[0]          // v17.4h = gamma.in[Y0[2..3] >> 4]
        mov             v1.s[1],  v17.s[0]          // v1.4h  = gamma.in[Y0[0..3] >> 4]

        ubfx            x23, x20, #20, #12          // Z1[1] >> 4
        ldr             h27, [x7, x24, lsl #1]      // gamma.in[Y1[2] >> 4]
        ubfx            x24, x21, #4,  #12          // Z1[2] >> 4
        ldr             h28, [x7, x25, lsl #1]      // gamma.in[Y1[3] >> 4]
        umull           v4.4s,  v1.4h,  v6.h[4]     // G0[0..3]  = gamma.in[Y0[0..3] >> 4] * mat[1][1]
        umlsl           v3.4s,  v1.4h,  v6.h[1]     // R0[0..3] -= gamma.in[Y0[0..3] >> 4] * mat[0][1]

        lsr             x25, x21, #52               // Z1[3] >> 4
        mov             v21.h[1], v26.h[0]          // v21.4h = gamma.in[Y1[0..1] >> 4]
        mov             v27.h[1], v28.h[0]          // v27.4h = gamma.in[Y1[2..3] >> 4]
        mov             v21.s[1], v27.s[0]          // v21.4h = gamma.in[Y1[0..3] >> 4]
        umlsl           v4.4s,  v0.4h,  v6.h[3]     // G0[0..3] -= gamma.in[X0[0..3] >> 4] * mat[1][0]
        umlsl           v5.4s,  v1.4h,  v6.h[7]     // B0[0..3] -= gamma.in[Y0[0..3] >> 4] * mat[2][1]

        ldr             h2,  [x7, x12, lsl #1]      // gamma.in[Z0[0] >> 4]
        ldr             h16, [x7, x13, lsl #1]      // gamma.in[Z0[1] >> 4]
        ldr             h17, [x7, x14, lsl #1]      // gamma.in[Z0[2] >> 4]
        ldr             h18, [x7, x15, lsl #1]      // gamma.in[Z0[3] >> 4]
        umull           v24.4s, v21.4h, v6.h[4]     // G1[0..3]  = gamma.in[Y1[0..3] >> 4] * mat[1][1]
        umlsl           v23.4s, v21.4h, v6.h[1]     // R1[0..3] -= gamma.in[Y1[0..3] >> 4] * mat[0][1]

        mov             v2.h[1],  v16.h[0]          // v2.4h  = gamma.in[Z0[0..1] >> 4]
        mov             v17.h[1], v18.h[0]          // v17.4h = gamma.in[Z0[2..3] >> 4]
        mov             v2.s[1],  v17.s[0]          // v2.4h  = gamma.in[Z0[0..3] >> 4]
        umlsl           v24.4s, v20.4h, v6.h[3]     // G1[0..3] -= gamma.in[X1[0..3] >> 4] * mat[1][0]
        umlsl           v25.4s, v21.4h, v6.h[7]     // B1[0..3] -= gamma.in[Y1[0..3] >> 4] * mat[2][1]

        ldr             h22, [x7, x22, lsl #1]      // gamma.in[Z1[0] >> 4]
        ldr             h26, [x7, x23, lsl #1]      // gamma.in[Z1[1] >> 4]
        ldr             h27, [x7, x24, lsl #1]      // gamma.in[Z1[2] >> 4]
        ldr             h28, [x7, x25, lsl #1]      // gamma.in[Z1[3] >> 4]
        mov             v22.h[1], v26.h[0]          // v22.4h = gamma.in[Z1[0..1] >> 4]
        mov             v27.h[1], v28.h[0]          // v27.4h = gamma.in[Z1[2..3] >> 4]
        mov             v22.s[1], v27.s[0]          // v22.4h = gamma.in[Z1[0..3] >> 4]

        umlsl           v3.4s,  v2.4h,  v6.h[2]     // R0[0..3] -= gamma.in[Z0[0..3] >> 4] * mat[0][2]
        sqshrun         v3.4h,  v3.4s,  #12         // clip(R0[0..3] >> 12)
        umlal           v4.4s,  v2.4h,  v6.h[5]     // G0[0..3] += gamma.in[Z0[0..3] >> 4] * mat[1][2]
        sqshrun         v4.4h,  v4.4s,  #12         // clip(G0[0..3] >> 12)
        umov            w9,  v3.h[0]                // clip(R0[0] >> 12)
        umov            w10, v4.h[1]                // clip(G0[1] >> 12)
        umlal           v5.4s,  v2.4h,  v7.h[0]     // B0[0..3] += gamma.in[Z0[0..3] >> 4] * mat[2][2]
        sqshrun         v5.4h,  v5.4s,  #12         // clip(B0[0..3] >> 12)

        umlsl           v23.4s, v22.4h, v6.h[2]     // R1[0..3] -= gamma.in[Z1[0..3] >> 4] * mat[0][2]
        sqshrun         v23.4h, v23.4s, #12         // clip(R1[0..3] >> 12)
        umlal           v24.4s, v22.4h, v6.h[5]     // G1[0..3] += gamma.in[Z1[0..3] >> 4] * mat[1][2]
        sqshrun         v24.4h, v24.4s, #12         // clip(G1[0..3] >> 12)
        umov            w19, v23.h[0]               // clip(R1[0] >> 12)
        umov            w20, v24.h[1]               // clip(G1[1] >> 12)
        umlal           v25.4s, v22.4h, v7.h[0]     // B1[0..3] += gamma.in[Z1[0..3] >> 4] * mat[2][2]
        sqshrun         v25.4h, v25.4s, #12         // clip(B1[0..3] >> 12)

        umov            w11, v5.h[2]                // clip(B0[2] >> 12)
        umov            w12, v4.h[0]                // clip(G0[0] >> 12)
        ldrh            w9,  [x8, x9,  lsl #1]      // R0[0] = gamma.out[clip(R0[0] >> 12)]
        lsl             x9,  x9,  #4                // R0[0] << 4
        umov            w13, v5.h[1]                // clip(B0[1] >> 12)
        ldrh            w10, [x8, x10, lsl #1]      // G0[1] = gamma.out[clip(G0[1] >> 12)]
        lsl             x10, x10, #4                // G0[1] << 4

        umov            w21, v25.h[2]               // clip(B1[2] >> 12)
        umov            w22, v24.h[0]               // clip(G1[0] >> 12)
        ldrh            w19, [x8, x19, lsl #1]      // R1[0] = gamma.out[clip(R1[0] >> 12)]
        lsl             x19, x19, #4                // R1[0] << 4
        umov            w23, v25.h[1]               // clip(B1[1] >> 12)
        ldrh            w20, [x8, x20, lsl #1]      // G1[1] = gamma.out[clip(G1[1] >> 12)]
        lsl             x20, x20, #4                // G1[1] << 4

        umov            w14, v3.h[3]                // clip(R0[3] >> 12)
        ldrh            w11, [x8, x11, lsl #1]      // B0[2] = gamma.out[clip(B0[2] >> 12)]
        lsl             x11, x11, #4                // B0[2] << 4
        umov            w15, v5.h[0]                // clip(B0[0] >> 12)
        ldrh            w12, [x8, x12, lsl #1]      // G0[0] = gamma.out[clip(G0[0] >> 12)]
        orr             x9,  x9,  x12, lsl #20      // R0[0] << 4, G0[0] << 4
        umov            w12, v3.h[2]                // clip(R0[2] >> 12)
        ldrh            w13, [x8, x13, lsl #1]      // B0[1] = gamma.out[clip(B0[1] >> 12)]

        umov            w24, v23.h[3]               // clip(R1[3] >> 12)
        ldrh            w21, [x8, x21, lsl #1]      // B1[2] = gamma.out[clip(B1[2] >> 12)]
        lsl             x21, x21, #4                // B1[2] << 4
        umov            w25, v25.h[0]               // clip(B1[0] >> 12)
        ldrh            w22, [x8, x22, lsl #1]      // G1[0] = gamma.out[clip(G1[0] >> 12)]
        orr             x19, x19, x22, lsl #20      // R1[0] << 4, G1[0] << 4
        umov            w22, v23.h[2]               // clip(R1[2] >> 12)
        ldrh            w23, [x8, x23, lsl #1]      // B1[1] = gamma.out[clip(B1[1] >> 12)]

        orr             x10, x10, x13, lsl #20      // G0[1] << 4, B0[1] << 4
        umov            w13, v4.h[3]                // clip(G0[3] >> 12)
        ldrh            w14, [x8, x14, lsl #1]      // R0[3] = gamma.out[clip(R0[3] >> 12)]
        orr             x11, x11, x14, lsl #20      // B0[2] << 4, R0[3] << 4
        umov            w14, v3.h[1]                // clip(R0[1] >> 12)
        ldrh            w15, [x8, x15, lsl #1]      // B0[0] = gamma.out[clip(B0[0] >> 12)]
        orr             x9,  x9,  x15, lsl #36      // R0[0] << 4, G0[0] << 4, B0[0] << 4
        umov            w15, v4.h[2]                // clip(G0[2] >> 12)

        orr             x20, x20, x23, lsl #20      // G1[1] << 4, B1[1] << 4
        umov            w23, v24.h[3]               // clip(G1[3] >> 12)
        ldrh            w24, [x8, x24, lsl #1]      // R1[3] = gamma.out[clip(R1[3] >> 12)]
        orr             x21, x21, x24, lsl #20      // B1[2] << 4, R1[3] << 4
        umov            w24, v23.h[1]               // clip(R1[1] >> 12)
        ldrh            w25, [x8, x25, lsl #1]      // B1[0] = gamma.out[clip(B1[0] >> 12)]
        orr             x19, x19, x25, lsl #36      // R1[0] << 4, G1[0] << 4, B1[0] << 4
        umov            w25, v24.h[2]               // clip(G1[2] >> 12)

        ldrh            w12, [x8, x12, lsl #1]      // R0[2] = gamma.out[clip(R0[2] >> 12)]
        orr             x10, x10, x12, lsl #36      // G0[1] << 4, B0[1] << 4, R0[2] << 4
        umov            w12, v5.h[3]                // clip(B0[3] >> 12)
        ldrh            w13, [x8, x13, lsl #1]      // G0[3] = gamma.out[clip(G0[3] >> 12)]
        orr             x11, x11, x13, lsl #36      // B0[2] << 4, R0[3] << 4, G0[3] << 4
        ldrh            w14, [x8, x14, lsl #1]      // R0[1] = gamma.out[clip(R0[1] >> 12)]
        orr             x9,  x9,  x14, lsl #52      // x9  = R0[0] << 4, G0[0] << 4, B0[0] << 4, R0[1] << 4
        ldrh            w15, [x8, x15, lsl #1]      // G0[2] = gamma.out[clip(G0[2] >> 12)]
        orr             x10, x10, x15, lsl #52      // x10 = G0[1] << 4, B0[1] << 4, R0[2] << 4, G0[2] << 4
        ldrh            w12, [x8, x12, lsl #1]      // B0[3] = gamma.out[clip(B0[3] >> 12)]
        orr             x11, x11, x12, lsl #52      // x11 = B0[2] << 4, R0[3] << 4, G0[3] << 4, B0[3] << 4
        stp             x9,  x10, [x1]
        str             x11, [x1, #16]

        ldrh            w22, [x8, x22, lsl #1]      // R1[2] = gamma.out[clip(R1[2] >> 12)]
        orr             x20, x20, x22, lsl #36      // G1[1] << 4, B1[1] << 4, R1[2] << 4
        umov            w22, v25.h[3]               // clip(B1[3] >> 12)
        ldrh            w23, [x8, x23, lsl #1]      // G1[3] = gamma.out[clip(G1[3] >> 12)]
        orr             x21, x21, x23, lsl #36      // B1[2] << 4, R1[3] << 4, G1[3] << 4
        ldrh            w24, [x8, x24, lsl #1]      // R1[1] = gamma.out[clip(R1[1] >> 12)]
        orr             x19, x19, x24, lsl #52      // x19 = R1[0] << 4, G1[0] << 4, B1[0] << 4, R1[1] << 4
        ldrh            w25, [x8, x25, lsl #1]      // G1[2] = gamma.out[clip(G1[2] >> 12)]
        orr             x20, x20, x25, lsl #52      // x20 = G1[1] << 4, B1[1] << 4, R1[2] << 4, G1[2] << 4
        ldrh            w22, [x8, x22, lsl #1]      // B1[3] = gamma.out[clip(B1[3] >> 12)]
        orr             x21, x21, x22, lsl #52      // x21 = B1[2] << 4, R1[3] << 4, G1[3] << 4, B1[3] << 4
        stp             x19, x20, [x16]
        str             x21, [x16, #16]

        add             x1,  x1,  #24
        add             x16, x16, #24

        subs            w0,  w0,  #4
        b.ge            2b

        .align JUMP_ALIGN
3:
        tst             w5,  #3
        b.eq            5f                          // no residual pixels; (w & 3) == 0

        ldr             w10, [x3]                   // w10 = X0[0] Y0[0]
        ldrh            w11, [x3, #4]               // w11 = Z0[0]
        add             x3,  x3,  #6
        ldr             w20, [x17]                  // w20 = X1[0] Y1[0]
        ldrh            w21, [x17, #4]              // w21 = Z1[0]
        add             x17, x17, #6
        ubfx            w9,  w10, #4,  #12          // X0[0] >> 4
        ubfx            w10, w10, #20, #12          // Y0[0] >> 4
        lsr             w11, w11, #4                // Z0[0] >> 4
        ldr             h0,  [x7, x9,  lsl #1]      // v0.4h = gamma.in[X0[0] >> 4]
        ldr             h1,  [x7, x10, lsl #1]      // v1.4h = gamma.in[Y0[0] >> 4]
        ldr             h2,  [x7, x11, lsl #1]      // v2.4h = gamma.in[Z0[0] >> 4]
        ubfx            w19, w20, #4,  #12          // X1[0] >> 4
        ubfx            w20, w20, #20, #12          // Y1[0] >> 4
        lsr             w21, w21, #4                // Z1[0] >> 4
        ldr             h20, [x7, x19, lsl #1]      // v20.4h = gamma.in[X1[0] >> 4]
        ldr             h21, [x7, x20, lsl #1]      // v21.4h = gamma.in[Y1[0] >> 4]
        ldr             h22, [x7, x21, lsl #1]      // v22.4h = gamma.in[Z1[0] >> 4]

        cmp             w0,  #-2
        b.lt            4f                          // (w & 3) == 1

        ldr             w10, [x3]                   // w10 = X0[1] Y0[1]
        ldrh            w11, [x3, #4]               // w11 = Z0[1]
        add             x3,  x3,  #6
        ldr             w20, [x17]                  // w20 = X1[1] Y1[1]
        ldrh            w21, [x17, #4]              // w21 = Z1[1]
        add             x17, x17,  #6
        ubfx            w9,  w10, #4,  #12          // X0[1] >> 4
        ubfx            w10, w10, #20, #12          // Y0[1] >> 4
        lsr             w11, w11, #4                // Z0[1] >> 4
        ldr             h16, [x7, x9,  lsl #1]      // gamma.in[X0[1] >> 4]
        ldr             h17, [x7, x10, lsl #1]      // gamma.in[Y0[1] >> 4]
        ldr             h18, [x7, x11, lsl #1]      // gamma.in[Z0[1] >> 4]
        ubfx            w19, w20, #4,  #12          // X1[1] >> 4
        ubfx            w20, w20, #20, #12          // Y1[1] >> 4
        lsr             w21, w21, #4                // Z1[1] >> 4
        ldr             h23, [x7, x19, lsl #1]      // gamma.in[X1[1] >> 4]
        ldr             h24, [x7, x20, lsl #1]      // gamma.in[Y1[1] >> 4]
        ldr             h25, [x7, x21, lsl #1]      // gamma.in[Z1[1] >> 4]
        mov             v0.h[1],  v16.h[0]          // v0.4h = gamma.in[X0[0..1] >> 4]
        mov             v1.h[1],  v17.h[0]          // v1.4h = gamma.in[Y0[0..1] >> 4]
        mov             v2.h[1],  v18.h[0]          // v2.4h = gamma.in[Z0[0..1] >> 4]
        mov             v20.h[1], v23.h[0]          // v20.4h = gamma.in[X1[0..1] >> 4]
        mov             v21.h[1], v24.h[0]          // v21.4h = gamma.in[Y1[0..1] >> 4]
        mov             v22.h[1], v25.h[0]          // v22.4h = gamma.in[Z1[0..1] >> 4]

        b.le            4f                          // (w & 3) == 2

        ldr             w10, [x3]                   // w10 = X0[2] Y0[2]
        ldrh            w11, [x3, #4]               // w11 = Z0[2]
        add             x3,  x3,  #6
        ldr             w20, [x17]                  // w20 = X1[2] Y1[2]
        ldrh            w21, [x17, #4]              // w21 = Z1[2]
        add             x17, x17, #6
        ubfx            w9,  w10, #4,  #12          // X0[2] >> 4
        ubfx            w10, w10, #20, #12          // Y0[2] >> 4
        lsr             w11, w11, #4                // Z0[2] >> 4
        ldr             h16, [x7, x9,  lsl #1]      // gamma.in[X0[2] >> 4]
        ldr             h17, [x7, x10, lsl #1]      // gamma.in[Y0[2] >> 4]
        ldr             h18, [x7, x11, lsl #1]      // gamma.in[Z0[2] >> 4]
        ubfx            w19, w20, #4,  #12          // X1[2] >> 4
        ubfx            w20, w20, #20, #12          // Y1[2] >> 4
        lsr             w21, w21, #4                // Z1[2] >> 4
        ldr             h23, [x7, x19, lsl #1]      // gamma.in[X1[2] >> 4]
        ldr             h24, [x7, x20, lsl #1]      // gamma.in[Y1[2] >> 4]
        ldr             h25, [x7, x21, lsl #1]      // gamma.in[Z1[2] >> 4]
        mov             v0.h[2],  v16.h[0]          // v0.4h = gamma.in[X0[0..2] >> 4]
        mov             v1.h[2],  v17.h[0]          // v1.4h = gamma.in[Y0[0..2] >> 4]
        mov             v2.h[2],  v18.h[0]          // v2.4h = gamma.in[Z0[0..2] >> 4]
        mov             v20.h[2], v23.h[0]          // v20.4h = gamma.in[X1[0..2] >> 4]
        mov             v21.h[2], v24.h[0]          // v21.4h = gamma.in[Y1[0..2] >> 4]
        mov             v22.h[2], v25.h[0]          // v22.4h = gamma.in[Z1[0..2] >> 4]

        .align JUMP_ALIGN
4:
        umull           v3.4s,  v0.4h,  v6.h[0]     // R0[0..2] = gamma.in[X0[0..2] >> 4] * mat[0][0]
        umull           v5.4s,  v0.4h,  v6.h[6]     // B0[0..2] = gamma.in[X0[0..2] >> 4] * mat[2][0]

        umull           v23.4s, v20.4h, v6.h[0]     // R1[0..2] = gamma.in[X1[0..2] >> 4] * mat[0][0]
        umull           v25.4s, v20.4h, v6.h[6]     // B1[0..2] = gamma.in[X1[0..2] >> 4] * mat[2][0]

        umull           v4.4s,  v1.4h,  v6.h[4]     // G0[0..2]  = gamma.in[Y0[0..2] >> 4] * mat[1][1]
        umlsl           v3.4s,  v1.4h,  v6.h[1]     // R0[0..2] -= gamma.in[Y0[0..2] >> 4] * mat[0][1]
        umlsl           v4.4s,  v0.4h,  v6.h[3]     // G0[0..2] -= gamma.in[X0[0..2] >> 4] * mat[1][0]
        umlsl           v5.4s,  v1.4h,  v6.h[7]     // B0[0..2] -= gamma.in[Y0[0..2] >> 4] * mat[2][1]

        umull           v24.4s, v21.4h, v6.h[4]     // G1[0..2]  = gamma.in[Y1[0..2] >> 4] * mat[1][1]
        umlsl           v23.4s, v21.4h, v6.h[1]     // R1[0..2] -= gamma.in[Y1[0..2] >> 4] * mat[0][1]
        umlsl           v24.4s, v20.4h, v6.h[3]     // G1[0..2] -= gamma.in[X1[0..2] >> 4] * mat[1][0]
        umlsl           v25.4s, v21.4h, v6.h[7]     // B1[0..2] -= gamma.in[Y1[0..2] >> 4] * mat[2][1]

        umlsl           v3.4s,  v2.4h,  v6.h[2]     // R0[0..2] -= gamma.in[Z0[0..2] >> 4] * mat[0][2]
        sqshrun         v3.4h,  v3.4s,  #12         // clip(R0[0..2] >> 12)
        umlal           v4.4s,  v2.4h,  v6.h[5]     // G0[0..2] += gamma.in[Z0[0..2] >> 4] * mat[1][2]
        sqshrun         v4.4h,  v4.4s,  #12         // clip(G0[0..2] >> 12)
        umlal           v5.4s,  v2.4h,  v7.h[0]     // B0[0..2] += gamma.in[Z0[0..2] >> 4] * mat[2][2]
        sqshrun         v5.4h,  v5.4s,  #12         // clip(B0[0..2] >> 12)

        umlsl           v23.4s, v22.4h, v6.h[2]     // R1[0..2] -= gamma.in[Z1[0..2] >> 4] * mat[0][2]
        sqshrun         v23.4h, v23.4s, #12         // clip(R1[0..2] >> 12)
        umlal           v24.4s, v22.4h, v6.h[5]     // G1[0..2] += gamma.in[Z1[0..2] >> 4] * mat[1][2]
        sqshrun         v24.4h, v24.4s, #12         // clip(G1[0..2] >> 12)
        umlal           v25.4s, v22.4h, v7.h[0]     // B1[0..2] += gamma.in[Z1[0..2] >> 4] * mat[2][2]
        sqshrun         v25.4h, v25.4s, #12         // clip(B1[0..2] >> 12)

        umov            w9,  v3.h[0]                // clip(R0[0] >> 12)
        umov            w10, v4.h[0]                // clip(G0[0] >> 12)
        umov            w11, v5.h[0]                // clip(B0[0] >> 12)
        ldrh            w9,  [x8, x9,  lsl #1]      // R0[0] = gamma.out[clip(R0[0] >> 12)]
        ldrh            w10, [x8, x10, lsl #1]      // G0[0] = gamma.out[clip(G0[0] >> 12)]
        ldrh            w11, [x8, x11, lsl #1]      // B0[0] = gamma.out[clip(B0[0] >> 12)]
        umov            w19, v23.h[0]               // clip(R1[0] >> 12)
        umov            w20, v24.h[0]               // clip(G1[0] >> 12)
        umov            w21, v25.h[0]               // clip(B1[0] >> 12)
        ldrh            w19, [x8, x19, lsl #1]      // R1[0] = gamma.out[clip(R1[0] >> 12)]
        ldrh            w20, [x8, x20, lsl #1]      // G1[0] = gamma.out[clip(G1[0] >> 12)]
        ldrh            w21, [x8, x21, lsl #1]      // B1[0] = gamma.out[clip(B1[0] >> 12)]
        lsl             w9,  w9,  #4                // w9  = R0[0] << 4
        lsl             w10, w10, #4                // w10 = G0[0] << 4
        lsl             w11, w11, #4                // w11 = B0[0] << 4
        strh            w9,  [x1]
        strh            w10, [x1, #2]
        strh            w11, [x1, #4]
        lsl             w19, w19, #4                // w19 = R1[0] << 4
        lsl             w20, w20, #4                // w20 = G1[0] << 4
        lsl             w21, w21, #4                // w21 = B1[0] << 4
        strh            w19, [x16]
        strh            w20, [x16, #2]
        strh            w21, [x16, #4]
        add             x1,  x1,  #6
        add             x16, x16, #6

        cmp             w0,  #-2
        b.lt            5f                          // (w & 3) == 1

        umov            w9,  v3.h[1]                // clip(R0[1] >> 12)
        umov            w10, v4.h[1]                // clip(G0[1] >> 12)
        umov            w11, v5.h[1]                // clip(B0[1] >> 12)
        ldrh            w9,  [x8, x9,  lsl #1]      // R0[1] = gamma.out[clip(R0[1] >> 12)]
        ldrh            w10, [x8, x10, lsl #1]      // G0[1] = gamma.out[clip(G0[1] >> 12)]
        ldrh            w11, [x8, x11, lsl #1]      // B0[1] = gamma.out[clip(B0[1] >> 12)]
        umov            w19, v23.h[1]               // clip(R1[1] >> 12)
        umov            w20, v24.h[1]               // clip(G1[1] >> 12)
        umov            w21, v25.h[1]               // clip(B1[1] >> 12)
        ldrh            w19, [x8, x19, lsl #1]      // R1[1] = gamma.out[clip(R1[1] >> 12)]
        ldrh            w20, [x8, x20, lsl #1]      // G1[1] = gamma.out[clip(G1[1] >> 12)]
        ldrh            w21, [x8, x21, lsl #1]      // B1[1] = gamma.out[clip(B1[1] >> 12)]
        lsl             w9,  w9,  #4                // w9  = R0[1] << 4
        lsl             w10, w10, #4                // w10 = G0[1] << 4
        lsl             w11, w11, #4                // w11 = B0[1] << 4
        strh            w9,  [x1]
        strh            w10, [x1, #2]
        strh            w11, [x1, #4]
        lsl             w19, w19, #4                // w19 = R1[1] << 4
        lsl             w20, w20, #4                // w20 = G1[1] << 4
        lsl             w21, w21, #4                // w21 = B1[1] << 4
        strh            w19, [x16]
        strh            w20, [x16, #2]
        strh            w21, [x16, #4]
        add             x1,  x1,  #6
        add             x16, x16, #6

        b.le            5f                          // (w & 3) == 2

        umov            w9,  v3.h[2]                // clip(R0[2] >> 12)
        umov            w10, v4.h[2]                // clip(G0[2] >> 12)
        umov            w11, v5.h[2]                // clip(B0[2] >> 12)
        ldrh            w9,  [x8, x9,  lsl #1]      // R0[2] = gamma.out[clip(R0[2] >> 12)]
        ldrh            w10, [x8, x10, lsl #1]      // G0[2] = gamma.out[clip(G0[2] >> 12)]
        ldrh            w11, [x8, x11, lsl #1]      // B0[2] = gamma.out[clip(B0[2] >> 12)]
        umov            w19, v23.h[2]               // clip(R1[2] >> 12)
        umov            w20, v24.h[2]               // clip(G1[2] >> 12)
        umov            w21, v25.h[2]               // clip(B1[2] >> 12)
        ldrh            w19, [x8, x19, lsl #1]      // R1[2] = gamma.out[clip(R1[2] >> 12)]
        ldrh            w20, [x8, x20, lsl #1]      // G1[2] = gamma.out[clip(G1[2] >> 12)]
        ldrh            w21, [x8, x21, lsl #1]      // B1[2] = gamma.out[clip(B1[2] >> 12)]
        lsl             w9,  w9,  #4                // w9  = R0[2] << 4
        lsl             w10, w10, #4                // w10 = G0[2] << 4
        lsl             w11, w11, #4                // w11 = B0[2] << 4
        strh            w9,  [x1]
        strh            w10, [x1, #2]
        strh            w11, [x1, #4]
        lsl             w19, w19, #4                // w19 = R1[2] << 4
        lsl             w20, w20, #4                // w20 = G1[2] << 4
        lsl             w21, w21, #4                // w21 = B1[2] << 4
        strh            w19, [x16]
        strh            w20, [x16, #2]
        strh            w21, [x16, #4]
        add             x1,  x1,  #6
        add             x16, x16, #6

        .align JUMP_ALIGN
5:
        add             x3,  x3,  x4
        add             x17, x17, x4
        add             x1,  x1,  x2
        add             x16, x16, x2

        subs            w6,  w6,  #2
        b.ge            1b

        ldp             x21, x22, [sp, #16]
        ldp             x23, x24, [sp, #32]
        ldr             x25, [sp, #48]
        ldp             x19, x20, [sp], #64

        .align JUMP_ALIGN
6:
        tbz             w6,  #0,  10f               // even number of lines; (h & 1) == 0

        subs            w0,  w5,  #4
        b.lt            8f                          // w < 4

        .align LOOP_ALIGN
7:      // loop for last odd line by 4 pixels: XYZ[0..3]
        ldp             x9,  x10, [x3]              // x9  = X[0] Y[0] Z[0] X[1], x10 = Y[1] Z[1] X[2] Y[2]
        ldr             x11, [x3, #16]              // x11 = Z[2] X[3] Y[3] Z[3]
        add             x3,  x3,  #24

        ubfx            x12, x9,  #4,  #12          // X[0] >> 4
        lsr             x13, x9,  #52               // X[1] >> 4
        ubfx            x14, x10, #36, #12          // X[2] >> 4
        ubfx            x15, x11, #20, #12          // X[3] >> 4

        ldr             h0,  [x7, x12, lsl #1]      // gamma.in[X[0] >> 4]
        ubfx            x12, x9,  #20, #12          // Y[0] >> 4
        ldr             h16, [x7, x13, lsl #1]      // gamma.in[X[1] >> 4]
        ubfx            x13, x10, #4,  #12          // Y[1] >> 4
        ldr             h17, [x7, x14, lsl #1]      // gamma.in[X[2] >> 4]
        lsr             x14, x10, #52               // Y[2] >> 4
        ldr             h18, [x7, x15, lsl #1]      // gamma.in[X[3] >> 4]
        ubfx            x15, x11, #36, #12          // Y[3] >> 4
        mov             v0.h[1],  v16.h[0]          // v0.4h  = gamma.in[X[0..1] >> 4]
        mov             v17.h[1], v18.h[0]          // v17.4h = gamma.in[X[2..3] >> 4]
        mov             v0.s[1],  v17.s[0]          // v0.4h  = gamma.in[X[0..3] >> 4]

        umull           v3.4s,  v0.4h,  v6.h[0]     // R[0..3] = gamma.in[X[0..3] >> 4] * mat[0][0]
        umull           v5.4s,  v0.4h,  v6.h[6]     // B[0..3] = gamma.in[X[0..3] >> 4] * mat[2][0]

        ldr             h1,  [x7, x12, lsl #1]      // gamma.in[Y[0] >> 4]
        ubfx            x12, x9,  #36, #12          // Z[0] >> 4
        ldr             h16, [x7, x13, lsl #1]      // gamma.in[Y[1] >> 4]
        ubfx            x13, x10, #20, #12          // Z[1] >> 4
        ldr             h17, [x7, x14, lsl #1]      // gamma.in[Y[2] >> 4]
        ubfx            x14, x11, #4,  #12          // Z[2] >> 4
        ldr             h18, [x7, x15, lsl #1]      // gamma.in[Y[3] >> 4]
        lsr             x15, x11, #52               // Z[3] >> 4
        mov             v1.h[1],  v16.h[0]          // v1.4h  = gamma.in[Y[0..1] >> 4]
        mov             v17.h[1], v18.h[0]          // v17.4h = gamma.in[Y[2..3] >> 4]
        mov             v1.s[1],  v17.s[0]          // v1.4h  = gamma.in[Y[0..3] >> 4]

        umull           v4.4s,  v1.4h,  v6.h[4]     // G[0..3]  = gamma.in[Y[0..3] >> 4] * mat[1][1]
        umlsl           v3.4s,  v1.4h,  v6.h[1]     // R[0..3] -= gamma.in[Y[0..3] >> 4] * mat[0][1]
        umlsl           v4.4s,  v0.4h,  v6.h[3]     // G[0..3] -= gamma.in[X[0..3] >> 4] * mat[1][0]
        umlsl           v5.4s,  v1.4h,  v6.h[7]     // B[0..3] -= gamma.in[Y[0..3] >> 4] * mat[2][1]

        ldr             h2,  [x7, x12, lsl #1]      // gamma.in[Z[0] >> 4]
        ldr             h16, [x7, x13, lsl #1]      // gamma.in[Z[1] >> 4]
        ldr             h17, [x7, x14, lsl #1]      // gamma.in[Z[2] >> 4]
        ldr             h18, [x7, x15, lsl #1]      // gamma.in[Z[3] >> 4]
        mov             v2.h[1],  v16.h[0]          // v2.4h  = gamma.in[Z[0..1] >> 4]
        mov             v17.h[1], v18.h[0]          // v17.4h = gamma.in[Z[2..3] >> 4]
        mov             v2.s[1],  v17.s[0]          // v2.4h  = gamma.in[Z[0..3] >> 4]

        umlsl           v3.4s,  v2.4h,  v6.h[2]     // R[0..3] -= gamma.in[Z[0..3] >> 4] * mat[0][2]
        sqshrun         v3.4h,  v3.4s,  #12         // clip(R[0..3] >> 12)
        umlal           v4.4s,  v2.4h,  v6.h[5]     // G[0..3] += gamma.in[Z[0..3] >> 4] * mat[1][2]
        sqshrun         v4.4h,  v4.4s,  #12         // clip(G[0..3] >> 12)
        umlal           v5.4s,  v2.4h,  v7.h[0]     // B[0..3] += gamma.in[Z[0..3] >> 4] * mat[2][2]
        sqshrun         v5.4h,  v5.4s,  #12         // clip(B[0..3] >> 12)

        umov            w9,  v3.h[0]                // clip(R[0] >> 12)
        umov            w10, v4.h[1]                // clip(G[1] >> 12)
        umov            w11, v5.h[2]                // clip(B[2] >> 12)

        umov            w12, v4.h[0]                // clip(G[0] >> 12)
        ldrh            w9,  [x8, x9,  lsl #1]      // R[0] = gamma.out[clip(R[0] >> 12)]
        lsl             x9,  x9,  #4                // R[0] << 4
        umov            w13, v5.h[1]                // clip(B[1] >> 12)
        ldrh            w10, [x8, x10, lsl #1]      // G[1] = gamma.out[clip(G[1] >> 12)]
        lsl             x10, x10, #4                // G[1] << 4
        umov            w14, v3.h[3]                // clip(R[3] >> 12)
        ldrh            w11, [x8, x11, lsl #1]      // B[2] = gamma.out[clip(B[2] >> 12)]
        lsl             x11, x11, #4                // B[2] << 4

        umov            w15, v5.h[0]                // clip(B[0] >> 12)
        ldrh            w12, [x8, x12, lsl #1]      // G[0] = gamma.out[clip(G[0] >> 12)]
        orr             x9,  x9,  x12, lsl #20      // R[0] << 4, G[0] << 4
        umov            w12, v3.h[2]                // clip(R[2] >> 12)
        ldrh            w13, [x8, x13, lsl #1]      // B[1] = gamma.out[clip(B[1] >> 12)]
        orr             x10, x10, x13, lsl #20      // G[1] << 4, B[1] << 4
        umov            w13, v4.h[3]                // clip(G[3] >> 12)
        ldrh            w14, [x8, x14, lsl #1]      // R[3] = gamma.out[clip(R[3] >> 12)]
        orr             x11, x11, x14, lsl #20      // B[2] << 4, R[3] << 4

        umov            w14, v3.h[1]                // clip(R[1] >> 12)
        ldrh            w15, [x8, x15, lsl #1]      // B[0] = gamma.out[clip(B[0] >> 12)]
        orr             x9,  x9,  x15, lsl #36      // R[0] << 4, G[0] << 4, B[0] << 4
        umov            w15, v4.h[2]                // clip(G[2] >> 12)
        ldrh            w12, [x8, x12, lsl #1]      // R[2] = gamma.out[clip(R[2] >> 12)]
        orr             x10, x10, x12, lsl #36      // G[1] << 4, B[1] << 4, R[2] << 4
        umov            w12, v5.h[3]                // clip(B[3] >> 12)
        ldrh            w13, [x8, x13, lsl #1]      // G[3] = gamma.out[clip(G[3] >> 12)]
        orr             x11, x11, x13, lsl #36      // B[2] << 4, R[3] << 4, G[3] << 4

        ldrh            w14, [x8, x14, lsl #1]      // R[1] = gamma.out[clip(R[1] >> 12)]
        orr             x9,  x9,  x14, lsl #52      // x9  = R[0] << 4, G[0] << 4, B[0] << 4, R[1] << 4
        ldrh            w15, [x8, x15, lsl #1]      // G[2] = gamma.out[clip(G[2] >> 12)]
        orr             x10, x10, x15, lsl #52      // x10 = G[1] << 4, B[1] << 4, R[2] << 4, G[2] << 4
        ldrh            w12, [x8, x12, lsl #1]      // B[3] = gamma.out[clip(B[3] >> 12)]
        orr             x11, x11, x12, lsl #52      // x11 = B[2] << 4, R[3] << 4, G[3] << 4, B[3] << 4

        stp             x9,  x10, [x1]
        str             x11, [x1, #16]
        add             x1,  x1,  #24

        subs            w0,  w0,  #4
        b.ge            7b

        .align JUMP_ALIGN
8:
        tst             w5,  #3
        b.eq            10f                         // no residual pixels; (w & 3) == 0

        ldr             w10, [x3]                   // w10 = X[0] Y[0]
        ldrh            w11, [x3, #4]               // w11 = Z[0]
        add             x3,  x3,  #6
        ubfx            w9,  w10, #4,  #12          // X[0] >> 4
        ubfx            w10, w10, #20, #12          // Y[0] >> 4
        lsr             w11, w11, #4                // Z[0] >> 4
        ldr             h0,  [x7, x9,  lsl #1]      // v0.4h = gamma.in[X[0] >> 4]
        ldr             h1,  [x7, x10, lsl #1]      // v1.4h = gamma.in[Y[0] >> 4]
        ldr             h2,  [x7, x11, lsl #1]      // v2.4h = gamma.in[Z[0] >> 4]

        cmp             w0,  #-2
        b.lt            9f                          // (w & 3) == 1

        ldr             w10, [x3]                   // w10 = X[1] Y[1]
        ldrh            w11, [x3, #4]               // w11 = Z[1]
        add             x3,  x3,  #6
        ubfx            w9,  w10, #4, #12           // X[1] >> 4
        ubfx            w10, w10, #20, #12          // Y[1] >> 4
        lsr             w11, w11, #4                // Z[1] >> 4
        ldr             h16, [x7, x9,  lsl #1]      // gamma.in[X[1] >> 4]
        ldr             h17, [x7, x10, lsl #1]      // gamma.in[Y[1] >> 4]
        ldr             h18, [x7, x11, lsl #1]      // gamma.in[Z[1] >> 4]
        mov             v0.h[1], v16.h[0]           // v0.4h = gamma.in[X[0..1] >> 4]
        mov             v1.h[1], v17.h[0]           // v1.4h = gamma.in[Y[0..1] >> 4]
        mov             v2.h[1], v18.h[0]           // v2.4h = gamma.in[Z[0..1] >> 4]

        b.le            9f                          // (w & 3) == 2

        ldr             w10, [x3]                   // w10 = X[2] Y[2]
        ldrh            w11, [x3, #4]               // w11 = Z[2]
        add             x3,  x3,  #6
        ubfx            w9,  w10, #4, #12           // X[2] >> 4
        ubfx            w10, w10, #20, #12          // Y[2] >> 4
        lsr             w11, w11, #4                // Z[2] >> 4
        ldr             h16, [x7, x9,  lsl #1]      // gamma.in[X[2] >> 4]
        ldr             h17, [x7, x10, lsl #1]      // gamma.in[Y[2] >> 4]
        ldr             h18, [x7, x11, lsl #1]      // gamma.in[Z[2] >> 4]
        mov             v0.h[2], v16.h[0]           // v0.4h = gamma.in[X[0..2] >> 4]
        mov             v1.h[2], v17.h[0]           // v1.4h = gamma.in[Y[0..2] >> 4]
        mov             v2.h[2], v18.h[0]           // v2.4h = gamma.in[Z[0..2] >> 4]

        .align JUMP_ALIGN
9:
        umull           v3.4s,  v0.4h,  v6.h[0]     // R[0..2] = gamma.in[X[0..2] >> 4] * mat[0][0]
        umull           v5.4s,  v0.4h,  v6.h[6]     // B[0..2] = gamma.in[X[0..2] >> 4] * mat[2][0]

        umull           v4.4s,  v1.4h,  v6.h[4]     // G[0..2]  = gamma.in[Y[0..2] >> 4] * mat[1][1]
        umlsl           v3.4s,  v1.4h,  v6.h[1]     // R[0..2] -= gamma.in[Y[0..2] >> 4] * mat[0][1]
        umlsl           v4.4s,  v0.4h,  v6.h[3]     // G[0..2] -= gamma.in[X[0..2] >> 4] * mat[1][0]
        umlsl           v5.4s,  v1.4h,  v6.h[7]     // B[0..2] -= gamma.in[Y[0..2] >> 4] * mat[2][1]

        umlsl           v3.4s,  v2.4h,  v6.h[2]     // R[0..2] -= gamma.in[Z[0..2] >> 4] * mat[0][2]
        sqshrun         v3.4h,  v3.4s,  #12         // clip(R[0..2] >> 12)
        umlal           v4.4s,  v2.4h,  v6.h[5]     // G[0..2] += gamma.in[Z[0..2] >> 4] * mat[1][2]
        sqshrun         v4.4h,  v4.4s,  #12         // clip(G[0..2] >> 12)
        umlal           v5.4s,  v2.4h,  v7.h[0]     // B[0..2] += gamma.in[Z[0..2] >> 4] * mat[2][2]
        sqshrun         v5.4h,  v5.4s,  #12         // clip(B[0..2] >> 12)

        umov            w9,  v3.h[0]                // clip(R[0] >> 12)
        umov            w10, v4.h[0]                // clip(G[0] >> 12)
        umov            w11, v5.h[0]                // clip(B[0] >> 12)
        ldrh            w9,  [x8, x9,  lsl #1]      // R[0] = gamma.out[clip(R[0] >> 12)]
        ldrh            w10, [x8, x10, lsl #1]      // G[0] = gamma.out[clip(G[0] >> 12)]
        ldrh            w11, [x8, x11, lsl #1]      // B[0] = gamma.out[clip(B[0] >> 12)]
        lsl             w9,  w9,  #4                // w9  = R[0] << 4
        lsl             w10, w10, #4                // w10 = G[0] << 4
        lsl             w11, w11, #4                // w11 = B[0] << 4
        strh            w9,  [x1]
        strh            w10, [x1, #2]
        strh            w11, [x1, #4]
        add             x1,  x1,  #6

        cmp             w0,  #-2
        b.lt            10f                         // (w & 3) == 1

        umov            w9,  v3.h[1]                // clip(R[1] >> 12)
        umov            w10, v4.h[1]                // clip(G[1] >> 12)
        umov            w11, v5.h[1]                // clip(B[1] >> 12)
        ldrh            w9,  [x8, x9,  lsl #1]      // R[1] = gamma.out[clip(R[1] >> 12)]
        ldrh            w10, [x8, x10, lsl #1]      // G[1] = gamma.out[clip(G[1] >> 12)]
        ldrh            w11, [x8, x11, lsl #1]      // B[1] = gamma.out[clip(B[1] >> 12)]
        lsl             w9,  w9,  #4                // w9  = R[1] << 4
        lsl             w10, w10, #4                // w10 = G[1] << 4
        lsl             w11, w11, #4                // w11 = B[1] << 4
        strh            w9,  [x1]
        strh            w10, [x1, #2]
        strh            w11, [x1, #4]
        add             x1,  x1,  #6

        b.le            10f                         // (w & 3) == 2

        umov            w9,  v3.h[2]                // clip(R[2] >> 12)
        umov            w10, v4.h[2]                // clip(G[2] >> 12)
        umov            w11, v5.h[2]                // clip(B[2] >> 12)
        ldrh            w9,  [x8, x9,  lsl #1]      // R[2] = gamma.out[clip(R[2] >> 12)]
        ldrh            w10, [x8, x10, lsl #1]      // G[2] = gamma.out[clip(G[2] >> 12)]
        ldrh            w11, [x8, x11, lsl #1]      // B[2] = gamma.out[clip(B[2] >> 12)]
        lsl             w9,  w9,  #4                // w9  = R[2] << 4
        lsl             w10, w10, #4                // w10 = G[2] << 4
        lsl             w11, w11, #4                // w11 = B[2] << 4
        strh            w9,  [x1]
        strh            w10, [x1, #2]
        strh            w11, [x1, #4]
        add             x1,  x1,  #6

        .align JUMP_ALIGN
10:
        ret
endfunc
