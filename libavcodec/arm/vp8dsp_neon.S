/*
 * VP8 NEON optimisations
 *
 * Copyright (c) 2010 Rob Clark <rob@ti.com>
 * Copyright (c) 2011 Mans Rullgard <mans@mansr.com>
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

#include "asm.S"
#include "neon.S"

function ff_vp8_luma_dc_wht_neon, export=1
        vld1.16         {q0-q1},  [r1,:128]
        vmov.i16        q15, #0

        vadd.i16        d4,  d0,  d3
        vadd.i16        d6,  d1,  d2
        vst1.16         {q15},    [r1,:128]!
        vsub.i16        d7,  d1,  d2
        vsub.i16        d5,  d0,  d3
        vst1.16         {q15},    [r1,:128]
        vadd.i16        q0,  q2,  q3
        vsub.i16        q1,  q2,  q3

        vmov.i16        q8, #3

        vtrn.32         d0,  d2
        vtrn.32         d1,  d3
        vtrn.16         d0,  d1
        vtrn.16         d2,  d3

        vadd.i16        d0,  d0,  d16

        vadd.i16        d4,  d0,  d3
        vadd.i16        d6,  d1,  d2
        vsub.i16        d7,  d1,  d2
        vsub.i16        d5,  d0,  d3
        vadd.i16        q0,  q2,  q3
        vsub.i16        q1,  q2,  q3

        vshr.s16        q0,  q0,  #3
        vshr.s16        q1,  q1,  #3

        mov             r3,  #32
        vst1.16         {d0[0]},  [r0,:16], r3
        vst1.16         {d1[0]},  [r0,:16], r3
        vst1.16         {d2[0]},  [r0,:16], r3
        vst1.16         {d3[0]},  [r0,:16], r3
        vst1.16         {d0[1]},  [r0,:16], r3
        vst1.16         {d1[1]},  [r0,:16], r3
        vst1.16         {d2[1]},  [r0,:16], r3
        vst1.16         {d3[1]},  [r0,:16], r3
        vst1.16         {d0[2]},  [r0,:16], r3
        vst1.16         {d1[2]},  [r0,:16], r3
        vst1.16         {d2[2]},  [r0,:16], r3
        vst1.16         {d3[2]},  [r0,:16], r3
        vst1.16         {d0[3]},  [r0,:16], r3
        vst1.16         {d1[3]},  [r0,:16], r3
        vst1.16         {d2[3]},  [r0,:16], r3
        vst1.16         {d3[3]},  [r0,:16], r3

        bx              lr
endfunc

function ff_vp8_idct_add_neon, export=1
        vld1.16         {q0-q1},  [r1,:128]
        movw            r3,  #20091
        movt            r3,  #35468/2
        vdup.32         d4,  r3

        vmull.s16       q12, d1,  d4[0]
        vmull.s16       q13, d3,  d4[0]
        vqdmulh.s16     d20, d1,  d4[1]
        vqdmulh.s16     d23, d3,  d4[1]
        vshrn.s32       d21, q12, #16
        vshrn.s32       d22, q13, #16
        vadd.s16        d21, d21, d1
        vadd.s16        d22, d22, d3

        vadd.s16        d16, d0,  d2
        vsub.s16        d17, d0,  d2
        vadd.s16        d18, d21, d23
        vsub.s16        d19, d20, d22
        vadd.s16        q0,  q8,  q9
        vsub.s16        q1,  q8,  q9

        vtrn.32         d0,  d3
        vtrn.32         d1,  d2
        vtrn.16         d0,  d1
        vtrn.16         d3,  d2

        vmov.i16        q15, #0
        vmull.s16       q12, d1,  d4[0]
        vst1.16         {q15},    [r1,:128]!
        vmull.s16       q13, d2,  d4[0]
        vst1.16         {q15},    [r1,:128]
        vqdmulh.s16     d21, d1,  d4[1]
        vqdmulh.s16     d23, d2,  d4[1]
        vshrn.s32       d20, q12, #16
        vshrn.s32       d22, q13, #16
        vadd.i16        d20, d20, d1
        vadd.i16        d22, d22, d2

        vadd.i16        d16, d0,  d3
        vsub.i16        d17, d0,  d3
        vadd.i16        d18, d20, d23
        vld1.32         {d20[]},  [r0,:32], r2
        vsub.i16        d19, d21, d22
        vld1.32         {d22[]},  [r0,:32], r2
        vadd.s16        q0,  q8,  q9
        vld1.32         {d23[]},  [r0,:32], r2
        vsub.s16        q1,  q8,  q9
        vld1.32         {d21[]},  [r0,:32], r2
        vrshr.s16       q0,  q0,  #3
        vtrn.32         q10, q11
        vrshr.s16       q1,  q1,  #3

        sub             r0,  r0,  r2,  lsl #2

        vtrn.32         d0,  d3
        vtrn.32         d1,  d2
        vtrn.16         d0,  d1
        vtrn.16         d3,  d2

        vaddw.u8        q0,  q0,  d20
        vaddw.u8        q1,  q1,  d21
        vqmovun.s16     d0,  q0
        vqmovun.s16     d1,  q1

        vst1.32         {d0[0]},  [r0,:32], r2
        vst1.32         {d0[1]},  [r0,:32], r2
        vst1.32         {d1[1]},  [r0,:32], r2
        vst1.32         {d1[0]},  [r0,:32], r2

        bx              lr
endfunc

function ff_vp8_idct_dc_add_neon, export=1
        mov             r3,  #0
        ldrsh           r12, [r1]
        strh            r3,  [r1]
        vdup.16         q1,  r12
        vrshr.s16       q1,  q1,  #3
        vld1.32         {d0[]},   [r0,:32], r2
        vld1.32         {d1[]},   [r0,:32], r2
        vld1.32         {d0[1]},  [r0,:32], r2
        vld1.32         {d1[1]},  [r0,:32], r2
        vaddw.u8        q2,  q1,  d0
        vaddw.u8        q3,  q1,  d1
        sub             r0,  r0,  r2, lsl #2
        vqmovun.s16     d0,  q2
        vqmovun.s16     d1,  q3
        vst1.32         {d0[0]},  [r0,:32], r2
        vst1.32         {d1[0]},  [r0,:32], r2
        vst1.32         {d0[1]},  [r0,:32], r2
        vst1.32         {d1[1]},  [r0,:32], r2
        bx              lr
endfunc

function ff_vp8_idct_dc_add4uv_neon, export=1
        vmov.i16        d0,  #0
        mov             r3,  #32
        vld1.16         {d16[]},  [r1,:16]
        vst1.16         {d0[0]},  [r1,:16], r3
        vld1.16         {d17[]},  [r1,:16]
        vst1.16         {d0[0]},  [r1,:16], r3
        vld1.16         {d18[]},  [r1,:16]
        vst1.16         {d0[0]},  [r1,:16], r3
        vld1.16         {d19[]},  [r1,:16]
        vst1.16         {d0[0]},  [r1,:16], r3
        mov             r3,  r0
        vrshr.s16       q8,  q8,  #3            @ dc >>= 3
        vld1.8          {d0},     [r0,:64], r2
        vrshr.s16       q9,  q9,  #3
        vld1.8          {d1},     [r0,:64], r2
        vaddw.u8        q10, q8,  d0
        vld1.8          {d2},     [r0,:64], r2
        vaddw.u8        q0,  q8,  d1
        vld1.8          {d3},     [r0,:64], r2
        vaddw.u8        q11, q8,  d2
        vld1.8          {d4},     [r0,:64], r2
        vaddw.u8        q1,  q8,  d3
        vld1.8          {d5},     [r0,:64], r2
        vaddw.u8        q12, q9,  d4
        vld1.8          {d6},     [r0,:64], r2
        vaddw.u8        q2,  q9,  d5
        vld1.8          {d7},     [r0,:64], r2
        vaddw.u8        q13, q9,  d6
        vqmovun.s16     d20, q10
        vaddw.u8        q3,  q9,  d7
        vqmovun.s16     d21, q0
        vqmovun.s16     d22, q11
        vst1.8          {d20},    [r3,:64], r2
        vqmovun.s16     d23, q1
        vst1.8          {d21},    [r3,:64], r2
        vqmovun.s16     d24, q12
        vst1.8          {d22},    [r3,:64], r2
        vqmovun.s16     d25, q2
        vst1.8          {d23},    [r3,:64], r2
        vqmovun.s16     d26, q13
        vst1.8          {d24},    [r3,:64], r2
        vqmovun.s16     d27, q3
        vst1.8          {d25},    [r3,:64], r2
        vst1.8          {d26},    [r3,:64], r2
        vst1.8          {d27},    [r3,:64], r2

        bx              lr
endfunc

function ff_vp8_idct_dc_add4y_neon, export=1
        vmov.i16        d0,  #0
        mov             r3,  #32
        vld1.16         {d16[]},  [r1,:16]
        vst1.16         {d0[0]},  [r1,:16], r3
        vld1.16         {d17[]},  [r1,:16]
        vst1.16         {d0[0]},  [r1,:16], r3
        vld1.16         {d18[]},  [r1,:16]
        vst1.16         {d0[0]},  [r1,:16], r3
        vld1.16         {d19[]},  [r1,:16]
        vst1.16         {d0[0]},  [r1,:16], r3
        vrshr.s16       q8,  q8,  #3            @ dc >>= 3
        vld1.8          {q0},     [r0,:128], r2
        vrshr.s16       q9,  q9,  #3
        vld1.8          {q1},     [r0,:128], r2
        vaddw.u8        q10, q8,  d0
        vld1.8          {q2},     [r0,:128], r2
        vaddw.u8        q0,  q9,  d1
        vld1.8          {q3},     [r0,:128], r2
        vaddw.u8        q11, q8,  d2
        vaddw.u8        q1,  q9,  d3
        vaddw.u8        q12, q8,  d4
        vaddw.u8        q2,  q9,  d5
        vaddw.u8        q13, q8,  d6
        vaddw.u8        q3,  q9,  d7
        sub             r0,  r0,  r2,  lsl #2
        vqmovun.s16     d20, q10
        vqmovun.s16     d21, q0
        vqmovun.s16     d22, q11
        vqmovun.s16     d23, q1
        vqmovun.s16     d24, q12
        vst1.8          {q10},    [r0,:128], r2
        vqmovun.s16     d25, q2
        vst1.8          {q11},    [r0,:128], r2
        vqmovun.s16     d26, q13
        vst1.8          {q12},    [r0,:128], r2
        vqmovun.s16     d27, q3
        vst1.8          {q13},    [r0,:128], r2

        bx              lr
endfunc

@ Register layout:
@   P3..Q3 -> q0..q7
@   flim_E -> q14
@   flim_I -> q15
@   hev_thresh -> r12
@
.macro  vp8_loop_filter, inner=0, simple=0
    .if \simple
        vabd.u8         q9,  q3,  q4            @ abs(P0-Q0)
        vabd.u8         q15, q2,  q5            @ abs(P1-Q1)
        vqadd.u8        q9,  q9,  q9            @ abs(P0-Q0) * 2
        vshr.u8         q10, q15, #1            @ abs(P1-Q1) / 2
        vqadd.u8        q11, q9,  q10           @ (abs(P0-Q0)*2) + (abs(P1-Q1)/2)
        vmov.i8         q13, #0x80
        vcle.u8         q8,  q11, q14           @ (abs(P0-Q0)*2) + (abs(P1-Q1)/2) <= flim
    .else
        @ calculate hev and normal_limit:
        vabd.u8         q12, q2,  q3            @ abs(P1-P0)
        vabd.u8         q13, q5,  q4            @ abs(Q1-Q0)
        vabd.u8         q10, q0,  q1            @ abs(P3-P2)
        vabd.u8         q11, q1,  q2            @ abs(P2-P1)
        vcle.u8         q8,  q12, q15           @ abs(P1-P0) <= flim_I
        vcle.u8         q9,  q13, q15           @ abs(Q1-Q0) <= flim_I
        vcle.u8         q10, q10, q15           @ abs(P3-P2) <= flim_I
        vcle.u8         q11, q11, q15           @ abs(P2-P1) <= flim_I
        vand            q8,  q8,  q9
        vabd.u8         q9,  q7,  q6            @ abs(Q3-Q2)
        vand            q8,  q8,  q11
        vabd.u8         q11, q6,  q5            @ abs(Q2-Q1)
        vand            q8,  q8,  q10
        vcle.u8         q10, q9,  q15           @ abs(Q3-Q2) <= flim_I
        vcle.u8         q11, q11, q15           @ abs(Q2-Q1) <= flim_I
        vabd.u8         q9,  q3,  q4            @ abs(P0-Q0)
        vabd.u8         q15, q2,  q5            @ abs(P1-Q1)
        vand            q8,  q8,  q10
        vqadd.u8        q9,  q9,  q9            @ abs(P0-Q0) * 2
        vand            q8,  q8,  q11
        vshr.u8         q10, q15, #1            @ abs(P1-Q1) / 2
        vdup.8          q15, r12                @ hev_thresh
        vqadd.u8        q11, q9,  q10           @ (abs(P0-Q0)*2) + (abs(P1-Q1)/2)
        vcgt.u8         q12, q12, q15           @ abs(P1-P0) > hev_thresh
        vcle.u8         q11, q11, q14           @ (abs(P0-Q0)*2) + (abs(P1-Q1)/2) <= flim_E
        vcgt.u8         q14, q13, q15           @ abs(Q1-Q0) > hev_thresh
        vand            q8,  q8,  q11
        vmov.i8         q13, #0x80
        vorr            q9,  q12, q14
    .endif

        @ at this point:
        @   q8: normal_limit
        @   q9: hev

        @ convert to signed value:
        veor            q3,  q3,  q13           @ PS0 = P0 ^ 0x80
        veor            q4,  q4,  q13           @ QS0 = Q0 ^ 0x80

        vmov.i16        q12, #3
        vsubl.s8        q10, d8,  d6            @ QS0 - PS0
        vsubl.s8        q11, d9,  d7            @   (widened to 16bit)
        veor            q2,  q2,  q13           @ PS1 = P1 ^ 0x80
        veor            q5,  q5,  q13           @ QS1 = Q1 ^ 0x80
        vmul.i16        q10, q10, q12           @ w = 3 * (QS0 - PS0)
        vmul.i16        q11, q11, q12

        vqsub.s8        q12, q2,  q5            @ clamp(PS1-QS1)
        vmov.i8         q14, #4
        vmov.i8         q15, #3
    .if \inner
        vand            q12, q12, q9            @ if(hev) w += clamp(PS1-QS1)
    .endif
        vaddw.s8        q10, q10, d24           @ w += clamp(PS1-QS1)
        vaddw.s8        q11, q11, d25
        vqmovn.s16      d20, q10                @ narrow result back into q10
        vqmovn.s16      d21, q11
    .if !\inner && !\simple
        veor            q1,  q1,  q13           @ PS2 = P2 ^ 0x80
        veor            q6,  q6,  q13           @ QS2 = Q2 ^ 0x80
    .endif
        vand            q10, q10, q8            @ w &= normal_limit

        @ registers used at this point..
        @   q0 -> P3  (don't corrupt)
        @   q1-q6 -> PS2-QS2
        @   q7 -> Q3  (don't corrupt)
        @   q9 -> hev
        @   q10 -> w
        @   q13 -> #0x80
        @   q14 -> #4
        @   q15 -> #3
        @   q8, q11, q12 -> unused

        @ filter_common:   is4tap==1
        @   c1 = clamp(w + 4) >> 3;
        @   c2 = clamp(w + 3) >> 3;
        @   Q0 = s2u(QS0 - c1);
        @   P0 = s2u(PS0 + c2);

    .if \simple
        vqadd.s8        q11, q10, q14           @ c1 = clamp((w&hev)+4)
        vqadd.s8        q12, q10, q15           @ c2 = clamp((w&hev)+3)
        vshr.s8         q11, q11, #3            @ c1 >>= 3
        vshr.s8         q12, q12, #3            @ c2 >>= 3
        vqsub.s8        q4,  q4,  q11           @ QS0 = clamp(QS0-c1)
        vqadd.s8        q3,  q3,  q12           @ PS0 = clamp(PS0+c2)
        veor            q4,  q4,  q13           @ Q0 = QS0 ^ 0x80
        veor            q3,  q3,  q13           @ P0 = PS0 ^ 0x80
        veor            q5,  q5,  q13           @ Q1 = QS1 ^ 0x80
        veor            q2,  q2,  q13           @ P1 = PS1 ^ 0x80
    .elseif \inner
        @ the !is4tap case of filter_common, only used for inner blocks
        @   c3 = ((c1&~hev) + 1) >> 1;
        @   Q1 = s2u(QS1 - c3);
        @   P1 = s2u(PS1 + c3);
        vqadd.s8        q11, q10, q14           @ c1 = clamp((w&hev)+4)
        vqadd.s8        q12, q10, q15           @ c2 = clamp((w&hev)+3)
        vshr.s8         q11, q11, #3            @ c1 >>= 3
        vshr.s8         q12, q12, #3            @ c2 >>= 3
        vqsub.s8        q4,  q4,  q11           @ QS0 = clamp(QS0-c1)
        vqadd.s8        q3,  q3,  q12           @ PS0 = clamp(PS0+c2)
        vbic            q11, q11, q9            @ c1 & ~hev
        veor            q4,  q4,  q13           @ Q0 = QS0 ^ 0x80
        vrshr.s8        q11, q11, #1            @ c3 >>= 1
        veor            q3,  q3,  q13           @ P0 = PS0 ^ 0x80
        vqsub.s8        q5,  q5,  q11           @ QS1 = clamp(QS1-c3)
        vqadd.s8        q2,  q2,  q11           @ PS1 = clamp(PS1+c3)
        veor            q5,  q5,  q13           @ Q1 = QS1 ^ 0x80
        veor            q2,  q2,  q13           @ P1 = PS1 ^ 0x80
    .else
        vand            q12, q10, q9            @ w & hev
        vqadd.s8        q11, q12, q14           @ c1 = clamp((w&hev)+4)
        vqadd.s8        q12, q12, q15           @ c2 = clamp((w&hev)+3)
        vshr.s8         q11, q11, #3            @ c1 >>= 3
        vshr.s8         q12, q12, #3            @ c2 >>= 3
        vbic            q10, q10, q9            @ w &= ~hev
        vqsub.s8        q4,  q4,  q11           @ QS0 = clamp(QS0-c1)
        vqadd.s8        q3,  q3,  q12           @ PS0 = clamp(PS0+c2)

        @ filter_mbedge:
        @   a = clamp((27*w + 63) >> 7);
        @   Q0 = s2u(QS0 - a);
        @   P0 = s2u(PS0 + a);
        @   a = clamp((18*w + 63) >> 7);
        @   Q1 = s2u(QS1 - a);
        @   P1 = s2u(PS1 + a);
        @   a = clamp((9*w + 63) >> 7);
        @   Q2 = s2u(QS2 - a);
        @   P2 = s2u(PS2 + a);
        vmov.i16        q9,  #63
        vshll.s8        q14, d20, #3
        vshll.s8        q15, d21, #3
        vaddw.s8        q14, q14, d20
        vaddw.s8        q15, q15, d21
        vadd.s16        q8,  q9,  q14
        vadd.s16        q9,  q9,  q15           @  9*w + 63
        vadd.s16        q11, q8,  q14
        vadd.s16        q12, q9,  q15           @ 18*w + 63
        vadd.s16        q14, q11, q14
        vadd.s16        q15, q12, q15           @ 27*w + 63
        vqshrn.s16      d16, q8,  #7
        vqshrn.s16      d17, q9,  #7            @ clamp(( 9*w + 63)>>7)
        vqshrn.s16      d22, q11, #7
        vqshrn.s16      d23, q12, #7            @ clamp((18*w + 63)>>7)
        vqshrn.s16      d28, q14, #7
        vqshrn.s16      d29, q15, #7            @ clamp((27*w + 63)>>7)
        vqadd.s8        q1,  q1,  q8            @ PS2 = clamp(PS2+a)
        vqsub.s8        q6,  q6,  q8            @ QS2 = clamp(QS2-a)
        vqadd.s8        q2,  q2,  q11           @ PS1 = clamp(PS1+a)
        vqsub.s8        q5,  q5,  q11           @ QS1 = clamp(QS1-a)
        vqadd.s8        q3,  q3,  q14           @ PS0 = clamp(PS0+a)
        vqsub.s8        q4,  q4,  q14           @ QS0 = clamp(QS0-a)
        veor            q3,  q3,  q13           @ P0 = PS0 ^ 0x80
        veor            q4,  q4,  q13           @ Q0 = QS0 ^ 0x80
        veor            q2,  q2,  q13           @ P1 = PS1 ^ 0x80
        veor            q5,  q5,  q13           @ Q1 = QS1 ^ 0x80
        veor            q1,  q1,  q13           @ P2 = PS2 ^ 0x80
        veor            q6,  q6,  q13           @ Q2 = QS2 ^ 0x80
    .endif
.endm

.macro  vp8_v_loop_filter16 name, inner=0, simple=0
function ff_vp8_v_loop_filter16\name\()_neon, export=1
        vpush           {q4-q7}
        sub             r0,  r0,  r1,  lsl #1+!\simple

        @ Load pixels:
    .if !\simple
        ldr             r12, [sp, #64]          @ hev_thresh
        vld1.8          {q0},     [r0,:128], r1 @ P3
        vld1.8          {q1},     [r0,:128], r1 @ P2
    .endif
        vld1.8          {q2},     [r0,:128], r1 @ P1
        vld1.8          {q3},     [r0,:128], r1 @ P0
        vld1.8          {q4},     [r0,:128], r1 @ Q0
        vld1.8          {q5},     [r0,:128], r1 @ Q1
    .if !\simple
        vld1.8          {q6},     [r0,:128], r1 @ Q2
        vld1.8          {q7},     [r0,:128]     @ Q3
        vdup.8          q15, r3                 @ flim_I
    .endif
        vdup.8          q14, r2                 @ flim_E

        vp8_loop_filter inner=\inner, simple=\simple

        @ back up to P2:  dst -= stride * 6
        sub             r0,  r0,  r1,  lsl #2
    .if !\simple
        sub             r0,  r0,  r1,  lsl #1

        @ Store pixels:
        vst1.8          {q1},     [r0,:128], r1 @ P2
    .endif
        vst1.8          {q2},     [r0,:128], r1 @ P1
        vst1.8          {q3},     [r0,:128], r1 @ P0
        vst1.8          {q4},     [r0,:128], r1 @ Q0
        vst1.8          {q5},     [r0,:128], r1 @ Q1
    .if !\simple
        vst1.8          {q6},     [r0,:128]     @ Q2
    .endif

        vpop            {q4-q7}
        bx              lr
endfunc
.endm

vp8_v_loop_filter16
vp8_v_loop_filter16 _inner,  inner=1
vp8_v_loop_filter16 _simple, simple=1

.macro  vp8_v_loop_filter8uv name, inner=0
function ff_vp8_v_loop_filter8uv\name\()_neon, export=1
        vpush           {q4-q7}
        sub             r0,  r0,  r2,  lsl #2
        sub             r1,  r1,  r2,  lsl #2
        ldr             r12, [sp, #64]          @ flim_I

        @ Load pixels:
        vld1.8          {d0},     [r0,:64], r2  @ P3
        vld1.8          {d1},     [r1,:64], r2  @ P3
        vld1.8          {d2},     [r0,:64], r2  @ P2
        vld1.8          {d3},     [r1,:64], r2  @ P2
        vld1.8          {d4},     [r0,:64], r2  @ P1
        vld1.8          {d5},     [r1,:64], r2  @ P1
        vld1.8          {d6},     [r0,:64], r2  @ P0
        vld1.8          {d7},     [r1,:64], r2  @ P0
        vld1.8          {d8},     [r0,:64], r2  @ Q0
        vld1.8          {d9},     [r1,:64], r2  @ Q0
        vld1.8          {d10},    [r0,:64], r2  @ Q1
        vld1.8          {d11},    [r1,:64], r2  @ Q1
        vld1.8          {d12},    [r0,:64], r2  @ Q2
        vld1.8          {d13},    [r1,:64], r2  @ Q2
        vld1.8          {d14},    [r0,:64]      @ Q3
        vld1.8          {d15},    [r1,:64]      @ Q3

        vdup.8          q14, r3                 @ flim_E
        vdup.8          q15, r12                @ flim_I
        ldr             r12, [sp, #68]          @ hev_thresh

        vp8_loop_filter inner=\inner

        @ back up to P2:  u,v -= stride * 6
        sub             r0,  r0,  r2,  lsl #2
        sub             r1,  r1,  r2,  lsl #2
        sub             r0,  r0,  r2,  lsl #1
        sub             r1,  r1,  r2,  lsl #1

        @ Store pixels:
        vst1.8          {d2},     [r0,:64], r2  @ P2
        vst1.8          {d3},     [r1,:64], r2  @ P2
        vst1.8          {d4},     [r0,:64], r2  @ P1
        vst1.8          {d5},     [r1,:64], r2  @ P1
        vst1.8          {d6},     [r0,:64], r2  @ P0
        vst1.8          {d7},     [r1,:64], r2  @ P0
        vst1.8          {d8},     [r0,:64], r2  @ Q0
        vst1.8          {d9},     [r1,:64], r2  @ Q0
        vst1.8          {d10},    [r0,:64], r2  @ Q1
        vst1.8          {d11},    [r1,:64], r2  @ Q1
        vst1.8          {d12},    [r0,:64]      @ Q2
        vst1.8          {d13},    [r1,:64]      @ Q2

        vpop            {q4-q7}
        bx              lr
endfunc
.endm

vp8_v_loop_filter8uv
vp8_v_loop_filter8uv _inner, inner=1

.macro  vp8_h_loop_filter16 name, inner=0, simple=0
function ff_vp8_h_loop_filter16\name\()_neon, export=1
        vpush           {q4-q7}
        sub             r0,  r0,  #4
    .if !\simple
        ldr             r12, [sp, #64]          @ hev_thresh
    .endif

        @ Load pixels:
        vld1.8          {d0},     [r0], r1      @ load first 8-line src data
        vld1.8          {d2},     [r0], r1
        vld1.8          {d4},     [r0], r1
        vld1.8          {d6},     [r0], r1
        vld1.8          {d8},     [r0], r1
        vld1.8          {d10},    [r0], r1
        vld1.8          {d12},    [r0], r1
        vld1.8          {d14},    [r0], r1
        vld1.8          {d1},     [r0], r1      @ load second 8-line src data
        vld1.8          {d3},     [r0], r1
        vld1.8          {d5},     [r0], r1
        vld1.8          {d7},     [r0], r1
        vld1.8          {d9},     [r0], r1
        vld1.8          {d11},    [r0], r1
        vld1.8          {d13},    [r0], r1
        vld1.8          {d15},    [r0], r1

        transpose_8x8   q0,  q1,  q2,  q3,  q4,  q5,  q6,  q7

        vdup.8          q14, r2                 @ flim_E
    .if !\simple
        vdup.8          q15, r3                 @ flim_I
    .endif

        vp8_loop_filter inner=\inner, simple=\simple

        sub             r0,  r0,  r1, lsl #4    @ backup 16 rows

        transpose_8x8   q0,  q1,  q2,  q3,  q4,  q5,  q6,  q7

        @ Store pixels:
        vst1.8          {d0},     [r0],     r1
        vst1.8          {d2},     [r0],     r1
        vst1.8          {d4},     [r0],     r1
        vst1.8          {d6},     [r0],     r1
        vst1.8          {d8},     [r0],     r1
        vst1.8          {d10},    [r0],     r1
        vst1.8          {d12},    [r0],     r1
        vst1.8          {d14},    [r0],     r1
        vst1.8          {d1},     [r0],     r1
        vst1.8          {d3},     [r0],     r1
        vst1.8          {d5},     [r0],     r1
        vst1.8          {d7},     [r0],     r1
        vst1.8          {d9},     [r0],     r1
        vst1.8          {d11},    [r0],     r1
        vst1.8          {d13},    [r0],     r1
        vst1.8          {d15},    [r0]

        vpop            {q4-q7}
        bx              lr
endfunc
.endm

vp8_h_loop_filter16
vp8_h_loop_filter16 _inner,  inner=1
vp8_h_loop_filter16 _simple, simple=1

.macro  vp8_h_loop_filter8uv name, inner=0
function ff_vp8_h_loop_filter8uv\name\()_neon, export=1
        vpush           {q4-q7}
        sub             r0,  r0,  #4
        sub             r1,  r1,  #4
        ldr             r12, [sp, #64]          @ flim_I

        @ Load pixels:
        vld1.8          {d0},     [r0], r2      @ load u
        vld1.8          {d1},     [r1], r2      @ load v
        vld1.8          {d2},     [r0], r2
        vld1.8          {d3},     [r1], r2
        vld1.8          {d4},     [r0], r2
        vld1.8          {d5},     [r1], r2
        vld1.8          {d6},     [r0], r2
        vld1.8          {d7},     [r1], r2
        vld1.8          {d8},     [r0], r2
        vld1.8          {d9},     [r1], r2
        vld1.8          {d10},    [r0], r2
        vld1.8          {d11},    [r1], r2
        vld1.8          {d12},    [r0], r2
        vld1.8          {d13},    [r1], r2
        vld1.8          {d14},    [r0], r2
        vld1.8          {d15},    [r1], r2

        transpose_8x8   q0,  q1,  q2,  q3,  q4,  q5,  q6,  q7

        vdup.8          q14, r3                 @ flim_E
        vdup.8          q15, r12                @ flim_I
        ldr             r12, [sp, #68]          @ hev_thresh

        vp8_loop_filter inner=\inner

        sub             r0,  r0,  r2, lsl #3    @ backup u 8 rows
        sub             r1,  r1,  r2, lsl #3    @ backup v 8 rows

        transpose_8x8   q0,  q1,  q2,  q3,  q4,  q5,  q6,  q7

        @ Store pixels:
        vst1.8          {d0},     [r0], r2
        vst1.8          {d1},     [r1], r2
        vst1.8          {d2},     [r0], r2
        vst1.8          {d3},     [r1], r2
        vst1.8          {d4},     [r0], r2
        vst1.8          {d5},     [r1], r2
        vst1.8          {d6},     [r0], r2
        vst1.8          {d7},     [r1], r2
        vst1.8          {d8},     [r0], r2
        vst1.8          {d9},     [r1], r2
        vst1.8          {d10},    [r0], r2
        vst1.8          {d11},    [r1], r2
        vst1.8          {d12},    [r0], r2
        vst1.8          {d13},    [r1], r2
        vst1.8          {d14},    [r0]
        vst1.8          {d15},    [r1]

        vpop            {q4-q7}
        bx              lr
endfunc
.endm

vp8_h_loop_filter8uv
vp8_h_loop_filter8uv _inner, inner=1

function ff_put_vp8_pixels16_neon, export=1
        ldr             r12, [sp, #0]           @ h
1:
        subs            r12, r12, #4
        vld1.8          {q0},     [r2], r3
        vld1.8          {q1},     [r2], r3
        vld1.8          {q2},     [r2], r3
        vld1.8          {q3},     [r2], r3
        vst1.8          {q0},     [r0,:128], r1
        vst1.8          {q1},     [r0,:128], r1
        vst1.8          {q2},     [r0,:128], r1
        vst1.8          {q3},     [r0,:128], r1
        bgt             1b
        bx              lr
endfunc

function ff_put_vp8_pixels8_neon, export=1
        ldr             r12, [sp, #0]           @ h
1:
        subs            r12, r12, #4
        vld1.8          {d0},     [r2], r3
        vld1.8          {d1},     [r2], r3
        vld1.8          {d2},     [r2], r3
        vld1.8          {d3},     [r2], r3
        vst1.8          {d0},     [r0,:64], r1
        vst1.8          {d1},     [r0,:64], r1
        vst1.8          {d2},     [r0,:64], r1
        vst1.8          {d3},     [r0,:64], r1
        bgt             1b
        bx              lr
endfunc

/* 4/6-tap 8th-pel MC */

