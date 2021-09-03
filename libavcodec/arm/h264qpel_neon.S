/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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

#include "libavutil/arm/asm.S"
#include "neon.S"

        /* H.264 qpel MC */

.macro  lowpass_const   r
        movw            \r,  #5
        movt            \r,  #20
        vmov.32         d6[0], \r
.endm

.macro  lowpass_8       r0,  r1,  r2,  r3,  d0,  d1,  narrow=1
  .if \narrow
        t0 .req q0
        t1 .req q8
  .else
        t0 .req \d0
        t1 .req \d1
  .endif
        vext.8          d2,  \r0, \r1, #2
        vext.8          d3,  \r0, \r1, #3
        vaddl.u8        q1,  d2,  d3
        vext.8          d4,  \r0, \r1, #1
        vext.8          d5,  \r0, \r1, #4
        vaddl.u8        q2,  d4,  d5
        vext.8          d30, \r0, \r1, #5
        vaddl.u8        t0,  \r0, d30
        vext.8          d18, \r2, \r3, #2
        vmla.i16        t0,  q1,  d6[1]
        vext.8          d19, \r2, \r3, #3
        vaddl.u8        q9,  d18, d19
        vext.8          d20, \r2, \r3, #1
        vmls.i16        t0,  q2,  d6[0]
        vext.8          d21, \r2, \r3, #4
        vaddl.u8        q10, d20, d21
        vext.8          d31, \r2, \r3, #5
        vaddl.u8        t1,  \r2, d31
        vmla.i16        t1,  q9,  d6[1]
        vmls.i16        t1,  q10, d6[0]
  .if \narrow
        vqrshrun.s16    \d0, t0,  #5
        vqrshrun.s16    \d1, t1,  #5
  .endif
        .unreq  t0
        .unreq  t1
.endm

.macro  lowpass_8_1     r0,  r1,  d0,  narrow=1
  .if \narrow
        t0 .req q0
  .else
        t0 .req \d0
  .endif
        vext.8          d2,  \r0, \r1, #2
        vext.8          d3,  \r0, \r1, #3
        vaddl.u8        q1,  d2,  d3
        vext.8          d4,  \r0, \r1, #1
        vext.8          d5,  \r0, \r1, #4
        vaddl.u8        q2,  d4,  d5
        vext.8          d30, \r0, \r1, #5
        vaddl.u8        t0,  \r0, d30
        vmla.i16        t0,  q1,  d6[1]
        vmls.i16        t0,  q2,  d6[0]
  .if \narrow
        vqrshrun.s16    \d0, t0,  #5
  .endif
        .unreq  t0
.endm

.macro  lowpass_8.16    r0,  r1,  l0,  h0,  l1,  h1,  d
        vext.16         q1,  \r0, \r1, #2
        vext.16         q0,  \r0, \r1, #3
        vaddl.s16       q9,  d2,  d0
        vext.16         q2,  \r0, \r1, #1
        vaddl.s16       q1,  d3,  d1
        vext.16         q3,  \r0, \r1, #4
        vaddl.s16       q10, d4,  d6
        vext.16         \r1, \r0, \r1, #5
        vaddl.s16       q2,  d5,  d7
        vaddl.s16       q0,  \h0, \h1
        vaddl.s16       q8,  \l0, \l1

        vshl.i32        q3,  q9,  #4
        vshl.i32        q9,  q9,  #2
        vshl.i32        q15, q10, #2
        vadd.i32        q9,  q9,  q3
        vadd.i32        q10, q10, q15

        vshl.i32        q3,  q1,  #4
        vshl.i32        q1,  q1,  #2
        vshl.i32        q15, q2,  #2
        vadd.i32        q1,  q1,  q3
        vadd.i32        q2,  q2,  q15

        vadd.i32        q9,  q9,  q8
        vsub.i32        q9,  q9,  q10

        vadd.i32        q1,  q1,  q0
        vsub.i32        q1,  q1,  q2

        vrshrn.s32      d18, q9,  #10
        vrshrn.s32      d19, q1,  #10

        vqmovun.s16     \d,  q9
.endm

function put_h264_qpel16_h_lowpass_neon_packed
        mov             r4,  lr
        mov             r12, #16
        mov             r3,  #8
        bl              put_h264_qpel8_h_lowpass_neon
        sub             r1,  r1,  r2, lsl #4
        add             r1,  r1,  #8
        mov             r12, #16
        mov             lr,  r4
        b               put_h264_qpel8_h_lowpass_neon
endfunc

