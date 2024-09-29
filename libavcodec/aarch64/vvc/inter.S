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

.macro vvc_avg type, bit_depth

.macro vvc_\type\()_\bit_depth\()_2_4 tap
.if \tap == 2
        ldr             s0, [src0]
        ldr             s2, [src1]
.else
        ldr             d0, [src0]
        ldr             d2, [src1]
.endif

.ifc \type, avg
        saddl           v4.4s, v0.4h, v2.4h
        add             v4.4s, v4.4s, v16.4s
        sqshrn          v4.4h, v4.4s, #(15 - \bit_depth)
.else
        mov             v4.16b, v16.16b
        smlal           v4.4s, v0.4h, v19.4h
        smlal           v4.4s, v2.4h, v20.4h
        sqshl           v4.4s, v4.4s, v22.4s
        sqxtn           v4.4h, v4.4s
.endif

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

function ff_vvc_\type\()_\bit_depth\()_neon, export=1
        dst             .req x0
        dst_stride      .req x1
        src0            .req x2
        src1            .req x3
        width           .req w4
        height          .req w5

        mov             x10, #(VVC_MAX_PB_SIZE * 2)
        cmp             width, #8
.ifc \type, avg
        movi            v16.4s, #(1 << (14 - \bit_depth))
.else
        lsr             x11, x6, #32        // weight0
        mov             w12, w6             // weight1
        lsr             x13, x7, #32        // offset
        mov             w14, w7             // shift

        dup             v19.8h, w11
        neg             w14, w14            // so we can use sqshl
        dup             v20.8h, w12
        dup             v16.4s, w13
        dup             v22.4s, w14
.endif // avg

 .if \bit_depth >= 10
        // clip pixel
        mov             w6, #((1 << \bit_depth) - 1)
        movi            v18.8h, #0
        dup             v17.8h, w6
.endif

        b.eq            8f
        b.hi            16f
        cmp             width, #4
        b.eq            4f
2:      // width == 2
        subs            height, height, #1
        vvc_\type\()_\bit_depth\()_2_4 2
        b.ne            2b
        b               32f
4:      // width == 4
        subs            height, height, #1
        vvc_\type\()_\bit_depth\()_2_4 4
        b.ne            4b
        b               32f
8:      // width == 8
        ld1             {v0.8h}, [src0], x10
        ld1             {v2.8h}, [src1], x10
.ifc \type, avg
        saddl           v4.4s, v0.4h, v2.4h
        saddl2          v5.4s, v0.8h, v2.8h
        add             v4.4s, v4.4s, v16.4s
        add             v5.4s, v5.4s, v16.4s
        sqshrn          v4.4h, v4.4s, #(15 - \bit_depth)
        sqshrn2         v4.8h, v5.4s, #(15 - \bit_depth)
.else
        mov             v4.16b, v16.16b
        mov             v5.16b, v16.16b
        smlal           v4.4s, v0.4h, v19.4h
        smlal           v4.4s, v2.4h, v20.4h
        smlal2          v5.4s, v0.8h, v19.8h
        smlal2          v5.4s, v2.8h, v20.8h
        sqshl           v4.4s, v4.4s, v22.4s
        sqshl           v5.4s, v5.4s, v22.4s
        sqxtn           v4.4h, v4.4s
        sqxtn2          v4.8h, v5.4s
.endif
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
.ifc \type, avg
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
.else   // avg
        mov             v4.16b, v16.16b
        mov             v5.16b, v16.16b
        mov             v6.16b, v16.16b
        mov             v7.16b, v16.16b
        smlal           v4.4s, v0.4h, v19.4h
        smlal           v4.4s, v2.4h, v20.4h
        smlal2          v5.4s, v0.8h, v19.8h
        smlal2          v5.4s, v2.8h, v20.8h
        smlal           v6.4s, v1.4h, v19.4h
        smlal           v6.4s, v3.4h, v20.4h
        smlal2          v7.4s, v1.8h, v19.8h
        smlal2          v7.4s, v3.8h, v20.8h
        sqshl           v4.4s, v4.4s, v22.4s
        sqshl           v5.4s, v5.4s, v22.4s
        sqshl           v6.4s, v6.4s, v22.4s
        sqshl           v7.4s, v7.4s, v22.4s
        sqxtn           v4.4h, v4.4s
        sqxtn           v6.4h, v6.4s
        sqxtn2          v4.8h, v5.4s
        sqxtn2          v6.8h, v7.4s
