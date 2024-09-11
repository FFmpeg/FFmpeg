/*
 * Copyright (c) 2024 Zhao Zhili <quinkblack@foxmail.com>
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

#define VVC_MAX_PB_SIZE 128

function ff_vvc_sad_neon, export=1
        src0            .req x0
        src1            .req x1
        dx              .req w2
        dy              .req w3
        block_w         .req w4
        block_h         .req w5

        sub             w7, dx, #4
        sub             w8, dy, #4
        add             w6, dx, dy, lsl #7
        add             w7, w7, w8, lsl #7
        sxtw            x6, w6
        sxtw            x7, w7
        add             src0, src0, x6, lsl #1
        sub             src1, src1, x7, lsl #1

        cmp             block_w, #16
        movi            v16.4s, #0
        b.ge            2f
1:
        // block_w == 8
        ldr             q0, [src0]
        ldr             q2, [src1]
        subs            block_h, block_h, #2
        sabal           v16.4s, v0.4h, v2.4h
        sabal2          v16.4s, v0.8h, v2.8h

        add             src0, src0, #(2 * VVC_MAX_PB_SIZE * 2)
        add             src1, src1, #(2 * VVC_MAX_PB_SIZE * 2)
        b.ne            1b
        b               4f
2:
        // block_w == 16, no block_w > 16 according the spec
        movi            v17.4s, #0
3:
        ldp             q0, q1, [src0], #(2 * VVC_MAX_PB_SIZE * 2)
        ldp             q2, q3, [src1], #(2 * VVC_MAX_PB_SIZE * 2)
        subs            block_h, block_h, #2
        sabal           v16.4s, v0.4h, v2.4h
        sabal2          v16.4s, v0.8h, v2.8h
        sabal           v17.4s, v1.4h, v3.4h
        sabal2          v17.4s, v1.8h, v3.8h

        b.ne            3b
        add             v16.4s, v16.4s, v17.4s
4:
        addv            s16, v16.4s
        mov             w0, v16.s[0]
        ret
endfunc
