/*
 * Copyright (c) 2009 David Conrad
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

.section .rodata
.align 4

vp3_idct_constants:
.short 64277, 60547, 54491, 46341, 36410, 25080, 12785

#define xC1S7 d0[0]
#define xC2S6 d0[1]
#define xC3S5 d0[2]
#define xC4S4 d0[3]
#define xC5S3 d1[0]
#define xC6S2 d1[1]
#define xC7S1 d1[2]

.text

.macro vp3_loop_filter
    vsubl.u8        q3,  d18, d17
    vsubl.u8        q2,  d16, d19
    vadd.i16        q1,  q3,  q3
    vadd.i16        q2,  q2,  q3
    vadd.i16        q0,  q1,  q2
    vrshr.s16       q0,  q0,  #3
    vmovl.u8        q9,  d18
    vdup.u16        q15, r2

    vabs.s16        q1,  q0
    vshr.s16        q0,  q0,  #15
    vqsub.u16       q2,  q15, q1
    vqsub.u16       q3,  q2,  q1
    vsub.i16        q1,  q2,  q3
    veor            q1,  q1,  q0
    vsub.i16        q0,  q1,  q0

    vaddw.u8        q2,  q0,  d17
    vsub.i16        q3,  q9,  q0
    vqmovun.s16     d0,  q2
    vqmovun.s16     d1,  q3
.endm

function ff_vp3_v_loop_filter_neon, export=1
    sub             ip,  r0,  r1
    sub             r0,  r0,  r1,  lsl #1
    vld1.64         {d16}, [r0,:64], r1
    vld1.64         {d17}, [r0,:64], r1
    vld1.64         {d18}, [r0,:64], r1
    vld1.64         {d19}, [r0,:64], r1
    ldrb            r2,    [r2, #129*4]

    vp3_loop_filter

    vst1.64         {d0},  [ip,:64], r1
    vst1.64         {d1},  [ip,:64], r1
    bx              lr
endfunc

function ff_vp3_h_loop_filter_neon, export=1
    sub             ip,  r0,  #1
    sub             r0,  r0,  #2
    vld1.32         {d16[]},  [r0], r1
    vld1.32         {d17[]},  [r0], r1
    vld1.32         {d18[]},  [r0], r1
    vld1.32         {d19[]},  [r0], r1
    vld1.32         {d16[1]}, [r0], r1
    vld1.32         {d17[1]}, [r0], r1
    vld1.32         {d18[1]}, [r0], r1
    vld1.32         {d19[1]}, [r0], r1
    ldrb            r2,  [r2, #129*4]

    vtrn.8          d16, d17
    vtrn.8          d18, d19
    vtrn.16         d16, d18
    vtrn.16         d17, d19

    vp3_loop_filter

    vtrn.8          d0,  d1

    vst1.16         {d0[0]}, [ip], r1
    vst1.16         {d1[0]}, [ip], r1
    vst1.16         {d0[1]}, [ip], r1
    vst1.16         {d1[1]}, [ip], r1
    vst1.16         {d0[2]}, [ip], r1
    vst1.16         {d1[2]}, [ip], r1
    vst1.16         {d0[3]}, [ip], r1
    vst1.16         {d1[3]}, [ip], r1
    bx              lr
endfunc


function vp3_idct_start_neon
    vpush           {d8-d15}
    movrel          r3,  vp3_idct_constants
    vld1.64         {d0-d1},   [r3,:128]
    vld1.64         {d16-d19}, [r2,:128]!
    vld1.64         {d20-d23}, [r2,:128]!
    vld1.64         {d24-d27}, [r2,:128]!
    vadd.s16        q1,  q8,  q12
    vsub.s16        q8,  q8,  q12
    vld1.64         {d28-d31}, [r2,:128]!
endfunc

function vp3_idct_core_neon
    vmull.s16       q2,  d18, xC1S7     // (ip[1] * C1) << 16
    vmull.s16       q3,  d19, xC1S7
    vmull.s16       q4,  d2,  xC4S4     // ((ip[0] + ip[4]) * C4) << 16
    vmull.s16       q5,  d3,  xC4S4
    vmull.s16       q6,  d16, xC4S4     // ((ip[0] - ip[4]) * C4) << 16
    vmull.s16       q7,  d17, xC4S4
    vshrn.s32       d4,  q2,  #16
    vshrn.s32       d5,  q3,  #16
    vshrn.s32       d6,  q4,  #16
    vshrn.s32       d7,  q5,  #16
    vshrn.s32       d8,  q6,  #16
    vshrn.s32       d9,  q7,  #16
    vadd.s16        q12, q1,  q3        // E = (ip[0] + ip[4]) * C4
    vadd.s16        q8,  q8,  q4        // F = (ip[0] - ip[4]) * C4
    vadd.s16        q1,  q2,  q9        // ip[1] * C1

    vmull.s16       q2,  d30, xC1S7     // (ip[7] * C1) << 16
    vmull.s16       q3,  d31, xC1S7
    vmull.s16       q4,  d30, xC7S1     // (ip[7] * C7) << 16
    vmull.s16       q5,  d31, xC7S1
    vmull.s16       q6,  d18, xC7S1     // (ip[1] * C7) << 16
    vmull.s16       q7,  d19, xC7S1
    vshrn.s32       d4,  q2,  #16
    vshrn.s32       d5,  q3,  #16
    vshrn.s32       d6,  q4,  #16       // ip[7] * C7
    vshrn.s32       d7,  q5,  #16
    vshrn.s32       d8,  q6,  #16       // ip[1] * C7
    vshrn.s32       d9,  q7,  #16
    vadd.s16        q2,  q2,  q15       // ip[7] * C1
    vadd.s16        q9,  q1,  q3        // A = ip[1] * C1 + ip[7] * C7
    vsub.s16        q15, q4,  q2        // B = ip[1] * C7 - ip[7] * C1

    vmull.s16       q2,  d22, xC5S3     // (ip[3] * C5) << 16
    vmull.s16       q3,  d23, xC5S3
    vmull.s16       q4,  d22, xC3S5     // (ip[3] * C3) << 16
    vmull.s16       q5,  d23, xC3S5
    vmull.s16       q6,  d26, xC5S3     // (ip[5] * C5) << 16
    vmull.s16       q7,  d27, xC5S3
    vshrn.s32       d4,  q2,  #16
    vshrn.s32       d5,  q3,  #16
    vshrn.s32       d6,  q4,  #16
    vshrn.s32       d7,  q5,  #16
    vshrn.s32       d8,  q6,  #16
    vshrn.s32       d9,  q7,  #16
    vadd.s16        q3,  q3,  q11       // ip[3] * C3
    vadd.s16        q4,  q4,  q13       // ip[5] * C5
    vadd.s16        q1,  q2,  q11       // ip[3] * C5
    vadd.s16        q11, q3,  q4        // C = ip[3] * C3 + ip[5] * C5

    vmull.s16       q2,  d26, xC3S5     // (ip[5] * C3) << 16
    vmull.s16       q3,  d27, xC3S5
    vmull.s16       q4,  d20, xC2S6     // (ip[2] * C2) << 16
    vmull.s16       q5,  d21, xC2S6
    vmull.s16       q6,  d28, xC6S2     // (ip[6] * C6) << 16
    vmull.s16       q7,  d29, xC6S2
    vshrn.s32       d4,  q2,  #16
    vshrn.s32       d5,  q3,  #16
    vshrn.s32       d6,  q4,  #16
    vshrn.s32       d7,  q5,  #16
    vshrn.s32       d8,  q6,  #16       // ip[6] * C6
    vshrn.s32       d9,  q7,  #16
    vadd.s16        q2,  q2,  q13       // ip[5] * C3
    vadd.s16        q3,  q3,  q10       // ip[2] * C2
    vsub.s16        q13, q2,  q1        // D = ip[5] * C3 - ip[3] * C5
    vsub.s16        q1,  q9,  q11       // (A - C)
    vadd.s16        q11, q9,  q11       // Cd = A + C
    vsub.s16        q9,  q15, q13       // (B - D)
    vadd.s16        q13, q15, q13       // Dd = B + D
    vadd.s16        q15, q3,  q4        // G = ip[2] * C2 + ip[6] * C6

    vmull.s16       q2,  d2,  xC4S4     // ((A - C) * C4) << 16
    vmull.s16       q3,  d3,  xC4S4
    vmull.s16       q4,  d28, xC2S6     // (ip[6] * C2) << 16
    vmull.s16       q5,  d29, xC2S6
    vmull.s16       q6,  d20, xC6S2     // (ip[2] * C6) << 16
    vmull.s16       q7,  d21, xC6S2
    vshrn.s32       d4,  q2,  #16
    vshrn.s32       d5,  q3,  #16
    vshrn.s32       d6,  q4,  #16
    vshrn.s32       d7,  q5,  #16
    vshrn.s32       d8,  q6,  #16       // ip[2] * C6
    vmull.s16       q5,  d18, xC4S4     // ((B - D) * C4) << 16
    vmull.s16       q6,  d19, xC4S4
    vshrn.s32       d9,  q7,  #16
    vadd.s16        q3,  q3,  q14       // ip[6] * C2
    vadd.s16        q10, q1,  q2        // Ad = (A - C) * C4
    vsub.s16        q14, q4,  q3        // H = ip[2] * C6 - ip[6] * C2
    bx              lr
endfunc

.macro VP3_IDCT_END type
function vp3_idct_end_\type\()_neon
.ifc \type, col
    vdup.16         q0,  r3
    vadd.s16        q12, q12, q0
    vadd.s16        q8,  q8,  q0
.endif

    vshrn.s32       d2,  q5,  #16
    vshrn.s32       d3,  q6,  #16
    vadd.s16        q2,  q12, q15       // Gd  = E + G
    vadd.s16        q9,  q1,  q9        // (B - D) * C4
    vsub.s16        q12, q12, q15       // Ed  = E - G
    vsub.s16        q3,  q8,  q10       // Fd  = F - Ad
    vadd.s16        q10, q8,  q10       // Add = F + Ad
    vadd.s16        q4,  q9,  q14       // Hd  = Bd + H
    vsub.s16        q14, q9,  q14       // Bdd = Bd - H
    vadd.s16        q8,  q2,  q11       // [0] = Gd + Cd
    vsub.s16        q15, q2,  q11       // [7] = Gd - Cd
    vadd.s16        q9,  q10, q4        // [1] = Add + Hd
    vsub.s16        q10, q10, q4        // [2] = Add - Hd
    vadd.s16        q11, q12, q13       // [3] = Ed + Dd
    vsub.s16        q12, q12, q13       // [4] = Ed - Dd
.ifc \type, row
    vtrn.16         q8,  q9
.endif
    vadd.s16        q13, q3,  q14       // [5] = Fd + Bdd
    vsub.s16        q14, q3,  q14       // [6] = Fd - Bdd

.ifc \type, row
    // 8x8 transpose
    vtrn.16         q10, q11
    vtrn.16         q12, q13
    vtrn.16         q14, q15
    vtrn.32         q8,  q10
    vtrn.32         q9,  q11
    vtrn.32         q12, q14
    vtrn.32         q13, q15
    vswp            d17, d24
    vswp            d19, d26
    vadd.s16        q1,  q8,  q12
    vswp            d21, d28
    vsub.s16        q8,  q8,  q12
    vswp            d23, d30
.endif
    bx              lr
endfunc
.endm

VP3_IDCT_END row
VP3_IDCT_END col

function ff_vp3_idct_neon, export=1
    mov             ip,  lr
    mov             r2,  r0
    bl              vp3_idct_start_neon
    bl              vp3_idct_end_row_neon
    mov             r3,  #8
    bl              vp3_idct_core_neon
    bl              vp3_idct_end_col_neon
    mov             lr,  ip
    vpop            {d8-d15}

    vshr.s16        q8,  q8,  #4
    vshr.s16        q9,  q9,  #4
    vshr.s16        q10, q10, #4
    vshr.s16        q11, q11, #4
    vshr.s16        q12, q12, #4
    vst1.64         {d16-d19}, [r0,:128]!
    vshr.s16        q13, q13, #4
    vshr.s16        q14, q14, #4
    vst1.64         {d20-d23}, [r0,:128]!
    vshr.s16        q15, q15, #4
    vst1.64         {d24-d27}, [r0,:128]!
    vst1.64         {d28-d31}, [r0,:128]!
    bx              lr
endfunc

function ff_vp3_idct_put_neon, export=1
    mov             ip,  lr
    bl              vp3_idct_start_neon
    bl              vp3_idct_end_row_neon
    mov             r3,  #8
    add             r3,  r3,  #2048         // convert signed pixel to unsigned
    bl              vp3_idct_core_neon
    bl              vp3_idct_end_col_neon
    mov             lr,  ip
    vpop            {d8-d15}

    vqshrun.s16     d0,  q8,  #4
    vqshrun.s16     d1,  q9,  #4
    vqshrun.s16     d2,  q10, #4
    vqshrun.s16     d3,  q11, #4
    vst1.64         {d0}, [r0,:64], r1
    vqshrun.s16     d4,  q12, #4
    vst1.64         {d1}, [r0,:64], r1
    vqshrun.s16     d5,  q13, #4
    vst1.64         {d2}, [r0,:64], r1
    vqshrun.s16     d6,  q14, #4
    vst1.64         {d3}, [r0,:64], r1
    vqshrun.s16     d7,  q15, #4
    vst1.64         {d4}, [r0,:64], r1
    vst1.64         {d5}, [r0,:64], r1
    vst1.64         {d6}, [r0,:64], r1
    vst1.64         {d7}, [r0,:64], r1
    bx              lr
endfunc

function ff_vp3_idct_add_neon, export=1
    mov             ip,  lr
    bl              vp3_idct_start_neon
    bl              vp3_idct_end_row_neon
    mov             r3,  #8
    bl              vp3_idct_core_neon
    bl              vp3_idct_end_col_neon
    mov             lr,  ip
    vpop            {d8-d15}
    mov             r2,  r0

    vld1.64         {d0}, [r0,:64], r1
    vshr.s16        q8,  q8,  #4
    vld1.64         {d1}, [r0,:64], r1
    vshr.s16        q9,  q9,  #4
    vld1.64         {d2}, [r0,:64], r1
    vaddw.u8        q8,  q8,  d0
    vld1.64         {d3}, [r0,:64], r1
    vaddw.u8        q9,  q9,  d1
    vld1.64         {d4}, [r0,:64], r1
    vshr.s16        q10, q10, #4
    vld1.64         {d5}, [r0,:64], r1
    vshr.s16        q11, q11, #4
    vld1.64         {d6}, [r0,:64], r1
    vqmovun.s16     d0,  q8
    vld1.64         {d7}, [r0,:64], r1
    vqmovun.s16     d1,  q9
    vaddw.u8        q10, q10, d2
    vaddw.u8        q11, q11, d3
    vshr.s16        q12, q12, #4
    vshr.s16        q13, q13, #4
    vqmovun.s16     d2,  q10
    vqmovun.s16     d3,  q11
    vaddw.u8        q12, q12, d4
    vaddw.u8        q13, q13, d5
    vshr.s16        q14, q14, #4
    vshr.s16        q15, q15, #4
    vst1.64         {d0}, [r2,:64], r1
    vqmovun.s16     d4,  q12
    vst1.64         {d1}, [r2,:64], r1
    vqmovun.s16     d5,  q13
    vst1.64         {d2}, [r2,:64], r1
    vaddw.u8        q14, q14, d6
    vst1.64         {d3}, [r2,:64], r1
    vaddw.u8        q15, q15, d7
    vst1.64         {d4}, [r2,:64], r1
    vqmovun.s16     d6,  q14
    vst1.64         {d5}, [r2,:64], r1
    vqmovun.s16     d7,  q15
    vst1.64         {d6}, [r2,:64], r1
    vst1.64         {d7}, [r2,:64], r1
    bx              lr
endfunc

function ff_vp3_idct_dc_add_neon, export=1
    ldrsh           r2,  [r2]
    movw            r3,  #46341
    mul             r2,  r3,  r2
    smulwt          r2,  r3,  r2
    mov             r3,  r0
    vdup.16         q15, r2
    vrshr.s16       q15, q15, #4

    vld1.8          {d0}, [r0,:64], r1
    vld1.8          {d1}, [r0,:64], r1
    vld1.8          {d2}, [r0,:64], r1
    vaddw.u8        q8,  q15, d0
    vld1.8          {d3}, [r0,:64], r1
    vaddw.u8        q9,  q15, d1
    vld1.8          {d4}, [r0,:64], r1
    vaddw.u8        q10, q15, d2
    vld1.8          {d5}, [r0,:64], r1
    vaddw.u8        q11, q15, d3
    vld1.8          {d6}, [r0,:64], r1
    vaddw.u8        q12, q15, d4
    vld1.8          {d7}, [r0,:64], r1
    vaddw.u8        q13, q15, d5
    vqmovun.s16     d0,  q8
    vaddw.u8        q14, q15, d6
    vqmovun.s16     d1,  q9
    vaddw.u8        q15, q15, d7
    vqmovun.s16     d2,  q10
    vst1.8          {d0}, [r3,:64], r1
    vqmovun.s16     d3,  q11
    vst1.8          {d1}, [r3,:64], r1
    vqmovun.s16     d4,  q12
    vst1.8          {d2}, [r3,:64], r1
    vqmovun.s16     d5,  q13
    vst1.8          {d3}, [r3,:64], r1
    vqmovun.s16     d6,  q14
    vst1.8          {d4}, [r3,:64], r1
    vqmovun.s16     d7,  q15
    vst1.8          {d5}, [r3,:64], r1
    vst1.8          {d6}, [r3,:64], r1
    vst1.8          {d7}, [r3,:64], r1
    bx              lr
endfunc
