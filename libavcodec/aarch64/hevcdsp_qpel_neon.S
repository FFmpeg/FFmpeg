/* -*-arm64-*-
 * vim: syntax=arm64asm
 *
 * Copyright (c) 2022 J. Dekker <jdek@itanimul.li>
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
#define MAX_PB_SIZE 64

const qpel_filters, align=4
        .byte           0,  0,  0,  0,  0,  0, 0,  0
        .byte           -1, 4,-10, 58, 17, -5, 1,  0
        .byte           -1, 4,-11, 40, 40,-11, 4, -1
        .byte           0,  1, -5, 17, 58,-10, 4, -1
endconst

.macro load_filter m
        movrel          x15, qpel_filters
        add             x15, x15, \m, lsl #3
        ld1             {v0.8b}, [x15]
        sxtl            v0.8h, v0.8b
.endm

.macro put_hevc type
.ifc \type, qpel
        // void put_hevc_qpel_h(int16_t *dst,
        //                      uint8_t *_src, ptrdiff_t _srcstride,
        //                      int height, intptr_t mx, intptr_t my, int width)
        dst        .req x0
        dststride  .req x7
        src        .req x1
        srcstride  .req x2
        height     .req x3
        heightw    .req w3
        mx         .req x4
        width      .req w6
.endif
.ifc \type, qpel_uni
        // void put_hevc_qpel_uni_h(uint8_t *_dst,  ptrdiff_t _dststride,
        //                          uint8_t *_src, ptrdiff_t _srcstride,
        //                          int height, intptr_t mx, intptr_t my, int width)
        dst        .req x0
        dststride  .req x1
        src        .req x2
        srcstride  .req x3
        height     .req x4
        heightw    .req w4
        mx         .req x5
        width      .req w7
.endif
.ifc \type, qpel_bi
        // void put_hevc_qpel_bi_h(uint8_t *_dst, ptrdiff_t _dststride,
        //                         uint8_t *_src, ptrdiff_t _srcstride,
        //                         int16_t *src2, int height, intptr_t mx,
        //                         intptr_t my, int width)
        dst        .req x0
        dststride  .req x1
        src        .req x2
        srcstride  .req x3
        height     .req x5
        heightw    .req w5
        mx         .req x6
        width      .req w8
.endif

.ifc \type, qpel
function ff_hevc_put_hevc_h4_8_neon, export=0
        uxtl            v16.8h,  v16.8b
        uxtl            v17.8h,  v17.8b
        uxtl            v18.8h,  v18.8b
        uxtl            v19.8h,  v19.8b

        mul             v23.4h,  v16.4h, v0.h[0]
        mul             v24.4h,  v18.4h, v0.h[0]

.irpc i, 1234567
        ext             v20.16b, v16.16b, v17.16b, #(2*\i)
        ext             v21.16b, v18.16b, v19.16b, #(2*\i)
        mla             v23.4h,  v20.4h, v0.h[\i]
        mla             v24.4h,  v21.4h, v0.h[\i]
.endr
        ret
endfunc
.endif

function ff_hevc_put_hevc_\type\()_h4_8_neon, export=1
        load_filter     mx
.ifc \type, qpel_bi
        mov             x16, #(MAX_PB_SIZE << 2) // src2bstridel
        add             x15, x4, #(MAX_PB_SIZE << 1) // src2b
.endif
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #(MAX_PB_SIZE << 2)
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
.endif
        add             x10, dst, dststride // dstb
        add             x12, src, srcstride // srcb
0:      ld1             {v16.8b, v17.8b}, [src], x13
        ld1             {v18.8b, v19.8b}, [x12], x13
.ifc \type, qpel_bi
        ld1             {v25.8h}, [ x4], x16
        ld1             {v26.8h}, [x15], x16
.endif

        bl              ff_hevc_put_hevc_h4_8_neon
        subs            heightw, heightw, #2

.ifc \type, qpel
        st1             {v23.4h}, [dst], x14
        st1             {v24.4h}, [x10], x14
.else
.ifc \type, qpel_bi
        sqadd           v23.4h, v23.4h, v25.4h
        sqadd           v24.4h, v24.4h, v26.4h
        sqrshrun        v23.8b, v23.8h, #7
        sqrshrun        v24.8b, v24.8h, #7
.else
        sqrshrun        v23.8b, v23.8h, #6
        sqrshrun        v24.8b, v24.8h, #6
.endif
        st1             {v23.s}[0], [dst], x14
        st1             {v24.s}[0], [x10], x14
.endif
        b.gt            0b // double line
        ret             mx
endfunc

.ifc \type, qpel
function ff_hevc_put_hevc_h8_8_neon, export=0
        uxtl            v16.8h,  v16.8b
        uxtl            v17.8h,  v17.8b
        uxtl            v18.8h,  v18.8b
        uxtl            v19.8h,  v19.8b

        mul             v23.8h,  v16.8h, v0.h[0]
        mul             v24.8h,  v18.8h, v0.h[0]

.irpc i, 1234567
        ext             v20.16b, v16.16b, v17.16b, #(2*\i)
        ext             v21.16b, v18.16b, v19.16b, #(2*\i)
        mla             v23.8h,  v20.8h, v0.h[\i]
        mla             v24.8h,  v21.8h, v0.h[\i]
.endr
        ret
endfunc
.endif

function ff_hevc_put_hevc_\type\()_h6_8_neon, export=1
        load_filter     mx
.ifc \type, qpel_bi
        mov             x16, #(MAX_PB_SIZE << 2) // src2bstridel
        add             x15, x4, #(MAX_PB_SIZE << 1) // src2b
.endif
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #((MAX_PB_SIZE << 2) - 8)
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
        sub             x14, x14, #4
.endif
        add             x10, dst, dststride // dstb
        add             x12, src, srcstride // srcb
0:      ld1             {v16.8b, v17.8b}, [src], x13
        ld1             {v18.8b, v19.8b}, [x12], x13
.ifc \type, qpel_bi
        ld1             {v25.8h}, [ x4], x16
        ld1             {v26.8h}, [x15], x16
.endif

        bl              ff_hevc_put_hevc_h8_8_neon
        subs            heightw, heightw, #2

.ifc \type, qpel
        st1             {v23.4h},   [dst], #8
        st1             {v24.4h},   [x10], #8
        st1             {v23.s}[2], [dst], x14
        st1             {v24.s}[2], [x10], x14
.else
.ifc \type, qpel_bi
        sqadd           v23.8h, v23.8h, v25.8h
        sqadd           v24.8h, v24.8h, v26.8h
        sqrshrun        v23.8b, v23.8h, #7
        sqrshrun        v24.8b, v24.8h, #7
.else
        sqrshrun        v23.8b, v23.8h, #6
        sqrshrun        v24.8b, v24.8h, #6
.endif
        st1             {v23.s}[0], [dst], #4
        st1             {v24.s}[0], [x10], #4
        st1             {v23.h}[2], [dst], x14
        st1             {v24.h}[2], [x10], x14
.endif
        b.gt            0b // double line
        ret             mx
endfunc

function ff_hevc_put_hevc_\type\()_h8_8_neon, export=1
        load_filter     mx
.ifc \type, qpel_bi
        mov             x16, #(MAX_PB_SIZE << 2) // src2bstridel
        add             x15, x4, #(MAX_PB_SIZE << 1) // src2b
.endif
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #(MAX_PB_SIZE << 2)
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
.endif
        add             x10, dst, dststride // dstb
        add             x12, src, srcstride // srcb
0:      ld1             {v16.8b, v17.8b}, [src], x13
        ld1             {v18.8b, v19.8b}, [x12], x13
.ifc \type, qpel_bi
        ld1             {v25.8h}, [ x4], x16
        ld1             {v26.8h}, [x15], x16
.endif

        bl              ff_hevc_put_hevc_h8_8_neon
        subs            heightw, heightw, #2

.ifc \type, qpel
        st1             {v23.8h}, [dst], x14
        st1             {v24.8h}, [x10], x14
.else
.ifc \type, qpel_bi
        sqadd           v23.8h, v23.8h, v25.8h
        sqadd           v24.8h, v24.8h, v26.8h
        sqrshrun        v23.8b, v23.8h, #7
        sqrshrun        v24.8b, v24.8h, #7
.else
        sqrshrun        v23.8b, v23.8h, #6
        sqrshrun        v24.8b, v24.8h, #6
.endif
        st1             {v23.8b}, [dst], x14
        st1             {v24.8b}, [x10], x14
.endif
        b.gt            0b // double line
        ret             mx
endfunc

.ifc \type, qpel
function ff_hevc_put_hevc_h16_8_neon, export=0
        uxtl            v16.8h,  v16.8b
        uxtl            v17.8h,  v17.8b
        uxtl            v18.8h,  v18.8b

        uxtl            v19.8h,  v19.8b
        uxtl            v20.8h,  v20.8b
        uxtl            v21.8h,  v21.8b

        mul             v26.8h,  v16.8h, v0.h[0]
        mul             v27.8h,  v17.8h, v0.h[0]
        mul             v28.8h,  v19.8h, v0.h[0]
        mul             v29.8h,  v20.8h, v0.h[0]
.irpc i, 1234567
        ext             v22.16b, v16.16b, v17.16b, #(2*\i)
        ext             v23.16b, v17.16b, v18.16b, #(2*\i)

        ext             v24.16b, v19.16b, v20.16b, #(2*\i)
        ext             v25.16b, v20.16b, v21.16b, #(2*\i)

        mla             v26.8h,  v22.8h, v0.h[\i]
        mla             v27.8h,  v23.8h, v0.h[\i]

        mla             v28.8h,  v24.8h, v0.h[\i]
        mla             v29.8h,  v25.8h, v0.h[\i]
.endr
        subs            x9, x9, #2
        ret
endfunc
.endif

function ff_hevc_put_hevc_\type\()_h12_8_neon, export=1
        load_filter     mx
        sxtw            height, heightw
.ifc \type, qpel_bi
        ldrh            w8, [sp] // width
        mov             x16, #(MAX_PB_SIZE << 2) // src2bstridel
        lsl             x17, height, #7 // src2b reset (height * (MAX_PB_SIZE << 1))
        add             x15, x4, #(MAX_PB_SIZE << 1) // src2b
.endif
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #((MAX_PB_SIZE << 2) - 16)
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
        sub             x14, x14, #8
.endif
        add             x10, dst, dststride // dstb
        add             x12, src, srcstride // srcb
0:      mov             x9, height
1:      ld1             {v16.8b-v18.8b}, [src], x13
        ld1             {v19.8b-v21.8b}, [x12], x13

        bl              ff_hevc_put_hevc_h16_8_neon

.ifc \type, qpel
        st1             {v26.8h}, [dst], #16
        st1             {v28.8h}, [x10], #16
        st1             {v27.4h}, [dst], x14
        st1             {v29.4h}, [x10], x14
.else
.ifc \type, qpel_bi
        ld1             {v16.8h, v17.8h}, [ x4], x16
        ld1             {v18.8h, v19.8h}, [x15], x16
        sqadd           v26.8h, v26.8h, v16.8h
        sqadd           v27.8h, v27.8h, v17.8h
        sqadd           v28.8h, v28.8h, v18.8h
        sqadd           v29.8h, v29.8h, v19.8h
        sqrshrun        v26.8b, v26.8h, #7
        sqrshrun        v27.8b, v27.8h, #7
        sqrshrun        v28.8b, v28.8h, #7
        sqrshrun        v29.8b, v29.8h, #7
.else
        sqrshrun        v26.8b, v26.8h, #6
        sqrshrun        v27.8b, v27.8h, #6
        sqrshrun        v28.8b, v28.8h, #6
        sqrshrun        v29.8b, v29.8h, #6
.endif
        st1             {v26.8b},   [dst], #8
        st1             {v28.8b},   [x10], #8
        st1             {v27.s}[0], [dst], x14
        st1             {v29.s}[0], [x10], x14
.endif
        b.gt            1b // double line
        subs            width, width, #12
        // reset src
        msub            src, srcstride, height, src
        msub            x12, srcstride, height, x12
        // reset dst
        msub            dst, dststride, height, dst
        msub            x10, dststride, height, x10
.ifc \type, qpel_bi
        // reset xsrc
        sub             x4,  x4,  x17
        sub             x15, x15, x17
        add             x4,  x4,  #24
        add             x15, x15, #24
.endif
        add             src, src, #12
        add             x12, x12, #12
.ifc \type, qpel
        add             dst, dst, #24
        add             x10, x10, #24
.else
        add             dst, dst, #12
        add             x10, x10, #12
.endif
        b.gt            0b
        ret             mx
endfunc

function ff_hevc_put_hevc_\type\()_h16_8_neon, export=1
        load_filter     mx
        sxtw            height, heightw
        mov             mx, x30
.ifc \type, qpel_bi
        ldrh            w8, [sp] // width
        mov             x16, #(MAX_PB_SIZE << 2) // src2bstridel
        lsl             x17, x5, #7 // src2b reset
        add             x15, x4, #(MAX_PB_SIZE << 1) // src2b
.endif
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #((MAX_PB_SIZE << 2) - 16)
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
        sub             x14, x14, #8
.endif
        add             x10, dst, dststride // dstb
        add             x12, src, srcstride // srcb
0:      mov             x9, height
1:      ld1             {v16.8b-v18.8b}, [src], x13
        ld1             {v19.8b-v21.8b}, [x12], x13

        bl              ff_hevc_put_hevc_h16_8_neon

.ifc \type, qpel
        st1             {v26.8h}, [dst], #16
        st1             {v28.8h}, [x10], #16
        st1             {v27.8h}, [dst], x14
        st1             {v29.8h}, [x10], x14
.else
.ifc \type, qpel_bi
        ld1             {v16.8h, v17.8h}, [ x4], x16
        ld1             {v18.8h, v19.8h}, [x15], x16
        sqadd           v26.8h, v26.8h, v16.8h
        sqadd           v27.8h, v27.8h, v17.8h
        sqadd           v28.8h, v28.8h, v18.8h
        sqadd           v29.8h, v29.8h, v19.8h
        sqrshrun        v26.8b, v26.8h, #7
        sqrshrun        v27.8b, v27.8h, #7
        sqrshrun        v28.8b, v28.8h, #7
        sqrshrun        v29.8b, v29.8h, #7
.else
        sqrshrun        v26.8b, v26.8h, #6
        sqrshrun        v27.8b, v27.8h, #6
        sqrshrun        v28.8b, v28.8h, #6
        sqrshrun        v29.8b, v29.8h, #6
.endif
        st1             {v26.8b}, [dst], #8
        st1             {v28.8b}, [x10], #8
        st1             {v27.8b}, [dst], x14
        st1             {v29.8b}, [x10], x14
.endif
        b.gt            1b // double line
        subs            width, width, #16
        // reset src
        msub            src, srcstride, height, src
        msub            x12, srcstride, height, x12
        // reset dst
        msub            dst, dststride, height, dst
        msub            x10, dststride, height, x10
.ifc \type, qpel_bi
        // reset xsrc
        sub             x4,  x4,  x17
        sub             x15, x15, x17
        add             x4,  x4,  #32
        add             x15, x15, #32
.endif
        add             src, src, #16
        add             x12, x12, #16
.ifc \type, qpel
        add             dst, dst, #32
        add             x10, x10, #32
.else
        add             dst, dst, #16
        add             x10, x10, #16
.endif
        b.gt            0b
        ret             mx
endfunc

.unreq height
.unreq heightw
.unreq width
.unreq src
.unreq dst
.unreq srcstride
.unreq dststride
.unreq mx
.endm

put_hevc qpel
put_hevc qpel_uni
put_hevc qpel_bi