.endif  // w_avg
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

.unreq dst
.unreq dst_stride
.unreq src0
.unreq src1
.unreq width
.unreq height
endfunc
.endm

vvc_avg avg, 8
vvc_avg avg, 10
vvc_avg avg, 12
vvc_avg w_avg, 8
vvc_avg w_avg, 10
vvc_avg w_avg, 12

/* x0: int16_t *dst
 * x1: const uint8_t *_src
 * x2: ptrdiff_t _src_stride
 * w3: int height
 * x4: intptr_t mx
 * x5: intptr_t my
 * w6: int width
 */
function ff_vvc_dmvr_8_neon, export=1
        dst             .req x0
        src             .req x1
        src_stride      .req x2
        height          .req w3
        mx              .req x4
        my              .req x5
        width           .req w6

        sxtw            x6, w6
        mov             x7, #(VVC_MAX_PB_SIZE * 2 + 8)
        cmp             width, #16
        sub             src_stride, src_stride, x6
        cset            w15, gt                     // width > 16
        movi            v16.8h, #2                  // DMVR_SHIFT
        sub             x7, x7, x6, lsl #1
1:
        cbz             w15, 2f
        ldr             q0, [src], #16
        uxtl            v1.8h, v0.8b
        uxtl2           v2.8h, v0.16b
        ushl            v1.8h, v1.8h, v16.8h
        ushl            v2.8h, v2.8h, v16.8h
        stp             q1, q2, [dst], #32
        b               3f
2:
        ldr             d0, [src], #8
        uxtl            v1.8h, v0.8b
        ushl            v1.8h, v1.8h, v16.8h
        str             q1, [dst], #16
3:
        subs            height, height, #1
        ldr             s3, [src], #4
        uxtl            v4.8h, v3.8b
        ushl            v4.4h, v4.4h, v16.4h
        st1             {v4.4h}, [dst], x7

        add             src, src, src_stride
        b.ne            1b

        ret
endfunc

function ff_vvc_dmvr_12_neon, export=1
        sxtw            x6, w6
        mov             x7, #(VVC_MAX_PB_SIZE * 2 + 8)
        cmp             width, #16
        sub             src_stride, src_stride, x6, lsl #1
        cset            w15, gt                     // width > 16
        movi            v16.8h, #2                  // offset4
        sub             x7, x7, x6, lsl #1
1:
        cbz             w15, 2f
        ldp             q0, q1, [src], #32
        uaddl           v2.4s, v0.4h, v16.4h
        uaddl2          v3.4s, v0.8h, v16.8h
        uaddl           v4.4s, v1.4h, v16.4h
        uaddl2          v5.4s, v1.8h, v16.8h
        ushr            v2.4s, v2.4s, #2
        ushr            v3.4s, v3.4s, #2
        ushr            v4.4s, v4.4s, #2
        ushr            v5.4s, v5.4s, #2
        uqxtn           v2.4h, v2.4s
        uqxtn2          v2.8h, v3.4s
        uqxtn           v4.4h, v4.4s
        uqxtn2          v4.8h, v5.4s

        stp             q2, q4, [dst], #32
        b               3f
2:
        ldr             q0, [src], #16
        uaddl           v2.4s, v0.4h, v16.4h
        uaddl2          v3.4s, v0.8h, v16.8h
        ushr            v2.4s, v2.4s, #2
        ushr            v3.4s, v3.4s, #2
        uqxtn           v2.4h, v2.4s
        uqxtn2          v2.8h, v3.4s
        str             q2, [dst], #16