.macro  vp8_epel8_h6    d,   a,   b
        vext.8          d27, \a,  \b,  #1
        vmovl.u8        q8,  \a
        vext.8          d28, \a,  \b,  #2
        vmovl.u8        q9,  d27
        vext.8          d29, \a,  \b,  #3
        vmovl.u8        q10, d28
        vext.8          d30, \a,  \b,  #4
        vmovl.u8        q11, d29
        vext.8          d31, \a,  \b,  #5
        vmovl.u8        q12, d30
        vmul.u16        q10, q10, d0[2]
        vmovl.u8        q13, d31
        vmul.u16        q11, q11, d0[3]
        vmls.u16        q10, q9,  d0[1]
        vmls.u16        q11, q12, d1[0]
        vmla.u16        q10, q8,  d0[0]
        vmla.u16        q11, q13, d1[1]
        vqadd.s16       q11, q10, q11
        vqrshrun.s16    \d,  q11, #7
.endm

.macro  vp8_epel16_h6   d0,  d1,  s0,  s1,  s2,  q0,  q1
        vext.8          q14, \q0, \q1, #3
        vext.8          q15, \q0, \q1, #4
        vmovl.u8        q11, d28
        vmovl.u8        q14, d29
        vext.8          q3,  \q0, \q1, #2
        vmovl.u8        q12, d30
        vmovl.u8        q15, d31
        vext.8          q8,  \q0, \q1, #1
        vmovl.u8        q10, d6
        vmovl.u8        q3,  d7
        vext.8          q2,  \q0, \q1, #5
        vmovl.u8        q13, d4
        vmovl.u8        q2,  d5
        vmovl.u8        q9,  d16
        vmovl.u8        q8,  d17
        vmul.u16        q11, q11, d0[3]
        vmul.u16        q10, q10, d0[2]
        vmul.u16        q3,  q3,  d0[2]
        vmul.u16        q14, q14, d0[3]
        vmls.u16        q11, q12, d1[0]
        vmovl.u8        q12, \s0
        vmovl.u8        q1,  \s1
        vmls.u16        q10, q9,  d0[1]
        vmls.u16        q3,  q8,  d0[1]
        vmls.u16        q14, q15, d1[0]
        vmla.u16        q10, q12, d0[0]
        vmla.u16        q11, q13, d1[1]
        vmla.u16        q3,  q1,  d0[0]
        vmla.u16        q14, q2,  d1[1]
        vqadd.s16       q11, q10, q11
        vqadd.s16       q14, q3,  q14
        vqrshrun.s16    \d0, q11, #7
        vqrshrun.s16    \d1, q14, #7
