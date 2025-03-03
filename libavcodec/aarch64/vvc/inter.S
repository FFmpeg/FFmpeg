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
#define BDOF_BLOCK_SIZE 16
#define BDOF_MIN_BLOCK_SIZE 4

.macro vvc_w_avg bit_depth

.macro vvc_w_avg_\bit_depth\()_2_4 tap
.if \tap == 2
        ldr             s0, [src0]
        ldr             s2, [src1]
.else
        ldr             d0, [src0]
        ldr             d2, [src1]
.endif
        mov             v4.16b, v16.16b
        smlal           v4.4s, v0.4h, v19.4h
        smlal           v4.4s, v2.4h, v20.4h
        sqshl           v4.4s, v4.4s, v22.4s
        sqxtun          v4.4h, v4.4s

.if \bit_depth == 8
        sqxtun          v4.8b, v4.8h
.if \tap == 2
        str             h4, [dst]
.else   // tap == 4
        str             s4, [dst]
.endif

.else   // bit_depth > 8
        umin            v4.4h, v4.4h, v17.4h
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

function ff_vvc_w_avg_\bit_depth\()_neon, export=1
        dst             .req x0
        dst_stride      .req x1
        src0            .req x2
        src1            .req x3
        width           .req w4
        height          .req w5

        mov             x10, #(VVC_MAX_PB_SIZE * 2)
        cmp             width, #8
        lsr             x11, x6, #32        // weight0
        mov             w12, w6             // weight1
        lsr             x13, x7, #32        // offset
        mov             w14, w7             // shift

        dup             v19.8h, w11
        neg             w14, w14            // so we can use sqshl
        dup             v20.8h, w12
        dup             v16.4s, w13
        dup             v22.4s, w14

.if \bit_depth >= 10
        // clip pixel
        mov             w6, #((1 << \bit_depth) - 1)
        dup             v17.8h, w6
.endif

        b.eq            8f
        b.hi            16f
        cmp             width, #4
        b.eq            4f
2:      // width == 2
        subs            height, height, #1
        vvc_w_avg_\bit_depth\()_2_4 2
        b.ne            2b
        b               32f
4:      // width == 4
        subs            height, height, #1
        vvc_w_avg_\bit_depth\()_2_4 4
        b.ne            4b
        b               32f
8:      // width == 8
        ld1             {v0.8h}, [src0], x10
        ld1             {v2.8h}, [src1], x10
        mov             v4.16b, v16.16b
        mov             v5.16b, v16.16b
        smlal           v4.4s, v0.4h, v19.4h
        smlal           v4.4s, v2.4h, v20.4h
        smlal2          v5.4s, v0.8h, v19.8h
        smlal2          v5.4s, v2.8h, v20.8h
        sqshl           v4.4s, v4.4s, v22.4s
        sqshl           v5.4s, v5.4s, v22.4s
        sqxtun          v4.4h, v4.4s
        sqxtun2         v4.8h, v5.4s
        subs            height, height, #1
.if \bit_depth == 8
        sqxtun          v4.8b, v4.8h
        st1             {v4.8b}, [dst], dst_stride
.else
        umin            v4.8h, v4.8h, v17.8h
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
        sqxtun          v4.4h, v4.4s
        sqxtun          v6.4h, v6.4s
        sqxtun2         v4.8h, v5.4s
        sqxtun2         v6.8h, v7.4s
        subs            w6, w6, #16
.if \bit_depth == 8
        sqxtun          v4.8b, v4.8h
        sqxtun2         v4.16b, v6.8h
        str             q4, [x9], #16
.else
        umin            v4.8h, v4.8h, v17.8h
        umin            v6.8h, v6.8h, v17.8h
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

vvc_w_avg 8
vvc_w_avg 10
vvc_w_avg 12

.macro vvc_avg bit_depth
function ff_vvc_avg_\bit_depth\()_neon, export=1
        mov             x10, #(VVC_MAX_PB_SIZE * 2)
        movi            v16.8h, #0
        movi            v17.16b, #255
        ushr            v17.8h, v17.8h, #(16 - \bit_depth)

        cmp             w4, #8
        b.gt            16f
        b.eq            8f
        cmp             w4, #4
        b.eq            4f

