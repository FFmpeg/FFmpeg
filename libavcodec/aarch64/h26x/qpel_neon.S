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
#define HEVC_MAX_PB_SIZE 64
#define VVC_MAX_PB_SIZE 128

const qpel_filters, align=4
        .byte           0,  0,  0,  0,  0,  0, 0,  0
        .byte           -1, 4,-10, 58, 17, -5, 1,  0
        .byte           -1, 4,-11, 40, 40,-11, 4, -1
        .byte           0,  1, -5, 17, 58,-10, 4, -1
endconst

const qpel_filters_abs, align=4
        .byte           0,  0,  0,  0,  0,  0, 0,  0
        .byte           1,  4, 10, 58, 17,  5, 1,  0
        .byte           1,  4, 11, 40, 40, 11, 4,  1
        .byte           0,  1,  5, 17, 58, 10, 4,  1
endconst

.macro load_filter m
        movrel          x15, qpel_filters
        add             x15, x15, \m, lsl #3
        ld1             {v0.8b}, [x15]
        sxtl            v0.8h, v0.8b
.endm

.macro vvc_load_filter m
        ld1             {v0.8b}, [\m]
        sxtl            v0.8h, v0.8b
.endm

.macro load_qpel_filterb freg, xreg
        movrel          \xreg, qpel_filters_abs
        add             \xreg, \xreg, \freg, lsl #3
        ld4r            {v0.16b, v1.16b, v2.16b, v3.16b}, [\xreg], #4
        ld4r            {v4.16b, v5.16b, v6.16b, v7.16b}, [\xreg]
.endm

.macro calc_qpelb dst, src0, src1, src2, src3, src4, src5, src6, src7
        umull           \dst\().8h, \src1\().8b, v1.8b
        umlsl           \dst\().8h, \src0\().8b, v0.8b
        umlsl           \dst\().8h, \src2\().8b, v2.8b
        umlal           \dst\().8h, \src3\().8b, v3.8b
        umlal           \dst\().8h, \src4\().8b, v4.8b
        umlsl           \dst\().8h, \src5\().8b, v5.8b
        umlal           \dst\().8h, \src6\().8b, v6.8b
        umlsl           \dst\().8h, \src7\().8b, v7.8b
.endm

.macro calc_qpelb2 dst, src0, src1, src2, src3, src4, src5, src6, src7
        umull2          \dst\().8h, \src1\().16b, v1.16b
        umlsl2          \dst\().8h, \src0\().16b, v0.16b
        umlsl2          \dst\().8h, \src2\().16b, v2.16b
        umlal2          \dst\().8h, \src3\().16b, v3.16b
        umlal2          \dst\().8h, \src4\().16b, v4.16b
        umlsl2          \dst\().8h, \src5\().16b, v5.16b
        umlal2          \dst\().8h, \src6\().16b, v6.16b
        umlsl2          \dst\().8h, \src7\().16b, v7.16b
.endm

.macro load_qpel_filterh freg, xreg
        movrel          \xreg, qpel_filters
        add             \xreg, \xreg, \freg, lsl #3
        ld1             {v0.8b}, [\xreg]
        sxtl            v0.8h, v0.8b
.endm

.macro vvc_load_qpel_filterh freg
        ld1             {v0.8b}, [\freg]
        sxtl            v0.8h, v0.8b
.endm

.macro calc_qpelh dst, src0, src1, src2, src3, src4, src5, src6, src7, op, shift=6
        smull           \dst\().4s, \src0\().4h, v0.h[0]
        smlal           \dst\().4s, \src1\().4h, v0.h[1]
        smlal           \dst\().4s, \src2\().4h, v0.h[2]
        smlal           \dst\().4s, \src3\().4h, v0.h[3]
        smlal           \dst\().4s, \src4\().4h, v0.h[4]
        smlal           \dst\().4s, \src5\().4h, v0.h[5]
        smlal           \dst\().4s, \src6\().4h, v0.h[6]
        smlal           \dst\().4s, \src7\().4h, v0.h[7]
.ifc \op, sqxtn
        sqxtn           \dst\().4h, \dst\().4s
.else
.ifc \op, sshr
        sshr            \dst\().4s, \dst\().4s, \shift
.else
        \op             \dst\().4h, \dst\().4s, \shift
.endif
.endif
.endm

.macro calc_qpelh2 dst, dstt, src0, src1, src2, src3, src4, src5, src6, src7, op, shift=6
        smull2          \dstt\().4s, \src0\().8h, v0.h[0]
        smlal2          \dstt\().4s, \src1\().8h, v0.h[1]
        smlal2          \dstt\().4s, \src2\().8h, v0.h[2]
        smlal2          \dstt\().4s, \src3\().8h, v0.h[3]
        smlal2          \dstt\().4s, \src4\().8h, v0.h[4]
        smlal2          \dstt\().4s, \src5\().8h, v0.h[5]
        smlal2          \dstt\().4s, \src6\().8h, v0.h[6]
        smlal2          \dstt\().4s, \src7\().8h, v0.h[7]
.ifc \op, sqxtn2
        sqxtn2          \dst\().8h, \dstt\().4s
.else
.ifc \op, sshr
        sshr            \dst\().4s, \dstt\().4s, \shift
.else
        \op             \dst\().8h, \dstt\().4s, \shift
.endif
.endif
.endm

.macro calc_all
        calc            v23, v16, v17, v18, v19, v20, v21, v22, v23
        b.eq            2f
        calc            v16, v17, v18, v19, v20, v21, v22, v23, v16
        b.eq            2f
        calc            v17, v18, v19, v20, v21, v22, v23, v16, v17
        b.eq            2f
        calc            v18, v19, v20, v21, v22, v23, v16, v17, v18
        b.eq            2f
        calc            v19, v20, v21, v22, v23, v16, v17, v18, v19
        b.eq            2f
        calc            v20, v21, v22, v23, v16, v17, v18, v19, v20
        b.eq            2f
        calc            v21, v22, v23, v16, v17, v18, v19, v20, v21
        b.eq            2f
        calc            v22, v23, v16, v17, v18, v19, v20, v21, v22
        b.hi            1b
.endm

.macro calc_all2
        calc            v30, v31, v16, v18, v20, v22, v24, v26, v28, v30, v17, v19, v21, v23, v25, v27, v29, v31
        b.eq            2f
        calc            v16, v17, v18, v20, v22, v24, v26, v28, v30, v16, v19, v21, v23, v25, v27, v29, v31, v17
        b.eq            2f
        calc            v18, v19, v20, v22, v24, v26, v28, v30, v16, v18, v21, v23, v25, v27, v29, v31, v17, v19
        b.eq            2f
        calc            v20, v21, v22, v24, v26, v28, v30, v16, v18, v20, v23, v25, v27, v29, v31, v17, v19, v21
        b.eq            2f
        calc            v22, v23, v24, v26, v28, v30, v16, v18, v20, v22, v25, v27, v29, v31, v17, v19, v21, v23
        b.eq            2f
        calc            v24, v25, v26, v28, v30, v16, v18, v20, v22, v24, v27, v29, v31, v17, v19, v21, v23, v25
        b.eq            2f
        calc            v26, v27, v28, v30, v16, v18, v20, v22, v24, v26, v29, v31, v17, v19, v21, v23, v25, v27
        b.eq            2f
        calc            v28, v29, v30, v16, v18, v20, v22, v24, v26, v28, v31, v17, v19, v21, v23, v25, v27, v29
        b.hi            1b
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

.ifnc \type, qpel_bi
function ff_vvc_put_\type\()_h4_8_neon, export=1
        vvc_load_filter mx
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(VVC_MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #(VVC_MAX_PB_SIZE << 2)
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
.endif
        b               1f
endfunc
.endif // !qpel_bi

function ff_hevc_put_hevc_\type\()_h4_8_neon, export=1
        load_filter     mx
.ifc \type, qpel_bi
        mov             x16, #(HEVC_MAX_PB_SIZE << 2) // src2bstridel
        add             x15, x4, #(HEVC_MAX_PB_SIZE << 1) // src2b
.endif
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(HEVC_MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #(HEVC_MAX_PB_SIZE << 2)
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
.endif
1:
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
        mov             x16, #(HEVC_MAX_PB_SIZE << 2) // src2bstridel
        add             x15, x4, #(HEVC_MAX_PB_SIZE << 1) // src2b
.endif
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(HEVC_MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #((HEVC_MAX_PB_SIZE << 2) - 8)
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

.ifnc \type, qpel_bi
function ff_vvc_put_\type\()_h8_8_neon, export=1
        vvc_load_filter mx
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(VVC_MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #(VVC_MAX_PB_SIZE << 2)
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
.endif
        b               1f
endfunc
.endif // !qpel_bi

function ff_hevc_put_hevc_\type\()_h8_8_neon, export=1
        load_filter     mx
.ifc \type, qpel_bi
        mov             x16, #(HEVC_MAX_PB_SIZE << 2) // src2bstridel
        add             x15, x4, #(HEVC_MAX_PB_SIZE << 1) // src2b
.endif
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(HEVC_MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #(HEVC_MAX_PB_SIZE << 2)
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
.endif
1:
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
        uxtl            v17.8h,  v17.8b
        uxtl            v18.8h,  v18.8b

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
        ret
endfunc
.endif

function ff_hevc_put_hevc_\type\()_h12_8_neon, export=1
        load_filter     mx
        sxtw            height, heightw
.ifc \type, qpel_bi
        ldrh            w8, [sp] // width
        mov             x16, #(HEVC_MAX_PB_SIZE << 2) // src2bstridel
        lsl             x17, height, #7 // src2b reset (height * (HEVC_MAX_PB_SIZE << 1))
        add             x15, x4, #(HEVC_MAX_PB_SIZE << 1) // src2b
.endif
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(HEVC_MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #((HEVC_MAX_PB_SIZE << 2) - 16)
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

        uxtl            v16.8h,  v16.8b
        uxtl            v19.8h,  v19.8b
        bl              ff_hevc_put_hevc_h16_8_neon
        subs            x9, x9, #2

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

.ifnc \type, qpel_bi
function ff_vvc_put_\type\()_h16_8_neon, export=1
        vvc_load_filter mx
        sxtw            height, heightw
        mov             mx, x30
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(VVC_MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #(VVC_MAX_PB_SIZE << 2)
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
.endif
        b               0f
endfunc
.endif // !qpel_bi

function ff_hevc_put_hevc_\type\()_h16_8_neon, export=1
        load_filter     mx
        sxtw            height, heightw
        mov             mx, x30
.ifc \type, qpel_bi
        ldrh            w8, [sp] // width
        mov             x16, #(HEVC_MAX_PB_SIZE << 2) // src2bstridel
        add             x15, x4, #(HEVC_MAX_PB_SIZE << 1) // src2b
.endif
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(HEVC_MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #(HEVC_MAX_PB_SIZE << 2)
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
.endif
0:
        add             x10, dst, dststride // dstb
        add             x12, src, srcstride // srcb

1:      ld1             {v16.8b-v18.8b}, [src], x13
        ld1             {v19.8b-v21.8b}, [x12], x13

        uxtl            v16.8h,  v16.8b
        uxtl            v19.8h,  v19.8b
        bl              ff_hevc_put_hevc_h16_8_neon
        subs            height, height, #2

.ifc \type, qpel
        st1             {v26.8h, v27.8h}, [dst], x14
        st1             {v28.8h, v29.8h}, [x10], x14
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
        st1             {v26.8b, v27.8b}, [dst], x14
        st1             {v28.8b, v29.8b}, [x10], x14
.endif
        b.gt            1b // double line
        ret             mx
endfunc

.ifnc \type, qpel_bi
function ff_vvc_put_\type\()_h32_8_neon, export=1
        vvc_load_filter mx
        sxtw            height, heightw
        mov             mx, x30
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(VVC_MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #(VVC_MAX_PB_SIZE << 2)
        sub             x14, x14, width, uxtw #1
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
        sub             x14, x14, width, uxtw
.endif
        b               1f
endfunc
.endif // !qpel_bi

function ff_hevc_put_hevc_\type\()_h32_8_neon, export=1
        load_filter     mx
        sxtw            height, heightw
        mov             mx, x30
.ifc \type, qpel_bi
        ldrh            w8, [sp] // width
        mov             x16, #(HEVC_MAX_PB_SIZE << 2) // src2bstridel
        lsl             x17, x5, #7 // src2b reset
        add             x15, x4, #(HEVC_MAX_PB_SIZE << 1) // src2b
        sub             x16, x16, width, uxtw #1
.endif
        sub             src, src, #3
        mov             mx, x30
.ifc \type, qpel
        mov             dststride, #(HEVC_MAX_PB_SIZE << 1)
        lsl             x13, srcstride, #1 // srcstridel
        mov             x14, #(HEVC_MAX_PB_SIZE << 2)
        sub             x14, x14, width, uxtw #1
.else
        lsl             x14, dststride, #1 // dststridel
        lsl             x13, srcstride, #1 // srcstridel
        sub             x14, x14, width, uxtw
.endif
1:
        sub             x13, x13, width, uxtw
        sub             x13, x13, #8
        add             x10, dst, dststride // dstb
        add             x12, src, srcstride // srcb
0:      mov             w9, width
        ld1             {v16.8b}, [src], #8
        ld1             {v19.8b}, [x12], #8
        uxtl            v16.8h, v16.8b
        uxtl            v19.8h, v19.8b
1:
        ld1             {v17.8b-v18.8b}, [src], #16
        ld1             {v20.8b-v21.8b}, [x12], #16

        bl              ff_hevc_put_hevc_h16_8_neon
        subs            w9, w9, #16

        mov             v16.16b, v18.16b
        mov             v19.16b, v21.16b
.ifc \type, qpel
        st1             {v26.8h, v27.8h}, [dst], #32
        st1             {v28.8h, v29.8h}, [x10], #32
.else
.ifc \type, qpel_bi
        ld1             {v20.8h, v21.8h}, [ x4], #32
        ld1             {v22.8h, v23.8h}, [x15], #32
        sqadd           v26.8h, v26.8h, v20.8h
        sqadd           v27.8h, v27.8h, v21.8h
        sqadd           v28.8h, v28.8h, v22.8h
        sqadd           v29.8h, v29.8h, v23.8h
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
        st1             {v26.8b, v27.8b}, [dst], #16
        st1             {v28.8b, v29.8b}, [x10], #16
.endif
        b.gt            1b // double line
        subs            height, height, #2
        add             src, src, x13
        add             x12, x12, x13
        add             dst, dst, x14
        add             x10, x10, x14
.ifc \type, qpel_bi
        add             x4,  x4,  x16
        add             x15, x15, x16
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

function ff_hevc_put_hevc_qpel_v4_8_neon, export=1
        load_qpel_filterb x5, x4
        sub             x1, x1, x2, lsl #1
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        sub             x1, x1, x2
        ldr             s16, [x1]
        ldr             s17, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             s18, [x1]
        ldr             s19, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             s20, [x1]
        ldr             s21, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             s22, [x1]
        add             x1, x1, x2
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().s}[0], [x1], x2
        movi            v24.8h, #0
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        st1             {v24.4h}, [x0], x9
        subs            w3, w3, #1
.endm
1:      calc_all
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_qpel_v6_8_neon, export=1
        load_qpel_filterb x5, x4
        sub             x1, x1, x2, lsl #1
        mov             x9, #(HEVC_MAX_PB_SIZE * 2 - 8)
        sub             x1, x1, x2
        ldr             d16, [x1]
        ldr             d17, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             d18, [x1]
        ldr             d19, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             d20, [x1]
        ldr             d21, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             d22, [x1]
        add             x1, x1, x2
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8b}, [x1], x2
        movi            v24.8h, #0
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        st1             {v24.4h}, [x0], #8
        st1             {v24.s}[2], [x0], x9
        subs            w3, w3, #1
.endm
1:      calc_all
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_qpel_v8_8_neon, export=1
        load_qpel_filterb x5, x4
        sub             x1, x1, x2, lsl #1
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        sub             x1, x1, x2
        ldr             d16, [x1]
        ldr             d17, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             d18, [x1]
        ldr             d19, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             d20, [x1]
        ldr             d21, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             d22, [x1]
        add             x1, x1, x2
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8b}, [x1], x2
        movi            v24.8h, #0
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        st1             {v24.8h}, [x0], x9
        subs            w3, w3, #1
.endm
1:      calc_all
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_qpel_v12_8_neon, export=1
        load_qpel_filterb x5, x4
        sub             x1, x1, x2, lsl #1
        mov             x9, #(HEVC_MAX_PB_SIZE * 2 - 16)
        sub             x1, x1, x2
        ldr             q16, [x1]
        ldr             q17, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             q18, [x1]
        ldr             q19, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             q20, [x1]
        ldr             q21, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             q22, [x1]
        add             x1, x1, x2
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().16b}, [x1], x2
        movi            v24.8h, #0
        movi            v25.8h, #0
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        calc_qpelb2     v25, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        st1             {v24.8h}, [x0], #16
        subs            w3, w3, #1
        st1             {v25.4h}, [x0], x9
.endm
1:      calc_all
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_qpel_v16_8_neon, export=1
        load_qpel_filterb x5, x4
        sub             x1, x1, x2, lsl #1
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        sub             x1, x1, x2
        ldr             q16, [x1]
        ldr             q17, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             q18, [x1]
        ldr             q19, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             q20, [x1]
        ldr             q21, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             q22, [x1]
        add             x1, x1, x2
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().16b}, [x1], x2
        movi            v24.8h, #0
        movi            v25.8h, #0
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        calc_qpelb2     v25, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        subs            w3, w3, #1
        st1             {v24.8h, v25.8h}, [x0], x9
.endm
1:      calc_all
.purgem calc
2:      ret
endfunc

// todo: reads #32 bytes
function ff_hevc_put_hevc_qpel_v24_8_neon, export=1
        sub             sp, sp, #32
        st1             {v8.8b, v9.8b, v10.8b}, [sp]
        load_qpel_filterb x5, x4
        sub             x1, x1, x2, lsl #1
        sub             x1, x1, x2
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.16b, v17.16b}, [x1], x2
        ld1             {v18.16b, v19.16b}, [x1], x2
        ld1             {v20.16b, v21.16b}, [x1], x2
        ld1             {v22.16b, v23.16b}, [x1], x2
        ld1             {v24.16b, v25.16b}, [x1], x2
        ld1             {v26.16b, v27.16b}, [x1], x2
        ld1             {v28.16b, v29.16b}, [x1], x2
.macro calc tmp0, tmp1, src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\tmp0\().16b, \tmp1\().16b}, [x1], x2
        movi            v8.8h, #0
        movi            v9.8h, #0
        movi            v10.8h, #0
        calc_qpelb      v8,  \src0, \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7
        calc_qpelb2     v9,  \src0, \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7
        calc_qpelb      v10, \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15
        subs            w3, w3, #1
        st1             {v8.8h, v9.8h, v10.8h}, [x0], x9
.endm
1:      calc_all2
.purgem calc
2:      ld1             {v8.8b, v9.8b, v10.8b}, [sp]
        add             sp, sp, #32
        ret
endfunc

function ff_hevc_put_hevc_qpel_v32_8_neon, export=1
        sub             sp, sp, #32
        st1             {v8.8b-v11.8b}, [sp]
        load_qpel_filterb x5, x4
        sub             x1, x1, x2, lsl #1
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        sub             x1, x1, x2
        ld1             {v16.16b, v17.16b}, [x1], x2
        ld1             {v18.16b, v19.16b}, [x1], x2
        ld1             {v20.16b, v21.16b}, [x1], x2
        ld1             {v22.16b, v23.16b}, [x1], x2
        ld1             {v24.16b, v25.16b}, [x1], x2
        ld1             {v26.16b, v27.16b}, [x1], x2
        ld1             {v28.16b, v29.16b}, [x1], x2
.macro calc tmp0, tmp1, src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\tmp0\().16b, \tmp1\().16b}, [x1], x2
        movi            v8.8h, #0
        movi            v9.8h, #0
        movi            v10.8h, #0
        movi            v11.8h, #0
        calc_qpelb      v8,  \src0, \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7
        calc_qpelb2     v9,  \src0, \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7
        calc_qpelb      v10, \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15
        calc_qpelb2     v11, \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15
        subs            w3, w3, #1
        st1             {v8.8h-v11.8h}, [x0], x9
.endm
1:      calc_all2
.purgem calc
2:      ld1             {v8.8b-v11.8b}, [sp], #32
        ret
endfunc

function ff_hevc_put_hevc_qpel_v48_8_neon, export=1
        stp             x2, x3, [sp, #-48]!
        stp             x0, x1, [sp, #16]
        stp             x5, x30, [sp, #32]
        bl              X(ff_hevc_put_hevc_qpel_v24_8_neon)
        ldr             x5, [sp, #32]
        ldp             x0, x1, [sp, #16]
        ldp             x2, x3, [sp], #32
        add             x0, x0, #48
        add             x1, x1, #24
        bl              X(ff_hevc_put_hevc_qpel_v24_8_neon)
        ldr             x30, [sp, #8]
        add             sp, sp, #16
        ret
endfunc

function ff_hevc_put_hevc_qpel_v64_8_neon, export=1
        sub             sp, sp, #32
        st1             {v8.8b-v11.8b}, [sp]
        load_qpel_filterb x5, x4
        sub             x1, x1, x2, lsl #1
        sub             x1, x1, x2
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
0:      mov             x8, x1          // src
        ld1             {v16.16b, v17.16b}, [x8], x2
        mov             w11, w3         // height
        ld1             {v18.16b, v19.16b}, [x8], x2
        mov             x10, x0         // dst
        ld1             {v20.16b, v21.16b}, [x8], x2
        ld1             {v22.16b, v23.16b}, [x8], x2
        ld1             {v24.16b, v25.16b}, [x8], x2
        ld1             {v26.16b, v27.16b}, [x8], x2
        ld1             {v28.16b, v29.16b}, [x8], x2
.macro calc tmp0, tmp1, src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\tmp0\().16b, \tmp1\().16b}, [x8], x2
        movi            v8.8h, #0
        movi            v9.8h, #0
        movi            v10.8h, #0
        movi            v11.8h, #0
        calc_qpelb      v8,  \src0, \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7
        calc_qpelb2     v9,  \src0, \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7
        calc_qpelb      v10, \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15
        calc_qpelb2     v11, \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15
        subs            x11, x11, #1
        st1             {v8.8h-v11.8h}, [x10], x9
.endm
1:      calc_all2
.purgem calc
2:      add             x0, x0, #64
        add             x1, x1, #32
        subs            w6, w6, #32
        b.hi            0b
        ld1             {v8.8b-v11.8b}, [sp], #32
        ret
endfunc

/* ff_hevc_put_hevc_qpel_vx require filter parameters be
 * [-, +, -, +, +, -, +, -],
 * vvc doesn't meet the requirement.
 */
function ff_vvc_put_qpel_v4_8_neon, export=1
        vvc_load_qpel_filterh x5
        sub             x1, x1, x2, lsl #1
        mov             x9, #(VVC_MAX_PB_SIZE * 2)
        sub             x1, x1, x2
        ldr             s16, [x1]
        ldr             s17, [x1, x2]
        add             x1, x1, x2, lsl #1
        ldr             s18, [x1]
        ldr             s19, [x1, x2]
        uxtl            v16.8h, v16.8b
        uxtl            v17.8h, v17.8b
        add             x1, x1, x2, lsl #1
        ldr             s20, [x1]
        ldr             s21, [x1, x2]
        uxtl            v18.8h, v18.8b
        uxtl            v19.8h, v19.8b
        add             x1, x1, x2, lsl #1
        ldr             s22, [x1]
        add             x1, x1, x2
        uxtl            v20.8h, v20.8b
        uxtl            v21.8h, v21.8b
        uxtl            v22.8h, v22.8b
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().s}[0], [x1], x2
        uxtl            \tmp\().8h, \tmp\().8b
        calc_qpelh      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqxtn
        subs            w3, w3, #1
        st1             {v24.4h}, [x0], x9
.endm
1:
        calc_all
.purgem calc
2:
        ret
endfunc

function ff_vvc_put_qpel_v8_8_neon, export=1
        vvc_load_qpel_filterh x5
        sub             x1, x1, x2, lsl #1
        sub             x1, x1, x2
        mov             x9, #(VVC_MAX_PB_SIZE * 2)
0:
        mov             x8, x1
        ldr             d16, [x8]
        ldr             d17, [x8, x2]
        mov             x10, x0
        mov             w11, w3
        add             x8, x8, x2, lsl #1
        ldr             d18, [x8]
        ldr             d19, [x8, x2]
        uxtl            v16.8h, v16.8b
        uxtl            v17.8h, v17.8b
        add             x8, x8, x2, lsl #1
        ldr             d20, [x8]
        ldr             d21, [x8, x2]
        uxtl            v18.8h, v18.8b
        uxtl            v19.8h, v19.8b
        add             x8, x8, x2, lsl #1
        ldr             d22, [x8]
        add             x8, x8, x2
        uxtl            v20.8h, v20.8b
        uxtl            v21.8h, v21.8b
        uxtl            v22.8h, v22.8b
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8b}, [x8], x2
        uxtl            \tmp\().8h, \tmp\().8b
        calc_qpelh      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqxtn
        calc_qpelh2     v24, v25, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqxtn2
        subs            w11, w11, #1
        st1             {v24.8h}, [x10], x9
.endm
1:
        calc_all
.purgem calc
2:
        subs            w6, w6, #8
        add             x0, x0, #16
        add             x1, x1, #8
        b.ne            0b
        ret
endfunc

function ff_hevc_put_hevc_qpel_bi_v4_8_neon, export=1
        load_qpel_filterb x7, x6
        sub             x2, x2, x3, lsl #1
        sub             x2, x2, x3
        mov             x12, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.s}[0], [x2], x3
        ld1             {v17.s}[0], [x2], x3
        ld1             {v18.s}[0], [x2], x3
        ld1             {v19.s}[0], [x2], x3
        ld1             {v20.s}[0], [x2], x3
        ld1             {v21.s}[0], [x2], x3
        ld1             {v22.s}[0], [x2], x3
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().s}[0], [x2], x3
        movi            v24.8h, #0
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        ld1             {v25.4h}, [x4], x12 // src2
        sqadd           v24.8h, v24.8h, v25.8h
        sqrshrun        v25.8b, v24.8h, #7
        subs            w5, w5, #1
        st1             {v25.s}[0], [x0], x1
.endm
1:      calc_all
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_qpel_bi_v6_8_neon, export=1
        load_qpel_filterb x7, x6
        sub             x2, x2, x3, lsl #1
        sub             x2, x2, x3
        ld1             {v16.8b}, [x2], x3
        sub             x1, x1, #4
        ld1             {v17.8b}, [x2], x3
        mov             x12, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v18.8b}, [x2], x3
        ld1             {v19.8b}, [x2], x3
        ld1             {v20.8b}, [x2], x3
        ld1             {v21.8b}, [x2], x3
        ld1             {v22.8b}, [x2], x3
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8b}, [x2], x3
        movi            v24.8h, #0
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        ld1             {v25.8h}, [x4], x12 // src2
        sqadd           v24.8h, v24.8h, v25.8h
        sqrshrun        v25.8b, v24.8h, #7
        st1             {v25.s}[0], [x0], #4
        subs            w5, w5, #1
        st1             {v25.h}[2], [x0], x1