.endm

.macro  vp8_epel8_v6    d0,  s0,  s1,  s2,  s3,  s4,  s5
        vmovl.u8        q10, \s2
        vmovl.u8        q11, \s3
        vmovl.u8        q9,  \s1
        vmovl.u8        q12, \s4
        vmovl.u8        q8,  \s0
        vmovl.u8        q13, \s5
        vmul.u16        q10, q10, d0[2]
        vmul.u16        q11, q11, d0[3]
        vmls.u16        q10, q9,  d0[1]
        vmls.u16        q11, q12, d1[0]
        vmla.u16        q10, q8,  d0[0]
        vmla.u16        q11, q13, d1[1]
        vqadd.s16       q11, q10, q11
        vqrshrun.s16    \d0, q11, #7
.endm

.macro  vp8_epel8_v6_y2 d0, d1, s0, s1, s2, s3, s4, s5, s6
        vmovl.u8        q10, \s0
        vmovl.u8        q11, \s3
        vmovl.u8        q14, \s6
        vmovl.u8        q9,  \s1
        vmovl.u8        q12, \s4
        vmovl.u8        q8,  \s2
        vmovl.u8        q13, \s5
        vmul.u16        q10, q10, d0[0]
        vmul.u16        q15, q11, d0[3]
        vmul.u16        q11, q11, d0[2]
        vmul.u16        q14, q14, d1[1]
        vmls.u16        q10, q9,  d0[1]
        vmls.u16        q15, q12, d1[0]
        vmls.u16        q11, q8,  d0[1]
        vmls.u16        q14, q13, d1[0]
        vmla.u16        q10, q8,  d0[2]
        vmla.u16        q15, q13, d1[1]
        vmla.u16        q11, q9,  d0[0]
        vmla.u16        q14, q12, d0[3]
        vqadd.s16       q15, q10, q15
        vqadd.s16       q14, q11, q14
        vqrshrun.s16    \d0, q15, #7
        vqrshrun.s16    \d1, q14, #7
