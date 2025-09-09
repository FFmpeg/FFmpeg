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

.macro alf_luma_filter_pixel index, pix_size, addr1, addr2, offset1, offset2
    .if \pix_size == 1
        ldur            d3, [\addr1, #\offset1]
        ldur            d4, [\addr2, #\offset2]
        uxtl            v6.8h, v3.8b
        uxtl            v7.8h, v4.8b
    .else
        ldur            q6, [\addr1, #(2*\offset1)]
        ldur            q7, [\addr2, #(2*\offset2)]
    .endif
    .if \index < 8
        dup             v17.4h, v0.h[\index]        // clip
        dup             v18.4h, v16.h[\index]       // -clip
        dup             v19.4h, v1.h[\index]        // filter

        dup             v26.4h, v22.h[\index]       // clip
        dup             v27.4h, v23.h[\index]       // -clip
        dup             v28.4h, v24.h[\index]       // filter
    .else
        dup             v17.4h, v0.h[\index - 8]    // clip
        dup             v18.4h, v16.h[\index - 8]   // -clip
        dup             v19.4h, v1.h[\index - 8]    // filter

        dup             v26.4h, v22.h[\index - 8]   // clip
        dup             v27.4h, v23.h[\index - 8]   // -clip
        dup             v28.4h, v24.h[\index - 8]   // filter
    .endif
        ins             v17.d[1], v26.d[0]
        ins             v18.d[1], v27.d[0]
        ins             v19.d[1], v28.d[0]

        sub             v6.8h, v6.8h, v5.8h
        sub             v7.8h, v7.8h, v5.8h
        smin            v6.8h, v6.8h, v17.8h
        smin            v7.8h, v7.8h, v17.8h
        smax            v6.8h, v6.8h, v18.8h
        smax            v7.8h, v7.8h, v18.8h
        add             v6.8h, v6.8h, v7.8h
        smlal           v20.4s, v19.4h, v6.4h       // v20: sum
        smlal2          v21.4s, v19.8h, v6.8h       // v21: sum
.endm

/* x0: dst
 * x1: pp
 * x2: filter
 * x3: clip
 * w4: is_near_vb
 * w5: pix_max
 */
.macro alf_filter_luma_kernel, pix_size
        dst             .req x0
        pp              .req x1
        filter          .req x2
        clip            .req x3
        is_near_vb      .req w4
        pix_max         .req w5
    .if \pix_size > 1
        dup             v25.8h, pix_max             // pix_max
    .endif
        ldr             q0, [clip]                  // clip
        ldr             q1, [filter]                // filter
        ldur            q22, [clip, #24]            // clip
        ldur            q24, [filter, #24]          // filter

        ldr             x5, [pp]                    // x5: p0
        ldr             x6, [pp, #(5*8)]            // x6: p5
        ldr             x7, [pp, #(6*8)]            // x7: p6
        neg             v16.8h, v0.8h               // -clip
        neg             v23.8h, v22.8h              // -clip

    .if \pix_size == 1
        ldr             d2, [x5]                    // curr
    .else
        ldr             q5, [x5]                    // curr
    .endif
        movi            v20.4s, #64
        cbz             is_near_vb, 1f
        shl             v20.4s, v20.4s, #3
1:
    .if \pix_size == 1
        uxtl            v5.8h, v2.8b
    .endif
        mov             v21.16b, v20.16b
        ldr             x8, [pp, #(3*8)]            // p3
        ldr             x9, [pp, #(4*8)]            // p4
        alf_luma_filter_pixel 0, \pix_size, x6, x7, 0, 0

        ldr             x6, [pp, #(1*8)]            // p1
        ldr             x7, [pp, #(2*8)]            // p2
        alf_luma_filter_pixel 1, \pix_size, x8, x9, 1, -1
        alf_luma_filter_pixel 2, \pix_size, x8, x9, 0, 0
        alf_luma_filter_pixel 3, \pix_size, x8, x9, -1, 1

        alf_luma_filter_pixel 4, \pix_size, x6, x7, 2, -2
        alf_luma_filter_pixel 5, \pix_size, x6, x7, 1, -1
        alf_luma_filter_pixel 6, \pix_size, x6, x7, 0, 0
        alf_luma_filter_pixel 7, \pix_size, x6, x7, -1, 1

        ldr             d0, [clip, #16]             // clip
        ldr             d1, [filter, #16]           // filter
        neg             v16.4h, v0.4h               // -clip

        ldr             d22, [clip, #40]            // clip
        ldr             d24, [filter, #40]          // filter
        neg             v23.4h, v22.4h              // -clip
        alf_luma_filter_pixel 8, \pix_size, x6, x7, -2, 2
        alf_luma_filter_pixel 9, \pix_size, x5, x5, 3, -3
        alf_luma_filter_pixel 10, \pix_size, x5, x5, 2, -2
        alf_luma_filter_pixel 11, \pix_size, x5, x5, 1, -1

        cbz             is_near_vb, 2f
        sshr            v20.4s, v20.4s, #10
        sshr            v21.4s, v21.4s, #10
        b               3f
2:
        sshr            v20.4s, v20.4s, #7
        sshr            v21.4s, v21.4s, #7
3:
        uxtl            v22.4s, v5.4h
        uxtl2           v23.4s, v5.8h
        add             v20.4s, v20.4s, v22.4s
        add             v21.4s, v21.4s, v23.4s
        sqxtun          v20.4h, v20.4s
        sqxtun2         v20.8h, v21.4s
    .if \pix_size == 1
        sqxtun          v20.8b, v20.8h
        str             d20, [dst]
    .else
        umin            v20.8h, v20.8h, v25.8h
        str             q20, [dst]
    .endif
        ret

        .unreq          dst
        .unreq          pp
        .unreq          filter
        .unreq          clip
        .unreq          is_near_vb
        .unreq          pix_max
.endm

.macro alf_chroma_filter_pixel index, pix_size, addr1, addr2, offset1, offset2
    .if \pix_size == 1
        ldur            s3, [\addr1, #\offset1]
        ldur            s4, [\addr2, #\offset2]
        uxtl            v6.8h, v3.8b
        uxtl            v7.8h, v4.8b
    .else
        ldur            d6, [\addr1, #(2*\offset1)]
        ldur            d7, [\addr2, #(2*\offset2)]
    .endif
    .if \index < 8
        dup             v17.4h, v0.h[\index]        // v17: clip[0]
        dup             v18.4h, v16.h[\index]       // v18: -clip[0]
        dup             v19.4h, v1.h[\index]        // v19: filter[0]
    .else
        dup             v17.4h, v0.h[\index - 8]    // v17: clip[0]
        dup             v18.4h, v16.h[\index - 8]   // v18: -clip[0]
        dup             v19.4h, v1.h[\index - 8]    // v19: filter[0]
    .endif

        sub             v6.4h, v6.4h, v5.4h
        sub             v7.4h, v7.4h, v5.4h
        smin            v6.4h, v6.4h, v17.4h
        smin            v7.4h, v7.4h, v17.4h
        smax            v6.4h, v6.4h, v18.4h
        smax            v7.4h, v7.4h, v18.4h
        add             v6.4h, v6.4h, v7.4h
        smlal           v20.4s, v19.4h, v6.4h       // v20: sum
.endm

/* x0: dst
 * x1: pp
 * x2: filter
 * x3: clip
 * w4: is_near_vb
 * w5: pix_max
 */
.macro alf_filter_chroma_kernel, pix_size
        dst             .req x0
        pp              .req x1
        filter          .req x2
        clip            .req x3
        is_near_vb      .req w4
        pix_max         .req w5
    .if \pix_size > 1
        dup             v25.4h, pix_max             // pix_max
    .endif
        ldr             q0, [clip]                  // clip
        ldr             q1, [filter]                // filter
        ldr             x5, [pp]                    // p0
        ldr             x6, [pp, #(3*8)]            // p3
        ldr             x7, [pp, #(4*8)]            // p4
        neg             v16.8h, v0.8h               // -clip

    .if \pix_size == 1
        ldr             s2, [x5]                    // curr
    .else
        ldr             d5, [x5]                    // curr
    .endif
        movi            v20.4s, #64
        cbz             is_near_vb, 1f
        shl             v20.4s, v20.4s, #3
1:
    .if \pix_size == 1
        uxtl            v5.8h, v2.8b
    .endif
        ldr             x8, [pp, #(1*8)]            // p1
        ldr             x9, [pp, #(2*8)]            // p2
        alf_chroma_filter_pixel 0, \pix_size, x6, x7, 0, 0
        alf_chroma_filter_pixel 1, \pix_size, x8, x9, 1, -1
        alf_chroma_filter_pixel 2, \pix_size, x8, x9, 0, 0
        alf_chroma_filter_pixel 3, \pix_size, x8, x9, -1, 1
        alf_chroma_filter_pixel 4, \pix_size, x5, x5, 2, -2
        alf_chroma_filter_pixel 5, \pix_size, x5, x5, 1, -1

        uxtl            v22.4s, v5.4h
        cbz             is_near_vb, 2f
        sshr            v20.4s, v20.4s, #10
        b               3f
2:
        sshr            v20.4s, v20.4s, #7
3:
        add             v20.4s, v20.4s, v22.4s
        sqxtun          v20.4h, v20.4s
    .if \pix_size == 1
        sqxtun          v20.8b, v20.8h
        str             s20, [dst]
    .else
        umin            v20.4h, v20.4h, v25.4h
        str             d20, [dst]
    .endif
        ret

        .unreq          dst
        .unreq          pp
        .unreq          filter
        .unreq          clip
        .unreq          is_near_vb
        .unreq          pix_max
.endm

function ff_alf_filter_luma_kernel_8_neon, export=1
        alf_filter_luma_kernel 1
endfunc

function ff_alf_filter_luma_kernel_12_neon, export=1
        mov             w5, #4095
        b               1f
endfunc

function ff_alf_filter_luma_kernel_10_neon, export=1
        mov             w5, #1023
1:
        alf_filter_luma_kernel 2
endfunc

function ff_alf_filter_chroma_kernel_8_neon, export=1
        alf_filter_chroma_kernel 1
endfunc

function ff_alf_filter_chroma_kernel_12_neon, export=1
        mov             w5, #4095
        b               1f
endfunc

function ff_alf_filter_chroma_kernel_10_neon, export=1
        mov             w5, #1023
1:
        alf_filter_chroma_kernel 2
endfunc

#define ALF_BLOCK_SIZE          4
#define ALF_GRADIENT_STEP       2
#define ALF_GRADIENT_BORDER     2
#define ALF_NUM_DIR             4
#define ALF_GRAD_BORDER_X2      (ALF_GRADIENT_BORDER * 2)
#define ALF_STRIDE_MUL          (ALF_GRADIENT_BORDER + 1)
#define ALF_GRAD_X_VSTEP        (ALF_GRADIENT_STEP * 8)
#define ALF_GSTRIDE_MUL         (ALF_NUM_DIR / ALF_GRADIENT_STEP)

// Shift right: equal to division by 2 (see ALF_GRADIENT_STEP)
#define ALF_GSTRIDE_XG_BYTES    (2 * ALF_NUM_DIR / ALF_GRADIENT_STEP)

#define ALF_GSTRIDE_SUB_BYTES   (2 * ((ALF_BLOCK_SIZE + ALF_GRADIENT_BORDER * 2) / ALF_GRADIENT_STEP) * ALF_NUM_DIR)

#define ALF_CLASS_INC           (ALF_GRADIENT_BORDER / ALF_GRADIENT_STEP)
#define ALF_CLASS_END           ((ALF_BLOCK_SIZE + ALF_GRADIENT_BORDER * 2) / ALF_GRADIENT_STEP)

.macro ff_alf_classify_grad pix_size
        // class_idx     .req x0
        // transpose_idx .req x1
        // _src          .req x2
        // _src_stride   .req x3
        // width         .req w4
        // height        .req w5
        // vb_pos        .req w6
        // gradient_tmp  .req x7

        mov             w16, #ALF_STRIDE_MUL
        add             w5, w5, #ALF_GRAD_BORDER_X2 // h = height + ALF_GRAD_BORDER_X2
        mul             x16, x3, x16                // ALF_STRIDE_MUL * stride
        add             w4, w4, #ALF_GRAD_BORDER_X2 // w = width + ALF_GRAD_BORDER_X2
        sub             x15, x2, x16                // src -= (ALF_STRIDE_MUL * stride)
        mov             x17, x7
    .if \pix_size == 1
        sub             x15, x15, #ALF_GRADIENT_BORDER
    .else
        sub             x15, x15, #ALF_GRAD_BORDER_X2
    .endif
        mov             w8, #0                      // y loop: y = 0
1:
        add             x16, x8, #1
        mul             x16, x16, x3
        madd            x10, x8, x3, x15            // s0 = src + y * stride
        add             x14, x16, x3
        add             x11, x15, x16               // s1
        add             x16, x14, x3
        add             x12, x15, x14               // s2
        add             x13, x15, x16               // s3

        // if (y == vb_pos): s3 = s2
        cmp             w8, w6
        add             w16, w6, #ALF_GRADIENT_BORDER
        csel            x13, x12, x13, eq
        // if (y == vb_pos + 2): s0 = s1
        cmp             w8, w16
        csel            x10, x11, x10, eq

    .if \pix_size == 1
        sub             x10, x10, #1                // s0-1
        sub             x11, x11, #2
        sub             x12, x12, #2
    .else
        sub             x10, x10, #2                // s0-1
        sub             x11, x11, #4
        sub             x12, x12, #4
    .endif

        // x loop
        mov             w9, #0
        b               11f
2:
        // Store operation starts from the second cycle
        st2             {v4.8h, v5.8h}, [x17], #32
11:
    .if \pix_size == 1
        // Load 8 pixels: s0 & s1+2
        mov             x16, #1
        mov             x14, #7
        ld1             {v0.8b}, [x10], x16         // s0-1
        ld1             {v2.8b}, [x13], x16         // s3
        ld1             {v1.8b}, [x10], x14         // s0
        ld1             {v3.8b}, [x13], x14         // s3+1
        uxtl            v16.8h, v0.8b
        uxtl            v20.8h, v1.8b
        uxtl            v28.8h, v2.8b
        uxtl            v19.8h, v3.8b

        mov             x16, #2
        mov             x14, #4
        ld1             {v0.8b}, [x11], x16         // s1-2
        ld1             {v3.8b}, [x12], x16         // s2-2
        ld1             {v1.8b}, [x11], x16         // s1
        ld1             {v4.8b}, [x12], x16         // s2
        ld1             {v2.8b}, [x11], x14         // s1+2
        ld1             {v5.8b}, [x12], x14         // s2+2
        uxtl            v17.8h, v0.8b
        uxtl            v22.8h, v1.8b
        uxtl            v26.8h, v2.8b
        uxtl            v18.8h, v3.8b
        uxtl            v24.8h, v4.8b
        uxtl            v27.8h, v5.8b
    .else
        mov             x16, #2
        mov             x14, #14
        ld1             {v16.8h}, [x10], x16        // s0-1
        ld1             {v28.8h}, [x13], x16        // s3
        ld1             {v20.8h}, [x10], x14        // s0
        ld1             {v19.8h}, [x13], x14        // s3+1

        mov             x16, #4
        mov             x14, #8
        ld1             {v17.8h}, [x11], x16        // s1-2
        ld1             {v18.8h}, [x12], x16        // s2-2
        ld1             {v22.8h}, [x11], x16        // s1
        ld1             {v24.8h}, [x12], x16        // s2
        ld1             {v26.8h}, [x11], x14        // s1+2
        ld1             {v27.8h}, [x12], x14        // s2+2
    .endif

        // Grad: Vertical & D0 (interleaved)
        trn1            v21.8h, v20.8h, v16.8h      // first abs: operand 1
        rev32           v23.8h, v22.8h              // second abs: operand 1
        trn2            v29.8h, v28.8h, v19.8h      // second abs: operand 2
        trn1            v30.8h, v22.8h, v22.8h
        trn2            v31.8h, v24.8h, v24.8h
        add             v30.8h, v30.8h, v30.8h
        add             v31.8h, v31.8h, v31.8h
        sub             v0.8h, v30.8h, v21.8h
        sub             v1.8h, v31.8h, v23.8h
        sabd            v4.8h, v0.8h, v24.8h

        // Grad: Horizontal & D1 (interleaved)
        trn2            v21.8h, v17.8h, v20.8h      // first abs: operand 1
        saba            v4.8h, v1.8h, v29.8h
        trn2            v23.8h, v22.8h, v18.8h      // first abs: operand 2
        trn1            v25.8h, v24.8h, v26.8h      // second abs: operand 1
        trn1            v29.8h, v27.8h, v28.8h      // second abs: operand 2
        sub             v0.8h, v30.8h, v21.8h
        sub             v1.8h, v31.8h, v25.8h
        add             w9, w9, #8                  // x += 8
        sabd            v5.8h, v0.8h, v23.8h
        cmp             w9, w4
        saba            v5.8h, v1.8h, v29.8h
        b.lt            2b

        add             w8, w8, #ALF_GRADIENT_STEP  // y += ALF_GRADIENT_STEP
        // 8 pixels -> 4 cycles of generic
        // 4 pixels -> paddings => half needs to be saved
        st2             {v4.4h, v5.4h}, [x17], #16
        cmp             w8, w5
        b.lt            1b
        ret
.endm

.macro ff_alf_classify_sum
        ld1             {v0.8h, v1.8h, v2.8h}, [x2], x3
        uaddw           v16.4s, v16.4s, v0.4h
        uaddw           v17.4s, v17.4s, v1.4h
        uaddw           v18.4s, v18.4s, v2.4h
        uaddw2          v16.4s, v16.4s, v0.8h
        uaddw2          v17.4s, v17.4s, v1.8h
        uaddw2          v18.4s, v18.4s, v2.8h
.endm

function ff_alf_classify_sum_neon, export=1
        // sum0          .req x0
        // sum1          .req x1
        // grad          .req x2
        // gshift        .req w3
        // steps         .req w4
        lsl             w3, w3, #1
        cmp             w4, #4
        add             w3, w3, #32

        ld1             {v0.8h, v1.8h, v2.8h}, [x2], x3
        uxtl            v16.4s, v0.4h
        uxtl            v17.4s, v1.4h
        uxtl            v18.4s, v2.4h
        uaddw2          v16.4s, v16.4s, v0.8h
        uaddw2          v17.4s, v17.4s, v1.8h
        uaddw2          v18.4s, v18.4s, v2.8h
        ff_alf_classify_sum
        ff_alf_classify_sum

        blt             60f
        ff_alf_classify_sum
60:
        add             v16.4s, v16.4s, v17.4s
        add             v18.4s, v18.4s, v17.4s
        st1             {v16.4s}, [x0]
        st1             {v18.4s}, [x1]
        ret
endfunc

function ff_alf_classify_grad_8_neon, export=1
        ff_alf_classify_grad 1
endfunc

function ff_alf_classify_grad_10_neon, export=1
endfunc

function ff_alf_classify_grad_12_neon, export=1
        ff_alf_classify_grad 2
endfunc