.macro  h264_qpel_h_lowpass type
function \type\()_h264_qpel16_h_lowpass_neon
        push            {lr}
        mov             r12, #16
        bl              \type\()_h264_qpel8_h_lowpass_neon
        sub             r0,  r0,  r3, lsl #4
        sub             r1,  r1,  r2, lsl #4
        add             r0,  r0,  #8
        add             r1,  r1,  #8
        mov             r12, #16
        pop             {lr}
endfunc

function \type\()_h264_qpel8_h_lowpass_neon
1:      vld1.8          {d0, d1},  [r1], r2
        vld1.8          {d16,d17}, [r1], r2
        subs            r12, r12, #2
        lowpass_8       d0,  d1,  d16, d17, d0,  d16
  .ifc \type,avg
        vld1.8          {d2},     [r0,:64], r3
        vld1.8          {d3},     [r0,:64]
        vrhadd.u8       d0,  d0,  d2
        vrhadd.u8       d16, d16, d3
        sub             r0,  r0,  r3
  .endif
        vst1.8          {d0},     [r0,:64], r3
        vst1.8          {d16},    [r0,:64], r3
        bne             1b
        bx              lr
endfunc
.endm

        h264_qpel_h_lowpass put
        h264_qpel_h_lowpass avg

.macro  h264_qpel_h_lowpass_l2 type
function \type\()_h264_qpel16_h_lowpass_l2_neon
        push            {lr}
        mov             r12, #16
        bl              \type\()_h264_qpel8_h_lowpass_l2_neon
        sub             r0,  r0,  r2, lsl #4
        sub             r1,  r1,  r2, lsl #4
        sub             r3,  r3,  r2, lsl #4
        add             r0,  r0,  #8
        add             r1,  r1,  #8
        add             r3,  r3,  #8
        mov             r12, #16
        pop             {lr}
endfunc

function \type\()_h264_qpel8_h_lowpass_l2_neon
1:      vld1.8          {d0, d1},  [r1], r2
        vld1.8          {d16,d17}, [r1], r2
        vld1.8          {d28},     [r3], r2
        vld1.8          {d29},     [r3], r2
        subs            r12, r12, #2
        lowpass_8       d0,  d1,  d16, d17, d0,  d1
        vrhadd.u8       q0,  q0,  q14
  .ifc \type,avg
        vld1.8          {d2},      [r0,:64], r2
        vld1.8          {d3},      [r0,:64]
        vrhadd.u8       q0,  q0,  q1
        sub             r0,  r0,  r2
  .endif
        vst1.8          {d0},      [r0,:64], r2
        vst1.8          {d1},      [r0,:64], r2
        bne             1b
        bx              lr
endfunc
.endm

        h264_qpel_h_lowpass_l2 put
        h264_qpel_h_lowpass_l2 avg

function put_h264_qpel16_v_lowpass_neon_packed
        mov             r4,  lr
        mov             r2,  #8
        bl              put_h264_qpel8_v_lowpass_neon
        sub             r1,  r1,  r3, lsl #2
        bl              put_h264_qpel8_v_lowpass_neon
        sub             r1,  r1,  r3, lsl #4
        sub             r1,  r1,  r3, lsl #2
        add             r1,  r1,  #8
        bl              put_h264_qpel8_v_lowpass_neon
        sub             r1,  r1,  r3, lsl #2
        mov             lr,  r4
        b               put_h264_qpel8_v_lowpass_neon
endfunc

.macro  h264_qpel_v_lowpass type
function \type\()_h264_qpel16_v_lowpass_neon
        mov             r4,  lr
        bl              \type\()_h264_qpel8_v_lowpass_neon
        sub             r1,  r1,  r3, lsl #2
        bl              \type\()_h264_qpel8_v_lowpass_neon
        sub             r0,  r0,  r2, lsl #4
        add             r0,  r0,  #8
        sub             r1,  r1,  r3, lsl #4
        sub             r1,  r1,  r3, lsl #2
        add             r1,  r1,  #8
        bl              \type\()_h264_qpel8_v_lowpass_neon
        sub             r1,  r1,  r3, lsl #2
        mov             lr,  r4
endfunc