.endm

.macro  vp8_epel8_h4    d,   a,   b
        vext.8          d28, \a,  \b,  #1
        vmovl.u8        q9,  \a
        vext.8          d29, \a,  \b,  #2
        vmovl.u8        q10, d28
        vext.8          d30, \a,  \b,  #3
        vmovl.u8        q11, d29
        vmovl.u8        q12, d30
        vmul.u16        q10, q10, d0[2]
        vmul.u16        q11, q11, d0[3]
        vmls.u16        q10, q9,  d0[1]
        vmls.u16        q11, q12, d1[0]
        vqadd.s16       q11, q10, q11
        vqrshrun.s16    \d,  q11, #7
.endm

.macro  vp8_epel8_v4_y2 d0,  d1,  s0,  s1,  s2,  s3,  s4
        vmovl.u8        q9,  \s0
        vmovl.u8        q10, \s1
        vmovl.u8        q11, \s2
        vmovl.u8        q12, \s3
        vmovl.u8        q13, \s4
        vmul.u16        q8,  q10, d0[2]
        vmul.u16        q14, q11, d0[3]
        vmul.u16        q11, q11, d0[2]
        vmul.u16        q15, q12, d0[3]
        vmls.u16        q8,  q9,  d0[1]
        vmls.u16        q14, q12, d1[0]
        vmls.u16        q11, q10, d0[1]
        vmls.u16        q15, q13, d1[0]
        vqadd.s16       q8,  q8,  q14
        vqadd.s16       q11, q11, q15
        vqrshrun.s16    \d0, q8,  #7
        vqrshrun.s16    \d1, q11, #7