2: // width == 2
        ldr             s0, [x2]
        subs            w5, w5, #1
        ldr             s1, [x3]
.if \bit_depth == 8
        shadd           v0.4h, v0.4h, v1.4h
        sqrshrun        v0.8b, v0.8h, #(15 - 1 - \bit_depth)
        str             h0, [x0]
.else
        shadd           v0.4h, v0.4h, v1.4h
        srshr           v0.4h, v0.4h, #(15 - 1 - \bit_depth)
        smax            v0.4h, v0.4h, v16.4h
        smin            v0.4h, v0.4h, v17.4h
        str             s0, [x0]
.endif
        add             x2, x2, #(VVC_MAX_PB_SIZE * 2)
        add             x3, x3, #(VVC_MAX_PB_SIZE * 2)
        add             x0, x0, x1
        b.ne            2b
        ret

4: // width == 4
        ldr             d0, [x2]
        subs            w5, w5, #1
        ldr             d1, [x3]
.if \bit_depth == 8
        shadd           v0.4h, v0.4h, v1.4h
        sqrshrun        v0.8b, v0.8h, #(15 - 1 - \bit_depth)
        str             s0, [x0]
.else
        shadd           v0.4h, v0.4h, v1.4h
        srshr           v0.4h, v0.4h, #(15 - 1 - \bit_depth)
        smax            v0.4h, v0.4h, v16.4h
        smin            v0.4h, v0.4h, v17.4h
        str             d0, [x0]
.endif
        add             x2, x2, #(VVC_MAX_PB_SIZE * 2)
        add             x3, x3, #(VVC_MAX_PB_SIZE * 2)
        add             x0, x0, x1
        b.ne            4b
        ret

8: // width == 8
        ldr             q0, [x2]
        subs            w5, w5, #1
        ldr             q1, [x3]
.if \bit_depth == 8
        shadd           v0.8h, v0.8h, v1.8h
        sqrshrun        v0.8b, v0.8h, #(15 - 1 - \bit_depth)
        str             d0, [x0]
.else
        shadd           v0.8h, v0.8h, v1.8h
        srshr           v0.8h, v0.8h, #(15 - 1 - \bit_depth)
        smax            v0.8h, v0.8h, v16.8h
        smin            v0.8h, v0.8h, v17.8h
        str             q0, [x0]
.endif
        add             x2, x2, #(VVC_MAX_PB_SIZE * 2)
        add             x3, x3, #(VVC_MAX_PB_SIZE * 2)
        add             x0, x0, x1
        b.ne            8b
        ret

16: // width >= 16
.if \bit_depth == 8
        sub             x1, x1, w4, sxtw
.else
        sub             x1, x1, w4, sxtw #1
.endif
        sub             x10, x10, w4, sxtw #1
3:
        mov             w6, w4 // width
1:
        ldp             q0, q1, [x2], #32
        subs            w6, w6, #16
        ldp             q2, q3, [x3], #32
.if \bit_depth == 8
        shadd           v4.8h, v0.8h, v2.8h
        shadd           v5.8h, v1.8h, v3.8h
        sqrshrun        v0.8b, v4.8h, #6
        sqrshrun2       v0.16b, v5.8h, #6
        st1             {v0.16b}, [x0], #16
.else
        shadd           v4.8h, v0.8h, v2.8h
        shadd           v5.8h, v1.8h, v3.8h
        srshr           v0.8h, v4.8h, #(15 - 1 - \bit_depth)
        srshr           v1.8h, v5.8h, #(15 - 1 - \bit_depth)
        smax            v0.8h, v0.8h, v16.8h
        smax            v1.8h, v1.8h, v16.8h
        smin            v0.8h, v0.8h, v17.8h
        smin            v1.8h, v1.8h, v17.8h
        stp             q0, q1, [x0], #32
.endif
        b.ne            1b

        subs            w5, w5, #1
        add             x2, x2, x10
        add             x3, x3, x10
        add             x0, x0, x1
        b.ne            3b
        ret
endfunc
.endm

vvc_avg 8
vvc_avg 10
vvc_avg 12

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
        ushll           v1.8h, v0.8b, #2
        ushll2          v2.8h, v0.16b, #2
        stp             q1, q2, [dst], #32
        b               3f