.endm
1:      calc_all
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_qpel_bi_v8_8_neon, export=1
        load_qpel_filterb x7, x6
        sub             x2, x2, x3, lsl #1
        sub             x2, x2, x3
        mov             x12, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8b}, [x2], x3
        ld1             {v17.8b}, [x2], x3
        ld1             {v18.8b}, [x2], x3
        ld1             {v19.8b}, [x2], x3
        ld1             {v20.8b}, [x2], x3
        ld1             {v21.8b}, [x2], x3
        ld1             {v22.8b}, [x2], x3
 .macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8b}, [x2], x3
        movi            v24.8h, #0
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        ld1             {v25.8h}, [x4], x12   // src2
        sqadd           v24.8h, v24.8h, v25.8h
        sqrshrun        v25.8b, v24.8h, #7
        subs            w5, w5, #1
        st1             {v25.8b}, [x0], x1
.endm
1:      calc_all
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_qpel_bi_v12_8_neon, export=1
        load_qpel_filterb x7, x6
        sub             x2, x2, x3, lsl #1
        sub             x2, x2, x3
        sub             x1, x1, #8
        ld1             {v16.16b}, [x2], x3
        mov             x12, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v17.16b}, [x2], x3
        ld1             {v18.16b}, [x2], x3
        ld1             {v19.16b}, [x2], x3
        ld1             {v20.16b}, [x2], x3
        ld1             {v21.16b}, [x2], x3
        ld1             {v22.16b}, [x2], x3
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().16b}, [x2], x3
        movi            v24.8h, #0
        movi            v25.8h, #0
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        calc_qpelb2     v25, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        ld1             {v26.8h, v27.8h}, [x4], x12   // src2
        sqadd           v24.8h, v24.8h, v26.8h
        sqadd           v25.8h, v25.8h, v27.8h
        sqrshrun        v26.8b, v24.8h, #7
        sqrshrun2       v26.16b, v25.8h, #7
        st1             {v26.8b}, [x0], #8
        subs            w5, w5, #1
        st1             {v26.s}[2], [x0], x1
.endm
1:      calc_all
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_qpel_bi_v16_8_neon, export=1
        load_qpel_filterb x7, x6
        sub             x2, x2, x3, lsl #1
        sub             x2, x2, x3
        mov             x12, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.16b}, [x2], x3
        ld1             {v17.16b}, [x2], x3
        ld1             {v18.16b}, [x2], x3
        ld1             {v19.16b}, [x2], x3
        ld1             {v20.16b}, [x2], x3
        ld1             {v21.16b}, [x2], x3
        ld1             {v22.16b}, [x2], x3
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().16b}, [x2], x3
        movi            v24.8h, #0
        movi            v25.8h, #0
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        calc_qpelb2     v25, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        ld1             {v26.8h, v27.8h}, [x4], x12   // src2
        sqadd           v24.8h, v24.8h, v26.8h
        sqadd           v25.8h, v25.8h, v27.8h
        sqrshrun        v26.8b, v24.8h, #7
        subs            w5, w5, #1
        sqrshrun2       v26.16b, v25.8h, #7
        st1             {v26.16b}, [x0], x1