.endm

function ff_put_vp8_epel16_v6_neon, export=1
        sub             r2,  r2,  r3,  lsl #1
        push            {r4,lr}
        vpush           {d8-d15}

        ldr             r4,  [sp, #80]          @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #72]          @ h
        add             r4,  lr,  r4, lsl #4
        vld1.16         {q0},     [r4,:128]
1:
        vld1.8          {d2-d3},  [r2], r3
        vld1.8          {d4-d5},  [r2], r3
        vld1.8          {d6-d7},  [r2], r3
        vld1.8          {d8-d9},  [r2], r3
        vld1.8          {d10-d11},[r2], r3
        vld1.8          {d12-d13},[r2], r3
        vld1.8          {d14-d15},[r2]
        sub             r2,  r2,  r3,  lsl #2

        vp8_epel8_v6_y2 d2,  d4,  d2,  d4,  d6,  d8,  d10, d12, d14
        vp8_epel8_v6_y2 d3,  d5,  d3,  d5,  d7,  d9,  d11, d13, d15

        vst1.8          {d2-d3},  [r0,:128], r1
        vst1.8          {d4-d5},  [r0,:128], r1
        subs            r12, r12, #2
        bne             1b

        vpop            {d8-d15}
        pop             {r4,pc}
endfunc

function ff_put_vp8_epel16_h6_neon, export=1
        sub             r2,  r2,  #2
        push            {r4,lr}

        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        vld1.16         {q0},     [r4,:128]
1:
        vld1.8          {d2-d4},  [r2], r3

        vp8_epel16_h6   d2,  d3,  d2,  d3,  d4,  q1,  q2

        vst1.8          {d2-d3}, [r0,:128], r1
        subs            r12, r12, #1
        bne             1b

        pop             {r4,pc}
endfunc

function ff_put_vp8_epel16_h6v6_neon, export=1
        sub             r2,  r2,  r3,  lsl #1
        sub             r2,  r2,  #2
        push            {r4,lr}
        vpush           {d8-d9}

        @ first pass (horizontal):
        ldr             r4,  [sp, #28]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #24]          @ h
        add             r4,  lr,  r4, lsl #4
        sub             sp,  sp,  #336+16
        vld1.16         {q0},     [r4,:128]
        add             lr,  sp,  #15
        add             r12, r12, #5
        bic             lr,  lr,  #15
1:
        vld1.8          {d2,d3,d4}, [r2], r3

        vp8_epel16_h6   d2,  d3,  d2,  d3,  d4,  q1,  q2

        vst1.8          {d2-d3}, [lr,:128]!
        subs            r12, r12, #1
        bne             1b

        @ second pass (vertical):
        ldr             r4,  [sp, #336+16+32]   @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #336+16+24]   @ h
        add             r4,  lr,  r4, lsl #4
        add             lr,  sp,  #15
        vld1.16         {q0},     [r4,:128]
        bic             lr,  lr,  #15
2:
        vld1.8          {d2-d5},  [lr,:128]!
        vld1.8          {d6-d9},  [lr,:128]!
        vld1.8          {d28-d31},[lr,:128]
        sub             lr,  lr,  #48

        vp8_epel8_v6    d2, d2, d4, d6, d8, d28, d30
        vp8_epel8_v6    d3, d3, d5, d7, d9, d29, d31

        vst1.8          {d2-d3}, [r0,:128], r1
        subs            r12, r12, #1
        bne             2b

        add             sp,  sp,  #336+16
        vpop            {d8-d9}
        pop             {r4,pc}
endfunc

function ff_put_vp8_epel8_v6_neon, export=1
        sub             r2,  r2,  r3,  lsl #1
        push            {r4,lr}

        ldr             r4,  [sp, #16]          @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        vld1.16         {q0},     [r4,:128]
1:
        vld1.8          {d2},  [r2], r3
        vld1.8          {d3},  [r2], r3
        vld1.8          {d4},  [r2], r3
        vld1.8          {d5},  [r2], r3
        vld1.8          {d6},  [r2], r3
        vld1.8          {d7},  [r2], r3
        vld1.8          {d28}, [r2]

        sub             r2,  r2,  r3,  lsl #2

        vp8_epel8_v6_y2 d2,  d3,  d2,  d3,  d4,  d5,  d6,  d7,  d28

        vst1.8          {d2}, [r0,:64], r1
        vst1.8          {d3}, [r0,:64], r1
        subs            r12, r12, #2
        bne             1b

        pop             {r4,pc}
endfunc

function ff_put_vp8_epel8_h6_neon, export=1
        sub             r2,  r2,  #2
        push            {r4,lr}

        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        vld1.16         {q0},     [r4,:128]
1:
        vld1.8          {d2,d3}, [r2], r3

        vp8_epel8_h6    d2,  d2,  d3

        vst1.8          {d2}, [r0,:64], r1
        subs            r12, r12, #1
        bne             1b

        pop             {r4,pc}
endfunc

function ff_put_vp8_epel8_h6v6_neon, export=1
        sub             r2,  r2,  r3,  lsl #1
        sub             r2,  r2,  #2
        push            {r4,lr}

        @ first pass (horizontal):
        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        sub             sp,  sp,  #168+16
        vld1.16         {q0},     [r4,:128]
        add             lr,  sp,  #15
        add             r12, r12, #5
        bic             lr,  lr,  #15
1:
        vld1.8          {d2,d3}, [r2], r3

        vp8_epel8_h6    d2,  d2,  d3

        vst1.8          {d2}, [lr,:64]!
        subs            r12, r12, #1
        bne             1b

        @ second pass (vertical):
        ldr             r4,  [sp, #168+16+16]   @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #168+16+8]    @ h
        add             r4,  lr,  r4, lsl #4
        add             lr,  sp,  #15
        vld1.16         {q0},     [r4,:128]
        bic             lr,  lr,  #15
2:
        vld1.8          {d2-d5},  [lr,:128]!
        vld1.8          {d6-d7},  [lr,:128]!
        vld1.8          {d30},    [lr,:64]
        sub             lr,  lr,  #32

        vp8_epel8_v6_y2 d2,  d3,  d2,  d3,  d4,  d5,  d6,  d7,  d30

        vst1.8          {d2}, [r0,:64], r1
        vst1.8          {d3}, [r0,:64], r1
        subs            r12, r12, #2
        bne             2b

        add             sp,  sp,  #168+16
        pop             {r4,pc}
endfunc

function ff_put_vp8_epel8_v4_neon, export=1
        sub             r2,  r2,  r3
        push            {r4,lr}

        ldr             r4,  [sp, #16]          @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        vld1.16         {q0},     [r4,:128]
1:
        vld1.8          {d2},     [r2], r3
        vld1.8          {d3},     [r2], r3
        vld1.8          {d4},     [r2], r3
        vld1.8          {d5},     [r2], r3
        vld1.8          {d6},     [r2]
        sub             r2,  r2,  r3,  lsl #1

        vp8_epel8_v4_y2 d2,  d3,  d2,  d3,  d4,  d5,  d6

        vst1.8          {d2}, [r0,:64], r1
        vst1.8          {d3}, [r0,:64], r1
        subs            r12, r12, #2
        bne             1b

        pop             {r4,pc}
endfunc

function ff_put_vp8_epel8_h4_neon, export=1
        sub             r2,  r2,  #1
        push            {r4,lr}

        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        vld1.16         {q0},     [r4,:128]
1:
        vld1.8          {d2,d3}, [r2], r3

        vp8_epel8_h4    d2,  d2,  d3

        vst1.8          {d2}, [r0,:64], r1
        subs            r12, r12, #1
        bne             1b

        pop             {r4,pc}
endfunc

function ff_put_vp8_epel8_h4v4_neon, export=1
        sub             r2,  r2,  r3
        sub             r2,  r2,  #1
        push            {r4,lr}

        @ first pass (horizontal):
        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        sub             sp,  sp,  #168+16
        vld1.16         {q0},     [r4,:128]
        add             lr,  sp,  #15
        add             r12, r12, #3
        bic             lr,  lr,  #15
1:
        vld1.8          {d2,d3}, [r2], r3

        vp8_epel8_h4    d2,  d2,  d3

        vst1.8          {d2}, [lr,:64]!
        subs            r12, r12, #1
        bne             1b

        @ second pass (vertical):
        ldr             r4,  [sp, #168+16+16]   @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #168+16+8]    @ h
        add             r4,  lr,  r4, lsl #4
        add             lr,  sp,  #15
        vld1.16         {q0},     [r4,:128]
        bic             lr,  lr,  #15
2:
        vld1.8          {d2-d5},  [lr,:128]!
        vld1.8          {d6},     [lr,:64]
        sub             lr,  lr,  #16

        vp8_epel8_v4_y2 d2,  d3,  d2,  d3,  d4,  d5,  d6

        vst1.8          {d2},     [r0,:64], r1
        vst1.8          {d3},     [r0,:64], r1
        subs            r12, r12, #2
        bne             2b

        add             sp,  sp,  #168+16
        pop             {r4,pc}
endfunc

function ff_put_vp8_epel8_h6v4_neon, export=1
        sub             r2,  r2,  r3
        sub             r2,  r2,  #2
        push            {r4,lr}

        @ first pass (horizontal):
        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        sub             sp,  sp,  #168+16
        vld1.16         {q0},     [r4,:128]
        add             lr,  sp,  #15
        add             r12, r12, #3
        bic             lr,  lr,  #15
1:
        vld1.8          {d2,d3}, [r2], r3

        vp8_epel8_h6    d2,  d2,  d3

        vst1.8          {d2}, [lr,:64]!
        subs            r12, r12, #1
        bne             1b

        @ second pass (vertical):
        ldr             r4,  [sp, #168+16+16]   @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #168+16+8]    @ h
        add             r4,  lr,  r4, lsl #4
        add             lr,  sp,  #15
        vld1.16         {q0},     [r4,:128]
        bic             lr,  lr,  #15
2:
        vld1.8          {d2-d5},  [lr,:128]!
        vld1.8          {d6},     [lr,:64]
        sub             lr,  lr,  #16

        vp8_epel8_v4_y2 d2,  d3,  d2,  d3,  d4,  d5,  d6

        vst1.8          {d2},     [r0,:64], r1
        vst1.8          {d3},     [r0,:64], r1
        subs            r12, r12, #2
        bne             2b

        add             sp,  sp,  #168+16
        pop             {r4,pc}
endfunc

function ff_put_vp8_epel8_h4v6_neon, export=1
        sub             r2,  r2,  r3,  lsl #1
        sub             r2,  r2,  #1
        push            {r4,lr}

        @ first pass (horizontal):
        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        sub             sp,  sp,  #168+16
        vld1.16         {q0},     [r4,:128]
        add             lr,  sp,  #15
        add             r12, r12, #5
        bic             lr,  lr,  #15
1:
        vld1.8          {d2,d3}, [r2], r3

        vp8_epel8_h4    d2,  d2,  d3

        vst1.8          {d2}, [lr,:64]!
        subs            r12, r12, #1
        bne             1b

        @ second pass (vertical):
        ldr             r4,  [sp, #168+16+16]   @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #168+16+8]    @ h
        add             r4,  lr,  r4, lsl #4
        add             lr,  sp,  #15
        vld1.16         {q0},     [r4,:128]
        bic             lr,  lr,  #15
2:
        vld1.8          {d2-d5},  [lr,:128]!
        vld1.8          {d6-d7},  [lr,:128]!
        vld1.8          {d30},    [lr,:64]
        sub             lr,  lr,  #32

        vp8_epel8_v6_y2 d2,  d3,  d2,  d3,  d4,  d5,  d6,  d7,  d30

        vst1.8          {d2}, [r0,:64], r1
        vst1.8          {d3}, [r0,:64], r1
        subs            r12, r12, #2
        bne             2b

        add             sp,  sp,  #168+16
        pop             {r4,pc}
endfunc

.ltorg

function ff_put_vp8_epel4_v6_neon, export=1
        sub             r2,  r2,  r3,  lsl #1
        push            {r4,lr}

        ldr             r4,  [sp, #16]          @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        vld1.16         {q0},     [r4,:128]
1:
        vld1.32         {d2[]},   [r2], r3
        vld1.32         {d3[]},   [r2], r3
        vld1.32         {d4[]},   [r2], r3
        vld1.32         {d5[]},   [r2], r3
        vld1.32         {d6[]},   [r2], r3
        vld1.32         {d7[]},   [r2], r3
        vld1.32         {d28[]},  [r2]
        sub             r2,  r2,  r3,  lsl #2
        vld1.32         {d2[1]},  [r2], r3
        vld1.32         {d3[1]},  [r2], r3
        vld1.32         {d4[1]},  [r2], r3
        vld1.32         {d5[1]},  [r2], r3
        vld1.32         {d6[1]},  [r2], r3
        vld1.32         {d7[1]},  [r2], r3
        vld1.32         {d28[1]}, [r2]
        sub             r2,  r2,  r3,  lsl #2

        vp8_epel8_v6_y2 d2,  d3,  d2,  d3,  d4,  d5,  d6,  d7,  d28

        vst1.32         {d2[0]},  [r0,:32], r1
        vst1.32         {d3[0]},  [r0,:32], r1
        vst1.32         {d2[1]},  [r0,:32], r1
        vst1.32         {d3[1]},  [r0,:32], r1
        subs            r12, r12, #4
        bne             1b

        pop             {r4,pc}
endfunc

function ff_put_vp8_epel4_h6_neon, export=1
        sub             r2,  r2,  #2
        push            {r4,lr}

        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        vld1.16         {q0},     [r4,:128]
1:
        vld1.8          {q1},     [r2], r3
        vp8_epel8_h6    d2,  d2,  d3
        vst1.32         {d2[0]},  [r0,:32], r1
        subs            r12, r12, #1
        bne             1b

        pop             {r4,pc}
endfunc

function ff_put_vp8_epel4_h6v6_neon, export=1
        sub             r2,  r2,  r3,  lsl #1
        sub             r2,  r2,  #2
        push            {r4,lr}

        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        sub             sp,  sp,  #52+16
        vld1.16         {q0},     [r4,:128]
        add             lr,  sp,  #15
        add             r12, r12, #5
        bic             lr,  lr,  #15
1:
        vld1.8          {q1},     [r2], r3
        vp8_epel8_h6    d2,  d2,  d3
        vst1.32         {d2[0]},  [lr,:32]!
        subs            r12, r12, #1
        bne             1b

        ldr             r4,  [sp, #52+16+16]    @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #52+16+8]     @ h
        add             r4,  lr,  r4, lsl #4
        add             lr,  sp,  #15
        vld1.16         {q0},     [r4,:128]
        bic             lr,  lr,  #15
2:
        vld1.8          {d2-d3},  [lr,:128]!
        vld1.8          {d6},     [lr,:64]!
        vld1.32         {d28[]},  [lr,:32]
        sub             lr,  lr,  #16
        vld1.8          {d4-d5},  [lr]!
        vld1.8          {d7},     [lr,:64]!
        vld1.32         {d28[1]}, [lr,:32]
        sub             lr,  lr,  #16
        vtrn.32         q1,  q2
        vtrn.32         d6,  d7
        vp8_epel8_v6_y2 d2,  d3,  d2,  d4,  d3,  d5,  d6,  d7,  d28
        vst1.32         {d2[0]},  [r0,:32], r1
        vst1.32         {d3[0]},  [r0,:32], r1
        vst1.32         {d2[1]},  [r0,:32], r1
        vst1.32         {d3[1]},  [r0,:32], r1
        subs            r12, r12, #4
        bne             2b

        add             sp,  sp,  #52+16
        pop             {r4,pc}
endfunc

function ff_put_vp8_epel4_h4v6_neon, export=1
        sub             r2,  r2,  r3,  lsl #1
        sub             r2,  r2,  #1
        push            {r4,lr}

        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        sub             sp,  sp,  #52+16
        vld1.16         {q0},     [r4,:128]
        add             lr,  sp,  #15
        add             r12, r12, #5
        bic             lr,  lr,  #15
1:
        vld1.8          {d2},     [r2], r3
        vp8_epel8_h4    d2,  d2,  d2
        vst1.32         {d2[0]},  [lr,:32]!
        subs            r12, r12, #1
        bne             1b

        ldr             r4,  [sp, #52+16+16]    @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #52+16+8]     @ h
        add             r4,  lr,  r4, lsl #4
        add             lr,  sp,  #15
        vld1.16         {q0},     [r4,:128]
        bic             lr,  lr,  #15
2:
        vld1.8          {d2-d3},  [lr,:128]!
        vld1.8          {d6},     [lr,:64]!
        vld1.32         {d28[]},  [lr,:32]
        sub             lr,  lr,  #16
        vld1.8          {d4-d5},  [lr]!
        vld1.8          {d7},     [lr,:64]!
        vld1.32         {d28[1]}, [lr,:32]
        sub             lr,  lr,  #16
        vtrn.32         q1,  q2
        vtrn.32         d6,  d7
        vp8_epel8_v6_y2 d2,  d3,  d2,  d4,  d3,  d5,  d6,  d7,  d28
        vst1.32         {d2[0]},  [r0,:32], r1
        vst1.32         {d3[0]},  [r0,:32], r1
        vst1.32         {d2[1]},  [r0,:32], r1
        vst1.32         {d3[1]},  [r0,:32], r1
        subs            r12, r12, #4
        bne             2b

        add             sp,  sp,  #52+16
        pop             {r4,pc}
endfunc

function ff_put_vp8_epel4_h6v4_neon, export=1
        sub             r2,  r2,  r3
        sub             r2,  r2,  #2
        push            {r4,lr}

        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        sub             sp,  sp,  #44+16
        vld1.16         {q0},     [r4,:128]
        add             lr,  sp,  #15
        add             r12, r12, #3
        bic             lr,  lr,  #15
1:
        vld1.8          {q1},     [r2], r3
        vp8_epel8_h6    d2,  d2,  d3
        vst1.32         {d2[0]},  [lr,:32]!
        subs            r12, r12, #1
        bne             1b

        ldr             r4,  [sp, #44+16+16]    @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #44+16+8]     @ h
        add             r4,  lr,  r4, lsl #4
        add             lr,  sp,  #15
        vld1.16         {q0},     [r4,:128]
        bic             lr,  lr,  #15
2:
        vld1.8          {d2-d3},  [lr,:128]!
        vld1.32         {d6[]},   [lr,:32]
        sub             lr,  lr,  #8
        vld1.8          {d4-d5},  [lr]!
        vld1.32         {d6[1]},  [lr,:32]
        sub             lr,  lr,  #8
        vtrn.32         q1,  q2
        vp8_epel8_v4_y2 d2,  d3,  d2,  d4,  d3,  d5,  d6
        vst1.32         {d2[0]},  [r0,:32], r1
        vst1.32         {d3[0]},  [r0,:32], r1
        vst1.32         {d2[1]},  [r0,:32], r1
        vst1.32         {d3[1]},  [r0,:32], r1
        subs            r12, r12, #4
        bne             2b

        add             sp,  sp,  #44+16
        pop             {r4,pc}
endfunc

function ff_put_vp8_epel4_h4_neon, export=1
        sub             r2,  r2,  #1
        push            {r4,lr}

        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        vld1.16         {q0},     [r4,:128]
1:
        vld1.8          {d2},     [r2], r3
        vp8_epel8_h4    d2,  d2,  d2
        vst1.32         {d2[0]},  [r0,:32], r1
        subs            r12, r12, #1
        bne             1b

        pop             {r4,pc}
endfunc

function ff_put_vp8_epel4_v4_neon, export=1
        sub             r2,  r2,  r3
        push            {r4,lr}

        ldr             r4,  [sp, #16]          @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        vld1.16         {q0},     [r4,:128]
1:
        vld1.32         {d2[]},   [r2], r3
        vld1.32         {d3[]},   [r2], r3
        vld1.32         {d4[]},   [r2], r3
        vld1.32         {d5[]},   [r2], r3
        vld1.32         {d6[]},   [r2]
        sub             r2,  r2,  r3,  lsl #1
        vld1.32         {d2[1]},  [r2], r3
        vld1.32         {d3[1]},  [r2], r3
        vld1.32         {d4[1]},  [r2], r3
        vld1.32         {d5[1]},  [r2], r3
        vld1.32         {d6[1]},  [r2]
        sub             r2,  r2,  r3,  lsl #1

        vp8_epel8_v4_y2 d2,  d3,  d2,  d3,  d4,  d5,  d6

        vst1.32         {d2[0]},  [r0,:32], r1
        vst1.32         {d3[0]},  [r0,:32], r1
        vst1.32         {d2[1]},  [r0,:32], r1
        vst1.32         {d3[1]},  [r0,:32], r1
        subs            r12, r12, #4
        bne             1b

        pop             {r4,pc}
endfunc

function ff_put_vp8_epel4_h4v4_neon, export=1
        sub             r2,  r2,  r3
        sub             r2,  r2,  #1
        push            {r4,lr}

        ldr             r4,  [sp, #12]          @ mx
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #8]           @ h
        add             r4,  lr,  r4, lsl #4
        sub             sp,  sp,  #44+16
        vld1.16         {q0},     [r4,:128]
        add             lr,  sp,  #15
        add             r12, r12, #3
        bic             lr,  lr,  #15
1:
        vld1.8          {d2},     [r2], r3
        vp8_epel8_h4    d2,  d2,  d3
        vst1.32         {d2[0]},  [lr,:32]!
        subs            r12, r12, #1
        bne             1b

        ldr             r4,  [sp, #44+16+16]    @ my
        movrel          lr,  subpel_filters-16
        ldr             r12, [sp, #44+16+8]     @ h
        add             r4,  lr,  r4, lsl #4
        add             lr,  sp,  #15
        vld1.16         {q0},     [r4,:128]
        bic             lr,  lr,  #15
2:
        vld1.8          {d2-d3},  [lr,:128]!
        vld1.32         {d6[]},   [lr,:32]
        sub             lr,  lr,  #8
        vld1.8          {d4-d5},  [lr]!
        vld1.32         {d6[1]},  [lr,:32]
        sub             lr,  lr,  #8
        vtrn.32         q1,  q2
        vp8_epel8_v4_y2 d2,  d3,  d2,  d4,  d3,  d5,  d6
        vst1.32         {d2[0]},  [r0,:32], r1
        vst1.32         {d3[0]},  [r0,:32], r1
        vst1.32         {d2[1]},  [r0,:32], r1
        vst1.32         {d3[1]},  [r0,:32], r1
        subs            r12, r12, #4
        bne             2b

        add             sp,  sp,  #44+16
        pop             {r4,pc}
endfunc

@ note: worst case sum of all 6-tap filter values * 255 is 0x7f80 so 16 bit
@ arithmatic can be used to apply filters
const   subpel_filters, align=4
        .short     0,   6, 123,  12,   1,   0,   0,   0
        .short     2,  11, 108,  36,   8,   1,   0,   0
        .short     0,   9,  93,  50,   6,   0,   0,   0
        .short     3,  16,  77,  77,  16,   3,   0,   0
        .short     0,   6,  50,  93,   9,   0,   0,   0
        .short     1,   8,  36, 108,  11,   2,   0,   0
        .short     0,   1,  12, 123,   6,   0,   0,   0
endconst

/* Bilinear MC */

function ff_put_vp8_bilin16_h_neon, export=1
        ldr             r3,  [sp, #4]           @ mx
        rsb             r12, r3,  #8
        vdup.8          d0,  r3
        vdup.8          d1,  r12
        ldr             r12, [sp]               @ h
1:
        subs            r12, r12, #2
        vld1.8          {d2-d4},  [r2], r1
        vext.8          q2,  q1,  q2,  #1
        vmull.u8        q8,  d2,  d1
        vmlal.u8        q8,  d4,  d0
        vld1.8          {d18-d20},[r2], r1
        vmull.u8        q3,  d3,  d1
        vmlal.u8        q3,  d5,  d0
        vext.8          q10, q9,  q10, #1
        vmull.u8        q11, d18, d1
        vmlal.u8        q11, d20, d0
        vmull.u8        q12, d19, d1
        vmlal.u8        q12, d21, d0
        vrshrn.u16      d4,  q8,  #3
        vrshrn.u16      d5,  q3,  #3
        vrshrn.u16      d6,  q11, #3
        vrshrn.u16      d7,  q12, #3
        vst1.8          {q2},     [r0,:128], r1
        vst1.8          {q3},     [r0,:128], r1
        bgt             1b

        bx              lr
endfunc

function ff_put_vp8_bilin16_v_neon, export=1
        ldr             r3,  [sp, #8]           @ my
        rsb             r12, r3,  #8
        vdup.8          d0,  r3
        vdup.8          d1,  r12
        ldr             r12, [sp]               @ h
        vld1.8          {q1},     [r2], r1
1:
        subs            r12, r12, #2
        vld1.8          {q2},     [r2], r1
        vmull.u8        q3,  d2,  d1
        vmlal.u8        q3,  d4,  d0
        vmull.u8        q8,  d3,  d1
        vmlal.u8        q8,  d5,  d0
        vld1.8          {q1},     [r2], r1
        vmull.u8        q9,  d4,  d1
        vmlal.u8        q9,  d2,  d0
        vmull.u8        q10, d5,  d1
        vmlal.u8        q10, d3,  d0
        vrshrn.u16      d4,  q3,  #3
        vrshrn.u16      d5,  q8,  #3
        vrshrn.u16      d6,  q9,  #3
        vrshrn.u16      d7,  q10, #3
        vst1.8          {q2},     [r0,:128], r1
        vst1.8          {q3},     [r0,:128], r1
        bgt             1b

        bx              lr
endfunc

function ff_put_vp8_bilin16_hv_neon, export=1
        ldr             r3,  [sp, #4]           @ mx
        rsb             r12, r3,  #8
        vdup.8          d0,  r3
        vdup.8          d1,  r12
        ldr             r3,  [sp, #8]           @ my
        rsb             r12, r3,  #8
        vdup.8          d2,  r3
        vdup.8          d3,  r12
        ldr             r12, [sp]               @ h

        vld1.8          {d4-d6},  [r2], r1
        vext.8          q3,  q2,  q3,  #1
        vmull.u8        q8,  d4,  d1
        vmlal.u8        q8,  d6,  d0
        vmull.u8        q9,  d5,  d1
        vmlal.u8        q9,  d7,  d0
        vrshrn.u16      d4,  q8,  #3
        vrshrn.u16      d5,  q9,  #3
1:
        subs            r12, r12, #2
        vld1.8          {d18-d20},[r2], r1
        vext.8          q10, q9,  q10, #1
        vmull.u8        q11, d18, d1
        vmlal.u8        q11, d20, d0
        vld1.8          {d26-d28},[r2], r1
        vmull.u8        q12, d19, d1
        vmlal.u8        q12, d21, d0
        vext.8          q14, q13, q14, #1
        vmull.u8        q8,  d26, d1
        vmlal.u8        q8,  d28, d0
        vmull.u8        q9,  d27, d1
        vmlal.u8        q9,  d29, d0
        vrshrn.u16      d6,  q11, #3
        vrshrn.u16      d7,  q12, #3
        vmull.u8        q12, d4,  d3
        vmlal.u8        q12, d6,  d2
        vmull.u8        q15, d5,  d3
        vmlal.u8        q15, d7,  d2
        vrshrn.u16      d4,  q8,  #3
        vrshrn.u16      d5,  q9,  #3
        vmull.u8        q10, d6,  d3
        vmlal.u8        q10, d4,  d2
        vmull.u8        q11, d7,  d3
        vmlal.u8        q11, d5,  d2
        vrshrn.u16      d24, q12, #3
        vrshrn.u16      d25, q15, #3
        vst1.8          {q12},    [r0,:128], r1
        vrshrn.u16      d20, q10, #3
        vrshrn.u16      d21, q11, #3
        vst1.8          {q10},    [r0,:128], r1
        bgt             1b

        bx              lr
endfunc

function ff_put_vp8_bilin8_h_neon, export=1
        ldr             r3,  [sp, #4]           @ mx
        rsb             r12, r3,  #8
        vdup.8          d0,  r3
        vdup.8          d1,  r12
        ldr             r12, [sp]               @ h
1:
        subs            r12, r12, #2
        vld1.8          {q1},     [r2], r1
        vext.8          d3,  d2,  d3,  #1
        vmull.u8        q2,  d2,  d1
        vmlal.u8        q2,  d3,  d0
        vld1.8          {q3},     [r2], r1
        vext.8          d7,  d6,  d7,  #1
        vmull.u8        q8,  d6,  d1
        vmlal.u8        q8,  d7,  d0
        vrshrn.u16      d4,  q2,  #3
        vrshrn.u16      d16, q8,  #3
        vst1.8          {d4},     [r0,:64], r1
        vst1.8          {d16},    [r0,:64], r1
        bgt             1b

        bx              lr
endfunc

function ff_put_vp8_bilin8_v_neon, export=1
        ldr             r3,  [sp, #8]           @ my
        rsb             r12, r3,  #8
        vdup.8          d0,  r3
        vdup.8          d1,  r12
        ldr             r12, [sp]               @ h
        vld1.8          {d2},     [r2], r1
1:
        subs            r12, r12, #2
        vld1.8          {d3},     [r2], r1
        vmull.u8        q2,  d2,  d1
        vmlal.u8        q2,  d3,  d0
        vld1.8          {d2},     [r2], r1
        vmull.u8        q3,  d3,  d1
        vmlal.u8        q3,  d2,  d0
        vrshrn.u16      d4,  q2,  #3
        vrshrn.u16      d6,  q3,  #3
        vst1.8          {d4},     [r0,:64], r1
        vst1.8          {d6},     [r0,:64], r1
        bgt             1b

        bx              lr
endfunc

function ff_put_vp8_bilin8_hv_neon, export=1
        ldr             r3,  [sp, #4]           @ mx
        rsb             r12, r3,  #8
        vdup.8          d0,  r3
        vdup.8          d1,  r12
        ldr             r3,  [sp, #8]           @ my
        rsb             r12, r3,  #8
        vdup.8          d2,  r3
        vdup.8          d3,  r12
        ldr             r12, [sp]               @ h

        vld1.8          {q2},     [r2], r1
        vext.8          d5,  d4,  d5,  #1
        vmull.u8        q9,  d4,  d1
        vmlal.u8        q9,  d5,  d0
        vrshrn.u16      d22, q9,  #3
1:
        subs            r12, r12, #2
        vld1.8          {q3},     [r2], r1
        vext.8          d7,  d6,  d7,  #1
        vmull.u8        q8,  d6,  d1
        vmlal.u8        q8,  d7,  d0
        vld1.8          {q2},     [r2], r1
        vext.8          d5,  d4,  d5,  #1
        vmull.u8        q9,  d4,  d1
        vmlal.u8        q9,  d5,  d0
        vrshrn.u16      d16, q8,  #3
        vmull.u8        q10, d22, d3
        vmlal.u8        q10, d16, d2
        vrshrn.u16      d22, q9,  #3
        vmull.u8        q12, d16, d3
        vmlal.u8        q12, d22, d2
        vrshrn.u16      d20, q10, #3
        vst1.8          {d20},    [r0,:64], r1
        vrshrn.u16      d23, q12, #3
        vst1.8          {d23},    [r0,:64], r1
        bgt             1b

        bx              lr
endfunc

function ff_put_vp8_bilin4_h_neon, export=1
        ldr             r3,  [sp, #4]           @ mx
        rsb             r12, r3,  #8
        vdup.8          d0,  r3
        vdup.8          d1,  r12
        ldr             r12, [sp]               @ h
1:
        subs            r12, r12, #2
        vld1.8          {d2},     [r2], r1
        vext.8          d3,  d2,  d3,  #1
        vld1.8          {d6},     [r2], r1
        vext.8          d7,  d6,  d7,  #1
        vtrn.32         q1,  q3
        vmull.u8        q2,  d2,  d1
        vmlal.u8        q2,  d3,  d0
        vrshrn.u16      d4,  q2,  #3
        vst1.32         {d4[0]},  [r0,:32], r1
        vst1.32         {d4[1]}, [r0,:32], r1
        bgt             1b

        bx              lr
endfunc

function ff_put_vp8_bilin4_v_neon, export=1
        ldr             r3,  [sp, #8]           @ my
        rsb             r12, r3,  #8
        vdup.8          d0,  r3
        vdup.8          d1,  r12
        ldr             r12, [sp]               @ h
        vld1.32         {d2[]},   [r2], r1
1:
        vld1.32         {d3[]},   [r2]
        vld1.32         {d2[1]},  [r2], r1
        vld1.32         {d3[1]},  [r2], r1
        vmull.u8        q2,  d2,  d1
        vmlal.u8        q2,  d3,  d0
        vtrn.32         d3,  d2
        vrshrn.u16      d4,  q2,  #3
        vst1.32         {d4[0]},  [r0,:32], r1
        vst1.32         {d4[1]},  [r0,:32], r1
        subs            r12, r12, #2
        bgt             1b

        bx              lr
endfunc

function ff_put_vp8_bilin4_hv_neon, export=1
        ldr             r3,  [sp, #4]           @ mx
        rsb             r12, r3,  #8
        vdup.8          d0,  r3
        vdup.8          d1,  r12
        ldr             r3,  [sp, #8]           @ my
        rsb             r12, r3,  #8
        vdup.8          d2,  r3
        vdup.8          d3,  r12
        ldr             r12, [sp]               @ h

        vld1.8          {d4},     [r2], r1
        vext.8          d5,  d4,  d4,  #1
        vmull.u8        q9,  d4,  d1
        vmlal.u8        q9,  d5,  d0
        vrshrn.u16      d22, q9,  #3
1:
        subs            r12, r12, #2
        vld1.8          {d6},     [r2], r1
        vext.8          d7,  d6,  d6,  #1
        vld1.8          {d4},     [r2], r1
        vext.8          d5,  d4,  d4,  #1
        vtrn.32         q3,  q2
        vmull.u8        q8,  d6,  d1
        vmlal.u8        q8,  d7,  d0
        vrshrn.u16      d16, q8,  #3
        vmull.u8        q10, d16, d2
        vtrn.32         d22, d16
        vmlal.u8        q10, d22, d3
        vrev64.32       d22, d16
        vrshrn.u16      d20, q10, #3
        vst1.32         {d20[0]}, [r0,:32], r1
        vst1.32         {d20[1]}, [r0,:32], r1
        bgt             1b

        bx              lr
endfunc