2:
        ldr             d0, [src], #8
        ushll           v1.8h, v0.8b, #2
        str             q1, [dst], #16
3:
        subs            height, height, #1
        ldr             s3, [src], #4
        ushll           v4.8h, v3.8b, #2
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
        sub             x7, x7, x6, lsl #1
1:
        cbz             w15, 2f
        ldp             q0, q1, [src], #32
        urshr           v0.8h, v0.8h, #2
        urshr           v1.8h, v1.8h, #2

        stp             q0, q1, [dst], #32
        b               3f
2:
        ldr             q0, [src], #16
        urshr           v0.8h, v0.8h, #2
        str             q0, [dst], #16
3:
        subs            height, height, #1
        ldr             d0, [src], #8
        urshr           v0.4h, v0.4h, #2
        st1             {v0.4h}, [dst], x7

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
        urshr           v6.8h, v6.8h, #(8 - 6)
        urshr           v7.8h, v16.8h, #(8 - 6)
        stp             q6, q7, [x13], #32

        cbz             w10, 3f

        ldp             q16, q17, [x12], #32
        mul             v16.8h, v16.8h, v2.8h
        mul             v17.8h, v17.8h, v2.8h
        mla             v16.8h, v6.8h, v3.8h
        mla             v17.8h, v7.8h, v3.8h
        urshr           v16.8h, v16.8h, #4
        urshr           v17.8h, v17.8h, #4
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
        urshr           v6.8h, v6.8h, #(8 - 6)
        str             q6, [x13], #16

        cbz             w10, 3f

        ldr             q16, [x12], #16
        mul             v16.8h, v16.8h, v2.8h
        mla             v16.8h, v6.8h, v3.8h
        urshr           v16.8h, v16.8h, #4
        str             q16, [x14], #16
