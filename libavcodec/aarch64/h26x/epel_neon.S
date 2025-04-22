/* -*-arm64-*-
 * vim: syntax=arm64asm
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

const epel_filters, align=4
        .byte  0,  0,  0,  0
        .byte -2, 58, 10, -2
        .byte -4, 54, 16, -2
        .byte -6, 46, 28, -4
        .byte -4, 36, 36, -4
        .byte -4, 28, 46, -6
        .byte -2, 16, 54, -4
        .byte -2, 10, 58, -2
endconst

const epel_filters_abs, align=4
        .byte  0,  0,  0,  0
        .byte  2, 58, 10,  2
        .byte  4, 54, 16,  2
        .byte  6, 46, 28,  4
        .byte  4, 36, 36,  4
        .byte  4, 28, 46,  6
        .byte  2, 16, 54,  4
        .byte  2, 10, 58,  2
endconst


.macro load_epel_filterb freg, xreg
        movrel          \xreg, epel_filters_abs
        add             \xreg, \xreg, \freg, lsl #2
        ld4r            {v0.16b, v1.16b, v2.16b, v3.16b}, [\xreg] // filter
.endm

.macro calc_epelb dst, src0, src1, src2, src3
        umull           \dst\().8h, \src1\().8b, v1.8b
        umlsl           \dst\().8h, \src0\().8b, v0.8b
        umlal           \dst\().8h, \src2\().8b, v2.8b
        umlsl           \dst\().8h, \src3\().8b, v3.8b
.endm

.macro calc_epelb2 dst, src0, src1, src2, src3
        umull2          \dst\().8h, \src1\().16b, v1.16b
        umlsl2          \dst\().8h, \src0\().16b, v0.16b
        umlal2          \dst\().8h, \src2\().16b, v2.16b
        umlsl2          \dst\().8h, \src3\().16b, v3.16b
.endm

.macro load_epel_filterh freg, xreg
        movrel          \xreg, epel_filters
        add             \xreg, \xreg, \freg, lsl #2
        ld1             {v0.8b}, [\xreg]
        sxtl            v0.8h, v0.8b
.endm

.macro vvc_load_epel_filterh freg
        ld1             {v0.8b}, [\freg]
        sxtl            v0.8h, v0.8b
.endm

.macro calc_epelh dst, src0, src1, src2, src3
        smull           \dst\().4s, \src0\().4h, v0.h[0]
        smlal           \dst\().4s, \src1\().4h, v0.h[1]
        smlal           \dst\().4s, \src2\().4h, v0.h[2]
        smlal           \dst\().4s, \src3\().4h, v0.h[3]
        sqshrn          \dst\().4h, \dst\().4s, #6
.endm

.macro calc_epelh2 dst, tmp, src0, src1, src2, src3
        smull2          \tmp\().4s, \src0\().8h, v0.h[0]
        smlal2          \tmp\().4s, \src1\().8h, v0.h[1]
        smlal2          \tmp\().4s, \src2\().8h, v0.h[2]
        smlal2          \tmp\().4s, \src3\().8h, v0.h[3]
        sqshrn2         \dst\().8h, \tmp\().4s, #6
.endm

.macro calc_all4
        calc            v16, v17, v18, v19
        b.eq            2f
        calc            v17, v18, v19, v16
        b.eq            2f
        calc            v18, v19, v16, v17
        b.eq            2f
        calc            v19, v16, v17, v18
        b.ne            1b
.endm

.macro calc_all8
        calc            v16, v17, v18, v19, v20, v21, v22, v23
        b.eq            2f
        calc            v18, v19, v20, v21, v22, v23, v16, v17
        b.eq            2f
        calc            v20, v21, v22, v23, v16, v17, v18, v19
        b.eq            2f
        calc            v22, v23, v16, v17, v18, v19, v20, v21
        b.ne            1b
.endm

.macro calc_all12
        calc            v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27
        b.eq            2f
        calc            v19, v20, v21, v22, v23, v24, v25, v26, v27, v16, v17, v18
        b.eq            2f
        calc            v22, v23, v24, v25, v26, v27, v16, v17, v18, v19, v20, v21
        b.eq            2f
        calc            v25, v26, v27, v16, v17, v18, v19, v20, v21, v22, v23, v24
        b.ne            1b
.endm

.macro calc_all16
        calc            v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31
        b.eq            2f
        calc            v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31, v16, v17, v18, v19
        b.eq            2f
        calc            v24, v25, v26, v27, v28, v29, v30, v31, v16, v17, v18, v19, v20, v21, v22, v23
        b.eq            2f
        calc            v28, v29, v30, v31, v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27
        b.ne            1b
.endm

function ff_vvc_put_pel_pixels4_8_neon, export=1
        mov             x7, #(VVC_MAX_PB_SIZE * 2)
        b               1f
endfunc

function ff_hevc_put_hevc_pel_pixels4_8_neon, export=1
        mov             x7, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v0.s}[0], [x1], x2
        ushll           v4.8h, v0.8b, #6
        subs            w3, w3, #1
        st1             {v4.d}[0], [x0], x7
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_pixels6_8_neon, export=1
        mov             x7, #(HEVC_MAX_PB_SIZE * 2 - 8)
1:      ld1             {v0.8b}, [x1], x2
        ushll           v4.8h, v0.8b, #6
        st1             {v4.d}[0], [x0], #8
        subs            w3, w3, #1
        st1             {v4.s}[2], [x0], x7
        b.ne            1b
        ret
endfunc

function ff_vvc_put_pel_pixels8_8_neon, export=1
        mov             x7, #(VVC_MAX_PB_SIZE * 2)
        b               1f
endfunc

function ff_hevc_put_hevc_pel_pixels8_8_neon, export=1
        mov             x7, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v0.8b}, [x1], x2
        ushll           v4.8h, v0.8b, #6
        subs            w3, w3, #1
        st1             {v4.8h}, [x0], x7
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_pixels12_8_neon, export=1
        mov             x7, #(HEVC_MAX_PB_SIZE * 2 - 16)
1:      ld1             {v0.8b, v1.8b}, [x1], x2
        ushll           v4.8h, v0.8b, #6
        st1             {v4.8h}, [x0], #16
        ushll           v5.8h, v1.8b, #6
        subs            w3, w3, #1
        st1             {v5.d}[0], [x0], x7
        b.ne            1b
        ret
endfunc

function ff_vvc_put_pel_pixels16_8_neon, export=1
        mov             x7, #(VVC_MAX_PB_SIZE * 2)
        b               1f
endfunc

function ff_hevc_put_hevc_pel_pixels16_8_neon, export=1
        mov             x7, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v0.8b, v1.8b}, [x1], x2
        ushll           v4.8h, v0.8b, #6
        ushll           v5.8h, v1.8b, #6
        subs            w3, w3, #1
        st1             {v4.8h, v5.8h}, [x0], x7
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_pixels24_8_neon, export=1
        mov             x7, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v0.8b-v2.8b}, [x1], x2
        ushll           v4.8h, v0.8b, #6
        ushll           v5.8h, v1.8b, #6
        ushll           v6.8h, v2.8b, #6
        subs            w3, w3, #1
        st1             {v4.8h-v6.8h}, [x0], x7
        b.ne            1b
        ret
endfunc

function ff_vvc_put_pel_pixels32_8_neon, export=1
        mov             x7, #(VVC_MAX_PB_SIZE * 2)
        b               1f
endfunc

function ff_hevc_put_hevc_pel_pixels32_8_neon, export=1
        mov             x7, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v0.8b-v3.8b}, [x1], x2
        ushll           v4.8h, v0.8b, #6
        ushll           v5.8h, v1.8b, #6
        ushll           v6.8h, v2.8b, #6
        ushll           v7.8h, v3.8b, #6
        subs            w3, w3, #1
        st1             {v4.8h-v7.8h}, [x0], x7
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_pixels48_8_neon, export=1
        mov             x7, #(HEVC_MAX_PB_SIZE)
1:      ld1             {v0.16b-v2.16b}, [x1], x2
        ushll           v4.8h, v0.8b, #6
        ushll2          v5.8h, v0.16b, #6
        ushll           v6.8h, v1.8b, #6
        ushll2          v7.8h, v1.16b, #6
        st1             {v4.8h-v7.8h}, [x0], #64
        ushll           v16.8h, v2.8b, #6
        ushll2          v17.8h, v2.16b, #6
        subs            w3, w3, #1
        st1             {v16.8h-v17.8h}, [x0], x7
        b.ne            1b
        ret
endfunc

.macro put_pel_pixels64_8_neon
        ushll           v4.8h, v0.8b, #6
        ushll2          v5.8h, v0.16b, #6
        ushll           v6.8h, v1.8b, #6
        ushll2          v7.8h, v1.16b, #6
        st1             {v4.8h-v7.8h}, [x0], #64
        ushll           v16.8h, v2.8b, #6
        ushll2          v17.8h, v2.16b, #6
        ushll           v18.8h, v3.8b, #6
        ushll2          v19.8h, v3.16b, #6
        st1             {v16.8h-v19.8h}, [x0], x7
.endm

function ff_vvc_put_pel_pixels64_8_neon, export=1
        mov             x7, #(2 * VVC_MAX_PB_SIZE - 64)
        b               1f
endfunc

function ff_hevc_put_hevc_pel_pixels64_8_neon, export=1
        mov             x7, #(HEVC_MAX_PB_SIZE)
1:
        ld1             {v0.16b-v3.16b}, [x1], x2
        subs            w3, w3, #1
        put_pel_pixels64_8_neon
        b.ne            1b
        ret
endfunc

function ff_vvc_put_pel_pixels128_8_neon, export=1
        mov             x7, #64
1:
        mov             x6, x1
        ld1             {v0.16b-v3.16b}, [x6], #64
        add             x1, x1, x2
        subs            w3, w3, #1
        put_pel_pixels64_8_neon
        ld1             {v0.16b-v3.16b}, [x6], #64
        put_pel_pixels64_8_neon
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_pixels4_8_neon, export=1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v0.s}[0], [x2], x3 // src
        ushll           v16.8h, v0.8b, #6
        ld1             {v20.4h}, [x4], x10 // src2
        sqadd           v16.8h, v16.8h, v20.8h
        sqrshrun        v0.8b,  v16.8h, #7
        st1             {v0.s}[0], [x0], x1
        subs            w5, w5, #1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_pixels6_8_neon, export=1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        sub             x1, x1, #4
1:      ld1             {v0.8b}, [x2], x3
        ushll           v16.8h, v0.8b, #6
        ld1             {v20.8h}, [x4], x10
        sqadd           v16.8h, v16.8h, v20.8h
        sqrshrun        v0.8b,  v16.8h, #7
        st1             {v0.s}[0], [x0], #4
        st1             {v0.h}[2], [x0], x1
        subs            w5, w5, #1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_pixels8_8_neon, export=1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v0.8b}, [x2], x3    // src
        ushll           v16.8h, v0.8b, #6
        ld1             {v20.8h}, [x4], x10  // src2
        sqadd           v16.8h, v16.8h, v20.8h
        sqrshrun        v0.8b,  v16.8h, #7
        subs            w5, w5, #1
        st1             {v0.8b}, [x0], x1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_pixels12_8_neon, export=1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        sub             x1, x1, #8
1:      ld1             {v0.16b}, [x2], x3
        ushll           v16.8h, v0.8b, #6
        ushll2          v17.8h, v0.16b, #6
        ld1             {v20.8h, v21.8h}, [x4], x10
        sqadd           v16.8h, v16.8h, v20.8h
        sqadd           v17.8h, v17.8h, v21.8h
        sqrshrun        v0.8b,  v16.8h, #7
        sqrshrun2       v0.16b, v17.8h, #7
        st1             {v0.8b}, [x0], #8
        subs            w5, w5, #1
        st1             {v0.s}[2], [x0], x1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_pixels16_8_neon, export=1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v0.16b}, [x2], x3 // src
        ushll           v16.8h, v0.8b, #6
        ushll2          v17.8h, v0.16b, #6
        ld1             {v20.8h, v21.8h}, [x4], x10  // src2
        sqadd           v16.8h, v16.8h, v20.8h
        sqadd           v17.8h, v17.8h, v21.8h
        sqrshrun        v0.8b,  v16.8h, #7
        sqrshrun2       v0.16b, v17.8h, #7
        subs            w5, w5, #1
        st1             {v0.16b}, [x0], x1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_pixels24_8_neon, export=1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v0.8b-v2.8b}, [x2], x3 // src
        ushll           v16.8h, v0.8b, #6
        ushll           v17.8h, v1.8b, #6
        ushll           v18.8h, v2.8b, #6
        ld1             {v20.8h-v22.8h}, [x4], x10  // src2
        sqadd           v16.8h, v16.8h, v20.8h
        sqadd           v17.8h, v17.8h, v21.8h
        sqadd           v18.8h, v18.8h, v22.8h
        sqrshrun        v0.8b, v16.8h, #7
        sqrshrun        v1.8b, v17.8h, #7
        sqrshrun        v2.8b, v18.8h, #7
        subs            w5, w5, #1
        st1             {v0.8b-v2.8b}, [x0], x1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_pixels32_8_neon, export=1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v0.16b-v1.16b}, [x2], x3 // src
        ushll           v16.8h, v0.8b, #6
        ushll2          v17.8h, v0.16b, #6
        ushll           v18.8h, v1.8b, #6
        ushll2          v19.8h, v1.16b, #6
        ld1             {v20.8h-v23.8h}, [x4], x10  // src2
        sqadd           v16.8h, v16.8h, v20.8h
        sqadd           v17.8h, v17.8h, v21.8h
        sqadd           v18.8h, v18.8h, v22.8h
        sqadd           v19.8h, v19.8h, v23.8h
        sqrshrun        v0.8b,  v16.8h, #7
        sqrshrun2       v0.16b, v17.8h, #7
        sqrshrun        v1.8b,  v18.8h, #7
        sqrshrun2       v1.16b, v19.8h, #7
        st1             {v0.16b-v1.16b}, [x0], x1
        subs            w5, w5, #1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_pixels48_8_neon, export=1
        mov             x10, #(HEVC_MAX_PB_SIZE)
1:      ld1             {v0.16b-v2.16b}, [x2], x3 // src
        ushll           v16.8h, v0.8b, #6
        ushll2          v17.8h, v0.16b, #6
        ushll           v18.8h, v1.8b, #6
        ushll2          v19.8h, v1.16b, #6
        ushll           v20.8h, v2.8b, #6
        ushll2          v21.8h, v2.16b, #6
        ld1             {v24.8h-v27.8h}, [x4], #(HEVC_MAX_PB_SIZE) // src2
        sqadd           v16.8h, v16.8h, v24.8h
        sqadd           v17.8h, v17.8h, v25.8h
        sqadd           v18.8h, v18.8h, v26.8h
        sqadd           v19.8h, v19.8h, v27.8h
        ld1             {v24.8h-v25.8h}, [x4], x10
        sqadd           v20.8h, v20.8h, v24.8h
        sqadd           v21.8h, v21.8h, v25.8h
        sqrshrun        v0.8b, v16.8h, #7
        sqrshrun2       v0.16b, v17.8h, #7
        sqrshrun        v1.8b, v18.8h, #7
        sqrshrun2       v1.16b, v19.8h, #7
        sqrshrun        v2.8b, v20.8h, #7
        sqrshrun2       v2.16b, v21.8h, #7
        subs            w5, w5, #1
        st1             {v0.16b-v2.16b}, [x0], x1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_pixels64_8_neon, export=1
1:      ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x2], x3 // src
        ushll           v16.8h, v0.8b, #6
        ushll2          v17.8h, v0.16b, #6
        ushll           v18.8h, v1.8b, #6
        ushll2          v19.8h, v1.16b, #6
        ushll           v20.8h, v2.8b, #6
        ushll2          v21.8h, v2.16b, #6
        ushll           v22.8h, v3.8b, #6
        ushll2          v23.8h, v3.16b, #6
        ld1             {v24.8h, v25.8h, v26.8h, v27.8h}, [x4], #(HEVC_MAX_PB_SIZE) // src2
        sqadd           v16.8h, v16.8h, v24.8h
        sqadd           v17.8h, v17.8h, v25.8h
        sqadd           v18.8h, v18.8h, v26.8h
        sqadd           v19.8h, v19.8h, v27.8h
        ld1             {v24.8h, v25.8h, v26.8h, v27.8h}, [x4], #(HEVC_MAX_PB_SIZE)
        sqadd           v20.8h, v20.8h, v24.8h
        sqadd           v21.8h, v21.8h, v25.8h
        sqadd           v22.8h, v22.8h, v26.8h
        sqadd           v23.8h, v23.8h, v27.8h
        sqrshrun        v0.8b, v16.8h, #7
        sqrshrun2       v0.16b, v17.8h, #7
        sqrshrun        v1.8b, v18.8h, #7
        sqrshrun2       v1.16b, v19.8h, #7
        sqrshrun        v2.8b, v20.8h, #7
        sqrshrun2       v2.16b, v21.8h, #7
        sqrshrun        v3.8b, v22.8h, #7
        sqrshrun2       v3.16b, v23.8h, #7
        st1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], x1
        subs            w5, w5, #1
        b.ne            1b
        ret
endfunc

.macro load_bi_w_pixels_param
        ldrsw           x8, [sp]            // wx1
#if defined(__APPLE__)
        ldpsw           x9, x10, [sp, #4]   // ox0, ox1
        ldrsw           x11, [sp, #32]      // width
#else
        ldrsw           x9, [sp, #8]        // ox0
        ldrsw           x10, [sp, #16]      // ox1
        ldrsw           x11, [sp, 40]       // width
#endif
.endm

function ff_hevc_put_hevc_pel_bi_w_pixels4_8_neon, export=1
        load_bi_w_pixels_param
        add             w6, w6, #6              // log2Wd
        dup             v0.8h, w7               // wx0
        dup             v1.8h, w8               // wx1
        add             w9, w9, w10
        add             w9, w9, #1              // ox0 + ox1 + 1
        lsl             w9, w9, w6
        add             w7, w6, #1              // (log2Wd + 1)
        mov             x8, #(2 * HEVC_MAX_PB_SIZE)
        neg             w7, w7
        dup             v2.4s, w9               // (ox0 + ox1 + 1) << logwWd
        dup             v6.4s, w7               // -(log2Wd + 1)
1:
        ld1             {v4.8b}, [x2], x3       // load src
        ld1             {v5.8b}, [x4], x8       // load src2
        subs            w5, w5, #1
        mov             v3.16b, v2.16b
        ushll           v4.8h, v4.8b, #6
        smlal           v3.4s, v4.4h, v1.4h
        smlal           v3.4s, v5.4h, v0.4h
        sshl            v3.4s, v3.4s, v6.4s
        sqxtn           v3.4h, v3.4s
        sqxtun          v3.8b, v3.8h
        st1             {v3.s}[0], [x0], x1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_w_pixels6_8_neon, export=1
        load_bi_w_pixels_param
        add             w6, w6, #6              // log2Wd
        dup             v0.8h, w7               // wx0
        dup             v1.8h, w8               // wx1
        add             w9, w9, w10
        add             w9, w9, #1              // ox0 + ox1 + 1
        lsl             w9, w9, w6
        add             w7, w6, #1              // (log2Wd + 1)
        mov             x8, #(2 * HEVC_MAX_PB_SIZE)
        neg             w7, w7
        dup             v2.4s, w9               // (ox0 + ox1 + 1) << logwWd
        dup             v6.4s, w7               // -(log2Wd + 1)
        sub             x1, x1, #4
1:
        ld1             {v4.8b}, [x2], x3       // load src
        ld1             {v5.8h}, [x4], x8       // load src2
        subs            w5, w5, #1
        mov             v3.16b, v2.16b
        mov             v7.16b, v2.16b
        ushll           v4.8h, v4.8b, #6
        smlal           v3.4s, v4.4h, v1.4h
        smlal           v3.4s, v5.4h, v0.4h
        smlal2          v7.4s, v4.8h, v1.8h
        smlal2          v7.4s, v5.8h, v0.8h
        sshl            v3.4s, v3.4s, v6.4s
        sshl            v7.4s, v7.4s, v6.4s
        sqxtn           v3.4h, v3.4s
        sqxtn2          v3.8h, v7.4s
        sqxtun          v3.8b, v3.8h
        str             s3, [x0], #4
        st1             {v3.h}[2], [x0], x1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_w_pixels8_8_neon, export=1
        load_bi_w_pixels_param
        add             w6, w6, #6              // log2Wd
        dup             v0.8h, w7               // wx0
        dup             v1.8h, w8               // wx1
        add             w9, w9, w10
        add             w9, w9, #1              // ox0 + ox1 + 1
        lsl             w9, w9, w6
        add             w7, w6, #1              // (log2Wd + 1)
        mov             x8, #(2 * HEVC_MAX_PB_SIZE)
        neg             w7, w7
        dup             v2.4s, w9               // (ox0 + ox1 + 1) << logwWd
        dup             v6.4s, w7               // -(log2Wd + 1)
1:
        ld1             {v4.8b}, [x2], x3       // load src
        ld1             {v5.8h}, [x4], x8       // load src2
        subs            w5, w5, #1
        mov             v3.16b, v2.16b
        mov             v7.16b, v2.16b
        ushll           v4.8h, v4.8b, #6
        smlal           v3.4s, v4.4h, v1.4h
        smlal           v3.4s, v5.4h, v0.4h
        smlal2          v7.4s, v4.8h, v1.8h
        smlal2          v7.4s, v5.8h, v0.8h
        sshl            v3.4s, v3.4s, v6.4s
        sshl            v7.4s, v7.4s, v6.4s
        sqxtn           v3.4h, v3.4s
        sqxtn2          v3.8h, v7.4s
        sqxtun          v3.8b, v3.8h
        st1             {v3.8b}, [x0], x1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_w_pixels12_8_neon, export=1
        load_bi_w_pixels_param
        add             w6, w6, #6              // log2Wd
        dup             v0.8h, w7               // wx0
        dup             v1.8h, w8               // wx1
        add             w9, w9, w10
        add             w9, w9, #1              // ox0 + ox1 + 1
        lsl             w9, w9, w6
        add             w7, w6, #1              // (log2Wd + 1)
        mov             x8, #(2 * HEVC_MAX_PB_SIZE)
        neg             w7, w7
        dup             v2.4s, w9               // (ox0 + ox1 + 1) << logwWd
        dup             v6.4s, w7               // -(log2Wd + 1)
        sub             x1, x1, #8
1:
        ld1             {v24.16b}, [x2], x3          // load src
        ld1             {v20.16b, v21.16b}, [x4], x8   // load src2
        subs            w5, w5, #1

        mov             v16.16b, v2.16b
        mov             v17.16b, v2.16b
        mov             v18.16b, v2.16b

        ushll           v4.8h, v24.8b, #6
        ushll2          v24.8h, v24.16b, #6

        smlal           v16.4s, v4.4h, v1.4h
        smlal           v16.4s, v20.4h, v0.4h
        smlal2          v17.4s, v4.8h, v1.8h
        smlal2          v17.4s, v20.8h, v0.8h
        smlal           v18.4s, v24.4h, v1.4h
        smlal           v18.4s, v21.4h, v0.4h

        sshl            v16.4s, v16.4s, v6.4s
        sshl            v17.4s, v17.4s, v6.4s
        sshl            v18.4s, v18.4s, v6.4s

        sqxtn           v16.4h, v16.4s
        sqxtn2          v16.8h, v17.4s
        sqxtn           v18.4h, v18.4s
        sqxtun          v3.8b, v16.8h
        sqxtun2         v3.16b, v18.8h
        str             d3, [x0], #8
        st1             {v3.s}[2], [x0], x1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_w_pixels16_8_neon, export=1
        load_bi_w_pixels_param
        add             w6, w6, #6              // log2Wd
        dup             v0.8h, w7               // wx0
        dup             v1.8h, w8               // wx1
        add             w9, w9, w10
        add             w9, w9, #1              // ox0 + ox1 + 1
        lsl             w9, w9, w6
        add             w7, w6, #1              // (log2Wd + 1)
        mov             x8, #(2 * HEVC_MAX_PB_SIZE)
        neg             w7, w7
        dup             v2.4s, w9               // (ox0 + ox1 + 1) << logwWd
        dup             v6.4s, w7               // -(log2Wd + 1)
1:
        ld1             {v24.16b}, [x2], x3          // load src
        ld1             {v20.16b, v21.16b}, [x4], x8   // load src2
        subs            w5, w5, #1

        mov             v16.16b, v2.16b
        mov             v17.16b, v2.16b
        mov             v18.16b, v2.16b
        mov             v19.16b, v2.16b

        ushll           v4.8h, v24.8b, #6
        ushll2          v24.8h, v24.16b, #6

        smlal           v16.4s, v4.4h, v1.4h
        smlal           v16.4s, v20.4h, v0.4h
        smlal2          v17.4s, v4.8h, v1.8h
        smlal2          v17.4s, v20.8h, v0.8h
        smlal           v18.4s, v24.4h, v1.4h
        smlal           v18.4s, v21.4h, v0.4h
        smlal2          v19.4s, v24.8h, v1.8h
        smlal2          v19.4s, v21.8h, v0.8h

        sshl            v16.4s, v16.4s, v6.4s
        sshl            v17.4s, v17.4s, v6.4s
        sshl            v18.4s, v18.4s, v6.4s
        sshl            v19.4s, v19.4s, v6.4s

        sqxtn           v16.4h, v16.4s
        sqxtn2          v16.8h, v17.4s
        sqxtn           v18.4h, v18.4s
        sqxtn2          v18.8h, v19.4s
        sqxtun          v3.8b, v16.8h
        sqxtun2         v3.16b, v18.8h
        st1             {v3.16b}, [x0], x1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_w_pixels24_8_neon, export=1
        load_bi_w_pixels_param
        add             w6, w6, #6              // log2Wd
        dup             v0.8h, w7               // wx0
        dup             v1.8h, w8               // wx1
        add             w9, w9, w10
        add             w9, w9, #1              // ox0 + ox1 + 1
        lsl             w9, w9, w6
        add             w7, w6, #1              // (log2Wd + 1)
        mov             x8, #(2 * HEVC_MAX_PB_SIZE)
        neg             w7, w7
        dup             v2.4s, w9               // (ox0 + ox1 + 1) << logwWd
        dup             v6.4s, w7               // -(log2Wd + 1)
        mov             x7, #24
        sub             x3, x3, x11
        sub             x8, x8, x11, lsl #1
        sub             x1, x1, x11
1:
        mov             w6, w11
2:
        ld1             {v24.16b, v25.16b}, [x2], x7
        ld1             {v20.8h, v21.8h, v22.8h}, [x4], #48

        subs            w6, w6, #24

        mov             v16.16b, v2.16b
        mov             v17.16b, v2.16b
        mov             v18.16b, v2.16b
        mov             v19.16b, v2.16b
        mov             v26.16b, v2.16b
        mov             v27.16b, v2.16b

        ushll           v4.8h, v24.8b, #6
        ushll2          v24.8h, v24.16b, #6
        ushll           v5.8h, v25.8b, #6

        smlal           v16.4s, v4.4h, v1.4h
        smlal           v16.4s, v20.4h, v0.4h
        smlal2          v17.4s, v4.8h, v1.8h
        smlal2          v17.4s, v20.8h, v0.8h
        smlal           v18.4s, v24.4h, v1.4h
        smlal           v18.4s, v21.4h, v0.4h
        smlal2          v19.4s, v24.8h, v1.8h
        smlal2          v19.4s, v21.8h, v0.8h
        smlal           v26.4s, v5.4h, v1.4h
        smlal           v26.4s, v22.4h, v0.4h
        smlal2          v27.4s, v5.8h, v1.8h
        smlal2          v27.4s, v22.8h, v0.8h

        sshl            v16.4s, v16.4s, v6.4s
        sshl            v17.4s, v17.4s, v6.4s
        sshl            v18.4s, v18.4s, v6.4s
        sshl            v19.4s, v19.4s, v6.4s
        sshl            v26.4s, v26.4s, v6.4s
        sshl            v27.4s, v27.4s, v6.4s

        sqxtn           v16.4h, v16.4s
        sqxtn2          v16.8h, v17.4s
        sqxtn           v18.4h, v18.4s
        sqxtn2          v18.8h, v19.4s
        sqxtn           v26.4h, v26.4s
        sqxtn2          v26.8h, v27.4s
        sqxtun          v3.8b, v16.8h
        sqxtun2         v3.16b, v18.8h
        sqxtun          v4.8b, v26.8h
        str             q3, [x0], #16
        str             d4, [x0], #8
        b.ne            2b

        subs            w5, w5, #1
        add             x0, x0, x1
        add             x2, x2, x3
        add             x4, x4, x8
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_pel_bi_w_pixels32_8_neon, export=1
        load_bi_w_pixels_param
        add             w6, w6, #6              // log2Wd
        dup             v0.8h, w7               // wx0
        dup             v1.8h, w8               // wx1
        add             w9, w9, w10
        add             w9, w9, #1              // ox0 + ox1 + 1
        lsl             w9, w9, w6
        add             w7, w6, #1              // (log2Wd + 1)
        mov             x8, #(2 * HEVC_MAX_PB_SIZE)
        neg             w7, w7
        dup             v2.4s, w9               // (ox0 + ox1 + 1) << logwWd
        dup             v6.4s, w7               // -(log2Wd + 1)
        sub             x3, x3, x11
        sub             x8, x8, x11, lsl #1
        sub             x1, x1, x11
1:
        mov             w6, w11
2:
        ld1             {v24.16b, v25.16b}, [x2], #32                   // load src
        ld1             {v20.8h, v21.8h, v22.8h, v23.8h}, [x4], #64     // load src2

        subs            w6, w6, #32

        mov             v16.16b, v2.16b
        mov             v17.16b, v2.16b
        mov             v18.16b, v2.16b
        mov             v19.16b, v2.16b
        mov             v26.16b, v2.16b
        mov             v27.16b, v2.16b
        mov             v28.16b, v2.16b
        mov             v29.16b, v2.16b

        ushll           v4.8h, v24.8b, #6
        ushll2          v24.8h, v24.16b, #6
        ushll           v5.8h, v25.8b, #6
        ushll2          v25.8h, v25.16b, #6

        smlal           v16.4s, v4.4h, v1.4h
        smlal           v16.4s, v20.4h, v0.4h
        smlal2          v17.4s, v4.8h, v1.8h
        smlal2          v17.4s, v20.8h, v0.8h
        smlal           v18.4s, v24.4h, v1.4h
        smlal           v18.4s, v21.4h, v0.4h
        smlal2          v19.4s, v24.8h, v1.8h
        smlal2          v19.4s, v21.8h, v0.8h
        smlal           v26.4s, v5.4h, v1.4h
        smlal           v26.4s, v22.4h, v0.4h
        smlal2          v27.4s, v5.8h, v1.8h
        smlal2          v27.4s, v22.8h, v0.8h
        smlal           v28.4s, v25.4h, v1.4h
        smlal           v28.4s, v23.4h, v0.4h
        smlal2          v29.4s, v25.8h, v1.8h
        smlal2          v29.4s, v23.8h, v0.8h

        sshl            v16.4s, v16.4s, v6.4s
        sshl            v17.4s, v17.4s, v6.4s
        sshl            v18.4s, v18.4s, v6.4s
        sshl            v19.4s, v19.4s, v6.4s
        sshl            v26.4s, v26.4s, v6.4s
        sshl            v27.4s, v27.4s, v6.4s
        sshl            v28.4s, v28.4s, v6.4s
        sshl            v29.4s, v29.4s, v6.4s

        sqxtn           v16.4h, v16.4s
        sqxtn2          v16.8h, v17.4s
        sqxtn           v18.4h, v18.4s
        sqxtn2          v18.8h, v19.4s
        sqxtn           v26.4h, v26.4s
        sqxtn2          v26.8h, v27.4s
        sqxtn           v28.4h, v28.4s
        sqxtn2          v28.8h, v29.4s
        sqxtun          v3.8b, v16.8h
        sqxtun2         v3.16b, v18.8h
        sqxtun          v4.8b, v26.8h
        sqxtun2         v4.16b, v28.8h
        st1             {v3.16b, v4.16b}, [x0], #32
        b.ne            2b

        subs            w5, w5, #1
        add             x0, x0, x1
        add             x2, x2, x3
        add             x4, x4, x8
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_bi_h4_8_neon, export=1
        load_epel_filterb x6, x7
        sub             x2, x2, #1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v4.8b}, [x2], x3
        ext             v5.8b, v4.8b, v4.8b, #1
        ext             v6.8b, v4.8b, v4.8b, #2
        ext             v7.8b, v4.8b, v4.8b, #3
        calc_epelb      v16, v4, v5, v6, v7
        ld1             {v20.4h}, [x4], x10
        sqadd           v16.8h, v16.8h, v20.8h
        sqrshrun        v4.8b, v16.8h, #7
        st1             {v4.s}[0], [x0], x1
        subs            w5, w5, #1   // height
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_bi_h6_8_neon, export=1
        load_epel_filterb x6, x7
        sub             w1, w1, #4
        sub             x2, x2, #1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v24.16b}, [x2], x3
        ext             v26.16b, v24.16b, v24.16b, #1
        ext             v27.16b, v24.16b, v24.16b, #2
        ext             v28.16b, v24.16b, v24.16b, #3
        calc_epelb      v16, v24, v26, v27, v28
        ld1             {v20.8h}, [x4], x10
        sqadd           v16.8h, v16.8h, v20.8h
        sqrshrun        v16.8b, v16.8h, #7
        st1             {v16.s}[0], [x0], #4
        st1             {v16.h}[2], [x0], x1
        subs            w5, w5, #1   // height
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_bi_h8_8_neon, export=1
        load_epel_filterb x6, x7
        sub             x2, x2, #1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v24.16b}, [x2], x3
        ext             v26.16b, v24.16b, v24.16b, #1
        ext             v27.16b, v24.16b, v24.16b, #2
        ext             v28.16b, v24.16b, v24.16b, #3
        calc_epelb      v16, v24, v26, v27, v28
        ld1             {v20.8h}, [x4], x10
        sqadd           v16.8h, v16.8h, v20.8h
        sqrshrun        v16.8b, v16.8h, #7
        st1             {v16.8b}, [x0], x1
        subs            w5, w5, #1   // height
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_bi_h12_8_neon, export=1
        load_epel_filterb x6, x7
        sub             x1, x1, #8
        sub             x2, x2, #1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v24.16b}, [x2], x3
        ext             v26.16b, v24.16b, v24.16b, #1
        ext             v27.16b, v24.16b, v24.16b, #2
        ext             v28.16b, v24.16b, v24.16b, #3
        calc_epelb      v16, v24, v26, v27, v28
        calc_epelb2     v17, v24, v26, v27, v28
        ld1             {v20.8h, v21.8h}, [x4], x10
        sqadd           v18.8h, v16.8h, v20.8h
        sqadd           v19.8h, v17.8h, v21.8h
        sqrshrun        v20.8b, v18.8h, #7
        sqrshrun        v21.8b, v19.8h, #7
        st1             {v20.8b}, [x0], #8
        st1             {v21.s}[0], [x0], x1
        subs            w5, w5, #1   // height
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_bi_h16_8_neon, export=1
        load_epel_filterb x6, x7
        sub             x2, x2, #1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
1:      ldr             q24, [x2]
        ldr             s25, [x2, #16]
        add             x2, x2, x3
        ext             v26.16b, v24.16b, v25.16b, #1
        ext             v27.16b, v24.16b, v25.16b, #2
        ext             v28.16b, v24.16b, v25.16b, #3
        calc_epelb      v16, v24, v26, v27, v28
        calc_epelb2     v17, v24, v26, v27, v28
        ld1             {v24.8h, v25.8h}, [x4], x10
        sqadd           v16.8h, v16.8h, v24.8h
        sqadd           v17.8h, v17.8h, v25.8h
        sqrshrun        v4.8b, v16.8h, #7
        sqrshrun2       v4.16b, v17.8h, #7
        st1             {v4.16b}, [x0], x1
        subs            w5, w5, #1   // height
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_bi_h24_8_neon, export=1
        load_epel_filterb x6, x7
        sub             x2, x2, #1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
1:      ld1             {v24.16b, v25.16b}, [x2], x3
        ext             v26.16b, v24.16b, v25.16b, #1
        ext             v27.16b, v24.16b, v25.16b, #2
        ext             v28.16b, v24.16b, v25.16b, #3
        calc_epelb      v16, v24, v26, v27, v28
        calc_epelb2     v17, v24, v26, v27, v28
        ext             v26.16b, v25.16b, v25.16b, #1
        ext             v27.16b, v25.16b, v25.16b, #2
        ext             v28.16b, v25.16b, v25.16b, #3
        calc_epelb      v18, v25, v26, v27, v28
        ld1             {v20.8h, v21.8h, v22.8h}, [x4], x10
        sqadd           v16.8h, v16.8h, v20.8h
        sqadd           v17.8h, v17.8h, v21.8h
        sqadd           v18.8h, v18.8h, v22.8h
        sqrshrun        v4.8b, v16.8h, #7
        sqrshrun        v5.8b, v17.8h, #7
        sqrshrun        v6.8b, v18.8h, #7
        st1             {v4.8b, v5.8b, v6.8b}, [x0], x1
        subs            w5, w5, #1   // height
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_bi_h32_8_neon, export=1
        load_epel_filterb x6, x7
        sub             x2, x2, #1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
1:      ldp             q24, q25, [x2]
        ldr             s26, [x2, #32]
        add             x2, x2, x3
        ext             v27.16b, v24.16b, v25.16b, #1
        ext             v28.16b, v24.16b, v25.16b, #2
        ext             v29.16b, v24.16b, v25.16b, #3
        calc_epelb      v16, v24, v27, v28, v29
        calc_epelb2     v17, v24, v27, v28, v29
        ext             v27.16b, v25.16b, v26.16b, #1
        ext             v28.16b, v25.16b, v26.16b, #2
        ext             v29.16b, v25.16b, v26.16b, #3
        calc_epelb      v18, v25, v27, v28, v29
        calc_epelb2     v19, v25, v27, v28, v29
        ld1             {v24.8h, v25.8h, v26.8h, v27.8h}, [x4], x10
        sqadd           v16.8h, v16.8h, v24.8h
        sqadd           v17.8h, v17.8h, v25.8h
        sqadd           v18.8h, v18.8h, v26.8h
        sqadd           v19.8h, v19.8h, v27.8h
        sqrshrun        v4.8b, v16.8h, #7
        sqrshrun2       v4.16b, v17.8h, #7
        sqrshrun        v5.8b, v18.8h, #7
        sqrshrun2       v5.16b, v19.8h, #7
        st1             {v4.16b, v5.16b}, [x0], x1
        subs            w5, w5, #1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_bi_h48_8_neon, export=1
        load_epel_filterb x6, x7
        sub             x2, x2, #1
        mov             x7, #24
        mov             x10, #(HEVC_MAX_PB_SIZE * 2 - 48)
1:      ld1             {v24.16b, v25.16b, v26.16b}, [x2]
        ldr             s27, [x2, #48]
        add             x2, x2, x3
        ext             v28.16b, v24.16b, v25.16b, #1
        ext             v29.16b, v24.16b, v25.16b, #2
        ext             v30.16b, v24.16b, v25.16b, #3
        calc_epelb      v16, v24, v28, v29, v30
        calc_epelb2     v17, v24, v28, v29, v30
        ext             v28.16b, v25.16b, v26.16b, #1
        ext             v29.16b, v25.16b, v26.16b, #2
        ext             v30.16b, v25.16b, v26.16b, #3
        calc_epelb      v18, v25, v28, v29, v30
        calc_epelb2     v19, v25, v28, v29, v30
        ext             v28.16b, v26.16b, v27.16b, #1
        ext             v29.16b, v26.16b, v27.16b, #2
        ext             v30.16b, v26.16b, v27.16b, #3
        calc_epelb      v20, v26, v28, v29, v30
        calc_epelb2     v21, v26, v28, v29, v30

        ld1             {v24.8h, v25.8h, v26.8h}, [x4], #48
        sqadd           v16.8h, v16.8h, v24.8h
        sqadd           v17.8h, v17.8h, v25.8h
        sqadd           v18.8h, v18.8h, v26.8h
        ld1             {v27.8h, v28.8h, v29.8h}, [x4], x10
        sqadd           v19.8h, v19.8h, v27.8h
        sqadd           v20.8h, v20.8h, v28.8h
        sqadd           v21.8h, v21.8h, v29.8h
        sqrshrun        v4.8b, v16.8h, #7
        sqrshrun2       v4.16b, v17.8h, #7
        sqrshrun        v5.8b, v18.8h, #7
        sqrshrun2       v5.16b, v19.8h, #7
        sqrshrun        v6.8b, v20.8h, #7
        sqrshrun2       v6.16b, v21.8h, #7
        st1             {v4.16b, v5.16b, v6.16b}, [x0], x1
        subs            w5, w5, #1   // height
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_bi_h64_8_neon, export=1
        load_epel_filterb x6, x7
        sub             x2, x2, #1
1:      ld1             {v24.16b, v25.16b, v26.16b, v27.16b}, [x2]
        ldr             s28, [x2, #64]
        add             x2, x2, x3
        ext             v29.16b, v24.16b, v25.16b, #1
        ext             v30.16b, v24.16b, v25.16b, #2
        ext             v31.16b, v24.16b, v25.16b, #3
        calc_epelb      v16, v24, v29, v30, v31
        calc_epelb2     v17, v24, v29, v30, v31
        ext             v29.16b, v25.16b, v26.16b, #1
        ext             v30.16b, v25.16b, v26.16b, #2
        ext             v31.16b, v25.16b, v26.16b, #3
        calc_epelb      v18, v25, v29, v30, v31
        calc_epelb2     v19, v25, v29, v30, v31
        ld1             {v4.8h, v5.8h, v6.8h, v7.8h}, [x4], #64
        sqadd           v16.8h, v16.8h, v4.8h
        sqadd           v17.8h, v17.8h, v5.8h
        sqadd           v18.8h, v18.8h, v6.8h
        sqadd           v19.8h, v19.8h, v7.8h
        sqrshrun        v16.8b, v16.8h, #7
        sqrshrun2       v16.16b, v17.8h, #7
        sqrshrun        v17.8b, v18.8h, #7
        sqrshrun2       v17.16b, v19.8h, #7

        ext             v29.16b, v26.16b, v27.16b, #1
        ext             v30.16b, v26.16b, v27.16b, #2
        ext             v31.16b, v26.16b, v27.16b, #3
        calc_epelb      v20, v26, v29, v30, v31
        calc_epelb2     v21, v26, v29, v30, v31
        ext             v29.16b, v27.16b, v28.16b, #1
        ext             v30.16b, v27.16b, v28.16b, #2
        ext             v31.16b, v27.16b, v28.16b, #3
        calc_epelb      v22, v27, v29, v30, v31
        calc_epelb2     v23, v27, v29, v30, v31
        ld1             {v4.8h, v5.8h, v6.8h, v7.8h}, [x4], #64
        sqadd           v20.8h, v20.8h, v4.8h
        sqadd           v21.8h, v21.8h, v5.8h
        sqadd           v22.8h, v22.8h, v6.8h
        sqadd           v23.8h, v23.8h, v7.8h
        sqrshrun        v18.8b, v20.8h, #7
        sqrshrun2       v18.16b, v21.8h, #7
        sqrshrun        v19.8b, v22.8h, #7
        sqrshrun2       v19.16b, v23.8h, #7
        st1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x0], x1
        subs            w5, w5, #1
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_bi_v4_8_neon, export=1
        load_epel_filterb x7, x6
        sub             x2, x2, x3
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.s}[0], [x2], x3
        ld1             {v17.s}[0], [x2], x3
        ld1             {v18.s}[0], [x2], x3
.macro calc src0, src1, src2, src3
        ld1             {\src3\().s}[0], [x2], x3
        calc_epelb      v4, \src0, \src1, \src2, \src3
        ld1             {v24.4h}, [x4], x10
        sqadd           v4.8h, v4.8h, v24.8h
        sqrshrun        v4.8b, v4.8h, #7
        subs            w5, w5, #1
        st1             {v4.s}[0], [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_bi_v6_8_neon, export=1
        load_epel_filterb x7, x6
        sub             x2, x2, x3
        sub             x1, x1, #4
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8b}, [x2], x3
        ld1             {v17.8b}, [x2], x3
        ld1             {v18.8b}, [x2], x3
.macro calc src0, src1, src2, src3
        ld1             {\src3\().8b}, [x2], x3
        calc_epelb      v4, \src0, \src1, \src2, \src3
        ld1             {v24.8h}, [x4], x10
        sqadd           v4.8h, v4.8h, v24.8h
        sqrshrun        v4.8b, v4.8h, #7
        st1             {v4.s}[0], [x0], #4
        subs            w5, w5, #1
        st1             {v4.h}[2], [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_bi_v8_8_neon, export=1
        load_epel_filterb x7, x6
        sub             x2, x2, x3
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8b}, [x2], x3
        ld1             {v17.8b}, [x2], x3
        ld1             {v18.8b}, [x2], x3
.macro calc src0, src1, src2, src3
        ld1             {\src3\().8b}, [x2], x3
        calc_epelb      v4, \src0, \src1, \src2, \src3
        ld1             {v24.8h}, [x4], x10
        sqadd           v4.8h, v4.8h, v24.8h
        sqrshrun        v4.8b, v4.8h, #7
        subs            w5, w5, #1
        st1             {v4.8b}, [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_bi_v12_8_neon, export=1
        load_epel_filterb x7, x6
        sub             x1, x1, #8
        sub             x2, x2, x3
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.16b}, [x2], x3
        ld1             {v17.16b}, [x2], x3
        ld1             {v18.16b}, [x2], x3
.macro calc src0, src1, src2, src3
        ld1             {\src3\().16b}, [x2], x3
        calc_epelb      v4, \src0, \src1, \src2, \src3
        calc_epelb2     v5, \src0, \src1, \src2, \src3
        ld1             {v24.8h, v25.8h}, [x4], x10
        sqadd           v4.8h, v4.8h, v24.8h
        sqadd           v5.8h, v5.8h, v25.8h
        sqrshrun        v4.8b, v4.8h, #7
        sqrshrun2       v4.16b, v5.8h, #7
        st1             {v4.8b}, [x0], #8
        subs            w5, w5, #1
        st1             {v4.s}[2], [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_bi_v16_8_neon, export=1
        load_epel_filterb x7, x6
        sub             x2, x2, x3
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.16b}, [x2], x3
        ld1             {v17.16b}, [x2], x3
        ld1             {v18.16b}, [x2], x3
.macro calc src0, src1, src2, src3
        ld1             {\src3\().16b}, [x2], x3
        calc_epelb      v4, \src0, \src1, \src2, \src3
        calc_epelb2     v5, \src0, \src1, \src2, \src3
        ld1             {v24.8h, v25.8h}, [x4], x10
        sqadd           v4.8h, v4.8h, v24.8h
        sqadd           v5.8h, v5.8h, v25.8h
        sqrshrun        v4.8b, v4.8h, #7
        sqrshrun2       v4.16b, v5.8h, #7
        st1             {v4.16b}, [x0], x1
        subs            w5, w5, #1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_bi_v24_8_neon, export=1
        load_epel_filterb x7, x6
        sub             x2, x2, x3
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8b, v17.8b, v18.8b}, [x2], x3
        ld1             {v19.8b, v20.8b, v21.8b}, [x2], x3
        ld1             {v22.8b, v23.8b, v24.8b}, [x2], x3
.macro calc src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11
        ld1             {\src9\().8b, \src10\().8b, \src11\().8b}, [x2], x3
        calc_epelb      v4, \src0, \src3, \src6, \src9
        calc_epelb      v5, \src1, \src4, \src7, \src10
        calc_epelb      v6, \src2, \src5, \src8, \src11
        ld1             {v28.8h, v29.8h, v30.8h}, [x4], x10
        sqadd           v4.8h, v4.8h, v28.8h
        sqadd           v5.8h, v5.8h, v29.8h
        sqadd           v6.8h, v6.8h, v30.8h
        sqrshrun        v4.8b, v4.8h, #7
        sqrshrun        v5.8b, v5.8h, #7
        sqrshrun        v6.8b, v6.8h, #7
        subs            w5, w5, #1
        st1             {v4.8b, v5.8b, v6.8b}, [x0], x1
.endm
1:      calc_all12
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_bi_v32_8_neon, export=1
        load_epel_filterb x7, x6
        sub             x2, x2, x3
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.16b, v17.16b}, [x2], x3
        ld1             {v18.16b, v19.16b}, [x2], x3
        ld1             {v20.16b, v21.16b}, [x2], x3
.macro calc src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\src6\().16b, \src7\().16b}, [x2], x3
        calc_epelb      v4, \src0, \src2, \src4, \src6
        calc_epelb2     v5, \src0, \src2, \src4, \src6
        calc_epelb      v6, \src1, \src3, \src5, \src7
        calc_epelb2     v7, \src1, \src3, \src5, \src7
        ld1             {v24.8h-v27.8h}, [x4], x10
        sqadd           v4.8h, v4.8h, v24.8h
        sqadd           v5.8h, v5.8h, v25.8h
        sqadd           v6.8h, v6.8h, v26.8h
        sqadd           v7.8h, v7.8h, v27.8h
        sqrshrun        v4.8b, v4.8h, #7
        sqrshrun2       v4.16b, v5.8h, #7
        sqrshrun        v5.8b, v6.8h, #7
        sqrshrun2       v5.16b, v7.8h, #7
        st1             {v4.16b, v5.16b}, [x0], x1
        subs            w5, w5, #1
.endm
1:      calc_all8
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_bi_v48_8_neon, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        stp             x7, x30, [sp, #48]
        bl              X(ff_hevc_put_hevc_epel_bi_v24_8_neon)
        ldp             x4, x5, [sp]
        ldp             x2, x3, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x7, [sp, #48]
        add             sp, sp, #48
        add             x0, x0, #24
        add             x2, x2, #24
        add             x4, x4, #48
        bl              X(ff_hevc_put_hevc_epel_bi_v24_8_neon)
        ldr             x30, [sp, #8]
        add             sp, sp, #16
        ret
endfunc

function ff_hevc_put_hevc_epel_bi_v64_8_neon, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        stp             x7, x30, [sp, #48]
        bl              X(ff_hevc_put_hevc_epel_bi_v32_8_neon)
        ldp             x4, x5, [sp]
        ldp             x2, x3, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x7, [sp, #48]
        add             sp, sp, #48
        add             x0, x0, #32
        add             x2, x2, #32
        add             x4, x4, #64
        bl              X(ff_hevc_put_hevc_epel_bi_v32_8_neon)
        ldr             x30, [sp, #8]
        add             sp, sp, #16
        ret
endfunc

function ff_hevc_put_hevc_epel_v4_8_neon, export=1
        load_epel_filterb x5, x4
        sub             x1, x1, x2
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ldr             s16, [x1]
        ldr             s17, [x1, x2]
        add             x1, x1, x2, lsl #1
        ld1             {v18.s}[0], [x1], x2
.macro calc src0, src1, src2, src3
        ld1             {\src3\().s}[0], [x1], x2
        movi            v4.8h, #0
        calc_epelb      v4, \src0, \src1, \src2, \src3
        subs            w3, w3, #1
        st1             {v4.4h}, [x0], x10
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_v6_8_neon, export=1
        load_epel_filterb x5, x4
        sub             x1, x1, x2
        mov             x10, #(HEVC_MAX_PB_SIZE * 2 - 8)
        ldr             d16, [x1]
        ldr             d17, [x1, x2]
        add             x1, x1, x2, lsl #1
        ld1             {v18.8b}, [x1], x2
.macro calc src0, src1, src2, src3
        ld1             {\src3\().8b}, [x1], x2
        movi            v4.8h, #0
        calc_epelb      v4, \src0, \src1, \src2, \src3
        st1             {v4.d}[0], [x0], #8
        subs            w3, w3, #1
        st1             {v4.s}[2], [x0], x10
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_v8_8_neon, export=1
        load_epel_filterb x5, x4
        sub             x1, x1, x2
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ldr             d16, [x1]
        ldr             d17, [x1, x2]
        add             x1, x1, x2, lsl #1
        ld1             {v18.8b}, [x1], x2
.macro calc src0, src1, src2, src3
        ld1             {\src3\().8b}, [x1], x2
        movi            v4.8h, #0
        calc_epelb      v4, \src0, \src1, \src2, \src3
        subs            w3, w3, #1
        st1             {v4.8h}, [x0], x10
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_v12_8_neon, export=1
        load_epel_filterb x5, x4
        sub             x1, x1, x2
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ldr             q16, [x1]
        ldr             q17, [x1, x2]
        add             x1, x1, x2, lsl #1
        ld1             {v18.16b}, [x1], x2
.macro calc src0, src1, src2, src3
        ld1             {\src3\().16b}, [x1], x2
        movi            v4.8h, #0
        movi            v5.8h, #0
        calc_epelb      v4, \src0, \src1, \src2, \src3
        calc_epelb2     v5, \src0, \src1, \src2, \src3
        str             q4, [x0]
        subs            w3, w3, #1
        str             d5, [x0, #16]
        add             x0, x0, x10
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_v16_8_neon, export=1
        load_epel_filterb x5, x4
        sub             x1, x1, x2
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ldr             q16, [x1]
        ldr             q17, [x1, x2]
        add             x1, x1, x2, lsl #1
        ld1             {v18.16b}, [x1], x2
.macro calc src0, src1, src2, src3
        ld1             {\src3\().16b}, [x1], x2
        movi            v4.8h, #0
        movi            v5.8h, #0
        calc_epelb      v4, \src0, \src1, \src2, \src3
        calc_epelb2     v5, \src0, \src1, \src2, \src3
        subs            w3, w3, #1
        st1             {v4.8h, v5.8h}, [x0], x10
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_v24_8_neon, export=1
        load_epel_filterb x5, x4
        sub             x1, x1, x2
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8b, v17.8b, v18.8b}, [x1], x2
        ld1             {v19.8b, v20.8b, v21.8b}, [x1], x2
        ld1             {v22.8b, v23.8b, v24.8b}, [x1], x2
.macro calc src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11
        ld1             {\src9\().8b, \src10\().8b, \src11\().8b}, [x1], x2
        movi            v4.8h, #0
        movi            v5.8h, #0
        movi            v6.8h, #0
        calc_epelb      v4, \src0, \src3, \src6, \src9
        calc_epelb      v5, \src1, \src4, \src7, \src10
        calc_epelb      v6, \src2, \src5, \src8, \src11
        subs            w3, w3, #1
        st1             {v4.8h-v6.8h}, [x0], x10
.endm
1:      calc_all12
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_v32_8_neon, export=1
        load_epel_filterb x5, x4
        sub             x1, x1, x2
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.16b, v17.16b}, [x1], x2
        ld1             {v18.16b, v19.16b}, [x1], x2
        ld1             {v20.16b, v21.16b}, [x1], x2
.macro calc src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\src6\().16b, \src7\().16b}, [x1], x2
        movi            v4.8h, #0
        movi            v5.8h, #0
        movi            v6.8h, #0
        movi            v7.8h, #0
        calc_epelb      v4, \src0, \src2, \src4, \src6
        calc_epelb2     v5, \src0, \src2, \src4, \src6
        calc_epelb      v6, \src1, \src3, \src5, \src7
        calc_epelb2     v7, \src1, \src3, \src5, \src7
        subs            w3, w3, #1
        st1             {v4.8h-v7.8h}, [x0], x10
.endm
1:      calc_all8
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_v48_8_neon, export=1
        load_epel_filterb x5, x4
        sub             x1, x1, x2
        mov             x10, #64
        ld1             {v16.16b, v17.16b, v18.16b}, [x1], x2
        ld1             {v19.16b, v20.16b, v21.16b}, [x1], x2
        ld1             {v22.16b, v23.16b, v24.16b}, [x1], x2
.macro calc src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11
        ld1             {\src9\().16b, \src10\().16b, \src11\().16b}, [x1], x2
        movi            v4.8h, #0
        movi            v5.8h, #0
        movi            v6.8h, #0
        movi            v7.8h, #0
        movi            v28.8h, #0
        movi            v29.8h, #0
        calc_epelb      v4,  \src0, \src3, \src6, \src9
        calc_epelb2     v5,  \src0, \src3, \src6, \src9
        calc_epelb      v6,  \src1, \src4, \src7, \src10
        calc_epelb2     v7,  \src1, \src4, \src7, \src10
        calc_epelb      v28, \src2, \src5, \src8, \src11
        calc_epelb2     v29, \src2, \src5, \src8, \src11
        st1             {v4.8h-v7.8h}, [x0], #64
        subs            w3, w3, #1
        st1             {v28.8h-v29.8h}, [x0], x10
.endm
1:      calc_all12
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_v64_8_neon, export=1
        load_epel_filterb x5, x4
        sub             sp, sp, #32
        st1             {v8.8b-v11.8b}, [sp]
        sub             x1, x1, x2
        ld1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x1], x2
        ld1             {v20.16b, v21.16b, v22.16b, v23.16b}, [x1], x2
        ld1             {v24.16b, v25.16b, v26.16b, v27.16b}, [x1], x2
.macro calc src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\src12\().16b-\src15\().16b}, [x1], x2
        movi            v4.8h, #0
        movi            v5.8h, #0
        movi            v6.8h, #0
        movi            v7.8h, #0
        movi            v8.8h, #0
        movi            v9.8h, #0
        movi            v10.8h, #0
        movi            v11.8h, #0
        calc_epelb      v4,  \src0, \src4, \src8,  \src12
        calc_epelb2     v5,  \src0, \src4, \src8,  \src12
        calc_epelb      v6,  \src1, \src5, \src9,  \src13
        calc_epelb2     v7,  \src1, \src5, \src9,  \src13
        calc_epelb      v8,  \src2, \src6, \src10, \src14
        calc_epelb2     v9,  \src2, \src6, \src10, \src14
        calc_epelb      v10, \src3, \src7, \src11, \src15
        calc_epelb2     v11, \src3, \src7, \src11, \src15
        st1             {v4.8h-v7.8h}, [x0], #64
        subs            w3, w3, #1
        st1             {v8.8h-v11.8h}, [x0], #64
.endm
1:      calc_all16
.purgem calc
2:      ld1             {v8.8b-v11.8b}, [sp]
        add             sp, sp, #32
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_v4_8_neon, export=1
        load_epel_filterb x6, x5
        sub             x2, x2, x3
        ld1             {v16.s}[0], [x2], x3
        ld1             {v17.s}[0], [x2], x3
        ld1             {v18.s}[0], [x2], x3
.macro calc src0, src1, src2, src3
        ld1             {\src3\().s}[0], [x2], x3
        calc_epelb      v4, \src0, \src1, \src2, \src3
        sqrshrun        v4.8b, v4.8h, #6
        subs            w4, w4, #1
        st1             {v4.s}[0], [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_uni_v6_8_neon, export=1
        load_epel_filterb x6, x5
        sub             x2, x2, x3
        sub             x1, x1, #4
        ld1             {v16.8b}, [x2], x3
        ld1             {v17.8b}, [x2], x3
        ld1             {v18.8b}, [x2], x3
.macro calc src0, src1, src2, src3
        ld1             {\src3\().8b}, [x2], x3
        calc_epelb      v4, \src0, \src1, \src2, \src3
        sqrshrun        v4.8b, v4.8h, #6
        st1             {v4.s}[0], [x0], #4
        subs            w4, w4, #1
        st1             {v4.h}[2], [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_uni_v8_8_neon, export=1
        load_epel_filterb x6, x5
        sub             x2, x2, x3
        ld1             {v16.8b}, [x2], x3
        ld1             {v17.8b}, [x2], x3
        ld1             {v18.8b}, [x2], x3
.macro calc src0, src1, src2, src3
        ld1             {\src3\().8b}, [x2], x3
        calc_epelb      v4, \src0, \src1, \src2, \src3
        sqrshrun        v4.8b,  v4.8h, #6
        subs            w4, w4, #1
        st1             {v4.8b}, [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_uni_v12_8_neon, export=1
        load_epel_filterb x6, x5
        sub             x2, x2, x3
        sub             x1, x1, #8
        ld1             {v16.16b}, [x2], x3
        ld1             {v17.16b}, [x2], x3
        ld1             {v18.16b}, [x2], x3
.macro calc src0, src1, src2, src3
        ld1             {\src3\().16b}, [x2], x3
        calc_epelb      v4, \src0, \src1, \src2, \src3
        calc_epelb2     v5, \src0, \src1, \src2, \src3
        sqrshrun        v4.8b,  v4.8h, #6
        sqrshrun2       v4.16b,  v5.8h, #6
        subs            w4, w4, #1
        st1             {v4.8b}, [x0], #8
        st1             {v4.s}[2], [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_uni_v16_8_neon, export=1
        load_epel_filterb x6, x5
        sub             x2, x2, x3
        ld1             {v16.16b}, [x2], x3
        ld1             {v17.16b}, [x2], x3
        ld1             {v18.16b}, [x2], x3
.macro calc src0, src1, src2, src3
        ld1             {\src3\().16b}, [x2], x3
        calc_epelb      v4, \src0, \src1, \src2, \src3
        calc_epelb2     v5, \src0, \src1, \src2, \src3
        sqrshrun        v4.8b,  v4.8h, #6
        sqrshrun2       v4.16b,  v5.8h, #6
        subs            w4, w4, #1
        st1             {v4.16b}, [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_uni_v24_8_neon, export=1
        load_epel_filterb x6, x5
        sub             x2, x2, x3
        ld1             {v16.8b, v17.8b, v18.8b}, [x2], x3
        ld1             {v19.8b, v20.8b, v21.8b}, [x2], x3
        ld1             {v22.8b, v23.8b, v24.8b}, [x2], x3
.macro calc src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11
        ld1             {\src9\().8b, \src10\().8b, \src11\().8b}, [x2], x3
        calc_epelb      v4, \src0, \src3, \src6, \src9
        calc_epelb      v5, \src1, \src4, \src7, \src10
        calc_epelb      v6, \src2, \src5, \src8, \src11
        sqrshrun        v4.8b,  v4.8h, #6
        sqrshrun        v5.8b,  v5.8h, #6
        sqrshrun        v6.8b,  v6.8h, #6
        subs            w4, w4, #1
        st1             {v4.8b-v6.8b}, [x0], x1
.endm
1:      calc_all12
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_uni_v32_8_neon, export=1
        load_epel_filterb x6, x5
        sub             x2, x2, x3
        ld1             {v16.16b, v17.16b}, [x2], x3
        ld1             {v18.16b, v19.16b}, [x2], x3
        ld1             {v20.16b, v21.16b}, [x2], x3
.macro calc src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\src6\().16b, \src7\().16b}, [x2], x3
        calc_epelb      v4, \src0, \src2, \src4, \src6
        calc_epelb2     v5, \src0, \src2, \src4, \src6
        calc_epelb      v6, \src1, \src3, \src5, \src7
        calc_epelb2     v7, \src1, \src3, \src5, \src7
        sqrshrun        v4.8b,  v4.8h, #6
        sqrshrun2       v4.16b, v5.8h, #6
        sqrshrun        v5.8b,  v6.8h, #6
        sqrshrun2       v5.16b, v7.8h, #6
        subs            w4, w4, #1
        st1             {v4.16b, v5.16b}, [x0], x1
.endm
1:      calc_all8
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_uni_v48_8_neon, export=1
        load_epel_filterb x6, x5
        sub             x2, x2, x3
        ld1             {v16.16b, v17.16b, v18.16b}, [x2], x3
        ld1             {v19.16b, v20.16b, v21.16b}, [x2], x3
        ld1             {v22.16b, v23.16b, v24.16b}, [x2], x3
.macro calc src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11
        ld1             {\src9\().16b, \src10\().16b, \src11\().16b}, [x2], x3
        calc_epelb      v4,  \src0, \src3, \src6, \src9
        calc_epelb2     v5,  \src0, \src3, \src6, \src9
        calc_epelb      v6,  \src1, \src4, \src7, \src10
        calc_epelb2     v7,  \src1, \src4, \src7, \src10
        calc_epelb      v28, \src2, \src5, \src8, \src11
        calc_epelb2     v29, \src2, \src5, \src8, \src11
        sqrshrun        v4.8b,  v4.8h, #6
        sqrshrun2       v4.16b, v5.8h, #6
        sqrshrun        v5.8b,  v6.8h, #6
        sqrshrun2       v5.16b, v7.8h, #6
        sqrshrun        v6.8b,  v28.8h, #6
        sqrshrun2       v6.16b, v29.8h, #6
        subs            w4, w4, #1
        st1             {v4.16b, v5.16b, v6.16b}, [x0], x1
.endm
1:      calc_all12
.purgem calc
2:      ret
endfunc

function ff_hevc_put_hevc_epel_uni_v64_8_neon, export=1
        load_epel_filterb x6, x5
        sub             sp, sp, #32
        st1             {v8.8b-v11.8b}, [sp]
        sub             x2, x2, x3
        ld1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x2], x3
        ld1             {v20.16b, v21.16b, v22.16b, v23.16b}, [x2], x3
        ld1             {v24.16b, v25.16b, v26.16b, v27.16b}, [x2], x3
.macro calc src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\src12\().16b, \src13\().16b, \src14\().16b, \src15\().16b}, [x2], x3
        calc_epelb      v10, \src3, \src7, \src11, \src15
        calc_epelb2     v11, \src3, \src7, \src11, \src15
        calc_epelb      v4,  \src0, \src4, \src8,  \src12
        calc_epelb2     v5,  \src0, \src4, \src8,  \src12
        calc_epelb      v6,  \src1, \src5, \src9,  \src13
        calc_epelb2     v7,  \src1, \src5, \src9,  \src13
        calc_epelb      v8,  \src2, \src6, \src10, \src14
        calc_epelb2     v9,  \src2, \src6, \src10, \src14
        sqrshrun        v4.8b,  v4.8h, #6
        sqrshrun2       v4.16b, v5.8h, #6
        sqrshrun        v5.8b,  v6.8h, #6
        sqrshrun2       v5.16b, v7.8h, #6
        sqrshrun        v6.8b,  v8.8h, #6
        sqrshrun2       v6.16b, v9.8h, #6
        sqrshrun        v7.8b,  v10.8h, #6
        sqrshrun2       v7.16b, v11.8h, #6
        subs            w4, w4, #1
        st1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x0], x1
.endm
1:      calc_all16
.purgem calc
2:      ld1             {v8.8b-v11.8b}, [sp], #32
        ret
endfunc


.macro EPEL_H_HEADER
        movrel          x5, epel_filters
        add             x5, x5, x4, lsl #2
        ld1r            {v30.4s}, [x5]
        sub             x1, x1, #1
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
.endm

.macro VVC_EPEL_H_HEADER
        ld1r            {v30.4s}, [x4]
        sub             x1, x1, #1
        mov             x10, #(VVC_MAX_PB_SIZE * 2)
.endm

function ff_vvc_put_epel_h4_8_neon, export=1
        VVC_EPEL_H_HEADER
        sxtl            v0.8h,   v30.8b
        b               1f
endfunc

function ff_hevc_put_hevc_epel_h4_8_neon, export=1
        EPEL_H_HEADER
        sxtl            v0.8h,   v30.8b
1:      ld1             {v4.8b}, [x1], x2
        subs            w3,  w3,  #1   // height
        uxtl            v4.8h,   v4.8b
        ext             v5.16b,  v4.16b,  v4.16b,  #2
        ext             v6.16b,  v4.16b,  v4.16b,  #4
        ext             v7.16b,  v4.16b,  v4.16b,  #6
        mul             v16.4h,  v4.4h,   v0.h[0]
        mla             v16.4h,  v5.4h,   v0.h[1]
        mla             v16.4h,  v6.4h,   v0.h[2]
        mla             v16.4h,  v7.4h,   v0.h[3]
        st1             {v16.4h}, [x0], x10
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_h6_8_neon, export=1
        EPEL_H_HEADER
        sxtl            v0.8h,   v30.8b
        add             x6,  x0,  #8
1:      ld1             {v3.16b},  [x1], x2
        subs            w3,  w3,  #1   // height
        uxtl2           v4.8h,   v3.16b
        uxtl            v3.8h,   v3.8b
        ext             v5.16b,  v3.16b,  v4.16b,  #2
        ext             v6.16b,  v3.16b,  v4.16b,  #4
        ext             v7.16b,  v3.16b,  v4.16b,  #6
        mul             v16.8h,  v3.8h,   v0.h[0]
        mla             v16.8h,  v5.8h,   v0.h[1]
        mla             v16.8h,  v6.8h,   v0.h[2]
        mla             v16.8h,  v7.8h,   v0.h[3]
        st1             {v16.4h},   [x0], x10
        st1             {v16.s}[2], [x6], x10
        b.ne            1b
        ret
endfunc

function ff_vvc_put_epel_h8_8_neon, export=1
        VVC_EPEL_H_HEADER
        sxtl            v0.8h,   v30.8b
        b               1f
endfunc

function ff_hevc_put_hevc_epel_h8_8_neon, export=1
        EPEL_H_HEADER
        sxtl            v0.8h,   v30.8b
1:      ld1             {v3.16b},  [x1], x2
        subs            w3,  w3,  #1   // height
        uxtl2           v4.8h,   v3.16b
        uxtl            v3.8h,   v3.8b
        ext             v5.16b,  v3.16b,  v4.16b,  #2
        ext             v6.16b,  v3.16b,  v4.16b,  #4
        ext             v7.16b,  v3.16b,  v4.16b,  #6
        mul             v16.8h,  v3.8h,   v0.h[0]
        mla             v16.8h,  v5.8h,   v0.h[1]
        mla             v16.8h,  v6.8h,   v0.h[2]
        mla             v16.8h,  v7.8h,   v0.h[3]
        st1             {v16.8h},   [x0], x10
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_h12_8_neon, export=1
        EPEL_H_HEADER
        add             x6,  x0,  #16
        sxtl            v0.8h,   v30.8b
1:      ld1             {v3.16b}, [x1], x2
        subs            w3,  w3,  #1   // height
        uxtl2           v4.8h,   v3.16b
        uxtl            v3.8h,   v3.8b
        ext             v5.16b,  v3.16b,  v4.16b,  #2
        ext             v6.16b,  v3.16b,  v4.16b,  #4
        ext             v7.16b,  v3.16b,  v4.16b,  #6
        ext             v20.16b, v4.16b,  v4.16b,  #2
        ext             v21.16b, v4.16b,  v4.16b,  #4
        ext             v22.16b, v4.16b,  v4.16b,  #6
        mul             v16.8h,  v3.8h,   v0.h[0]
        mla             v16.8h,  v5.8h,   v0.h[1]
        mla             v16.8h,  v6.8h,   v0.h[2]
        mla             v16.8h,  v7.8h,   v0.h[3]
        mul             v17.4h,  v4.4h,   v0.h[0]
        mla             v17.4h,  v20.4h,  v0.h[1]
        mla             v17.4h,  v21.4h,  v0.h[2]
        mla             v17.4h,  v22.4h,  v0.h[3]
        st1             {v16.8h}, [x0], x10
        st1             {v17.4h}, [x6], x10
        b.ne            1b
        ret
endfunc

function ff_vvc_put_epel_h16_8_neon, export=1
        VVC_EPEL_H_HEADER
        sxtl            v0.8h,   v30.8b
        b               1f
endfunc

function ff_hevc_put_hevc_epel_h16_8_neon, export=1
        EPEL_H_HEADER
        sxtl            v0.8h,   v30.8b
1:      ld1             {v1.8b, v2.8b, v3.8b}, [x1], x2
        subs            w3,  w3,  #1   // height
        uxtl            v1.8h,   v1.8b
        uxtl            v2.8h,   v2.8b
        uxtl            v3.8h,   v3.8b
        ext             v5.16b,  v1.16b,  v2.16b,  #2
        ext             v6.16b,  v1.16b,  v2.16b,  #4
        ext             v7.16b,  v1.16b,  v2.16b,  #6
        ext             v20.16b, v2.16b,  v3.16b,  #2
        ext             v21.16b, v2.16b,  v3.16b,  #4
        ext             v22.16b, v2.16b,  v3.16b,  #6
        mul             v16.8h,  v1.8h,   v0.h[0]
        mla             v16.8h,  v5.8h,   v0.h[1]
        mla             v16.8h,  v6.8h,   v0.h[2]
        mla             v16.8h,  v7.8h,   v0.h[3]
        mul             v17.8h,  v2.8h,   v0.h[0]
        mla             v17.8h,  v20.8h,  v0.h[1]
        mla             v17.8h,  v21.8h,  v0.h[2]
        mla             v17.8h,  v22.8h,  v0.h[3]
        st1             {v16.8h, v17.8h}, [x0], x10
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_h24_8_neon, export=1
        EPEL_H_HEADER
        sxtl            v0.8h,   v30.8b
1:      ld1             {v1.8b, v2.8b, v3.8b, v4.8b}, [x1], x2
        subs            w3,  w3,  #1   // height
        uxtl            v1.8h,   v1.8b
        uxtl            v2.8h,   v2.8b
        uxtl            v3.8h,   v3.8b
        uxtl            v4.8h,   v4.8b
        ext             v5.16b,  v1.16b,  v2.16b,  #2
        ext             v6.16b,  v1.16b,  v2.16b,  #4
        ext             v7.16b,  v1.16b,  v2.16b,  #6
        ext             v20.16b, v2.16b,  v3.16b,  #2
        ext             v21.16b, v2.16b,  v3.16b,  #4
        ext             v22.16b, v2.16b,  v3.16b,  #6
        ext             v23.16b, v3.16b,  v4.16b,  #2
        ext             v24.16b, v3.16b,  v4.16b,  #4
        ext             v25.16b, v3.16b,  v4.16b,  #6
        mul             v16.8h,  v1.8h,   v0.h[0]
        mla             v16.8h,  v5.8h,   v0.h[1]
        mla             v16.8h,  v6.8h,   v0.h[2]
        mla             v16.8h,  v7.8h,   v0.h[3]
        mul             v17.8h,  v2.8h,   v0.h[0]
        mla             v17.8h,  v20.8h,  v0.h[1]
        mla             v17.8h,  v21.8h,  v0.h[2]
        mla             v17.8h,  v22.8h,  v0.h[3]
        mul             v18.8h,  v3.8h,   v0.h[0]
        mla             v18.8h,  v23.8h,  v0.h[1]
        mla             v18.8h,  v24.8h,  v0.h[2]
        mla             v18.8h,  v25.8h,  v0.h[3]
        st1             {v16.8h, v17.8h, v18.8h}, [x0], x10
        b.ne            1b
        ret
endfunc

function ff_vvc_put_epel_h32_8_neon, export=1
        VVC_EPEL_H_HEADER
        b               0f
endfunc

function ff_hevc_put_hevc_epel_h32_8_neon, export=1
        EPEL_H_HEADER
0:
        ld1             {v1.8b}, [x1], #8
        sub             x2,  x2,  w6, uxtw    // decrement src stride
        mov             w7,  w6               // original width
        sub             x2,  x2,  #8          // decrement src stride
        sub             x10, x10, w6, uxtw #1 // decrement dst stride
        sxtl            v0.8h,   v30.8b
        uxtl            v1.8h,   v1.8b
1:      ld1             {v2.8b, v3.8b}, [x1], #16
        subs            w6,  w6,  #16   // width
        uxtl            v2.8h,   v2.8b
        uxtl            v3.8h,   v3.8b
        ext             v5.16b,  v1.16b,  v2.16b,  #2
        ext             v6.16b,  v1.16b,  v2.16b,  #4
        ext             v7.16b,  v1.16b,  v2.16b,  #6
        ext             v20.16b, v2.16b,  v3.16b,  #2
        ext             v21.16b, v2.16b,  v3.16b,  #4
        ext             v22.16b, v2.16b,  v3.16b,  #6
        mul             v16.8h,  v1.8h,   v0.h[0]
        mla             v16.8h,  v5.8h,   v0.h[1]
        mla             v16.8h,  v6.8h,   v0.h[2]
        mla             v16.8h,  v7.8h,   v0.h[3]
        mul             v17.8h,  v2.8h,   v0.h[0]
        mla             v17.8h,  v20.8h,  v0.h[1]
        mla             v17.8h,  v21.8h,  v0.h[2]
        mla             v17.8h,  v22.8h,  v0.h[3]
        st1             {v16.8h, v17.8h}, [x0], #32
        mov             v1.16b,  v3.16b
        b.gt            1b
        subs            w3,  w3,  #1   // height
        add             x1,  x1,  x2
        b.le            9f
        ld1             {v1.8b}, [x1], #8
        mov             w6,  w7
        add             x0,  x0,  x10
        uxtl            v1.8h,   v1.8b
        b               1b
9:
        ret
endfunc

.macro EPEL_UNI_W_H_HEADER elems=4s
        ldr             x12, [sp]
        sub             x2, x2, #1
        movrel          x9, epel_filters
        add             x9, x9, x12, lsl #2
        ld1r            {v28.4s}, [x9]
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.\elems, w6
        dup             v31.4s, w10
        dup             v29.4s, w7
.endm

function ff_hevc_put_hevc_epel_uni_w_h4_8_neon, export=1
        EPEL_UNI_W_H_HEADER 4h
        sxtl            v0.8h,   v28.8b
1:
        ld1             {v4.8b}, [x2], x3
        subs            w4,  w4,  #1
        uxtl            v4.8h,   v4.8b
        ext             v5.16b,  v4.16b,  v4.16b,  #2
        ext             v6.16b,  v4.16b,  v4.16b,  #4
        ext             v7.16b,  v4.16b,  v4.16b,  #6
        mul             v16.4h,  v4.4h,   v0.h[0]
        mla             v16.4h,  v5.4h,   v0.h[1]
        mla             v16.4h,  v6.4h,   v0.h[2]
        mla             v16.4h,  v7.4h,   v0.h[3]
        smull           v16.4s,  v16.4h,  v30.4h
        sqrshl          v16.4s,  v16.4s,  v31.4s
        sqadd           v16.4s,  v16.4s,  v29.4s
        sqxtn           v16.4h,  v16.4s
        sqxtun          v16.8b,  v16.8h
        str             s16, [x0]
        add             x0,  x0,  x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_h6_8_neon, export=1
        EPEL_UNI_W_H_HEADER 8h
        sub             x1,  x1,  #4
        sxtl            v0.8h,   v28.8b
1:
        ld1             {v3.8b, v4.8b}, [x2], x3
        subs            w4,  w4,  #1
        uxtl            v3.8h,   v3.8b
        uxtl            v4.8h,   v4.8b
        ext             v5.16b,  v3.16b,  v4.16b,  #2
        ext             v6.16b,  v3.16b,  v4.16b,  #4
        ext             v7.16b,  v3.16b,  v4.16b,  #6
        mul             v16.8h,  v3.8h,   v0.h[0]
        mla             v16.8h,  v5.8h,   v0.h[1]
        mla             v16.8h,  v6.8h,   v0.h[2]
        mla             v16.8h,  v7.8h,   v0.h[3]
        smull           v17.4s,  v16.4h,  v30.4h
        smull2          v18.4s,  v16.8h,  v30.8h
        sqrshl          v17.4s,  v17.4s,  v31.4s
        sqrshl          v18.4s,  v18.4s,  v31.4s
        sqadd           v17.4s,  v17.4s,  v29.4s
        sqadd           v18.4s,  v18.4s,  v29.4s
        sqxtn           v16.4h,  v17.4s
        sqxtn2          v16.8h,  v18.4s
        sqxtun          v16.8b,  v16.8h
        str             s16, [x0], #4
        st1             {v16.h}[2], [x0], x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_h8_8_neon, export=1
        EPEL_UNI_W_H_HEADER 8h
        sxtl            v0.8h,   v28.8b
1:
        ld1             {v3.8b, v4.8b}, [x2], x3
        subs            w4,  w4,  #1
        uxtl            v3.8h,   v3.8b
        uxtl            v4.8h,   v4.8b
        ext             v5.16b,  v3.16b,  v4.16b,  #2
        ext             v6.16b,  v3.16b,  v4.16b,  #4
        ext             v7.16b,  v3.16b,  v4.16b,  #6
        mul             v16.8h,  v3.8h,   v0.h[0]
        mla             v16.8h,  v5.8h,   v0.h[1]
        mla             v16.8h,  v6.8h,   v0.h[2]
        mla             v16.8h,  v7.8h,   v0.h[3]
        smull           v17.4s,  v16.4h,  v30.4h
        smull2          v18.4s,  v16.8h,  v30.8h
        sqrshl          v17.4s,  v17.4s,  v31.4s
        sqrshl          v18.4s,  v18.4s,  v31.4s
        sqadd           v17.4s,  v17.4s,  v29.4s
        sqadd           v18.4s,  v18.4s,  v29.4s
        sqxtn           v16.4h,  v17.4s
        sqxtn2          v16.8h,  v18.4s
        sqxtun          v16.8b,  v16.8h
        st1             {v16.8b}, [x0], x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_h12_8_neon, export=1
        EPEL_UNI_W_H_HEADER 8h
        sxtl            v0.8h,   v28.8b
1:
        ld1             {v3.8b, v4.8b}, [x2], x3
        subs            w4,  w4,  #1
        uxtl            v3.8h,   v3.8b
        uxtl            v4.8h,   v4.8b
        ext             v5.16b,  v3.16b,  v4.16b,  #2
        ext             v6.16b,  v3.16b,  v4.16b,  #4
        ext             v7.16b,  v3.16b,  v4.16b,  #6
        ext             v20.16b, v4.16b,  v4.16b,  #2
        ext             v21.16b, v4.16b,  v4.16b,  #4
        ext             v22.16b, v4.16b,  v4.16b,  #6
        mul             v16.8h,  v3.8h,   v0.h[0]
        mla             v16.8h,  v5.8h,   v0.h[1]
        mla             v16.8h,  v6.8h,   v0.h[2]
        mla             v16.8h,  v7.8h,   v0.h[3]
        mul             v17.4h,  v4.4h,   v0.h[0]
        mla             v17.4h,  v20.4h,  v0.h[1]
        mla             v17.4h,  v21.4h,  v0.h[2]
        mla             v17.4h,  v22.4h,  v0.h[3]
        smull           v18.4s,  v16.4h,  v30.4h
        smull2          v19.4s,  v16.8h,  v30.8h
        smull           v20.4s,  v17.4h,  v30.4h
        sqrshl          v18.4s,  v18.4s,  v31.4s
        sqrshl          v19.4s,  v19.4s,  v31.4s
        sqrshl          v20.4s,  v20.4s,  v31.4s
        sqadd           v18.4s,  v18.4s,  v29.4s
        sqadd           v19.4s,  v19.4s,  v29.4s
        sqadd           v20.4s,  v20.4s,  v29.4s
        sqxtn           v16.4h,  v18.4s
        sqxtn2          v16.8h,  v19.4s
        sqxtn           v17.4h,  v20.4s
        sqxtun          v16.8b,  v16.8h
        sqxtun          v17.8b,  v17.8h
        str             d16, [x0]
        str             s17, [x0, #8]
        add             x0,  x0,  x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_h16_8_neon, export=1
        EPEL_UNI_W_H_HEADER 8h
        sxtl            v0.8h,   v28.8b
1:
        ld1             {v1.8b, v2.8b, v3.8b}, [x2], x3
        subs            w4,  w4,  #1
        uxtl            v1.8h,   v1.8b
        uxtl            v2.8h,   v2.8b
        uxtl            v3.8h,   v3.8b
        ext             v5.16b,  v1.16b,  v2.16b,  #2
        ext             v6.16b,  v1.16b,  v2.16b,  #4
        ext             v7.16b,  v1.16b,  v2.16b,  #6
        ext             v20.16b, v2.16b,  v3.16b,  #2
        ext             v21.16b, v2.16b,  v3.16b,  #4
        ext             v22.16b, v2.16b,  v3.16b,  #6
        mul             v16.8h,  v1.8h,   v0.h[0]
        mla             v16.8h,  v5.8h,   v0.h[1]
        mla             v16.8h,  v6.8h,   v0.h[2]
        mla             v16.8h,  v7.8h,   v0.h[3]
        mul             v17.8h,  v2.8h,   v0.h[0]
        mla             v17.8h,  v20.8h,  v0.h[1]
        mla             v17.8h,  v21.8h,  v0.h[2]
        mla             v17.8h,  v22.8h,  v0.h[3]
        smull           v18.4s,  v16.4h,  v30.4h
        smull2          v19.4s,  v16.8h,  v30.8h
        smull           v20.4s,  v17.4h,  v30.4h
        smull2          v21.4s,  v17.8h,  v30.8h
        sqrshl          v18.4s,  v18.4s,  v31.4s
        sqrshl          v19.4s,  v19.4s,  v31.4s
        sqrshl          v20.4s,  v20.4s,  v31.4s
        sqrshl          v21.4s,  v21.4s,  v31.4s
        sqadd           v18.4s,  v18.4s,  v29.4s
        sqadd           v19.4s,  v19.4s,  v29.4s
        sqadd           v20.4s,  v20.4s,  v29.4s
        sqadd           v21.4s,  v21.4s,  v29.4s
        sqxtn           v16.4h,  v18.4s
        sqxtn2          v16.8h,  v19.4s
        sqxtn           v17.4h,  v20.4s
        sqxtn2          v17.8h,  v21.4s
        sqxtun          v16.8b,  v16.8h
        sqxtun          v17.8b,  v17.8h
        st1             {v16.8b, v17.8b}, [x0], x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_h24_8_neon, export=1
        EPEL_UNI_W_H_HEADER 8h
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
        ext             v20.16b, v2.16b,  v3.16b,  #2
        ext             v21.16b, v2.16b,  v3.16b,  #4
        ext             v22.16b, v2.16b,  v3.16b,  #6
        ext             v23.16b, v3.16b,  v4.16b,  #2
        ext             v24.16b, v3.16b,  v4.16b,  #4
        ext             v25.16b, v3.16b,  v4.16b,  #6
        mul             v16.8h,  v1.8h,   v0.h[0]
        mla             v16.8h,  v5.8h,   v0.h[1]
        mla             v16.8h,  v6.8h,   v0.h[2]
        mla             v16.8h,  v7.8h,   v0.h[3]
        mul             v17.8h,  v2.8h,   v0.h[0]
        mla             v17.8h,  v20.8h,  v0.h[1]
        mla             v17.8h,  v21.8h,  v0.h[2]
        mla             v17.8h,  v22.8h,  v0.h[3]
        mul             v18.8h,  v3.8h,   v0.h[0]
        mla             v18.8h,  v23.8h,  v0.h[1]
        mla             v18.8h,  v24.8h,  v0.h[2]
        mla             v18.8h,  v25.8h,  v0.h[3]
        smull           v20.4s,  v16.4h,  v30.4h
        smull2          v21.4s,  v16.8h,  v30.8h
        smull           v22.4s,  v17.4h,  v30.4h
        smull2          v23.4s,  v17.8h,  v30.8h
        smull           v24.4s,  v18.4h,  v30.4h
        smull2          v25.4s,  v18.8h,  v30.8h
        sqrshl          v20.4s,  v20.4s,  v31.4s
        sqrshl          v21.4s,  v21.4s,  v31.4s
        sqrshl          v22.4s,  v22.4s,  v31.4s
        sqrshl          v23.4s,  v23.4s,  v31.4s
        sqrshl          v24.4s,  v24.4s,  v31.4s
        sqrshl          v25.4s,  v25.4s,  v31.4s
        sqadd           v20.4s,  v20.4s,  v29.4s
        sqadd           v21.4s,  v21.4s,  v29.4s
        sqadd           v22.4s,  v22.4s,  v29.4s
        sqadd           v23.4s,  v23.4s,  v29.4s
        sqadd           v24.4s,  v24.4s,  v29.4s
        sqadd           v25.4s,  v25.4s,  v29.4s
        sqxtn           v16.4h,  v20.4s
        sqxtn2          v16.8h,  v21.4s
        sqxtn           v17.4h,  v22.4s
        sqxtn2          v17.8h,  v23.4s
        sqxtn           v18.4h,  v24.4s
        sqxtn2          v18.8h,  v25.4s
        sqxtun          v16.8b,  v16.8h
        sqxtun          v17.8b,  v17.8h
        sqxtun          v18.8b,  v18.8h
        st1             {v16.8b, v17.8b, v18.8b}, [x0], x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_h32_8_neon, export=1
        EPEL_UNI_W_H_HEADER 8h
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
        ext             v5.16b,  v1.16b,  v2.16b,  #2
        ext             v6.16b,  v1.16b,  v2.16b,  #4
        ext             v7.16b,  v1.16b,  v2.16b,  #6
        ext             v20.16b, v2.16b,  v3.16b,  #2
        ext             v21.16b, v2.16b,  v3.16b,  #4
        ext             v22.16b, v2.16b,  v3.16b,  #6
        mul             v16.8h,  v1.8h,   v0.h[0]
        mla             v16.8h,  v5.8h,   v0.h[1]
        mla             v16.8h,  v6.8h,   v0.h[2]
        mla             v16.8h,  v7.8h,   v0.h[3]
        mul             v17.8h,  v2.8h,   v0.h[0]
        mla             v17.8h,  v20.8h,  v0.h[1]
        mla             v17.8h,  v21.8h,  v0.h[2]
        mla             v17.8h,  v22.8h,  v0.h[3]
        smull           v18.4s,  v16.4h,  v30.4h
        smull2          v19.4s,  v16.8h,  v30.8h
        smull           v20.4s,  v17.4h,  v30.4h
        smull2          v21.4s,  v17.8h,  v30.8h
        sqrshl          v18.4s,  v18.4s,  v31.4s
        sqrshl          v19.4s,  v19.4s,  v31.4s
        sqrshl          v20.4s,  v20.4s,  v31.4s
        sqrshl          v21.4s,  v21.4s,  v31.4s
        sqadd           v18.4s,  v18.4s,  v29.4s
        sqadd           v19.4s,  v19.4s,  v29.4s
        sqadd           v20.4s,  v20.4s,  v29.4s
        sqadd           v21.4s,  v21.4s,  v29.4s
        sqxtn           v16.4h,  v18.4s
        sqxtn2          v16.8h,  v19.4s
        sqxtn           v17.4h,  v20.4s
        sqxtn2          v17.8h,  v21.4s
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

function ff_vvc_put_epel_h4_8_neon_i8mm, export=1
        VVC_EPEL_H_HEADER
        b               1f
endfunc

function ff_hevc_put_hevc_epel_h4_8_neon_i8mm, export=1
        EPEL_H_HEADER
1:      ld1             {v4.8b}, [x1], x2
        subs            w3, w3, #1   // height
        ext             v5.8b, v4.8b, v4.8b, #1
        ext             v6.8b, v4.8b, v4.8b, #2
        ext             v7.8b, v4.8b, v4.8b, #3
        trn1            v4.2s, v4.2s, v5.2s
        trn1            v6.2s, v6.2s, v7.2s
        trn1            v4.2d, v4.2d, v6.2d
        movi            v16.16b, #0
        usdot           v16.4s, v4.16b, v30.16b
        xtn             v16.4h, v16.4s
        st1             {v16.4h}, [x0], x10
        b.ne            1b
        ret
endfunc


function ff_hevc_put_hevc_epel_h6_8_neon_i8mm, export=1
        EPEL_H_HEADER
1:      ld1             {v4.16b},  [x1], x2
        subs            w3, w3, #1   // height
        ext             v5.16b, v4.16b, v4.16b, #1
        ext             v6.8b, v4.8b, v4.8b, #2
        ext             v7.8b, v4.8b, v4.8b, #3
        trn1            v16.2s, v4.2s, v5.2s
        trn2            v17.2s, v4.2s, v5.2s
        trn1            v6.2s, v6.2s, v7.2s
        trn1            v16.2d, v16.2d, v6.2d
        movi            v18.16b, #0
        movi            v19.16b, #0
        usdot           v18.4s, v16.16b, v30.16b
        usdot           v19.2s, v17.8b, v30.8b
        xtn             v18.4h, v18.4s
        xtn             v19.4h, v19.4s
        str             d18, [x0]
        str             s19, [x0, #8]
        add             x0, x0, x10
        b.ne            1b
        ret
endfunc

function ff_vvc_put_epel_h8_8_neon_i8mm, export=1
        VVC_EPEL_H_HEADER
        b               1f
endfunc

function ff_hevc_put_hevc_epel_h8_8_neon_i8mm, export=1
        EPEL_H_HEADER
1:      ld1             {v4.16b}, [x1], x2
        subs            w3, w3, #1   // height
        ext             v5.16b, v4.16b, v4.16b, #1
        ext             v6.16b, v4.16b, v4.16b, #2
        ext             v7.16b, v4.16b, v4.16b, #3
        zip1            v20.4s, v4.4s, v6.4s
        zip1            v21.4s, v5.4s, v7.4s
        movi            v16.16b, #0
        movi            v17.16b, #0
        usdot           v16.4s, v20.16b, v30.16b
        usdot           v17.4s, v21.16b, v30.16b
        xtn             v16.4h, v16.4s
        xtn             v17.4h, v17.4s
        st2             {v16.4h, v17.4h}, [x0], x10
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_h12_8_neon_i8mm, export=1
        EPEL_H_HEADER
1:      ld1             {v4.16b}, [x1], x2
        subs            w3, w3, #1   // height
        ext             v5.16b, v4.16b, v4.16b, #1
        ext             v6.16b, v4.16b, v4.16b, #2
        ext             v7.16b, v4.16b, v4.16b, #3
        trn1            v20.2d, v4.2d, v6.2d
        trn2            v22.2d, v4.2d, v6.2d
        trn1            v21.2d, v5.2d, v7.2d
        trn2            v23.2d, v5.2d, v7.2d
        trn1            v4.4s, v20.4s, v21.4s
        trn2            v5.4s, v20.4s, v21.4s
        trn1            v6.4s, v22.4s, v23.4s
        movi            v16.16b, #0
        movi            v17.16b, #0
        movi            v18.16b, #0
        usdot           v16.4s, v4.16b, v30.16b
        usdot           v17.4s, v5.16b, v30.16b
        usdot           v18.4s, v6.16b, v30.16b
        xtn             v16.4h, v16.4s
        xtn2            v16.8h, v17.4s
        xtn             v18.4h, v18.4s
        str             q16, [x0]
        str             d18, [x0, #16]
        add             x0, x0, x10
        b.ne            1b
        ret
endfunc

function ff_vvc_put_epel_h16_8_neon_i8mm, export=1
        VVC_EPEL_H_HEADER
        b               1f
endfunc

function ff_hevc_put_hevc_epel_h16_8_neon_i8mm, export=1
        EPEL_H_HEADER
1:      ld1             {v0.16b, v1.16b}, [x1], x2
        subs            w3, w3, #1   // height
        ext             v5.16b, v0.16b, v1.16b, #1
        ext             v6.16b, v0.16b, v1.16b, #2
        ext             v7.16b, v0.16b, v1.16b, #3
        zip1            v20.4s, v0.4s, v6.4s
        zip2            v22.4s, v0.4s, v6.4s
        zip1            v21.4s, v5.4s, v7.4s
        zip2            v23.4s, v5.4s, v7.4s
        movi            v16.16b, #0
        movi            v17.16b, #0
        movi            v18.16b, #0
        movi            v19.16b, #0
        usdot           v16.4s, v20.16b, v30.16b
        usdot           v17.4s, v21.16b, v30.16b
        usdot           v18.4s, v22.16b, v30.16b
        usdot           v19.4s, v23.16b, v30.16b
        xtn             v16.4h, v16.4s
        xtn2            v16.8h, v18.4s
        xtn             v17.4h, v17.4s
        xtn2            v17.8h, v19.4s
        st2             {v16.8h, v17.8h}, [x0], x10
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_h24_8_neon_i8mm, export=1
        EPEL_H_HEADER
1:      ld1             {v0.16b, v1.16b}, [x1], x2
        subs            w3, w3, #1   // height
        ext             v5.16b, v0.16b, v1.16b, #1
        ext             v6.16b, v0.16b, v1.16b, #2
        ext             v7.16b, v0.16b, v1.16b, #3
        ext             v26.16b, v1.16b, v1.16b, #1
        ext             v27.16b, v1.16b, v1.16b, #2
        ext             v28.16b, v1.16b, v1.16b, #3
        movi            v16.16b, #0
        movi            v17.16b, #0
        movi            v18.16b, #0
        movi            v19.16b, #0
        movi            v20.16b, #0
        movi            v21.16b, #0
        movi            v22.16b, #0
        movi            v23.16b, #0
        usdot           v16.4s, v0.16b, v30.16b
        usdot           v17.4s, v5.16b, v30.16b
        usdot           v18.4s, v6.16b, v30.16b
        usdot           v19.4s, v7.16b, v30.16b
        usdot           v20.4s, v1.16b, v30.16b
        usdot           v21.4s, v26.16b, v30.16b
        usdot           v22.4s, v27.16b, v30.16b
        usdot           v23.4s, v28.16b, v30.16b
        xtn             v16.4h, v16.4s
        xtn2            v16.8h, v20.4s
        xtn             v17.4h, v17.4s
        xtn2            v17.8h, v21.4s
        xtn             v18.4h, v18.4s
        xtn2            v18.8h, v22.4s
        xtn             v19.4h, v19.4s
        xtn2            v19.8h, v23.4s
        zip1            v20.8h, v16.8h, v18.8h
        zip1            v21.8h, v17.8h, v19.8h
        zip2            v22.8h, v16.8h, v18.8h
        zip2            v23.8h, v17.8h, v19.8h
        zip1            v22.8h, v22.8h, v23.8h
        add             x7, x0, #32
        st2             {v20.8h, v21.8h}, [x0], x10
        st1             {v22.8h}, [x7]
        b.ne            1b
        ret
endfunc

function ff_vvc_put_epel_h32_8_neon_i8mm, export=1
        VVC_EPEL_H_HEADER
        b               1f
endfunc

function ff_hevc_put_hevc_epel_h32_8_neon_i8mm, export=1
        EPEL_H_HEADER
1:      ld1             {v0.16b, v1.16b, v2.16b}, [x1], x2
        subs            w3, w3, #1   // height
        ext             v5.16b, v0.16b, v1.16b, #1
        ext             v6.16b, v0.16b, v1.16b, #2
        ext             v7.16b, v0.16b, v1.16b, #3
        ext             v26.16b, v1.16b, v2.16b, #1
        ext             v27.16b, v1.16b, v2.16b, #2
        ext             v28.16b, v1.16b, v2.16b, #3
        movi            v16.16b, #0
        movi            v17.16b, #0
        movi            v18.16b, #0
        movi            v19.16b, #0
        movi            v20.16b, #0
        movi            v21.16b, #0
        movi            v22.16b, #0
        movi            v23.16b, #0
        usdot           v16.4s, v0.16b, v30.16b
        usdot           v17.4s, v5.16b, v30.16b
        usdot           v18.4s, v6.16b, v30.16b
        usdot           v19.4s, v7.16b, v30.16b
        usdot           v20.4s, v1.16b, v30.16b
        usdot           v21.4s, v26.16b, v30.16b
        usdot           v22.4s, v27.16b, v30.16b
        usdot           v23.4s, v28.16b, v30.16b
        xtn             v16.4h, v16.4s
        xtn2            v16.8h, v20.4s
        xtn             v17.4h, v17.4s
        xtn2            v17.8h, v21.4s
        xtn             v18.4h, v18.4s
        xtn2            v18.8h, v22.4s
        xtn             v19.4h, v19.4s
        xtn2            v19.8h, v23.4s
        st4             {v16.8h, v17.8h, v18.8h, v19.8h}, [x0], x10
        b.ne            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_h48_8_neon_i8mm, export=1
        EPEL_H_HEADER
1:      ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x1], x2
        subs            w3, w3, #1   // height
        ext             v4.16b, v0.16b, v1.16b, #1
        ext             v5.16b, v0.16b, v1.16b, #2
        ext             v6.16b, v0.16b, v1.16b, #3
        ext             v16.16b, v1.16b, v2.16b, #1
        ext             v17.16b, v1.16b, v2.16b, #2
        ext             v18.16b, v1.16b, v2.16b, #3
        movi            v20.16b, #0
        movi            v21.16b, #0
        movi            v22.16b, #0
        movi            v23.16b, #0
        usdot           v20.4s, v0.16b, v30.16b
        usdot           v21.4s, v4.16b, v30.16b
        usdot           v22.4s, v5.16b, v30.16b
        usdot           v23.4s, v6.16b, v30.16b
        movi            v24.16b, #0
        movi            v25.16b, #0
        movi            v26.16b, #0
        movi            v27.16b, #0
        usdot           v24.4s, v1.16b, v30.16b
        usdot           v25.4s, v16.16b, v30.16b
        usdot           v26.4s, v17.16b, v30.16b
        usdot           v27.4s, v18.16b, v30.16b
        xtn             v20.4h, v20.4s
        xtn2            v20.8h, v24.4s
        xtn             v21.4h, v21.4s
        xtn2            v21.8h, v25.4s
        xtn             v22.4h, v22.4s
        xtn2            v22.8h, v26.4s
        xtn             v23.4h, v23.4s
        xtn2            v23.8h, v27.4s
        add             x7, x0, #64
        st4             {v20.8h, v21.8h, v22.8h, v23.8h}, [x0], x10
        ext             v4.16b, v2.16b, v3.16b, #1
        ext             v5.16b, v2.16b, v3.16b, #2
        ext             v6.16b, v2.16b, v3.16b, #3
        movi            v20.16b, #0
        movi            v21.16b, #0
        movi            v22.16b, #0
        movi            v23.16b, #0
        usdot           v20.4s, v2.16b, v30.16b
        usdot           v21.4s, v4.16b, v30.16b
        usdot           v22.4s, v5.16b, v30.16b
        usdot           v23.4s, v6.16b, v30.16b
        zip1            v24.4s, v20.4s, v22.4s
        zip2            v25.4s, v20.4s, v22.4s
        zip1            v26.4s, v21.4s, v23.4s
        zip2            v27.4s, v21.4s, v23.4s
        xtn             v20.4h, v24.4s
        xtn2            v20.8h, v25.4s
        xtn             v21.4h, v26.4s
        xtn2            v21.8h, v27.4s
        st2             {v20.8h, v21.8h}, [x7]
        b.ne            1b
        ret
endfunc

.macro put_epel_h64_8_neon_i8mm
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x1], #64
        ext             v4.16b, v0.16b, v1.16b, #1
        ext             v5.16b, v0.16b, v1.16b, #2
        ext             v6.16b, v0.16b, v1.16b, #3
        ext             v16.16b, v1.16b, v2.16b, #1
        ext             v17.16b, v1.16b, v2.16b, #2
        ext             v18.16b, v1.16b, v2.16b, #3
        movi            v20.16b, #0
        movi            v21.16b, #0
        movi            v22.16b, #0
        movi            v23.16b, #0
        usdot           v20.4s, v0.16b, v30.16b
        usdot           v21.4s, v4.16b, v30.16b
        usdot           v22.4s, v5.16b, v30.16b
        usdot           v23.4s, v6.16b, v30.16b
        movi            v24.16b, #0
        movi            v25.16b, #0
        movi            v26.16b, #0
        movi            v27.16b, #0
        usdot           v24.4s, v1.16b, v30.16b
        usdot           v25.4s, v16.16b, v30.16b
        usdot           v26.4s, v17.16b, v30.16b
        usdot           v27.4s, v18.16b, v30.16b
        xtn             v20.4h, v20.4s
        xtn2            v20.8h, v24.4s
        xtn             v21.4h, v21.4s
        xtn2            v21.8h, v25.4s
        xtn             v22.4h, v22.4s
        xtn2            v22.8h, v26.4s
        xtn             v23.4h, v23.4s
        xtn2            v23.8h, v27.4s
        st4             {v20.8h, v21.8h, v22.8h, v23.8h}, [x0], #64
        ld1             {v7.8b}, [x1], x2
        ext             v4.16b, v2.16b, v3.16b, #1
        ext             v5.16b, v2.16b, v3.16b, #2
        ext             v6.16b, v2.16b, v3.16b, #3
        ext             v16.16b, v3.16b, v7.16b, #1
        ext             v17.16b, v3.16b, v7.16b, #2
        ext             v18.16b, v3.16b, v7.16b, #3
        movi            v20.16b, #0
        movi            v21.16b, #0
        movi            v22.16b, #0
        movi            v23.16b, #0
        usdot           v20.4s, v2.16b, v30.16b
        usdot           v21.4s, v4.16b, v30.16b
        usdot           v22.4s, v5.16b, v30.16b
        usdot           v23.4s, v6.16b, v30.16b
        movi            v24.16b, #0
        movi            v25.16b, #0
        movi            v26.16b, #0
        movi            v27.16b, #0
        usdot           v24.4s, v3.16b, v30.16b
        usdot           v25.4s, v16.16b, v30.16b
        usdot           v26.4s, v17.16b, v30.16b
        usdot           v27.4s, v18.16b, v30.16b
        xtn             v20.4h, v20.4s
        xtn2            v20.8h, v24.4s
        xtn             v21.4h, v21.4s
        xtn2            v21.8h, v25.4s
        xtn             v22.4h, v22.4s
        xtn2            v22.8h, v26.4s
        xtn             v23.4h, v23.4s
        xtn2            v23.8h, v27.4s
        st4             {v20.8h, v21.8h, v22.8h, v23.8h}, [x0], x10
.endm

function ff_vvc_put_epel_h64_8_neon_i8mm, export=1
        VVC_EPEL_H_HEADER
        mov             x10, #(VVC_MAX_PB_SIZE * 2 - 64)
        sub             x2, x2, #64
        b               1f
endfunc

function ff_hevc_put_hevc_epel_h64_8_neon_i8mm, export=1
        EPEL_H_HEADER
        mov             x10, #64
        sub             x2, x2, #64
1:
        subs            w3, w3, #1   // height
        put_epel_h64_8_neon_i8mm
        b.ne            1b
        ret
endfunc

function ff_vvc_put_epel_h128_8_neon_i8mm, export=1
        VVC_EPEL_H_HEADER
        sub             x11, x2, #128
        mov             x10, #64
        mov             x2, #0
1:
        put_epel_h64_8_neon_i8mm
        subs            w3, w3, #1
        put_epel_h64_8_neon_i8mm
        add             x1, x1, x11
        b.ne            1b
        ret
endfunc

DISABLE_I8MM
#endif

function vvc_put_epel_hv4_8_end_neon
        vvc_load_epel_filterh x5
        mov             x10, #(VVC_MAX_PB_SIZE * 2)
        b               0f
endfunc

function hevc_put_hevc_epel_hv4_8_end_neon
        load_epel_filterh x5, x4
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
0:
        ldr             d16, [sp]
        ldr             d17, [sp, x10]
        add             sp, sp, x10, lsl #1
        ld1             {v18.4h}, [sp], x10
.macro calc src0, src1, src2, src3
        ld1             {\src3\().4h}, [sp], x10
        calc_epelh      v4, \src0, \src1, \src2, \src3
        subs            w3, w3, #1
        st1             {v4.4h}, [x0], x10
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_hv6_8_end_neon
        load_epel_filterh x5, x4
        mov             x5, #120
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ldr             q16, [sp]
        ldr             q17, [sp, x10]
        add             sp, sp, x10, lsl #1
        ld1             {v18.8h}, [sp], x10
.macro calc src0, src1, src2, src3
        ld1             {\src3\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src1, \src2, \src3
        calc_epelh2     v4, v5, \src0, \src1, \src2, \src3
        st1             {v4.d}[0], [x0], #8
        subs            w3, w3, #1
        st1             {v4.s}[2], [x0], x5
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function vvc_put_epel_hv8_8_end_neon
        vvc_load_epel_filterh x5
        mov             x10, #(VVC_MAX_PB_SIZE * 2)
        b               0f
endfunc

function hevc_put_hevc_epel_hv8_8_end_neon
        load_epel_filterh x5, x4
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
0:
        ldr             q16, [sp]
        ldr             q17, [sp, x10]
        add             sp, sp, x10, lsl #1
        ld1             {v18.8h}, [sp], x10
.macro calc src0, src1, src2, src3
        ld1             {\src3\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src1, \src2, \src3
        calc_epelh2     v4, v5, \src0, \src1, \src2, \src3
        subs            w3, w3, #1
        st1             {v4.8h}, [x0], x10
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_hv12_8_end_neon
        load_epel_filterh x5, x4
        mov             x5, #112
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h, v17.8h}, [sp], x10
        ld1             {v18.8h, v19.8h}, [sp], x10
        ld1             {v20.8h, v21.8h}, [sp], x10
.macro calc src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\src6\().8h, \src7\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src2, \src4, \src6
        calc_epelh2     v4, v5, \src0, \src2, \src4, \src6
        calc_epelh      v5,     \src1, \src3, \src5, \src7
        st1             {v4.8h}, [x0], #16
        subs            w3, w3, #1
        st1             {v5.4h}, [x0], x5
.endm
1:      calc_all8
.purgem calc
2:      ret
endfunc

function vvc_put_epel_hv16_8_end_neon
        vvc_load_epel_filterh x5
        mov             x10, #(VVC_MAX_PB_SIZE * 2)
        b               0f
endfunc

function hevc_put_hevc_epel_hv16_8_end_neon
        load_epel_filterh x5, x4
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
0:
        ld1             {v16.8h, v17.8h}, [sp], x10
        ld1             {v18.8h, v19.8h}, [sp], x10
        ld1             {v20.8h, v21.8h}, [sp], x10
.macro calc src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\src6\().8h, \src7\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src2, \src4, \src6
        calc_epelh2     v4, v5, \src0, \src2, \src4, \src6
        calc_epelh      v5,     \src1, \src3, \src5, \src7
        calc_epelh2     v5, v6, \src1, \src3, \src5, \src7
        subs            w3, w3, #1
        st1             {v4.8h, v5.8h}, [x0], x10
.endm
1:      calc_all8
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_hv24_8_end_neon
        load_epel_filterh x5, x4
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h, v17.8h, v18.8h}, [sp], x10
        ld1             {v19.8h, v20.8h, v21.8h}, [sp], x10
        ld1             {v22.8h, v23.8h, v24.8h}, [sp], x10
.macro calc src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11
        ld1             {\src9\().8h-\src11\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src3, \src6, \src9
        calc_epelh2     v4, v5, \src0, \src3, \src6, \src9
        calc_epelh      v5,     \src1, \src4, \src7, \src10
        calc_epelh2     v5, v6, \src1, \src4, \src7, \src10
        calc_epelh      v6,     \src2, \src5, \src8, \src11
        calc_epelh2     v6, v7, \src2, \src5, \src8, \src11
        subs            w3, w3, #1
        st1             {v4.8h-v6.8h}, [x0], x10
.endm
1:      calc_all12
.purgem calc
2:      ret
endfunc

.macro epel_hv suffix
function ff_hevc_put_hevc_epel_hv4_8_\suffix, export=1
        add             w10, w3, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x5, x30, [sp, #-32]!
        stp             x0, x3, [sp, #16]
        add             x0, sp, #32
        sub             x1, x1, x2
        add             w3, w3, #3
        bl              X(ff_hevc_put_hevc_epel_h4_8_\suffix)
        ldp             x0, x3, [sp, #16]
        ldp             x5, x30, [sp], #32
        b               hevc_put_hevc_epel_hv4_8_end_neon
endfunc

function ff_vvc_put_epel_hv4_8_\suffix, export=1
        add             w10, w3, #3
        lsl             x10, x10, #8
        sub             sp, sp, x10 // tmp_array
        stp             x5, x30, [sp, #-32]!
        stp             x0, x3, [sp, #16]
        add             x0, sp, #32
        sub             x1, x1, x2
        add             w3, w3, #3
        bl              X(ff_vvc_put_epel_h4_8_\suffix)
        ldp             x0, x3, [sp, #16]
        ldp             x5, x30, [sp], #32
        b               vvc_put_epel_hv4_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_hv6_8_\suffix, export=1
        add             w10, w3, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x5, x30, [sp, #-32]!
        stp             x0, x3, [sp, #16]
        add             x0, sp, #32
        sub             x1, x1, x2
        add             w3, w3, #3
        bl              X(ff_hevc_put_hevc_epel_h6_8_\suffix)
        ldp             x0, x3, [sp, #16]
        ldp             x5, x30, [sp], #32
        b               hevc_put_hevc_epel_hv6_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_hv8_8_\suffix, export=1
        add             w10, w3, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x5, x30, [sp, #-32]!
        stp             x0, x3, [sp, #16]
        add             x0, sp, #32
        sub             x1, x1, x2
        add             w3, w3, #3
        bl              X(ff_hevc_put_hevc_epel_h8_8_\suffix)
        ldp             x0, x3, [sp, #16]
        ldp             x5, x30, [sp], #32
        b               hevc_put_hevc_epel_hv8_8_end_neon
endfunc

function ff_vvc_put_epel_hv8_8_\suffix, export=1
        add             w10, w3, #3
        lsl             x10, x10, #8
        sub             sp, sp, x10 // tmp_array
        stp             x5, x30, [sp, #-32]!
        stp             x0, x3, [sp, #16]
        add             x0, sp, #32
        sub             x1, x1, x2
        add             w3, w3, #3
        bl              X(ff_vvc_put_epel_h8_8_\suffix)
        ldp             x0, x3, [sp, #16]
        ldp             x5, x30, [sp], #32
        b               vvc_put_epel_hv8_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_hv12_8_\suffix, export=1
        add             w10, w3, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x5, x30, [sp, #-32]!
        stp             x0, x3, [sp, #16]
        add             x0, sp, #32
        sub             x1, x1, x2
        add             w3, w3, #3
        bl              X(ff_hevc_put_hevc_epel_h12_8_\suffix)
        ldp             x0, x3, [sp, #16]
        ldp             x5, x30, [sp], #32
        b               hevc_put_hevc_epel_hv12_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_hv16_8_\suffix, export=1
        add             w10, w3, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x5, x30, [sp, #-32]!
        stp             x0, x3, [sp, #16]
        add             x0, sp, #32
        sub             x1, x1, x2
        add             w3, w3, #3
        bl              X(ff_hevc_put_hevc_epel_h16_8_\suffix)
        ldp             x0, x3, [sp, #16]
        ldp             x5, x30, [sp], #32
        b               hevc_put_hevc_epel_hv16_8_end_neon
endfunc

function ff_vvc_put_epel_hv16_8_\suffix, export=1
        add             w10, w3, #3
        lsl             x10, x10, #8
        sub             sp, sp, x10 // tmp_array
        stp             x5, x30, [sp, #-32]!
        stp             x0, x3, [sp, #16]
        add             x0, sp, #32
        sub             x1, x1, x2
        add             w3, w3, #3
        bl              X(ff_vvc_put_epel_h16_8_\suffix)
        ldp             x0, x3, [sp, #16]
        ldp             x5, x30, [sp], #32
        b               vvc_put_epel_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_hv24_8_\suffix, export=1
        add             w10, w3, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x5, x30, [sp, #-32]!
        stp             x0, x3, [sp, #16]
        add             x0, sp, #32
        sub             x1, x1, x2
        add             w3, w3, #3
        bl              X(ff_hevc_put_hevc_epel_h24_8_\suffix)
        ldp             x0, x3, [sp, #16]
        ldp             x5, x30, [sp], #32
        b               hevc_put_hevc_epel_hv24_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_hv32_8_\suffix, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x30, [sp, #48]
        mov             x6, #16
        bl              X(ff_hevc_put_hevc_epel_hv16_8_\suffix)
        ldp             x0, x1, [sp, #32]
        ldp             x2, x3, [sp, #16]
        ldp             x4, x5, [sp], #48
        add             x0, x0, #32
        add             x1, x1, #16
        mov             x6, #16
        bl              X(ff_hevc_put_hevc_epel_hv16_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

function ff_vvc_put_epel_hv32_8_\suffix, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x30, [sp, #48]
        mov             x6, #16
        bl              X(ff_vvc_put_epel_hv16_8_\suffix)
        ldp             x0, x1, [sp, #32]
        ldp             x2, x3, [sp, #16]
        ldp             x4, x5, [sp], #48
        add             x0, x0, #32
        add             x1, x1, #16
        mov             x6, #16
        bl              X(ff_vvc_put_epel_hv16_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

function ff_hevc_put_hevc_epel_hv48_8_\suffix, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x30, [sp, #48]
        mov             x6, #24
        bl              X(ff_hevc_put_hevc_epel_hv24_8_\suffix)
        ldp             x0, x1, [sp, #32]
        ldp             x2, x3, [sp, #16]
        ldp             x4, x5, [sp], #48
        add             x0, x0, #48
        add             x1, x1, #24
        mov             x6, #24
        bl              X(ff_hevc_put_hevc_epel_hv24_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

function ff_hevc_put_hevc_epel_hv64_8_\suffix, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x30, [sp, #48]
        mov             x6, #16
        bl              X(ff_hevc_put_hevc_epel_hv16_8_\suffix)
        ldp             x4, x5, [sp]
        ldp             x2, x3, [sp, #16]
        ldp             x0, x1, [sp, #32]
        add             x0, x0, #32
        add             x1, x1, #16
        mov             x6, #16
        bl              X(ff_hevc_put_hevc_epel_hv16_8_\suffix)
        ldp             x4, x5, [sp]
        ldp             x2, x3, [sp, #16]
        ldp             x0, x1, [sp, #32]
        add             x0, x0, #64
        add             x1, x1, #32
        mov             x6, #16
        bl              X(ff_hevc_put_hevc_epel_hv16_8_\suffix)
        ldp             x0, x1, [sp, #32]
        ldp             x2, x3, [sp, #16]
        ldp             x4, x5, [sp], #48
        add             x0, x0, #96
        add             x1, x1, #48
        mov             x6, #16
        bl              X(ff_hevc_put_hevc_epel_hv16_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

function ff_vvc_put_epel_hv64_8_\suffix, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x30, [sp, #48]
        mov             x6, #32
        bl              X(ff_vvc_put_epel_hv32_8_\suffix)
        ldp             x0, x1, [sp, #32]
        ldp             x2, x3, [sp, #16]
        ldp             x4, x5, [sp], #48
        add             x0, x0, #64
        add             x1, x1, #32
        mov             x6, #32
        bl              X(ff_vvc_put_epel_hv32_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

function ff_vvc_put_epel_hv128_8_\suffix, export=1
        stp             x4, x5, [sp, #-64]!
        stp             x2, x3, [sp, #16]
        stp             x0, x1, [sp, #32]
        str             x30, [sp, #48]
        mov             x6, #64
        bl              X(ff_vvc_put_epel_hv64_8_\suffix)
        ldp             x0, x1, [sp, #32]
        ldp             x2, x3, [sp, #16]
        ldp             x4, x5, [sp], #48
        add             x0, x0, #128
        add             x1, x1, #64
        mov             x6, #64
        bl              X(ff_vvc_put_epel_hv64_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

.endm

epel_hv neon

function hevc_put_hevc_epel_uni_hv4_8_end_neon
        load_epel_filterh x6, x5
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.4h}, [sp], x10
        ld1             {v17.4h}, [sp], x10
        ld1             {v18.4h}, [sp], x10
.macro calc src0, src1, src2, src3
        ld1             {\src3\().4h}, [sp], x10
        calc_epelh      v4, \src0, \src1, \src2, \src3
        sqrshrun        v4.8b, v4.8h, #6
        subs            w4, w4, #1
        st1             {v4.s}[0], [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_uni_hv6_8_end_neon
        load_epel_filterh x6, x5
        sub             x1, x1, #4
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h}, [sp], x10
        ld1             {v17.8h}, [sp], x10
        ld1             {v18.8h}, [sp], x10
.macro calc src0, src1, src2, src3
        ld1             {\src3\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src1, \src2, \src3
        calc_epelh2     v4, v5, \src0, \src1, \src2, \src3
        sqrshrun        v4.8b, v4.8h, #6
        st1             {v4.s}[0], [x0], #4
        subs            w4, w4, #1
        st1             {v4.h}[2], [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_uni_hv8_8_end_neon
        load_epel_filterh x6, x5
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h}, [sp], x10
        ld1             {v17.8h}, [sp], x10
        ld1             {v18.8h}, [sp], x10
.macro calc src0, src1, src2, src3
        ld1             {\src3\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src1, \src2, \src3
        calc_epelh2     v4, v5, \src0, \src1, \src2, \src3
        sqrshrun        v4.8b, v4.8h, #6
        subs            w4, w4, #1
        st1             {v4.8b}, [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_uni_hv12_8_end_neon
        load_epel_filterh x6, x5
        sub             x1, x1, #8
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h, v17.8h}, [sp], x10
        ld1             {v18.8h, v19.8h}, [sp], x10
        ld1             {v20.8h, v21.8h}, [sp], x10
.macro calc src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\src6\().8h, \src7\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src2, \src4, \src6
        calc_epelh2     v4, v5, \src0, \src2, \src4, \src6
        calc_epelh      v5,     \src1, \src3, \src5, \src7
        sqrshrun        v4.8b, v4.8h, #6
        sqrshrun2       v4.16b, v5.8h, #6
        st1             {v4.8b}, [x0], #8
        st1             {v4.s}[2], [x0], x1
        subs            w4, w4, #1
.endm
1:      calc_all8
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_uni_hv16_8_end_neon
        load_epel_filterh x6, x5
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h, v17.8h}, [sp], x10
        ld1             {v18.8h, v19.8h}, [sp], x10
        ld1             {v20.8h, v21.8h}, [sp], x10
.macro calc src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\src6\().8h, \src7\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src2, \src4, \src6
        calc_epelh2     v4, v5, \src0, \src2, \src4, \src6
        calc_epelh      v5,     \src1, \src3, \src5, \src7
        calc_epelh2     v5, v6, \src1, \src3, \src5, \src7
        sqrshrun        v4.8b, v4.8h, #6
        sqrshrun2       v4.16b, v5.8h, #6
        subs            w4, w4, #1
        st1             {v4.16b}, [x0], x1
.endm
1:      calc_all8
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_uni_hv24_8_end_neon
        load_epel_filterh x6, x5
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h, v17.8h, v18.8h}, [sp], x10
        ld1             {v19.8h, v20.8h, v21.8h}, [sp], x10
        ld1             {v22.8h, v23.8h, v24.8h}, [sp], x10
.macro calc src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11
        ld1             {\src9\().8h, \src10\().8h, \src11\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src3, \src6, \src9
        calc_epelh2     v4, v5, \src0, \src3, \src6, \src9
        calc_epelh      v5,     \src1, \src4, \src7, \src10
        calc_epelh2     v5, v6, \src1, \src4, \src7, \src10
        calc_epelh      v6,     \src2, \src5, \src8, \src11
        calc_epelh2     v6, v7, \src2, \src5, \src8, \src11
        sqrshrun        v4.8b, v4.8h, #6
        sqrshrun        v5.8b, v5.8h, #6
        sqrshrun        v6.8b, v6.8h, #6
        subs            w4, w4, #1
        st1             {v4.8b, v5.8b, v6.8b}, [x0], x1
.endm
1:      calc_all12
.purgem calc
2:      ret
endfunc

.macro epel_uni_hv suffix
function ff_hevc_put_hevc_epel_uni_hv4_8_\suffix, export=1
        add             w10, w4, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        str             x30, [sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w4, #3
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_epel_h4_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x30, [sp], #48
        b               hevc_put_hevc_epel_uni_hv4_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_uni_hv6_8_\suffix, export=1
        add             w10, w4, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        str             x30, [sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w4, #3
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_epel_h6_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x30, [sp], #48
        b               hevc_put_hevc_epel_uni_hv6_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_uni_hv8_8_\suffix, export=1
        add             w10, w4, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        str             x30, [sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w4, #3
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_epel_h8_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x30, [sp], #48
        b               hevc_put_hevc_epel_uni_hv8_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_uni_hv12_8_\suffix, export=1
        add             w10, w4, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        str             x30, [sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w4, #3
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_epel_h12_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x30, [sp], #48
        b               hevc_put_hevc_epel_uni_hv12_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_uni_hv16_8_\suffix, export=1
        add             w10, w4, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        str             x30, [sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w4, #3
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_epel_h16_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x30, [sp], #48
        b               hevc_put_hevc_epel_uni_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_uni_hv24_8_\suffix, export=1
        add             w10, w4, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        str             x30, [sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w4, #3
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_epel_h24_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x30, [sp], #48
        b               hevc_put_hevc_epel_uni_hv24_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_uni_hv32_8_\suffix, export=1
        stp             x5, x6, [sp, #-64]!
        stp             x3, x4, [sp, #16]
        stp             x1, x2, [sp, #32]
        stp             x0, x30, [sp, #48]
        mov             x7, #16
        bl              X(ff_hevc_put_hevc_epel_uni_hv16_8_\suffix)
        ldp             x5, x6, [sp]
        ldp             x3, x4, [sp, #16]
        ldp             x1, x2, [sp, #32]
        ldr             x0, [sp, #48]
        add             x0, x0, #16
        add             x2, x2, #16
        mov             x7, #16
        bl              X(ff_hevc_put_hevc_epel_uni_hv16_8_\suffix)
        ldr             x30, [sp, #56]
        add             sp, sp, #64
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_hv48_8_\suffix, export=1
        stp             x5, x6, [sp, #-64]!
        stp             x3, x4, [sp, #16]
        stp             x1, x2, [sp, #32]
        stp             x0, x30, [sp, #48]
        mov             x7, #24
        bl              X(ff_hevc_put_hevc_epel_uni_hv24_8_\suffix)
        ldp             x5, x6, [sp]
        ldp             x3, x4, [sp, #16]
        ldp             x1, x2, [sp, #32]
        ldr             x0, [sp, #48]
        add             x0, x0, #24
        add             x2, x2, #24
        mov             x7, #24
        bl              X(ff_hevc_put_hevc_epel_uni_hv24_8_\suffix)
        ldr             x30, [sp, #56]
        add             sp, sp, #64
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_hv64_8_\suffix, export=1
        stp             x5, x6, [sp, #-64]!
        stp             x3, x4, [sp, #16]
        stp             x1, x2, [sp, #32]
        stp             x0, x30, [sp, #48]
        mov             x7, #16
        bl              X(ff_hevc_put_hevc_epel_uni_hv16_8_\suffix)
        ldp             x5, x6, [sp]
        ldp             x3, x4, [sp, #16]
        ldp             x1, x2, [sp, #32]
        ldr             x0, [sp, #48]
        add             x0, x0, #16
        add             x2, x2, #16
        mov             x7, #16
        bl              X(ff_hevc_put_hevc_epel_uni_hv16_8_\suffix)
        ldp             x5, x6, [sp]
        ldp             x3, x4, [sp, #16]
        ldp             x1, x2, [sp, #32]
        ldr             x0, [sp, #48]
        add             x0, x0, #32
        add             x2, x2, #32
        mov             x7, #16
        bl              X(ff_hevc_put_hevc_epel_uni_hv16_8_\suffix)
        ldp             x5, x6, [sp]
        ldp             x3, x4, [sp, #16]
        ldp             x1, x2, [sp, #32]
        ldr             x0, [sp, #48]
        add             x0, x0, #48
        add             x2, x2, #48
        mov             x7, #16
        bl              X(ff_hevc_put_hevc_epel_uni_hv16_8_\suffix)
        ldr             x30, [sp, #56]
        add             sp, sp, #64
        ret
endfunc
.endm

epel_uni_hv neon

#if HAVE_I8MM
ENABLE_I8MM

epel_hv neon_i8mm

epel_uni_hv neon_i8mm

function ff_hevc_put_hevc_epel_uni_w_h4_8_neon_i8mm, export=1
        EPEL_UNI_W_H_HEADER
1:
        ld1             {v0.8b}, [x2], x3
        subs            w4, w4, #1
        ext             v1.8b, v0.8b, v0.8b, #1
        ext             v2.8b, v0.8b, v0.8b, #2
        ext             v3.8b, v0.8b, v0.8b, #3
        trn1            v0.2s, v0.2s, v2.2s
        trn1            v1.2s, v1.2s, v3.2s
        zip1            v0.4s, v0.4s, v1.4s
        movi            v16.16b, #0
        usdot           v16.4s, v0.16b, v28.16b
        mul             v16.4s, v16.4s, v30.4s
        sqrshl          v16.4s, v16.4s, v31.4s
        sqadd           v16.4s, v16.4s, v29.4s
        sqxtn           v16.4h, v16.4s
        sqxtun          v16.8b, v16.8h
        str             s16, [x0]
        add             x0, x0, x1
        b.hi            1b
        ret
endfunc


function ff_hevc_put_hevc_epel_uni_w_h6_8_neon_i8mm, export=1
        EPEL_UNI_W_H_HEADER
        sub             x1, x1, #4
1:
        ld1             {v0.16b}, [x2], x3
        subs            w4, w4, #1
        ext             v1.16b, v0.16b, v0.16b, #1
        ext             v2.16b, v0.16b, v0.16b, #2
        ext             v3.16b, v0.16b, v0.16b, #3
        trn1            v4.2s, v0.2s, v1.2s
        trn2            v6.2s, v0.2s, v1.2s
        trn1            v5.2s, v2.2s, v3.2s
        zip1            v4.2d, v4.2d, v5.2d
        movi            v16.16b, #0
        movi            v17.16b, #0
        usdot           v16.4s, v4.16b, v28.16b
        usdot           v17.2s, v6.8b, v28.8b
        mul             v16.4s, v16.4s, v30.4s
        mul             v17.2s, v17.2s, v30.2s
        sqrshl          v16.4s, v16.4s, v31.4s
        sqrshl          v17.2s, v17.2s, v31.2s
        sqadd           v16.4s, v16.4s, v29.4s
        sqadd           v17.2s, v17.2s, v29.2s
        sqxtn           v16.4h, v16.4s
        sqxtn2          v16.8h, v17.4s
        sqxtun          v16.8b, v16.8h
        str             s16, [x0], #4
        st1             {v16.h}[2], [x0], x1
        b.hi            1b
        ret
endfunc

.macro  EPEL_UNI_W_H_CALC s0, s1, d0, d1
        movi            \d0\().16b, #0
        movi            \d1\().16b, #0
        usdot           \d0\().4s, \s0\().16b, v28.16b
        usdot           \d1\().4s, \s1\().16b, v28.16b
        mul             \d0\().4s, \d0\().4s, v30.4s
        mul             \d1\().4s, \d1\().4s, v30.4s
        sqrshl          \d0\().4s, \d0\().4s, v31.4s
        sqrshl          \d1\().4s, \d1\().4s, v31.4s
        sqadd           \d0\().4s, \d0\().4s, v29.4s
        sqadd           \d1\().4s, \d1\().4s, v29.4s
.endm

function ff_hevc_put_hevc_epel_uni_w_h8_8_neon_i8mm, export=1
        EPEL_UNI_W_H_HEADER
1:
        ld1             {v0.16b}, [x2], x3
        subs            w4, w4, #1
        ext             v1.16b, v0.16b, v0.16b, #1
        ext             v2.16b, v0.16b, v0.16b, #2
        ext             v3.16b, v0.16b, v0.16b, #3
        zip1            v4.4s, v0.4s, v2.4s
        zip1            v5.4s, v1.4s, v3.4s
        EPEL_UNI_W_H_CALC v4, v5, v16, v17
        sqxtn           v16.4h, v16.4s
        sqxtn           v17.4h, v17.4s
        zip1            v16.8h, v16.8h, v17.8h
        sqxtun          v16.8b, v16.8h
        str             d16, [x0]
        add             x0, x0, x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_h12_8_neon_i8mm, export=1
        EPEL_UNI_W_H_HEADER
1:
        ld1             {v0.16b}, [x2], x3
        subs            w4, w4, #1
        ext             v1.16b, v0.16b, v0.16b, #1
        ext             v2.16b, v0.16b, v0.16b, #2
        ext             v3.16b, v0.16b, v0.16b, #3
        zip1            v4.4s, v0.4s, v2.4s
        zip1            v5.4s, v1.4s, v3.4s
        zip2            v6.4s, v0.4s, v2.4s
        zip2            v7.4s, v1.4s, v3.4s
        zip1            v6.4s, v6.4s, v7.4s
        EPEL_UNI_W_H_CALC v4, v5, v16, v17
        movi            v18.16b, #0
        usdot           v18.4s, v6.16b, v28.16b
        mul             v18.4s, v18.4s, v30.4s
        sqrshl          v18.4s, v18.4s, v31.4s
        sqadd           v18.4s, v18.4s, v29.4s
        sqxtn           v16.4h, v16.4s
        sqxtn           v17.4h, v17.4s
        sqxtn           v18.4h, v18.4s
        zip1            v16.8h, v16.8h, v17.8h
        sqxtun          v16.8b, v16.8h
        sqxtun          v18.8b, v18.8h
        str             d16, [x0]
        str             s18, [x0, #8]
        add             x0, x0, x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_h16_8_neon_i8mm, export=1
        EPEL_UNI_W_H_HEADER
1:
        ld1             {v0.16b, v1.16b}, [x2], x3
        subs            w4, w4, #1
        ext             v4.16b, v0.16b, v1.16b, #1
        ext             v5.16b, v0.16b, v1.16b, #2
        ext             v6.16b, v0.16b, v1.16b, #3
        zip1            v20.4s, v0.4s, v5.4s
        zip1            v21.4s, v4.4s, v6.4s
        zip2            v22.4s, v0.4s, v5.4s
        zip2            v23.4s, v4.4s, v6.4s
        EPEL_UNI_W_H_CALC v20, v21, v16, v17
        EPEL_UNI_W_H_CALC v22, v23, v18, v19
        sqxtn           v16.4h, v16.4s
        sqxtn           v17.4h, v17.4s
        sqxtn2          v16.8h, v18.4s
        sqxtn2          v17.8h, v19.4s
        sqxtun          v16.8b, v16.8h
        sqxtun          v17.8b, v17.8h
        st2             {v16.8b, v17.8b}, [x0], x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_h24_8_neon_i8mm, export=1
        EPEL_UNI_W_H_HEADER
1:
        ld1             {v0.16b, v1.16b}, [x2], x3
        subs            w4, w4, #1
        ext             v2.16b, v0.16b, v1.16b, #1
        ext             v3.16b, v0.16b, v1.16b, #2
        ext             v4.16b, v0.16b, v1.16b, #3
        ext             v5.16b, v1.16b, v1.16b, #1
        ext             v6.16b, v1.16b, v1.16b, #2
        ext             v7.16b, v1.16b, v1.16b, #3
        zip1            v20.4s, v0.4s, v3.4s
        zip1            v21.4s, v2.4s, v4.4s
        zip2            v22.4s, v0.4s, v3.4s
        zip2            v23.4s, v2.4s, v4.4s
        zip1            v24.4s, v1.4s, v6.4s
        zip1            v25.4s, v5.4s, v7.4s
        EPEL_UNI_W_H_CALC v20, v21, v16, v17
        EPEL_UNI_W_H_CALC v22, v23, v18, v19
        EPEL_UNI_W_H_CALC v24, v25, v26, v27
        sqxtn           v16.4h, v16.4s
        sqxtn           v17.4h, v17.4s
        sqxtn           v18.4h, v18.4s
        sqxtn           v19.4h, v19.4s
        sqxtn           v26.4h, v26.4s
        sqxtn           v27.4h, v27.4s
        zip1            v16.8h, v16.8h, v17.8h
        zip1            v18.8h, v18.8h, v19.8h
        zip1            v26.8h, v26.8h, v27.8h
        sqxtun          v16.8b, v16.8h
        sqxtun2         v16.16b, v18.8h
        sqxtun          v26.8b, v26.8h
        str             q16, [x0]
        str             d26, [x0, #16]
        add             x0, x0, x1
        b.hi            1b
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_h32_8_neon_i8mm, export=1
        EPEL_UNI_W_H_HEADER
1:
        ld1             {v0.16b, v1.16b, v2.16b}, [x2], x3
        subs            w4, w4, #1
        ext             v3.16b, v0.16b, v1.16b, #1
        ext             v4.16b, v0.16b, v1.16b, #2
        ext             v5.16b, v0.16b, v1.16b, #3
        ext             v16.16b, v1.16b, v2.16b, #1
        ext             v17.16b, v1.16b, v2.16b, #2
        ext             v18.16b, v1.16b, v2.16b, #3
        EPEL_UNI_W_H_CALC v0, v3, v6, v7
        EPEL_UNI_W_H_CALC v4, v5, v19, v20
        EPEL_UNI_W_H_CALC v1, v16, v21, v22
        EPEL_UNI_W_H_CALC v17, v18, v23, v24
        sqxtn           v6.4h, v6.4s
        sqxtn2          v6.8h, v21.4s
        sqxtn           v7.4h, v7.4s
        sqxtn2          v7.8h, v22.4s
        sqxtn           v19.4h, v19.4s
        sqxtn2          v19.8h, v23.4s
        sqxtn           v20.4h, v20.4s
        sqxtn2          v20.8h, v24.4s
        sqxtun          v0.8b, v6.8h
        sqxtun          v1.8b, v7.8h
        sqxtun          v2.8b, v19.8h
        sqxtun          v3.8b, v20.8h
        st4             {v0.8b, v1.8b, v2.8b, v3.8b}, [x0], x1
        b.hi            1b
        ret
endfunc



function ff_hevc_put_hevc_epel_uni_w_h48_8_neon_i8mm, export=1
        EPEL_UNI_W_H_HEADER
        sub             x1, x1, #32
1:
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x2], x3
        subs            w4, w4, #1
        ext             v4.16b, v0.16b, v1.16b, #1
        ext             v5.16b, v0.16b, v1.16b, #2
        ext             v6.16b, v0.16b, v1.16b, #3
        ext             v16.16b, v1.16b, v2.16b, #1
        ext             v17.16b, v1.16b, v2.16b, #2
        ext             v18.16b, v1.16b, v2.16b, #3
        EPEL_UNI_W_H_CALC v0, v4, v19, v20
        EPEL_UNI_W_H_CALC v5, v6, v21, v22
        EPEL_UNI_W_H_CALC v1, v16, v23, v24
        EPEL_UNI_W_H_CALC v17, v18, v25, v26
        sqxtn           v19.4h, v19.4s
        sqxtn2          v19.8h, v23.4s
        sqxtn           v20.4h, v20.4s
        sqxtn2          v20.8h, v24.4s
        sqxtn           v21.4h, v21.4s
        sqxtn2          v21.8h, v25.4s
        sqxtn           v22.4h, v22.4s
        sqxtn2          v22.8h, v26.4s
        sqxtun          v19.8b, v19.8h
        sqxtun          v20.8b, v20.8h
        sqxtun          v21.8b, v21.8h
        sqxtun          v22.8b, v22.8h
        st4             {v19.8b, v20.8b, v21.8b, v22.8b}, [x0], #32
        ext             v5.16b, v2.16b, v3.16b, #1
        ext             v6.16b, v2.16b, v3.16b, #2
        ext             v7.16b, v2.16b, v3.16b, #3
        EPEL_UNI_W_H_CALC v2, v5, v19, v20
        EPEL_UNI_W_H_CALC v6, v7, v21, v22
        sqxtn           v19.4h, v19.4s
        sqxtn           v20.4h, v20.4s
        sqxtn           v21.4h, v21.4s
        sqxtn           v22.4h, v22.4s
        zip1            v4.8h, v19.8h, v21.8h
        zip1            v5.8h, v20.8h, v22.8h
        sqxtun          v4.8b, v4.8h
        sqxtun          v5.8b, v5.8h
        st2             {v4.8b, v5.8b}, [x0], x1
        b.hi            1b
        ret
endfunc


function ff_hevc_put_hevc_epel_uni_w_h64_8_neon_i8mm, export=1
        EPEL_UNI_W_H_HEADER
        sub             x1, x1, #32
        sub             x3, x3, #64
1:
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x2], #64
        subs            w4, w4, #1
        ext             v4.16b, v0.16b, v1.16b, #1
        ext             v5.16b, v0.16b, v1.16b, #2
        ext             v6.16b, v0.16b, v1.16b, #3
        ext             v16.16b, v1.16b, v2.16b, #1
        ext             v17.16b, v1.16b, v2.16b, #2
        ext             v18.16b, v1.16b, v2.16b, #3
        EPEL_UNI_W_H_CALC v0, v4, v19, v20
        EPEL_UNI_W_H_CALC v5, v6, v21, v22
        EPEL_UNI_W_H_CALC v1, v16, v23, v24
        EPEL_UNI_W_H_CALC v17, v18, v25, v26
        sqxtn           v19.4h, v19.4s
        sqxtn2          v19.8h, v23.4s
        sqxtn           v20.4h, v20.4s
        sqxtn2          v20.8h, v24.4s
        sqxtn           v21.4h, v21.4s
        sqxtn2          v21.8h, v25.4s
        sqxtn           v22.4h, v22.4s
        sqxtn2          v22.8h, v26.4s
        sqxtun          v19.8b, v19.8h
        sqxtun          v20.8b, v20.8h
        sqxtun          v21.8b, v21.8h
        sqxtun          v22.8b, v22.8h
        st4             {v19.8b, v20.8b, v21.8b, v22.8b}, [x0], #32
        ld1             {v7.8b}, [x2], x3
        ext             v4.16b, v2.16b, v3.16b, #1
        ext             v5.16b, v2.16b, v3.16b, #2
        ext             v6.16b, v2.16b, v3.16b, #3
        ext             v16.16b, v3.16b, v7.16b, #1
        ext             v17.16b, v3.16b, v7.16b, #2
        ext             v18.16b, v3.16b, v7.16b, #3
        EPEL_UNI_W_H_CALC v2, v4, v19, v20
        EPEL_UNI_W_H_CALC v5, v6, v21, v22
        EPEL_UNI_W_H_CALC v3, v16, v23, v24
        EPEL_UNI_W_H_CALC v17, v18, v25, v26
        sqxtn           v19.4h, v19.4s
        sqxtn2          v19.8h, v23.4s
        sqxtn           v20.4h, v20.4s
        sqxtn2          v20.8h, v24.4s
        sqxtn           v21.4h, v21.4s
        sqxtn2          v21.8h, v25.4s
        sqxtn           v22.4h, v22.4s
        sqxtn2          v22.8h, v26.4s
        sqxtun          v19.8b, v19.8h
        sqxtun          v20.8b, v20.8h
        sqxtun          v21.8b, v21.8h
        sqxtun          v22.8b, v22.8h
        st4             {v19.8b, v20.8b, v21.8b, v22.8b}, [x0], x1
        b.hi            1b
        ret
endfunc
DISABLE_I8MM
#endif

.macro epel_uni_w_hv_start
        mov             x15, x5         //denom
        mov             x16, x6         //wx
        mov             x17, x7         //ox
        add             w15, w15, #6    //shift = denom+6


        ldp             x5, x6, [sp]
        ldr             x7, [sp, #16]

        stp             d14, d15, [sp, #-64]!
        stp             d8, d9, [sp, #16]
        stp             d10, d11, [sp, #32]
        stp             d12, d13, [sp, #48]

        dup             v13.8h, w16     //wx
        dup             v14.4s, w17     //ox

        mov             w17, #1
        lsl             w17, w17, w15
        lsr             w17, w17, #1
        dup             v15.4s, w17

        neg             w15, w15        // -shift
        dup             v12.4s, w15     //shift
.endm

.macro epel_uni_w_hv_end
        smull           v28.4s, v4.4h, v13.4h
        smull2          v29.4s, v4.8h, v13.8h
        add             v28.4s, v28.4s, v15.4s
        add             v29.4s, v29.4s, v15.4s
        sshl            v28.4s, v28.4s, v12.4s
        sshl            v29.4s, v29.4s, v12.4s
        add             v28.4s, v28.4s, v14.4s
        add             v29.4s, v29.4s, v14.4s
        sqxtn           v4.4h, v28.4s
        sqxtn2          v4.8h, v29.4s
.endm

.macro epel_uni_w_hv_end2
        smull           v28.4s, v4.4h, v13.4h
        smull2          v29.4s, v4.8h, v13.8h
        smull           v30.4s, v5.4h, v13.4h
        smull2          v31.4s, v5.8h, v13.8h
        add             v28.4s, v28.4s, v15.4s
        add             v29.4s, v29.4s, v15.4s
        add             v30.4s, v30.4s, v15.4s
        add             v31.4s, v31.4s, v15.4s

        sshl            v28.4s, v28.4s, v12.4s
        sshl            v29.4s, v29.4s, v12.4s
        sshl            v30.4s, v30.4s, v12.4s
        sshl            v31.4s, v31.4s, v12.4s

        add             v28.4s, v28.4s, v14.4s
        add             v29.4s, v29.4s, v14.4s
        add             v30.4s, v30.4s, v14.4s
        add             v31.4s, v31.4s, v14.4s

        sqxtn           v4.4h, v28.4s
        sqxtn2          v4.8h, v29.4s
        sqxtn           v5.4h, v30.4s
        sqxtn2          v5.8h, v31.4s
.endm

.macro epel_uni_w_hv_end3
        smull           v1.4s,  v4.4h, v13.4h
        smull2          v2.4s,  v4.8h, v13.8h
        smull           v28.4s, v5.4h, v13.4h
        smull2          v29.4s, v5.8h, v13.8h
        smull           v30.4s, v6.4h, v13.4h
        smull2          v31.4s, v6.8h, v13.8h
        add             v1.4s, v1.4s, v15.4s
        add             v2.4s, v2.4s, v15.4s
        add             v28.4s, v28.4s, v15.4s
        add             v29.4s, v29.4s, v15.4s
        add             v30.4s, v30.4s, v15.4s
        add             v31.4s, v31.4s, v15.4s

        sshl            v1.4s, v1.4s, v12.4s
        sshl            v2.4s, v2.4s, v12.4s
        sshl            v28.4s, v28.4s, v12.4s
        sshl            v29.4s, v29.4s, v12.4s
        sshl            v30.4s, v30.4s, v12.4s
        sshl            v31.4s, v31.4s, v12.4s
        add             v1.4s, v1.4s, v14.4s
        add             v2.4s, v2.4s, v14.4s
        add             v28.4s, v28.4s, v14.4s
        add             v29.4s, v29.4s, v14.4s
        add             v30.4s, v30.4s, v14.4s
        add             v31.4s, v31.4s, v14.4s

        sqxtn           v4.4h, v1.4s
        sqxtn2          v4.8h, v2.4s
        sqxtn           v5.4h, v28.4s
        sqxtn2          v5.8h, v29.4s
        sqxtn           v6.4h, v30.4s
        sqxtn2          v6.8h, v31.4s
.endm



function hevc_put_hevc_epel_uni_w_hv4_8_end_neon
        load_epel_filterh x6, x5
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.4h}, [sp], x10
        ld1             {v17.4h}, [sp], x10
        ld1             {v18.4h}, [sp], x10
1:      ld1             {v19.4h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v16, v17, v18, v19
        epel_uni_w_hv_end
        sqxtun          v4.8b, v4.8h
        str             s4, [x0]
        add             x0, x0, x1
        b.eq            2f

        ld1             {v16.4h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v17, v18, v19, v16
        epel_uni_w_hv_end
        sqxtun          v4.8b, v4.8h
        str             s4, [x0]
        add             x0, x0, x1
        b.eq            2f

        ld1             {v17.4h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v18, v19, v16, v17
        epel_uni_w_hv_end
        sqxtun          v4.8b, v4.8h
        str             s4, [x0]
        add             x0, x0, x1
        b.eq            2f

        ld1             {v18.4h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v19, v16, v17, v18
        epel_uni_w_hv_end
        sqxtun          v4.8b, v4.8h
        str             s4, [x0]
        add             x0, x0, x1
        b.ne            1b
2:
        ldp             d8, d9, [sp, #16]
        ldp             d10, d11, [sp, #32]
        ldp             d12, d13, [sp, #48]
        ldp             d14, d15, [sp], #64
        ret
endfunc

function hevc_put_hevc_epel_uni_w_hv6_8_end_neon
        load_epel_filterh x6, x5
        sub             x1, x1, #4
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h}, [sp], x10
        ld1             {v17.8h}, [sp], x10
        ld1             {v18.8h}, [sp], x10
1:      ld1             {v19.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v16, v17, v18, v19
        calc_epelh2     v4, v5, v16, v17, v18, v19
        epel_uni_w_hv_end
        sqxtun          v4.8b, v4.8h
        st1             {v4.s}[0], [x0], #4
        st1             {v4.h}[2], [x0], x1
        b.eq            2f

        ld1             {v16.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v17, v18, v19, v16
        calc_epelh2     v4, v5, v17, v18, v19, v16
        epel_uni_w_hv_end
        sqxtun          v4.8b, v4.8h
        st1             {v4.s}[0], [x0], #4
        st1             {v4.h}[2], [x0], x1
        b.eq            2f

        ld1             {v17.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v18, v19, v16, v17
        calc_epelh2     v4, v5, v18, v19, v16, v17
        epel_uni_w_hv_end
        sqxtun          v4.8b, v4.8h
        st1             {v4.s}[0], [x0], #4
        st1             {v4.h}[2], [x0], x1
        b.eq            2f

        ld1             {v18.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v19, v16, v17, v18
        calc_epelh2     v4, v5, v19, v16, v17, v18
        epel_uni_w_hv_end
        sqxtun          v4.8b, v4.8h
        st1             {v4.s}[0], [x0], #4
        st1             {v4.h}[2], [x0], x1
        b.ne            1b
2:
        ldp             d8, d9, [sp, #16]
        ldp             d10, d11, [sp, #32]
        ldp             d12, d13, [sp, #48]
        ldp             d14, d15, [sp], #64
        ret
endfunc

function hevc_put_hevc_epel_uni_w_hv8_8_end_neon
        load_epel_filterh x6, x5
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h}, [sp], x10
        ld1             {v17.8h}, [sp], x10
        ld1             {v18.8h}, [sp], x10
1:      ld1             {v19.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v16, v17, v18, v19
        calc_epelh2     v4, v5, v16, v17, v18, v19
        epel_uni_w_hv_end
        sqxtun          v4.8b, v4.8h
        st1             {v4.8b}, [x0], x1
        b.eq            2f

        ld1             {v16.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v17, v18, v19, v16
        calc_epelh2     v4, v5, v17, v18, v19, v16
        epel_uni_w_hv_end
        sqxtun          v4.8b, v4.8h
        st1             {v4.8b}, [x0], x1
        b.eq            2f

        ld1             {v17.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v18, v19, v16, v17
        calc_epelh2     v4, v5, v18, v19, v16, v17
        epel_uni_w_hv_end
        sqxtun          v4.8b, v4.8h
        st1             {v4.8b}, [x0], x1
        b.eq            2f

        ld1             {v18.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v19, v16, v17, v18
        calc_epelh2     v4, v5, v19, v16, v17, v18
        epel_uni_w_hv_end
        sqxtun          v4.8b, v4.8h
        st1             {v4.8b}, [x0], x1
        b.ne            1b
2:
        ldp             d8, d9, [sp, #16]
        ldp             d10, d11, [sp, #32]
        ldp             d12, d13, [sp, #48]
        ldp             d14, d15, [sp], #64
        ret
endfunc

function hevc_put_hevc_epel_uni_w_hv12_8_end_neon
        load_epel_filterh x6, x5
        sub             x1, x1, #8
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h, v17.8h}, [sp], x10
        ld1             {v18.8h, v19.8h}, [sp], x10
        ld1             {v20.8h, v21.8h}, [sp], x10
1:      ld1             {v22.8h, v23.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v16, v18, v20, v22
        calc_epelh2     v4, v5, v16, v18, v20, v22
        calc_epelh      v5, v17, v19, v21, v23
        epel_uni_w_hv_end2
        sqxtun          v4.8b, v4.8h
        sqxtun2         v4.16b, v5.8h
        st1             {v4.8b}, [x0], #8
        st1             {v4.s}[2], [x0], x1
        b.eq            2f

        ld1             {v16.8h, v17.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v18, v20, v22, v16
        calc_epelh2     v4, v5, v18, v20, v22, v16
        calc_epelh      v5, v19, v21, v23, v17
        epel_uni_w_hv_end2
        sqxtun          v4.8b, v4.8h
        sqxtun2         v4.16b, v5.8h
        st1             {v4.8b}, [x0], #8
        st1             {v4.s}[2], [x0], x1
        b.eq            2f

        ld1             {v18.8h, v19.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v20, v22, v16, v18
        calc_epelh2     v4, v5, v20, v22, v16, v18
        calc_epelh      v5, v21, v23, v17, v19
        epel_uni_w_hv_end2
        sqxtun          v4.8b, v4.8h
        sqxtun2         v4.16b, v5.8h
        st1             {v4.8b}, [x0], #8
        st1             {v4.s}[2], [x0], x1
        b.eq            2f

        ld1             {v20.8h, v21.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v22, v16, v18, v20
        calc_epelh2     v4, v5, v22, v16, v18, v20
        calc_epelh      v5, v23, v17, v19, v21
        epel_uni_w_hv_end2
        sqxtun          v4.8b, v4.8h
        sqxtun2         v4.16b, v5.8h
        st1             {v4.8b}, [x0], #8
        st1             {v4.s}[2], [x0], x1
        b.ne            1b
2:
        ldp             d8, d9, [sp, #16]
        ldp             d10, d11, [sp, #32]
        ldp             d12, d13, [sp, #48]
        ldp             d14, d15, [sp], #64
        ret
endfunc

function hevc_put_hevc_epel_uni_w_hv16_8_end_neon
        load_epel_filterh x6, x5
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h, v17.8h}, [sp], x10
        ld1             {v18.8h, v19.8h}, [sp], x10
        ld1             {v20.8h, v21.8h}, [sp], x10
1:      ld1             {v22.8h, v23.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v16, v18, v20, v22
        calc_epelh2     v4, v5, v16, v18, v20, v22
        calc_epelh      v5, v17, v19, v21, v23
        calc_epelh2     v5, v6, v17, v19, v21, v23
        epel_uni_w_hv_end2
        sqxtun          v4.8b, v4.8h
        sqxtun2         v4.16b, v5.8h
        st1             {v4.16b}, [x0], x1
        b.eq            2f

        ld1             {v16.8h, v17.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v18, v20, v22, v16
        calc_epelh2     v4, v5, v18, v20, v22, v16
        calc_epelh      v5, v19, v21, v23, v17
        calc_epelh2     v5, v6, v19, v21, v23, v17
        epel_uni_w_hv_end2
        sqxtun          v4.8b, v4.8h
        sqxtun2         v4.16b, v5.8h
        st1             {v4.16b}, [x0], x1
        b.eq            2f

        ld1             {v18.8h, v19.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v20, v22, v16, v18
        calc_epelh2     v4, v5, v20, v22, v16, v18
        calc_epelh      v5, v21, v23, v17, v19
        calc_epelh2     v5, v6, v21, v23, v17, v19
        epel_uni_w_hv_end2
        sqxtun          v4.8b, v4.8h
        sqxtun2         v4.16b, v5.8h
        st1             {v4.16b}, [x0], x1
        b.eq            2f

        ld1             {v20.8h, v21.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v22, v16, v18, v20
        calc_epelh2     v4, v5, v22, v16, v18, v20
        calc_epelh      v5, v23, v17, v19, v21
        calc_epelh2     v5, v6, v23, v17, v19, v21
        epel_uni_w_hv_end2
        sqxtun          v4.8b, v4.8h
        sqxtun2         v4.16b, v5.8h
        st1             {v4.16b}, [x0], x1
        b.ne            1b
2:
        ldp             d8, d9, [sp, #16]
        ldp             d10, d11, [sp, #32]
        ldp             d12, d13, [sp, #48]
        ldp             d14, d15, [sp], #64
        ret
endfunc

function hevc_put_hevc_epel_uni_w_hv24_8_end_neon
        load_epel_filterh x6, x5
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h, v17.8h, v18.8h}, [sp], x10
        ld1             {v19.8h, v20.8h, v21.8h}, [sp], x10
        ld1             {v22.8h, v23.8h, v24.8h}, [sp], x10
1:      ld1             {v25.8h, v26.8h, v27.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v16, v19, v22, v25
        calc_epelh2     v4, v5, v16, v19, v22, v25
        calc_epelh      v5, v17, v20, v23, v26
        calc_epelh2     v5, v6, v17, v20, v23, v26
        calc_epelh      v6, v18, v21, v24, v27
        calc_epelh2     v6, v7, v18, v21, v24, v27

        epel_uni_w_hv_end3
        sqxtun          v4.8b, v4.8h
        sqxtun          v5.8b, v5.8h
        sqxtun          v6.8b, v6.8h
        st1             {v4.8b, v5.8b, v6.8b}, [x0], x1
        b.eq            2f

        ld1             {v16.8h, v17.8h, v18.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v19, v22, v25, v16
        calc_epelh2     v4, v5, v19, v22, v25, v16
        calc_epelh      v5, v20, v23, v26, v17
        calc_epelh2     v5, v6, v20, v23, v26, v17
        calc_epelh      v6, v21, v24, v27, v18
        calc_epelh2     v6, v7, v21, v24, v27, v18
        epel_uni_w_hv_end3

        sqxtun          v4.8b, v4.8h
        sqxtun          v5.8b, v5.8h
        sqxtun          v6.8b, v6.8h
        st1             {v4.8b, v5.8b, v6.8b}, [x0], x1
        b.eq            2f

        ld1             {v19.8h, v20.8h, v21.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v22, v25, v16, v19
        calc_epelh2     v4, v5, v22, v25, v16, v19
        calc_epelh      v5, v23, v26, v17, v20
        calc_epelh2     v5, v6, v23, v26, v17, v20
        calc_epelh      v6, v24, v27, v18, v21
        calc_epelh2     v6, v7, v24, v27, v18, v21
        epel_uni_w_hv_end3

        sqxtun          v4.8b, v4.8h
        sqxtun          v5.8b, v5.8h
        sqxtun          v6.8b, v6.8h
        st1             {v4.8b, v5.8b, v6.8b}, [x0], x1
        b.eq            2f

        ld1             {v22.8h, v23.8h, v24.8h}, [sp], x10
        subs            x4, x4, #1
        calc_epelh      v4, v25, v16, v19, v22
        calc_epelh2     v4, v5, v25, v16, v19, v22
        calc_epelh      v5, v26, v17, v20, v23
        calc_epelh2     v5, v6, v26, v17, v20, v23
        calc_epelh      v6, v27, v18, v21, v24
        calc_epelh2     v6, v7, v27, v18, v21, v24
        epel_uni_w_hv_end3

        sqxtun          v4.8b, v4.8h
        sqxtun          v5.8b, v5.8h
        sqxtun          v6.8b, v6.8h
        st1             {v4.8b, v5.8b, v6.8b}, [x0], x1
        b.ne            1b
2:
        ldp             d8, d9, [sp, #16]
        ldp             d10, d11, [sp, #32]
        ldp             d12, d13, [sp, #48]
        ldp             d14, d15, [sp], #64
        ret
endfunc

.macro epel_uni_w_hv suffix
function ff_hevc_put_hevc_epel_uni_w_hv4_8_\suffix, export=1
        epel_uni_w_hv_start
        sxtw            x4, w4

        add             x10, x4, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10     // tmp_array
        str             x30, [sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             x3, x4, #3
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_epel_h4_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x30, [sp], #48
        b               hevc_put_hevc_epel_uni_w_hv4_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv6_8_\suffix, export=1
        epel_uni_w_hv_start
        sxtw            x4, w4

        add             x10, x4, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10     // tmp_array
        str             x30, [sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             x3, x4, #3
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_epel_h6_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x30, [sp], #48
        b               hevc_put_hevc_epel_uni_w_hv6_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv8_8_\suffix, export=1
        epel_uni_w_hv_start
        sxtw            x4, w4

        add             x10, x4, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10     // tmp_array
        str             x30, [sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             x3, x4, #3
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_epel_h8_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x30, [sp], #48
        b               hevc_put_hevc_epel_uni_w_hv8_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv12_8_\suffix, export=1
        epel_uni_w_hv_start
        sxtw            x4, w4

        add             x10, x4, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10     // tmp_array
        str             x30, [sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             x3, x4, #3
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_epel_h12_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x30, [sp], #48
        b               hevc_put_hevc_epel_uni_w_hv12_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv16_8_\suffix, export=1
        epel_uni_w_hv_start
        sxtw            x4, w4

        add             x10, x4, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10     // tmp_array
        str             x30, [sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             x3, x4, #3
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_epel_h16_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x30, [sp], #48
        b               hevc_put_hevc_epel_uni_w_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv24_8_\suffix, export=1
        epel_uni_w_hv_start
        sxtw            x4, w4

        add             x10, x4, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10     // tmp_array
        str             x30, [sp, #-48]!
        stp             x4, x6, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             x3, x4, #3
        mov             x4, x5
        bl              X(ff_hevc_put_hevc_epel_h24_8_\suffix)
        ldp             x4, x6, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldr             x30, [sp], #48
        b               hevc_put_hevc_epel_uni_w_hv24_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv32_8_\suffix, export=1
        ldp             x15, x16, [sp]
        mov             x17, #16
        stp             x15, x16, [sp, #-96]!
        stp             x0, x30, [sp, #16]
        stp             x1, x2, [sp, #32]
        stp             x3, x4, [sp, #48]
        stp             x5, x6, [sp, #64]
        stp             x17, x7, [sp, #80]

        bl              X(ff_hevc_put_hevc_epel_uni_w_hv16_8_\suffix)
        ldp             x0, x30, [sp, #16]
        ldp             x1, x2, [sp, #32]
        ldp             x3, x4, [sp, #48]
        ldp             x5, x6, [sp, #64]
        ldp             x17, x7, [sp, #80]
        ldp             x15, x16, [sp], #96
        add             x0, x0, #16
        add             x2, x2, #16
        mov             x17, #16
        stp             x15, x16, [sp, #-32]!
        stp             x17, x30, [sp, #16]
        bl              X(ff_hevc_put_hevc_epel_uni_w_hv16_8_\suffix)
        ldp             x17, x30, [sp, #16]
        ldp             x15, x16, [sp], #32
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv48_8_\suffix, export=1
        ldp             x15, x16, [sp]
        mov             x17, #24
        stp             x15, x16, [sp, #-96]!
        stp             x0, x30, [sp, #16]
        stp             x1, x2, [sp, #32]
        stp             x3, x4, [sp, #48]
        stp             x5, x6, [sp, #64]
        stp             x17, x7, [sp, #80]
        bl              X(ff_hevc_put_hevc_epel_uni_w_hv24_8_\suffix)
        ldp             x0, x30, [sp, #16]
        ldp             x1, x2, [sp, #32]
        ldp             x3, x4, [sp, #48]
        ldp             x5, x6, [sp, #64]
        ldp             x17, x7, [sp, #80]
        ldp             x15, x16, [sp], #96
        add             x0, x0, #24
        add             x2, x2, #24
        mov             x17, #24
        stp             x15, x16, [sp, #-32]!
        stp             x17, x30, [sp, #16]
        bl              X(ff_hevc_put_hevc_epel_uni_w_hv24_8_\suffix)
        ldp             x17, x30, [sp, #16]
        ldp             x15, x16, [sp], #32
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv64_8_\suffix, export=1
        ldp             x15, x16, [sp]
        mov             x17, #32
        stp             x15, x16, [sp, #-96]!
        stp             x0, x30, [sp, #16]
        stp             x1, x2, [sp, #32]
        stp             x3, x4, [sp, #48]
        stp             x5, x6, [sp, #64]
        stp             x17, x7, [sp, #80]

        bl              X(ff_hevc_put_hevc_epel_uni_w_hv32_8_\suffix)
        ldp             x0, x30, [sp, #16]
        ldp             x1, x2, [sp, #32]
        ldp             x3, x4, [sp, #48]
        ldp             x5, x6, [sp, #64]
        ldp             x17, x7, [sp, #80]
        ldp             x15, x16, [sp], #96
        add             x0, x0, #32
        add             x2, x2, #32
        mov             x17, #32
        stp             x15, x16, [sp, #-32]!
        stp             x17, x30, [sp, #16]
        bl              X(ff_hevc_put_hevc_epel_uni_w_hv32_8_\suffix)
        ldp             x17, x30, [sp, #16]
        ldp             x15, x16, [sp], #32
        ret
endfunc
.endm

epel_uni_w_hv neon


function hevc_put_hevc_epel_bi_hv4_8_end_neon
        load_epel_filterh x7, x6
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.4h}, [sp], x10
        ld1             {v17.4h}, [sp], x10
        ld1             {v18.4h}, [sp], x10
.macro calc src0, src1, src2, src3
        ld1             {\src3\().4h}, [sp], x10
        calc_epelh      v4, \src0, \src1, \src2, \src3
        ld1             {v6.4h}, [x4], x10
        sqadd           v4.4h, v4.4h, v6.4h
        sqrshrun        v4.8b, v4.8h, #7
        subs            w5, w5, #1
        st1             {v4.s}[0], [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_bi_hv6_8_end_neon
        load_epel_filterh x7, x6
        sub             x1, x1, #4
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h}, [sp], x10
        ld1             {v17.8h}, [sp], x10
        ld1             {v18.8h}, [sp], x10
.macro calc src0, src1, src2, src3
        ld1             {\src3\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src1, \src2, \src3
        calc_epelh2     v4, v5, \src0, \src1, \src2, \src3
        ld1             {v6.8h}, [x4], x10
        sqadd           v4.8h, v4.8h, v6.8h
        sqrshrun        v4.8b, v4.8h, #7
        st1             {v4.s}[0], [x0], #4
        subs            w5, w5, #1
        st1             {v4.h}[2], [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_bi_hv8_8_end_neon
        load_epel_filterh x7, x6
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h}, [sp], x10
        ld1             {v17.8h}, [sp], x10
        ld1             {v18.8h}, [sp], x10
.macro calc src0, src1, src2, src3
        ld1             {\src3\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src1, \src2, \src3
        calc_epelh2     v4, v5, \src0, \src1, \src2, \src3
        ld1             {v6.8h}, [x4], x10
        sqadd           v4.8h, v4.8h, v6.8h
        sqrshrun        v4.8b, v4.8h, #7
        subs            w5, w5, #1
        st1             {v4.8b}, [x0], x1
.endm
1:      calc_all4
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_bi_hv12_8_end_neon
        load_epel_filterh x7, x6
        sub             x1, x1, #8
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h, v17.8h}, [sp], x10
        ld1             {v18.8h, v19.8h}, [sp], x10
        ld1             {v20.8h, v21.8h}, [sp], x10
.macro calc src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\src6\().8h, \src7\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src2, \src4, \src6
        calc_epelh2     v4, v5, \src0, \src2, \src4, \src6
        calc_epelh      v5,     \src1, \src3, \src5, \src7
        ld1             {v6.8h, v7.8h}, [x4], x10
        sqadd           v4.8h, v4.8h, v6.8h
        sqadd           v5.8h, v5.8h, v7.8h
        sqrshrun        v4.8b, v4.8h, #7
        sqrshrun2       v4.16b, v5.8h, #7
        st1             {v4.8b}, [x0], #8
        subs            w5, w5, #1
        st1             {v4.s}[2], [x0], x1
.endm
1:      calc_all8
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_bi_hv16_8_end_neon
        load_epel_filterh x7, x6
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h, v17.8h}, [sp], x10
        ld1             {v18.8h, v19.8h}, [sp], x10
        ld1             {v20.8h, v21.8h}, [sp], x10
.macro calc src0, src1, src2, src3, src4, src5, src6, src7
        ld1             {\src6\().8h, \src7\().8h}, [sp], x10
        calc_epelh      v4,     \src0, \src2, \src4, \src6
        calc_epelh2     v4, v5, \src0, \src2, \src4, \src6
        calc_epelh      v5,     \src1, \src3, \src5, \src7
        calc_epelh2     v5, v6, \src1, \src3, \src5, \src7
        ld1             {v6.8h, v7.8h}, [x4], x10
        sqadd           v4.8h, v4.8h, v6.8h
        sqadd           v5.8h, v5.8h, v7.8h
        sqrshrun        v4.8b, v4.8h, #7
        sqrshrun2       v4.16b, v5.8h, #7
        st1             {v4.16b}, [x0], x1
        subs            w5, w5, #1
.endm
1:      calc_all8
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_bi_hv24_8_end_neon
        load_epel_filterh x7, x6
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h, v17.8h, v18.8h}, [sp], x10
        ld1             {v19.8h, v20.8h, v21.8h}, [sp], x10
        ld1             {v22.8h, v23.8h, v24.8h}, [sp], x10
.macro calc src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11
        ld1             {\src9\().8h, \src10\().8h, \src11\().8h}, [sp], x10
        calc_epelh      v1,     \src0, \src3, \src6, \src9
        calc_epelh2     v1, v2, \src0, \src3, \src6, \src9
        calc_epelh      v2,     \src1, \src4, \src7, \src10
        calc_epelh2     v2, v3, \src1, \src4, \src7, \src10
        calc_epelh      v3,     \src2, \src5, \src8, \src11
        calc_epelh2     v3, v4, \src2, \src5, \src8, \src11
        ld1             {v4.8h, v5.8h, v6.8h}, [x4], x10
        sqadd           v1.8h, v1.8h, v4.8h
        sqadd           v2.8h, v2.8h, v5.8h
        sqadd           v3.8h, v3.8h, v6.8h
        sqrshrun        v1.8b, v1.8h, #7
        sqrshrun        v2.8b, v2.8h, #7
        sqrshrun        v3.8b, v3.8h, #7
        subs            w5, w5, #1
        st1             {v1.8b, v2.8b, v3.8b}, [x0], x1
.endm
1:      calc_all12
.purgem calc
2:      ret
endfunc

function hevc_put_hevc_epel_bi_hv32_8_end_neon
        load_epel_filterh x7, x6
        mov             x10, #(HEVC_MAX_PB_SIZE * 2)
        ld1             {v16.8h, v17.8h, v18.8h, v19.8h}, [sp], x10
        ld1             {v20.8h, v21.8h, v22.8h, v23.8h}, [sp], x10
        ld1             {v24.8h, v25.8h, v26.8h, v27.8h}, [sp], x10
.macro calc src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10, src11, src12, src13, src14, src15
        ld1             {\src12\().8h, \src13\().8h, \src14\().8h, \src15\().8h}, [sp], x10
        calc_epelh      v1,     \src0, \src4, \src8,  \src12
        calc_epelh2     v1, v2, \src0, \src4, \src8,  \src12
        calc_epelh      v2,     \src1, \src5, \src9,  \src13
        calc_epelh2     v2, v3, \src1, \src5, \src9,  \src13
        calc_epelh      v3,     \src2, \src6, \src10, \src14
        calc_epelh2     v3, v4, \src2, \src6, \src10, \src14
        calc_epelh      v4,     \src3, \src7, \src11, \src15
        calc_epelh2     v4, v5, \src3, \src7, \src11, \src15
        ld1             {v5.8h, v6.8h, v7.8h, v8.8h}, [x4], x10
        sqadd           v1.8h, v1.8h, v5.8h
        sqadd           v2.8h, v2.8h, v6.8h
        sqadd           v3.8h, v3.8h, v7.8h
        sqadd           v4.8h, v4.8h, v8.8h
        sqrshrun        v1.8b, v1.8h, #7
        sqrshrun        v2.8b, v2.8h, #7
        sqrshrun        v3.8b, v3.8h, #7
        sqrshrun        v4.8b, v4.8h, #7
        st1             {v1.8b, v2.8b, v3.8b, v4.8b}, [x0], x1
        subs            w5, w5, #1
.endm
1:      calc_all16
.purgem calc
2:      ldr             d8, [sp], #16
        ret
endfunc

.macro epel_bi_hv suffix
function ff_hevc_put_hevc_epel_bi_hv4_8_\suffix, export=1
        add             w10, w5, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x7, x30, [sp, #-48]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w5, #3
        mov             x4, x6
        mov             x5, x7
        bl              X(ff_hevc_put_hevc_epel_h4_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x7, x30, [sp], #48
        b               hevc_put_hevc_epel_bi_hv4_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_bi_hv6_8_\suffix, export=1
        add             w10, w5, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x7, x30, [sp, #-48]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w5, #3
        mov             x4, x6
        mov             x5, x7
        bl              X(ff_hevc_put_hevc_epel_h6_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x7, x30, [sp], #48
        b               hevc_put_hevc_epel_bi_hv6_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_bi_hv8_8_\suffix, export=1
        add             w10, w5, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x7, x30, [sp, #-48]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w5, #3
        mov             x4, x6
        mov             x5, x7
        bl              X(ff_hevc_put_hevc_epel_h8_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x7, x30, [sp], #48
        b               hevc_put_hevc_epel_bi_hv8_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_bi_hv12_8_\suffix, export=1
        add             w10, w5, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x7, x30, [sp, #-48]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w5, #3
        mov             x4, x6
        mov             x5, x7
        bl              X(ff_hevc_put_hevc_epel_h12_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x7, x30, [sp], #48
        b               hevc_put_hevc_epel_bi_hv12_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_bi_hv16_8_\suffix, export=1
        add             w10, w5, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x7, x30, [sp, #-48]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w5, #3
        mov             x4, x6
        mov             x5, x7
        bl              X(ff_hevc_put_hevc_epel_h16_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x7, x30, [sp], #48
        b               hevc_put_hevc_epel_bi_hv16_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_bi_hv24_8_\suffix, export=1
        add             w10, w5, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x7, x30, [sp, #-48]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w5, #3
        mov             x4, x6
        mov             x5, x7
        bl              X(ff_hevc_put_hevc_epel_h24_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x7, x30, [sp], #48
        b               hevc_put_hevc_epel_bi_hv24_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_bi_hv32_8_\suffix, export=1
        str             d8, [sp, #-16]!
        add             w10, w5, #3
        lsl             x10, x10, #7
        sub             sp, sp, x10 // tmp_array
        stp             x7, x30, [sp, #-48]!
        stp             x4, x5, [sp, #16]
        stp             x0, x1, [sp, #32]
        add             x0, sp, #48
        sub             x1, x2, x3
        mov             x2, x3
        add             w3, w5, #3
        mov             x4, x6
        mov             x5, x7
        mov             w6, #32
        bl              X(ff_hevc_put_hevc_epel_h32_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x0, x1, [sp, #32]
        ldp             x7, x30, [sp], #48
        b               hevc_put_hevc_epel_bi_hv32_8_end_neon
endfunc

function ff_hevc_put_hevc_epel_bi_hv48_8_\suffix, export=1
        stp             x6, x7, [sp, #-80]!
        stp             x4, x5, [sp, #16]
        stp             x2, x3, [sp, #32]
        stp             x0, x1, [sp, #48]
        str             x30, [sp, #64]
        bl              X(ff_hevc_put_hevc_epel_bi_hv24_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x2, x3, [sp, #32]
        ldp             x0, x1, [sp, #48]
        ldp             x6, x7, [sp], #64
        add             x0, x0, #24
        add             x2, x2, #24
        add             x4, x4, #48
        bl              X(ff_hevc_put_hevc_epel_bi_hv24_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc

function ff_hevc_put_hevc_epel_bi_hv64_8_\suffix, export=1
        stp             x6, x7, [sp, #-80]!
        stp             x4, x5, [sp, #16]
        stp             x2, x3, [sp, #32]
        stp             x0, x1, [sp, #48]
        str             x30, [sp, #64]
        bl              X(ff_hevc_put_hevc_epel_bi_hv32_8_\suffix)
        ldp             x4, x5, [sp, #16]
        ldp             x2, x3, [sp, #32]
        ldp             x0, x1, [sp, #48]
        ldp             x6, x7, [sp], #64
        add             x0, x0, #32
        add             x2, x2, #32
        add             x4, x4, #64
        bl              X(ff_hevc_put_hevc_epel_bi_hv32_8_\suffix)
        ldr             x30, [sp], #16
        ret
endfunc
.endm

epel_bi_hv neon

#if HAVE_I8MM
ENABLE_I8MM

epel_uni_w_hv neon_i8mm
epel_bi_hv neon_i8mm

DISABLE_I8MM
#endif


.macro EPEL_UNI_W_V_HEADER
        ldr             x12, [sp, #8]
        movrel          x9, epel_filters
        add             x9, x9, x12, lsl #2
        ld4r            {v0.16b, v1.16b, v2.16b, v3.16b}, [x9] // filter
        neg             v0.16b, v0.16b
        neg             v3.16b, v3.16b
        mov             w10, #-6
        sub             w10, w10, w5
        dup             v30.8h, w6
        dup             v31.4s, w10
        dup             v29.4s, w7
        sub             x2, x2, x3
.endm

.macro EPEL_UNI_W_V4_CALC d0, s0, s1, s2, s3
        movi            \d0\().16b, #0
        umlsl           \d0\().8h, \s0\().8b, v0.8b
        umlal           \d0\().8h, \s1\().8b, v1.8b
        umlal           \d0\().8h, \s2\().8b, v2.8b
        umlsl           \d0\().8h, \s3\().8b, v3.8b
        smull           \d0\().4s, \d0\().4h, v30.4h
        sqrshl          \d0\().4s, \d0\().4s, v31.4s
        sqadd           \d0\().4s, \d0\().4s, v29.4s
        sqxtn           \d0\().4h, \d0\().4s
        sqxtun          \d0\().8b, \d0\().8h
.endm

function ff_hevc_put_hevc_epel_uni_w_v4_8_neon, export=1
        EPEL_UNI_W_V_HEADER

        ldr             s4, [x2]
        ldr             s5, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             s6, [x2]
1:
        ldr             s7, [x2, x3]
        subs            w4, w4, #1
        add             x2, x2, x3, lsl #1
        EPEL_UNI_W_V4_CALC v16, v4, v5, v6, v7
        str             s16, [x0]
        b.eq            2f
        add             x0, x0, x1
        ldr             s4, [x2]
        subs            w4, w4, #1
        EPEL_UNI_W_V4_CALC v17, v5, v6, v7, v4
        str             s17, [x0]
        add             x0, x0, x1
        b.eq            2f
        ldr             s5, [x2, x3]
        subs            w4, w4, #1
        add             x2, x2, x3, lsl #1
        EPEL_UNI_W_V4_CALC v18, v6, v7, v4, v5
        str             s18, [x0]
        add             x0, x0, x1
        b.eq            2f
        ldr             s6, [x2]
        subs            w4, w4, #1
        EPEL_UNI_W_V4_CALC v19, v7, v4, v5, v6
        str             s19, [x0]
        add             x0, x0, x1
        b.hi            1b
2:
        ret
endfunc

.macro EPEL_UNI_W_V8_CALC d0, s0, s1, s2, s3, t0, t1
        movi            \d0\().16b, #0
        umlsl           \d0\().8h, \s0\().8b, v0.8b
        umlal           \d0\().8h, \s1\().8b, v1.8b
        umlal           \d0\().8h, \s2\().8b, v2.8b
        umlsl           \d0\().8h, \s3\().8b, v3.8b
        smull           \t0\().4s, \d0\().4h, v30.4h
        smull2          \t1\().4s, \d0\().8h, v30.8h
        sqrshl          \t0\().4s, \t0\().4s, v31.4s
        sqrshl          \t1\().4s, \t1\().4s, v31.4s
        sqadd           \t0\().4s, \t0\().4s, v29.4s
        sqadd           \t1\().4s, \t1\().4s, v29.4s
        sqxtn           \d0\().4h, \t0\().4s
        sqxtn2          \d0\().8h, \t1\().4s
        sqxtun          \d0\().8b, \d0\().8h
.endm

function ff_hevc_put_hevc_epel_uni_w_v6_8_neon, export=1
        EPEL_UNI_W_V_HEADER

        sub             x1, x1, #4
        ldr             d4, [x2]
        ldr             d5, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             d6, [x2]
1:
        ldr             d7, [x2, x3]
        subs            w4, w4, #1
        add             x2, x2, x3, lsl #1
        EPEL_UNI_W_V8_CALC v16, v4, v5, v6, v7, v20, v21
        str             s16, [x0], #4
        st1             {v16.h}[2], [x0], x1
        b.eq            2f
        ldr             d4, [x2]
        subs            w4, w4, #1
        EPEL_UNI_W_V8_CALC v17, v5, v6, v7, v4, v20, v21
        str             s17, [x0], #4
        st1             {v17.h}[2], [x0], x1
        b.eq            2f
        ldr             d5, [x2, x3]
        subs            w4, w4, #1
        add             x2, x2, x3, lsl #1
        EPEL_UNI_W_V8_CALC v18, v6, v7, v4, v5, v20, v21
        str             s18, [x0], #4
        st1             {v18.h}[2], [x0], x1
        b.eq            2f
        ldr             d6, [x2]
        subs            w4, w4, #1
        EPEL_UNI_W_V8_CALC v19, v7, v4, v5, v6, v20, v21
        str             s19, [x0], #4
        st1             {v19.h}[2], [x0], x1
        b.hi            1b
2:
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_v8_8_neon, export=1
        EPEL_UNI_W_V_HEADER

        ldr             d4, [x2]
        ldr             d5, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             d6, [x2]
1:
        ldr             d7, [x2, x3]
        subs            w4, w4, #1
        add             x2, x2, x3, lsl #1
        EPEL_UNI_W_V8_CALC v16, v4, v5, v6, v7, v20, v21
        str             d16, [x0]
        add             x0, x0, x1
        b.eq            2f
        ldr             d4, [x2]
        subs            w4, w4, #1
        EPEL_UNI_W_V8_CALC v17, v5, v6, v7, v4, v20, v21
        str             d17, [x0]
        add             x0, x0, x1
        b.eq            2f
        ldr             d5, [x2, x3]
        subs            w4, w4, #1
        add             x2, x2, x3, lsl #1
        EPEL_UNI_W_V8_CALC v18, v6, v7, v4, v5, v20, v21
        str             d18, [x0]
        add             x0, x0, x1
        b.eq            2f
        ldr             d6, [x2]
        subs            w4, w4, #1
        EPEL_UNI_W_V8_CALC v19, v7, v4, v5, v6, v20, v21
        str             d19, [x0]
        add             x0, x0, x1
        b.hi            1b
2:
        ret
endfunc

.macro EPEL_UNI_W_V12_CALC d0, d1, s0, s1, s2, s3, t0, t1, t2, t3
        movi            \d0\().16b, #0
        movi            \d1\().16b, #0
        umlsl           \d0\().8h, \s0\().8b, v0.8b
        umlsl2          \d1\().8h, \s0\().16b, v0.16b
        umlal           \d0\().8h, \s1\().8b, v1.8b
        umlal2          \d1\().8h, \s1\().16b, v1.16b
        umlal           \d0\().8h, \s2\().8b, v2.8b
        umlal2          \d1\().8h, \s2\().16b, v2.16b
        umlsl           \d0\().8h, \s3\().8b, v3.8b
        umlsl2          \d1\().8h, \s3\().16b, v3.16b

        smull           \t0\().4s, \d0\().4h, v30.4h
        smull2          \t1\().4s, \d0\().8h, v30.8h
        smull           \t2\().4s, \d1\().4h, v30.4h

        sqrshl          \t0\().4s, \t0\().4s, v31.4s
        sqrshl          \t1\().4s, \t1\().4s, v31.4s
        sqrshl          \t2\().4s, \t2\().4s, v31.4s
        sqadd           \t0\().4s, \t0\().4s, v29.4s
        sqadd           \t1\().4s, \t1\().4s, v29.4s
        sqadd           \t2\().4s, \t2\().4s, v29.4s

        sqxtn           \d0\().4h, \t0\().4s
        sqxtn2          \d0\().8h, \t1\().4s
        sqxtn           \d1\().4h, \t2\().4s
        sqxtun          \d0\().8b,  \d0\().8h
        sqxtun2         \d0\().16b, \d1\().8h
.endm

function ff_hevc_put_hevc_epel_uni_w_v12_8_neon, export=1
        EPEL_UNI_W_V_HEADER

        ldr             q4, [x2]
        ldr             q5, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             q6, [x2]
        sub             x1, x1, #8
1:
        ldr             q7, [x2, x3]
        subs            w4, w4, #1
        add             x2, x2, x3, lsl #1
        EPEL_UNI_W_V12_CALC v16, v17, v4, v5, v6, v7, v24, v25, v26, v27
        str             d16, [x0], #8
        st1             {v16.s}[2], [x0]
        add             x0, x0, x1
        b.eq            2f
        ldr             q4, [x2]
        subs            w4, w4, #1
        EPEL_UNI_W_V12_CALC v18, v19, v5, v6, v7, v4, v24, v25, v26, v27
        str             d18, [x0], #8
        st1             {v18.s}[2], [x0]
        add             x0, x0, x1
        b.eq            2f
        ldr             q5, [x2, x3]
        subs            w4, w4, #1
        add             x2, x2, x3, lsl #1
        EPEL_UNI_W_V12_CALC v20, v21, v6, v7, v4, v5, v24, v25, v26, v27
        str             d20, [x0], #8
        st1             {v20.s}[2], [x0]
        add             x0, x0, x1
        b.eq            2f
        ldr             q6, [x2]
        subs            w4, w4, #1
        EPEL_UNI_W_V12_CALC v22, v23, v7, v4, v5, v6, v24, v25, v26, v27
        str             d22, [x0], #8
        st1             {v22.s}[2], [x0]
        add             x0, x0, x1
        b.hi            1b
2:
        ret
endfunc

.macro EPEL_UNI_W_V16_CALC d0, d1, s0, s1, s2, s3, t0, t1, t2, t3
        movi            \d0\().16b, #0
        movi            \d1\().16b, #0
        umlsl           \d0\().8h, \s0\().8b, v0.8b
        umlsl2          \d1\().8h, \s0\().16b, v0.16b
        umlal           \d0\().8h, \s1\().8b, v1.8b
        umlal2          \d1\().8h, \s1\().16b, v1.16b
        umlal           \d0\().8h, \s2\().8b, v2.8b
        umlal2          \d1\().8h, \s2\().16b, v2.16b
        umlsl           \d0\().8h, \s3\().8b, v3.8b
        umlsl2          \d1\().8h, \s3\().16b, v3.16b

        smull           \t0\().4s, \d0\().4h, v30.4h
        smull2          \t1\().4s, \d0\().8h, v30.8h
        smull           \t2\().4s, \d1\().4h, v30.4h
        smull2          \t3\().4s, \d1\().8h, v30.8h

        sqrshl          \t0\().4s, \t0\().4s, v31.4s
        sqrshl          \t1\().4s, \t1\().4s, v31.4s
        sqrshl          \t2\().4s, \t2\().4s, v31.4s
        sqrshl          \t3\().4s, \t3\().4s, v31.4s
        sqadd           \t0\().4s, \t0\().4s, v29.4s
        sqadd           \t1\().4s, \t1\().4s, v29.4s
        sqadd           \t2\().4s, \t2\().4s, v29.4s
        sqadd           \t3\().4s, \t3\().4s, v29.4s

        sqxtn           \d0\().4h, \t0\().4s
        sqxtn2          \d0\().8h, \t1\().4s
        sqxtn           \d1\().4h, \t2\().4s
        sqxtn2          \d1\().8h, \t3\().4s
        sqxtun          \d0\().8b,  \d0\().8h
        sqxtun2         \d0\().16b, \d1\().8h
.endm


function ff_hevc_put_hevc_epel_uni_w_v16_8_neon, export=1
        EPEL_UNI_W_V_HEADER

        ldr             q4, [x2]
        ldr             q5, [x2, x3]
        add             x2, x2, x3, lsl #1
        ldr             q6, [x2]
1:
        ldr             q7, [x2, x3]
        subs            w4, w4, #1
        add             x2, x2, x3, lsl #1
        EPEL_UNI_W_V16_CALC v16, v17, v4, v5, v6, v7, v24, v25, v26, v27
        str             q16, [x0]
        add             x0, x0, x1
        b.eq            2f
        ldr             q4, [x2]
        subs            w4, w4, #1
        EPEL_UNI_W_V16_CALC v18, v19, v5, v6, v7, v4, v24, v25, v26, v27
        str             q18, [x0]
        add             x0, x0, x1
        b.eq            2f
        ldr             q5, [x2, x3]
        subs            w4, w4, #1
        add             x2, x2, x3, lsl #1
        EPEL_UNI_W_V16_CALC v20, v21, v6, v7, v4, v5, v24, v25, v26, v27
        str             q20, [x0]
        add             x0, x0, x1
        b.eq            2f
        ldr             q6, [x2]
        subs            w4, w4, #1
        EPEL_UNI_W_V16_CALC v22, v23, v7, v4, v5, v6, v24, v25, v26, v27
        str             q22, [x0]
        add             x0, x0, x1
        b.hi            1b
2:
        ret
endfunc



function ff_hevc_put_hevc_epel_uni_w_v24_8_neon, export=1
        EPEL_UNI_W_V_HEADER

        ldp             q16, q17, [x2]
        add             x2, x2, x3
        ldp             q18, q19, [x2]
        add             x2, x2, x3
        ldp             q20, q21, [x2]
        add             x2, x2, x3
1:
        ldp             q22, q23, [x2]
        subs            w4, w4, #1
        add             x2, x2, x3
        EPEL_UNI_W_V16_CALC v4, v5, v16, v18, v20, v22, v24, v25, v26, v27
        EPEL_UNI_W_V8_CALC  v6, v17, v19, v21, v23, v24, v25
        str             q4, [x0]
        str             d6, [x0, #16]
        add             x0, x0, x1
        b.eq            2f
        ldp             q16, q17, [x2]
        subs            w4, w4, #1
        add             x2, x2, x3
        EPEL_UNI_W_V16_CALC v4, v5, v18, v20, v22, v16, v24, v25, v26, v27
        EPEL_UNI_W_V8_CALC  v6, v19, v21, v23, v17, v24, v25
        str             q4, [x0]
        str             d6, [x0, #16]
        add             x0, x0, x1
        b.eq            2f
        ldp             q18, q19, [x2]
        subs            w4, w4, #1
        add             x2, x2, x3
        EPEL_UNI_W_V16_CALC v4, v5, v20, v22, v16, v18,  v24, v25, v26, v27
        EPEL_UNI_W_V8_CALC  v6, v21, v23, v17, v19, v24, v25
        str             q4, [x0]
        str             d6, [x0, #16]
        add             x0, x0, x1
        b.eq            2f
        ldp             q20, q21, [x2]
        subs            w4, w4, #1
        add             x2, x2, x3
        EPEL_UNI_W_V16_CALC v4, v5, v22, v16, v18, v20, v24, v25, v26, v27
        EPEL_UNI_W_V8_CALC  v6, v23, v17, v19, v21, v24, v25
        str             q4, [x0]
        str             d6, [x0, #16]
        add             x0, x0, x1
        b.hi            1b
2:
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_v32_8_neon, export=1
        EPEL_UNI_W_V_HEADER

        ldp             q16, q17, [x2]
        add             x2, x2, x3
        ldp             q18, q19, [x2]
        add             x2, x2, x3
        ldp             q20, q21, [x2]
        add             x2, x2, x3
1:
        ldp             q22, q23, [x2]
        subs            w4, w4, #1
        add             x2, x2, x3
        EPEL_UNI_W_V16_CALC v4, v5, v16, v18, v20, v22, v24, v25, v26, v27
        EPEL_UNI_W_V16_CALC v6, v7, v17, v19, v21, v23, v24, v25, v26, v27
        str             q4, [x0]
        str             q6, [x0, #16]
        add             x0, x0, x1
        b.eq            2f
        ldp             q16, q17, [x2]
        subs            w4, w4, #1
        add             x2, x2, x3
        EPEL_UNI_W_V16_CALC v4, v5, v18, v20, v22, v16, v24, v25, v26, v27
        EPEL_UNI_W_V16_CALC v6, v7, v19, v21, v23, v17, v24, v25, v26, v27
        str             q4, [x0]
        str             q6, [x0, #16]
        add             x0, x0, x1
        b.eq            2f
        ldp             q18, q19, [x2]
        subs            w4, w4, #1
        add             x2, x2, x3
        EPEL_UNI_W_V16_CALC v4, v5, v20, v22, v16, v18,  v24, v25, v26, v27
        EPEL_UNI_W_V16_CALC v6, v7, v21, v23, v17, v19, v24, v25, v26, v27
        str             q4, [x0]
        str             q6, [x0, #16]
        add             x0, x0, x1
        b.eq            2f
        ldp             q20, q21, [x2]
        subs            w4, w4, #1
        add             x2, x2, x3
        EPEL_UNI_W_V16_CALC v4, v5, v22, v16, v18, v20, v24, v25, v26, v27
        EPEL_UNI_W_V16_CALC v6, v7, v23, v17, v19, v21, v24, v25, v26, v27
        str             q4, [x0]
        str             q6, [x0, #16]
        add             x0, x0, x1
        b.hi            1b
2:
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_v48_8_neon, export=1
        EPEL_UNI_W_V_HEADER
        stp             d8, d9, [sp, #-32]!
        stp             d10, d11, [sp, #16]

        ld1             {v16.16b, v17.16b, v18.16b}, [x2], x3
        ld1             {v19.16b, v20.16b, v21.16b}, [x2], x3
        ld1             {v22.16b, v23.16b, v24.16b}, [x2], x3
1:
        ld1             {v25.16b, v26.16b, v27.16b}, [x2], x3
        subs            w4, w4, #1
        EPEL_UNI_W_V16_CALC v4, v6, v16, v19, v22, v25, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v5, v7, v17, v20, v23, v26, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v6, v7, v18, v21, v24, v27, v8, v9, v10, v11
        st1             {v4.16b, v5.16b, v6.16b}, [x0], x1
        b.eq            2f
        ld1             {v16.16b, v17.16b, v18.16b}, [x2], x3
        subs            w4, w4, #1
        EPEL_UNI_W_V16_CALC v4, v6, v19, v22, v25, v16, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v5, v7, v20, v23, v26, v17, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v6, v7, v21, v24, v27, v18, v8, v9, v10, v11
        st1             {v4.16b, v5.16b, v6.16b}, [x0], x1
        b.eq            2f
        ld1             {v19.16b, v20.16b, v21.16b}, [x2], x3
        subs            w4, w4, #1
        EPEL_UNI_W_V16_CALC v4, v6,  v22, v25, v16, v19, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v5, v7,  v23, v26, v17, v20, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v6, v7,  v24, v27, v18, v21, v8, v9, v10, v11
        st1             {v4.16b, v5.16b, v6.16b}, [x0], x1
        b.eq            2f
        ld1             {v22.16b, v23.16b, v24.16b}, [x2], x3
        subs            w4, w4, #1
        EPEL_UNI_W_V16_CALC v4, v6,  v25, v16, v19, v22, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v5, v7,  v26, v17, v20, v23, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v6, v7,  v27, v18, v21, v24, v8, v9, v10, v11
        st1             {v4.16b, v5.16b, v6.16b}, [x0], x1
        b.hi            1b
2:
        ldp             d10, d11, [sp, #16]
        ldp             d8, d9, [sp], #32
        ret
endfunc

function ff_hevc_put_hevc_epel_uni_w_v64_8_neon, export=1
        EPEL_UNI_W_V_HEADER
        stp             d8, d9, [sp, #-64]!
        stp             d10, d11, [sp, #16]
        stp             d12, d13, [sp, #32]
        stp             d14, d15, [sp, #48]

        ld1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x2], x3
        ld1             {v20.16b, v21.16b, v22.16b, v23.16b}, [x2], x3
        ld1             {v24.16b, v25.16b, v26.16b, v27.16b}, [x2], x3
1:
        ld1             {v12.16b, v13.16b, v14.16b, v15.16b}, [x2], x3
        subs            w4, w4, #1
        EPEL_UNI_W_V16_CALC v4, v6, v16, v20, v24, v12, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v5, v7, v17, v21, v25, v13, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v6, v7, v18, v22, v26, v14, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v7,v28, v19, v23, v27, v15, v8, v9, v10, v11
        st1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x0], x1
        b.eq            2f
        ld1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x2], x3
        subs            w4, w4, #1
        EPEL_UNI_W_V16_CALC v4, v6, v20, v24, v12, v16, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v5, v7, v21, v25, v13, v17, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v6, v7, v22, v26, v14, v18, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v7,v28, v23, v27, v15, v19, v8, v9, v10, v11
        st1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x0], x1
        b.eq            2f
        ld1             {v20.16b, v21.16b, v22.16b, v23.16b}, [x2], x3
        subs            w4, w4, #1
        EPEL_UNI_W_V16_CALC v4, v6, v24, v12, v16, v20, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v5, v7, v25, v13, v17, v21, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v6, v7, v26, v14, v18, v22, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v7,v28, v27, v15, v19, v23, v8, v9, v10, v11
        st1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x0], x1
        b.eq            2f
        ld1             {v24.16b, v25.16b, v26.16b, v27.16b}, [x2], x3
        subs            w4, w4, #1
        EPEL_UNI_W_V16_CALC v4, v6, v12, v16, v20, v24, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v5, v7, v13, v17, v21, v25, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v6, v7, v14, v18, v22, v26, v8, v9, v10, v11
        EPEL_UNI_W_V16_CALC v7,v28, v15, v19, v23, v27, v8, v9, v10, v11
        st1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x0], x1
        b.hi            1b
2:
        ldp             d10, d11, [sp, #16]
        ldp             d12, d13, [sp, #32]
        ldp             d14, d15, [sp, #48]
        ldp             d8, d9, [sp], #64
        ret
endfunc