function \type\()_h264_qpel8_v_lowpass_neon
        vld1.8          {d8},  [r1], r3
        vld1.8          {d10}, [r1], r3
        vld1.8          {d12}, [r1], r3
        vld1.8          {d14}, [r1], r3
        vld1.8          {d22}, [r1], r3
        vld1.8          {d24}, [r1], r3
        vld1.8          {d26}, [r1], r3
        vld1.8          {d28}, [r1], r3
        vld1.8          {d9},  [r1], r3
        vld1.8          {d11}, [r1], r3
        vld1.8          {d13}, [r1], r3
        vld1.8          {d15}, [r1], r3
        vld1.8          {d23}, [r1]

        transpose_8x8   q4,  q5,  q6,  q7,  q11, q12, q13, q14
        lowpass_8       d8,  d9,  d10, d11, d8,  d10
        lowpass_8       d12, d13, d14, d15, d12, d14
        lowpass_8       d22, d23, d24, d25, d22, d24
        lowpass_8       d26, d27, d28, d29, d26, d28
        transpose_8x8   d8,  d10, d12, d14, d22, d24, d26, d28

  .ifc \type,avg
        vld1.8          {d9},  [r0,:64], r2
        vld1.8          {d11}, [r0,:64], r2
        vld1.8          {d13}, [r0,:64], r2
        vrhadd.u8       d8,  d8,  d9
        vld1.8          {d15}, [r0,:64], r2
        vrhadd.u8       d10, d10, d11
        vld1.8          {d23}, [r0,:64], r2
        vrhadd.u8       d12, d12, d13
        vld1.8          {d25}, [r0,:64], r2
        vrhadd.u8       d14, d14, d15
        vld1.8          {d27}, [r0,:64], r2
        vrhadd.u8       d22, d22, d23
        vld1.8          {d29}, [r0,:64], r2
        vrhadd.u8       d24, d24, d25
        vrhadd.u8       d26, d26, d27
        vrhadd.u8       d28, d28, d29
        sub             r0,  r0,  r2,  lsl #3
  .endif

        vst1.8          {d8},  [r0,:64], r2
        vst1.8          {d10}, [r0,:64], r2
        vst1.8          {d12}, [r0,:64], r2
        vst1.8          {d14}, [r0,:64], r2
        vst1.8          {d22}, [r0,:64], r2
        vst1.8          {d24}, [r0,:64], r2
        vst1.8          {d26}, [r0,:64], r2
        vst1.8          {d28}, [r0,:64], r2

        bx              lr
endfunc
.endm

        h264_qpel_v_lowpass put
        h264_qpel_v_lowpass avg

.macro  h264_qpel_v_lowpass_l2 type
function \type\()_h264_qpel16_v_lowpass_l2_neon
        mov             r4,  lr
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        sub             r1,  r1,  r3, lsl #2
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        sub             r0,  r0,  r3, lsl #4
        sub             r12, r12, r2, lsl #4
        add             r0,  r0,  #8
        add             r12, r12, #8
        sub             r1,  r1,  r3, lsl #4
        sub             r1,  r1,  r3, lsl #2
        add             r1,  r1,  #8
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        sub             r1,  r1,  r3, lsl #2
        mov             lr,  r4
endfunc

function \type\()_h264_qpel8_v_lowpass_l2_neon
        vld1.8          {d8},  [r1], r3
        vld1.8          {d10}, [r1], r3
        vld1.8          {d12}, [r1], r3
        vld1.8          {d14}, [r1], r3
        vld1.8          {d22}, [r1], r3
        vld1.8          {d24}, [r1], r3
        vld1.8          {d26}, [r1], r3
        vld1.8          {d28}, [r1], r3
        vld1.8          {d9},  [r1], r3
        vld1.8          {d11}, [r1], r3
        vld1.8          {d13}, [r1], r3
        vld1.8          {d15}, [r1], r3
        vld1.8          {d23}, [r1]

        transpose_8x8   q4,  q5,  q6,  q7,  q11, q12, q13, q14
        lowpass_8       d8,  d9,  d10, d11, d8,  d9
        lowpass_8       d12, d13, d14, d15, d12, d13
        lowpass_8       d22, d23, d24, d25, d22, d23
        lowpass_8       d26, d27, d28, d29, d26, d27
        transpose_8x8   d8,  d9,  d12, d13, d22, d23, d26, d27

        vld1.8          {d0},  [r12], r2
        vld1.8          {d1},  [r12], r2
        vld1.8          {d2},  [r12], r2
        vld1.8          {d3},  [r12], r2
        vld1.8          {d4},  [r12], r2
        vrhadd.u8       q0,  q0,  q4
        vld1.8          {d5},  [r12], r2
        vrhadd.u8       q1,  q1,  q6
        vld1.8          {d10}, [r12], r2
        vrhadd.u8       q2,  q2,  q11
        vld1.8          {d11}, [r12], r2
        vrhadd.u8       q5,  q5,  q13

  .ifc \type,avg
        vld1.8          {d16}, [r0,:64], r3
        vld1.8          {d17}, [r0,:64], r3
        vrhadd.u8       d0,  d0,  d16
        vld1.8          {d16}, [r0,:64], r3
        vrhadd.u8       d1,  d1,  d17
        vld1.8          {d17}, [r0,:64], r3
        vrhadd.u8       d2,  d2,  d16
        vld1.8          {d16}, [r0,:64], r3
        vrhadd.u8       d3,  d3,  d17
        vld1.8          {d17}, [r0,:64], r3
        vrhadd.u8       d4,  d4,  d16
        vld1.8          {d16}, [r0,:64], r3
        vrhadd.u8       d5,  d5,  d17
        vld1.8          {d17}, [r0,:64], r3
        vrhadd.u8       d10, d10, d16
        vrhadd.u8       d11, d11, d17
        sub             r0,  r0,  r3,  lsl #3
  .endif

        vst1.8          {d0},  [r0,:64], r3
        vst1.8          {d1},  [r0,:64], r3
        vst1.8          {d2},  [r0,:64], r3
        vst1.8          {d3},  [r0,:64], r3
        vst1.8          {d4},  [r0,:64], r3
        vst1.8          {d5},  [r0,:64], r3
        vst1.8          {d10}, [r0,:64], r3
        vst1.8          {d11}, [r0,:64], r3

        bx              lr
