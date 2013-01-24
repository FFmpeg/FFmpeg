/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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
#include "neon.S"

        /* H.264 loop filter */

.macro  h264_loop_filter_start
        ldr             r12, [sp]
        tst             r2,  r2
        ldr             r12, [r12]
        it              ne
        tstne           r3,  r3
        vmov.32         d24[0], r12
        and             r12, r12, r12, lsl #16
        it              eq
        bxeq            lr
        ands            r12, r12, r12, lsl #8
        it              lt
        bxlt            lr
.endm

.macro  h264_loop_filter_luma
        vdup.8          q11, r2         @ alpha
        vmovl.u8        q12, d24
        vabd.u8         q6,  q8,  q0    @ abs(p0 - q0)
        vmovl.u16       q12, d24
        vabd.u8         q14, q9,  q8    @ abs(p1 - p0)
        vsli.16         q12, q12, #8
        vabd.u8         q15, q1,  q0    @ abs(q1 - q0)
        vsli.32         q12, q12, #16
        vclt.u8         q6,  q6,  q11   @ < alpha
        vdup.8          q11, r3         @ beta
        vclt.s8         q7,  q12, #0
        vclt.u8         q14, q14, q11   @ < beta
        vclt.u8         q15, q15, q11   @ < beta
        vbic            q6,  q6,  q7
        vabd.u8         q4,  q10, q8    @ abs(p2 - p0)
        vand            q6,  q6,  q14
        vabd.u8         q5,  q2,  q0    @ abs(q2 - q0)
        vclt.u8         q4,  q4,  q11   @ < beta
        vand            q6,  q6,  q15
        vclt.u8         q5,  q5,  q11   @ < beta
        vand            q4,  q4,  q6
        vand            q5,  q5,  q6
        vand            q12, q12, q6
        vrhadd.u8       q14, q8,  q0
        vsub.i8         q6,  q12, q4
        vqadd.u8        q7,  q9,  q12
        vhadd.u8        q10, q10, q14
        vsub.i8         q6,  q6,  q5
        vhadd.u8        q14, q2,  q14
        vmin.u8         q7,  q7,  q10
        vqsub.u8        q11, q9,  q12
        vqadd.u8        q2,  q1,  q12
        vmax.u8         q7,  q7,  q11
        vqsub.u8        q11, q1,  q12
        vmin.u8         q14, q2,  q14
        vmovl.u8        q2,  d0
        vmax.u8         q14, q14, q11
        vmovl.u8        q10, d1
        vsubw.u8        q2,  q2,  d16
        vsubw.u8        q10, q10, d17
        vshl.i16        q2,  q2,  #2
        vshl.i16        q10, q10, #2
        vaddw.u8        q2,  q2,  d18
        vaddw.u8        q10, q10, d19
        vsubw.u8        q2,  q2,  d2
        vsubw.u8        q10, q10, d3
        vrshrn.i16      d4,  q2,  #3
        vrshrn.i16      d5,  q10, #3
        vbsl            q4,  q7,  q9
        vbsl            q5,  q14, q1
        vneg.s8         q7,  q6
        vmovl.u8        q14, d16
        vmin.s8         q2,  q2,  q6
        vmovl.u8        q6,  d17
        vmax.s8         q2,  q2,  q7
        vmovl.u8        q11, d0
        vmovl.u8        q12, d1
        vaddw.s8        q14, q14, d4
        vaddw.s8        q6,  q6,  d5
        vsubw.s8        q11, q11, d4
        vsubw.s8        q12, q12, d5
        vqmovun.s16     d16, q14
        vqmovun.s16     d17, q6
        vqmovun.s16     d0,  q11
        vqmovun.s16     d1,  q12
.endm

