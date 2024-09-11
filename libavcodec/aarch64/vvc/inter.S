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

.macro vvc_avg, bit_depth

.macro vvc_avg_\bit_depth\()_2_4, tap
.if \tap == 2
        ldr             s0, [src0]
        ldr             s2, [src1]
.else
        ldr             d0, [src0]
        ldr             d2, [src1]
.endif
        saddl           v4.4s, v0.4h, v2.4h
        add             v4.4s, v4.4s, v16.4s
        sqshrn          v4.4h, v4.4s, #(15 - \bit_depth)
.if \bit_depth == 8
        sqxtun          v4.8b, v4.8h
.if \tap == 2
        str             h4, [dst]
.else   // tap == 4
        str             s4, [dst]
.endif

.else   // bit_depth > 8
        smin            v4.4h, v4.4h, v17.4h
        smax            v4.4h, v4.4h, v18.4h
.if \tap == 2
        str             s4, [dst]
.else
        str             d4, [dst]
.endif
.endif
        add             src0, src0, x10
        add             src1, src1, x10
        add             dst, dst, dst_stride
.endm

function ff_vvc_avg_\bit_depth\()_neon, export=1
        dst             .req x0
        dst_stride      .req x1
        src0            .req x2
        src1            .req x3
        width           .req w4
        height          .req w5

        mov             x10, #(VVC_MAX_PB_SIZE * 2)
        cmp             width, #8
.if \bit_depth == 8
        movi            v16.4s, #64
.else
.if \bit_depth == 10
        mov             w6, #1023
        movi            v16.4s, #16
.else
        mov             w6, #4095
        movi            v16.4s, #4
.endif
        movi            v18.8h, #0
        dup             v17.8h, w6
.endif
        b.eq            8f
        b.hi            16f
        cmp             width, #4
        b.eq            4f
2:      // width == 2
        subs            height, height, #1
        vvc_avg_\bit_depth\()_2_4 2
        b.ne            2b
        b               32f
4:      // width == 4
        subs            height, height, #1
        vvc_avg_\bit_depth\()_2_4 4
        b.ne            4b
        b               32f
8:      // width == 8
        ld1             {v0.8h}, [src0], x10
        ld1             {v2.8h}, [src1], x10
        saddl           v4.4s, v0.4h, v2.4h
        saddl2          v5.4s, v0.8h, v2.8h
        add             v4.4s, v4.4s, v16.4s
        add             v5.4s, v5.4s, v16.4s
        sqshrn          v4.4h, v4.4s, #(15 - \bit_depth)
        sqshrn2         v4.8h, v5.4s, #(15 - \bit_depth)
        subs            height, height, #1
.if \bit_depth == 8
        sqxtun          v4.8b, v4.8h
        st1             {v4.8b}, [dst], dst_stride
.else
        smin            v4.8h, v4.8h, v17.8h
        smax            v4.8h, v4.8h, v18.8h
        st1             {v4.8h}, [dst], dst_stride
.endif
        b.ne            8b
        b               32f
16:     // width >= 16
        mov             w6, width
        mov             x7, src0
        mov             x8, src1
        mov             x9, dst
17:
        ldp             q0, q1, [x7], #32
        ldp             q2, q3, [x8], #32
        saddl           v4.4s, v0.4h, v2.4h
        saddl2          v5.4s, v0.8h, v2.8h
        saddl           v6.4s, v1.4h, v3.4h
        saddl2          v7.4s, v1.8h, v3.8h
        add             v4.4s, v4.4s, v16.4s
        add             v5.4s, v5.4s, v16.4s
        add             v6.4s, v6.4s, v16.4s
        add             v7.4s, v7.4s, v16.4s
        sqshrn          v4.4h, v4.4s, #(15 - \bit_depth)
        sqshrn2         v4.8h, v5.4s, #(15 - \bit_depth)
        sqshrn          v6.4h, v6.4s, #(15 - \bit_depth)
        sqshrn2         v6.8h, v7.4s, #(15 - \bit_depth)
        subs            w6, w6, #16
.if \bit_depth == 8
        sqxtun          v4.8b, v4.8h
        sqxtun2         v4.16b, v6.8h
        str             q4, [x9], #16
.else
        smin            v4.8h, v4.8h, v17.8h
        smin            v6.8h, v6.8h, v17.8h
        smax            v4.8h, v4.8h, v18.8h
        smax            v6.8h, v6.8h, v18.8h
        stp             q4, q6, [x9], #32
.endif
        b.ne            17b

        subs            height, height, #1
        add             src0, src0, x10
        add             src1, src1, x10
        add             dst, dst, dst_stride
        b.ne            16b
32:
        ret
endfunc
.endm

vvc_avg 8
vvc_avg 10
vvc_avg 12