endfunc
.endm

        h264_qpel_v_lowpass_l2 put
        h264_qpel_v_lowpass_l2 avg

function put_h264_qpel8_hv_lowpass_neon_top
        lowpass_const   r12
        mov             r12, #12
1:      vld1.8          {d0, d1},  [r1], r3
        vld1.8          {d16,d17}, [r1], r3
        subs            r12, r12, #2
        lowpass_8       d0,  d1,  d16, d17, q11, q12, narrow=0
        vst1.8          {d22-d25}, [r4,:128]!
        bne             1b

        vld1.8          {d0, d1},  [r1]
        lowpass_8_1     d0,  d1,  q12, narrow=0

        mov             r12, #-16
        add             r4,  r4,  r12
        vld1.8          {d30,d31}, [r4,:128], r12
        vld1.8          {d20,d21}, [r4,:128], r12
        vld1.8          {d18,d19}, [r4,:128], r12
        vld1.8          {d16,d17}, [r4,:128], r12
        vld1.8          {d14,d15}, [r4,:128], r12
        vld1.8          {d12,d13}, [r4,:128], r12
        vld1.8          {d10,d11}, [r4,:128], r12
        vld1.8          {d8, d9},  [r4,:128], r12
        vld1.8          {d6, d7},  [r4,:128], r12
        vld1.8          {d4, d5},  [r4,:128], r12
        vld1.8          {d2, d3},  [r4,:128], r12
        vld1.8          {d0, d1},  [r4,:128]

        swap4           d1,  d3,  d5,  d7,  d8,  d10, d12, d14
        transpose16_4x4 q0,  q1,  q2,  q3,  q4,  q5,  q6,  q7

        swap4           d17, d19, d21, d31, d24, d26, d28, d22
        transpose16_4x4 q8,  q9,  q10, q15, q12, q13, q14, q11

        vst1.8          {d30,d31}, [r4,:128]!
        vst1.8          {d6, d7},  [r4,:128]!
        vst1.8          {d20,d21}, [r4,:128]!
        vst1.8          {d4, d5},  [r4,:128]!
        vst1.8          {d18,d19}, [r4,:128]!
        vst1.8          {d2, d3},  [r4,:128]!
        vst1.8          {d16,d17}, [r4,:128]!
        vst1.8          {d0, d1},  [r4,:128]

        lowpass_8.16    q4,  q12, d8,  d9,  d24, d25, d8
        lowpass_8.16    q5,  q13, d10, d11, d26, d27, d9
        lowpass_8.16    q6,  q14, d12, d13, d28, d29, d10
        lowpass_8.16    q7,  q11, d14, d15, d22, d23, d11

        vld1.8          {d16,d17}, [r4,:128], r12
        vld1.8          {d30,d31}, [r4,:128], r12
        lowpass_8.16    q8,  q15, d16, d17, d30, d31, d12
        vld1.8          {d16,d17}, [r4,:128], r12
        vld1.8          {d30,d31}, [r4,:128], r12
        lowpass_8.16    q8,  q15, d16, d17, d30, d31, d13
        vld1.8          {d16,d17}, [r4,:128], r12
        vld1.8          {d30,d31}, [r4,:128], r12
        lowpass_8.16    q8,  q15, d16, d17, d30, d31, d14
        vld1.8          {d16,d17}, [r4,:128], r12
        vld1.8          {d30,d31}, [r4,:128]
        lowpass_8.16    q8,  q15, d16, d17, d30, d31, d15

        transpose_8x8   d12, d13, d14, d15, d8,  d9,  d10, d11

        bx              lr