function ff_h264_v_loop_filter_luma_neon, export=1
        h264_loop_filter_start

        vld1.8          {d0, d1},  [r0,:128], r1
        vld1.8          {d2, d3},  [r0,:128], r1
        vld1.8          {d4, d5},  [r0,:128], r1
        sub             r0,  r0,  r1, lsl #2
        sub             r0,  r0,  r1, lsl #1
        vld1.8          {d20,d21}, [r0,:128], r1
        vld1.8          {d18,d19}, [r0,:128], r1
        vld1.8          {d16,d17}, [r0,:128], r1

        vpush           {d8-d15}

        h264_loop_filter_luma

        sub             r0,  r0,  r1, lsl #1
        vst1.8          {d8, d9},  [r0,:128], r1
        vst1.8          {d16,d17}, [r0,:128], r1
        vst1.8          {d0, d1},  [r0,:128], r1
        vst1.8          {d10,d11}, [r0,:128]

        vpop            {d8-d15}
        bx              lr
endfunc

function ff_h264_h_loop_filter_luma_neon, export=1
        h264_loop_filter_start

        sub             r0,  r0,  #4
        vld1.8          {d6},  [r0], r1
        vld1.8          {d20}, [r0], r1
        vld1.8          {d18}, [r0], r1
        vld1.8          {d16}, [r0], r1
        vld1.8          {d0},  [r0], r1
        vld1.8          {d2},  [r0], r1
        vld1.8          {d4},  [r0], r1
        vld1.8          {d26}, [r0], r1
        vld1.8          {d7},  [r0], r1
        vld1.8          {d21}, [r0], r1
        vld1.8          {d19}, [r0], r1
        vld1.8          {d17}, [r0], r1
        vld1.8          {d1},  [r0], r1
        vld1.8          {d3},  [r0], r1
        vld1.8          {d5},  [r0], r1
        vld1.8          {d27}, [r0], r1

        transpose_8x8   q3, q10, q9, q8, q0, q1, q2, q13

        vpush           {d8-d15}

        h264_loop_filter_luma

        transpose_4x4   q4, q8, q0, q5

        sub             r0,  r0,  r1, lsl #4
        add             r0,  r0,  #2
        vst1.32         {d8[0]},  [r0], r1
        vst1.32         {d16[0]}, [r0], r1
        vst1.32         {d0[0]},  [r0], r1
        vst1.32         {d10[0]}, [r0], r1
        vst1.32         {d8[1]},  [r0], r1
        vst1.32         {d16[1]}, [r0], r1
        vst1.32         {d0[1]},  [r0], r1
        vst1.32         {d10[1]}, [r0], r1
        vst1.32         {d9[0]},  [r0], r1
        vst1.32         {d17[0]}, [r0], r1
        vst1.32         {d1[0]},  [r0], r1
        vst1.32         {d11[0]}, [r0], r1
        vst1.32         {d9[1]},  [r0], r1
        vst1.32         {d17[1]}, [r0], r1
        vst1.32         {d1[1]},  [r0], r1
        vst1.32         {d11[1]}, [r0], r1

        vpop            {d8-d15}
        bx              lr
endfunc

.macro  h264_loop_filter_chroma
        vdup.8          d22, r2         @ alpha
        vmovl.u8        q12, d24
        vabd.u8         d26, d16, d0    @ abs(p0 - q0)
        vmovl.u8        q2,  d0
        vabd.u8         d28, d18, d16   @ abs(p1 - p0)
        vsubw.u8        q2,  q2,  d16
        vsli.16         d24, d24, #8
        vshl.i16        q2,  q2,  #2
        vabd.u8         d30, d2,  d0    @ abs(q1 - q0)
        vaddw.u8        q2,  q2,  d18
        vclt.u8         d26, d26, d22   @ < alpha
        vsubw.u8        q2,  q2,  d2
        vdup.8          d22, r3         @ beta
        vrshrn.i16      d4,  q2,  #3
        vclt.u8         d28, d28, d22   @ < beta
        vclt.u8         d30, d30, d22   @ < beta
        vmin.s8         d4,  d4,  d24
        vneg.s8         d25, d24
        vand            d26, d26, d28
        vmax.s8         d4,  d4,  d25
        vand            d26, d26, d30
        vmovl.u8        q11, d0
        vand            d4,  d4,  d26
        vmovl.u8        q14, d16
        vaddw.s8        q14, q14, d4
        vsubw.s8        q11, q11, d4
        vqmovun.s16     d16, q14
        vqmovun.s16     d0,  q11
