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