endfunc

.macro  h264_qpel8_hv_lowpass type
function \type\()_h264_qpel8_hv_lowpass_neon
        mov             r10, lr
        bl              put_h264_qpel8_hv_lowpass_neon_top
  .ifc \type,avg
        vld1.8          {d0},      [r0,:64], r2
        vld1.8          {d1},      [r0,:64], r2
        vld1.8          {d2},      [r0,:64], r2
        vld1.8          {d3},      [r0,:64], r2
        vrhadd.u8       q6,  q6,  q0
        vld1.8          {d4},      [r0,:64], r2
        vld1.8          {d5},      [r0,:64], r2
        vrhadd.u8       q7,  q7,  q1
        vld1.8          {d6},      [r0,:64], r2
        vld1.8          {d7},      [r0,:64], r2
        vrhadd.u8       q4,  q4,  q2
        vrhadd.u8       q5,  q5,  q3
        sub             r0,  r0,  r2,  lsl #3
  .endif

        vst1.8          {d12},     [r0,:64], r2
        vst1.8          {d13},     [r0,:64], r2
        vst1.8          {d14},     [r0,:64], r2
        vst1.8          {d15},     [r0,:64], r2
        vst1.8          {d8},      [r0,:64], r2
        vst1.8          {d9},      [r0,:64], r2
        vst1.8          {d10},     [r0,:64], r2
        vst1.8          {d11},     [r0,:64], r2

        mov             lr,  r10
        bx              lr
endfunc
.endm

        h264_qpel8_hv_lowpass put
        h264_qpel8_hv_lowpass avg

.macro  h264_qpel8_hv_lowpass_l2 type
function \type\()_h264_qpel8_hv_lowpass_l2_neon
        mov             r10, lr
        bl              put_h264_qpel8_hv_lowpass_neon_top

        vld1.8          {d0, d1},  [r2,:128]!
        vld1.8          {d2, d3},  [r2,:128]!
        vrhadd.u8       q0,  q0,  q6
        vld1.8          {d4, d5},  [r2,:128]!
        vrhadd.u8       q1,  q1,  q7
        vld1.8          {d6, d7},  [r2,:128]!
        vrhadd.u8       q2,  q2,  q4
        vrhadd.u8       q3,  q3,  q5
  .ifc \type,avg
        vld1.8          {d16},     [r0,:64], r3
        vld1.8          {d17},     [r0,:64], r3
        vld1.8          {d18},     [r0,:64], r3
        vld1.8          {d19},     [r0,:64], r3
        vrhadd.u8       q0,  q0,  q8
        vld1.8          {d20},     [r0,:64], r3
        vld1.8          {d21},     [r0,:64], r3
        vrhadd.u8       q1,  q1,  q9
        vld1.8          {d22},     [r0,:64], r3
        vld1.8          {d23},     [r0,:64], r3
        vrhadd.u8       q2,  q2,  q10
        vrhadd.u8       q3,  q3,  q11
        sub             r0,  r0,  r3,  lsl #3
  .endif
        vst1.8          {d0},      [r0,:64], r3
        vst1.8          {d1},      [r0,:64], r3
        vst1.8          {d2},      [r0,:64], r3
        vst1.8          {d3},      [r0,:64], r3
        vst1.8          {d4},      [r0,:64], r3
        vst1.8          {d5},      [r0,:64], r3
        vst1.8          {d6},      [r0,:64], r3
        vst1.8          {d7},      [r0,:64], r3

        mov             lr,  r10
        bx              lr
endfunc
.endm

        h264_qpel8_hv_lowpass_l2 put
        h264_qpel8_hv_lowpass_l2 avg

.macro  h264_qpel16_hv  type
function \type\()_h264_qpel16_hv_lowpass_neon
        mov             r9,  lr
        bl              \type\()_h264_qpel8_hv_lowpass_neon
        sub             r1,  r1,  r3, lsl #2
        bl              \type\()_h264_qpel8_hv_lowpass_neon
        sub             r1,  r1,  r3, lsl #4
        sub             r1,  r1,  r3, lsl #2
        add             r1,  r1,  #8
        sub             r0,  r0,  r2, lsl #4
        add             r0,  r0,  #8
        bl              \type\()_h264_qpel8_hv_lowpass_neon
        sub             r1,  r1,  r3, lsl #2
        mov             lr,  r9
        b               \type\()_h264_qpel8_hv_lowpass_neon
endfunc