.endm

function ff_h264_v_loop_filter_chroma_neon, export=1
        h264_loop_filter_start

        sub             r0,  r0,  r1, lsl #1
        vld1.8          {d18}, [r0,:64], r1
        vld1.8          {d16}, [r0,:64], r1
        vld1.8          {d0},  [r0,:64], r1
        vld1.8          {d2},  [r0,:64]

        h264_loop_filter_chroma

        sub             r0,  r0,  r1, lsl #1
        vst1.8          {d16}, [r0,:64], r1
        vst1.8          {d0},  [r0,:64], r1

        bx              lr
endfunc

function ff_h264_h_loop_filter_chroma_neon, export=1
        h264_loop_filter_start

        sub             r0,  r0,  #2
        vld1.32         {d18[0]}, [r0], r1
        vld1.32         {d16[0]}, [r0], r1
        vld1.32         {d0[0]},  [r0], r1
        vld1.32         {d2[0]},  [r0], r1
        vld1.32         {d18[1]}, [r0], r1
        vld1.32         {d16[1]}, [r0], r1
        vld1.32         {d0[1]},  [r0], r1
        vld1.32         {d2[1]},  [r0], r1

        vtrn.16         d18, d0
        vtrn.16         d16, d2
        vtrn.8          d18, d16
        vtrn.8          d0,  d2

        h264_loop_filter_chroma

        vtrn.16         d18, d0
        vtrn.16         d16, d2
        vtrn.8          d18, d16
        vtrn.8          d0,  d2

        sub             r0,  r0,  r1, lsl #3
        vst1.32         {d18[0]}, [r0], r1
        vst1.32         {d16[0]}, [r0], r1
        vst1.32         {d0[0]},  [r0], r1
        vst1.32         {d2[0]},  [r0], r1
        vst1.32         {d18[1]}, [r0], r1
        vst1.32         {d16[1]}, [r0], r1
        vst1.32         {d0[1]},  [r0], r1
        vst1.32         {d2[1]},  [r0], r1

        bx              lr
endfunc

@ Biweighted prediction

.macro  biweight_16     macs, macd
        vdup.8          d0,  r4
        vdup.8          d1,  r5
        vmov            q2,  q8
        vmov            q3,  q8
1:      subs            r3,  r3,  #2
        vld1.8          {d20-d21},[r0,:128], r2
        \macd           q2,  d0,  d20
        pld             [r0]
        \macd           q3,  d0,  d21
        vld1.8          {d22-d23},[r1,:128], r2
        \macs           q2,  d1,  d22
        pld             [r1]
        \macs           q3,  d1,  d23
        vmov            q12, q8
        vld1.8          {d28-d29},[r0,:128], r2
        vmov            q13, q8
        \macd           q12, d0,  d28
        pld             [r0]
        \macd           q13, d0,  d29
        vld1.8          {d30-d31},[r1,:128], r2
        \macs           q12, d1,  d30
        pld             [r1]
        \macs           q13, d1,  d31
        vshl.s16        q2,  q2,  q9
        vshl.s16        q3,  q3,  q9
        vqmovun.s16     d4,  q2
        vqmovun.s16     d5,  q3
        vshl.s16        q12, q12, q9
        vshl.s16        q13, q13, q9
        vqmovun.s16     d24, q12
        vqmovun.s16     d25, q13
        vmov            q3,  q8
        vst1.8          {d4- d5}, [r6,:128], r2
        vmov            q2,  q8
        vst1.8          {d24-d25},[r6,:128], r2
        bne             1b
        pop             {r4-r6, pc}
.endm

.macro  biweight_8      macs, macd
        vdup.8          d0,  r4
        vdup.8          d1,  r5
        vmov            q1,  q8
        vmov            q10, q8
