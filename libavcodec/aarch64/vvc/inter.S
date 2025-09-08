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

function ff_vvc_dmvr_v_8_neon, export=1
        movrel          x7, X(ff_vvc_inter_luma_dmvr_filters)
        add             x7, x7, x5, lsl #1
        ld2r            {v0.16b, v1.16b}, [x7]
        tbz             w6, #4, 12f

        ldr             s16, [x1, #16]
        ld1             {v2.16b}, [x1], x2
20:
        ldr             s17, [x1, #16]
        umull           v4.8h, v0.8b, v2.8b
        umull2          v5.8h, v0.16b, v2.16b
        ld1             {v3.16b}, [x1], x2
        umull           v16.8h, v0.8b, v16.8b
        umull           v6.8h, v1.8b, v3.8b
        umull2          v7.8h, v1.16b, v3.16b
        add             v4.8h, v4.8h, v6.8h
        umull           v18.8h, v1.8b, v17.8b
        add             v5.8h, v5.8h, v7.8h
        urshr           v4.8h, v4.8h, #2
        add             v19.4h, v16.4h, v18.4h
        urshr           v5.8h, v5.8h, #2
        urshr           v19.4h, v19.4h, #2
        st1             {v4.8h, v5.8h}, [x0], #32
        subs            w3, w3, #1
        mov             v2.16b, v3.16b
        st1             {v19.4h}, [x0], #8
        mov             v16.16b, v17.16b
        add             x0, x0, #(VVC_MAX_PB_SIZE * 2 - 32 - 8)
        b.ne            20b
        ret

12:
        ldr             s16, [x1, #8]
        ld1             {v2.8b}, [x1], x2
2:
        ldr             s17, [x1, #8]
        umull           v4.8h, v0.8b, v2.8b
        ld1             {v3.8b}, [x1], x2
        umull           v16.8h, v0.8b, v16.8b
        umull           v6.8h, v1.8b, v3.8b
        add             v4.8h, v4.8h, v6.8h
        umull           v18.8h, v1.8b, v17.8b
        srshr           v4.8h, v4.8h, #2
        add             v19.4h, v16.4h, v18.4h
        srshr           v19.4h, v19.4h, #2
        st1             {v4.8h}, [x0], #16
        subs            w3, w3, #1
        mov             v2.16b, v3.16b
        st1             {v19.4h}, [x0], #8
        mov             v16.16b, v17.16b
        add             x0, x0, #(VVC_MAX_PB_SIZE * 2 - 16 - 8)
        b.ne            2b
        ret
endfunc

function ff_vvc_dmvr_h_8_neon, export=1
        movrel          x7, X(ff_vvc_inter_luma_dmvr_filters)
        add             x7, x7, x4, lsl #1
        ld2r            {v0.16b, v1.16b}, [x7]
        tbz             w6, #4, 12f
20:
        ldur            q3, [x1, #1]
        ldr             q2, [x1]
        umull           v4.8h, v0.8b, v2.8b
        umull2          v5.8h, v0.16b, v2.16b
        ldur            s17, [x1, #17]
        umull           v6.8h, v1.8b, v3.8b
        ldr             s16, [x1, #16]
        umull2          v7.8h, v1.16b, v3.16b
        add             v4.8h, v4.8h, v6.8h
        umull           v17.8h, v1.8b, v17.8b
        add             v5.8h, v5.8h, v7.8h
        umull           v16.8h, v0.8b, v16.8b
        srshr           v4.8h, v4.8h, #2
        add             v16.4h, v16.4h, v17.4h
        srshr           v5.8h, v5.8h, #2
        srshr           v16.4h, v16.4h, #2
        st1             {v4.8h, v5.8h}, [x0], #32
        subs            w3, w3, #1
        st1             {v16.4h}, [x0], #8
        add             x1, x1, x2
        add             x0, x0, #(VVC_MAX_PB_SIZE * 2 - 32 - 8)
        b.ne            20b
        ret

12:
        ldur            d3, [x1, #1]
        ldr             d2, [x1]
        umull           v4.8h, v0.8b, v2.8b
        ldur            s17, [x1, #9]
        umull           v6.8h, v1.8b, v3.8b
        ldr             s16, [x1, #8]
        add             v4.8h, v4.8h, v6.8h
        umull           v17.8h, v1.8b, v17.8b
        umull           v16.8h, v0.8b, v16.8b
        srshr           v4.8h, v4.8h, #2
        add             v16.4h, v16.4h, v17.4h
        srshr           v16.4h, v16.4h, #2
        st1             {v4.8h}, [x0], #16
        subs            w3, w3, #1
        st1             {v16.4h}, [x0], #8
        add             x1, x1, x2
        add             x0, x0, #(VVC_MAX_PB_SIZE * 2 - 16 - 8)
        b.ne            12b
        ret
endfunc

.macro vvc_dmvr_h_10 bit_depth
function ff_vvc_dmvr_h_\bit_depth\()_neon, export=1
        movrel          x7, X(ff_vvc_inter_luma_dmvr_filters)
        add             x7, x7, x4, lsl #1
        ld2r            {v0.16b, v1.16b}, [x7]
        uxtl            v0.8h, v0.8b
        uxtl            v1.8h, v1.8b
        tbz             w6, #4, 12f
20:
        ldur            q3, [x1, #2]
        ldr             q2, [x1]
        ldr             q22, [x1, #16]
        mul             v4.8h, v0.8h, v2.8h
        mul             v6.8h, v1.8h, v3.8h
        ldur            q23, [x1, #18]
        mul             v5.8h, v0.8h, v22.8h
        ldur            d17, [x1, #34]
        mul             v7.8h, v1.8h, v23.8h
        uhadd           v4.8h, v4.8h, v6.8h
        ldr             d16, [x1, #32]
        uhadd           v5.8h, v5.8h, v7.8h
        mul             v17.4h, v1.4h, v17.4h
        mul             v16.4h, v0.4h, v16.4h
        urshr           v4.8h, v4.8h, #(\bit_depth - 6 - 1)
        urshr           v5.8h, v5.8h, #(\bit_depth - 6 - 1)
        uhadd           v16.4h, v16.4h, v17.4h
        urshr           v16.4h, v16.4h, #(\bit_depth - 6 - 1)
        st1             {v4.8h, v5.8h}, [x0], #32
        subs            w3, w3, #1
        st1             {v16.4h}, [x0], #8
        add             x1, x1, x2
        add             x0, x0, #(VVC_MAX_PB_SIZE * 2 - 32 - 8)
        b.ne            20b
        ret

12:
        ldur            q3, [x1, #2]
        ldr             q2, [x1]
        mul             v4.8h, v0.8h, v2.8h
        ldur            d17, [x1, #18]
        mul             v6.8h, v1.8h, v3.8h
        ldr             d16, [x1, #16]
        uhadd           v4.8h, v4.8h, v6.8h
        mul             v17.4h, v1.4h, v17.4h
        mul             v16.4h, v0.4h, v16.4h
        urshr           v4.8h, v4.8h, #(\bit_depth - 6 - 1)
        uhadd           v16.4h, v16.4h, v17.4h
        urshr           v16.4h, v16.4h, #(\bit_depth - 6 - 1)
        st1             {v4.8h}, [x0], #16
        subs            w3, w3, #1
        st1             {v16.4h}, [x0], #8
        add             x1, x1, x2
        add             x0, x0, #(VVC_MAX_PB_SIZE * 2 - 16 - 8)
        b.ne            12b
        ret
endfunc
.endm

vvc_dmvr_h_10 10
vvc_dmvr_h_10 12

function ff_vvc_dmvr_hv_8_neon, export=1
        tmp0            .req x7
        tmp1            .req x8

        sub             sp, sp, #(VVC_MAX_PB_SIZE * 4)

        movrel          x9, X(ff_vvc_inter_luma_dmvr_filters)
        add             x12, x9, mx, lsl #1
        mov             tmp0, sp
        add             tmp1, tmp0, #(VVC_MAX_PB_SIZE * 2)
        // We know the value are positive
        ld2r            {v0.16b, v1.16b}, [x12]

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
        umull           v6.8h, v4.8b, v0.8b
        umull2          v16.8h, v4.16b, v0.16b
        umlal           v6.8h, v5.8b, v1.8b
        umlal2          v16.8h, v5.16b, v1.16b
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
        umull           v6.8h, v4.8b, v0.8b
        umlal           v6.8h, v5.8b, v1.8b
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
        umull           v6.8h, v4.8b, v0.8b
        umlal           v6.8h, v5.8b, v1.8b
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
        mvni            v29.4s, #(12 - 6 - 1)
        b               0f
endfunc

function ff_vvc_dmvr_hv_10_neon, export=1
        mvni            v29.4s, #(10 - 6 - 1)
0:
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
        dup             v2.8h, w10                  // filter_y[0]
        dup             v3.8h, w11                  // filter_y[1]

        // Valid value for width can only be 8 + 4, 16 + 4
        cmp             width, #16
        mov             w10, #0                     // start filter_y or not
        add             height, height, #1
        sub             dst, dst, #(VVC_MAX_PB_SIZE * 2)
        sub             src_stride, src_stride, w6, sxtw #1
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

        urshl           v4.4s, v4.4s, v29.4s
        urshl           v5.4s, v5.4s, v29.4s
        urshl           v18.4s, v18.4s, v29.4s
        urshl           v19.4s, v19.4s, v29.4s
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
        uqrshrn         v6.4h, v17.4s, #4
        uqrshrn2        v6.8h, v18.4s, #4
        uqrshrn         v7.4h, v19.4s, #4
        uqrshrn2        v7.8h, v20.4s, #4
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

        urshl           v4.4s, v4.4s, v29.4s
        urshl           v5.4s, v5.4s, v29.4s
        uqxtn           v6.4h, v4.4s
        uqxtn2          v6.8h, v5.4s
        str             q6, [x13], #16

        cbz             w10, 3f

        ldr             q16, [x12], #16
        umull           v17.4s, v16.4h, v2.4h
        umull2          v18.4s, v16.8h, v2.8h
        umlal           v17.4s, v6.4h, v3.4h
        umlal2          v18.4s, v6.8h, v3.8h
        urshr           v17.4s, v17.4s, #4
        urshr           v18.4s, v18.4s, #4
        uqxtn           v16.4h, v17.4s
        uqxtn2          v16.8h, v18.4s
        str             q16, [x14], #16
3:
        ldur            d7, [src, #2]
        ldr             d6, [src], #8
        umull           v4.4s, v7.4h, v1.4h
        umlal           v4.4s, v6.4h, v0.4h
        urshl           v4.4s, v4.4s, v29.4s
        uqxtn           v6.4h, v4.4s
        str             d6, [x13], #8

        cbz             w10, 4f

        ldr             d16, [x12], #8
        umull           v17.4s, v16.4h, v2.4h
        umlal           v17.4s, v6.4h, v3.4h
        urshr           v17.4s, v17.4s, #4
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

function vvc_bdof_grad_filter_8x_neon, export=0
        gh0             .req x0
        gh1             .req x1
        gv0             .req x2
        gv1             .req x3
        src0            .req x4
        src1            .req x5
        width           .req w6
        height          .req w7
        tbnz            w6, #4, 16f

8:
        ldur            q0, [src0, #2]
        ldur            q1, [src0, #-2]
        ldr             q2, [src0, #(VVC_MAX_PB_SIZE << 1)]
        ldr             q3, [src0, #-(VVC_MAX_PB_SIZE << 1)]
        sshr            v0.8h, v0.8h, #6
        sshr            v1.8h, v1.8h, #6
        ldur            q4, [src1, #2]
        ldur            q5, [src1, #-2]
        sshr            v2.8h, v2.8h, #6
        sshr            v3.8h, v3.8h, #6
        ldr             q6, [src1, #(VVC_MAX_PB_SIZE << 1)]
        ldr             q7, [src1, #-(VVC_MAX_PB_SIZE << 1)]
        // results of gradient_h0
        sub             v0.8h, v0.8h, v1.8h
        // results of gradient_v0
        sub             v2.8h, v2.8h, v3.8h

        sshr            v4.8h, v4.8h, #6
        sshr            v5.8h, v5.8h, #6
        sshr            v6.8h, v6.8h, #6
        sshr            v7.8h, v7.8h, #6
        // results of gradient_h1
        sub             v4.8h, v4.8h, v5.8h
        // results of gradient_v1
        sub             v6.8h, v6.8h, v7.8h

        // (gradient_h0 + gradient_h1) >> 1
        shadd           v1.8h, v0.8h, v4.8h
        // gradient_h0 - gradient_h1
        sub             v5.8h, v0.8h, v4.8h

        // (gradient_v0 + gradient_v1) >> 1
        shadd           v3.8h, v2.8h, v6.8h
        // gradient_v0 - gradient_v1
        sub             v7.8h, v2.8h, v6.8h

        st1             {v1.8h}, [gh0]
        st1             {v5.8h}, [gh1]
        st1             {v3.8h}, [gv0]
        st1             {v7.8h}, [gv1]

        subs            height, height, #1
        add             gh0, gh0, #(BDOF_BLOCK_SIZE << 1)
        add             gv0, gv0, #(BDOF_BLOCK_SIZE << 1)
        add             src0, src0, #(VVC_MAX_PB_SIZE << 1)
        add             gh1, gh1, #(BDOF_BLOCK_SIZE << 1)
        add             gv1, gv1, #(BDOF_BLOCK_SIZE << 1)
        add             src1, src1, #(VVC_MAX_PB_SIZE << 1)
        b.ne            8b
        ret

16:
        ldur            q0, [src0, #2]
        ldur            q1, [src0, #18]
        ldur            q16, [src0, #-2]
        sshr            v0.8h, v0.8h, #6
        ldur            q17, [src0, #14]
        sshr            v1.8h, v1.8h, #6
        ldp             q18, q19, [src0, #-(VVC_MAX_PB_SIZE << 1)]
        sshr            v16.8h, v16.8h, #6
        ldp             q2, q3, [src0, #(VVC_MAX_PB_SIZE << 1)]!
        ldur            q20, [src1, #2]
        sshr            v17.8h, v17.8h, #6
        ldur            q21, [src1, #18]
        sshr            v2.8h, v2.8h, #6
        ldur            q22, [src1, #-2]
        sshr            v3.8h, v3.8h, #6
        ldur            q23, [src1, #14]
        sshr            v18.8h, v18.8h, #6
        ldp             q26, q27, [src1, #-(VVC_MAX_PB_SIZE << 1)]
        sshr            v19.8h, v19.8h, #6
        ldp             q24, q25, [src1, #(VVC_MAX_PB_SIZE << 1)]!

        // results of gradient_h0
        sub             v0.8h, v0.8h, v16.8h
        sub             v1.8h, v1.8h, v17.8h

        // results of gradient_v0
        sub             v2.8h, v2.8h, v18.8h
        sub             v3.8h, v3.8h, v19.8h

        sshr            v20.8h, v20.8h, #6
        sshr            v21.8h, v21.8h, #6
        sshr            v22.8h, v22.8h, #6
        sshr            v23.8h, v23.8h, #6

        // results of gradient_h1
        sub             v20.8h, v20.8h, v22.8h
        sub             v21.8h, v21.8h, v23.8h

        sshr            v24.8h, v24.8h, #6
        sshr            v25.8h, v25.8h, #6

        // gradient_h0 - gradient_h1
        sub             v22.8h, v0.8h, v20.8h
        sub             v23.8h, v1.8h, v21.8h

        // (gradient_h0 + gradient_h1) >> 1
        shadd           v16.8h, v0.8h, v20.8h
        shadd           v17.8h, v1.8h, v21.8h

        st1             {v22.8h, v23.8h}, [gh1], #32

        sshr            v26.8h, v26.8h, #6
        sshr            v27.8h, v27.8h, #6

        st1             {v16.8h, v17.8h}, [gh0], #32

        // results of gradient_v1
        sub             v24.8h, v24.8h, v26.8h
        sub             v25.8h, v25.8h, v27.8h

        // (gradient_v0 + gradient_v1) >> 1
        shadd           v18.8h, v2.8h, v24.8h
        shadd           v19.8h, v3.8h, v25.8h

        // gradient_v0 - gradient_v1
        sub             v26.8h, v2.8h, v24.8h
        sub             v27.8h, v3.8h, v25.8h

        st1             {v18.8h,v19.8h}, [gv0], #32

        subs            height, height, #1
        st1             {v26.8h,v27.8h}, [gv1], #32

        b.ne            16b
        ret

.unreq gh0
.unreq gh1
.unreq gv0
.unreq gv1
.unreq src0
.unreq src1
.unreq width
.unreq height
endfunc

.macro vvc_apply_bdof_block_8x bit_depth
        dst             .req x0
        dst_stride      .req x1
        src0            .req x2
        src1            .req x3
        gh              .req x4
        gv              .req x5
        vx              .req x6
        vy              .req x7

        ldr             w8, [sp]
        mov             x12, #(BDOF_BLOCK_SIZE * 2)
        mov             x14, #(VVC_MAX_PB_SIZE * 2)
.if \bit_depth >= 10
        // clip pixel
        mov             w15, #((1 << \bit_depth) - 1)
        dup             v19.8h, w15
.endif

0:
        ldr             s0, [vx], #(2 * BDOF_MIN_BLOCK_SIZE)
        ldr             s1, [vy], #(2 * BDOF_MIN_BLOCK_SIZE)
        mov             w13, #(BDOF_MIN_BLOCK_SIZE)
1:
        ld1             {v5.8h}, [src0], x14
        ld1             {v6.8h}, [src1], x14

        saddl           v17.4s, v5.4h, v6.4h
        ld1             {v4.8h}, [gv], x12
        saddl2          v16.4s, v5.8h, v6.8h
        ld1             {v2.8h}, [gh], x12
        smlal           v17.4s, v4.4h, v1.h[0]
        smlal2          v16.4s, v4.8h, v1.h[1]
        smlal           v17.4s, v2.4h, v0.h[0]
        smlal2          v16.4s, v2.8h, v0.h[1]

        sqrshrun        v5.4h, v17.4s, #(15 - \bit_depth)
        sqrshrun2       v5.8h, v16.4s, #(15 - \bit_depth)
        subs            w13, w13, #1
.if \bit_depth == 8
        sqxtun          v5.8b, v5.8h
        st1             {v5.8b}, [dst], dst_stride
.else
        smin            v5.8h, v5.8h, v19.8h
        st1             {v5.8h}, [dst], dst_stride
.endif
        b.ne            1b

        subs            w8, w8, #(BDOF_MIN_BLOCK_SIZE)
        b.ne            0b
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

function vvc_apply_bdof_block_8x_8_neon, export=0
        vvc_apply_bdof_block_8x 8
endfunc

function vvc_apply_bdof_block_8x_10_neon, export=0
        vvc_apply_bdof_block_8x 10
endfunc

function vvc_apply_bdof_block_8x_12_neon, export=0
        vvc_apply_bdof_block_8x 12
endfunc

.macro vvc_apply_bdof_block_16x bit_depth
        dst             .req x0
        dst_stride      .req x1
        src0            .req x2
        src1            .req x3
        gh              .req x4
        gv              .req x5
        vx              .req x6
        vy              .req x7

        ldr             w8, [sp]
        movi            v7.4s, #(1 << (14 - \bit_depth))
.if \bit_depth >= 10
        // clip pixel
        mov             w15, #((1 << \bit_depth) - 1)
        movi            v18.8h, #0
        dup             v19.8h, w15
.endif

0:
        ld1r            {v0.8h}, [vx], #2
        ld1r            {v1.8h}, [vy], #2
        ld1r            {v2.8h}, [vx], #2
        ld1r            {v3.8h}, [vy], #2

        mov             w13, #(BDOF_MIN_BLOCK_SIZE)

        ld1r            {v20.8h}, [vx], #2
        ld1r            {v21.8h}, [vy], #2
        ld1r            {v22.8h}, [vx], #2
        ld1r            {v23.8h}, [vy], #2

        ins             v0.d[1], v2.d[1]
        ins             v1.d[1], v3.d[1]
        ins             v20.d[1], v22.d[1]
        ins             v21.d[1], v23.d[1]
1:
        ldp             q2, q22, [gh], #(BDOF_BLOCK_SIZE * 2)
        ldp             q4, q24, [gv], #(BDOF_BLOCK_SIZE * 2)
        smull           v3.4s, v0.4h, v2.4h
        smull2          v16.4s, v0.8h, v2.8h
        smlal           v3.4s, v1.4h, v4.4h
        smlal2          v16.4s, v1.8h, v4.8h

        ldp             q5, q25, [src0], #(VVC_MAX_PB_SIZE * 2)
        ldp             q6, q26, [src1], #(VVC_MAX_PB_SIZE * 2)

        smull           v23.4s, v20.4h, v22.4h
        smull2          v27.4s, v20.8h, v22.8h
        smlal           v23.4s, v21.4h, v24.4h
        smlal2          v27.4s, v21.8h, v24.8h

        saddl           v2.4s, v5.4h, v6.4h
        add             v2.4s, v2.4s, v7.4s
        add             v2.4s, v2.4s, v3.4s
        saddl2          v4.4s, v5.8h, v6.8h
        add             v4.4s, v4.4s, v7.4s
        add             v4.4s, v4.4s, v16.4s

        saddl           v22.4s, v25.4h, v26.4h
        add             v22.4s, v22.4s, v7.4s
        add             v22.4s, v22.4s, v23.4s
        saddl2          v24.4s, v25.8h, v26.8h
        add             v24.4s, v24.4s, v7.4s
        add             v24.4s, v24.4s, v27.4s

        sqshrn          v5.4h, v2.4s, #(15 - \bit_depth)
        sqshrn2         v5.8h, v4.4s, #(15 - \bit_depth)
        sqshrn          v25.4h, v22.4s, #(15 - \bit_depth)
        sqshrn2         v25.8h, v24.4s, #(15 - \bit_depth)

        subs            w13, w13, #1
.if \bit_depth == 8
        sqxtun          v5.8b, v5.8h
        sqxtun2         v5.16b, v25.8h
        str             q5, [dst]
.else
        smin            v5.8h, v5.8h, v19.8h
        smax            v5.8h, v5.8h, v18.8h
        smin            v25.8h, v25.8h, v19.8h
        smax            v25.8h, v25.8h, v18.8h
        stp             q5, q25, [dst]
.endif
        add             dst, dst, dst_stride
        b.ne            1b

        subs            w8, w8, #(BDOF_MIN_BLOCK_SIZE)
        b.ne            0b
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

function vvc_apply_bdof_block_16x_8_neon, export=0
        vvc_apply_bdof_block_16x 8
endfunc

function vvc_apply_bdof_block_16x_10_neon, export=0
        vvc_apply_bdof_block_16x 10
endfunc

function vvc_apply_bdof_block_16x_12_neon, export=0
        vvc_apply_bdof_block_16x 12
endfunc

const bdof_vx_vy_8x_tbl
        .byte 0, 1, 16, 16, 16, 16, 8, 9
        .byte 6, 7, 16, 16, 16, 16, 14, 15
endconst

const bdof_vx_vy_16x_tbl
        .byte 0,  1,  64, 64, 64, 64, 8,  9
        .byte 6,  7,  64, 64, 64, 64, 16, 17
        .byte 14, 15, 64, 64, 64, 64, 24, 25
        .byte 22, 23, 64, 64, 64, 64, 30, 31
endconst

// line(-1), line0, line1, line2, line3, line4
// line3 and line4 becomes line(-1) and line0 in the next block.
.macro bdof_vx_vy_8x_save_line tmp0, tmp1, tmp2, tmp3, tmp4
        mov             \tmp0\().16b, v28.16b
        mov             \tmp1\().16b, v29.16b
        mov             \tmp2\().16b, v30.16b
        mov             \tmp3\().16b, v31.16b
        mov             \tmp4\().16b, v8.16b
.endm

.macro bdof_vx_vy_8x_add_line tmp0, tmp1, tmp2, tmp3, tmp4
        add             v25.4s, v25.4s, \tmp0\().4s
        add             v27.4s, v27.4s, \tmp1\().4s
        add             v23.4s, v23.4s, \tmp2\().4s
        sub             v26.4s, v26.4s, \tmp3\().4s
        sub             v24.4s, v24.4s, \tmp4\().4s
.endm

.macro bdof_vx_vy_8x_padding_left_right src, tmp0, tmp1, dst
        tbl             \tmp0\().16b, { \src\().16b }, v0.16b
        saddl           \tmp1\().4s, \tmp0\().4h, \src\().4h
        saddl2          \dst\().4s, \tmp0\().8h, \src\().8h
        addp            \dst\().4s, \tmp1\().4s, \dst\().4s
.endm

.macro bdof_vx_vy_sign src, tmp0, tmp1, dst
        cmlt            \tmp0\().8h, \src\().8h, #0
        cmgt            \tmp1\().8h, \src\().8h, #0
        sub             \dst\().8h, \tmp0\().8h, \tmp1\().8h
.endm

.macro bdof_vx_vy_clip_mask src, max, min, mask, dst
        smin            \src\().4s, \src\().4s, \max\().4s
        smax            \src\().4s, \src\().4s, \min\().4s
        cmgt            \mask\().4s, \mask\().4s, #0
        and             \dst\().16b, \src\().16b, \mask\().16b
.endm

.macro bdof_vx_vy_16x_save_line tmp0, tmp1, tmp2, tmp3, tmp4
        mov             \tmp0\().16b, v29.16b
        mov             \tmp1\().16b, v30.16b
        mov             \tmp2\().16b, v31.16b
        mov             \tmp3\().16b, v8.16b
        mov             \tmp4\().16b, v9.16b
.endm

.macro bdof_vx_vy_16x_add_line tmp0, tmp1, tmp2, tmp3, tmp4
        add             v25.4s, v25.4s, \tmp0\().4s
        add             v24.4s, v24.4s, \tmp1\().4s
        add             v26.4s, v26.4s, \tmp2\().4s
        sub             v28.4s, v28.4s, \tmp3\().4s
        sub             v27.4s, v27.4s, \tmp4\().4s
.endm

.macro bdof_vx_vy_16x_padding_left_right src0, src1, tmp0, tmp1, tmp2, dst
        tbl             \tmp0\().16b, {\src0\().16b, \src1\().16b}, v0.16b
        tbl             v2.16b, {\src0\().16b, \src1\().16b}, v1.16b
        saddl           \tmp1\().4s, \tmp0\().4h, \src0\().4h
        saddl           \tmp2\().4s, v2.4h, \src1\().4h
        saddl2          \tmp0\().4s, \tmp0\().8h, \src0\().8h
        saddl2          \dst\().4s, v2.8h, \src1\().8h
        addp            \tmp0\().4s, \tmp1\().4s, \tmp0\().4s
        addp            \dst\().4s, \tmp2\().4s, \dst\().4s
        addp            \dst\().4s, \tmp0\().4s, \dst\().4s
.endm

/*
 * Line tricks:
 * We need 6 lines of information, from 4N-1, 4N, 4N+1 to 4N+4. 4N-1
 * and 4N+0 are processed in the last group, so they can be reused.
 *
 * (4N-1) [xxxxxxxxxxxxx] <--- reuse
 * (4N)   [xxxxxxxxxxxxx] <--- reuse
 * (4N+1) [xxxxxxxxxxxxx]
 * (4N+2) [xxxxxxxxxxxxx]
 * (4N+3) [xxxxxxxxxxxxx] ---> save for reuse
 * (4N+4) [xxxxxxxxxxxxx] ---> save for reuse
 *
 * Special case:
 * 1. Line -1 needs to duplicate line 0.
 * 2. Last line +1 needs to duplicate the last line.
 *
 * ---------------------------------------------------------------------
 * Pixel tricks:
 *
 * [C-1, C0, C1, C2, ... C16]
 *
 * For each line, we need to sum parameters for 4 * 6 pixels:
 * - C-1 + C0 + C1 + C2 + C3 + C4
 * - C3 + C4 + C5 + C6 + C7 + C8
 * - C7 + C8 + C9 + C10 + C11 + C12
 * - C11 + C12 + C13 + C14 + C15 + C16
 *
 * C-1 is C0, C16 is C15, so we can do:
 *
 * [C0, C1, C2, C3, | C4, C5, C6, C7, | C8, ... C15]
 *         +        |      +          |
 * [C0,  0,  0, C4, | C3,  0,  0, C8, | C7, ... C15]
 *
 * 8x is similar.
 * ----------------------------------------------------------------------
 * x0: const int16_t *_src0,
 * x1: const int16_t *_src1,
 * x2: const int16_t *gradient_h,
 * x3: const int16_t *gradient_v,
 * x4: int16_t vx[16],
 * x5: int16_t vy[16],
 * w6: int block_h
 */
function vvc_derive_bdof_vx_vy_8x_neon, export=0
        stp             d11, d10, [sp, #-0x20]!
        stp             d9, d8, [sp, #0x10]

        movrel          x11, bdof_vx_vy_8x_tbl
        ldr             q0, [x11]                           // table
        mvni            v2.4s, #30                          // -31, for log2
        movi            v3.4s, #15                          // clip to 15
        mvni            v4.4s, #14                          // clip to -15

        mov             w11, #0x8
        mov             w12, w6                             // y = block_h
        b               4f

1:
        // save line4 results
        bdof_vx_vy_8x_save_line v5, v6, v7, v16, v17
2:
        addp            v25.4s, v25.4s, v25.4s
        addp            v27.4s, v27.4s, v27.4s
        addp            v26.4s, v26.4s, v26.4s
        addp            v23.4s, v23.4s, v23.4s
        addp            v24.4s, v24.4s, v24.4s

        clz             v28.4s, v25.4s
        add             v28.4s, v28.4s, v2.4s               // log2
        shl             v26.4s, v26.4s, #0x2
        sshl            v26.4s, v26.4s, v28.4s

        bdof_vx_vy_clip_mask v26, v3, v4, v25, v25
        sqxtn           v26.4h, v25.4s
        st1             {v26.s}[0], [x4], x11

        subs            x12, x12, #(BDOF_MIN_BLOCK_SIZE)

        clz             v26.4s, v27.4s
        add             v26.4s, v26.4s, v2.4s
        shl             v24.4s, v24.4s, #0x2
        mul             v23.4s, v25.4s, v23.4s
        sshr            v23.4s, v23.4s, #0x1
        sub             v23.4s, v24.4s, v23.4s
        sshl            v23.4s, v23.4s, v26.4s

        bdof_vx_vy_clip_mask v23, v3, v4, v27, v23
        sqxtn           v23.4h, v23.4s
        st1             {v23.s}[0], [x5], x11

        b.eq            16f
4:
        mov             x15, #0x0                           // dy, inner loop

        movi            v25.2d, #0
        movi            v27.2d, #0
        movi            v23.2d, #0
        movi            v26.2d, #0
        movi            v24.2d, #0
        b               8f

5:
        // add line(-1) and line0 from previous results
        bdof_vx_vy_8x_add_line v18, v19, v20, v21, v22
        bdof_vx_vy_8x_add_line v5, v6, v7, v16, v17
        add             x15, x15, #1
8:
        cmp             w12, w6
        b.hs            9f
        // y < block_h && dy == 0, reuse previous results
        cbz             x15, 5b
9:
        ldr             q28, [x0]                                   // src0
        ldr             q29, [x1]                                   // src1
        ldr             q30, [x2], #(BDOF_BLOCK_SIZE * 2)           // (gh0 + gh1) >> 1
        ldr             q31, [x3], #(BDOF_BLOCK_SIZE * 2)           // (gv0 + gv1) >> 1
        add             x0, x0, #(VVC_MAX_PB_SIZE * 2)
        add             x1, x1, #(VVC_MAX_PB_SIZE * 2)

        sshr            v28.8h, v28.8h, #0x4
        sshr            v29.8h, v29.8h, #0x4
        sub             v8.8h, v28.8h, v29.8h                       // diff

        abs             v28.8h, v30.8h
        abs             v29.8h, v31.8h

        bdof_vx_vy_8x_padding_left_right v28, v9, v10, v28
        bdof_vx_vy_8x_padding_left_right v29, v9, v10, v29

        bdof_vx_vy_sign v30, v9, v10, v9
        bdof_vx_vy_sign v31, v10, v31, v31

        mul             v30.8h, v31.8h, v30.8h
        mul             v9.8h, v9.8h, v8.8h
        mul             v8.8h, v31.8h, v8.8h

        bdof_vx_vy_8x_padding_left_right v30, v31, v10, v30
        bdof_vx_vy_8x_padding_left_right v9, v31, v10, v31
        bdof_vx_vy_8x_padding_left_right v8, v9, v10, v8

        bdof_vx_vy_8x_add_line v28, v29, v30, v31, v8

        cmp             w12, w6
        b.ne            10f
        cbnz            x15, 10f

        // y == block_h && dy == 0, duplicate first line results
        bdof_vx_vy_8x_add_line v28, v29, v30, v31, v8
        add             x15, x15, #0x1
        b               9b
10:
        cmp             x15, #(BDOF_MIN_BLOCK_SIZE - 1)
        b.eq            11f
        cmp             x15, #(BDOF_MIN_BLOCK_SIZE)
        b.ne            12f
        b               1b
11:
        // y == BDOF_MIN_BLOCK_SIZE && dy == BDOF_MIN_BLOCK_SIZE - 1
        // duplicate the results and break
        cmp             x12, #(BDOF_MIN_BLOCK_SIZE)
        b.eq            13f
        bdof_vx_vy_8x_save_line v18, v19, v20, v21, v22
12:
        add             x15, x15, #1
        b               8b
13:
        // y == BDOF_MIN_BLOCK_SIZE && dy == BDOF_MIN_BLOCK_SIZE - 1
        // padding bottom then break
        bdof_vx_vy_8x_add_line v28, v29, v30, v31, v8
        b               2b
16:
        ldp             d9, d8, [sp, #0x10]
        ldp             d11, d10, [sp], #0x20
        ret
endfunc

/*
 * x0: const int16_t *_src0,
 * x1: const int16_t *_src1,
 * x2: const int16_t *gradient_h,
 * x3: const int16_t *gradient_v,
 * x4: int16_t vx[16],
 * x5: int16_t vy[16],
 * w6: int block_h
 */
function vvc_derive_bdof_vx_vy_16x_neon, export=0
        stp             d15, d14, [sp, #-0x40]!
        stp             d13, d12, [sp, #0x10]
        stp             d11, d10, [sp, #0x20]
        stp             d9, d8,   [sp, #0x30]

        movrel          x12, bdof_vx_vy_16x_tbl
        ldp             q0, q1, [x12]                       // table
        mov             w13, w6                             // y = block_h
        b               4f

1:
        // save line4
        bdof_vx_vy_16x_save_line v6, v7, v16, v17, v18
2:
        clz             v3.4s, v25.4s
        mvni            v5.4s, #0x1e
        add             v3.4s, v3.4s, v5.4s                 // -log2()
        shl             v4.4s, v28.4s, #0x2
        sshl            v3.4s, v4.4s, v3.4s

        movi            v28.4s, #0xf                        // clip to 15
        mvni            v29.4s, #0xe                        // clip to -15
        bdof_vx_vy_clip_mask v3, v28, v29, v25, v3
        sqxtn           v4.4h, v3.4s
        st1             {v4.d}[0], [x4], #(BDOF_MIN_BLOCK_SIZE * 2)

        subs            x13, x13, #(BDOF_MIN_BLOCK_SIZE)    // y -= BDOF_MIN_BLOCK_SIZE

        clz             v4.4s, v24.4s
        add             v4.4s, v4.4s, v5.4s                 // -log2()
        shl             v5.4s, v27.4s, #0x2
        mul             v3.4s, v3.4s, v26.4s
        sshr            v3.4s, v3.4s, #0x1
        sub             v3.4s, v5.4s, v3.4s
        sshl            v3.4s, v3.4s, v4.4s

        bdof_vx_vy_clip_mask v3, v28, v29, v24, v3
        sqxtn           v3.4h, v3.4s
        st1             {v3.d}[0], [x5], #(BDOF_MIN_BLOCK_SIZE * 2)
        b.eq            16f
4:
        mov             w14, #0x0                           // dy, inner loop

        movi            v25.2d, #0
        movi            v24.2d, #0
        movi            v26.2d, #0
        movi            v28.2d, #0
        movi            v27.2d, #0
        b               8f

5:
        // add line(-1) and line0 from previous results
        bdof_vx_vy_16x_add_line v19, v20, v21, v22, v23
        bdof_vx_vy_16x_add_line v6, v7, v16, v17, v18
        add             w14, w14, #0x1

 8:
        cmp             w13, w6
        b.hs            9f
        // y < block_h && dy == 0, reuse previous results
        cbz             w14, 5b
9:
        ld1             {v29.8h, v30.8h}, [x0]              // src0
        sshr            v31.8h, v29.8h, #0x4
        ld1             {v8.8h, v9.8h}, [x1]                // src1
        sshr            v10.8h, v8.8h, #0x4
        ldp             q13, q8, [x2], #32                  // (gh0 + gh1) >> 1
        sshr            v29.8h, v30.8h, #0x4
        sshr            v30.8h, v9.8h, #0x4
        ldp             q5, q3, [x3], #32                   // (gv0 + gv1) >> 1
        sub             v31.8h, v31.8h, v10.8h              // diff, left half
        sub             v4.8h, v29.8h, v30.8h               // diff, right half

        abs             v29.8h, v13.8h
        abs             v30.8h, v8.8h
        abs             v9.8h, v5.8h
        abs             v10.8h, v3.8h

        add             x0, x0, #(VVC_MAX_PB_SIZE * 2)
        add             x1, x1, #(VVC_MAX_PB_SIZE * 2)

        bdof_vx_vy_16x_padding_left_right v29, v30, v11, v12, v14, v29
        bdof_vx_vy_16x_padding_left_right v9, v10, v11, v12, v14, v30

        bdof_vx_vy_sign v13, v9, v10, v9
        bdof_vx_vy_sign v8, v10, v11, v10
        bdof_vx_vy_sign v5, v11, v5, v5
        bdof_vx_vy_sign v3, v11, v3, v3

        mul             v11.8h, v5.8h, v13.8h
        mul             v12.8h, v3.8h, v8.8h
        mul             v8.8h, v9.8h, v31.8h
        mul             v9.8h, v10.8h, v4.8h
        mul             v13.8h, v5.8h, v31.8h
        mul             v14.8h, v3.8h, v4.8h

        bdof_vx_vy_16x_padding_left_right v11, v12, v3, v4, v5, v31
        bdof_vx_vy_16x_padding_left_right v8, v9, v3, v4, v5, v8
        bdof_vx_vy_16x_padding_left_right v13, v14, v3, v4, v5, v9

        bdof_vx_vy_16x_add_line v29, v30, v31, v8, v9
        // check whether padding top
        cmp             w13, w6
        b.ne            10f
        cbnz            w14, 10f
        // y == block_h && dy == 0, padding top
        bdof_vx_vy_16x_add_line v29, v30, v31, v8, v9
        add             w14, w14, #0x1
        b               9b
10:
        cmp             w14, #(BDOF_MIN_BLOCK_SIZE - 1)
        b.eq            11f
        cmp             w14, #(BDOF_MIN_BLOCK_SIZE)
        b.ne            12f
        // save line4
        b               1b
 11:
        // y == BDOF_MIN_BLOCK_SIZE && dy == BDOF_MIN_BLOCK_SIZE - 1, padding bottom
        cmp             x13, #(BDOF_MIN_BLOCK_SIZE)
        b.eq            13f
        // save line3
        bdof_vx_vy_16x_save_line v19, v20, v21, v22, v23
12:
        add             w14, w14, #0x1                      // dy++
        b               8b
13:
        // padding bottom
        bdof_vx_vy_16x_add_line v29, v30, v31, v8, v9
        b               2b
16:
        // restore
        ldp             d9, d8, [sp, #0x30]
        ldp             d11, d10, [sp, #0x20]
        ldp             d13, d12, [sp, #0x10]
        ldp             d15, d14, [sp], #0x40
        ret
endfunc

function ff_vvc_apply_bdof_10_neon, export=1
        mov             w6, #10
        b               0f
endfunc

function ff_vvc_apply_bdof_12_neon, export=1
        mov             w6, #12
        b               0f
endfunc

// int16_t gradient_buf_h[2][BDOF_BLOCK_SIZE * BDOF_BLOCK_SIZE + 2]
// int16_t gradient_buf_v[2][BDOF_BLOCK_SIZE * BDOF_BLOCK_SIZE + 2]
// int16_t vx[BDOF_BLOCK_SIZE], vy[BDOF_BLOCK_SIZE];
#define APPLY_BDOF_STACK_SIZE   ((BDOF_BLOCK_SIZE * BDOF_BLOCK_SIZE + 2) * 8 + BDOF_BLOCK_SIZE * 4)
#define GRADIENT_H0_OFFSET      2
#define GRADIENT_H1_OFFSET      ((BDOF_BLOCK_SIZE * BDOF_BLOCK_SIZE + 2) * 2 + 2)
#define GRADIENT_V0_OFFSET      ((BDOF_BLOCK_SIZE * BDOF_BLOCK_SIZE + 2) * 4 + 2)
#define GRADIENT_V1_OFFSET      ((BDOF_BLOCK_SIZE * BDOF_BLOCK_SIZE + 2) * 6 + 2)
#define VX_OFFSET               ((BDOF_BLOCK_SIZE * BDOF_BLOCK_SIZE + 2) * 8)
#define VY_OFFSET               ((BDOF_BLOCK_SIZE * BDOF_BLOCK_SIZE + 2) * 8 + BDOF_BLOCK_SIZE * 2)
function ff_vvc_apply_bdof_8_neon, export=1
        mov             w6, #8
0:
        stp             x19, x20, [sp, #-0x40]!
        stp             x21, x22, [sp, #0x10]
        stp             x23, x24, [sp, #0x20]
        stp             x25, x30, [sp, #0x30]

        sub             sp, sp, #APPLY_BDOF_STACK_SIZE
        mov             w19, w6                         // bit_depth
        mov             x20, x0                         // dst
        mov             x21, x1                         // dst_stride
        mov             x22, x2                         // src0
        mov             x23, x3                         // src1
        mov             w24, w4                         // block_w
        mov             w25, w5                         // block_h

        // int16_t *gradient_h[2] = {&gradient_buf_h[0][1], &gradient_buf_h[1][1]};
        add             x0, sp, #GRADIENT_H0_OFFSET
        add             x1, sp, #GRADIENT_H1_OFFSET
        add             x2, sp, #GRADIENT_V0_OFFSET
        add             x3, sp, #GRADIENT_V1_OFFSET
        mov             x4, x22
        mov             x5, x23
        mov             w6, w24
        mov             w7, w25
        bl              vvc_bdof_grad_filter_8x_neon

        cmp             w24, #8
        mov             x0, x22                         // src0
        mov             x1, x23                         // src1
        add             x2, sp, #GRADIENT_H0_OFFSET     // gh0
        add             x3, sp, #GRADIENT_V0_OFFSET     // gv0
        add             x4, sp, #VX_OFFSET              // vx
        add             x5, sp, #VY_OFFSET              // vy
        mov             w6, w25                         // block_h

        b.gt            16f

        bl              vvc_derive_bdof_vx_vy_8x_neon
        cmp             w19, #10                        // check bitdepth
        mov             x0, x20                         // dst
        mov             x1, x21                         // dst_stride
        mov             x2, x22                         // src0
        mov             x3, x23                         // src1
        add             x4, sp, #GRADIENT_H1_OFFSET     // gh1
        add             x5, sp, #GRADIENT_V1_OFFSET     // gv1
        add             x6, sp, #VX_OFFSET
        add             x7, sp, #VY_OFFSET
        str             w25, [sp]
        b.eq            1f
        b.gt            2f
        // 8bit
0:
        bl              vvc_apply_bdof_block_8x_8_neon
        b               32f
1:
        // 10bit
        bl              vvc_apply_bdof_block_8x_10_neon
        b               32f
2:
        // 12bit
        bl              vvc_apply_bdof_block_8x_12_neon
        b               32f
16:
        bl              vvc_derive_bdof_vx_vy_16x_neon

        cmp             w19, #10                        // check bitdepth
        mov             x0, x20                         // dst
        mov             x1, x21                         // dst_stride
        mov             x2, x22                         // src0
        mov             x3, x23                         // src1
        add             x4, sp, #GRADIENT_H1_OFFSET     // gh1
        add             x5, sp, #GRADIENT_V1_OFFSET     // gv1
        add             x6, sp, #VX_OFFSET
        add             x7, sp, #VY_OFFSET
        str             w25, [sp]
        b.eq            17f
        b.gt            18f
        // 8bit
        bl              vvc_apply_bdof_block_16x_8_neon
        b               32f
17:
        // 10bit
        bl              vvc_apply_bdof_block_16x_10_neon
        b               32f
18:
        // 12bit
        bl              vvc_apply_bdof_block_16x_12_neon
32:
        add             sp, sp, #APPLY_BDOF_STACK_SIZE
        ldp             x25, x30, [sp, #0x30]
        ldp             x23, x24, [sp, #0x20]
        ldp             x21, x22, [sp, #0x10]
        ldp             x19, x20, [sp], #0x40
        ret
endfunc

#undef APPLY_BDOF_STACK_SIZE
#undef GRADIENT_H0_OFFSET
#undef GRADIENT_H1_OFFSET
#undef GRADIENT_V0_OFFSET
#undef GRADIENT_V1_OFFSET
#undef VX_OFFSET
#undef VY_OFFSET
