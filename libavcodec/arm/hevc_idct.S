/*
 * ARM NEON optimised IDCT functions for HEVC decoding
 * Copyright (c) 2017 Alexandra Hájková
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/arm/asm.S"

const trans, align=4
        .short 64, 83, 64, 36
        .short 89, 75, 50, 18
        .short 90, 87, 80, 70
        .short 57, 43, 25, 9
endconst

.macro sum_sub out, in, c, op
  .ifc \op, +
        vmlal.s16       \out, \in, \c
  .else
        vmlsl.s16       \out, \in, \c
  .endif
.endm

.macro tr_4x4 in0, in1, in2, in3, out0, out1, out2, out3, shift, tmp0, tmp1, tmp2, tmp3, tmp4
         vshll.s16      \tmp0, \in0, #6
         vmull.s16      \tmp2, \in1, d4[1]
         vmov           \tmp1, \tmp0
         vmull.s16      \tmp3, \in1, d4[3]
         vmlal.s16      \tmp0, \in2, d4[0] @e0
         vmlsl.s16      \tmp1, \in2, d4[0] @e1
         vmlal.s16      \tmp2, \in3, d4[3] @o0
         vmlsl.s16      \tmp3, \in3, d4[1] @o1

         vadd.s32       \tmp4, \tmp0, \tmp2
         vsub.s32       \tmp0, \tmp0, \tmp2
         vadd.s32       \tmp2, \tmp1, \tmp3
         vsub.s32       \tmp1, \tmp1, \tmp3
         vqrshrn.s32    \out0, \tmp4, #\shift
         vqrshrn.s32    \out3, \tmp0, #\shift
         vqrshrn.s32    \out1, \tmp2, #\shift
         vqrshrn.s32    \out2, \tmp1, #\shift
.endm

.macro tr_4x4_8 in0, in1, in2, in3, out0, out1, out2, out3, tmp0, tmp1, tmp2, tmp3
         vshll.s16      \tmp0, \in0, #6
         vld1.s16       {\in0}, [r1, :64]!
         vmov           \tmp1, \tmp0
         vmull.s16      \tmp2, \in1, \in0[1]
         vmull.s16      \tmp3, \in1, \in0[3]
         vmlal.s16      \tmp0, \in2, \in0[0] @e0
         vmlsl.s16      \tmp1, \in2, \in0[0] @e1
         vmlal.s16      \tmp2, \in3, \in0[3] @o0
         vmlsl.s16      \tmp3, \in3, \in0[1] @o1

         vld1.s16       {\in0}, [r1, :64]

         vadd.s32       \out0, \tmp0, \tmp2
         vadd.s32       \out1, \tmp1, \tmp3
         vsub.s32       \out2, \tmp1, \tmp3
         vsub.s32       \out3, \tmp0, \tmp2

         sub            r1,  r1,  #8
.endm

@ Do a 4x4 transpose, using q registers for the subtransposes that don't
@ need to address the indiviudal d registers.
@ r0,r1 == rq0, r2,r3 == rq1
.macro transpose_4x4 rq0, rq1, r0, r1, r2, r3
        vtrn.32         \rq0, \rq1
        vtrn.16         \r0,  \r1
        vtrn.16         \r2,  \r3
.endm

.macro idct_4x4 bitdepth
function ff_hevc_idct_4x4_\bitdepth\()_neon, export=1
@r0 - coeffs
        vld1.s16        {q0-q1}, [r0, :128]

        movrel          r1, trans
        vld1.s16        {d4}, [r1, :64]

        tr_4x4          d0, d1, d2, d3, d16, d17, d18, d19, 7, q10, q11, q12, q13, q0
        transpose_4x4   q8, q9, d16, d17, d18, d19

        tr_4x4          d16, d17, d18, d19, d0, d1, d2, d3, 20 - \bitdepth, q10, q11, q12, q13, q0
        transpose_4x4   q0, q1, d0, d1, d2, d3
        vst1.s16        {d0-d3}, [r0, :128]
        bx lr
endfunc
.endm

.macro transpose8_4x4 r0, r1, r2, r3
        vtrn.16         \r0,  \r1
        vtrn.16         \r2,  \r3
        vtrn.32         \r0,  \r2
        vtrn.32         \r1,  \r3
.endm

.macro transpose_8x8 r0, r1, r2, r3, r4, r5, r6, r7, l0, l1, l2, l3, l4, l5, l6, l7
        transpose8_4x4  \r0, \r1, \r2, \r3
        transpose8_4x4  \r4, \r5, \r6, \r7

        transpose8_4x4  \l0, \l1, \l2, \l3
        transpose8_4x4  \l4, \l5, \l6, \l7
.endm

.macro tr_8x4 shift, in0, in1, in2, in3, in4, in5, in6, in7
        tr_4x4_8        \in0, \in2, \in4, \in6, q8, q9, q10, q11, q12, q13, q14, q15

        vmull.s16       q14, \in1, \in0[2]
        vmull.s16       q12, \in1, \in0[0]
        vmull.s16       q13, \in1, \in0[1]
        sum_sub         q14, \in3, \in0[0], -
        sum_sub         q12, \in3, \in0[1], +
        sum_sub         q13, \in3, \in0[3], -

        sum_sub         q14, \in5, \in0[3], +
        sum_sub         q12, \in5, \in0[2], +
        sum_sub         q13, \in5, \in0[0], -

        sum_sub         q14, \in7, \in0[1], +
        sum_sub         q12, \in7, \in0[3], +
        sum_sub         q13, \in7, \in0[2], -

        vadd.s32        q15, q10, q14
        vsub.s32        q10, q10, q14
        vqrshrn.s32     \in2, q15, \shift

        vmull.s16       q15, \in1, \in0[3]
        sum_sub         q15, \in3, \in0[2], -
        sum_sub         q15, \in5, \in0[1], +
        sum_sub         q15, \in7, \in0[0], -

        vqrshrn.s32     \in5, q10,  \shift

        vadd.s32        q10, q8, q12
        vsub.s32        q8,  q8, q12
        vadd.s32        q12, q9, q13
        vsub.s32        q9,  q9, q13
        vadd.s32        q14, q11, q15
        vsub.s32        q11, q11, q15

        vqrshrn.s32     \in0, q10, \shift
        vqrshrn.s32     \in7, q8,  \shift
        vqrshrn.s32     \in1, q12, \shift
        vqrshrn.s32     \in6, q9,  \shift
        vqrshrn.s32     \in3, q14, \shift
        vqrshrn.s32     \in4, q11, \shift
.endm

.macro idct_8x8 bitdepth
function ff_hevc_idct_8x8_\bitdepth\()_neon, export=1
@r0 - coeffs
        vpush           {q4-q7}

        mov             r1,  r0
        mov             r2,  #64
        add             r3,  r0,  #32
        vld1.s16        {q0-q1}, [r1,:128], r2
        vld1.s16        {q2-q3}, [r3,:128], r2
        vld1.s16        {q4-q5}, [r1,:128], r2
        vld1.s16        {q6-q7}, [r3,:128], r2

        movrel          r1, trans

        tr_8x4          7, d0, d2, d4, d6, d8, d10, d12, d14
        tr_8x4          7, d1, d3, d5, d7, d9, d11, d13, d15

        @ Transpose each 4x4 block, and swap how d4-d7 and d8-d11 are used.
        @ Layout before:
        @ d0  d1
        @ d2  d3
        @ d4  d5
        @ d6  d7
        @ d8  d9
        @ d10 d11
        @ d12 d13
        @ d14 d15
        transpose_8x8   d0, d2, d4, d6, d8, d10, d12, d14, d1, d3, d5, d7, d9, d11, d13, d15
        @ Now the layout is:
        @ d0  d8
        @ d2  d10
        @ d4  d12
        @ d6  d14
        @ d1  d9
        @ d3  d11
        @ d5  d13
        @ d7  d15

        tr_8x4          20 - \bitdepth, d0, d2, d4, d6, d1, d3, d5, d7
        vswp            d0, d8
        tr_8x4          20 - \bitdepth, d0, d10, d12, d14, d9, d11, d13, d15
        vswp            d0, d8

        transpose_8x8   d0, d2, d4, d6, d8, d10, d12, d14, d1, d3, d5, d7, d9, d11, d13, d15

        mov             r1,  r0
        mov             r2,  #64
        add             r3,  r0,  #32
        vst1.s16        {q0-q1}, [r1,:128], r2
        vst1.s16        {q2-q3}, [r3,:128], r2
        vst1.s16        {q4-q5}, [r1,:128], r2
        vst1.s16        {q6-q7}, [r3,:128], r2

        vpop            {q4-q7}
        bx              lr
endfunc
.endm

idct_4x4 8
idct_4x4 10
idct_8x8 8
idct_8x8 10