1:      subs            r3,  r3,  #2
        vld1.8          {d4},[r0,:64], r2
        \macd           q1,  d0,  d4
        pld             [r0]
        vld1.8          {d5},[r1,:64], r2
        \macs           q1,  d1,  d5
        pld             [r1]
        vld1.8          {d6},[r0,:64], r2
        \macd           q10, d0,  d6
        pld             [r0]
        vld1.8          {d7},[r1,:64], r2
        \macs           q10, d1,  d7
        pld             [r1]
        vshl.s16        q1,  q1,  q9
        vqmovun.s16     d2,  q1
        vshl.s16        q10, q10, q9
        vqmovun.s16     d4,  q10
        vmov            q10, q8
        vst1.8          {d2},[r6,:64], r2
        vmov            q1,  q8
        vst1.8          {d4},[r6,:64], r2
        bne             1b
        pop             {r4-r6, pc}
.endm

.macro  biweight_4      macs, macd
        vdup.8          d0,  r4
        vdup.8          d1,  r5
        vmov            q1,  q8
        vmov            q10, q8
1:      subs            r3,  r3,  #4
        vld1.32         {d4[0]},[r0,:32], r2
        vld1.32         {d4[1]},[r0,:32], r2
        \macd           q1,  d0,  d4
        pld             [r0]
        vld1.32         {d5[0]},[r1,:32], r2
        vld1.32         {d5[1]},[r1,:32], r2
        \macs           q1,  d1,  d5
        pld             [r1]
        blt             2f
        vld1.32         {d6[0]},[r0,:32], r2
        vld1.32         {d6[1]},[r0,:32], r2
        \macd           q10, d0,  d6
        pld             [r0]
        vld1.32         {d7[0]},[r1,:32], r2
        vld1.32         {d7[1]},[r1,:32], r2
        \macs           q10, d1,  d7
        pld             [r1]
        vshl.s16        q1,  q1,  q9
        vqmovun.s16     d2,  q1
        vshl.s16        q10, q10, q9
        vqmovun.s16     d4,  q10
        vmov            q10, q8
        vst1.32         {d2[0]},[r6,:32], r2
        vst1.32         {d2[1]},[r6,:32], r2
        vmov            q1,  q8
        vst1.32         {d4[0]},[r6,:32], r2
        vst1.32         {d4[1]},[r6,:32], r2
        bne             1b
        pop             {r4-r6, pc}
2:      vshl.s16        q1,  q1,  q9
        vqmovun.s16     d2,  q1
        vst1.32         {d2[0]},[r6,:32], r2
        vst1.32         {d2[1]},[r6,:32], r2
        pop             {r4-r6, pc}
.endm