function \type\()_h264_qpel16_hv_lowpass_l2_neon
        mov             r9,  lr
        sub             r2,  r4,  #256
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        sub             r1,  r1,  r3, lsl #2
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        sub             r1,  r1,  r3, lsl #4
        sub             r1,  r1,  r3, lsl #2
        add             r1,  r1,  #8
        sub             r0,  r0,  r3, lsl #4
        add             r0,  r0,  #8
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        sub             r1,  r1,  r3, lsl #2
        mov             lr,  r9
        b               \type\()_h264_qpel8_hv_lowpass_l2_neon
endfunc
.endm

        h264_qpel16_hv put
        h264_qpel16_hv avg

.macro  h264_qpel8      type
function ff_\type\()_h264_qpel8_mc10_neon, export=1
        lowpass_const   r3
        mov             r3,  r1
        sub             r1,  r1,  #2
        mov             r12, #8
        b               \type\()_h264_qpel8_h_lowpass_l2_neon
endfunc

function ff_\type\()_h264_qpel8_mc20_neon, export=1
        lowpass_const   r3
        sub             r1,  r1,  #2
        mov             r3,  r2
        mov             r12, #8
        b               \type\()_h264_qpel8_h_lowpass_neon
endfunc

function ff_\type\()_h264_qpel8_mc30_neon, export=1
        lowpass_const   r3
        add             r3,  r1,  #1
        sub             r1,  r1,  #2
        mov             r12, #8
        b               \type\()_h264_qpel8_h_lowpass_l2_neon
endfunc

function ff_\type\()_h264_qpel8_mc01_neon, export=1
        push            {lr}
        mov             r12, r1
\type\()_h264_qpel8_mc01:
        lowpass_const   r3
        mov             r3,  r2
        sub             r1,  r1,  r2, lsl #1
        vpush           {d8-d15}
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        vpop            {d8-d15}
        pop             {pc}
endfunc

function ff_\type\()_h264_qpel8_mc11_neon, export=1
        push            {r0, r1, r11, lr}
\type\()_h264_qpel8_mc11:
        lowpass_const   r3
        mov             r11, sp
A       bic             sp,  sp,  #15
T       bic             r0,  r11, #15
T       mov             sp,  r0
        sub             sp,  sp,  #64
        mov             r0,  sp
        sub             r1,  r1,  #2
        mov             r3,  #8
        mov             r12, #8
        vpush           {d8-d15}
        bl              put_h264_qpel8_h_lowpass_neon
        ldrd            r0,  r1,  [r11], #8
        mov             r3,  r2
        add             r12, sp,  #64
        sub             r1,  r1,  r2, lsl #1
        mov             r2,  #8
        bl              \type\()_h264_qpel8_v_lowpass_l2_neon
        vpop            {d8-d15}
        mov             sp,  r11
        pop             {r11, pc}
endfunc

function ff_\type\()_h264_qpel8_mc21_neon, export=1
        push            {r0, r1, r4, r10, r11, lr}
\type\()_h264_qpel8_mc21:
        lowpass_const   r3
        mov             r11, sp
A       bic             sp,  sp,  #15
T       bic             r0,  r11, #15
T       mov             sp,  r0
        sub             sp,  sp,  #(8*8+16*12)
        sub             r1,  r1,  #2
        mov             r3,  #8
        mov             r0,  sp
        mov             r12, #8
        vpush           {d8-d15}
        bl              put_h264_qpel8_h_lowpass_neon
        mov             r4,  r0
        ldrd            r0,  r1,  [r11], #8
        sub             r1,  r1,  r2, lsl #1
        sub             r1,  r1,  #2
        mov             r3,  r2
        sub             r2,  r4,  #64
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        vpop            {d8-d15}
        mov             sp,  r11
        pop             {r4, r10, r11, pc}
endfunc

function ff_\type\()_h264_qpel8_mc31_neon, export=1
        add             r1,  r1,  #1
        push            {r0, r1, r11, lr}
        sub             r1,  r1,  #1
        b               \type\()_h264_qpel8_mc11
endfunc

function ff_\type\()_h264_qpel8_mc02_neon, export=1
        push            {lr}
        lowpass_const   r3
        sub             r1,  r1,  r2, lsl #1
        mov             r3,  r2
        vpush           {d8-d15}
        bl              \type\()_h264_qpel8_v_lowpass_neon
        vpop            {d8-d15}
        pop             {pc}
endfunc

function ff_\type\()_h264_qpel8_mc12_neon, export=1
        push            {r0, r1, r4, r10, r11, lr}
\type\()_h264_qpel8_mc12:
        lowpass_const   r3
        mov             r11, sp