3:
        ldur            s5, [src, #1]
        ldr             s4, [src], #4
        uxtl            v7.8h, v5.8b
        uxtl            v6.8h, v4.8b
        mul             v6.4h, v6.4h, v0.4h
        mla             v6.4h, v7.4h, v1.4h
        urshr           v6.4h, v6.4h, #(8 - 6)
        str             d6, [x13], #8

        cbz             w10, 4f

        ldr             d16, [x12], #8
        mul             v16.4h, v16.4h, v2.4h
        mla             v16.4h, v6.4h, v3.4h
        urshr           v16.4h, v16.4h, #4
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
        ldur            d7, [src, #2]
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

function ff_vvc_prof_grad_filter_8x_neon, export=1
        gh              .req x0
        gv              .req x1
        gstride         .req x2
        src             .req x3
        src_stride      .req x4
        width           .req w5
        height          .req w6

        lsl             src_stride, src_stride, #1
        neg             x7, src_stride
1:
        mov             x10, src
        mov             w11, width
        mov             x12, gh
        mov             x13, gv
2:
        ldur            q0, [x10, #2]
        ldur            q1, [x10, #-2]
        subs            w11, w11, #8
        ldr             q2, [x10, src_stride]
        ldr             q3, [x10, x7]
        sshr            v0.8h, v0.8h, #6
        sshr            v1.8h, v1.8h, #6
        sshr            v2.8h, v2.8h, #6
        sshr            v3.8h, v3.8h, #6
        sub             v0.8h, v0.8h, v1.8h
        sub             v2.8h, v2.8h, v3.8h
        st1             {v0.8h}, [x12], #16
        st1             {v2.8h}, [x13], #16
        add             x10, x10, #16
        b.ne            2b

        subs            height, height, #1
        add             gh, gh, gstride, lsl #1
        add             gv, gv, gstride, lsl #1
        add             src, src, src_stride
        b.ne            1b
        ret

.unreq gh
.unreq gv
.unreq gstride
.unreq src
.unreq src_stride
.unreq width
.unreq height
endfunc

.macro vvc_apply_bdof_block bit_depth
        dst             .req x0
        dst_stride      .req x1
        src0            .req x2
        src1            .req x3
        gh              .req x4
        gv              .req x5
        vx              .req x6
        vy              .req x7

        ld1r            {v0.8h}, [vx], #2
        ld1r            {v1.8h}, [vy], #2
        ld1r            {v2.8h}, [vx]
        ld1r            {v3.8h}, [vy]
        ins             v0.d[1], v2.d[1]
        ins             v1.d[1], v3.d[1]

        movi            v7.4s, #(1 << (14 - \bit_depth))
        ldp             x8, x9, [gh]
        ldp             x10, x11, [gv]
        mov             x12, #(BDOF_BLOCK_SIZE * 2)
        mov             w13, #(BDOF_MIN_BLOCK_SIZE)
        mov             x14, #(VVC_MAX_PB_SIZE * 2)
.if \bit_depth >= 10
        // clip pixel
        mov             w15, #((1 << \bit_depth) - 1)
        movi            v18.8h, #0
        lsl             dst_stride, dst_stride, #1
        dup             v19.8h, w15
.endif
1:
        ld1             {v2.8h}, [x8], x12
        ld1             {v3.8h}, [x9], x12
        ld1             {v4.8h}, [x10], x12
        ld1             {v5.8h}, [x11], x12
        sub             v2.8h, v2.8h, v3.8h
        sub             v4.8h, v4.8h, v5.8h
        smull           v3.4s, v0.4h, v2.4h
        smull2          v16.4s, v0.8h, v2.8h
        smlal           v3.4s, v1.4h, v4.4h
        smlal2          v16.4s, v1.8h, v4.8h

        ld1             {v5.8h}, [src0], x14
        ld1             {v6.8h}, [src1], x14
        saddl           v2.4s, v5.4h, v6.4h
        add             v2.4s, v2.4s, v7.4s
        add             v2.4s, v2.4s, v3.4s
        saddl2          v4.4s, v5.8h, v6.8h
        add             v4.4s, v4.4s, v7.4s
        add             v4.4s, v4.4s, v16.4s

        sqshrn          v5.4h, v2.4s, #(15 - \bit_depth)
        sqshrn2         v5.8h, v4.4s, #(15 - \bit_depth)
        subs            w13, w13, #1
.if \bit_depth == 8
        sqxtun          v5.8b, v5.8h
        str             d5, [dst]
        add             dst, dst, dst_stride
.else
        smin            v5.8h, v5.8h, v19.8h
        smax            v5.8h, v5.8h, v18.8h
        st1             {v5.8h}, [dst], dst_stride
.endif
        b.ne            1b
        ret

.unreq dst
.unreq dst_stride
.unreq src0
.unreq src1
.unreq gh
.unreq gv
.unreq vx
.unreq vy
.endm

function ff_vvc_apply_bdof_block_8_neon, export=1
        vvc_apply_bdof_block 8
endfunc

function ff_vvc_apply_bdof_block_10_neon, export=1
        vvc_apply_bdof_block 10
endfunc

function ff_vvc_apply_bdof_block_12_neon, export=1
        vvc_apply_bdof_block 12
endfunc

function ff_vvc_derive_bdof_vx_vy_neon, export=1
        src0            .req x0
        src1            .req x1
        pad_mask        .req w2
        gh              .req x3
        gv              .req x4
        vx              .req x5
        vy              .req x6

        gh0             .req x7
        gh1             .req x8
        gv0             .req x9
        gv1             .req x10
        y               .req x12

        sgx2            .req w7
        sgy2            .req w8
        sgxgy           .req w9
        sgxdi           .req w10
        sgydi           .req w11

        sgx2_v          .req v22
        sgy2_v          .req v23
        sgxgy_v         .req v24
        sgxdi_v         .req v25
        sgydi_v         .req v26

        sgx2_v2         .req v27
        sgy2_v2         .req v28
        sgxgy_v2        .req v29
        sgxdi_v2        .req v30
        sgydi_v2        .req v31

        ldp             gh0, gh1, [gh]
        ldp             gv0, gv1, [gv]
        movi            sgx2_v.4s, #0
        movi            sgy2_v.4s, #0
        movi            sgxgy_v.4s, #0
        movi            sgxdi_v.4s, #0
        movi            sgydi_v.4s, #0
        movi            sgx2_v2.4s, #0
        movi            sgy2_v2.4s, #0
        movi            sgxgy_v2.4s, #0
        movi            sgxdi_v2.4s, #0
        movi            sgydi_v2.4s, #0
        mov             x13, #-1                    // dy
        movi            v6.4s, #0
        mov             y, #-1
        tbz             pad_mask, #1, 1f            // check pad top
        mov             x13, #0                     // dy: pad top
1:
        mov             x16, #-2                    // dx
        add             x14, src0, x13, lsl #8      // local src0
        add             x15, src1, x13, lsl #8      // local src1
        add             x17, x16, x13, lsl #5
        ldr             q0, [x14, x16]
        ldr             q1, [x15, x16]
        ldr             q2, [gh0, x17]
        ldr             q3, [gh1, x17]
        ldr             q4, [gv0, x17]
        ldr             q5, [gv1, x17]
        add             x16, x16, #8
        add             x17, x17, #8
        ins             v0.s[3], v6.s[3]
        ins             v1.s[3], v6.s[3]
        ins             v2.s[3], v6.s[3]
        ins             v3.s[3], v6.s[3]
        ins             v4.s[3], v6.s[3]
        ins             v5.s[3], v6.s[3]

        ldr             q16, [x14, x16]
        ldr             q17, [x15, x16]
        ldr             q18, [gh0, x17]
        ldr             q19, [gh1, x17]
        ldr             q20, [gv0, x17]
        ldr             q21, [gv1, x17]
        ins             v16.s[3], v6.s[3]
        ins             v17.s[3], v6.s[3]
        ins             v18.s[3], v6.s[3]
        ins             v19.s[3], v6.s[3]
        ins             v20.s[3], v6.s[3]
        ins             v21.s[3], v6.s[3]

        tbz             pad_mask, #0, 20f
        // pad left
        ins             v0.h[0], v0.h[1]
        ins             v1.h[0], v1.h[1]
        ins             v2.h[0], v2.h[1]
        ins             v3.h[0], v3.h[1]
        ins             v4.h[0], v4.h[1]
        ins             v5.h[0], v5.h[1]
20:
        tbz             pad_mask, #2, 21f
        // pad right
        ins             v16.h[5], v16.h[4]
        ins             v17.h[5], v17.h[4]
        ins             v18.h[5], v18.h[4]
        ins             v19.h[5], v19.h[4]
        ins             v20.h[5], v20.h[4]
        ins             v21.h[5], v21.h[4]
21:
        sshr            v0.8h, v0.8h, #4
        sshr            v1.8h, v1.8h, #4
        add             v2.8h, v2.8h, v3.8h
        add             v4.8h, v4.8h, v5.8h
        sub             v0.8h, v0.8h, v1.8h         // diff
        sshr            v2.8h, v2.8h, #1            // temph
        sshr            v4.8h, v4.8h, #1            // tempv

        sshr            v16.8h, v16.8h, #4
        sshr            v17.8h, v17.8h, #4
        add             v18.8h, v18.8h, v19.8h
        add             v20.8h, v20.8h, v21.8h
        sub             v16.8h, v16.8h, v17.8h      // diff
        sshr            v18.8h, v18.8h, #1          // temph
        sshr            v20.8h, v20.8h, #1          // tempv

        abs             v3.8h, v2.8h
        abs             v5.8h, v4.8h
        uxtl            v19.4s, v3.4h
        uxtl            v21.4s, v5.4h
        uxtl2           v3.4s, v3.8h
        uxtl2           v5.4s, v5.8h
        add             v3.4s, v3.4s, v19.4s
        add             v5.4s, v5.4s, v21.4s
        add             sgx2_v.4s, sgx2_v.4s, v3.4s
        add             sgy2_v.4s, sgy2_v.4s, v5.4s

        abs             v3.8h, v18.8h
        abs             v5.8h, v20.8h
        uxtl            v19.4s, v3.4h
        uxtl            v21.4s, v5.4h
        uxtl2           v3.4s, v3.8h
        uxtl2           v5.4s, v5.8h
        add             v3.4s, v3.4s, v19.4s
        add             v5.4s, v5.4s, v21.4s
        add             sgx2_v2.4s, sgx2_v2.4s, v3.4s
        add             sgy2_v2.4s, sgy2_v2.4s, v5.4s

        cmgt            v17.8h, v4.8h, #0
        cmlt            v7.8h, v4.8h, #0
        cmgt            v19.8h, v20.8h, #0
        cmlt            v21.8h, v20.8h, #0
        sub             v17.8h, v7.8h, v17.8h       // VVC_SIGN(tempv)
        sub             v19.8h, v21.8h, v19.8h      // VVC_SIGN(tempv)

        smlal           sgxgy_v.4s, v17.4h, v2.4h
        smlal2          sgxgy_v.4s, v17.8h, v2.8h
        smlsl           sgydi_v.4s, v17.4h, v0.4h
        smlsl2          sgydi_v.4s, v17.8h, v0.8h

        cmgt            v3.8h, v2.8h, #0
        cmlt            v5.8h, v2.8h, #0
        cmgt            v17.8h, v18.8h, #0
        cmlt            v21.8h, v18.8h, #0
        sub             v3.8h, v5.8h, v3.8h         // VVC_SIGN(temph)
        sub             v17.8h, v21.8h, v17.8h      // VVC_SIGN(temph)

        smlal           sgxgy_v2.4s, v19.4h, v18.4h
        smlal2          sgxgy_v2.4s, v19.8h, v18.8h
        smlsl           sgydi_v2.4s, v19.4h, v16.4h
        smlsl2          sgydi_v2.4s, v19.8h, v16.8h

        smlsl           sgxdi_v.4s, v3.4h, v0.4h
        smlsl2          sgxdi_v.4s, v3.8h, v0.8h
        smlsl           sgxdi_v2.4s, v17.4h, v16.4h
        smlsl2          sgxdi_v2.4s, v17.8h, v16.8h
3:
        add             y, y, #1
        cmp             y, #(BDOF_MIN_BLOCK_SIZE)
        mov             x13, y
        b.gt            4f
        b.lt            1b
        tbz             pad_mask, #3, 1b
        sub             x13, x13, #1                // pad bottom
        b               1b
4:
        addv            s22, sgx2_v.4s
        addv            s23, sgy2_v.4s
        addv            s24, sgxgy_v.4s
        addv            s25, sgxdi_v.4s
        addv            s26, sgydi_v.4s

        mov             w3, #31
        mov             w16, #-15
        mov             w17, #15
40:
        mov             w14, #0

        mov             sgx2, v22.s[0]
        mov             sgy2, v23.s[0]
        mov             sgxgy, v24.s[0]
        mov             sgxdi, v25.s[0]
        mov             sgydi, v26.s[0]

        cbz             sgx2, 5f
        clz             w12, sgx2
        lsl             sgxdi, sgxdi, #2
        sub             w13, w3, w12                // log2(sgx2)
        asr             sgxdi, sgxdi, w13
        cmp             sgxdi, w16
        csel            w14, w16, sgxdi, lt         // clip to -15
        b.le            5f
        cmp             sgxdi, w17
        csel            w14, w17, sgxdi, gt         // clip to 15
5:
        strh            w14, [vx], #2

        mov             w15, #0
        cbz             sgy2, 6f
        lsl             sgydi, sgydi, #2
        smull           x14, w14, sgxgy
        asr             w14, w14, #1
        sub             sgydi, sgydi, w14
        clz             w12, sgy2
        sub             w13, w3, w12                // log2(sgy2)
        asr             sgydi, sgydi, w13
        cmp             sgydi, w16
        csel            w15, w16, sgydi, lt         // clip to -15
        b.le            6f
        cmp             sgydi, w17
        csel            w15, w17, sgydi, gt         // clip to 15
6:
        strh            w15, [vy], #2
        cbz             x0, 7f
        addv            s22, sgx2_v2.4s
        addv            s23, sgy2_v2.4s
        addv            s24, sgxgy_v2.4s
        addv            s25, sgxdi_v2.4s
        addv            s26, sgydi_v2.4s
        mov             x0, #0
        b               40b
7:
        ret

.unreq src0
.unreq src1
.unreq pad_mask
.unreq gh
.unreq gv
.unreq vx
.unreq vy
.unreq sgx2
.unreq sgy2
.unreq sgxgy
.unreq sgxdi
.unreq sgydi
.unreq sgx2_v
.unreq sgy2_v
.unreq sgxgy_v
.unreq sgxdi_v
.unreq sgydi_v
.unreq sgx2_v2
.unreq sgy2_v2
.unreq sgxgy_v2
.unreq sgxdi_v2
.unreq sgydi_v2
.unreq y
endfunc
