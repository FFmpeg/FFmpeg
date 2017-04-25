/*
 * ARM NEON optimised IDCT functions for HEVC decoding
 *
 * Copyright (c) 2014 Seppo Tomperi <seppo.tomperi@vtt.fi>
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

.macro idct_4x4_dc bitdepth
function ff_hevc_idct_4x4_dc_\bitdepth\()_neon, export=1
        ldrsh           r1, [r0]
        ldr             r2, =(1 << (13 - \bitdepth))
        add             r1, #1
        asr             r1, #1
        add             r1, r2
        asr             r1, #(14 - \bitdepth)
        vdup.16         q0, r1
        vdup.16         q1, r1
        vst1.16         {q0, q1}, [r0, :128]
        bx              lr
endfunc
.endm

.macro idct_8x8_dc bitdepth
function ff_hevc_idct_8x8_dc_\bitdepth\()_neon, export=1
        ldrsh           r1, [r0]
        ldr             r2, =(1 << (13 - \bitdepth))
        add             r1, #1
        asr             r1, #1
        add             r1, r2
        asr             r1, #(14 - \bitdepth)
        vdup.16         q8, r1
        vdup.16         q9, r1
        vmov.16         q10, q8
        vmov.16         q11, q8
        vmov.16         q12, q8
        vmov.16         q13, q8
        vmov.16         q14, q8
        vmov.16         q15, q8
        vstm            r0, {q8-q15}
        bx              lr
endfunc
.endm

.macro idct_16x16_dc bitdepth
function ff_hevc_idct_16x16_dc_\bitdepth\()_neon, export=1
        ldrsh           r1, [r0]
        ldr             r2, =(1 << (13 - \bitdepth))
        add             r1, #1
        asr             r1, #1
        add             r1, r2
        asr             r1, #(14 - \bitdepth)
        vdup.16         q8, r1
        vdup.16         q9, r1
        vmov.16         q10, q8
        vmov.16         q11, q8
        vmov.16         q12, q8
        vmov.16         q13, q8
        vmov.16         q14, q8
        vmov.16         q15, q8
        vstm            r0!, {q8-q15}
        vstm            r0!, {q8-q15}
        vstm            r0!, {q8-q15}
        vstm            r0, {q8-q15}
        bx              lr
endfunc
.endm

.macro idct_32x32_dc bitdepth
function ff_hevc_idct_32x32_dc_\bitdepth\()_neon, export=1
        ldrsh           r1, [r0]
        ldr             r2, =(1 << (13 - \bitdepth))
        add             r1, #1
        asr             r1, #1
        add             r1, r2
        asr             r1, #(14 - \bitdepth)
        mov             r3, #16
        vdup.16         q8, r1
        vdup.16         q9, r1
        vmov.16         q10, q8
        vmov.16         q11, q8
        vmov.16         q12, q8
        vmov.16         q13, q8
        vmov.16         q14, q8
        vmov.16         q15, q8
1:      subs            r3, #1
        vstm            r0!, {q8-q15}
        bne             1b
        bx              lr
endfunc
.endm

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

.macro butterfly e, o, tmp_p, tmp_m
        vadd.s32        \tmp_p, \e, \o
        vsub.s32        \tmp_m, \e, \o
.endm

.macro tr16_8x4 in0, in1, in2, in3, in4, in5, in6, in7
        tr_4x4_8        \in0, \in2, \in4, \in6, q8, q9, q10, q11, q12, q13, q14, q15

        vmull.s16       q12, \in1, \in0[0]
        vmull.s16       q13, \in1, \in0[1]
        vmull.s16       q14, \in1, \in0[2]
        vmull.s16       q15, \in1, \in0[3]
        sum_sub         q12, \in3, \in0[1], +
        sum_sub         q13, \in3, \in0[3], -
        sum_sub         q14, \in3, \in0[0], -
        sum_sub         q15, \in3, \in0[2], -

        sum_sub         q12, \in5, \in0[2], +
        sum_sub         q13, \in5, \in0[0], -
        sum_sub         q14, \in5, \in0[3], +
        sum_sub         q15, \in5, \in0[1], +

        sum_sub         q12, \in7, \in0[3], +
        sum_sub         q13, \in7, \in0[2], -
        sum_sub         q14, \in7, \in0[1], +
        sum_sub         q15, \in7, \in0[0], -

        butterfly       q8,  q12, q0, q7
        butterfly       q9,  q13, q1, q6
        butterfly       q10, q14, q2, q5
        butterfly       q11, q15, q3, q4
        add             r4,  sp,  #512
        vst1.s16        {q0-q1}, [r4, :128]!
        vst1.s16        {q2-q3}, [r4, :128]!
        vst1.s16        {q4-q5}, [r4, :128]!
        vst1.s16        {q6-q7}, [r4, :128]
.endm

.macro load16 in0, in1, in2, in3, in4, in5, in6, in7
        vld1.s16        {\in0}, [r1, :64], r2
        vld1.s16        {\in1}, [r3, :64], r2
        vld1.s16        {\in2}, [r1, :64], r2
        vld1.s16        {\in3}, [r3, :64], r2
        vld1.s16        {\in4}, [r1, :64], r2
        vld1.s16        {\in5}, [r3, :64], r2
        vld1.s16        {\in6}, [r1, :64], r2
        vld1.s16        {\in7}, [r3, :64], r2
.endm

.macro add_member in, t0, t1, t2, t3, t4, t5, t6, t7, op0, op1, op2, op3, op4, op5, op6, op7
        sum_sub q5,     \in, \t0, \op0
        sum_sub q6,     \in, \t1, \op1
        sum_sub q7,     \in, \t2, \op2
        sum_sub q8,     \in, \t3, \op3
        sum_sub q9,     \in, \t4, \op4
        sum_sub q10,    \in, \t5, \op5
        sum_sub q11,    \in, \t6, \op6
        sum_sub q12,    \in, \t7, \op7
.endm

.macro butterfly16 in0, in1, in2, in3, in4, in5, in6, in7
        vadd.s32        q4, \in0, \in1
        vsub.s32        \in0, \in0, \in1
        vadd.s32        \in1, \in2, \in3
        vsub.s32        \in2, \in2, \in3
        vadd.s32        \in3, \in4, \in5
        vsub.s32        \in4, \in4, \in5
        vadd.s32        \in5, \in6, \in7
        vsub.s32        \in6, \in6, \in7
.endm

.macro store16 in0, in1, in2, in3, in4, in5, in6, in7
        vst1.s16        \in0, [r1, :64], r2
        vst1.s16        \in1, [r3, :64], r4
        vst1.s16        \in2, [r1, :64], r2
        vst1.s16        \in3, [r3, :64], r4
        vst1.s16        \in4, [r1, :64], r2
        vst1.s16        \in5, [r3, :64], r4
        vst1.s16        \in6, [r1, :64], r2
        vst1.s16        \in7, [r3, :64], r4
.endm

.macro scale out0, out1, out2, out3, out4, out5, out6, out7, in0, in1, in2, in3, in4, in5, in6, in7, shift
        vqrshrn.s32     \out0, \in0, \shift
        vqrshrn.s32     \out1, \in1, \shift
        vqrshrn.s32     \out2, \in2, \shift
        vqrshrn.s32     \out3, \in3, \shift
        vqrshrn.s32     \out4, \in4, \shift
        vqrshrn.s32     \out5, \in5, \shift
        vqrshrn.s32     \out6, \in6, \shift
        vqrshrn.s32     \out7, \in7, \shift
.endm

.macro tr_16x4 name, shift
function func_tr_16x4_\name
        mov             r1,  r5
        add             r3,  r5, #64
        mov             r2,  #128
        load16          d0, d1, d2, d3, d4, d5, d6, d7
        movrel          r1, trans

        tr16_8x4        d0, d1, d2, d3, d4, d5, d6, d7

        add             r1,  r5, #32
        add             r3,  r5, #(64 + 32)
        mov             r2,  #128
        load16          d8, d9, d2, d3, d4, d5, d6, d7
        movrel          r1, trans + 16
        vld1.s16        {q0}, [r1, :128]
        vmull.s16       q5, d8, d0[0]
        vmull.s16       q6, d8, d0[1]
        vmull.s16       q7, d8, d0[2]
        vmull.s16       q8, d8, d0[3]
        vmull.s16       q9, d8, d1[0]
        vmull.s16       q10, d8, d1[1]
        vmull.s16       q11, d8, d1[2]
        vmull.s16       q12, d8, d1[3]

        add_member      d9, d0[1], d1[0], d1[3], d1[1], d0[2], d0[0], d0[3], d1[2], +, +, +, -, -, -, -, -
        add_member      d2, d0[2], d1[3], d0[3], d0[1], d1[2], d1[0], d0[0], d1[1], +, +, -, -, -, +, +, +
        add_member      d3, d0[3], d1[1], d0[1], d1[3], d0[0], d1[2], d0[2], d1[0], +, -, -, +, +, +, -, -
        add_member      d4, d1[0], d0[2], d1[2], d0[0], d1[3], d0[1], d1[1], d0[3], +, -, -, +, -, -, +, +
        add_member      d5, d1[1], d0[0], d1[0], d1[2], d0[1], d0[3], d1[3], d0[2], +, -, +, +, -, +, +, -
        add_member      d6, d1[2], d0[3], d0[0], d0[2], d1[1], d1[3], d1[0], d0[1], +, -, +, -, +, +, -, +
        add_member      d7, d1[3], d1[2], d1[1], d1[0], d0[3], d0[2], d0[1], d0[0], +, -, +, -, +, -, +, -

        add             r4, sp, #512
        vld1.s16        {q0-q1}, [r4, :128]!
        vld1.s16        {q2-q3}, [r4, :128]!

        butterfly16     q0, q5, q1, q6, q2, q7, q3, q8
        scale           d26, d27, d28, d29, d30, d31, d16, d17, q4, q0, q5, q1, q6, q2, q7, q3, \shift
        transpose8_4x4  d26, d28, d30, d16
        transpose8_4x4  d17, d31, d29, d27
        mov             r1, r6
        add             r3, r6, #(24 +3*32)
        mov             r2, #32
        mov             r4, #-32
        store16         d26, d27, d28, d29, d30, d31, d16, d17

        add             r4, sp, #576
        vld1.s16        {q0-q1}, [r4, :128]!
        vld1.s16        {q2-q3}, [r4, :128]
        butterfly16     q0, q9, q1, q10, q2, q11, q3, q12
        scale           d26, d27, d28, d29, d30, d31, d8, d9, q4, q0, q9, q1, q10, q2, q11, q3, \shift
        transpose8_4x4  d26, d28, d30, d8
        transpose8_4x4  d9, d31, d29, d27

        add             r1, r6, #8
        add             r3, r6, #(16 + 3 * 32)
        mov             r2, #32
        mov             r4, #-32
        store16         d26, d27, d28, d29, d30, d31, d8, d9

        bx              lr
endfunc
.endm

.macro idct_16x16 bitdepth
function ff_hevc_idct_16x16_\bitdepth\()_neon, export=1
@r0 - coeffs
        push            {r4-r7, lr}
        vpush           {q4-q7}

        @ Align the stack, allocate a temp buffer
T       mov             r7,  sp
T       and             r7,  r7,  #15
A       and             r7,  sp,  #15
        add             r7,  r7,  #640
        sub             sp,  sp,  r7

.irp i, 0, 1, 2, 3
        add             r5, r0, #(8 * \i)
        add             r6, sp, #(8 * \i * 16)
        bl              func_tr_16x4_firstpass
.endr

.irp i, 0, 1, 2, 3
        add             r5, sp, #(8 * \i)
        add             r6, r0, #(8 * \i * 16)
        bl              func_tr_16x4_secondpass_\bitdepth
.endr

        add             sp,  sp,  r7

        vpop            {q4-q7}
        pop             {r4-r7, pc}
endfunc
.endm

tr_16x4 firstpass, 7
tr_16x4 secondpass_8, 20 - 8
tr_16x4 secondpass_10, 20 - 10
.ltorg

idct_4x4 8
idct_4x4_dc 8
idct_4x4 10
idct_4x4_dc 10
idct_8x8 8
idct_8x8_dc 8
idct_8x8 10
idct_8x8_dc 10
idct_16x16 8
idct_16x16_dc 8
idct_16x16 10
idct_16x16_dc 10
idct_32x32_dc 8
idct_32x32_dc 10