3:
        subs            height, height, #1
        ldr             d0, [src], #8
        uaddl           v3.4s, v0.4h, v16.4h
        ushr            v3.4s, v3.4s, #2
        uqxtn           v3.4h, v3.4s
        st1             {v3.4h}, [dst], x7

        add             src, src, src_stride
        b.ne            1b

        ret
endfunc

function ff_vvc_dmvr_hv_8_neon, export=1
        tmp0            .req x7
        tmp1            .req x8

        sub             sp, sp, #(VVC_MAX_PB_SIZE * 4)

        movrel          x9, X(ff_vvc_inter_luma_dmvr_filters)
        add             x12, x9, mx, lsl #1
        ldrb            w10, [x12]
        ldrb            w11, [x12, #1]
        mov             tmp0, sp
        add             tmp1, tmp0, #(VVC_MAX_PB_SIZE * 2)
        // We know the value are positive
        dup             v0.8h, w10                  // filter_x[0]
        dup             v1.8h, w11                  // filter_x[1]

        add             x12, x9, my, lsl #1
        ldrb            w10, [x12]
        ldrb            w11, [x12, #1]
        sxtw            x6, w6
        movi            v30.8h, #(1 << (8 - 7))     // offset1
        movi            v31.8h, #8                  // offset2
        dup             v2.8h, w10                  // filter_y[0]
        dup             v3.8h, w11                  // filter_y[1]

        // Valid value for width can only be 8 + 4, 16 + 4
        cmp             width, #16
        mov             w10, #0                     // start filter_y or not
        add             height, height, #1
        sub             dst, dst, #(VVC_MAX_PB_SIZE * 2)
        sub             src_stride, src_stride, x6
        cset            w15, gt                     // width > 16
1:
        mov             x12, tmp0
        mov             x13, tmp1
        mov             x14, dst
        cbz             w15, 2f

        // width > 16
        ldur            q5, [src, #1]
        ldr             q4, [src], #16
        uxtl            v7.8h, v5.8b
        uxtl2           v17.8h, v5.16b
        uxtl            v6.8h, v4.8b
        uxtl2           v16.8h, v4.16b
        mul             v6.8h, v6.8h, v0.8h
        mul             v16.8h, v16.8h, v0.8h
        mla             v6.8h, v7.8h, v1.8h
        mla             v16.8h, v17.8h, v1.8h
        add             v6.8h, v6.8h, v30.8h
        add             v16.8h, v16.8h, v30.8h
        ushr            v6.8h, v6.8h, #(8 - 6)
        ushr            v7.8h, v16.8h, #(8 - 6)
        stp             q6, q7, [x13], #32

        cbz             w10, 3f

        ldp             q16, q17, [x12], #32
        mul             v16.8h, v16.8h, v2.8h
        mul             v17.8h, v17.8h, v2.8h
        mla             v16.8h, v6.8h, v3.8h
        mla             v17.8h, v7.8h, v3.8h
        add             v16.8h, v16.8h, v31.8h
        add             v17.8h, v17.8h, v31.8h
        ushr            v16.8h, v16.8h, #4
        ushr            v17.8h, v17.8h, #4
        stp             q16, q17, [x14], #32
        b               3f
2:
        // width > 8
        ldur            d5, [src, #1]
        ldr             d4, [src], #8
        uxtl            v7.8h, v5.8b
        uxtl            v6.8h, v4.8b
        mul             v6.8h, v6.8h, v0.8h
        mla             v6.8h, v7.8h, v1.8h
        add             v6.8h, v6.8h, v30.8h
        ushr            v6.8h, v6.8h, #(8 - 6)
        str             q6, [x13], #16

        cbz             w10, 3f

        ldr             q16, [x12], #16
        mul             v16.8h, v16.8h, v2.8h
        mla             v16.8h, v6.8h, v3.8h
        add             v16.8h, v16.8h, v31.8h
        ushr            v16.8h, v16.8h, #4
        str             q16, [x14], #16
3:
        ldr             s5, [src, #1]
        ldr             s4, [src], #4
        uxtl            v7.8h, v5.8b
        uxtl            v6.8h, v4.8b
        mul             v6.4h, v6.4h, v0.4h
        mla             v6.4h, v7.4h, v1.4h
        add             v6.4h, v6.4h, v30.4h
        ushr            v6.4h, v6.4h, #(8 - 6)
        str             d6, [x13], #8

        cbz             w10, 4f

        ldr             d16, [x12], #8
        mul             v16.4h, v16.4h, v2.4h
        mla             v16.4h, v6.4h, v3.4h
        add             v16.4h, v16.4h, v31.4h
        ushr            v16.4h, v16.4h, #4
        str             d16, [x14], #8
4:
        subs            height, height, #1
        mov             w10, #1
        add             src, src, src_stride
        add             dst, dst, #(VVC_MAX_PB_SIZE * 2)
        eor             tmp0, tmp0, tmp1
        eor             tmp1, tmp0, tmp1
        eor             tmp0, tmp0, tmp1
        b.ne            1b

        add             sp, sp, #(VVC_MAX_PB_SIZE * 4)
        ret
endfunc

function ff_vvc_dmvr_hv_12_neon, export=1
        movi            v29.4s, #(12 - 6)
        movi            v30.4s, #(1 << (12 - 7))    // offset1
        b               0f
endfunc

function ff_vvc_dmvr_hv_10_neon, export=1
        movi            v29.4s, #(10 - 6)
        movi            v30.4s, #(1 << (10 - 7))    // offset1
0:
        movi            v31.4s, #8                  // offset2
        neg             v29.4s, v29.4s

        sub             sp, sp, #(VVC_MAX_PB_SIZE * 4)

        movrel          x9, X(ff_vvc_inter_luma_dmvr_filters)
        add             x12, x9, mx, lsl #1
        ldrb            w10, [x12]
        ldrb            w11, [x12, #1]
        mov             tmp0, sp
        add             tmp1, tmp0, #(VVC_MAX_PB_SIZE * 2)
        // We know the value are positive
        dup             v0.8h, w10                  // filter_x[0]
        dup             v1.8h, w11                  // filter_x[1]

        add             x12, x9, my, lsl #1
        ldrb            w10, [x12]
        ldrb            w11, [x12, #1]
        sxtw            x6, w6
        dup             v2.8h, w10                  // filter_y[0]
        dup             v3.8h, w11                  // filter_y[1]

        // Valid value for width can only be 8 + 4, 16 + 4
        cmp             width, #16
        mov             w10, #0                     // start filter_y or not
        add             height, height, #1
        sub             dst, dst, #(VVC_MAX_PB_SIZE * 2)
        sub             src_stride, src_stride, x6, lsl #1
        cset            w15, gt                     // width > 16
1:
        mov             x12, tmp0
        mov             x13, tmp1
        mov             x14, dst
        cbz             w15, 2f

        // width > 16
        add             x16, src, #2
        ldp             q6, q16, [src], #32
        ldp             q7, q17, [x16]
        umull           v4.4s, v6.4h, v0.4h
        umull2          v5.4s, v6.8h, v0.8h
        umull           v18.4s, v16.4h, v0.4h
        umull2          v19.4s, v16.8h, v0.8h
        umlal           v4.4s, v7.4h, v1.4h
        umlal2          v5.4s, v7.8h, v1.8h
        umlal           v18.4s, v17.4h, v1.4h
        umlal2          v19.4s, v17.8h, v1.8h

        add             v4.4s, v4.4s, v30.4s
        add             v5.4s, v5.4s, v30.4s
        add             v18.4s, v18.4s, v30.4s
        add             v19.4s, v19.4s, v30.4s
        ushl            v4.4s, v4.4s, v29.4s
        ushl            v5.4s, v5.4s, v29.4s
        ushl            v18.4s, v18.4s, v29.4s
        ushl            v19.4s, v19.4s, v29.4s
        uqxtn           v6.4h, v4.4s
        uqxtn2          v6.8h, v5.4s
        uqxtn           v7.4h, v18.4s
        uqxtn2          v7.8h, v19.4s
        stp             q6, q7, [x13], #32

        cbz             w10, 3f

        ldp             q4, q5, [x12], #32
        umull           v17.4s, v4.4h, v2.4h
        umull2          v18.4s, v4.8h, v2.8h
        umull           v19.4s, v5.4h, v2.4h
        umull2          v20.4s, v5.8h, v2.8h
        umlal           v17.4s, v6.4h, v3.4h
        umlal2          v18.4s, v6.8h, v3.8h
        umlal           v19.4s, v7.4h, v3.4h
        umlal2          v20.4s, v7.8h, v3.8h
        add             v17.4s, v17.4s, v31.4s
        add             v18.4s, v18.4s, v31.4s
        add             v19.4s, v19.4s, v31.4s
        add             v20.4s, v20.4s, v31.4s
        ushr            v17.4s, v17.4s, #4
        ushr            v18.4s, v18.4s, #4
        ushr            v19.4s, v19.4s, #4
        ushr            v20.4s, v20.4s, #4
        uqxtn           v6.4h, v17.4s
        uqxtn2          v6.8h, v18.4s
        uqxtn           v7.4h, v19.4s
        uqxtn2          v7.8h, v20.4s
        stp             q6, q7, [x14], #32
        b               3f
2:
        // width > 8
        ldur            q7, [src, #2]
        ldr             q6, [src], #16
        umull           v4.4s, v6.4h, v0.4h
        umull2          v5.4s, v6.8h, v0.8h
        umlal           v4.4s, v7.4h, v1.4h
        umlal2          v5.4s, v7.8h, v1.8h

        add             v4.4s, v4.4s, v30.4s
        add             v5.4s, v5.4s, v30.4s
        ushl            v4.4s, v4.4s, v29.4s
        ushl            v5.4s, v5.4s, v29.4s
        uqxtn           v6.4h, v4.4s
        uqxtn2          v6.8h, v5.4s
        str             q6, [x13], #16

        cbz             w10, 3f

        ldr             q16, [x12], #16
        umull           v17.4s, v16.4h, v2.4h
        umull2          v18.4s, v16.8h, v2.8h
        umlal           v17.4s, v6.4h, v3.4h
        umlal2          v18.4s, v6.8h, v3.8h
        add             v17.4s, v17.4s, v31.4s
        add             v18.4s, v18.4s, v31.4s
        ushr            v17.4s, v17.4s, #4
        ushr            v18.4s, v18.4s, #4
        uqxtn           v16.4h, v17.4s
        uqxtn2          v16.8h, v18.4s
        str             q16, [x14], #16
3:
        ldr             d7, [src, #2]
        ldr             d6, [src], #8
        umull           v4.4s, v7.4h, v1.4h
        umlal           v4.4s, v6.4h, v0.4h
        add             v4.4s, v4.4s, v30.4s
        ushl            v4.4s, v4.4s, v29.4s
        uqxtn           v6.4h, v4.4s
        str             d6, [x13], #8

        cbz             w10, 4f

        ldr             d16, [x12], #8
        umull           v17.4s, v16.4h, v2.4h
        umlal           v17.4s, v6.4h, v3.4h
        add             v17.4s, v17.4s, v31.4s
        ushr            v17.4s, v17.4s, #4
        uqxtn           v16.4h, v17.4s
        str             d16, [x14], #8
4:
        subs            height, height, #1
        mov             w10, #1
        add             src, src, src_stride
        add             dst, dst, #(VVC_MAX_PB_SIZE * 2)
        eor             tmp0, tmp0, tmp1
        eor             tmp1, tmp0, tmp1
        eor             tmp0, tmp0, tmp1
        b.ne            1b

        add             sp, sp, #(VVC_MAX_PB_SIZE * 4)
        ret

.unreq dst
.unreq src
.unreq src_stride
.unreq height
.unreq mx
.unreq my
.unreq width
.unreq tmp0
.unreq tmp1
endfunc