A       bic             sp,  sp,  #15
T       bic             r0,  r11, #15
T       mov             sp,  r0
        sub             sp,  sp,  #(8*8+16*12)
        sub             r1,  r1,  r2, lsl #1
        mov             r3,  r2
        mov             r2,  #8
        mov             r0,  sp
        vpush           {d8-d15}
        bl              put_h264_qpel8_v_lowpass_neon
        mov             r4,  r0
        ldrd            r0,  r1,  [r11], #8
        sub             r1,  r1,  r3, lsl #1
        sub             r1,  r1,  #2
        sub             r2,  r4,  #64
        bl              \type\()_h264_qpel8_hv_lowpass_l2_neon
        vpop            {d8-d15}
        mov             sp,  r11
        pop             {r4, r10, r11, pc}
endfunc

function ff_\type\()_h264_qpel8_mc22_neon, export=1
        push            {r4, r10, r11, lr}
        mov             r11, sp
A       bic             sp,  sp,  #15
T       bic             r4,  r11, #15
T       mov             sp,  r4
        sub             r1,  r1,  r2, lsl #1
        sub             r1,  r1,  #2
        mov             r3,  r2
        sub             sp,  sp,  #(16*12)
        mov             r4,  sp
        vpush           {d8-d15}
        bl              \type\()_h264_qpel8_hv_lowpass_neon
        vpop            {d8-d15}
        mov             sp,  r11
        pop             {r4, r10, r11, pc}
endfunc

function ff_\type\()_h264_qpel8_mc32_neon, export=1
        push            {r0, r1, r4, r10, r11, lr}
        add             r1,  r1,  #1
        b               \type\()_h264_qpel8_mc12
endfunc

function ff_\type\()_h264_qpel8_mc03_neon, export=1
        push            {lr}
        add             r12, r1,  r2
        b               \type\()_h264_qpel8_mc01
endfunc

function ff_\type\()_h264_qpel8_mc13_neon, export=1
        push            {r0, r1, r11, lr}
        add             r1,  r1,  r2
        b               \type\()_h264_qpel8_mc11
endfunc

function ff_\type\()_h264_qpel8_mc23_neon, export=1
        push            {r0, r1, r4, r10, r11, lr}
        add             r1,  r1,  r2
        b               \type\()_h264_qpel8_mc21
endfunc

function ff_\type\()_h264_qpel8_mc33_neon, export=1
        add             r1,  r1,  #1
        push            {r0, r1, r11, lr}
        add             r1,  r1,  r2
        sub             r1,  r1,  #1
        b               \type\()_h264_qpel8_mc11
endfunc
.endm

        h264_qpel8 put
        h264_qpel8 avg

.macro  h264_qpel16     type
function ff_\type\()_h264_qpel16_mc10_neon, export=1
        lowpass_const   r3
        mov             r3,  r1
        sub             r1,  r1,  #2
        b               \type\()_h264_qpel16_h_lowpass_l2_neon
endfunc

function ff_\type\()_h264_qpel16_mc20_neon, export=1
        lowpass_const   r3
        sub             r1,  r1,  #2
        mov             r3,  r2
        b               \type\()_h264_qpel16_h_lowpass_neon
endfunc

function ff_\type\()_h264_qpel16_mc30_neon, export=1
        lowpass_const   r3
        add             r3,  r1,  #1
        sub             r1,  r1,  #2
        b               \type\()_h264_qpel16_h_lowpass_l2_neon
endfunc

function ff_\type\()_h264_qpel16_mc01_neon, export=1
        push            {r4, lr}
        mov             r12, r1
\type\()_h264_qpel16_mc01:
        lowpass_const   r3
        mov             r3,  r2
        sub             r1,  r1,  r2, lsl #1
        vpush           {d8-d15}
        bl              \type\()_h264_qpel16_v_lowpass_l2_neon
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_h264_qpel16_mc11_neon, export=1
        push            {r0, r1, r4, r11, lr}
\type\()_h264_qpel16_mc11:
        lowpass_const   r3
        mov             r11, sp
A       bic             sp,  sp,  #15
T       bic             r0,  r11, #15
T       mov             sp,  r0
        sub             sp,  sp,  #256
        mov             r0,  sp
        sub             r1,  r1,  #2
        mov             r3,  #16
        vpush           {d8-d15}
        bl              put_h264_qpel16_h_lowpass_neon
        ldrd            r0,  r1,  [r11], #8
        mov             r3,  r2
        add             r12, sp,  #64
        sub             r1,  r1,  r2, lsl #1
        mov             r2,  #16
        bl              \type\()_h264_qpel16_v_lowpass_l2_neon
        vpop            {d8-d15}
        mov             sp,  r11
        pop             {r4, r11, pc}