.macro  biweight_func   w
function ff_biweight_h264_pixels_\w\()_neon, export=1
        push            {r4-r6, lr}
        ldr             r12, [sp, #16]
        add             r4,  sp,  #20
        ldm             r4,  {r4-r6}
        lsr             lr,  r4,  #31
        add             r6,  r6,  #1
        eors            lr,  lr,  r5,  lsr #30
        orr             r6,  r6,  #1
        vdup.16         q9,  r12
        lsl             r6,  r6,  r12
        vmvn            q9,  q9
        vdup.16         q8,  r6
        mov             r6,  r0
        beq             10f
        subs            lr,  lr,  #1
        beq             20f
        subs            lr,  lr,  #1
        beq             30f
        b               40f
10:     biweight_\w     vmlal.u8, vmlal.u8
20:     rsb             r4,  r4,  #0
        biweight_\w     vmlal.u8, vmlsl.u8
30:     rsb             r4,  r4,  #0
        rsb             r5,  r5,  #0
        biweight_\w     vmlsl.u8, vmlsl.u8
40:     rsb             r5,  r5,  #0
        biweight_\w     vmlsl.u8, vmlal.u8
endfunc
.endm

        biweight_func   16
        biweight_func   8
        biweight_func   4

@ Weighted prediction

.macro  weight_16       add
        vdup.8          d0,  r12
1:      subs            r2,  r2,  #2
        vld1.8          {d20-d21},[r0,:128], r1
        vmull.u8        q2,  d0,  d20
        pld             [r0]
        vmull.u8        q3,  d0,  d21
        vld1.8          {d28-d29},[r0,:128], r1
        vmull.u8        q12, d0,  d28
        pld             [r0]
        vmull.u8        q13, d0,  d29
        \add            q2,  q8,  q2
        vrshl.s16       q2,  q2,  q9
        \add            q3,  q8,  q3
        vrshl.s16       q3,  q3,  q9
        vqmovun.s16     d4,  q2
        vqmovun.s16     d5,  q3
        \add            q12, q8,  q12
        vrshl.s16       q12, q12, q9
        \add            q13, q8,  q13
        vrshl.s16       q13, q13, q9
        vqmovun.s16     d24, q12
        vqmovun.s16     d25, q13
        vst1.8          {d4- d5}, [r4,:128], r1
        vst1.8          {d24-d25},[r4,:128], r1
        bne             1b
        pop             {r4, pc}
.endm

.macro  weight_8        add
        vdup.8          d0,  r12
1:      subs            r2,  r2,  #2
        vld1.8          {d4},[r0,:64], r1
        vmull.u8        q1,  d0,  d4
        pld             [r0]
        vld1.8          {d6},[r0,:64], r1
        vmull.u8        q10, d0,  d6
        \add            q1,  q8,  q1
        pld             [r0]
        vrshl.s16       q1,  q1,  q9
        vqmovun.s16     d2,  q1
        \add            q10, q8,  q10
        vrshl.s16       q10, q10, q9
        vqmovun.s16     d4,  q10
        vst1.8          {d2},[r4,:64], r1
        vst1.8          {d4},[r4,:64], r1
        bne             1b
        pop             {r4, pc}
.endm

.macro  weight_4        add
        vdup.8          d0,  r12
        vmov            q1,  q8
        vmov            q10, q8
1:      subs            r2,  r2,  #4
        vld1.32         {d4[0]},[r0,:32], r1
        vld1.32         {d4[1]},[r0,:32], r1
        vmull.u8        q1,  d0,  d4
        pld             [r0]
        blt             2f
        vld1.32         {d6[0]},[r0,:32], r1
        vld1.32         {d6[1]},[r0,:32], r1
        vmull.u8        q10, d0,  d6
        pld             [r0]
        \add            q1,  q8,  q1
        vrshl.s16       q1,  q1,  q9
        vqmovun.s16     d2,  q1
        \add            q10, q8,  q10
        vrshl.s16       q10, q10, q9
        vqmovun.s16     d4,  q10
        vmov            q10, q8
        vst1.32         {d2[0]},[r4,:32], r1
        vst1.32         {d2[1]},[r4,:32], r1
        vmov            q1,  q8
        vst1.32         {d4[0]},[r4,:32], r1
        vst1.32         {d4[1]},[r4,:32], r1
        bne             1b
        pop             {r4, pc}
2:      \add            q1,  q8,  q1
        vrshl.s16       q1,  q1,  q9
        vqmovun.s16     d2,  q1
        vst1.32         {d2[0]},[r4,:32], r1
        vst1.32         {d2[1]},[r4,:32], r1
        pop             {r4, pc}
.endm

.macro  weight_func     w
function ff_weight_h264_pixels_\w\()_neon, export=1
        push            {r4, lr}
        ldr             r12, [sp, #8]
        ldr             r4,  [sp, #12]
        cmp             r3,  #1
        lsl             r4,  r4,  r3
        vdup.16         q8,  r4
        mov             r4,  r0
        ble             20f
        rsb             lr,  r3,  #1
        vdup.16         q9,  lr
        cmp             r12, #0
        blt             10f
        weight_\w       vhadd.s16
10:     rsb             r12, r12, #0
        weight_\w       vhsub.s16
20:     rsb             lr,  r3,  #0
        vdup.16         q9,  lr
        cmp             r12, #0
        blt             10f
        weight_\w       vadd.s16
10:     rsb             r12, r12, #0
        weight_\w       vsub.s16
endfunc
.endm

        weight_func     16
        weight_func     8
        weight_func     4
