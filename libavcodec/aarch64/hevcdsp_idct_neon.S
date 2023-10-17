/*
 * ARM NEON optimised IDCT functions for HEVC decoding
 * Copyright (c) 2014 Seppo Tomperi <seppo.tomperi@vtt.fi>
 * Copyright (c) 2017 Alexandra Hájková
 *
 * Ported from arm/hevcdsp_idct_neon.S by
 * Copyright (c) 2020 Reimar Döffinger
 * Copyright (c) 2023 J. Dekker <jdek@itanimul.li>
 * Copyright (c) 2023 xu fulong <839789740@qq.com>
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
#include "neon.S"

const trans, align=4
        .short          64, 83, 64, 36
        .short          89, 75, 50, 18
        .short          90, 87, 80, 70
        .short          57, 43, 25, 9
        .short          90, 90, 88, 85
        .short          82, 78, 73, 67
        .short          61, 54, 46, 38
        .short          31, 22, 13, 4
endconst

function ff_hevc_add_residual_4x4_8_neon, export=1
        ld1             {v0.8h-v1.8h}, [x1]
        ld1             {v2.s}[0], [x0], x2
        ld1             {v2.s}[1], [x0], x2
        ld1             {v2.s}[2], [x0], x2
        ld1             {v2.s}[3], [x0], x2
        sub             x0, x0, x2, lsl #2
        uxtl            v6.8h,  v2.8b
        uxtl2           v7.8h,  v2.16b
        sqadd           v0.8h,  v0.8h, v6.8h
        sqadd           v1.8h,  v1.8h, v7.8h
        sqxtun          v0.8b,  v0.8h
        sqxtun2         v0.16b, v1.8h
        st1             {v0.s}[0], [x0], x2
        st1             {v0.s}[1], [x0], x2
        st1             {v0.s}[2], [x0], x2
        st1             {v0.s}[3], [x0], x2
        ret
endfunc

function ff_hevc_add_residual_8x8_8_neon, export=1
        add             x12, x0, x2
        add             x2, x2, x2
        mov             x3, #8
1:      subs            x3, x3, #2
        ld1             {v2.d}[0], [x0]
        ld1             {v2.d}[1], [x12]
        uxtl            v3.8h,  v2.8b
        ld1             {v0.8h-v1.8h}, [x1], #32
        uxtl2           v2.8h,  v2.16b
        sqadd           v0.8h,  v0.8h, v3.8h
        sqadd           v1.8h,  v1.8h, v2.8h
        sqxtun          v0.8b,  v0.8h
        sqxtun2         v0.16b, v1.8h
        st1             {v0.d}[0], [x0],  x2
        st1             {v0.d}[1], [x12], x2
        bne             1b
        ret
endfunc

function ff_hevc_add_residual_16x16_8_neon, export=1
        mov             x3,  #16
        add             x12, x0, x2
        add             x2,  x2, x2
1:      subs            x3,  x3, #2
        ld1             {v16.16b},     [x0]
        ld1             {v0.8h-v3.8h}, [x1], #64
        ld1             {v19.16b},    [x12]
        uxtl            v17.8h, v16.8b
        uxtl2           v18.8h, v16.16b
        uxtl            v20.8h, v19.8b
        uxtl2           v21.8h, v19.16b
        sqadd           v0.8h,  v0.8h, v17.8h
        sqadd           v1.8h,  v1.8h, v18.8h
        sqadd           v2.8h,  v2.8h, v20.8h
        sqadd           v3.8h,  v3.8h, v21.8h
        sqxtun          v0.8b,  v0.8h
        sqxtun2         v0.16b,  v1.8h
        sqxtun          v1.8b,  v2.8h
        sqxtun2         v1.16b,  v3.8h
        st1             {v0.16b},     [x0], x2
        st1             {v1.16b},    [x12], x2
        bne             1b
        ret
endfunc

function ff_hevc_add_residual_32x32_8_neon, export=1
        add             x12,  x0, x2
        add             x2,  x2, x2
        mov             x3,  #32
1:      subs            x3,  x3, #2
        ld1             {v20.16b, v21.16b}, [x0]
        uxtl            v16.8h,  v20.8b
        uxtl2           v17.8h,  v20.16b
        ld1             {v22.16b, v23.16b}, [x12]
        uxtl            v18.8h,  v21.8b
        uxtl2           v19.8h,  v21.16b
        uxtl            v20.8h,  v22.8b
        ld1             {v0.8h-v3.8h}, [x1], #64
        ld1             {v4.8h-v7.8h}, [x1], #64
        uxtl2           v21.8h,  v22.16b
        uxtl            v22.8h,  v23.8b
        uxtl2           v23.8h,  v23.16b
        sqadd           v0.8h,   v0.8h,   v16.8h
        sqadd           v1.8h,   v1.8h,   v17.8h
        sqadd           v2.8h,   v2.8h,   v18.8h
        sqadd           v3.8h,   v3.8h,   v19.8h
        sqadd           v4.8h,   v4.8h,   v20.8h
        sqadd           v5.8h,   v5.8h,   v21.8h
        sqadd           v6.8h,   v6.8h,   v22.8h
        sqadd           v7.8h,   v7.8h,   v23.8h
        sqxtun          v0.8b,   v0.8h
        sqxtun2         v0.16b,  v1.8h
        sqxtun          v1.8b,   v2.8h
        sqxtun2         v1.16b,  v3.8h
        sqxtun          v2.8b,   v4.8h
        sqxtun2         v2.16b,  v5.8h
        st1             {v0.16b, v1.16b}, [x0],  x2
        sqxtun          v3.8b,   v6.8h
        sqxtun2         v3.16b,  v7.8h
        st1             {v2.16b, v3.16b}, [x12], x2
        bne             1b
        ret
endfunc

.macro add_res bitdepth
function ff_hevc_add_residual_4x4_\bitdepth\()_neon, export=1
        mvni            v21.8h, #((0xFF << (\bitdepth - 8)) & 0xFF), lsl #8
        b               hevc_add_residual_4x4_16_neon
endfunc
function ff_hevc_add_residual_8x8_\bitdepth\()_neon, export=1
        mvni            v21.8h, #((0xFF << (\bitdepth - 8)) & 0xFF), lsl #8
        b               hevc_add_residual_8x8_16_neon
endfunc
function ff_hevc_add_residual_16x16_\bitdepth\()_neon, export=1
        mvni            v21.8h, #((0xFF << (\bitdepth - 8)) & 0xFF), lsl #8
        b               hevc_add_residual_16x16_16_neon
endfunc
function ff_hevc_add_residual_32x32_\bitdepth\()_neon, export=1
        mvni            v21.8h, #((0xFF << (\bitdepth - 8)) & 0xFF), lsl #8
        b               hevc_add_residual_32x32_16_neon
endfunc
.endm

add_res 10
add_res 12

function hevc_add_residual_4x4_16_neon, export=0
        mov             x12,  x0
        ld1             {v0.8h-v1.8h}, [x1]
        ld1             {v2.d}[0], [x12], x2
        ld1             {v2.d}[1], [x12], x2
        ld1             {v3.d}[0], [x12], x2
        sqadd           v0.8h, v0.8h, v2.8h
        ld1             {v3.d}[1], [x12], x2
        movi            v4.8h, #0
        sqadd           v1.8h, v1.8h, v3.8h
        clip            v4.8h, v21.8h, v0.8h, v1.8h
        st1             {v0.d}[0], [x0],  x2
        st1             {v0.d}[1], [x0],  x2
        st1             {v1.d}[0], [x0],  x2
        st1             {v1.d}[1], [x0],  x2
        ret
endfunc

function hevc_add_residual_8x8_16_neon, export=0
        add             x12, x0, x2
        add             x2,  x2, x2
        mov             x3,  #8
        movi            v4.8h, #0
1:      subs            x3,  x3, #2
        ld1             {v0.8h-v1.8h}, [x1], #32
        ld1             {v2.8h}, [x0]
        sqadd           v0.8h, v0.8h, v2.8h
        ld1             {v3.8h}, [x12]
        sqadd           v1.8h, v1.8h, v3.8h
        clip            v4.8h, v21.8h, v0.8h, v1.8h
        st1             {v0.8h}, [x0],  x2
        st1             {v1.8h}, [x12], x2
        bne             1b
        ret
endfunc

function hevc_add_residual_16x16_16_neon, export=0
        mov             x3,  #16
        movi            v20.8h, #0
        add             x12,  x0, x2
        add             x2,  x2, x2
1:      subs            x3,  x3, #2
        ld1             {v16.8h-v17.8h}, [x0]
        ld1             {v0.8h-v3.8h},   [x1], #64
        sqadd           v0.8h, v0.8h, v16.8h
        ld1             {v18.8h-v19.8h}, [x12]
        sqadd           v1.8h, v1.8h, v17.8h
        sqadd           v2.8h, v2.8h, v18.8h
        sqadd           v3.8h, v3.8h, v19.8h
        clip            v20.8h, v21.8h, v0.8h, v1.8h, v2.8h, v3.8h
        st1             {v0.8h-v1.8h}, [x0],  x2
        st1             {v2.8h-v3.8h}, [x12], x2
        bne             1b
        ret
endfunc

function hevc_add_residual_32x32_16_neon, export=0
        mov             x3, #32
        movi            v20.8h, #0
1:      subs            x3, x3, #1
        ld1             {v0.8h -v3.8h},  [x1], #64
        ld1             {v16.8h-v19.8h}, [x0]
        sqadd           v0.8h, v0.8h, v16.8h
        sqadd           v1.8h, v1.8h, v17.8h
        sqadd           v2.8h, v2.8h, v18.8h
        sqadd           v3.8h, v3.8h, v19.8h
        clip            v20.8h, v21.8h, v0.8h, v1.8h, v2.8h, v3.8h
        st1             {v0.8h-v3.8h}, [x0], x2
        bne             1b
        ret
endfunc

.macro tr_4x4 in0, in1, in2, in3, out0, out1, out2, out3, shift
        sshll           v20.4s, \in0, #6
        sshll           v21.4s, \in0, #6
        smull           v22.4s, \in1, v4.h[1]
        smull           v23.4s, \in1, v4.h[3]
        smlal           v20.4s, \in2, v4.h[0] //e0
        smlsl           v21.4s, \in2, v4.h[0] //e1
        smlal           v22.4s, \in3, v4.h[3] //o0
        smlsl           v23.4s, \in3, v4.h[1] //o1

        add             v24.4s, v20.4s, v22.4s
        sub             v20.4s, v20.4s, v22.4s
        add             v22.4s, v21.4s, v23.4s
        sub             v21.4s, v21.4s, v23.4s
        sqrshrn         \out0, v24.4s, #\shift
        sqrshrn         \out3, v20.4s, #\shift
        sqrshrn         \out1, v22.4s, #\shift
        sqrshrn         \out2, v21.4s, #\shift
.endm

.macro idct_4x4 bitdepth
function ff_hevc_idct_4x4_\bitdepth\()_neon, export=1
        ld1             {v0.4h-v3.4h}, [x0]

        movrel          x1, trans
        ld1             {v4.4h}, [x1]

        tr_4x4          v0.4h, v1.4h, v2.4h, v3.4h, v16.4h, v17.4h, v18.4h, v19.4h, 7
        transpose_4x8H  v16, v17, v18, v19, v26, v27, v28, v29

        tr_4x4          v16.4h, v17.4h, v18.4h, v19.4h, v0.4h, v1.4h, v2.4h, v3.4h, 20 - \bitdepth
        transpose_4x8H  v0, v1, v2, v3, v26, v27, v28, v29
        st1             {v0.4h-v3.4h}, [x0]
        ret
endfunc
.endm

.macro sum_sub out, in, c, op, p
  .ifc \op, +
        smlal\p         \out, \in, \c
  .else
        smlsl\p         \out, \in, \c
  .endif
.endm

.macro fixsqrshrn d, dt, n, m
  .ifc \dt, .8h
        sqrshrn2        \d\dt, \n\().4s, \m
  .else
        sqrshrn         \n\().4h, \n\().4s, \m
        mov             \d\().d[0], \n\().d[0]
  .endif
.endm

// uses and clobbers v28-v31 as temp registers
.macro tr_4x4_8 in0, in1, in2, in3, out0, out1, out2, out3, p1, p2
        sshll\p1        v28.4s, \in0, #6
        mov             v29.16b, v28.16b
        smull\p1        v30.4s, \in1, v0.h[1]
        smull\p1        v31.4s, \in1, v0.h[3]
        smlal\p2        v28.4s, \in2, v0.h[0] //e0
        smlsl\p2        v29.4s, \in2, v0.h[0] //e1
        smlal\p2        v30.4s, \in3, v0.h[3] //o0
        smlsl\p2        v31.4s, \in3, v0.h[1] //o1

        add             \out0, v28.4s, v30.4s
        add             \out1, v29.4s, v31.4s
        sub             \out2, v29.4s, v31.4s
        sub             \out3, v28.4s, v30.4s
.endm

.macro transpose_8x8 r0, r1, r2, r3, r4, r5, r6, r7
        transpose_4x8H  \r0, \r1, \r2, \r3, v2, v3, v4, v5
        transpose_4x8H  \r4, \r5, \r6, \r7, v2, v3, v4, v5
.endm

.macro tr_8x4 shift, in0,in0t, in1,in1t, in2,in2t, in3,in3t, in4,in4t, in5,in5t, in6,in6t, in7,in7t, p1, p2
        tr_4x4_8        \in0\in0t, \in2\in2t, \in4\in4t, \in6\in6t, v24.4s, v25.4s, v26.4s, v27.4s, \p1, \p2

        smull\p1        v30.4s, \in1\in1t, v0.h[6]
        smull\p1        v28.4s, \in1\in1t, v0.h[4]
        smull\p1        v29.4s, \in1\in1t, v0.h[5]
        sum_sub         v30.4s, \in3\in3t, v0.h[4], -, \p1
        sum_sub         v28.4s, \in3\in3t, v0.h[5], +, \p1
        sum_sub         v29.4s, \in3\in3t, v0.h[7], -, \p1

        sum_sub         v30.4s, \in5\in5t, v0.h[7], +, \p2
        sum_sub         v28.4s, \in5\in5t, v0.h[6], +, \p2
        sum_sub         v29.4s, \in5\in5t, v0.h[4], -, \p2

        sum_sub         v30.4s, \in7\in7t, v0.h[5], +, \p2
        sum_sub         v28.4s, \in7\in7t, v0.h[7], +, \p2
        sum_sub         v29.4s, \in7\in7t, v0.h[6], -, \p2

        add             v31.4s, v26.4s, v30.4s
        sub             v26.4s, v26.4s, v30.4s
        fixsqrshrn      \in2,\in2t, v31, \shift


        smull\p1        v31.4s, \in1\in1t, v0.h[7]
        sum_sub         v31.4s, \in3\in3t, v0.h[6], -, \p1
        sum_sub         v31.4s, \in5\in5t, v0.h[5], +, \p2
        sum_sub         v31.4s, \in7\in7t, v0.h[4], -, \p2
        fixsqrshrn      \in5,\in5t, v26, \shift


        add             v26.4s, v24.4s, v28.4s
        sub             v24.4s, v24.4s, v28.4s
        add             v28.4s, v25.4s, v29.4s
        sub             v25.4s, v25.4s, v29.4s
        add             v30.4s, v27.4s, v31.4s
        sub             v27.4s, v27.4s, v31.4s

        fixsqrshrn      \in0,\in0t, v26, \shift
        fixsqrshrn      \in7,\in7t, v24, \shift
        fixsqrshrn      \in1,\in1t, v28, \shift
        fixsqrshrn      \in6,\in6t, v25, \shift
        fixsqrshrn      \in3,\in3t, v30, \shift
        fixsqrshrn      \in4,\in4t, v27, \shift
.endm

.macro idct_8x8 bitdepth
function ff_hevc_idct_8x8_\bitdepth\()_neon, export=1
//x0 - coeffs
        mov             x1,  x0
        ld1             {v16.8h-v19.8h}, [x1], #64
        ld1             {v20.8h-v23.8h}, [x1]

        movrel          x1, trans
        ld1             {v0.8h}, [x1]

        tr_8x4          7, v16,.4h, v17,.4h, v18,.4h, v19,.4h, v20,.4h, v21,.4h, v22,.4h, v23,.4h
        tr_8x4          7, v16,.8h, v17,.8h, v18,.8h, v19,.8h, v20,.8h, v21,.8h, v22,.8h, v23,.8h, 2, 2

        transpose_8x8   v16, v17, v18, v19, v20, v21, v22, v23

        tr_8x4          20 - \bitdepth, v16,.4h, v17,.4h, v18,.4h, v19,.4h, v16,.8h, v17,.8h, v18,.8h, v19,.8h, , 2
        tr_8x4          20 - \bitdepth, v20,.4h, v21,.4h, v22,.4h, v23,.4h, v20,.8h, v21,.8h, v22,.8h, v23,.8h, , 2

        transpose_8x8   v16, v17, v18, v19, v20, v21, v22, v23

        mov             x1,  x0
        st1             {v16.8h-v19.8h}, [x1], #64
        st1             {v20.8h-v23.8h}, [x1]

        ret
endfunc
.endm

.macro butterfly e, o, tmp_p, tmp_m
        add             \tmp_p, \e, \o
        sub             \tmp_m, \e, \o
.endm

.macro tr16_8x4 in0, in1, in2, in3, offset
        tr_4x4_8        \in0\().4h, \in1\().4h, \in2\().4h, \in3\().4h, v24.4s, v25.4s, v26.4s, v27.4s

        smull2          v28.4s, \in0\().8h, v0.h[4]
        smull2          v29.4s, \in0\().8h, v0.h[5]
        smull2          v30.4s, \in0\().8h, v0.h[6]
        smull2          v31.4s, \in0\().8h, v0.h[7]
        sum_sub         v28.4s, \in1\().8h, v0.h[5], +, 2
        sum_sub         v29.4s, \in1\().8h, v0.h[7], -, 2
        sum_sub         v30.4s, \in1\().8h, v0.h[4], -, 2
        sum_sub         v31.4s, \in1\().8h, v0.h[6], -, 2

        sum_sub         v28.4s, \in2\().8h, v0.h[6], +, 2
        sum_sub         v29.4s, \in2\().8h, v0.h[4], -, 2
        sum_sub         v30.4s, \in2\().8h, v0.h[7], +, 2
        sum_sub         v31.4s, \in2\().8h, v0.h[5], +, 2

        sum_sub         v28.4s, \in3\().8h, v0.h[7], +, 2
        sum_sub         v29.4s, \in3\().8h, v0.h[6], -, 2
        sum_sub         v30.4s, \in3\().8h, v0.h[5], +, 2
        sum_sub         v31.4s, \in3\().8h, v0.h[4], -, 2

        butterfly       v24.4s, v28.4s, v16.4s, v23.4s
        butterfly       v25.4s, v29.4s, v17.4s, v22.4s
        butterfly       v26.4s, v30.4s, v18.4s, v21.4s
        butterfly       v27.4s, v31.4s, v19.4s, v20.4s
        add             x4,  sp,  #\offset
        st1             {v16.4s-v19.4s}, [x4], #64
        st1             {v20.4s-v23.4s}, [x4]
.endm

.macro load16 in0, in1, in2, in3
        ld1             {\in0}[0], [x1], x2
        ld1             {\in0}[1], [x3], x2
        ld1             {\in1}[0], [x1], x2
        ld1             {\in1}[1], [x3], x2
        ld1             {\in2}[0], [x1], x2
        ld1             {\in2}[1], [x3], x2
        ld1             {\in3}[0], [x1], x2
        ld1             {\in3}[1], [x3], x2
.endm

.macro add_member in, t0, t1, t2, t3, t4, t5, t6, t7, op0, op1, op2, op3, op4, op5, op6, op7, p
        sum_sub         v21.4s, \in, \t0, \op0, \p
        sum_sub         v22.4s, \in, \t1, \op1, \p
        sum_sub         v23.4s, \in, \t2, \op2, \p
        sum_sub         v24.4s, \in, \t3, \op3, \p
        sum_sub         v25.4s, \in, \t4, \op4, \p
        sum_sub         v26.4s, \in, \t5, \op5, \p
        sum_sub         v27.4s, \in, \t6, \op6, \p
        sum_sub         v28.4s, \in, \t7, \op7, \p
.endm

.macro butterfly16 in0, in1, in2, in3, in4, in5, in6, in7
        add             v20.4s, \in0, \in1
        sub             \in0, \in0, \in1
        add             \in1, \in2, \in3
        sub             \in2, \in2, \in3
        add             \in3, \in4, \in5
        sub             \in4, \in4, \in5
        add             \in5, \in6, \in7
        sub             \in6, \in6, \in7
.endm

.macro store16 in0, in1, in2, in3, rx
        st1             {\in0}[0], [x1], x2
        st1             {\in0}[1], [x3], \rx
        st1             {\in1}[0], [x1], x2
        st1             {\in1}[1], [x3], \rx
        st1             {\in2}[0], [x1], x2
        st1             {\in2}[1], [x3], \rx
        st1             {\in3}[0], [x1], x2
        st1             {\in3}[1], [x3], \rx
.endm

.macro scale out0, out1, out2, out3, in0, in1, in2, in3, in4, in5, in6, in7, shift
        sqrshrn         \out0\().4h, \in0, \shift
        sqrshrn2        \out0\().8h, \in1, \shift
        sqrshrn         \out1\().4h, \in2, \shift
        sqrshrn2        \out1\().8h, \in3, \shift
        sqrshrn         \out2\().4h, \in4, \shift
        sqrshrn2        \out2\().8h, \in5, \shift
        sqrshrn         \out3\().4h, \in6, \shift
        sqrshrn2        \out3\().8h, \in7, \shift
.endm

// use temp register to transpose, then we can reuse it
.macro transpose16_4x4_2 r0, r1, r2, r3, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5
        // lower halves
        trn1            \tmp0\().4h, \r0\().4h, \r1\().4h
        trn2            \tmp1\().4h, \r0\().4h, \r1\().4h
        trn1            \tmp2\().4h, \r2\().4h, \r3\().4h
        trn2            \tmp3\().4h, \r2\().4h, \r3\().4h
        trn1            \tmp4\().2s, \tmp0\().2s, \tmp2\().2s
        trn2            \tmp5\().2s, \tmp0\().2s, \tmp2\().2s
        trn1            \tmp0\().2s, \tmp1\().2s, \tmp3\().2s
        trn2            \tmp2\().2s, \tmp1\().2s, \tmp3\().2s
        mov             \r0\().d[0], \tmp4\().d[0]
        mov             \r2\().d[0], \tmp5\().d[0]
        mov             \r1\().d[0], \tmp0\().d[0]
        mov             \r3\().d[0], \tmp2\().d[0]

        // upper halves in reverse order
        trn1            \tmp0\().8h, \r3\().8h, \r2\().8h
        trn2            \tmp1\().8h, \r3\().8h, \r2\().8h
        trn1            \tmp2\().8h, \r1\().8h, \r0\().8h
        trn2            \tmp3\().8h, \r1\().8h, \r0\().8h
        trn1            \tmp4\().4s, \tmp0\().4s, \tmp2\().4s
        trn2            \tmp5\().4s, \tmp0\().4s, \tmp2\().4s
        trn1            \tmp0\().4s, \tmp1\().4s, \tmp3\().4s
        trn2            \tmp2\().4s, \tmp1\().4s, \tmp3\().4s
        mov             \r3\().d[1], \tmp4\().d[1]
        mov             \r1\().d[1], \tmp5\().d[1]
        mov             \r2\().d[1], \tmp0\().d[1]
        mov             \r0\().d[1], \tmp2\().d[1]
.endm

// stores in0, in2, in4, in6 ascending from off1 and
// stores in1, in3, in5, in7 descending from off2
.macro store_to_stack off1, off2, in0, in2, in4, in6, in7, in5, in3, in1
        add             x1, sp, #\off1
        add             x3, sp, #\off2
        mov             x2, #-16
        mov             x4, #16
        st1             {\in0}, [x1], x4
        st1             {\in1}, [x3], x2
        st1             {\in2}, [x1], x4
        st1             {\in3}, [x3], x2
        st1             {\in4}, [x1], x4
        st1             {\in5}, [x3], x2
        st1             {\in6}, [x1]
        st1             {\in7}, [x3]
.endm

.macro tr_16x4 name, shift, offset, step
function func_tr_16x4_\name
        mov             x1,  x5
        add             x3,  x5, #(\step * 64)
        mov             x2,  #(\step * 128)
        load16          v16.d, v17.d, v18.d, v19.d
        movrel          x1,  trans
        ld1             {v0.8h}, [x1]

        tr16_8x4        v16, v17, v18, v19, \offset

        add             x1,  x5, #(\step * 32)
        add             x3,  x5, #(\step * 3 *32)
        mov             x2,  #(\step * 128)
        load16          v20.d, v17.d, v18.d, v19.d
        movrel          x1, trans, 16
        ld1             {v1.8h}, [x1]
        smull           v21.4s, v20.4h, v1.h[0]
        smull           v22.4s, v20.4h, v1.h[1]
        smull           v23.4s, v20.4h, v1.h[2]
        smull           v24.4s, v20.4h, v1.h[3]
        smull           v25.4s, v20.4h, v1.h[4]
        smull           v26.4s, v20.4h, v1.h[5]
        smull           v27.4s, v20.4h, v1.h[6]
        smull           v28.4s, v20.4h, v1.h[7]

        add_member      v20.8h, v1.h[1], v1.h[4], v1.h[7], v1.h[5], v1.h[2], v1.h[0], v1.h[3], v1.h[6], +, +, +, -, -, -, -, -, 2
        add_member      v17.4h, v1.h[2], v1.h[7], v1.h[3], v1.h[1], v1.h[6], v1.h[4], v1.h[0], v1.h[5], +, +, -, -, -, +, +, +
        add_member      v17.8h, v1.h[3], v1.h[5], v1.h[1], v1.h[7], v1.h[0], v1.h[6], v1.h[2], v1.h[4], +, -, -, +, +, +, -, -, 2
        add_member      v18.4h, v1.h[4], v1.h[2], v1.h[6], v1.h[0], v1.h[7], v1.h[1], v1.h[5], v1.h[3], +, -, -, +, -, -, +, +
        add_member      v18.8h, v1.h[5], v1.h[0], v1.h[4], v1.h[6], v1.h[1], v1.h[3], v1.h[7], v1.h[2], +, -, +, +, -, +, +, -, 2
        add_member      v19.4h, v1.h[6], v1.h[3], v1.h[0], v1.h[2], v1.h[5], v1.h[7], v1.h[4], v1.h[1], +, -, +, -, +, +, -, +
        add_member      v19.8h, v1.h[7], v1.h[6], v1.h[5], v1.h[4], v1.h[3], v1.h[2], v1.h[1], v1.h[0], +, -, +, -, +, -, +, -, 2

        add             x4, sp, #\offset
        ld1             {v16.4s-v19.4s}, [x4], #64
        butterfly16     v16.4s, v21.4s, v17.4s, v22.4s, v18.4s, v23.4s, v19.4s, v24.4s
    .if \shift > 0
        scale           v29, v30, v31, v24, v20.4s, v16.4s, v21.4s, v17.4s, v22.4s, v18.4s, v23.4s, v19.4s, \shift
        transpose16_4x4_2 v29, v30, v31, v24, v2, v3, v4, v5, v6, v7
        mov             x1,  x6
        add             x3,  x6, #(24 +3*32)
        mov             x2, #32
        mov             x4, #-32
        store16         v29.d, v30.d, v31.d, v24.d, x4
    .else
        store_to_stack  \offset, (\offset + 240), v20.4s, v21.4s, v22.4s, v23.4s, v19.4s, v18.4s, v17.4s, v16.4s
    .endif

        add             x4, sp, #(\offset + 64)
        ld1             {v16.4s-v19.4s}, [x4]
        butterfly16     v16.4s, v25.4s, v17.4s, v26.4s, v18.4s, v27.4s, v19.4s, v28.4s
   .if \shift > 0
        scale           v29, v30, v31, v20, v20.4s, v16.4s, v25.4s, v17.4s, v26.4s, v18.4s, v27.4s, v19.4s, \shift
        transpose16_4x4_2 v29, v30, v31, v20, v2, v3, v4, v5, v6, v7

        add             x1,  x6, #8
        add             x3,  x6, #(16 + 3 * 32)
        mov             x2, #32
        mov             x4, #-32
        store16         v29.d, v30.d, v31.d, v20.d, x4
   .else
        store_to_stack  (\offset + 64), (\offset + 176), v20.4s, v25.4s, v26.4s, v27.4s, v19.4s, v18.4s, v17.4s, v16.4s
   .endif

        ret
endfunc
.endm

.macro idct_16x16 bitdepth
function ff_hevc_idct_16x16_\bitdepth\()_neon, export=1
//r0 - coeffs
        mov             x15, x30

        // allocate a temp buffer
        sub             sp,  sp,  #640

.irp i, 0, 1, 2, 3
        add             x5,  x0, #(8 * \i)
        add             x6,  sp, #(8 * \i * 16)
        bl              func_tr_16x4_firstpass
.endr

.irp i, 0, 1, 2, 3
        add             x5,  sp, #(8 * \i)
        add             x6,  x0, #(8 * \i * 16)
        bl              func_tr_16x4_secondpass_\bitdepth
.endr

        add             sp,  sp,  #640

        ret             x15
endfunc
.endm

.macro load32
        add             x1,  x5, #64
        add             x3,  x1, #128
        mov             x2,  #256
        ld1             {v4.d}[0],  [x1], x2
        ld1             {v4.d}[1],  [x3], x2
        ld1             {v5.d}[0],  [x1], x2
        ld1             {v5.d}[1],  [x3], x2
        ld1             {v6.d}[0],  [x1], x2
        ld1             {v6.d}[1],  [x3], x2
        ld1             {v7.d}[0],  [x1], x2
        ld1             {v7.d}[1],  [x3], x2
        ld1             {v16.d}[0], [x1], x2
        ld1             {v16.d}[1], [x3], x2
        ld1             {v17.d}[0], [x1], x2
        ld1             {v17.d}[1], [x3], x2
        ld1             {v18.d}[0], [x1], x2
        ld1             {v18.d}[1], [x3], x2
        ld1             {v19.d}[0], [x1], x2
        ld1             {v19.d}[1], [x3], x2
.endm

.macro add_member32 in, t0, t1, t2, t3, op0, op1, op2, op3, p
        sum_sub         v24.4s, \in, \t0, \op0, \p
        sum_sub         v25.4s, \in, \t1, \op1, \p
        sum_sub         v26.4s, \in, \t2, \op2, \p
        sum_sub         v27.4s, \in, \t3, \op3, \p
.endm

.macro butterfly32 in0, in1, in2, in3, out
        add             \out, \in0, \in1
        sub             \in0, \in0, \in1
        add             \in1, \in2, \in3
        sub             \in2, \in2, \in3
.endm

.macro multiply in
        smull           v24.4s, v4.4h, \in\().h[0]
        smull           v25.4s, v4.4h, \in\().h[1]
        smull           v26.4s, v4.4h, \in\().h[2]
        smull           v27.4s, v4.4h, \in\().h[3]
.endm

.macro scale_store shift
        ld1             {v28.8h-v31.8h}, [x4], #64
        butterfly32     v28.4s, v24.4s, v29.4s, v25.4s, v2.4s
        butterfly32     v30.4s, v26.4s, v31.4s, v27.4s, v3.4s
        scale           v20, v21, v22, v23, v2.4s, v28.4s, v24.4s, v29.4s, v3.4s, v30.4s, v26.4s, v31.4s, \shift

        transpose16_4x4_2 v20, v21, v22, v23, v24, v25, v26, v27, v28, v29
        store16         v20.d, v21.d, v22.d, v23.d, x8

        // reload coefficients
        ld1             {v2.4h-v3.4h}, [x9]
.endm

function tr_block1
        multiply        v0
        add_member32    v4.8h,  v0.h[1], v1.h[0], v1.h[3], v2.h[2], +, +, +, +, 2
        add_member32    v5.4h,  v0.h[2], v1.h[3], v3.h[0], v3.h[2], +, +, +, -
        add_member32    v5.8h,  v0.h[3], v2.h[2], v3.h[2], v1.h[3], +, +, -, -, 2
        add_member32    v6.4h,  v1.h[0], v3.h[1], v2.h[1], v0.h[0], +, +, -, -
        add_member32    v6.8h,  v1.h[1], v3.h[3], v1.h[0], v1.h[2], +, -, -, -, 2
        add_member32    v7.4h,  v1.h[2], v3.h[0], v0.h[0], v3.h[1], +, -, -, -
        add_member32    v7.8h,  v1.h[3], v2.h[1], v1.h[1], v2.h[3], +, -, -, +, 2
        add_member32    v16.4h, v2.h[0], v1.h[2], v2.h[2], v1.h[0], +, -, -, +
        add_member32    v16.8h, v2.h[1], v0.h[3], v3.h[3], v0.h[2], +, -, -, +, 2
        add_member32    v17.4h, v2.h[2], v0.h[1], v2.h[3], v2.h[1], +, -, +, +
        add_member32    v17.8h, v2.h[3], v0.h[2], v1.h[2], v3.h[3], +, -, +, -, 2
        add_member32    v18.4h, v3.h[0], v1.h[1], v0.h[1], v2.h[0], +, -, +, -
        add_member32    v18.8h, v3.h[1], v2.h[0], v0.h[3], v0.h[1], +, -, +, -, 2
        add_member32    v19.4h, v3.h[2], v2.h[3], v2.h[0], v1.h[1], +, -, +, -
        add_member32    v19.8h, v3.h[3], v3.h[2], v3.h[1], v3.h[0], +, -, +, -, 2
        ret
endfunc

function tr_block2
        multiply        v1
        add_member32    v4.8h,  v3.h[1], v3.h[3], v3.h[0], v2.h[1], +, -, -, -, 2
        add_member32    v5.4h,  v2.h[1], v1.h[0], v0.h[0], v1.h[1], -, -, -, -
        add_member32    v5.8h,  v0.h[0], v1.h[2], v3.h[1], v2.h[3], -, -, -, +, 2
        add_member32    v6.4h,  v2.h[0], v3.h[2], v1.h[1], v0.h[3], -, +, +, +
        add_member32    v6.8h,  v3.h[2], v0.h[3], v1.h[3], v3.h[1], +, +, +, -, 2
        add_member32    v7.4h,  v1.h[1], v1.h[3], v2.h[3], v0.h[0], +, +, -, -
        add_member32    v7.8h,  v0.h[3], v3.h[1], v0.h[1], v3.h[3], +, -, -, +, 2
        add_member32    v16.4h, v3.h[0], v0.h[2], v3.h[2], v0.h[1], +, -, -, +
        add_member32    v16.8h, v2.h[2], v2.h[0], v1.h[0], v3.h[2], -, -, +, +, 2
        add_member32    v17.4h, v0.h[1], v3.h[0], v2.h[0], v0.h[2], -, +, +, -
        add_member32    v17.8h, v1.h[3], v0.h[1], v2.h[2], v3.h[0], -, +, -, -, 2
        add_member32    v18.4h, v3.h[3], v2.h[1], v0.h[2], v1.h[0], +, +, -, +
        add_member32    v18.8h, v1.h[2], v2.h[3], v3.h[3], v2.h[2], +, -, -, +, 2
        add_member32    v19.4h, v0.h[2], v0.h[1], v0.h[3], v1.h[2], +, -, +, -
        add_member32    v19.8h, v2.h[3], v2.h[2], v2.h[1], v2.h[0], +, -, +, -, 2
        ret
endfunc

function tr_block3
        multiply        v2
        add_member32    v4.8h,  v1.h[2], v0.h[3], v0.h[0], v0.h[2], -, -, -, -, 2
        add_member32    v5.4h,  v2.h[2], v3.h[3], v2.h[3], v1.h[2], -, -, +, +
        add_member32    v5.8h,  v1.h[0], v0.h[2], v2.h[1], v3.h[3], +, +, +, -, 2
        add_member32    v6.4h,  v3.h[0], v2.h[2], v0.h[1], v1.h[3], +, -, -, -
        add_member32    v6.8h,  v0.h[2], v2.h[0], v3.h[0], v0.h[0], -, -, +, +, 2
        add_member32    v7.4h,  v3.h[2], v1.h[0], v2.h[0], v2.h[2], -, +, +, -
        add_member32    v7.8h,  v0.h[0], v3.h[2], v0.h[2], v3.h[0], +, +, -, -, 2
        add_member32    v16.4h, v3.h[3], v0.h[1], v3.h[1], v0.h[3], -, -, +, +
        add_member32    v16.8h, v0.h[1], v2.h[3], v1.h[3], v1.h[1], -, +, +, -, 2
        add_member32    v17.4h, v3.h[1], v1.h[3], v0.h[3], v3.h[2], +, +, -, +
        add_member32    v17.8h, v0.h[3], v1.h[1], v3.h[2], v2.h[0], +, -, +, +, 2
        add_member32    v18.4h, v2.h[3], v3.h[1], v1.h[2], v0.h[1], -, -, +, -
        add_member32    v18.8h, v1.h[1], v0.h[0], v1.h[0], v2.h[1], -, +, -, +, 2
        add_member32    v19.4h, v2.h[1], v3.h[0], v3.h[3], v3.h[1], +, -, +, +
        add_member32    v19.8h, v1.h[3], v1.h[2], v1.h[1], v1.h[0], +, -, +, -, 2
        ret
endfunc

function tr_block4
        multiply        v3
        add_member32    v4.8h,  v1.h[1], v2.h[0], v2.h[3], v3.h[2], -, -, -, -, 2
        add_member32    v5.4h,  v0.h[0], v0.h[3], v2.h[0], v3.h[1], +, +, +, +
        add_member32    v5.8h,  v2.h[0], v0.h[0], v1.h[1], v3.h[0], -, -, -, -, 2
        add_member32    v6.4h,  v3.h[3], v1.h[2], v0.h[2], v2.h[3], +, +, +, +
        add_member32    v6.8h,  v2.h[1], v2.h[3], v0.h[0], v2.h[2], +, -, -, -, 2
        add_member32    v7.4h,  v0.h[2], v3.h[3], v0.h[3], v2.h[1], -, -, +, +
        add_member32    v7.8h,  v1.h[0], v2.h[2], v1.h[2], v2.h[0], +, +, -, -, 2
        add_member32    v16.4h, v2.h[3], v1.h[1], v2.h[1], v1.h[3], -, -, +, +
        add_member32    v16.8h, v3.h[1], v0.h[1], v3.h[0], v1.h[2], -, +, -, -, 2
        add_member32    v17.4h, v1.h[2], v1.h[0], v3.h[3], v1.h[1], +, -, +, +
        add_member32    v17.8h, v0.h[1], v2.h[1], v3.h[1], v1.h[0], -, +, +, -, 2
        add_member32    v18.4h, v1.h[3], v3.h[2], v2.h[2], v0.h[3], +, -, -, +
        add_member32    v18.8h, v3.h[2], v3.h[0], v1.h[3], v0.h[2], -, -, +, -, 2
        add_member32    v19.4h, v2.h[2], v1.h[3], v1.h[0], v0.h[1], -, +, -, +
        add_member32    v19.8h, v0.h[3], v0.h[2], v0.h[1], v0.h[0], +, -, +, -, 2
        ret
endfunc

.macro tr_32x4 name, shift
function func_tr_32x4_\name
        mov             x10, x30
        bl              func_tr_16x4_noscale

        load32
        movrel          x9, trans, 32
        ld1             {v0.4h-v1.4h}, [x9], #16
        ld1             {v2.4h-v3.4h}, [x9]
        add             x4, sp, #2048
        mov             x2, #64
        mov             x8, #-64

        bl              tr_block1
        mov             x1, x11
        add             x3, x11, #(56 + 3 * 64)
        scale_store     \shift

        bl              tr_block2
        add             x1, x11, #8
        add             x3, x11, #(48 + 3 * 64)
        scale_store     \shift

        bl              tr_block3
        add             x1, x11, #16
        add             x3, x11, #(40 + 3 * 64)
        scale_store     \shift

        bl              tr_block4
        add             x1, x11, #24
        add             x3, x11, #(32 + 3 * 64)
        scale_store     \shift

        ret             x10
endfunc
.endm

.macro idct_32x32 bitdepth
function ff_hevc_idct_32x32_\bitdepth\()_neon, export=1
        mov             x15, x30
        // allocate a temp buffer
        sub             sp,  sp,  #2432

.irp i, 0, 1, 2, 3, 4, 5, 6, 7
        add             x5, x0, #(8 * \i)
        add             x11, sp, #(8 * \i * 32)
        bl              func_tr_32x4_firstpass
.endr

.irp i, 0, 1, 2, 3, 4, 5, 6, 7
        add             x5, sp, #(8 * \i)
        add             x11, x0, #(8 * \i * 32)
        bl              func_tr_32x4_secondpass_\bitdepth
.endr

        add             sp,  sp,  #2432
        ret             x15
endfunc
.endm

idct_4x4 8
idct_4x4 10

idct_8x8 8
idct_8x8 10

tr_16x4 firstpass, 7, 512, 1
tr_16x4 secondpass_8, 20 - 8, 512, 1
tr_16x4 secondpass_10, 20 - 10, 512, 1
tr_16x4 noscale, 0, 2048, 4

idct_16x16 8
idct_16x16 10

.ltorg
tr_32x4 firstpass, 7
tr_32x4 secondpass_8, 20 - 8
tr_32x4 secondpass_10, 20 - 10
.ltorg

idct_32x32 8
idct_32x32 10

.macro tr4_luma_shift r0, r1, r2, r3, shift
        saddl           v0.4s, \r0, \r2         // c0 = src0 + src2
        saddl           v1.4s, \r2, \r3         // c1 = src2 + src3
        ssubl           v2.4s, \r0, \r3         // c2 = src0 - src3
        smull           v3.4s, \r1, v21.4h      // c3 = 74 * src1

        saddl           v7.4s, \r0, \r3         // src0 + src3
        ssubw           v7.4s, v7.4s, \r2       // src0 - src2 + src3
        mul             v7.4s, v7.4s, v18.4s    // dst2 = 74 * (src0 - src2 + src3)

        mul             v5.4s, v0.4s, v19.4s    // 29 * c0
        mul             v6.4s, v1.4s, v20.4s    // 55 * c1
        add             v5.4s, v5.4s, v6.4s     // 29 * c0 + 55 * c1
        add             v5.4s, v5.4s, v3.4s     // dst0 = 29 * c0 + 55 * c1 + c3

        mul             v1.4s, v1.4s, v19.4s    // 29 * c1
        mul             v6.4s, v2.4s, v20.4s    // 55 * c2
        sub             v6.4s, v6.4s, v1.4s     // 55 * c2 - 29 * c1
        add             v6.4s, v6.4s, v3.4s     // dst1 = 55 * c2 - 29 * c1 + c3

        mul             v0.4s, v0.4s, v20.4s    // 55 * c0
        mul             v2.4s, v2.4s, v19.4s    // 29 * c2
        add             v0.4s, v0.4s, v2.4s     // 55 * c0 + 29 * c2
        sub             v0.4s, v0.4s, v3.4s     // dst3 = 55 * c0 + 29 * c2 - c3

        sqrshrn         \r0, v5.4s, \shift
        sqrshrn         \r1, v6.4s, \shift
        sqrshrn         \r2, v7.4s, \shift
        sqrshrn         \r3, v0.4s, \shift
.endm

function ff_hevc_transform_luma_4x4_neon_8, export=1
        ld1             {v28.4h-v31.4h}, [x0]
        movi            v18.4s, #74
        movi            v19.4s, #29
        movi            v20.4s, #55
        movi            v21.4h, #74

        tr4_luma_shift  v28.4h, v29.4h, v30.4h, v31.4h, #7
        transpose_4x4H  v28, v29, v30, v31, v22, v23, v24, v25

        tr4_luma_shift  v28.4h, v29.4h, v30.4h, v31.4h, #12
        transpose_4x4H  v28, v29, v30, v31, v22, v23, v24, v25

        st1             {v28.4h-v31.4h}, [x0]
        ret
endfunc

// void ff_hevc_idct_NxN_dc_DEPTH_neon(int16_t *coeffs)
.macro idct_dc size, bitdepth
function ff_hevc_idct_\size\()x\size\()_dc_\bitdepth\()_neon, export=1
        ld1r            {v4.8h}, [x0]
        srshr           v4.8h,  v4.8h,  #1
        srshr           v0.8h,  v4.8h,  #(14 - \bitdepth)
        srshr           v1.8h,  v4.8h,  #(14 - \bitdepth)
.if \size > 4
        srshr           v2.8h,  v4.8h,  #(14 - \bitdepth)
        srshr           v3.8h,  v4.8h,  #(14 - \bitdepth)
.if \size > 16 /* dc 32x32 */
        mov             x2,  #4
1:
        subs            x2,  x2, #1
.endif
        add             x12,  x0, #64
        mov             x13,  #128
.if \size > 8 /* dc 16x16 */
        st1             {v0.8h-v3.8h},  [x0], x13
        st1             {v0.8h-v3.8h}, [x12], x13
        st1             {v0.8h-v3.8h},  [x0], x13
        st1             {v0.8h-v3.8h}, [x12], x13
        st1             {v0.8h-v3.8h},  [x0], x13
        st1             {v0.8h-v3.8h}, [x12], x13
.endif /* dc 8x8 */
        st1             {v0.8h-v3.8h},  [x0], x13
        st1             {v0.8h-v3.8h}, [x12], x13
.if \size > 16 /* dc 32x32 */
        bne             1b
.endif
.else /* dc 4x4 */
        st1             {v0.8h-v1.8h},  [x0]
.endif
        ret
endfunc
.endm

idct_dc 4, 8
idct_dc 4, 10

idct_dc 8, 8
idct_dc 8, 10

idct_dc 16, 8
idct_dc 16, 10

idct_dc 32, 8
idct_dc 32, 10