endfunc

function ff_\type\()_h264_qpel16_mc21_neon, export=1
        push            {r0, r1, r4-r5, r9-r11, lr}
\type\()_h264_qpel16_mc21:
        lowpass_const   r3
        mov             r11, sp
A       bic             sp,  sp,  #15
T       bic             r0,  r11, #15
T       mov             sp,  r0
        sub             sp,  sp,  #(16*16+16*12)
        sub             r1,  r1,  #2
        mov             r0,  sp
        vpush           {d8-d15}
        bl              put_h264_qpel16_h_lowpass_neon_packed
        mov             r4,  r0
        ldrd            r0,  r1,  [r11], #8
        sub             r1,  r1,  r2, lsl #1
        sub             r1,  r1,  #2
        mov             r3,  r2
        bl              \type\()_h264_qpel16_hv_lowpass_l2_neon
        vpop            {d8-d15}
        mov             sp,  r11
        pop             {r4-r5, r9-r11, pc}
endfunc

function ff_\type\()_h264_qpel16_mc31_neon, export=1
        add             r1,  r1,  #1
        push            {r0, r1, r4, r11, lr}
        sub             r1,  r1,  #1
        b               \type\()_h264_qpel16_mc11
endfunc

function ff_\type\()_h264_qpel16_mc02_neon, export=1
        push            {r4, lr}
        lowpass_const   r3
        sub             r1,  r1,  r2, lsl #1
        mov             r3,  r2
        vpush           {d8-d15}
        bl              \type\()_h264_qpel16_v_lowpass_neon
        vpop            {d8-d15}
        pop             {r4, pc}
endfunc

function ff_\type\()_h264_qpel16_mc12_neon, export=1
        push            {r0, r1, r4-r5, r9-r11, lr}
\type\()_h264_qpel16_mc12:
        lowpass_const   r3
        mov             r11, sp
A       bic             sp,  sp,  #15
T       bic             r0,  r11, #15
T       mov             sp,  r0
        sub             sp,  sp,  #(16*16+16*12)
        sub             r1,  r1,  r2, lsl #1
        mov             r0,  sp
        mov             r3,  r2
        vpush           {d8-d15}
        bl              put_h264_qpel16_v_lowpass_neon_packed
        mov             r4,  r0
        ldrd            r0,  r1,  [r11], #8
        sub             r1,  r1,  r3, lsl #1
        sub             r1,  r1,  #2
        mov             r2,  r3
        bl              \type\()_h264_qpel16_hv_lowpass_l2_neon
        vpop            {d8-d15}
        mov             sp,  r11
        pop             {r4-r5, r9-r11, pc}
endfunc

function ff_\type\()_h264_qpel16_mc22_neon, export=1
        push            {r4, r9-r11, lr}
        lowpass_const   r3
        mov             r11, sp
A       bic             sp,  sp,  #15
T       bic             r4,  r11, #15
T       mov             sp,  r4
        sub             r1,  r1,  r2, lsl #1
        sub             r1,  r1,  #2
        mov             r3,  r2
        sub             sp,  sp,  #(16*12)
        mov             r4,  sp
        vpush           {d8-d15}
        bl              \type\()_h264_qpel16_hv_lowpass_neon
        vpop            {d8-d15}
        mov             sp,  r11
        pop             {r4, r9-r11, pc}
endfunc

function ff_\type\()_h264_qpel16_mc32_neon, export=1
        push            {r0, r1, r4-r5, r9-r11, lr}
        add             r1,  r1,  #1
        b               \type\()_h264_qpel16_mc12
endfunc

function ff_\type\()_h264_qpel16_mc03_neon, export=1
        push            {r4, lr}
        add             r12, r1,  r2
        b               \type\()_h264_qpel16_mc01
endfunc

function ff_\type\()_h264_qpel16_mc13_neon, export=1
        push            {r0, r1, r4, r11, lr}
        add             r1,  r1,  r2
        b               \type\()_h264_qpel16_mc11
endfunc

function ff_\type\()_h264_qpel16_mc23_neon, export=1
        push            {r0, r1, r4-r5, r9-r11, lr}
        add             r1,  r1,  r2
        b               \type\()_h264_qpel16_mc21
endfunc

function ff_\type\()_h264_qpel16_mc33_neon, export=1
        add             r1,  r1,  #1
        push            {r0, r1, r4, r11, lr}
        add             r1,  r1,  r2
        sub             r1,  r1,  #1
        b               \type\()_h264_qpel16_mc11
endfunc
.endm

        h264_qpel16 put
        h264_qpel16 avg