.endm
1:      calc_all
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_qpel_bi_v24_8_neon, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        stp             x7, x30, [sp, #48]
        bl              X(ff_hevc_put_hevc_qpel_bi_v16_8_neon)
        ldp             x2, x3, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x7, [sp, #48]
        ldp             x4, x5, [sp], #48
        add             x0, x0, #16
        add             x2, x2, #16
        add             x4, x4, #32
        bl              X(ff_hevc_put_hevc_qpel_bi_v8_8_neon)
        ldr             x30, [sp, #8]
        add             sp, sp, #16
        ret
endfunc

function ff_hevc_put_hevc_qpel_bi_v32_8_neon, export=1
        stp             d8, d9, [sp, #-64]!
        stp             d10, d11, [sp, #16]
        stp             d12, d13, [sp, #32]
        stp             d14, d15, [sp, #48]
        sub             x2, x2, x3, lsl #1
        sub             x2, x2, x3
        load_qpel_filterb x7, x6
        ldr             w6, [sp, #64]
        mov             x12, #(HEVC_MAX_PB_SIZE * 2)
0:      mov             x8, x2          // src
        ld1             {v16.16b, v17.16b}, [x8], x3
        mov             w11, w5         // height
        ld1             {v18.16b, v19.16b}, [x8], x3
        mov             x10, x0         // dst
        ld1             {v20.16b, v21.16b}, [x8], x3
        mov             x9, x4          // src2
        ld1             {v22.16b, v23.16b}, [x8], x3
        ld1             {v24.16b, v25.16b}, [x8], x3
        ld1             {v26.16b, v27.16b}, [x8], x3
        ld1             {v28.16b, v29.16b}, [x8], x3
.macro calc tmp0, tmp1, src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\tmp0\().8h, \tmp1\().8h}, [x8], x3
        movi            v8.8h, #0
        movi            v9.8h, #0
        movi            v10.8h, #0
        movi            v11.8h, #0
        calc_qpelb      v8,  \src0,  \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7
        calc_qpelb2     v9,  \src0, \src1,  \src2,  \src3,  \src4,  \src5,  \src6,  \src7
        calc_qpelb      v10, \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15
        calc_qpelb2     v11, \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15
        ld1             {v12.8h, v13.8h, v14.8h, v15.8h}, [x9], x12   // src2
        sqadd           v8.8h, v8.8h, v12.8h
        sqadd           v9.8h, v9.8h, v13.8h
        sqadd           v10.8h, v10.8h, v14.8h
        sqadd           v11.8h, v11.8h, v15.8h
        sqrshrun        v12.8b, v8.8h, #7
        sqrshrun2       v12.16b, v9.8h, #7
        sqrshrun        v13.8b, v10.8h, #7
        sqrshrun2       v13.16b, v11.8h, #7
        subs            x11, x11, #1
        st1             {v12.16b, v13.16b}, [x10], x1
.endm
1:      calc_all2
.purgem calc
2:      add             x0, x0, #32 // dst
        add             x2, x2, #32 // src
        add             x4, x4, #64 // src2
        subs            w6, w6, #32
        b.ne            0b
        ldp             d10, d11, [sp, #16]
        ldp             d12, d13, [sp, #32]
        ldp             d14, d15, [sp, #48]
        ldp             d8, d9, [sp], #64
        ret
endfunc

function ff_hevc_put_hevc_qpel_bi_v48_8_neon, export=1
        mov             x8, #32
        str             x8, [sp, #-80]!
        stp             x4, x5, [sp, #16]
        stp             x2, x3, [sp, #32]
        stp             x0, x1, [sp, #48]
        stp             x7, x30, [sp, #64]
        bl              X(ff_hevc_put_hevc_qpel_bi_v32_8_neon)
        ldp             x4, x5, [sp, #16]
        ldp             x2, x3, [sp, #32]
        ldp             x0, x1, [sp, #48]
        ldr             x7, [sp, #64]
        add             sp, sp, #64
        add             x0, x0, #32
        add             x2, x2, #32
        add             x4, x4, #64
        bl              X(ff_hevc_put_hevc_qpel_bi_v16_8_neon)
        ldr             x30, [sp, #8]
        add             sp, sp, #16
        ret
endfunc

function ff_hevc_put_hevc_qpel_bi_v64_8_neon, export=1
        b               X(ff_hevc_put_hevc_qpel_bi_v32_8_neon)
endfunc

function ff_vvc_put_pel_uni_pixels4_8_neon, export=1
        b               X(ff_hevc_put_hevc_pel_uni_pixels4_8_neon)
endfunc

function ff_hevc_put_hevc_pel_uni_pixels4_8_neon, export=1
1:
        ldr             s0, [x2]
        ldr             s1, [x2, x3]
        subs            w4, w4, #2
        add             x2, x2, x3, lsl #1
        str             s0, [x0]
        str             s1, [x0, x1]
        add             x0, x0, x1, lsl #1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_uni_pixels6_8_neon, export=1
        sub             x1, x1, #4
1:
        ldr             d0, [x2]
        ldr             d1, [x2, x3]
        subs            w4, w4, #2
        add             x2, x2, x3, lsl #1
        str             s0, [x0], #4
        st1             {v0.h}[2], [x0], x1
        str             s1, [x0], #4
        st1             {v1.h}[2], [x0], x1
        b.hi            1b
        ret
endfunc

function ff_vvc_put_pel_uni_pixels8_8_neon, export=1
        b               X(ff_hevc_put_hevc_pel_uni_pixels8_8_neon)
endfunc

function ff_hevc_put_hevc_pel_uni_pixels8_8_neon, export=1
1:
        ldr             d0, [x2]
        ldr             d1, [x2, x3]
        subs            w4, w4, #2
        add             x2, x2, x3, lsl #1
        str             d0, [x0]
        str             d1, [x0, x1]
        add             x0, x0, x1, lsl #1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_uni_pixels12_8_neon, export=1
        sub             x1, x1, #8
1:
        ldr             q0, [x2]
        ldr             q1, [x2, x3]
        subs            w4, w4, #2
        add             x2, x2, x3, lsl #1
        str             d0, [x0], #8
        st1             {v0.s}[2], [x0], x1
        str             d1, [x0], #8
        st1             {v1.s}[2], [x0], x1
        b.hi            1b
        ret
endfunc

function ff_vvc_put_pel_uni_pixels16_8_neon, export=1
        b               X(ff_hevc_put_hevc_pel_uni_pixels16_8_neon)
endfunc

function ff_hevc_put_hevc_pel_uni_pixels16_8_neon, export=1
1:
        ldr             q0, [x2]
        ldr             q1, [x2, x3]
        subs            w4, w4, #2
        add             x2, x2, x3, lsl #1
        str             q0, [x0]
        str             q1, [x0, x1]
        add             x0, x0, x1, lsl #1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_uni_pixels24_8_neon, export=1
1:
        ld1             {v0.8b, v1.8b, v2.8b}, [x2], x3
        subs            w4, w4, #1
        st1             {v0.8b, v1.8b, v2.8b}, [x0], x1
        b.hi            1b
        ret
endfunc

function ff_vvc_put_pel_uni_pixels32_8_neon, export=1
        b               X(ff_hevc_put_hevc_pel_uni_pixels32_8_neon)
endfunc

function ff_hevc_put_hevc_pel_uni_pixels32_8_neon, export=1
1:
        ld1             {v0.16b, v1.16b}, [x2], x3
        subs            w4, w4, #1
        st1             {v0.16b, v1.16b}, [x0], x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_uni_pixels48_8_neon, export=1
1:
        ld1             {v0.16b, v1.16b, v2.16b}, [x2], x3
        subs            w4, w4, #1
        st1             {v0.16b, v1.16b, v2.16b}, [x0], x1
        b.hi            1b
        ret
endfunc

function ff_vvc_put_pel_uni_pixels64_8_neon, export=1
        b               X(ff_hevc_put_hevc_pel_uni_pixels64_8_neon)
endfunc

function ff_hevc_put_hevc_pel_uni_pixels64_8_neon, export=1
1:
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x2], x3
        subs            w4, w4, #1
        st1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], x1
        b.hi            1b
        ret
endfunc

function ff_vvc_put_pel_uni_pixels128_8_neon, export=1
        sub             x1, x1, #64
        sub             x3, x3, #64
1:
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x2], #64
        subs            w4, w4, #1
        ld1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x2], x3
        st1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
        st1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x0], x1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_v4_8_neon, export=1
        load_qpel_filterb x6, x5
        sub             x2, x2, x3, lsl #1
        sub             x2, x2, x3
        ldr             s16, [x2]
        ldr             s17, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             s18, [x2]
        ldr             s19, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             s20, [x2]
        ldr             s21, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             s22, [x2]
        add             x2, x2, x3
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().s}[0], [x2], x3
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        sqrshrun        v24.8b, v24.8h, #6
        subs            w4, w4, #1
        st1             {v24.s}[0], [x0], x1
.endm
1:      calc_all
.purgem calc
2: ret
endfunc

function ff_hevc_put_hevc_qpel_uni_v6_8_neon, export=1
        load_qpel_filterb x6, x5
        sub             x2, x2, x3, lsl #1
        sub             x1, x1, #4
        sub             x2, x2, x3
        ldr             d16, [x2]
        ldr             d17, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             d18, [x2]
        ldr             d19, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             d20, [x2]
        ldr             d21, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             d22, [x2]
        add             x2, x2, x3
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8b}, [x2], x3
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        sqrshrun        v24.8b, v24.8h, #6
        st1             {v24.s}[0], [x0], #4
        subs            w4, w4, #1
        st1             {v24.h}[2], [x0], x1
.endm
1:      calc_all
.purgem calc
2: ret
endfunc

function ff_hevc_put_hevc_qpel_uni_v8_8_neon, export=1
        load_qpel_filterb x6, x5
        sub             x2, x2, x3, lsl #1
        sub             x2, x2, x3
        ldr             d16, [x2]
        ldr             d17, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             d18, [x2]
        ldr             d19, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             d20, [x2]
        ldr             d21, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             d22, [x2]
        add             x2, x2, x3
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8b}, [x2], x3
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        sqrshrun        v24.8b, v24.8h, #6
        subs            w4, w4, #1
        st1             {v24.8b}, [x0], x1
.endm
1:      calc_all
.purgem calc
2: ret
endfunc

function ff_hevc_put_hevc_qpel_uni_v12_8_neon, export=1
        load_qpel_filterb x6, x5
        sub             x2, x2, x3, lsl #1
        sub             x1, x1, #8
        sub             x2, x2, x3
0:      mov             x8, x2          // src
        mov             w11, w4         // height
        mov             x10, x0         // dst
        ldr             q16, [x8]
        ldr             q17, [x8, x3]
        add             x8, x8, x3, lsl #1
        ldr             q18, [x8]
        ldr             q19, [x8, x3]
        add             x8, x8, x3, lsl #1
        ldr             q20, [x8]
        ldr             q21, [x8, x3]
        add             x8, x8, x3, lsl #1
        ldr             q22, [x8]
        add             x8, x8, x3
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().16b}, [x8], x3
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        calc_qpelb2     v25, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        sqrshrun        v24.8b, v24.8h, #6
        sqrshrun2       v24.16b, v25.8h, #6
        st1             {v24.8b}, [x10], #8
        subs            x11, x11, #1
        st1             {v24.s}[2], [x10], x1
.endm
1:      calc_all
.purgem calc
2:      add             x0, x0, #12
        add             x2, x2, #12
        subs            w7, w7, #12
        b.ne            0b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_v16_8_neon, export=1
        load_qpel_filterb x6, x5
        sub             x2, x2, x3, lsl #1
        sub             x2, x2, x3
0:      mov             x8, x2          // src
        mov             w11, w4         // height
        mov             x10, x0         // dst
        ldr             q16, [x8]
        ldr             q17, [x8, x3]
        add             x8, x8, x3, lsl #1
        ldr             q18, [x8]
        ldr             q19, [x8, x3]
        add             x8, x8, x3, lsl #1
        ldr             q20, [x8]
        ldr             q21, [x8, x3]
        add             x8, x8, x3, lsl #1
        ldr             q22, [x8]
        add             x8, x8, x3
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().16b}, [x8], x3
        calc_qpelb      v24, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        calc_qpelb2     v25, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7
        sqrshrun        v24.8b, v24.8h, #6
        sqrshrun2       v24.16b, v25.8h, #6
        subs            x11, x11, #1
        st1             {v24.16b}, [x10], x1
.endm
1:      calc_all
.purgem calc
2:      add             x0, x0, #16
        add             x2, x2, #16
        subs            w7, w7, #16
        b.ne            0b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_v24_8_neon, export=1
        b               X(ff_hevc_put_hevc_qpel_uni_v12_8_neon)
endfunc

function ff_hevc_put_hevc_qpel_uni_v32_8_neon, export=1
        b               X(ff_hevc_put_hevc_qpel_uni_v16_8_neon)
endfunc

function ff_hevc_put_hevc_qpel_uni_v48_8_neon, export=1
        b               X(ff_hevc_put_hevc_qpel_uni_v16_8_neon)
endfunc

function ff_hevc_put_hevc_qpel_uni_v64_8_neon, export=1
        b               X(ff_hevc_put_hevc_qpel_uni_v16_8_neon)
endfunc

function ff_vvc_put_pel_uni_w_pixels4_8_neon, export=1
        b               X(ff_hevc_put_hevc_pel_uni_w_pixels4_8_neon)
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels4_8_neon, export=1
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.8h, w6
        dup             v31.4s, w10
        dup             v29.4s, w7
1:
        ldr             s0, [x2]
        ldr             s1, [x2, x3]
        add             x2, x2, x3, lsl #1
        ushll           v0.8h, v0.8b, #6
        ushll           v1.8h, v1.8b, #6
        smull           v0.4s, v0.4h, v30.4h
        smull           v1.4s, v1.4h, v30.4h
        sqrshl          v0.4s, v0.4s, v31.4s
        sqrshl          v1.4s, v1.4s, v31.4s
        sqadd           v0.4s, v0.4s, v29.4s
        sqadd           v1.4s, v1.4s, v29.4s
        sqxtn           v0.4h, v0.4s
        sqxtn           v1.4h, v1.4s
        sqxtun          v0.8b, v0.8h
        sqxtun          v1.8b, v1.8h
        str             s0, [x0]
        str             s1, [x0, x1]
        add             x0, x0, x1, lsl #1
        subs            w4, w4, #2
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels6_8_neon, export=1
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.8h, w6
        dup             v31.4s, w10
        dup             v29.4s, w7
        sub             x1, x1, #4
1:
        ldr             d0, [x2]
        ldr             d1, [x2, x3]
        add             x2, x2, x3, lsl #1
        ushll           v0.8h, v0.8b, #6
        ushll           v1.8h, v1.8b, #6
        smull           v4.4s, v0.4h, v30.4h
        smull2          v5.4s, v0.8h, v30.8h
        smull           v6.4s, v1.4h, v30.4h
        smull2          v7.4s, v1.8h, v30.8h
        sqrshl          v4.4s, v4.4s, v31.4s
        sqrshl          v5.4s, v5.4s, v31.4s
        sqrshl          v6.4s, v6.4s, v31.4s
        sqrshl          v7.4s, v7.4s, v31.4s
        sqadd           v4.4s, v4.4s, v29.4s
        sqadd           v5.4s, v5.4s, v29.4s
        sqadd           v6.4s, v6.4s, v29.4s
        sqadd           v7.4s, v7.4s, v29.4s
        sqxtn           v0.4h, v4.4s
        sqxtn2          v0.8h, v5.4s
        sqxtn           v1.4h, v6.4s
        sqxtn2          v1.8h, v7.4s
        sqxtun          v0.8b, v0.8h
        sqxtun          v1.8b, v1.8h
        str             s0, [x0], #4
        st1             {v0.h}[2], [x0], x1
        str             s1, [x0], #4
        st1             {v1.h}[2], [x0], x1
        subs            w4, w4, #2
        b.ne            1b
        ret
endfunc

function ff_vvc_put_pel_uni_w_pixels8_8_neon, export=1
        b               X(ff_hevc_put_hevc_pel_uni_w_pixels8_8_neon)
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels8_8_neon, export=1
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.8h, w6
        dup             v31.4s, w10
        dup             v29.4s, w7
1:
        ldr             d0, [x2]
        ldr             d1, [x2, x3]
        add             x2, x2, x3, lsl #1
        ushll           v0.8h, v0.8b, #6
        ushll           v1.8h, v1.8b, #6
        smull           v4.4s, v0.4h, v30.4h
        smull2          v5.4s, v0.8h, v30.8h
        smull           v6.4s, v1.4h, v30.4h
        smull2          v7.4s, v1.8h, v30.8h
        sqrshl          v4.4s, v4.4s, v31.4s
        sqrshl          v5.4s, v5.4s, v31.4s
        sqrshl          v6.4s, v6.4s, v31.4s
        sqrshl          v7.4s, v7.4s, v31.4s
        sqadd           v4.4s, v4.4s, v29.4s
        sqadd           v5.4s, v5.4s, v29.4s
        sqadd           v6.4s, v6.4s, v29.4s
        sqadd           v7.4s, v7.4s, v29.4s
        sqxtn           v0.4h, v4.4s
        sqxtn2          v0.8h, v5.4s
        sqxtn           v1.4h, v6.4s
        sqxtn2          v1.8h, v7.4s
        sqxtun          v0.8b, v0.8h
        sqxtun          v1.8b, v1.8h
        str             d0, [x0]
        str             d1, [x0, x1]
        add             x0, x0, x1, lsl #1
        subs            w4, w4, #2
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels12_8_neon, export=1
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.8h, w6
        dup             v31.4s, w10
        dup             v29.4s, w7
        sub             x1, x1, #8
1:
        ldr             q0, [x2]
        ldr             q1, [x2, x3]
        add             x2, x2, x3, lsl #1
        ushll           v4.8h, v0.8b, #6
        ushll2          v5.8h, v0.16b, #6
        ushll           v6.8h, v1.8b, #6
        ushll2          v7.8h, v1.16b, #6
        smull           v16.4s, v4.4h, v30.4h
        smull2          v17.4s, v4.8h, v30.8h
        smull           v18.4s, v5.4h, v30.4h
        smull2          v19.4s, v5.8h, v30.8h
        smull           v20.4s, v6.4h, v30.4h
        smull2          v21.4s, v6.8h, v30.8h
        smull           v22.4s, v7.4h, v30.4h
        smull2          v23.4s, v7.8h, v30.8h

        sqrshl          v16.4s, v16.4s, v31.4s
        sqrshl          v17.4s, v17.4s, v31.4s
        sqrshl          v18.4s, v18.4s, v31.4s
        sqrshl          v19.4s, v19.4s, v31.4s
        sqrshl          v20.4s, v20.4s, v31.4s
        sqrshl          v21.4s, v21.4s, v31.4s
        sqrshl          v22.4s, v22.4s, v31.4s
        sqrshl          v23.4s, v23.4s, v31.4s
        sqadd           v16.4s, v16.4s, v29.4s
        sqadd           v17.4s, v17.4s, v29.4s
        sqadd           v18.4s, v18.4s, v29.4s
        sqadd           v19.4s, v19.4s, v29.4s
        sqadd           v20.4s, v20.4s, v29.4s
        sqadd           v21.4s, v21.4s, v29.4s
        sqadd           v22.4s, v22.4s, v29.4s
        sqadd           v23.4s, v23.4s, v29.4s
        sqxtn           v0.4h, v16.4s
        sqxtn2          v0.8h, v17.4s
        sqxtn           v1.4h, v18.4s
        sqxtn2          v1.8h, v19.4s
        sqxtn           v2.4h, v20.4s
        sqxtn2          v2.8h, v21.4s
        sqxtn           v3.4h, v22.4s
        sqxtn2          v3.8h, v23.4s
        sqxtun          v0.8b, v0.8h
        sqxtun2         v0.16b, v1.8h
        sqxtun          v2.8b, v2.8h
        sqxtun2         v2.16b, v3.8h
        str             d0, [x0], #8
        st1             {v0.s}[2], [x0], x1
        str             d2, [x0], #8
        st1             {v2.s}[2], [x0], x1
        subs            w4, w4, #2
        b.ne            1b
        ret
endfunc

.macro PEL_UNI_W_PIXEL_CALC     s0, t0, t1, d0, d1, d2, d3
        ushll           \t0\().8h, \s0\().8b, #6
        ushll2          \t1\().8h, \s0\().16b, #6
        smull           \d0\().4s, \t0\().4h, v30.4h
        smull2          \d1\().4s, \t0\().8h, v30.8h
        smull           \d2\().4s, \t1\().4h, v30.4h
        smull2          \d3\().4s, \t1\().8h, v30.8h
        sqrshl          \d0\().4s, \d0\().4s, v31.4s
        sqrshl          \d1\().4s, \d1\().4s, v31.4s
        sqrshl          \d2\().4s, \d2\().4s, v31.4s
        sqrshl          \d3\().4s, \d3\().4s, v31.4s
        sqadd           \d0\().4s, \d0\().4s, v29.4s
        sqadd           \d1\().4s, \d1\().4s, v29.4s
        sqadd           \d2\().4s, \d2\().4s, v29.4s
        sqadd           \d3\().4s, \d3\().4s, v29.4s
        sqxtn           \t0\().4h, \d0\().4s
        sqxtn2          \t0\().8h, \d1\().4s
        sqxtn           \t1\().4h, \d2\().4s
        sqxtn2          \t1\().8h, \d3\().4s
        sqxtun          \s0\().8b,  \t0\().8h
        sqxtun2         \s0\().16b, \t1\().8h
.endm


function ff_hevc_put_hevc_pel_uni_w_pixels16_8_neon, export=1
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.8h, w6
        dup             v31.4s, w10
        dup             v29.4s, w7
1:
        ldr             q0, [x2]
        ldr             q1, [x2, x3]
        add             x2, x2, x3, lsl #1
        PEL_UNI_W_PIXEL_CALC v0, v4, v5, v16, v17, v18, v19
        PEL_UNI_W_PIXEL_CALC v1, v6, v7, v20, v21, v22, v23
        str             q0, [x0]
        str             q1, [x0, x1]
        add             x0, x0, x1, lsl #1
        subs            w4, w4, #2
        b.ne            1b
        ret
endfunc

function ff_vvc_put_pel_uni_w_pixels16_8_neon, export=1
        b               X(ff_hevc_put_hevc_pel_uni_w_pixels16_8_neon)
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels24_8_neon, export=1
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.8h, w6
        dup             v31.4s, w10
        dup             v29.4s, w7
1:
        ld1             {v0.16b, v1.16b}, [x2], x3
        ushll           v4.8h, v0.8b, #6
        ushll2          v5.8h, v0.16b, #6
        ushll           v6.8h, v1.8b, #6
        smull           v16.4s, v4.4h, v30.4h
        smull2          v17.4s, v4.8h, v30.8h
        smull           v18.4s, v5.4h, v30.4h
        smull2          v19.4s, v5.8h, v30.8h
        smull           v20.4s, v6.4h, v30.4h
        smull2          v21.4s, v6.8h, v30.8h
        sqrshl          v16.4s, v16.4s, v31.4s
        sqrshl          v17.4s, v17.4s, v31.4s
        sqrshl          v18.4s, v18.4s, v31.4s
        sqrshl          v19.4s, v19.4s, v31.4s
        sqrshl          v20.4s, v20.4s, v31.4s
        sqrshl          v21.4s, v21.4s, v31.4s
        sqadd           v16.4s, v16.4s, v29.4s
        sqadd           v17.4s, v17.4s, v29.4s
        sqadd           v18.4s, v18.4s, v29.4s
        sqadd           v19.4s, v19.4s, v29.4s
        sqadd           v20.4s, v20.4s, v29.4s
        sqadd           v21.4s, v21.4s, v29.4s
        sqxtn           v0.4h, v16.4s
        sqxtn2          v0.8h, v17.4s
        sqxtn           v1.4h, v18.4s
        sqxtn2          v1.8h, v19.4s
        sqxtn           v2.4h, v20.4s
        sqxtn2          v2.8h, v21.4s
        sqxtun          v0.8b, v0.8h
        sqxtun          v1.8b, v1.8h
        sqxtun          v2.8b, v2.8h
        st1             {v0.8b, v1.8b, v2.8b}, [x0], x1
        subs            w4, w4, #1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels32_8_neon, export=1
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.8h, w6
        dup             v31.4s, w10
        dup             v29.4s, w7
1:
        ld1             {v0.16b, v1.16b}, [x2], x3
        PEL_UNI_W_PIXEL_CALC v0, v4, v5, v16, v17, v18, v19
        PEL_UNI_W_PIXEL_CALC v1, v6, v7, v20, v21, v22, v23
        st1             {v0.16b, v1.16b}, [x0], x1
        subs            w4, w4, #1
        b.ne            1b
        ret
endfunc

function ff_vvc_put_pel_uni_w_pixels32_8_neon, export=1
        b               X(ff_hevc_put_hevc_pel_uni_w_pixels32_8_neon)
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels48_8_neon, export=1
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.8h, w6
        dup             v31.4s, w10
        dup             v29.4s, w7
1:
        ld1             {v0.16b, v1.16b, v2.16b}, [x2], x3
        PEL_UNI_W_PIXEL_CALC v0, v4, v5, v16, v17, v18, v19
        PEL_UNI_W_PIXEL_CALC v1, v6, v7, v20, v21, v22, v23
        PEL_UNI_W_PIXEL_CALC v2, v4, v5, v16, v17, v18, v19
        st1             {v0.16b, v1.16b, v2.16b}, [x0], x1
        subs            w4, w4, #1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels64_8_neon, export=1
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.8h, w6
        dup             v31.4s, w10
        dup             v29.4s, w7
1:
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x2], x3
        PEL_UNI_W_PIXEL_CALC v0, v4, v5, v16, v17, v18, v19
        PEL_UNI_W_PIXEL_CALC v1, v6, v7, v20, v21, v22, v23
        PEL_UNI_W_PIXEL_CALC v2, v4, v5, v16, v17, v18, v19
        PEL_UNI_W_PIXEL_CALC v3, v6, v7, v20, v21, v22, v23
        st1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], x1
        subs            w4, w4, #1
        b.ne            1b
        ret
endfunc

function ff_vvc_put_pel_uni_w_pixels64_8_neon, export=1
        b               X(ff_hevc_put_hevc_pel_uni_w_pixels64_8_neon)
endfunc

function ff_vvc_put_pel_uni_w_pixels128_8_neon, export=1
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.8h, w6
        dup             v31.4s, w10
        dup             v29.4s, w7
        sub             x1, x1, #64
        sub             x3, x3, #64
1:
        mov             x11, x2
        mov             x12, x0
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x2], #64
        PEL_UNI_W_PIXEL_CALC v0, v4, v5, v16, v17, v18, v19
        PEL_UNI_W_PIXEL_CALC v1, v6, v7, v20, v21, v22, v23
        PEL_UNI_W_PIXEL_CALC v2, v4, v5, v16, v17, v18, v19
        PEL_UNI_W_PIXEL_CALC v3, v6, v7, v20, v21, v22, v23
        st1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64

        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x2], x3
        subs            w4, w4, #1
        PEL_UNI_W_PIXEL_CALC v0, v4, v5, v16, v17, v18, v19
        PEL_UNI_W_PIXEL_CALC v1, v6, v7, v20, v21, v22, v23
        PEL_UNI_W_PIXEL_CALC v2, v4, v5, v16, v17, v18, v19
        PEL_UNI_W_PIXEL_CALC v3, v6, v7, v20, v21, v22, v23
        st1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], x1
        b.ne            1b
        ret
endfunc

.macro QPEL_UNI_W_V_HEADER
        ldur            x12, [sp, #8]          // my
        sub             x2, x2, x3, lsl #1
        sub             x2, x2, x3
        movrel          x9, qpel_filters_abs
        add             x9, x9, x12, lsl #3
        ldr             d28, [x9]
        dup             v0.16b, v28.b[0]
        dup             v1.16b, v28.b[1]
        dup             v2.16b, v28.b[2]
        dup             v3.16b, v28.b[3]
        dup             v4.16b, v28.b[4]
        dup             v5.16b, v28.b[5]
        dup             v6.16b, v28.b[6]
        dup             v7.16b, v28.b[7]

        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.8h, w6              // wx
        dup             v31.4s, w10             // shift
        dup             v29.4s, w7              // ox
.endm

.macro QPEL_FILTER_B dst, src0, src1, src2, src3, src4, src5, src6, src7
        umull           \dst\().8h, \src1\().8b, v1.8b
        umlsl           \dst\().8h, \src0\().8b, v0.8b
        umlsl           \dst\().8h, \src2\().8b, v2.8b
        umlal           \dst\().8h, \src3\().8b, v3.8b
        umlal           \dst\().8h, \src4\().8b, v4.8b
        umlsl           \dst\().8h, \src5\().8b, v5.8b
        umlal           \dst\().8h, \src6\().8b, v6.8b
        umlsl           \dst\().8h, \src7\().8b, v7.8b
.endm

.macro QPEL_FILTER_B2 dst, src0, src1, src2, src3, src4, src5, src6, src7
        umull2          \dst\().8h, \src1\().16b, v1.16b
        umlsl2          \dst\().8h, \src0\().16b, v0.16b
        umlsl2          \dst\().8h, \src2\().16b, v2.16b
        umlal2          \dst\().8h, \src3\().16b, v3.16b
        umlal2          \dst\().8h, \src4\().16b, v4.16b
        umlsl2          \dst\().8h, \src5\().16b, v5.16b
        umlal2          \dst\().8h, \src6\().16b, v6.16b
        umlsl2          \dst\().8h, \src7\().16b, v7.16b
.endm

.macro  QPEL_UNI_W_V_4
        smull           v24.4s, v24.4h, v30.4h
        sqrshl          v24.4s, v24.4s, v31.4s
        sqadd           v24.4s, v24.4s, v29.4s
        sqxtn           v24.4h, v24.4s
        sqxtun          v24.8b, v24.8h
        st1             {v24.s}[0], [x0], x1
.endm

function ff_hevc_put_hevc_qpel_uni_w_v4_8_neon, export=1
        QPEL_UNI_W_V_HEADER
        ldr             s16, [x2]
        ldr             s17, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             s18, [x2]
        ldr             s19, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             s20, [x2]
        ldr             s21, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             s22, [x2]

1:      ldr             s23, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v24, v16, v17, v18, v19, v20, v21, v22, v23
        QPEL_UNI_W_V_4
        subs            w4, w4, #1
        b.eq            2f

        ldr             s16, [x2]
        QPEL_FILTER_B   v24, v17, v18, v19, v20, v21, v22, v23, v16
        QPEL_UNI_W_V_4
        subs            w4, w4, #1
        b.eq            2f

        ldr             s17, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v24, v18, v19, v20, v21, v22, v23, v16, v17
        QPEL_UNI_W_V_4
        subs            w4, w4, #1
        b.eq            2f

        ldr             s18, [x2]
        QPEL_FILTER_B   v24, v19, v20, v21, v22, v23, v16, v17, v18
        QPEL_UNI_W_V_4
        subs            w4, w4, #1
        b.eq            2f

        ldr             s19, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v24, v20, v21, v22, v23, v16, v17, v18, v19
        QPEL_UNI_W_V_4
        subs            w4, w4, #1
        b.eq            2f

        ldr             s20, [x2]
        QPEL_FILTER_B   v24, v21, v22, v23, v16, v17, v18, v19, v20
        QPEL_UNI_W_V_4
        subs            w4, w4, #1
        b.eq            2f

        ldr             s21, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v24, v22, v23, v16, v17, v18, v19, v20, v21
        QPEL_UNI_W_V_4
        subs            w4, w4, #1
        b.eq            2f

        ldr             s22, [x2]
        QPEL_FILTER_B   v24, v23, v16, v17, v18, v19, v20, v21, v22
        QPEL_UNI_W_V_4
        subs            w4, w4, #1
        b.ne            1b
2:
        ret
endfunc

.macro QPEL_UNI_W_V_8
        smull           v24.4s, v26.4h, v30.4h
        smull2          v25.4s, v26.8h, v30.8h
        sqrshl          v24.4s, v24.4s, v31.4s
        sqrshl          v25.4s, v25.4s, v31.4s
        sqadd           v24.4s, v24.4s, v29.4s
        sqadd           v25.4s, v25.4s, v29.4s
        sqxtn           v24.4h, v24.4s
        sqxtn2          v24.8h, v25.4s
        sqxtun          v24.8b, v24.8h
        st1             {v24.d}[0], [x0], x1
.endm

function ff_hevc_put_hevc_qpel_uni_w_v8_8_neon, export=1
        QPEL_UNI_W_V_HEADER
        ldr             d16, [x2]
        ldr             d17, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             d18, [x2]
        ldr             d19, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             d20, [x2]
        ldr             d21, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             d22, [x2]

1:      ldr             d23, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v26, v16, v17, v18, v19, v20, v21, v22, v23
        QPEL_UNI_W_V_8
        subs            w4, w4, #1
        b.eq            2f

        ldr             d16, [x2]
        QPEL_FILTER_B   v26, v17, v18, v19, v20, v21, v22, v23, v16
        QPEL_UNI_W_V_8
        subs            w4, w4, #1
        b.eq            2f

        ldr             d17, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v26, v18, v19, v20, v21, v22, v23, v16, v17
        QPEL_UNI_W_V_8
        subs            w4, w4, #1
        b.eq            2f

        ldr             d18, [x2]
        QPEL_FILTER_B   v26, v19, v20, v21, v22, v23, v16, v17, v18
        QPEL_UNI_W_V_8
        subs            w4, w4, #1
        b.eq            2f

        ldr             d19, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v26, v20, v21, v22, v23, v16, v17, v18, v19
        QPEL_UNI_W_V_8
        subs            w4, w4, #1
        b.eq            2f

        ldr             d20, [x2]
        QPEL_FILTER_B   v26, v21, v22, v23, v16, v17, v18, v19, v20
        QPEL_UNI_W_V_8
        subs            w4, w4, #1
        b.eq            2f

        ldr             d21, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v26, v22, v23, v16, v17, v18, v19, v20, v21
        QPEL_UNI_W_V_8
        subs            w4, w4, #1
        b.eq            2f

        ldr             d22, [x2]
        QPEL_FILTER_B   v26, v23, v16, v17, v18, v19, v20, v21, v22
        QPEL_UNI_W_V_8
        subs            w4, w4, #1
        b.ne            1b
2:
        ret
endfunc

.macro QPEL_UNI_W_V_16
        smull           v24.4s, v26.4h, v30.4h
        smull2          v25.4s, v26.8h, v30.8h
        smull           v26.4s, v27.4h, v30.4h
        smull2          v27.4s, v27.8h, v30.8h
        sqrshl          v24.4s, v24.4s, v31.4s
        sqrshl          v25.4s, v25.4s, v31.4s
        sqrshl          v26.4s, v26.4s, v31.4s
        sqrshl          v27.4s, v27.4s, v31.4s
        sqadd           v24.4s, v24.4s, v29.4s
        sqadd           v25.4s, v25.4s, v29.4s
        sqadd           v26.4s, v26.4s, v29.4s
        sqadd           v27.4s, v27.4s, v29.4s
        sqxtn           v24.4h, v24.4s
        sqxtn2          v24.8h, v25.4s
        sqxtn           v26.4h, v26.4s
        sqxtn2          v26.8h, v27.4s
        sqxtun          v24.8b, v24.8h
        sqxtun2         v24.16b, v26.8h
        st1             {v24.16b}, [x0], x1
.endm

function ff_hevc_put_hevc_qpel_uni_w_v16_8_neon, export=1
        QPEL_UNI_W_V_HEADER
        ldr             q16, [x2]
        ldr             q17, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             q18, [x2]
        ldr             q19, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             q20, [x2]
        ldr             q21, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             q22, [x2]

1:      ldr             q23, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v26, v16, v17, v18, v19, v20, v21, v22, v23
        QPEL_FILTER_B2  v27, v16, v17, v18, v19, v20, v21, v22, v23
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q16, [x2]
        QPEL_FILTER_B   v26, v17, v18, v19, v20, v21, v22, v23, v16
        QPEL_FILTER_B2  v27, v17, v18, v19, v20, v21, v22, v23, v16
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q17, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v26, v18, v19, v20, v21, v22, v23, v16, v17
        QPEL_FILTER_B2  v27, v18, v19, v20, v21, v22, v23, v16, v17
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q18, [x2]
        QPEL_FILTER_B   v26, v19, v20, v21, v22, v23, v16, v17, v18
        QPEL_FILTER_B2  v27, v19, v20, v21, v22, v23, v16, v17, v18
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q19, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v26, v20, v21, v22, v23, v16, v17, v18, v19
        QPEL_FILTER_B2  v27, v20, v21, v22, v23, v16, v17, v18, v19
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q20, [x2]
        QPEL_FILTER_B   v26, v21, v22, v23, v16, v17, v18, v19, v20
        QPEL_FILTER_B2  v27, v21, v22, v23, v16, v17, v18, v19, v20
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q21, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v26, v22, v23, v16, v17, v18, v19, v20, v21
        QPEL_FILTER_B2  v27, v22, v23, v16, v17, v18, v19, v20, v21
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q22, [x2]
        QPEL_FILTER_B   v26, v23, v16, v17, v18, v19, v20, v21, v22
        QPEL_FILTER_B2  v27, v23, v16, v17, v18, v19, v20, v21, v22
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.ne            1b
2:
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_w_v64_8_neon, export=1
        QPEL_UNI_W_V_HEADER
        ldur            w13, [sp, #16]
        mov             x14, x0
        mov             x15, x2
        mov             w11, w4

3:
        ldr             q16, [x2]
        ldr             q17, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             q18, [x2]
        ldr             q19, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             q20, [x2]
        ldr             q21, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             q22, [x2]


1:      ldr             q23, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v26, v16, v17, v18, v19, v20, v21, v22, v23
        QPEL_FILTER_B2  v27, v16, v17, v18, v19, v20, v21, v22, v23
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q16, [x2]
        QPEL_FILTER_B   v26, v17, v18, v19, v20, v21, v22, v23, v16
        QPEL_FILTER_B2  v27, v17, v18, v19, v20, v21, v22, v23, v16
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q17, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v26, v18, v19, v20, v21, v22, v23, v16, v17
        QPEL_FILTER_B2  v27, v18, v19, v20, v21, v22, v23, v16, v17
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q18, [x2]
        QPEL_FILTER_B   v26, v19, v20, v21, v22, v23, v16, v17, v18
        QPEL_FILTER_B2  v27, v19, v20, v21, v22, v23, v16, v17, v18
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q19, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v26, v20, v21, v22, v23, v16, v17, v18, v19
        QPEL_FILTER_B2  v27, v20, v21, v22, v23, v16, v17, v18, v19
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q20, [x2]
        QPEL_FILTER_B   v26, v21, v22, v23, v16, v17, v18, v19, v20
        QPEL_FILTER_B2  v27, v21, v22, v23, v16, v17, v18, v19, v20
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q21, [x2, x3]
        add             x2, x2, x3, lsl #1
        QPEL_FILTER_B   v26, v22, v23, v16, v17, v18, v19, v20, v21
        QPEL_FILTER_B2  v27, v22, v23, v16, v17, v18, v19, v20, v21
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.eq            2f

        ldr             q22, [x2]
        QPEL_FILTER_B   v26, v23, v16, v17, v18, v19, v20, v21, v22
        QPEL_FILTER_B2  v27, v23, v16, v17, v18, v19, v20, v21, v22
        QPEL_UNI_W_V_16
        subs            w4, w4, #1
        b.ne            1b
2:
        subs            w13, w13, #16
        add             x14, x14, #16
        add             x15, x15, #16
        mov             x0, x14
        mov             x2, x15
        mov             w4, w11
        b.hi            3b
        ret
endfunc

function hevc_put_hevc_qpel_uni_hv4_8_end_neon
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        load_qpel_filterh x6, x5
        ldr             d16, [sp]
        ldr             d17, [sp, x9]
        add             sp, sp, x9, lsl #1
        ldr             d18, [sp]
        ldr             d19, [sp, x9]
        add             sp, sp, x9, lsl #1
        ldr             d20, [sp]
        ldr             d21, [sp, x9]
        add             sp, sp, x9, lsl #1
        ldr             d22, [sp]
        add             sp, sp, x9
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().4h}, [sp], x9
        calc_qpelh      v1, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqrshrn, #12
        sqxtun          v1.8b, v1.8h
        subs            w4, w4, #1
        st1             {v1.s}[0], [x0], x1
.endm
1:      calc_all
.purgem calc
2:      mov             sp, x14
        ret
endfunc

function hevc_put_hevc_qpel_uni_hv6_8_end_neon
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        load_qpel_filterh x6, x5
        sub             x1, x1, #4
        ldr             q16, [sp]
        ldr             q17, [sp, x9]
        add             sp, sp, x9, lsl #1
        ldr             q18, [sp]
        ldr             q19, [sp, x9]
        add             sp, sp, x9, lsl #1
        ldr             q20, [sp]
        ldr             q21, [sp, x9]
        add             sp, sp, x9, lsl #1
        ldr             q22, [sp]
        add             sp, sp, x9
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8h}, [sp], x9
        calc_qpelh      v1, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqrshrn, #12
        calc_qpelh2     v1, v2, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqrshrn2, #12
        sqxtun          v1.8b, v1.8h
        st1             {v1.s}[0], [x0], #4
        subs            w4, w4, #1
        st1             {v1.h}[2], [x0], x1
.endm
1:      calc_all
.purgem calc
2:      mov             sp, x14
        ret
endfunc

function hevc_put_hevc_qpel_uni_hv8_8_end_neon
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        load_qpel_filterh x6, x5
        ldr             q16, [sp]
        ldr             q17, [sp, x9]
        add             sp, sp, x9, lsl #1
        ldr             q18, [sp]
        ldr             q19, [sp, x9]
        add             sp, sp, x9, lsl #1
        ldr             q20, [sp]
        ldr             q21, [sp, x9]
        add             sp, sp, x9, lsl #1
        ldr             q22, [sp]
        add             sp, sp, x9
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8h}, [sp], x9
        calc_qpelh      v1, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqrshrn, #12
        calc_qpelh2     v1, v2, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqrshrn2, #12
        sqxtun          v1.8b, v1.8h
        subs            w4, w4, #1
        st1             {v1.8b}, [x0], x1
.endm
1:      calc_all
.purgem calc
2:      mov             sp, x14
        ret
endfunc

function hevc_put_hevc_qpel_uni_hv12_8_end_neon
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        load_qpel_filterh x6, x5
        sub             x1, x1, #8
        ld1             {v16.8h, v17.8h}, [sp], x9
        ld1             {v18.8h, v19.8h}, [sp], x9
        ld1             {v20.8h, v21.8h}, [sp], x9
        ld1             {v22.8h, v23.8h}, [sp], x9
        ld1             {v24.8h, v25.8h}, [sp], x9
        ld1             {v26.8h, v27.8h}, [sp], x9
        ld1             {v28.8h, v29.8h}, [sp], x9
.macro calc tmp0, tmp1, src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\tmp0\().8h, \tmp1\().8h}, [sp], x9
        calc_qpelh      v1,     \src0,  \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7, sqrshrn, #12
        calc_qpelh2     v1, v2, \src0, \src1,  \src2,  \src3,  \src4,  \src5,  \src6,  \src7, sqrshrn2, #12
        calc_qpelh      v2,     \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15, sqrshrn, #12
        sqxtun          v1.8b, v1.8h
        sqxtun2         v1.16b, v2.8h
        st1             {v1.8b}, [x0], #8
        subs            w4, w4, #1
        st1             {v1.s}[2], [x0], x1
.endm
1:      calc_all2
.purgem calc
2:      mov             sp, x14
        ret
endfunc

function hevc_put_hevc_qpel_uni_hv16_8_end_neon
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        load_qpel_filterh x6, x5
        sub             w12, w9, w7, lsl #1
0:      mov             x8, sp          // src
        ld1             {v16.8h, v17.8h}, [x8], x9
        mov             w11, w4         // height
        ld1             {v18.8h, v19.8h}, [x8], x9
        mov             x10, x0         // dst
        ld1             {v20.8h, v21.8h}, [x8], x9
        ld1             {v22.8h, v23.8h}, [x8], x9
        ld1             {v24.8h, v25.8h}, [x8], x9
        ld1             {v26.8h, v27.8h}, [x8], x9
        ld1             {v28.8h, v29.8h}, [x8], x9
.macro calc tmp0, tmp1, src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\tmp0\().8h, \tmp1\().8h}, [x8], x9
        calc_qpelh      v1,     \src0, \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7,  sqrshrn,  #12
        calc_qpelh2     v1, v2, \src0, \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7,  sqrshrn2, #12
        calc_qpelh      v2,     \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15, sqrshrn,  #12
        calc_qpelh2     v2, v3, \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15, sqrshrn2, #12
        sqxtun          v1.8b, v1.8h
        subs            x11, x11, #1
        sqxtun2         v1.16b, v2.8h
        st1             {v1.16b}, [x10], x1
.endm
1:      calc_all2
.purgem calc
2:      add             x0, x0, #16
        add             sp, sp, #32
        subs            w7, w7, #16
        b.ne            0b
        mov             sp, x14
        ret
endfunc

.macro qpel_uni_hv suffix
function ff_hevc_put_hevc_qpel_uni_hv4_8_\suffix, export=1
        add             w10, w4, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x30, x14,[sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        sub             x1, x2, x3, lsl #1
        sub             x1, x1, x3
        add             x0, sp, #48
        mov             x2, x3
        add             x3, x4, #7
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_qpel_h4_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x30, x14, [sp], #48
        b               hevc_put_hevc_qpel_uni_hv4_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_uni_hv6_8_\suffix, export=1
        add             w10, w4, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x30, x14,[sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        sub             x1, x2, x3, lsl #1
        sub             x1, x1, x3
        add             x0, sp, #48
        mov             x2, x3
        add             w3, w4, #7
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_qpel_h6_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x30, x14, [sp], #48
        b               hevc_put_hevc_qpel_uni_hv6_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_uni_hv8_8_\suffix, export=1
        add             w10, w4, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x30, x14,[sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        sub             x1, x2, x3, lsl #1
        sub             x1, x1, x3
        add             x0, sp, #48
        mov             x2, x3
        add             w3, w4, #7
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_qpel_h8_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x30, x14, [sp], #48
        b               hevc_put_hevc_qpel_uni_hv8_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_uni_hv12_8_\suffix, export=1
        add             w10, w4, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x7, x30, [sp, #-64]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x14,    [sp, #48]
        sub             x1, x2, x3, lsl #1
        sub             x1, x1, x3
        mov             x2, x3
        add             x0, sp, #64
        add             w3, w4, #7
        mov             x4, x5
        mov             w6, #12
        bl              X(ff_hevc_put_hevc_qpel_h12_8_\suffix)
        ldr             x14,    [sp, #48]
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x7, x30, [sp], #64
        b               hevc_put_hevc_qpel_uni_hv12_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_uni_hv16_8_\suffix, export=1
        add             w10, w4, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x7, x30, [sp, #-64]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x14,    [sp, #48]
        add             x0, sp, #64
        sub             x1, x2, x3, lsl #1
        sub             x1, x1, x3
        mov             x2, x3
        add             w3, w4, #7
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_qpel_h16_8_\suffix)
        ldr             x14,    [sp, #48]
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x7, x30, [sp], #64
        b               hevc_put_hevc_qpel_uni_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_uni_hv24_8_\suffix, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        stp             x6, x30, [sp, #48]
        mov             x7, #16
        bl              X(ff_hevc_put_hevc_qpel_uni_hv16_8_\suffix)
        ldp             x2, x3, [sp, #16]
        add             x2, x2, #16
        ldp             x0, x1, [sp, #32]
        ldp             x4, x5, [sp], #48
        mov             x7, #8
        add             x0, x0, #16
        ldr             x6, [sp]
        bl              X(ff_hevc_put_hevc_qpel_uni_hv8_8_\suffix)
        ldr             x30, [sp, #8]
        add             sp, sp, #16
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_hv32_8_\suffix, export=1
        add             w10, w4, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x7, x30, [sp, #-64]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x14,    [sp, #48]
        sub             x1, x2, x3, lsl #1
        add             x0, sp, #64
        sub             x1, x1, x3
        mov             x2, x3
        add             w3, w4, #7
        mov             x4, x5
        mov             w6, #32
        bl              X(ff_hevc_put_hevc_qpel_h32_8_\suffix)
        ldr             x14,    [sp, #48]
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x7, x30, [sp], #64
        b               hevc_put_hevc_qpel_uni_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_uni_hv48_8_\suffix, export=1
        add             w10, w4, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x7, x30, [sp, #-64]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x14,    [sp, #48]
        sub             x1, x2, x3, lsl #1
        sub             x1, x1, x3
        mov             x2, x3
        add             x0, sp, #64
        add             w3, w4, #7
        mov             x4, x5
.ifc \suffix, neon
        mov             w6, #48
        bl              X(ff_hevc_put_hevc_qpel_h32_8_\suffix)
.else
        bl              X(ff_hevc_put_hevc_qpel_h48_8_\suffix)
.endif
        ldr             x14,    [sp, #48]
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x7, x30, [sp], #64
        b               hevc_put_hevc_qpel_uni_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_uni_hv64_8_\suffix, export=1
        add             w10, w4, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x7, x30, [sp, #-64]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x14,    [sp, #48]
        add             x0, sp, #64
        sub             x1, x2, x3, lsl #1
        mov             x2, x3
        sub             x1, x1, x3
        add             w3, w4, #7
        mov             x4, x5
.ifc \suffix, neon
        mov             w6, #64
        bl              X(ff_hevc_put_hevc_qpel_h32_8_\suffix)
.else
        bl              X(ff_hevc_put_hevc_qpel_h64_8_\suffix)
.endif
        ldr             x14,    [sp, #48]
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x7, x30, [sp], #64
        b               hevc_put_hevc_qpel_uni_hv16_8_end_neon
endfunc
.endm

qpel_uni_hv neon

#if HAVE_I8MM
ENABLE_I8MM

qpel_uni_hv neon_i8mm

DISABLE_I8MM
#endif

.macro QPEL_UNI_W_H_HEADER elems=4s
        ldr             x12, [sp]
        sub             x2, x2, #3
        movrel          x9, qpel_filters
        add             x9, x9, x12, lsl #3
        ld1r            {v28.2d}, [x9]
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.\elems, w6          // wx
        dup             v31.4s, w10             // shift
        dup             v29.4s, w7              // ox
.endm

function ff_hevc_put_hevc_qpel_uni_w_h4_8_neon, export=1
        QPEL_UNI_W_H_HEADER 4h
        sxtl            v0.8h,   v28.8b
1:
        ld1             {v1.8b, v2.8b}, [x2], x3
        subs            w4,  w4,  #1
        uxtl            v1.8h,   v1.8b
        uxtl            v2.8h,   v2.8b
        ext             v3.16b,  v1.16b,  v2.16b,  #2
        ext             v4.16b,  v1.16b,  v2.16b,  #4
        ext             v5.16b,  v1.16b,  v2.16b,  #6
        ext             v6.16b,  v1.16b,  v2.16b,  #8
        ext             v7.16b,  v1.16b,  v2.16b,  #10
        ext             v16.16b, v1.16b,  v2.16b,  #12
        ext             v17.16b, v1.16b,  v2.16b,  #14
        mul             v18.4h,  v1.4h,   v0.h[0]
        mla             v18.4h,  v3.4h,   v0.h[1]
        mla             v18.4h,  v4.4h,   v0.h[2]
        mla             v18.4h,  v5.4h,   v0.h[3]
        mla             v18.4h,  v6.4h,   v0.h[4]
        mla             v18.4h,  v7.4h,   v0.h[5]
        mla             v18.4h,  v16.4h,  v0.h[6]
        mla             v18.4h,  v17.4h,  v0.h[7]
        smull           v16.4s,  v18.4h,  v30.4h
        sqrshl          v16.4s,  v16.4s,  v31.4s
        sqadd           v16.4s,  v16.4s,  v29.4s
        sqxtn           v16.4h,  v16.4s
        sqxtun          v16.8b,  v16.8h
        str             s16, [x0]
        add             x0,  x0,  x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h6_8_neon, export=1
        QPEL_UNI_W_H_HEADER 8h
        sub             x1,  x1,  #4
        sxtl            v0.8h,   v28.8b
1:
        ld1             {v1.8b, v2.8b}, [x2], x3
        subs            w4,  w4,  #1
        uxtl            v1.8h,   v1.8b
        uxtl            v2.8h,   v2.8b
        ext             v3.16b,  v1.16b,  v2.16b,  #2
        ext             v4.16b,  v1.16b,  v2.16b,  #4
        ext             v5.16b,  v1.16b,  v2.16b,  #6
        ext             v6.16b,  v1.16b,  v2.16b,  #8
        ext             v7.16b,  v1.16b,  v2.16b,  #10
        ext             v16.16b, v1.16b,  v2.16b,  #12
        ext             v17.16b, v1.16b,  v2.16b,  #14
        mul             v18.8h,  v1.8h,   v0.h[0]
        mla             v18.8h,  v3.8h,   v0.h[1]
        mla             v18.8h,  v4.8h,   v0.h[2]
        mla             v18.8h,  v5.8h,   v0.h[3]
        mla             v18.8h,  v6.8h,   v0.h[4]
        mla             v18.8h,  v7.8h,   v0.h[5]
        mla             v18.8h,  v16.8h,  v0.h[6]
        mla             v18.8h,  v17.8h,  v0.h[7]
        smull           v16.4s,  v18.4h,  v30.4h
        smull2          v17.4s,  v18.8h,  v30.8h
        sqrshl          v16.4s,  v16.4s,  v31.4s
        sqrshl          v17.4s,  v17.4s,  v31.4s
        sqadd           v16.4s,  v16.4s,  v29.4s
        sqadd           v17.4s,  v17.4s,  v29.4s
        sqxtn           v16.4h,  v16.4s
        sqxtn2          v16.8h,  v17.4s
        sqxtun          v16.8b,  v16.8h
        str             s16, [x0], #4
        st1             {v16.h}[2], [x0], x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h8_8_neon, export=1
        QPEL_UNI_W_H_HEADER 8h
        sxtl            v0.8h,   v28.8b
1:
        ld1             {v1.8b, v2.8b}, [x2], x3
        subs            w4,  w4,  #1
        uxtl            v1.8h,   v1.8b
        uxtl            v2.8h,   v2.8b
        ext             v3.16b,  v1.16b,  v2.16b,  #2
        ext             v4.16b,  v1.16b,  v2.16b,  #4
        ext             v5.16b,  v1.16b,  v2.16b,  #6
        ext             v6.16b,  v1.16b,  v2.16b,  #8
        ext             v7.16b,  v1.16b,  v2.16b,  #10
        ext             v16.16b, v1.16b,  v2.16b,  #12
        ext             v17.16b, v1.16b,  v2.16b,  #14
        mul             v18.8h,  v1.8h,   v0.h[0]
        mla             v18.8h,  v3.8h,   v0.h[1]
        mla             v18.8h,  v4.8h,   v0.h[2]
        mla             v18.8h,  v5.8h,   v0.h[3]
        mla             v18.8h,  v6.8h,   v0.h[4]
        mla             v18.8h,  v7.8h,   v0.h[5]
        mla             v18.8h,  v16.8h,  v0.h[6]
        mla             v18.8h,  v17.8h,  v0.h[7]
        smull           v16.4s,  v18.4h,  v30.4h
        smull2          v17.4s,  v18.8h,  v30.8h
        sqrshl          v16.4s,  v16.4s,  v31.4s
        sqrshl          v17.4s,  v17.4s,  v31.4s
        sqadd           v16.4s,  v16.4s,  v29.4s
        sqadd           v17.4s,  v17.4s,  v29.4s
        sqxtn           v16.4h,  v16.4s
        sqxtn2          v16.8h,  v17.4s
        sqxtun          v16.8b,  v16.8h
        st1             {v16.8b}, [x0], x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h12_8_neon, export=1
        QPEL_UNI_W_H_HEADER 8h
        add             x13, x0,  #8
        sxtl            v0.8h,   v28.8b
1:
        ld1             {v1.8b, v2.8b, v3.8b}, [x2], x3
        subs            w4,  w4,  #1
        uxtl            v1.8h,   v1.8b
        uxtl            v2.8h,   v2.8b
        uxtl            v3.8h,   v3.8b
        ext             v4.16b,  v1.16b,  v2.16b,  #2
        ext             v5.16b,  v1.16b,  v2.16b,  #4
        ext             v6.16b,  v1.16b,  v2.16b,  #6
        ext             v7.16b,  v1.16b,  v2.16b,  #8
        ext             v16.16b, v1.16b,  v2.16b,  #10
        ext             v17.16b, v1.16b,  v2.16b,  #12
        ext             v18.16b, v1.16b,  v2.16b,  #14
        mul             v19.8h,  v1.8h,   v0.h[0]
        mla             v19.8h,  v4.8h,   v0.h[1]
        mla             v19.8h,  v5.8h,   v0.h[2]
        mla             v19.8h,  v6.8h,   v0.h[3]
        mla             v19.8h,  v7.8h,   v0.h[4]
        mla             v19.8h,  v16.8h,  v0.h[5]
        mla             v19.8h,  v17.8h,  v0.h[6]
        mla             v19.8h,  v18.8h,  v0.h[7]
        ext             v4.16b,  v2.16b,  v3.16b,  #2
        ext             v5.16b,  v2.16b,  v3.16b,  #4
        ext             v6.16b,  v2.16b,  v3.16b,  #6
        ext             v7.16b,  v2.16b,  v3.16b,  #8
        ext             v16.16b, v2.16b,  v3.16b,  #10
        ext             v17.16b, v2.16b,  v3.16b,  #12
        ext             v18.16b, v2.16b,  v3.16b,  #14
        mul             v20.4h,  v2.4h,   v0.h[0]
        mla             v20.4h,  v4.4h,   v0.h[1]
        mla             v20.4h,  v5.4h,   v0.h[2]
        mla             v20.4h,  v6.4h,   v0.h[3]
        mla             v20.4h,  v7.4h,   v0.h[4]
        mla             v20.4h,  v16.4h,  v0.h[5]
        mla             v20.4h,  v17.4h,  v0.h[6]
        mla             v20.4h,  v18.4h,  v0.h[7]
        smull           v16.4s,  v19.4h,  v30.4h
        smull2          v17.4s,  v19.8h,  v30.8h
        smull           v18.4s,  v20.4h,  v30.4h
        sqrshl          v16.4s,  v16.4s,  v31.4s
        sqrshl          v17.4s,  v17.4s,  v31.4s
        sqrshl          v18.4s,  v18.4s,  v31.4s
        sqadd           v16.4s,  v16.4s,  v29.4s
        sqadd           v17.4s,  v17.4s,  v29.4s
        sqadd           v18.4s,  v18.4s,  v29.4s
        sqxtn           v16.4h,  v16.4s
        sqxtn2          v16.8h,  v17.4s
        sqxtn           v17.4h,  v18.4s
        sqxtun          v16.8b,  v16.8h
        sqxtun          v17.8b,  v17.8h
        st1             {v16.8b},   [x0],  x1
        st1             {v17.s}[0], [x13], x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h16_8_neon, export=1
        QPEL_UNI_W_H_HEADER 8h
        sxtl            v0.8h,   v28.8b
1:
        ld1             {v1.8b, v2.8b, v3.8b}, [x2], x3
        subs            w4,  w4,  #1
        uxtl            v1.8h,   v1.8b
        uxtl            v2.8h,   v2.8b
        uxtl            v3.8h,   v3.8b
        ext             v4.16b,  v1.16b,  v2.16b,  #2
        ext             v5.16b,  v1.16b,  v2.16b,  #4
        ext             v6.16b,  v1.16b,  v2.16b,  #6
        ext             v7.16b,  v1.16b,  v2.16b,  #8
        ext             v16.16b, v1.16b,  v2.16b,  #10
        ext             v17.16b, v1.16b,  v2.16b,  #12
        ext             v18.16b, v1.16b,  v2.16b,  #14
        mul             v19.8h,  v1.8h,   v0.h[0]
        mla             v19.8h,  v4.8h,   v0.h[1]
        mla             v19.8h,  v5.8h,   v0.h[2]
        mla             v19.8h,  v6.8h,   v0.h[3]
        mla             v19.8h,  v7.8h,   v0.h[4]
        mla             v19.8h,  v16.8h,  v0.h[5]
        mla             v19.8h,  v17.8h,  v0.h[6]
        mla             v19.8h,  v18.8h,  v0.h[7]
        ext             v4.16b,  v2.16b,  v3.16b,  #2
        ext             v5.16b,  v2.16b,  v3.16b,  #4
        ext             v6.16b,  v2.16b,  v3.16b,  #6
        ext             v7.16b,  v2.16b,  v3.16b,  #8
        ext             v16.16b, v2.16b,  v3.16b,  #10
        ext             v17.16b, v2.16b,  v3.16b,  #12
        ext             v18.16b, v2.16b,  v3.16b,  #14
        mul             v20.8h,  v2.8h,   v0.h[0]
        mla             v20.8h,  v4.8h,   v0.h[1]
        mla             v20.8h,  v5.8h,   v0.h[2]
        mla             v20.8h,  v6.8h,   v0.h[3]
        mla             v20.8h,  v7.8h,   v0.h[4]
        mla             v20.8h,  v16.8h,  v0.h[5]
        mla             v20.8h,  v17.8h,  v0.h[6]
        mla             v20.8h,  v18.8h,  v0.h[7]
        smull           v16.4s,  v19.4h,  v30.4h
        smull2          v17.4s,  v19.8h,  v30.8h
        smull           v18.4s,  v20.4h,  v30.4h
        smull2          v19.4s,  v20.8h,  v30.8h
        sqrshl          v16.4s,  v16.4s,  v31.4s
        sqrshl          v17.4s,  v17.4s,  v31.4s
        sqrshl          v18.4s,  v18.4s,  v31.4s
        sqrshl          v19.4s,  v19.4s,  v31.4s
        sqadd           v16.4s,  v16.4s,  v29.4s
        sqadd           v17.4s,  v17.4s,  v29.4s
        sqadd           v18.4s,  v18.4s,  v29.4s
        sqadd           v19.4s,  v19.4s,  v29.4s
        sqxtn           v16.4h,  v16.4s
        sqxtn2          v16.8h,  v17.4s
        sqxtn           v17.4h,  v18.4s
        sqxtn2          v17.8h,  v19.4s
        sqxtun          v16.8b,  v16.8h
        sqxtun          v17.8b,  v17.8h
        st1             {v16.8b, v17.8b}, [x0], x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h24_8_neon, export=1
        QPEL_UNI_W_H_HEADER 8h
        sxtl            v0.8h,   v28.8b
1:
        ld1             {v1.8b, v2.8b, v3.8b, v4.8b}, [x2], x3
        subs            w4,  w4,  #1
        uxtl            v1.8h,   v1.8b
        uxtl            v2.8h,   v2.8b
        uxtl            v3.8h,   v3.8b
        uxtl            v4.8h,   v4.8b
        ext             v5.16b,  v1.16b,  v2.16b,  #2
        ext             v6.16b,  v1.16b,  v2.16b,  #4
        ext             v7.16b,  v1.16b,  v2.16b,  #6
        ext             v16.16b, v1.16b,  v2.16b,  #8
        ext             v17.16b, v1.16b,  v2.16b,  #10
        ext             v18.16b, v1.16b,  v2.16b,  #12
        ext             v19.16b, v1.16b,  v2.16b,  #14
        mul             v20.8h,  v1.8h,   v0.h[0]
        mla             v20.8h,  v5.8h,   v0.h[1]
        mla             v20.8h,  v6.8h,   v0.h[2]
        mla             v20.8h,  v7.8h,   v0.h[3]
        mla             v20.8h,  v16.8h,  v0.h[4]
        mla             v20.8h,  v17.8h,  v0.h[5]
        mla             v20.8h,  v18.8h,  v0.h[6]
        mla             v20.8h,  v19.8h,  v0.h[7]
        ext             v5.16b,  v2.16b,  v3.16b,  #2
        ext             v6.16b,  v2.16b,  v3.16b,  #4
        ext             v7.16b,  v2.16b,  v3.16b,  #6
        ext             v16.16b, v2.16b,  v3.16b,  #8
        ext             v17.16b, v2.16b,  v3.16b,  #10
        ext             v18.16b, v2.16b,  v3.16b,  #12
        ext             v19.16b, v2.16b,  v3.16b,  #14
        mul             v21.8h,  v2.8h,   v0.h[0]
        mla             v21.8h,  v5.8h,   v0.h[1]
        mla             v21.8h,  v6.8h,   v0.h[2]
        mla             v21.8h,  v7.8h,   v0.h[3]
        mla             v21.8h,  v16.8h,  v0.h[4]
        mla             v21.8h,  v17.8h,  v0.h[5]
        mla             v21.8h,  v18.8h,  v0.h[6]
        mla             v21.8h,  v19.8h,  v0.h[7]
        ext             v5.16b,  v3.16b,  v4.16b,  #2
        ext             v6.16b,  v3.16b,  v4.16b,  #4
        ext             v7.16b,  v3.16b,  v4.16b,  #6
        ext             v16.16b, v3.16b,  v4.16b,  #8
        ext             v17.16b, v3.16b,  v4.16b,  #10
        ext             v18.16b, v3.16b,  v4.16b,  #12
        ext             v19.16b, v3.16b,  v4.16b,  #14
        mul             v22.8h,  v3.8h,   v0.h[0]
        mla             v22.8h,  v5.8h,   v0.h[1]
        mla             v22.8h,  v6.8h,   v0.h[2]
        mla             v22.8h,  v7.8h,   v0.h[3]
        mla             v22.8h,  v16.8h,  v0.h[4]
        mla             v22.8h,  v17.8h,  v0.h[5]
        mla             v22.8h,  v18.8h,  v0.h[6]
        mla             v22.8h,  v19.8h,  v0.h[7]
        smull           v16.4s,  v20.4h,  v30.4h
        smull2          v17.4s,  v20.8h,  v30.8h
        smull           v18.4s,  v21.4h,  v30.4h
        smull2          v19.4s,  v21.8h,  v30.8h
        smull           v20.4s,  v22.4h,  v30.4h
        smull2          v21.4s,  v22.8h,  v30.8h
        sqrshl          v16.4s,  v16.4s,  v31.4s
        sqrshl          v17.4s,  v17.4s,  v31.4s
        sqrshl          v18.4s,  v18.4s,  v31.4s
        sqrshl          v19.4s,  v19.4s,  v31.4s
        sqrshl          v20.4s,  v20.4s,  v31.4s
        sqrshl          v21.4s,  v21.4s,  v31.4s
        sqadd           v16.4s,  v16.4s,  v29.4s
        sqadd           v17.4s,  v17.4s,  v29.4s
        sqadd           v18.4s,  v18.4s,  v29.4s
        sqadd           v19.4s,  v19.4s,  v29.4s
        sqadd           v20.4s,  v20.4s,  v29.4s
        sqadd           v21.4s,  v21.4s,  v29.4s
        sqxtn           v16.4h,  v16.4s
        sqxtn2          v16.8h,  v17.4s
        sqxtn           v17.4h,  v18.4s
        sqxtn2          v17.8h,  v19.4s
        sqxtn           v18.4h,  v20.4s
        sqxtn2          v18.8h,  v21.4s
        sqxtun          v16.8b,  v16.8h
        sqxtun          v17.8b,  v17.8h
        sqxtun          v18.8b,  v18.8h
        st1             {v16.8b, v17.8b, v18.8b}, [x0], x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h32_8_neon, export=1
        QPEL_UNI_W_H_HEADER 8h
        ldr             w10, [sp, #16]        // width
        ld1             {v1.8b}, [x2], #8
        sub             x3,  x3,  w10, uxtw   // decrement src stride
        mov             w11, w10              // original width
        sub             x3,  x3,  #8          // decrement src stride
        sub             x1,  x1,  w10, uxtw   // decrement dst stride
        sxtl            v0.8h,   v28.8b
        uxtl            v1.8h,   v1.8b
1:
        ld1             {v2.8b, v3.8b}, [x2], #16
        subs            w10, w10, #16         // width
        uxtl            v2.8h,   v2.8b
        uxtl            v3.8h,   v3.8b
        ext             v4.16b,  v1.16b,  v2.16b,  #2
        ext             v5.16b,  v1.16b,  v2.16b,  #4
        ext             v6.16b,  v1.16b,  v2.16b,  #6
        ext             v7.16b,  v1.16b,  v2.16b,  #8
        ext             v16.16b, v1.16b,  v2.16b,  #10
        ext             v17.16b, v1.16b,  v2.16b,  #12
        ext             v18.16b, v1.16b,  v2.16b,  #14
        mul             v19.8h,  v1.8h,   v0.h[0]
        mla             v19.8h,  v4.8h,   v0.h[1]
        mla             v19.8h,  v5.8h,   v0.h[2]
        mla             v19.8h,  v6.8h,   v0.h[3]
        mla             v19.8h,  v7.8h,   v0.h[4]
        mla             v19.8h,  v16.8h,  v0.h[5]
        mla             v19.8h,  v17.8h,  v0.h[6]
        mla             v19.8h,  v18.8h,  v0.h[7]
        ext             v4.16b,  v2.16b,  v3.16b,  #2
        ext             v5.16b,  v2.16b,  v3.16b,  #4
        ext             v6.16b,  v2.16b,  v3.16b,  #6
        ext             v7.16b,  v2.16b,  v3.16b,  #8
        ext             v16.16b, v2.16b,  v3.16b,  #10
        ext             v17.16b, v2.16b,  v3.16b,  #12
        ext             v18.16b, v2.16b,  v3.16b,  #14
        mul             v20.8h,  v2.8h,   v0.h[0]
        mla             v20.8h,  v4.8h,   v0.h[1]
        mla             v20.8h,  v5.8h,   v0.h[2]
        mla             v20.8h,  v6.8h,   v0.h[3]
        mla             v20.8h,  v7.8h,   v0.h[4]
        mla             v20.8h,  v16.8h,  v0.h[5]
        mla             v20.8h,  v17.8h,  v0.h[6]
        mla             v20.8h,  v18.8h,  v0.h[7]
        smull           v16.4s,  v19.4h,  v30.4h
        smull2          v17.4s,  v19.8h,  v30.8h
        smull           v18.4s,  v20.4h,  v30.4h
        smull2          v19.4s,  v20.8h,  v30.8h
        sqrshl          v16.4s,  v16.4s,  v31.4s
        sqrshl          v17.4s,  v17.4s,  v31.4s
        sqrshl          v18.4s,  v18.4s,  v31.4s
        sqrshl          v19.4s,  v19.4s,  v31.4s
        sqadd           v16.4s,  v16.4s,  v29.4s
        sqadd           v17.4s,  v17.4s,  v29.4s
        sqadd           v18.4s,  v18.4s,  v29.4s
        sqadd           v19.4s,  v19.4s,  v29.4s
        sqxtn           v16.4h,  v16.4s
        sqxtn2          v16.8h,  v17.4s
        sqxtn           v17.4h,  v18.4s
        sqxtn2          v17.8h,  v19.4s
        sqxtun          v16.8b,  v16.8h
        sqxtun          v17.8b,  v17.8h
        st1             {v16.8b, v17.8b}, [x0], #16
        mov             v1.16b,  v3.16b
        b.gt            1b
        subs            w4,  w4,  #1          // height
        add             x2,  x2,  x3
        b.le            9f
        ld1             {v1.8b}, [x2], #8
        mov             w10, w11
        add             x0,  x0,  x1
        uxtl            v1.8h,   v1.8b
        b               1b
9:
        ret
endfunc

#if HAVE_I8MM
ENABLE_I8MM
function ff_hevc_put_hevc_qpel_uni_w_h4_8_neon_i8mm, export=1
        QPEL_UNI_W_H_HEADER
1:
        ld1             {v0.16b}, [x2], x3
        ext             v1.16b, v0.16b, v0.16b, #1
        ext             v2.16b, v0.16b, v0.16b, #2
        ext             v3.16b, v0.16b, v0.16b, #3
        zip1            v0.2d, v0.2d, v1.2d
        zip1            v2.2d, v2.2d, v3.2d
        movi            v16.16b, #0
        movi            v17.16b, #0
        usdot           v16.4s, v0.16b, v28.16b
        usdot           v17.4s, v2.16b, v28.16b
        addp            v16.4s, v16.4s, v17.4s
        mul             v16.4s, v16.4s, v30.4s
        sqrshl          v16.4s, v16.4s, v31.4s
        sqadd           v16.4s, v16.4s, v29.4s
        sqxtn           v16.4h, v16.4s
        sqxtun          v16.8b, v16.8h
        str             s16, [x0]
        add             x0, x0, x1
        subs            w4, w4, #1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h6_8_neon_i8mm, export=1
        QPEL_UNI_W_H_HEADER
        sub             x1, x1, #4
1:
        ld1             {v0.16b}, [x2], x3
        ext             v1.16b, v0.16b, v0.16b, #1
        ext             v2.16b, v0.16b, v0.16b, #2
        ext             v3.16b, v0.16b, v0.16b, #3
        ext             v4.16b, v0.16b, v0.16b, #4
        ext             v5.16b, v0.16b, v0.16b, #5
        zip1            v0.2d, v0.2d, v1.2d
        zip1            v2.2d, v2.2d, v3.2d
        zip1            v4.2d, v4.2d, v5.2d
        movi            v16.16b, #0
        movi            v17.16b, #0
        movi            v18.16b, #0
        usdot           v16.4s, v0.16b, v28.16b
        usdot           v17.4s, v2.16b, v28.16b
        usdot           v18.4s, v4.16b, v28.16b
        addp            v16.4s, v16.4s, v17.4s
        addp            v18.4s, v18.4s, v18.4s
        mul             v16.4s, v16.4s, v30.4s
        mul             v18.2s, v18.2s, v30.2s
        sqrshl          v16.4s, v16.4s, v31.4s
        sqrshl          v18.2s, v18.2s, v31.2s
        sqadd           v16.4s, v16.4s, v29.4s
        sqadd           v18.2s, v18.2s, v29.2s
        sqxtn           v16.4h, v16.4s
        sqxtn2          v16.8h, v18.4s
        sqxtun          v16.8b, v16.8h
        str             s16, [x0], #4
        st1             {v16.h}[2], [x0], x1
        subs            w4, w4, #1
        b.hi            1b
        ret
endfunc


.macro  QPEL_UNI_W_H_CALC s0, s1, s2, s3, d0, d1, d2, d3
        movi            \d0\().16b, #0
        movi            \d1\().16b, #0
        movi            \d2\().16b, #0
        movi            \d3\().16b, #0
        usdot           \d0\().4s, \s0\().16b, v28.16b
        usdot           \d1\().4s, \s1\().16b, v28.16b
        usdot           \d2\().4s, \s2\().16b, v28.16b
        usdot           \d3\().4s, \s3\().16b, v28.16b
        addp            \d0\().4s, \d0\().4s, \d1\().4s
        addp            \d2\().4s, \d2\().4s, \d3\().4s
        mul             \d0\().4s, \d0\().4s, v30.4s
        mul             \d2\().4s, \d2\().4s, v30.4s
        sqrshl          \d0\().4s, \d0\().4s, v31.4s
        sqrshl          \d2\().4s, \d2\().4s, v31.4s
        sqadd           \d0\().4s, \d0\().4s, v29.4s
        sqadd           \d2\().4s, \d2\().4s, v29.4s
.endm

.macro  QPEL_UNI_W_H_CALC_HALF s0, s1, d0, d1
        movi            \d0\().16b, #0
        movi            \d1\().16b, #0
        usdot           \d0\().4s, \s0\().16b, v28.16b
        usdot           \d1\().4s, \s1\().16b, v28.16b
        addp            \d0\().4s, \d0\().4s, \d1\().4s
        mul             \d0\().4s, \d0\().4s, v30.4s
        sqrshl          \d0\().4s, \d0\().4s, v31.4s
        sqadd           \d0\().4s, \d0\().4s, v29.4s
.endm


function ff_hevc_put_hevc_qpel_uni_w_h8_8_neon_i8mm, export=1
        QPEL_UNI_W_H_HEADER
1:
        ld1             {v16.16b, v17.16b}, [x2], x3
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7
        zip1            v0.2d, v16.2d, v1.2d
        zip1            v2.2d, v2.2d, v3.2d
        zip1            v4.2d, v4.2d, v5.2d
        zip1            v6.2d, v6.2d, v7.2d
        QPEL_UNI_W_H_CALC  v0, v2, v4, v6,  v18, v19, v20, v21
        sqxtn           v18.4h, v18.4s
        sqxtn2          v18.8h, v20.4s
        sqxtun          v18.8b, v18.8h
        str             d18, [x0]
        add             x0, x0, x1
        subs            w4, w4, #1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h12_8_neon_i8mm, export=1
        QPEL_UNI_W_H_HEADER
        add             x13, x0, #8
1:
        ld1             {v16.16b, v17.16b}, [x2], x3
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7
        zip1            v18.2d, v16.2d, v1.2d
        zip1            v19.2d, v2.2d, v3.2d
        zip1            v20.2d, v4.2d, v5.2d
        zip1            v21.2d, v6.2d, v7.2d
        zip2            v22.2d, v16.2d, v1.2d
        zip2            v23.2d, v2.2d, v3.2d
        QPEL_UNI_W_H_CALC  v18, v19, v20, v21, v0, v2, v4, v6
        QPEL_UNI_W_H_CALC_HALF v22, v23, v24, v25
        sqxtn           v0.4h, v0.4s
        sqxtn2          v0.8h, v4.4s
        sqxtn           v1.4h, v24.4s
        sqxtun          v0.8b, v0.8h
        sqxtun          v1.8b, v1.8h

        str             d0, [x0]
        str             s1, [x13]
        add             x0, x0, x1
        add             x13, x13, x1
        subs            w4, w4, #1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h16_8_neon_i8mm, export=1
        QPEL_UNI_W_H_HEADER
1:
        ld1             {v16.16b, v17.16b}, [x2], x3
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7
        QPEL_UNI_W_H_CALC  v16, v2, v1, v3, v18, v19, v20, v21   // v18: 0, 8, 2, 10 v20: 1, 9, 3, 11
        QPEL_UNI_W_H_CALC  v4, v6, v5, v7, v22, v23, v24, v25    // v22: 4, 12, 6, 14 v24: 5, 13, 7, 15
        sqxtn           v0.4h, v18.4s
        sqxtn2          v0.8h, v22.4s
        sqxtn           v1.4h, v20.4s
        sqxtn2          v1.8h, v24.4s
        trn1            v2.8h, v0.8h, v1.8h
        trn2            v3.8h, v0.8h, v1.8h
        sqxtun          v0.8b, v2.8h
        sqxtun2         v0.16b, v3.8h
        st1             {v0.16b}, [x0], x1
        subs            w4, w4, #1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h24_8_neon_i8mm, export=1
        QPEL_UNI_W_H_HEADER
        sub             x1, x1, #16
1:
        ld1             {v16.16b, v17.16b}, [x2], x3
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7
        QPEL_UNI_W_H_CALC  v16, v2, v1, v3, v18, v19, v20, v21
        QPEL_UNI_W_H_CALC  v4, v6, v5, v7, v22, v23, v24, v25
        sqxtn           v18.4h, v18.4s
        sqxtn2          v18.8h, v22.4s
        sqxtn           v19.4h, v20.4s
        sqxtn2          v19.8h, v24.4s
        trn1            v20.8h, v18.8h, v19.8h
        trn2            v21.8h, v18.8h, v19.8h
        sqxtun          v26.8b, v20.8h
        sqxtun2         v26.16b, v21.8h                         // 0-15
        ext             v1.16b, v17.16b, v17.16b, #1
        ext             v2.16b, v17.16b, v17.16b, #2
        ext             v3.16b, v17.16b, v17.16b, #3
        ext             v4.16b, v17.16b, v17.16b, #4
        ext             v5.16b, v17.16b, v17.16b, #5
        ext             v6.16b, v17.16b, v17.16b, #6
        ext             v7.16b, v17.16b, v17.16b, #7
        zip1            v0.2d, v17.2d, v1.2d
        zip1            v2.2d, v2.2d, v3.2d
        zip1            v4.2d, v4.2d, v5.2d
        zip1            v6.2d, v6.2d, v7.2d
        QPEL_UNI_W_H_CALC  v0, v2, v4, v6, v18, v19, v20, v21
        sqxtn           v18.4h, v18.4s
        sqxtn2          v18.8h, v20.4s
        sqxtun          v27.8b, v18.8h

        st1             {v26.16b}, [x0], #16
        st1             {v27.8b}, [x0], x1
        subs            w4, w4, #1
        b.hi            1b
        ret
endfunc


function ff_hevc_put_hevc_qpel_uni_w_h32_8_neon_i8mm, export=1
        QPEL_UNI_W_H_HEADER
1:
        ld1             {v16.16b, v17.16b, v18.16b}, [x2], x3
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7
        QPEL_UNI_W_H_CALC  v16, v2, v1, v3, v0, v19, v20, v21
        QPEL_UNI_W_H_CALC  v4, v6, v5, v7, v22, v23, v24, v25
        sqxtn           v0.4h, v0.4s
        sqxtn2          v0.8h, v22.4s
        sqxtn           v19.4h, v20.4s
        sqxtn2          v19.8h, v24.4s
        trn1            v20.8h, v0.8h, v19.8h
        trn2            v21.8h, v0.8h, v19.8h
        sqxtun          v26.8b, v20.8h
        sqxtun2         v26.16b, v21.8h                         // 0-15
        ext             v1.16b, v17.16b, v18.16b, #1
        ext             v2.16b, v17.16b, v18.16b, #2
        ext             v3.16b, v17.16b, v18.16b, #3
        ext             v4.16b, v17.16b, v18.16b, #4
        ext             v5.16b, v17.16b, v18.16b, #5
        ext             v6.16b, v17.16b, v18.16b, #6
        ext             v7.16b, v17.16b, v18.16b, #7
        QPEL_UNI_W_H_CALC  v17, v2, v1, v3, v0, v19, v20, v21
        QPEL_UNI_W_H_CALC  v4, v6, v5, v7, v22, v23, v24, v25
        sqxtn           v0.4h, v0.4s
        sqxtn2          v0.8h, v22.4s
        sqxtn           v19.4h, v20.4s
        sqxtn2          v19.8h, v24.4s
        trn1            v20.8h, v0.8h, v19.8h
        trn2            v21.8h, v0.8h, v19.8h
        sqxtun          v27.8b, v20.8h
        sqxtun2         v27.16b, v21.8h                         // 16-31
        st1             {v26.16b, v27.16b}, [x0], x1
        subs            w4, w4, #1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h48_8_neon_i8mm, export=1
        QPEL_UNI_W_H_HEADER
1:
        ld1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x2], x3
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7
        QPEL_UNI_W_H_CALC  v16, v2, v1, v3, v20, v24, v21, v0
        QPEL_UNI_W_H_CALC  v4, v6, v5, v7, v22, v24, v23, v0
        sqxtn           v20.4h, v20.4s
        sqxtn2          v20.8h, v22.4s
        sqxtn           v21.4h, v21.4s
        sqxtn2          v21.8h, v23.4s
        trn1            v22.8h, v20.8h, v21.8h
        trn2            v23.8h, v20.8h, v21.8h
        sqxtun          v25.8b, v22.8h
        sqxtun2         v25.16b, v23.8h                         // 0-15
        ext             v1.16b, v17.16b, v18.16b, #1
        ext             v2.16b, v17.16b, v18.16b, #2
        ext             v3.16b, v17.16b, v18.16b, #3
        ext             v4.16b, v17.16b, v18.16b, #4
        ext             v5.16b, v17.16b, v18.16b, #5
        ext             v6.16b, v17.16b, v18.16b, #6
        ext             v7.16b, v17.16b, v18.16b, #7
        QPEL_UNI_W_H_CALC  v17, v2, v1, v3, v20, v24, v21, v0
        QPEL_UNI_W_H_CALC  v4, v6, v5, v7, v22, v24, v23, v0
        sqxtn           v20.4h, v20.4s
        sqxtn2          v20.8h, v22.4s
        sqxtn           v21.4h, v21.4s
        sqxtn2          v21.8h, v23.4s
        trn1            v22.8h, v20.8h, v21.8h
        trn2            v23.8h, v20.8h, v21.8h
        sqxtun          v26.8b, v22.8h
        sqxtun2         v26.16b, v23.8h                         // 16-31
        ext             v1.16b, v18.16b, v19.16b, #1
        ext             v2.16b, v18.16b, v19.16b, #2
        ext             v3.16b, v18.16b, v19.16b, #3
        ext             v4.16b, v18.16b, v19.16b, #4
        ext             v5.16b, v18.16b, v19.16b, #5
        ext             v6.16b, v18.16b, v19.16b, #6
        ext             v7.16b, v18.16b, v19.16b, #7
        QPEL_UNI_W_H_CALC  v18, v2, v1, v3, v20, v24, v21, v0
        QPEL_UNI_W_H_CALC  v4, v6, v5, v7, v22, v24, v23, v0
        sqxtn           v20.4h, v20.4s
        sqxtn2          v20.8h, v22.4s
        sqxtn           v21.4h, v21.4s
        sqxtn2          v21.8h, v23.4s
        trn1            v22.8h, v20.8h, v21.8h
        trn2            v23.8h, v20.8h, v21.8h
        sqxtun          v27.8b, v22.8h
        sqxtun2         v27.16b, v23.8h                         // 32-47
        st1             {v25.16b, v26.16b, v27.16b}, [x0], x1
        subs            w4, w4, #1
        b.hi            1b
        ret
endfunc



function ff_hevc_put_hevc_qpel_uni_w_h64_8_neon_i8mm, export=1
        QPEL_UNI_W_H_HEADER
        sub             x3, x3, #64
1:
        ld1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x2], #64
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7
        QPEL_UNI_W_H_CALC  v16, v2, v1, v3, v20, v24, v21, v0
        QPEL_UNI_W_H_CALC  v4, v6, v5, v7, v22, v24, v23, v0
        sqxtn           v20.4h, v20.4s
        sqxtn2          v20.8h, v22.4s
        sqxtn           v21.4h, v21.4s
        sqxtn2          v21.8h, v23.4s
        trn1            v22.8h, v20.8h, v21.8h
        trn2            v23.8h, v20.8h, v21.8h
        sqxtun          v16.8b, v22.8h
        sqxtun2         v16.16b, v23.8h                         // 0-15
        ext             v1.16b, v17.16b, v18.16b, #1
        ext             v2.16b, v17.16b, v18.16b, #2
        ext             v3.16b, v17.16b, v18.16b, #3
        ext             v4.16b, v17.16b, v18.16b, #4
        ext             v5.16b, v17.16b, v18.16b, #5
        ext             v6.16b, v17.16b, v18.16b, #6
        ext             v7.16b, v17.16b, v18.16b, #7
        QPEL_UNI_W_H_CALC  v17, v2, v1, v3, v20, v24, v21, v0
        QPEL_UNI_W_H_CALC  v4, v6, v5, v7, v22, v24, v23, v0
        sqxtn           v20.4h, v20.4s
        sqxtn2          v20.8h, v22.4s
        sqxtn           v21.4h, v21.4s
        sqxtn2          v21.8h, v23.4s
        trn1            v22.8h, v20.8h, v21.8h
        trn2            v23.8h, v20.8h, v21.8h
        sqxtun          v17.8b, v22.8h
        sqxtun2         v17.16b, v23.8h                         // 16-31
        ext             v1.16b, v18.16b, v19.16b, #1
        ext             v2.16b, v18.16b, v19.16b, #2
        ext             v3.16b, v18.16b, v19.16b, #3
        ext             v4.16b, v18.16b, v19.16b, #4
        ext             v5.16b, v18.16b, v19.16b, #5
        ext             v6.16b, v18.16b, v19.16b, #6
        ext             v7.16b, v18.16b, v19.16b, #7
        QPEL_UNI_W_H_CALC  v18, v2, v1, v3, v20, v24, v21, v0
        QPEL_UNI_W_H_CALC  v4, v6, v5, v7, v22, v24, v23, v0
        ld1             {v0.16b}, [x2], x3
        sqxtn           v20.4h, v20.4s
        sqxtn2          v20.8h, v22.4s
        sqxtn           v21.4h, v21.4s
        sqxtn2          v21.8h, v23.4s
        trn1            v22.8h, v20.8h, v21.8h
        trn2            v23.8h, v20.8h, v21.8h
        sqxtun          v18.8b, v22.8h
        sqxtun2         v18.16b, v23.8h                         // 32-47
        ext             v1.16b, v19.16b, v0.16b, #1
        ext             v2.16b, v19.16b, v0.16b, #2
        ext             v3.16b, v19.16b, v0.16b, #3
        ext             v4.16b, v19.16b, v0.16b, #4
        ext             v5.16b, v19.16b, v0.16b, #5
        ext             v6.16b, v19.16b, v0.16b, #6
        ext             v7.16b, v19.16b, v0.16b, #7
        QPEL_UNI_W_H_CALC  v19, v2, v1, v3, v20, v24, v21, v0
        QPEL_UNI_W_H_CALC  v4, v6, v5, v7, v22, v24, v23, v0
        sqxtn           v20.4h, v20.4s
        sqxtn2          v20.8h, v22.4s
        sqxtn           v21.4h, v21.4s
        sqxtn2          v21.8h, v23.4s
        trn1            v22.8h, v20.8h, v21.8h
        trn2            v23.8h, v20.8h, v21.8h
        sqxtun          v19.8b, v22.8h
        sqxtun2         v19.16b, v23.8h                         // 48-63

        st1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x0], x1
        subs            w4, w4, #1
        b.hi            1b
        ret
endfunc

.macro QPEL_H_HEADER
        movrel          x9, qpel_filters
        add             x9, x9, x4, lsl #3
        ldr             x11, [x9]
        dup             v31.2d, x11
        sub             x1, x1, #3
.endm

.macro VVC_QPEL_H_HEADER
        ld1r            {v31.2d}, [x4]
        sub             x1, x1, #3
.endm

function ff_vvc_put_qpel_h4_8_neon_i8mm, export=1
        VVC_QPEL_H_HEADER
        mov             x10, #VVC_MAX_PB_SIZE * 2
        b               1f
endfunc

function ff_hevc_put_hevc_qpel_h4_8_neon_i8mm, export=1
        QPEL_H_HEADER
        mov             x10, #HEVC_MAX_PB_SIZE * 2
1:
        ld1             {v0.16b}, [x1], x2
        ext             v1.16b, v0.16b, v0.16b, #1
        ext             v2.16b, v0.16b, v0.16b, #2
        ext             v3.16b, v0.16b, v0.16b, #3
        zip1            v0.2d, v0.2d, v1.2d
        zip1            v2.2d, v2.2d, v3.2d
        movi            v16.16b, #0
        movi            v17.16b, #0
        usdot           v16.4s, v0.16b, v31.16b
        usdot           v17.4s, v2.16b, v31.16b
        addp            v16.4s, v16.4s, v17.4s
        sqxtn           v16.4h, v16.4s
        str             d16, [x0]
        add             x0, x0, x10
        subs            w3, w3, #1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_h6_8_neon_i8mm, export=1
        QPEL_H_HEADER
        mov             x10, #HEVC_MAX_PB_SIZE * 2
        add             x15, x0, #8
1:
        ld1             {v0.16b}, [x1], x2
        ext             v1.16b, v0.16b, v0.16b, #1
        ext             v2.16b, v0.16b, v0.16b, #2
        ext             v3.16b, v0.16b, v0.16b, #3
        ext             v4.16b, v0.16b, v0.16b, #4
        ext             v5.16b, v0.16b, v0.16b, #5
        zip1            v0.2d, v0.2d, v1.2d
        zip1            v2.2d, v2.2d, v3.2d
        zip1            v4.2d, v4.2d, v5.2d
        movi            v16.16b, #0
        movi            v17.16b, #0
        movi            v18.16b, #0
        usdot           v16.4s, v0.16b, v31.16b
        usdot           v17.4s, v2.16b, v31.16b
        usdot           v18.4s, v4.16b, v31.16b
        addp            v16.4s, v16.4s, v17.4s
        addp            v18.4s, v18.4s, v18.4s
        sqxtn           v16.4h, v16.4s
        sqxtn           v18.4h, v18.4s
        str             d16, [x0]
        str             s18, [x15]
        add             x0, x0, x10
        add             x15, x15, x10
        subs            w3, w3, #1
        b.ne            1b
        ret
endfunc

function ff_vvc_put_qpel_h8_8_neon_i8mm, export=1
        VVC_QPEL_H_HEADER
        mov             x10, #VVC_MAX_PB_SIZE * 2
        b               1f
endfunc

function ff_hevc_put_hevc_qpel_h8_8_neon_i8mm, export=1
        QPEL_H_HEADER
        mov             x10, #HEVC_MAX_PB_SIZE * 2
1:
        ld1             {v0.16b}, [x1], x2
        ext             v1.16b, v0.16b, v0.16b, #1
        ext             v2.16b, v0.16b, v0.16b, #2
        ext             v3.16b, v0.16b, v0.16b, #3
        ext             v4.16b, v0.16b, v0.16b, #4
        ext             v5.16b, v0.16b, v0.16b, #5
        ext             v6.16b, v0.16b, v0.16b, #6
        ext             v7.16b, v0.16b, v0.16b, #7
        zip1            v0.2d, v0.2d, v1.2d
        zip1            v2.2d, v2.2d, v3.2d
        zip1            v4.2d, v4.2d, v5.2d
        zip1            v6.2d, v6.2d, v7.2d
        movi            v16.16b, #0
        movi            v17.16b, #0
        movi            v18.16b, #0
        movi            v19.16b, #0
        usdot           v16.4s, v0.16b, v31.16b
        usdot           v17.4s, v2.16b, v31.16b
        usdot           v18.4s, v4.16b, v31.16b
        usdot           v19.4s, v6.16b, v31.16b
        addp            v16.4s, v16.4s, v17.4s
        addp            v18.4s, v18.4s, v19.4s
        sqxtn           v16.4h, v16.4s
        sqxtn2          v16.8h, v18.4s
        str             q16, [x0]
        add             x0, x0, x10
        subs            w3, w3, #1
        b.ne            1b
        ret
endfunc

.macro QPEL_H_CALC s0, s1, s2, s3, d0, d1, d2, d3
        movi            \d0\().16b, #0
        movi            \d1\().16b, #0
        movi            \d2\().16b, #0
        movi            \d3\().16b, #0
        usdot           \d0\().4s, \s0\().16b, v31.16b
        usdot           \d1\().4s, \s1\().16b, v31.16b
        usdot           \d2\().4s, \s2\().16b, v31.16b
        usdot           \d3\().4s, \s3\().16b, v31.16b
.endm

function ff_hevc_put_hevc_qpel_h12_8_neon_i8mm, export=1
        QPEL_H_HEADER
        mov             x10, #HEVC_MAX_PB_SIZE * 2
        add             x15, x0, #16
1:
        ld1             {v16.16b, v17.16b}, [x1], x2
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7
        zip1            v18.2d, v4.2d, v5.2d
        zip1            v19.2d, v6.2d, v7.2d
        QPEL_H_CALC     v16, v1, v2, v3, v20, v21, v22, v23
        addp            v20.4s, v20.4s, v22.4s
        addp            v21.4s, v21.4s, v23.4s
        movi            v24.16b, #0
        movi            v25.16b, #0
        usdot           v24.4s, v18.16b, v31.16b
        usdot           v25.4s, v19.16b, v31.16b
        addp            v24.4s, v24.4s, v25.4s
        trn1            v26.4s, v20.4s, v21.4s
        trn2            v27.4s, v20.4s, v21.4s
        sqxtn           v26.4h, v26.4s
        sqxtn           v27.4h, v27.4s
        sqxtn2          v26.8h, v24.4s

        str             q26, [x0]
        str             d27, [x15]
        add             x0, x0, x10
        add             x15, x15, x10
        subs            w3, w3, #1
        b.ne            1b
        ret
endfunc

function ff_vvc_put_qpel_h16_8_neon_i8mm, export=1
        VVC_QPEL_H_HEADER
        mov             x10, #VVC_MAX_PB_SIZE * 2
        b               1f
endfunc

function ff_hevc_put_hevc_qpel_h16_8_neon_i8mm, export=1
        QPEL_H_HEADER
        mov             x10, #HEVC_MAX_PB_SIZE * 2
1:
        ld1             {v16.16b, v17.16b}, [x1], x2
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7

        QPEL_H_CALC     v16, v1, v2, v3, v20, v21, v22, v23
        QPEL_H_CALC     v4, v5, v6, v7, v24, v25, v26, v27

        addp            v20.4s, v20.4s, v22.4s
        addp            v21.4s, v21.4s, v23.4s
        addp            v24.4s, v24.4s, v26.4s
        addp            v25.4s, v25.4s, v27.4s

        trn1            v22.4s, v20.4s, v21.4s
        trn2            v23.4s, v20.4s, v21.4s
        trn1            v26.4s, v24.4s, v25.4s
        trn2            v27.4s, v24.4s, v25.4s

        sqxtn           v18.4h, v22.4s
        sqxtn2          v18.8h, v26.4s
        sqxtn           v19.4h, v23.4s
        sqxtn2          v19.8h, v27.4s

        stp             q18, q19, [x0]
        add             x0, x0, x10
        subs            w3, w3, #1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_h24_8_neon_i8mm, export=1
        QPEL_H_HEADER
        mov             x10, #HEVC_MAX_PB_SIZE * 2
        add             x15, x0, #32
1:
        ld1             {v16.16b, v17.16b}, [x1], x2
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7
        QPEL_H_CALC     v16, v1, v2, v3, v20, v21, v22, v23
        QPEL_H_CALC     v4, v5, v6, v7, v24, v25, v26, v27
        addp            v20.4s, v20.4s, v22.4s
        addp            v21.4s, v21.4s, v23.4s
        addp            v24.4s, v24.4s, v26.4s
        addp            v25.4s, v25.4s, v27.4s
        trn1            v22.4s, v20.4s, v21.4s
        trn2            v23.4s, v20.4s, v21.4s
        trn1            v26.4s, v24.4s, v25.4s
        trn2            v27.4s, v24.4s, v25.4s
        sqxtn           v18.4h, v22.4s
        sqxtn2          v18.8h, v26.4s
        sqxtn           v19.4h, v23.4s
        sqxtn2          v19.8h, v27.4s
        stp             q18, q19, [x0]
        add             x0, x0, x10
        ext             v1.16b, v17.16b, v17.16b, #1
        ext             v2.16b, v17.16b, v17.16b, #2
        ext             v3.16b, v17.16b, v17.16b, #3
        ext             v4.16b, v17.16b, v17.16b, #4
        ext             v5.16b, v17.16b, v17.16b, #5
        ext             v6.16b, v17.16b, v17.16b, #6
        ext             v7.16b, v17.16b, v17.16b, #7
        zip1            v0.2d, v17.2d, v1.2d
        zip1            v2.2d, v2.2d, v3.2d
        zip1            v4.2d, v4.2d, v5.2d
        zip1            v6.2d, v6.2d, v7.2d
        QPEL_H_CALC     v0, v2, v4, v6, v20, v21, v22, v23
        addp            v20.4s, v20.4s, v21.4s
        addp            v22.4s, v22.4s, v23.4s
        sqxtn           v20.4h, v20.4s
        sqxtn2          v20.8h, v22.4s
        str             q20, [x15]
        add             x15, x15, x10
        subs            w3, w3, #1
        b.ne            1b
        ret
endfunc

function ff_vvc_put_qpel_h32_8_neon_i8mm, export=1
        VVC_QPEL_H_HEADER
        mov             x10, #VVC_MAX_PB_SIZE * 2
        add             x15, x0, #32
        b               1f
endfunc

function ff_hevc_put_hevc_qpel_h32_8_neon_i8mm, export=1
        QPEL_H_HEADER
        mov             x10, #HEVC_MAX_PB_SIZE * 2
        add             x15, x0, #32
1:
        ld1             {v16.16b, v17.16b, v18.16b}, [x1], x2
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7
        QPEL_H_CALC     v16, v1, v2, v3, v20, v21, v22, v23
        QPEL_H_CALC     v4, v5, v6, v7, v24, v25, v26, v27
        addp            v20.4s, v20.4s, v22.4s
        addp            v21.4s, v21.4s, v23.4s
        addp            v24.4s, v24.4s, v26.4s
        addp            v25.4s, v25.4s, v27.4s
        trn1            v22.4s, v20.4s, v21.4s
        trn2            v23.4s, v20.4s, v21.4s
        trn1            v26.4s, v24.4s, v25.4s
        trn2            v27.4s, v24.4s, v25.4s
        sqxtn           v20.4h, v22.4s
        sqxtn2          v20.8h, v26.4s
        sqxtn           v21.4h, v23.4s
        sqxtn2          v21.8h, v27.4s
        stp             q20, q21, [x0]
        add             x0, x0, x10
        ext             v1.16b, v17.16b, v18.16b, #1
        ext             v2.16b, v17.16b, v18.16b, #2
        ext             v3.16b, v17.16b, v18.16b, #3
        ext             v4.16b, v17.16b, v18.16b, #4
        ext             v5.16b, v17.16b, v18.16b, #5
        ext             v6.16b, v17.16b, v18.16b, #6
        ext             v7.16b, v17.16b, v18.16b, #7
        QPEL_H_CALC     v17, v1, v2, v3, v20, v21, v22, v23
        QPEL_H_CALC     v4, v5, v6, v7, v24, v25, v26, v27
        addp            v20.4s, v20.4s, v22.4s
        addp            v21.4s, v21.4s, v23.4s
        addp            v24.4s, v24.4s, v26.4s
        addp            v25.4s, v25.4s, v27.4s
        trn1            v22.4s, v20.4s, v21.4s
        trn2            v23.4s, v20.4s, v21.4s
        trn1            v26.4s, v24.4s, v25.4s
        trn2            v27.4s, v24.4s, v25.4s
        sqxtn           v20.4h, v22.4s
        sqxtn2          v20.8h, v26.4s
        sqxtn           v21.4h, v23.4s
        sqxtn2          v21.8h, v27.4s
        stp             q20, q21, [x15]
        add             x15, x15, x10
        subs            w3, w3, #1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_qpel_h48_8_neon_i8mm, export=1
        QPEL_H_HEADER
        mov             x10, #HEVC_MAX_PB_SIZE * 2 - 64
1:
        ld1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x1], x2
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7
        QPEL_H_CALC     v16, v1, v2, v3, v20, v21, v22, v23
        QPEL_H_CALC     v4, v5, v6, v7, v24, v25, v26, v27
        addp            v20.4s, v20.4s, v22.4s
        addp            v21.4s, v21.4s, v23.4s
        addp            v24.4s, v24.4s, v26.4s
        addp            v25.4s, v25.4s, v27.4s
        trn1            v22.4s, v20.4s, v21.4s
        trn2            v23.4s, v20.4s, v21.4s
        trn1            v26.4s, v24.4s, v25.4s
        trn2            v27.4s, v24.4s, v25.4s
        sqxtn           v20.4h, v22.4s
        sqxtn2          v20.8h, v26.4s
        sqxtn           v21.4h, v23.4s
        sqxtn2          v21.8h, v27.4s
        stp             q20, q21, [x0], #32

        ext             v1.16b, v17.16b, v18.16b, #1
        ext             v2.16b, v17.16b, v18.16b, #2
        ext             v3.16b, v17.16b, v18.16b, #3
        ext             v4.16b, v17.16b, v18.16b, #4
        ext             v5.16b, v17.16b, v18.16b, #5
        ext             v6.16b, v17.16b, v18.16b, #6
        ext             v7.16b, v17.16b, v18.16b, #7
        QPEL_H_CALC     v17, v1, v2, v3, v20, v21, v22, v23
        QPEL_H_CALC     v4, v5, v6, v7, v24, v25, v26, v27
        addp            v20.4s, v20.4s, v22.4s
        addp            v21.4s, v21.4s, v23.4s
        addp            v24.4s, v24.4s, v26.4s
        addp            v25.4s, v25.4s, v27.4s
        trn1            v22.4s, v20.4s, v21.4s
        trn2            v23.4s, v20.4s, v21.4s
        trn1            v26.4s, v24.4s, v25.4s
        trn2            v27.4s, v24.4s, v25.4s
        sqxtn           v20.4h, v22.4s
        sqxtn2          v20.8h, v26.4s
        sqxtn           v21.4h, v23.4s
        sqxtn2          v21.8h, v27.4s
        stp             q20, q21, [x0], #32
        ext             v1.16b, v18.16b, v19.16b, #1
        ext             v2.16b, v18.16b, v19.16b, #2
        ext             v3.16b, v18.16b, v19.16b, #3
        ext             v4.16b, v18.16b, v19.16b, #4
        ext             v5.16b, v18.16b, v19.16b, #5
        ext             v6.16b, v18.16b, v19.16b, #6
        ext             v7.16b, v18.16b, v19.16b, #7
        QPEL_H_CALC     v18, v1, v2, v3, v20, v21, v22, v23
        QPEL_H_CALC     v4, v5, v6, v7, v24, v25, v26, v27
        addp            v20.4s, v20.4s, v22.4s
        addp            v21.4s, v21.4s, v23.4s
        addp            v24.4s, v24.4s, v26.4s
        addp            v25.4s, v25.4s, v27.4s
        trn1            v22.4s, v20.4s, v21.4s
        trn2            v23.4s, v20.4s, v21.4s
        trn1            v26.4s, v24.4s, v25.4s
        trn2            v27.4s, v24.4s, v25.4s
        sqxtn           v20.4h, v22.4s
        sqxtn2          v20.8h, v26.4s
        sqxtn           v21.4h, v23.4s
        sqxtn2          v21.8h, v27.4s
        stp             q20, q21, [x0]
        add             x0, x0, x10
        subs            w3, w3, #1
        b.ne            1b
        ret
endfunc

.macro put_qpel_h64_8_neon_i8mm
        ld1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x1], #64
        ext             v1.16b, v16.16b, v17.16b, #1
        ext             v2.16b, v16.16b, v17.16b, #2
        ext             v3.16b, v16.16b, v17.16b, #3
        ext             v4.16b, v16.16b, v17.16b, #4
        ext             v5.16b, v16.16b, v17.16b, #5
        ext             v6.16b, v16.16b, v17.16b, #6
        ext             v7.16b, v16.16b, v17.16b, #7
        QPEL_H_CALC     v16, v1, v2, v3, v20, v21, v22, v23
        QPEL_H_CALC     v4, v5, v6, v7, v24, v25, v26, v27
        addp            v20.4s, v20.4s, v22.4s
        addp            v21.4s, v21.4s, v23.4s
        addp            v24.4s, v24.4s, v26.4s
        addp            v25.4s, v25.4s, v27.4s
        trn1            v22.4s, v20.4s, v21.4s
        trn2            v23.4s, v20.4s, v21.4s
        trn1            v26.4s, v24.4s, v25.4s
        trn2            v27.4s, v24.4s, v25.4s
        sqxtn           v20.4h, v22.4s
        sqxtn2          v20.8h, v26.4s
        sqxtn           v21.4h, v23.4s
        sqxtn2          v21.8h, v27.4s
        stp             q20, q21, [x0], #32

        ext             v1.16b, v17.16b, v18.16b, #1
        ext             v2.16b, v17.16b, v18.16b, #2
        ext             v3.16b, v17.16b, v18.16b, #3
        ext             v4.16b, v17.16b, v18.16b, #4
        ext             v5.16b, v17.16b, v18.16b, #5
        ext             v6.16b, v17.16b, v18.16b, #6
        ext             v7.16b, v17.16b, v18.16b, #7
        QPEL_H_CALC     v17, v1, v2, v3, v20, v21, v22, v23
        QPEL_H_CALC     v4, v5, v6, v7, v24, v25, v26, v27
        addp            v20.4s, v20.4s, v22.4s
        addp            v21.4s, v21.4s, v23.4s
        addp            v24.4s, v24.4s, v26.4s
        addp            v25.4s, v25.4s, v27.4s
        trn1            v22.4s, v20.4s, v21.4s
        trn2            v23.4s, v20.4s, v21.4s
        trn1            v26.4s, v24.4s, v25.4s
        trn2            v27.4s, v24.4s, v25.4s
        sqxtn           v20.4h, v22.4s
        sqxtn2          v20.8h, v26.4s
        sqxtn           v21.4h, v23.4s
        sqxtn2          v21.8h, v27.4s
        stp             q20, q21, [x0], #32
        ext             v1.16b, v18.16b, v19.16b, #1
        ext             v2.16b, v18.16b, v19.16b, #2
        ext             v3.16b, v18.16b, v19.16b, #3
        ext             v4.16b, v18.16b, v19.16b, #4
        ext             v5.16b, v18.16b, v19.16b, #5
        ext             v6.16b, v18.16b, v19.16b, #6
        ext             v7.16b, v18.16b, v19.16b, #7
        QPEL_H_CALC     v18, v1, v2, v3, v20, v21, v22, v23
        QPEL_H_CALC     v4, v5, v6, v7, v24, v25, v26, v27
        addp            v20.4s, v20.4s, v22.4s
        addp            v21.4s, v21.4s, v23.4s
        addp            v24.4s, v24.4s, v26.4s
        addp            v25.4s, v25.4s, v27.4s
        trn1            v22.4s, v20.4s, v21.4s
        trn2            v23.4s, v20.4s, v21.4s
        trn1            v26.4s, v24.4s, v25.4s
        trn2            v27.4s, v24.4s, v25.4s
        sqxtn           v20.4h, v22.4s
        sqxtn2          v20.8h, v26.4s
        sqxtn           v21.4h, v23.4s
        sqxtn2          v21.8h, v27.4s
        stp             q20, q21, [x0], #32
        ld1             {v28.8b}, [x1], x2
        ext             v1.16b, v19.16b, v28.16b, #1
        ext             v2.16b, v19.16b, v28.16b, #2
        ext             v3.16b, v19.16b, v28.16b, #3
        ext             v4.16b, v19.16b, v28.16b, #4
        ext             v5.16b, v19.16b, v28.16b, #5
        ext             v6.16b, v19.16b, v28.16b, #6
        ext             v7.16b, v19.16b, v28.16b, #7
        QPEL_H_CALC     v19, v1, v2, v3, v20, v21, v22, v23
        QPEL_H_CALC     v4, v5, v6, v7, v24, v25, v26, v27
        addp            v20.4s, v20.4s, v22.4s
        addp            v21.4s, v21.4s, v23.4s
        addp            v24.4s, v24.4s, v26.4s
        addp            v25.4s, v25.4s, v27.4s
        trn1            v22.4s, v20.4s, v21.4s
        trn2            v23.4s, v20.4s, v21.4s
        trn1            v26.4s, v24.4s, v25.4s
        trn2            v27.4s, v24.4s, v25.4s
        sqxtn           v20.4h, v22.4s
        sqxtn2          v20.8h, v26.4s
        sqxtn           v21.4h, v23.4s
        sqxtn2          v21.8h, v27.4s
        stp             q20, q21, [x0]
        add             x0, x0, x10
.endm

function ff_vvc_put_qpel_h64_8_neon_i8mm, export=1
        VVC_QPEL_H_HEADER
        mov             x10, #(VVC_MAX_PB_SIZE * 2 - 32 * 3)
        sub             x2, x2, #64
        b               1f
endfunc

function ff_hevc_put_hevc_qpel_h64_8_neon_i8mm, export=1
        QPEL_H_HEADER
        mov             x10, #32
        sub             x2, x2, #64
1:
        put_qpel_h64_8_neon_i8mm
        subs            w3, w3, #1
        b.ne            1b
        ret
endfunc

function ff_vvc_put_qpel_h128_8_neon_i8mm, export=1
        VVC_QPEL_H_HEADER
        sub             x11, x2, #128
        mov             x10, #32
        mov             x2, #0
1:
        put_qpel_h64_8_neon_i8mm
        subs            w3, w3, #1
        put_qpel_h64_8_neon_i8mm
        add             x1, x1, x11
        b.ne            1b
        ret
endfunc

DISABLE_I8MM
#endif

function vvc_put_qpel_hv4_8_end_neon
         vvc_load_qpel_filterh x5
         mov             x7, #(VVC_MAX_PB_SIZE * 2)
         b               1f
endfunc

function hevc_put_hevc_qpel_hv4_8_end_neon
        load_qpel_filterh x5, x4
1:
        ldr             d16, [sp]
        ldr             d17, [sp, x7]
        add             sp, sp, x7, lsl #1
        ldr             d18, [sp]
        ldr             d19, [sp, x7]
        add             sp, sp, x7, lsl #1
        ldr             d20, [sp]
        ldr             d21, [sp, x7]
        add             sp, sp, x7, lsl #1
        ldr             d22, [sp]
        add             sp, sp, x7
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().4h}, [sp], x7
        calc_qpelh      v1, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqshrn
        subs            w3, w3, #1
        st1             {v1.4h}, [x0], x7
.endm
1:      calc_all
.purgem calc
2:      mov             sp, x14
        ret
endfunc

function hevc_put_hevc_qpel_hv6_8_end_neon
        mov             x8, #120
        load_qpel_filterh x5, x4
        ldr             q16, [sp]
        ldr             q17, [sp, x7]
        add             sp, sp, x7, lsl #1
        ldr             q18, [sp]
        ldr             q19, [sp, x7]
        add             sp, sp, x7, lsl #1
        ldr             q20, [sp]
        ldr             q21, [sp, x7]
        add             sp, sp, x7, lsl #1
        ldr             q22, [sp]
        add             sp, sp, x7
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8h}, [sp], x7
        calc_qpelh      v1, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqshrn
        calc_qpelh2     v1, v2, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqshrn2
        st1             {v1.4h}, [x0], #8
        subs            w3, w3, #1
        st1             {v1.s}[2], [x0], x8
.endm
1:      calc_all
.purgem calc
2:      mov             sp, x14
        ret
endfunc

function vvc_put_qpel_hv8_8_end_neon
        vvc_load_qpel_filterh x5
        mov             x7, #(VVC_MAX_PB_SIZE * 2)
        b               1f
endfunc

function hevc_put_hevc_qpel_hv8_8_end_neon
        mov             x7, #128
        load_qpel_filterh x5, x4
1:
        ldr             q16, [sp]
        ldr             q17, [sp, x7]
        add             sp, sp, x7, lsl #1
        ldr             q18, [sp]
        ldr             q19, [sp, x7]
        add             sp, sp, x7, lsl #1
        ldr             q20, [sp]
        ldr             q21, [sp, x7]
        add             sp, sp, x7, lsl #1
        ldr             q22, [sp]
        add             sp, sp, x7
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8h}, [sp], x7
        calc_qpelh      v1, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqshrn
        calc_qpelh2     v1, v2, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sqshrn2
        subs            w3, w3, #1
        st1             {v1.8h}, [x0], x7
.endm
1:      calc_all
.purgem calc
2:      mov             sp, x14
        ret
endfunc

function hevc_put_hevc_qpel_hv12_8_end_neon
        mov             x7, #128
        load_qpel_filterh x5, x4
        mov             x8, #112
        ld1             {v16.8h, v17.8h}, [sp], x7
        ld1             {v18.8h, v19.8h}, [sp], x7
        ld1             {v20.8h, v21.8h}, [sp], x7
        ld1             {v22.8h, v23.8h}, [sp], x7
        ld1             {v24.8h, v25.8h}, [sp], x7
        ld1             {v26.8h, v27.8h}, [sp], x7
        ld1             {v28.8h, v29.8h}, [sp], x7
.macro calc tmp0, tmp1, src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\tmp0\().8h, \tmp1\().8h}, [sp], x7
        calc_qpelh      v1,     \src0,  \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7, sqshrn
        calc_qpelh2     v1, v2, \src0, \src1,  \src2,  \src3,  \src4,  \src5,  \src6,  \src7, sqshrn2
        calc_qpelh      v2,     \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15, sqshrn
        st1             {v1.8h}, [x0], #16
        subs            w3, w3, #1
        st1             {v2.4h}, [x0], x8
.endm
1:      calc_all2
.purgem calc
2:      mov             sp, x14
        ret
endfunc

function vvc_put_qpel_hv16_8_end_neon
        vvc_load_qpel_filterh x5
        mov             x7, #(VVC_MAX_PB_SIZE * 2)
        b               1f
endfunc

function hevc_put_hevc_qpel_hv16_8_end_neon
        mov             x7, #128
        load_qpel_filterh x5, x4
1:
        ld1             {v16.8h, v17.8h}, [sp], x7
        ld1             {v18.8h, v19.8h}, [sp], x7
        ld1             {v20.8h, v21.8h}, [sp], x7
        ld1             {v22.8h, v23.8h}, [sp], x7
        ld1             {v24.8h, v25.8h}, [sp], x7
        ld1             {v26.8h, v27.8h}, [sp], x7
        ld1             {v28.8h, v29.8h}, [sp], x7
.macro calc tmp0, tmp1, src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\tmp0\().8h, \tmp1\().8h}, [sp], x7
        calc_qpelh      v1,     \src0,  \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7, sqshrn
        calc_qpelh2     v1, v2, \src0, \src1,  \src2,  \src3,  \src4,  \src5,  \src6,  \src7, sqshrn2
        calc_qpelh      v2,     \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15, sqshrn
        calc_qpelh2     v2, v3, \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15, sqshrn2
        subs            w3, w3, #1
        st1             {v1.8h, v2.8h}, [x0], x7
.endm
1:      calc_all2
.purgem calc
2:      mov             sp, x14
        ret
endfunc

function vvc_put_qpel_hv32_8_end_neon
        vvc_load_qpel_filterh x5
        mov             x7, #(VVC_MAX_PB_SIZE * 2)
        b               0f
endfunc

function hevc_put_hevc_qpel_hv32_8_end_neon
        mov             x7, #128
        load_qpel_filterh x5, x4
0:      mov             x8, sp          // src
        ld1             {v16.8h, v17.8h}, [x8], x7
        mov             w9, w3          // height
        ld1             {v18.8h, v19.8h}, [x8], x7
        mov             x5, x0          // dst
        ld1             {v20.8h, v21.8h}, [x8], x7
        ld1             {v22.8h, v23.8h}, [x8], x7
        ld1             {v24.8h, v25.8h}, [x8], x7
        ld1             {v26.8h, v27.8h}, [x8], x7
        ld1             {v28.8h, v29.8h}, [x8], x7
.macro calc tmp0, tmp1, src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\tmp0\().8h, \tmp1\().8h}, [x8], x7
        calc_qpelh      v1,     \src0,  \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7, sqshrn
        calc_qpelh2     v1, v2, \src0, \src1,  \src2,  \src3,  \src4,  \src5,  \src6,  \src7, sqshrn2
        calc_qpelh      v2,     \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15, sqshrn
        calc_qpelh2     v2, v3, \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15, sqshrn2
        subs            x9, x9, #1
        st1             {v1.8h, v2.8h}, [x5], x7
.endm
1:      calc_all2
.purgem calc
2:      add             x0, x0, #32
        add             sp, sp, #32
        subs            w6, w6, #16
        b.hi            0b
        mov             sp, x14
        ret
endfunc

.macro qpel_hv suffix
function ff_hevc_put_hevc_qpel_hv4_8_\suffix, export=1
        add             w10, w3, #8
        mov             x7, #128
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x5,  x30, [sp, #-48]!
        stp             x0,  x3,  [sp, #16]
        str             x14,      [sp, #32]
        add             x0, sp, #48
        sub             x1, x1, x2, lsl #1
        add             x3, x3, #7
        sub             x1, x1, x2
        bl              X(ff_hevc_put_hevc_qpel_h4_8_\suffix)
        ldr             x14,      [sp, #32]
        ldp             x0,  x3,  [sp, #16]
        ldp             x5,  x30, [sp], #48
        b               hevc_put_hevc_qpel_hv4_8_end_neon
endfunc

function ff_vvc_put_qpel_hv4_8_\suffix, export=1
        add             w10, w3, #8
        lsl             x10, x10, #8
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x5,  x30, [sp, #-48]!
        stp             x0,  x3,  [sp, #16]
        str             x14,      [sp, #32]
        add             x0, sp, #48
        sub             x1, x1, x2, lsl #1
        add             x3, x3, #7
        sub             x1, x1, x2
        bl              X(ff_vvc_put_qpel_h4_8_\suffix)
        ldr             x14,      [sp, #32]
        ldp             x0,  x3,  [sp, #16]
        ldp             x5,  x30, [sp], #48
        b               vvc_put_qpel_hv4_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_hv6_8_\suffix, export=1
        add             w10, w3, #8
        mov             x7, #128
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x5,  x30, [sp, #-48]!
        stp             x0,  x3,  [sp, #16]
        str             x14,      [sp, #32]
        add             x0, sp, #48
        sub             x1, x1, x2, lsl #1
        add             x3, x3, #7
        sub             x1, x1, x2
        bl              X(ff_hevc_put_hevc_qpel_h6_8_\suffix)
        ldr             x14,      [sp, #32]
        ldp             x0,  x3,  [sp, #16]
        ldp             x5,  x30, [sp], #48
        b               hevc_put_hevc_qpel_hv6_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_hv8_8_\suffix, export=1
        add             w10, w3, #8
        lsl             x10, x10, #7
        sub             x1, x1, x2, lsl #1
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x5,  x30, [sp, #-48]!
        stp             x0,  x3,  [sp, #16]
        str             x14,      [sp, #32]
        add             x0, sp, #48
        add             x3, x3, #7
        sub             x1, x1, x2
        bl              X(ff_hevc_put_hevc_qpel_h8_8_\suffix)
        ldr             x14,      [sp, #32]
        ldp             x0,  x3,  [sp, #16]
        ldp             x5,  x30, [sp], #48
        b               hevc_put_hevc_qpel_hv8_8_end_neon
endfunc

function ff_vvc_put_qpel_hv8_8_\suffix, export=1
        add             w10, w3, #8
        lsl             x10, x10, #8
        sub             x1, x1, x2, lsl #1
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x5,  x30, [sp, #-48]!
        stp             x0,  x3,  [sp, #16]
        str             x14,      [sp, #32]
        add             x0, sp, #48
        add             x3, x3, #7
        sub             x1, x1, x2
        bl              X(ff_vvc_put_qpel_h8_8_\suffix)
        ldr             x14,      [sp, #32]
        ldp             x0,  x3,  [sp, #16]
        ldp             x5,  x30, [sp], #48
        b               vvc_put_qpel_hv8_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_hv12_8_\suffix, export=1
        add             w10, w3, #8
        lsl             x10, x10, #7
        sub             x1, x1, x2, lsl #1
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x5,  x30, [sp, #-48]!
        stp             x0,  x3,  [sp, #16]
        str             x14,      [sp, #32]
        add             x0, sp, #48
        add             x3, x3, #7
        sub             x1, x1, x2
        mov             w6, #12
        bl              X(ff_hevc_put_hevc_qpel_h12_8_\suffix)
        ldr             x14,      [sp, #32]
        ldp             x0,  x3,  [sp, #16]
        ldp             x5,  x30, [sp], #48
        b               hevc_put_hevc_qpel_hv12_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_hv16_8_\suffix, export=1
        add             w10, w3, #8
        lsl             x10, x10, #7
        sub             x1, x1, x2, lsl #1
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x5,  x30, [sp, #-48]!
        stp             x0,  x3,  [sp, #16]
        str             x14,      [sp, #32]
        add             x3, x3, #7
        add             x0, sp, #48
        sub             x1, x1, x2
        bl              X(ff_hevc_put_hevc_qpel_h16_8_\suffix)
        ldr             x14,      [sp, #32]
        ldp             x0,  x3,  [sp, #16]
        ldp             x5,  x30, [sp], #48
        b               hevc_put_hevc_qpel_hv16_8_end_neon
endfunc

function ff_vvc_put_qpel_hv16_8_\suffix, export=1
        add             w10, w3, #8
        lsl             x10, x10, #8
        sub             x1, x1, x2, lsl #1
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x5,  x30, [sp, #-48]!
        stp             x0,  x3,  [sp, #16]
        str             x14,      [sp, #32]
        add             x3, x3, #7
        add             x0, sp, #48
        sub             x1, x1, x2
        bl              X(ff_vvc_put_qpel_h16_8_\suffix)
        ldr             x14,      [sp, #32]
        ldp             x0,  x3,  [sp, #16]
        ldp             x5,  x30, [sp], #48
        b               vvc_put_qpel_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_hv24_8_\suffix, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x30, [sp, #48]
        bl              X(ff_hevc_put_hevc_qpel_hv12_8_\suffix)
        ldp             x0, x1, [sp, #32]
        ldp             x2, x3, [sp, #16]
        ldp             x4, x5, [sp], #48
        add             x1, x1, #12
        add             x0, x0, #24
        bl              X(ff_hevc_put_hevc_qpel_hv12_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

function ff_hevc_put_hevc_qpel_hv32_8_\suffix, export=1
        add             w10, w3, #8
        sub             x1, x1, x2, lsl #1
        lsl             x10, x10, #7
        sub             x1, x1, x2
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x5,  x30, [sp, #-48]!
        stp             x0,  x3,  [sp, #16]
        str             x14,      [sp, #32]
        add             x3, x3, #7
        add             x0, sp, #48
        mov             w6, #32
        bl              X(ff_hevc_put_hevc_qpel_h32_8_\suffix)
        ldr             x14,      [sp, #32]
        ldp             x0,  x3,  [sp, #16]
        ldp             x5,  x30, [sp], #48
        b               hevc_put_hevc_qpel_hv32_8_end_neon
endfunc

function ff_vvc_put_qpel_hv32_8_\suffix, export=1
        add             w10, w3, #8
        sub             x1, x1, x2, lsl #1
        lsl             x10, x10, #8
        sub             x1, x1, x2
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x5,  x30, [sp, #-48]!
        stp             x0,  x3,  [sp, #16]
        str             x14,      [sp, #32]
        add             x3, x3, #7
        add             x0, sp, #48
        mov             w6, #32
        bl              X(ff_vvc_put_qpel_h32_8_\suffix)
        ldr             x14,      [sp, #32]
        ldp             x0,  x3,  [sp, #16]
        ldp             x5,  x30, [sp], #48
        b               vvc_put_qpel_hv32_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_hv48_8_\suffix, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x30, [sp, #48]
        bl              X(ff_hevc_put_hevc_qpel_hv24_8_\suffix)
        ldp             x0, x1, [sp, #32]
        ldp             x2, x3, [sp, #16]
        ldp             x4, x5, [sp], #48
        add             x1, x1, #24
        add             x0, x0, #48
        bl              X(ff_hevc_put_hevc_qpel_hv24_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

function ff_hevc_put_hevc_qpel_hv64_8_\suffix, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x30, [sp, #48]
        mov             x6, #32
        bl              X(ff_hevc_put_hevc_qpel_hv32_8_\suffix)
        ldp             x0, x1, [sp, #32]
        ldp             x2, x3, [sp, #16]
        ldp             x4, x5, [sp], #48
        add             x1, x1, #32
        add             x0, x0, #64
        mov             x6, #32
        bl              X(ff_hevc_put_hevc_qpel_hv32_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

function ff_vvc_put_qpel_hv64_8_\suffix, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x30, [sp, #48]
        mov             x6, #32
        bl              X(ff_vvc_put_qpel_hv32_8_\suffix)
        ldp             x0, x1, [sp, #32]
        ldp             x2, x3, [sp, #16]
        ldp             x4, x5, [sp], #48
        add             x1, x1, #32
        add             x0, x0, #64
        mov             x6, #32
        bl              X(ff_vvc_put_qpel_hv32_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

function ff_vvc_put_qpel_hv128_8_\suffix, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x30, [sp, #48]
        mov             x6, #64
        bl              X(ff_vvc_put_qpel_hv64_8_\suffix)
        ldp             x0, x1, [sp, #32]
        ldp             x2, x3, [sp, #16]
        ldp             x4, x5, [sp], #48
        add             x1, x1, #64
        add             x0, x0, #128
        mov             x6, #64
        bl              X(ff_vvc_put_qpel_hv64_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

.endm

qpel_hv neon

#if HAVE_I8MM
ENABLE_I8MM

qpel_hv neon_i8mm

DISABLE_I8MM
#endif

.macro QPEL_UNI_W_HV_HEADER width, suffix
        ldp             x14, x15, [sp]          // mx, my
        ldr             w13, [sp, #16]          // width
        stp             x19, x30, [sp, #-80]!
        stp             x20, x21, [sp, #16]
        stp             x22, x23, [sp, #32]
        stp             x24, x25, [sp, #48]
        stp             x26, x27, [sp, #64]
        mov             x19, sp
        mov             x11, #(HEVC_MAX_PB_SIZE*(HEVC_MAX_PB_SIZE+8)*2)
        sub             sp, sp, x11
        mov             x20, x0
        mov             x21, x1
        mov             x0, sp
        sub             x1, x2, x3, lsl #1
        sub             x1, x1, x3
        mov             x2, x3
        add             w3, w4, #7
        mov             w22, w4                 // height
        mov             x4, x14                 // mx
        mov             x23, x15                // my
        mov             w24, w6                 // wx
        mov             w25, w7                 // ox
        mov             w26, #-6
        sub             w26, w26, w5            // -shift
        mov             w27, w13                // width
.ifc \suffix, neon
.if \width >= 32
        mov             w6,  #\width
        bl              X(ff_hevc_put_hevc_qpel_h32_8_neon)
.else
        bl              X(ff_hevc_put_hevc_qpel_h\width\()_8_\suffix)
.endif
.else
        bl              X(ff_hevc_put_hevc_qpel_h\width\()_8_\suffix)
.endif
        movrel          x9, qpel_filters
        add             x9, x9, x23, lsl #3
        ld1             {v0.8b}, [x9]
        sxtl            v0.8h, v0.8b
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        dup             v28.4s, w24
        dup             v29.4s, w25
        dup             v30.4s, w26
.endm

.macro QPEL_UNI_W_HV_END
        mov             sp, x19
        ldp             x20, x21, [sp, #16]
        ldp             x22, x23, [sp, #32]
        ldp             x24, x25, [sp, #48]
        ldp             x26, x27, [sp, #64]
        ldp             x19, x30, [sp], #80
.endm

.macro QPEL_UNI_W_HV_4
        sshr            v26.4s, v26.4s, #6
        mul             v24.4s, v26.4s, v28.4s
        sqrshl          v24.4s, v24.4s, v30.4s
        sqadd           v24.4s, v24.4s, v29.4s
        sqxtn           v24.4h, v24.4s
        sqxtun          v24.8b, v24.8h
        st1             {v24.s}[0], [x20], x21
.endm

.macro QPEL_FILTER_H    dst, src0, src1, src2, src3, src4, src5, src6, src7
        smull           \dst\().4s, \src0\().4h, v0.h[0]
        smlal           \dst\().4s, \src1\().4h, v0.h[1]
        smlal           \dst\().4s, \src2\().4h, v0.h[2]
        smlal           \dst\().4s, \src3\().4h, v0.h[3]
        smlal           \dst\().4s, \src4\().4h, v0.h[4]
        smlal           \dst\().4s, \src5\().4h, v0.h[5]
        smlal           \dst\().4s, \src6\().4h, v0.h[6]
        smlal           \dst\().4s, \src7\().4h, v0.h[7]
.endm

.macro QPEL_FILTER_H2    dst, src0, src1, src2, src3, src4, src5, src6, src7
        smull2          \dst\().4s, \src0\().8h, v0.h[0]
        smlal2          \dst\().4s, \src1\().8h, v0.h[1]
        smlal2          \dst\().4s, \src2\().8h, v0.h[2]
        smlal2          \dst\().4s, \src3\().8h, v0.h[3]
        smlal2          \dst\().4s, \src4\().8h, v0.h[4]
        smlal2          \dst\().4s, \src5\().8h, v0.h[5]
        smlal2          \dst\().4s, \src6\().8h, v0.h[6]
        smlal2          \dst\().4s, \src7\().8h, v0.h[7]
.endm

function hevc_put_hevc_qpel_uni_w_hv4_8_end_neon
        ldr             d16, [sp]
        ldr             d17, [sp, x10]
        add             sp, sp, x10, lsl #1
        ldr             d18, [sp]
        ldr             d19, [sp, x10]
        add             sp, sp, x10, lsl #1
        ldr             d20, [sp]
        ldr             d21, [sp, x10]
        add             sp, sp, x10, lsl #1
        ldr             d22, [sp]
        add             sp, sp, x10
1:
        ldr             d23, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v16, v17, v18, v19, v20, v21, v22, v23
        QPEL_UNI_W_HV_4
        subs            w22, w22, #1
        b.eq            2f

        ldr             d16, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v17, v18, v19, v20, v21, v22, v23, v16
        QPEL_UNI_W_HV_4
        subs            w22, w22, #1
        b.eq            2f

        ldr             d17, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v18, v19, v20, v21, v22, v23, v16, v17
        QPEL_UNI_W_HV_4
        subs            w22, w22, #1
        b.eq            2f

        ldr             d18, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v19, v20, v21, v22, v23, v16, v17, v18
        QPEL_UNI_W_HV_4
        subs            w22, w22, #1
        b.eq            2f

        ldr             d19, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v20, v21, v22, v23, v16, v17, v18, v19
        QPEL_UNI_W_HV_4
        subs            w22, w22, #1
        b.eq            2f

        ldr             d20, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v21, v22, v23, v16, v17, v18, v19, v20
        QPEL_UNI_W_HV_4
        subs            w22, w22, #1
        b.eq            2f

        ldr             d21, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v22, v23, v16, v17, v18, v19, v20, v21
        QPEL_UNI_W_HV_4
        subs            w22, w22, #1
        b.eq            2f

        ldr             d22, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v23, v16, v17, v18, v19, v20, v21, v22
        QPEL_UNI_W_HV_4
        subs            w22, w22, #1
        b.hi            1b

2:
        QPEL_UNI_W_HV_END
        ret
endfunc

.macro QPEL_UNI_W_HV_8
        sshr            v26.4s, v26.4s, #6
        sshr            v27.4s, v27.4s, #6
        mul             v24.4s, v26.4s, v28.4s
        mul             v25.4s, v27.4s, v28.4s
        sqrshl          v24.4s, v24.4s, v30.4s
        sqrshl          v25.4s, v25.4s, v30.4s
        sqadd           v24.4s, v24.4s, v29.4s
        sqadd           v25.4s, v25.4s, v29.4s
        sqxtn           v24.4h, v24.4s
        sqxtn2          v24.8h, v25.4s
        sqxtun          v24.8b, v24.8h
        st1             {v24.d}[0], [x20], x21
.endm

function hevc_put_hevc_qpel_uni_w_hv8_8_end_neon
        ldr             q16, [sp]
        ldr             q17, [sp, x10]
        add             sp, sp, x10, lsl #1
        ldr             q18, [sp]
        ldr             q19, [sp, x10]
        add             sp, sp, x10, lsl #1
        ldr             q20, [sp]
        ldr             q21, [sp, x10]
        add             sp, sp, x10, lsl #1
        ldr             q22, [sp]
        add             sp, sp, x10
1:
        ldr             q23, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v16, v17, v18, v19, v20, v21, v22, v23
        QPEL_FILTER_H2  v27, v16, v17, v18, v19, v20, v21, v22, v23
        QPEL_UNI_W_HV_8
        subs            w22, w22, #1
        b.eq            2f

        ldr             q16, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v17, v18, v19, v20, v21, v22, v23, v16
        QPEL_FILTER_H2  v27, v17, v18, v19, v20, v21, v22, v23, v16
        QPEL_UNI_W_HV_8
        subs            w22, w22, #1
        b.eq            2f

        ldr             q17, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v18, v19, v20, v21, v22, v23, v16, v17
        QPEL_FILTER_H2  v27, v18, v19, v20, v21, v22, v23, v16, v17
        QPEL_UNI_W_HV_8
        subs            w22, w22, #1
        b.eq            2f

        ldr             q18, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v19, v20, v21, v22, v23, v16, v17, v18
        QPEL_FILTER_H2  v27, v19, v20, v21, v22, v23, v16, v17, v18
        QPEL_UNI_W_HV_8
        subs            w22, w22, #1
        b.eq            2f

        ldr             q19, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v20, v21, v22, v23, v16, v17, v18, v19
        QPEL_FILTER_H2  v27, v20, v21, v22, v23, v16, v17, v18, v19
        QPEL_UNI_W_HV_8
        subs            w22, w22, #1
        b.eq            2f

        ldr             q20, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v21, v22, v23, v16, v17, v18, v19, v20
        QPEL_FILTER_H2  v27, v21, v22, v23, v16, v17, v18, v19, v20
        QPEL_UNI_W_HV_8
        subs            w22, w22, #1
        b.eq            2f

        ldr             q21, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v22, v23, v16, v17, v18, v19, v20, v21
        QPEL_FILTER_H2  v27, v22, v23, v16, v17, v18, v19, v20, v21
        QPEL_UNI_W_HV_8
        subs            w22, w22, #1
        b.eq            2f

        ldr             q22, [sp]
        add             sp, sp, x10
        QPEL_FILTER_H   v26, v23, v16, v17, v18, v19, v20, v21, v22
        QPEL_FILTER_H2  v27, v23, v16, v17, v18, v19, v20, v21, v22
        QPEL_UNI_W_HV_8
        subs            w22, w22, #1
        b.hi            1b

2:
        QPEL_UNI_W_HV_END
        ret
endfunc

.macro QPEL_UNI_W_HV_16
        sshr            v24.4s, v24.4s, #6
        sshr            v25.4s, v25.4s, #6
        sshr            v26.4s, v26.4s, #6
        sshr            v27.4s, v27.4s, #6
        mul             v24.4s, v24.4s, v28.4s
        mul             v25.4s, v25.4s, v28.4s
        mul             v26.4s, v26.4s, v28.4s
        mul             v27.4s, v27.4s, v28.4s
        sqrshl          v24.4s, v24.4s, v30.4s
        sqrshl          v25.4s, v25.4s, v30.4s
        sqrshl          v26.4s, v26.4s, v30.4s
        sqrshl          v27.4s, v27.4s, v30.4s
        sqadd           v24.4s, v24.4s, v29.4s
        sqadd           v25.4s, v25.4s, v29.4s
        sqadd           v26.4s, v26.4s, v29.4s
        sqadd           v27.4s, v27.4s, v29.4s
        sqxtn           v24.4h, v24.4s
        sqxtn2          v24.8h, v25.4s
        sqxtn           v26.4h, v26.4s
        sqxtn2          v26.8h, v27.4s
        sqxtun          v24.8b, v24.8h
        sqxtun2         v24.16b, v26.8h

        st1             {v24.16b}, [x20], x21
.endm

function hevc_put_hevc_qpel_uni_w_hv16_8_end_neon
        mov             x11, sp
        mov             w12, w22
        mov             x13, x20
        mov             x14, sp
3:
        ldp             q16, q1, [x11]
        add             x11, x11, x10
        ldp             q17, q2, [x11]
        add             x11, x11, x10
        ldp             q18, q3, [x11]
        add             x11, x11, x10
        ldp             q19, q4, [x11]
        add             x11, x11, x10
        ldp             q20, q5, [x11]
        add             x11, x11, x10
        ldp             q21, q6, [x11]
        add             x11, x11, x10
        ldp             q22, q7, [x11]
        add             x11, x11, x10
1:
        ldp             q23, q31, [x11]
        add             x11, x11, x10
        QPEL_FILTER_H   v24, v16, v17, v18, v19, v20, v21, v22, v23
        QPEL_FILTER_H2  v25, v16, v17, v18, v19, v20, v21, v22, v23
        QPEL_FILTER_H   v26,  v1,  v2,  v3,  v4,  v5,  v6,  v7, v31
        QPEL_FILTER_H2  v27,  v1,  v2,  v3,  v4,  v5,  v6,  v7, v31
        QPEL_UNI_W_HV_16
        subs            w22, w22, #1
        b.eq            2f

        ldp             q16, q1, [x11]
        add             x11, x11, x10
        QPEL_FILTER_H   v24, v17, v18, v19, v20, v21, v22, v23, v16
        QPEL_FILTER_H2  v25, v17, v18, v19, v20, v21, v22, v23, v16
        QPEL_FILTER_H   v26,  v2,  v3,  v4,  v5,  v6,  v7, v31,  v1
        QPEL_FILTER_H2  v27,  v2,  v3,  v4,  v5,  v6,  v7, v31,  v1
        QPEL_UNI_W_HV_16
        subs            w22, w22, #1
        b.eq            2f

        ldp             q17, q2, [x11]
        add             x11, x11, x10
        QPEL_FILTER_H   v24, v18, v19, v20, v21, v22, v23, v16, v17
        QPEL_FILTER_H2  v25, v18, v19, v20, v21, v22, v23, v16, v17
        QPEL_FILTER_H   v26,  v3,  v4,  v5,  v6,  v7, v31,  v1,  v2
        QPEL_FILTER_H2  v27,  v3,  v4,  v5,  v6,  v7, v31,  v1,  v2
        QPEL_UNI_W_HV_16
        subs            w22, w22, #1
        b.eq            2f

        ldp             q18, q3, [x11]
        add             x11, x11, x10
        QPEL_FILTER_H   v24, v19, v20, v21, v22, v23, v16, v17, v18
        QPEL_FILTER_H2  v25, v19, v20, v21, v22, v23, v16, v17, v18
        QPEL_FILTER_H   v26,  v4,  v5,  v6,  v7, v31,  v1,  v2,  v3
        QPEL_FILTER_H2  v27,  v4,  v5,  v6,  v7, v31,  v1,  v2,  v3
        QPEL_UNI_W_HV_16
        subs            w22, w22, #1
        b.eq            2f

        ldp             q19, q4, [x11]
        add             x11, x11, x10
        QPEL_FILTER_H   v24, v20, v21, v22, v23, v16, v17, v18, v19
        QPEL_FILTER_H2  v25, v20, v21, v22, v23, v16, v17, v18, v19
        QPEL_FILTER_H   v26,  v5,  v6,  v7, v31,  v1,  v2,  v3,  v4
        QPEL_FILTER_H2  v27,  v5,  v6,  v7, v31,  v1,  v2,  v3,  v4
        QPEL_UNI_W_HV_16
        subs            w22, w22, #1
        b.eq            2f

        ldp             q20, q5, [x11]
        add             x11, x11, x10
        QPEL_FILTER_H   v24, v21, v22, v23, v16, v17, v18, v19, v20
        QPEL_FILTER_H2  v25, v21, v22, v23, v16, v17, v18, v19, v20
        QPEL_FILTER_H   v26,  v6,  v7, v31,  v1,  v2,  v3,  v4,  v5
        QPEL_FILTER_H2  v27,  v6,  v7, v31,  v1,  v2,  v3,  v4,  v5
        QPEL_UNI_W_HV_16
        subs            w22, w22, #1
        b.eq            2f

        ldp             q21, q6, [x11]
        add             x11, x11, x10
        QPEL_FILTER_H   v24, v22, v23, v16, v17, v18, v19, v20, v21
        QPEL_FILTER_H2  v25, v22, v23, v16, v17, v18, v19, v20, v21
        QPEL_FILTER_H   v26,  v7, v31,  v1,  v2,  v3,  v4,  v5,  v6
        QPEL_FILTER_H2  v27,  v7, v31,  v1,  v2,  v3,  v4,  v5,  v6
        QPEL_UNI_W_HV_16
        subs            w22, w22, #1
        b.eq            2f

        ldp             q22, q7, [x11]
        add             x11, x11, x10
        QPEL_FILTER_H   v24, v23, v16, v17, v18, v19, v20, v21, v22
        QPEL_FILTER_H2  v25, v23, v16, v17, v18, v19, v20, v21, v22
        QPEL_FILTER_H   v26, v31,  v1,  v2,  v3,  v4,  v5,  v6,  v7
        QPEL_FILTER_H2  v27, v31,  v1,  v2,  v3,  v4,  v5,  v6,  v7
        QPEL_UNI_W_HV_16
        subs            w22, w22, #1
        b.hi            1b
2:
        subs            w27, w27, #16
        add             x11, x14, #32
        add             x20, x13, #16
        mov             w22, w12
        mov             x14, x11
        mov             x13, x20
        b.hi            3b
        QPEL_UNI_W_HV_END
        ret
endfunc

.macro qpel_uni_w_hv suffix
function ff_hevc_put_hevc_qpel_uni_w_hv4_8_\suffix, export=1
        QPEL_UNI_W_HV_HEADER 4, \suffix
        b               hevc_put_hevc_qpel_uni_w_hv4_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_uni_w_hv8_8_\suffix, export=1
        QPEL_UNI_W_HV_HEADER 8, \suffix
        b               hevc_put_hevc_qpel_uni_w_hv8_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_uni_w_hv16_8_\suffix, export=1
        QPEL_UNI_W_HV_HEADER 16, \suffix
        b               hevc_put_hevc_qpel_uni_w_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_uni_w_hv32_8_\suffix, export=1
        QPEL_UNI_W_HV_HEADER 32, \suffix
        b               hevc_put_hevc_qpel_uni_w_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_uni_w_hv64_8_\suffix, export=1
        QPEL_UNI_W_HV_HEADER 64, \suffix
        b               hevc_put_hevc_qpel_uni_w_hv16_8_end_neon
endfunc
.endm

qpel_uni_w_hv neon

function hevc_put_hevc_qpel_bi_hv4_8_end_neon
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        load_qpel_filterh x7, x6
        ld1             {v16.4h}, [sp], x9
        ld1             {v17.4h}, [sp], x9
        ld1             {v18.4h}, [sp], x9
        ld1             {v19.4h}, [sp], x9
        ld1             {v20.4h}, [sp], x9
        ld1             {v21.4h}, [sp], x9
        ld1             {v22.4h}, [sp], x9
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().4h}, [sp], x9
        calc_qpelh      v1, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sshr
        ld1             {v5.4h}, [x4], x9 // src2
        saddw           v1.4s, v1.4s, v5.4h
        rshrn           v1.4h, v1.4s, #7
        sqxtun          v1.8b, v1.8h
        subs            w5, w5, #1
        st1             {v1.s}[0], [x0], x1
.endm
1:      calc_all
.purgem calc
2:      mov             sp, x14
        ret
endfunc

function hevc_put_hevc_qpel_bi_hv6_8_end_neon
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        load_qpel_filterh x7, x6
        sub             x1, x1, #4
        ld1             {v16.8h}, [sp], x9
        ld1             {v17.8h}, [sp], x9
        ld1             {v18.8h}, [sp], x9
        ld1             {v19.8h}, [sp], x9
        ld1             {v20.8h}, [sp], x9
        ld1             {v21.8h}, [sp], x9
        ld1             {v22.8h}, [sp], x9
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8h}, [sp], x9
        calc_qpelh      v1, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sshr
        calc_qpelh2     v2, v2, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sshr
        ld1             {v5.8h}, [x4], x9 // src2
        saddw           v1.4s, v1.4s, v5.4h
        saddw2          v2.4s, v2.4s, v5.8h
        rshrn           v1.4h, v1.4s, #7
        rshrn2          v1.8h, v2.4s, #7
        sqxtun          v1.8b, v1.8h
        st1             {v1.s}[0], [x0], #4
        subs            w5, w5, #1
        st1             {v1.h}[2], [x0], x1
.endm
1:      calc_all
.purgem calc
2:      mov             sp, x14
        ret
endfunc

function hevc_put_hevc_qpel_bi_hv8_8_end_neon
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        load_qpel_filterh x7, x6
        ld1             {v16.8h}, [sp], x9
        ld1             {v17.8h}, [sp], x9
        ld1             {v18.8h}, [sp], x9
        ld1             {v19.8h}, [sp], x9
        ld1             {v20.8h}, [sp], x9
        ld1             {v21.8h}, [sp], x9
        ld1             {v22.8h}, [sp], x9
.macro calc tmp, src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\tmp\().8h}, [sp], x9
        calc_qpelh      v1, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sshr
        calc_qpelh2     v2, v2, \src0, \src1, \src2, \src3, \src4, \src5, \src6, \src7, sshr
        ld1             {v5.8h}, [x4], x9 // src2
        saddw           v1.4s, v1.4s, v5.4h
        saddw2          v2.4s, v2.4s, v5.8h
        rshrn           v1.4h, v1.4s, #7
        rshrn2          v1.8h, v2.4s, #7
        sqxtun          v1.8b, v1.8h
        subs            w5, w5, #1
        st1             {v1.8b}, [x0], x1
.endm
1:      calc_all
.purgem calc
2:      mov             sp, x14
        ret
endfunc

function hevc_put_hevc_qpel_bi_hv16_8_end_neon
        load_qpel_filterh x7, x8
        mov             x9, #(HEVC_MAX_PB_SIZE * 2)
        mov             x10, x6
0:      mov             x8, sp          // src
        ld1             {v16.8h, v17.8h}, [x8], x9
        mov             w11, w5         // height
        ld1             {v18.8h, v19.8h}, [x8], x9
        mov             x12, x4         // src2
        ld1             {v20.8h, v21.8h}, [x8], x9
        mov             x7, x0          // dst
        ld1             {v22.8h, v23.8h}, [x8], x9
        ld1             {v24.8h, v25.8h}, [x8], x9
        ld1             {v26.8h, v27.8h}, [x8], x9
        ld1             {v28.8h, v29.8h}, [x8], x9
.macro calc tmp0, tmp1, src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\tmp0\().8h, \tmp1\().8h}, [x8], x9
        calc_qpelh      v1,     \src0,  \src1, \src2,  \src3,  \src4,  \src5,  \src6,  \src7, sshr
        calc_qpelh2     v2, v2, \src0, \src1,  \src2,  \src3,  \src4,  \src5,  \src6,  \src7, sshr
        calc_qpelh      v3,     \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15, sshr
        calc_qpelh2     v4, v4, \src8, \src9, \src10, \src11, \src12, \src13, \src14, \src15, sshr
        ld1             {v5.8h, v6.8h}, [x12], x9 // src2
        saddw           v1.4s, v1.4s, v5.4h
        saddw2          v2.4s, v2.4s, v5.8h
        saddw           v3.4s, v3.4s, v6.4h
        saddw2          v4.4s, v4.4s, v6.8h
        rshrn           v1.4h, v1.4s, #7
        rshrn2          v1.8h, v2.4s, #7
        rshrn           v2.4h, v3.4s, #7
        rshrn2          v2.8h, v4.4s, #7
        sqxtun          v1.8b, v1.8h
        sqxtun2         v1.16b, v2.8h
        subs            x11, x11, #1
        st1             {v1.16b}, [x7], x1
.endm
1:      calc_all2
.purgem calc
2:      add             x0, x0, #16
        add             sp, sp, #32
        subs            x10, x10, #16
        add             x4, x4, #32
        b.ne            0b
        mov             sp, x14
        ret
endfunc

.macro qpel_bi_hv suffix
function ff_hevc_put_hevc_qpel_bi_hv4_8_\suffix, export=1
        add             w10, w5, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10 // tmp_array
        stp             x7, x30, [sp, #-64]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x14,    [sp, #48]
        sub             x1, x2, x3, lsl #1
        sub             x1, x1, x3
        add             x0, sp, #64
        mov             x2, x3
        add             w3, w5, #7
        mov             x4, x6
        bl              X(ff_hevc_put_hevc_qpel_h4_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x14,    [sp, #48]
        ldp             x7, x30, [sp], #64
        b               hevc_put_hevc_qpel_bi_hv4_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_bi_hv6_8_\suffix, export=1
        add             w10, w5, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x7, x30, [sp, #-64]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x14,    [sp, #48]
        sub             x1, x2, x3, lsl #1
        sub             x1, x1, x3
        add             x0, sp, #64
        mov             x2, x3
        add             x3, x5, #7
        mov             x4, x6
        bl              X(ff_hevc_put_hevc_qpel_h6_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x14,    [sp, #48]
        ldp             x7, x30, [sp], #64
        b               hevc_put_hevc_qpel_bi_hv6_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_bi_hv8_8_\suffix, export=1
        add             w10, w5, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x7, x30, [sp, #-64]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x14,    [sp, #48]
        sub             x1, x2, x3, lsl #1
        sub             x1, x1, x3
        add             x0, sp, #64
        mov             x2, x3
        add             x3, x5, #7
        mov             x4, x6
        bl              X(ff_hevc_put_hevc_qpel_h8_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x14,    [sp, #48]
        ldp             x7, x30, [sp], #64
        b               hevc_put_hevc_qpel_bi_hv8_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_bi_hv12_8_\suffix, export=1
        stp             x6, x7, [sp, #-80]!
        stp             x4, x5, [sp, #16]
        stp             x2, x3, [sp, #32]
        stp             x0, x1, [sp, #48]
        str             x30, [sp, #64]
        bl              X(ff_hevc_put_hevc_qpel_bi_hv8_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x2, x3, [sp, #32]
        ldp             x0, x1, [sp, #48]
        ldp             x6, x7, [sp], #64
        add             x4, x4, #16
        add             x2, x2, #8
        add             x0, x0, #8
        bl              X(ff_hevc_put_hevc_qpel_bi_hv4_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

function ff_hevc_put_hevc_qpel_bi_hv16_8_\suffix, export=1
        add             w10, w5, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x7, x30, [sp, #-64]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x14,    [sp, #48]
        add             x0, sp, #64
        sub             x1, x2, x3, lsl #1
        sub             x1, x1, x3
        mov             x2, x3
        add             w3, w5, #7
        mov             x4, x6
        bl              X(ff_hevc_put_hevc_qpel_h16_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x14,    [sp, #48]
        ldp             x7, x30, [sp], #64
        mov             x6, #16          // width
        b               hevc_put_hevc_qpel_bi_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_bi_hv24_8_\suffix, export=1
        stp             x6, x7, [sp, #-80]!
        stp             x4, x5, [sp, #16]
        stp             x2, x3, [sp, #32]
        stp             x0, x1, [sp, #48]
        str             x30, [sp, #64]
        bl              X(ff_hevc_put_hevc_qpel_bi_hv16_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x2, x3, [sp, #32]
        ldp             x0, x1, [sp, #48]
        ldp             x6, x7, [sp], #64
        add             x4, x4, #32
        add             x2, x2, #16
        add             x0, x0, #16
        bl              X(ff_hevc_put_hevc_qpel_bi_hv8_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

function ff_hevc_put_hevc_qpel_bi_hv32_8_\suffix, export=1
        add             w10, w5, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10         // tmp_array
        stp             x7, x30, [sp, #-64]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x14,    [sp, #48]
        add             x0, sp, #64
        sub             x1, x2, x3, lsl #1
        mov             x2, x3
        sub             x1, x1, x3
        add             w3, w5, #7
        mov             x4, x6
        mov             w6, #32
        bl              X(ff_hevc_put_hevc_qpel_h32_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x14,    [sp, #48]
        ldp             x7, x30, [sp], #64
        mov             x6, #32 // width
        b               hevc_put_hevc_qpel_bi_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_bi_hv48_8_\suffix, export=1
        add             w10, w5, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10 // tmp_array
        stp             x7, x30, [sp, #-64]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x14,    [sp, #48]
        add             x0, sp, #64
        sub             x1, x2, x3, lsl #1
        mov             x2, x3
        sub             x1, x1, x3
        add             w3, w5, #7
        mov             x4, x6
.ifc \suffix, neon
        mov             w6, #48
        bl              X(ff_hevc_put_hevc_qpel_h32_8_\suffix)
.else
        bl              X(ff_hevc_put_hevc_qpel_h48_8_\suffix)
.endif
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x14,    [sp, #48]
        ldp             x7, x30, [sp], #64
        mov             x6, #48 // width
        b               hevc_put_hevc_qpel_bi_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_qpel_bi_hv64_8_\suffix, export=1
        add             w10, w5, #8
        lsl             x10, x10, #7
        mov             x14, sp
        sub             sp, sp, x10 // tmp_array
        stp             x7, x30, [sp, #-64]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x14,    [sp, #48]
        add             x0, sp, #64
        sub             x1, x2, x3, lsl #1
        mov             x2, x3
        sub             x1, x1, x3
        add             w3, w5, #7
        mov             x4, x6
.ifc \suffix, neon
        mov             w6, #64
        bl              X(ff_hevc_put_hevc_qpel_h32_8_\suffix)
.else
        bl              X(ff_hevc_put_hevc_qpel_h64_8_\suffix)
.endif
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x14,    [sp, #48]
        ldp             x7, x30, [sp], #64
        mov             x6, #64          // width
        b               hevc_put_hevc_qpel_bi_hv16_8_end_neon
endfunc
.endm

qpel_bi_hv neon

#if HAVE_I8MM
ENABLE_I8MM

qpel_uni_w_hv neon_i8mm

qpel_bi_hv neon_i8mm

DISABLE_I8MM
#endif // HAVE_I8MM
