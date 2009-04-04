/*
 * ARM NEON optimised DSP functions
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

#include "asm.S"

        preserve8
        .fpu neon
        .text

        .macro pixels16 avg=0
.if \avg
        mov             ip,  r0
.endif
1:      vld1.64         {d0, d1},  [r1], r2
        vld1.64         {d2, d3},  [r1], r2
        vld1.64         {d4, d5},  [r1], r2
        pld             [r1, r2, lsl #2]
        vld1.64         {d6, d7},  [r1], r2
        pld             [r1]
        pld             [r1, r2]
        pld             [r1, r2, lsl #1]
.if \avg
        vld1.64         {d16,d17}, [ip], r2
        vrhadd.u8       q0,  q0,  q8
        vld1.64         {d18,d19}, [ip], r2
        vrhadd.u8       q1,  q1,  q9
        vld1.64         {d20,d21}, [ip], r2
        vrhadd.u8       q2,  q2,  q10
        vld1.64         {d22,d23}, [ip], r2
        vrhadd.u8       q3,  q3,  q11
.endif
        subs            r3,  r3,  #4
        vst1.64         {d0, d1},  [r0,:128], r2
        vst1.64         {d2, d3},  [r0,:128], r2
        vst1.64         {d4, d5},  [r0,:128], r2
        vst1.64         {d6, d7},  [r0,:128], r2
        bne             1b
        bx              lr
        .endm

        .macro pixels16_x2 vhadd=vrhadd.u8
1:      vld1.64         {d0-d2},   [r1], r2
        vld1.64         {d4-d6},   [r1], r2
        pld             [r1]
        pld             [r1, r2]
        subs            r3,  r3,  #2
        vext.8          q1,  q0,  q1,  #1
        \vhadd          q0,  q0,  q1
        vext.8          q3,  q2,  q3,  #1
        \vhadd          q2,  q2,  q3
        vst1.64         {d0, d1},  [r0,:128], r2
        vst1.64         {d4, d5},  [r0,:128], r2
        bne             1b
        bx              lr
        .endm

        .macro pixels16_y2 vhadd=vrhadd.u8
        push            {lr}
        add             ip,  r1,  r2
        lsl             lr,  r2,  #1
        vld1.64         {d0, d1},  [r1], lr
        vld1.64         {d2, d3},  [ip], lr
1:      subs            r3,  r3,  #2
        \vhadd          q2,  q0,  q1
        vld1.64         {d0, d1},  [r1],      lr
        \vhadd          q3,  q0,  q1
        vld1.64         {d2, d3},  [ip],      lr
        pld             [r1]
        pld             [ip]
        vst1.64         {d4, d5},  [r0,:128], r2
        vst1.64         {d6, d7},  [r0,:128], r2
        bne             1b
        pop             {pc}
        .endm

        .macro pixels16_xy2 vshrn=vrshrn.u16 no_rnd=0
        push            {lr}
        lsl             lr,  r2,  #1
        add             ip,  r1,  r2
        vld1.64         {d0-d2},   [r1], lr
        vld1.64         {d4-d6},   [ip], lr
.if \no_rnd
        vmov.i16        q13, #1
.endif
        pld             [r1]
        pld             [ip]
        vext.8          q1,  q0,  q1,  #1
        vext.8          q3,  q2,  q3,  #1
        vaddl.u8        q8,  d0,  d2
        vaddl.u8        q10, d1,  d3
        vaddl.u8        q9,  d4,  d6
        vaddl.u8        q11, d5,  d7
1:      subs            r3,  r3,  #2
        vld1.64         {d0-d2},   [r1], lr
        vadd.u16        q12, q8,  q9
        pld             [r1]
.if \no_rnd
        vadd.u16        q12, q12, q13
.endif
        vext.8          q15, q0,  q1,  #1
        vadd.u16        q1 , q10, q11
        \vshrn          d28, q12, #2
.if \no_rnd
        vadd.u16        q1,  q1,  q13
.endif
        \vshrn          d29, q1,  #2
        vaddl.u8        q8,  d0,  d30
        vld1.64         {d2-d4},   [ip], lr
        vaddl.u8        q10, d1,  d31
        vst1.64         {d28,d29}, [r0,:128], r2
        vadd.u16        q12, q8,  q9
        pld             [ip]
.if \no_rnd
        vadd.u16        q12, q12, q13
.endif
        vext.8          q2,  q1,  q2,  #1
        vadd.u16        q0,  q10, q11
        \vshrn          d30, q12, #2
.if \no_rnd
        vadd.u16        q0,  q0,  q13
.endif
        \vshrn          d31, q0,  #2
        vaddl.u8        q9,  d2,  d4
        vaddl.u8        q11, d3,  d5
        vst1.64         {d30,d31}, [r0,:128], r2
        bgt             1b
        pop             {pc}
        .endm

        .macro pixels8
1:      vld1.64         {d0}, [r1], r2
        vld1.64         {d1}, [r1], r2
        vld1.64         {d2}, [r1], r2
        pld             [r1, r2, lsl #2]
        vld1.64         {d3}, [r1], r2
        pld             [r1]
        pld             [r1, r2]
        pld             [r1, r2, lsl #1]
        subs            r3,  r3,  #4
        vst1.64         {d0}, [r0,:64], r2
        vst1.64         {d1}, [r0,:64], r2
        vst1.64         {d2}, [r0,:64], r2
        vst1.64         {d3}, [r0,:64], r2
        bne             1b
        bx              lr
        .endm

        .macro pixels8_x2 vhadd=vrhadd.u8
1:      vld1.64         {d0, d1},  [r1], r2
        vext.8          d1,  d0,  d1,  #1
        vld1.64         {d2, d3},  [r1], r2
        vext.8          d3,  d2,  d3,  #1
        pld             [r1]
        pld             [r1, r2]
        subs            r3,  r3,  #2
        vswp            d1,  d2
        \vhadd          q0,  q0,  q1
        vst1.64         {d0},      [r0,:64], r2
        vst1.64         {d1},      [r0,:64], r2
        bne             1b
        bx              lr
        .endm

        .macro pixels8_y2 vhadd=vrhadd.u8
        push            {lr}
        add             ip,  r1,  r2
        lsl             lr,  r2,  #1
        vld1.64         {d0},      [r1], lr
        vld1.64         {d1},      [ip], lr
1:      subs            r3,  r3,  #2
        \vhadd          d4,  d0,  d1
        vld1.64         {d0},      [r1],     lr
        \vhadd          d5,  d0,  d1
        vld1.64         {d1},      [ip],     lr
        pld             [r1]
        pld             [ip]
        vst1.64         {d4},      [r0,:64], r2
        vst1.64         {d5},      [r0,:64], r2
        bne             1b
        pop             {pc}
        .endm

        .macro pixels8_xy2 vshrn=vrshrn.u16 no_rnd=0
        push            {lr}
        lsl             lr,  r2,  #1
        add             ip,  r1,  r2
        vld1.64         {d0, d1},  [r1], lr
        vld1.64         {d2, d3},  [ip], lr
.if \no_rnd
        vmov.i16        q11, #1
.endif
        pld             [r1]
        pld             [ip]
        vext.8          d4,  d0,  d1,  #1
        vext.8          d6,  d2,  d3,  #1
        vaddl.u8        q8,  d0,  d4
        vaddl.u8        q9,  d2,  d6
1:      subs            r3,  r3,  #2
        vld1.64         {d0, d1},  [r1], lr
        pld             [r1]
        vadd.u16        q10, q8,  q9
        vext.8          d4,  d0,  d1,  #1
.if \no_rnd
        vadd.u16        q10, q10, q11
.endif
        vaddl.u8        q8,  d0,  d4
        \vshrn          d5,  q10, #2
        vld1.64         {d2, d3},  [ip], lr
        vadd.u16        q10, q8,  q9
        pld             [ip]
.if \no_rnd
        vadd.u16        q10, q10, q11
.endif
        vst1.64         {d5},      [r0,:64], r2
        \vshrn          d7,  q10, #2
        vext.8          d6,  d2,  d3,  #1
        vaddl.u8        q9,  d2,  d6
        vst1.64         {d7},      [r0,:64], r2
        bgt             1b
        pop             {pc}
        .endm

        .macro pixfunc pfx name suf rnd_op args:vararg
function ff_\pfx\name\suf\()_neon, export=1
        \name \rnd_op \args
        .endfunc
        .endm

        .macro pixfunc2 pfx name args:vararg
        pixfunc \pfx \name
        pixfunc \pfx \name \args
        .endm

function ff_put_h264_qpel16_mc00_neon, export=1
        mov   r3, #16
        .endfunc

        pixfunc  put_ pixels16
        pixfunc2 put_ pixels16_x2,  _no_rnd, vhadd.u8
        pixfunc2 put_ pixels16_y2,  _no_rnd, vhadd.u8
        pixfunc2 put_ pixels16_xy2, _no_rnd, vshrn.u16, 1

function ff_avg_h264_qpel16_mc00_neon, export=1
        mov   r3, #16
        .endfunc

        pixfunc  avg_ pixels16,, 1

function ff_put_h264_qpel8_mc00_neon, export=1
        mov   r3, #8
        .endfunc

        pixfunc  put_ pixels8
        pixfunc2 put_ pixels8_x2,   _no_rnd, vhadd.u8
        pixfunc2 put_ pixels8_y2,   _no_rnd, vhadd.u8
        pixfunc2 put_ pixels8_xy2,  _no_rnd, vshrn.u16, 1

function ff_put_signed_pixels_clamped_neon, export=1
        vmov.u8         d31, #128
        vld1.64         {d16-d17}, [r0,:128]!
        vqmovn.s16      d0, q8
        vld1.64         {d18-d19}, [r0,:128]!
        vqmovn.s16      d1, q9
        vld1.64         {d16-d17}, [r0,:128]!
        vqmovn.s16      d2, q8
        vld1.64         {d18-d19}, [r0,:128]!
        vadd.u8         d0, d0, d31
        vld1.64         {d20-d21}, [r0,:128]!
        vadd.u8         d1, d1, d31
        vld1.64         {d22-d23}, [r0,:128]!
        vadd.u8         d2, d2, d31
        vst1.64         {d0},      [r1,:64], r2
        vqmovn.s16      d3, q9
        vst1.64         {d1},      [r1,:64], r2
        vqmovn.s16      d4, q10
        vst1.64         {d2},      [r1,:64], r2
        vqmovn.s16      d5, q11
        vld1.64         {d24-d25}, [r0,:128]!
        vadd.u8         d3, d3, d31
        vld1.64         {d26-d27}, [r0,:128]!
        vadd.u8         d4, d4, d31
        vadd.u8         d5, d5, d31
        vst1.64         {d3},      [r1,:64], r2
        vqmovn.s16      d6, q12
        vst1.64         {d4},      [r1,:64], r2
        vqmovn.s16      d7, q13
        vst1.64         {d5},      [r1,:64], r2
        vadd.u8         d6, d6, d31
        vadd.u8         d7, d7, d31
        vst1.64         {d6},      [r1,:64], r2
        vst1.64         {d7},      [r1,:64], r2
        bx              lr
        .endfunc

function ff_add_pixels_clamped_neon, export=1
        mov             r3, r1
        vld1.64         {d16},   [r1,:64], r2
        vld1.64         {d0-d1}, [r0,:128]!
        vaddw.u8        q0, q0, d16
        vld1.64         {d17},   [r1,:64], r2
        vld1.64         {d2-d3}, [r0,:128]!
        vqmovun.s16     d0, q0
        vld1.64         {d18},   [r1,:64], r2
        vaddw.u8        q1, q1, d17
        vld1.64         {d4-d5}, [r0,:128]!
        vaddw.u8        q2, q2, d18
        vst1.64         {d0},    [r3,:64], r2
        vqmovun.s16     d2, q1
        vld1.64         {d19},   [r1,:64], r2
        vld1.64         {d6-d7}, [r0,:128]!
        vaddw.u8        q3, q3, d19
        vqmovun.s16     d4, q2
        vst1.64         {d2},    [r3,:64], r2
        vld1.64         {d16},   [r1,:64], r2
        vqmovun.s16     d6, q3
        vld1.64         {d0-d1}, [r0,:128]!
        vaddw.u8        q0, q0, d16
        vst1.64         {d4},    [r3,:64], r2
        vld1.64         {d17},   [r1,:64], r2
        vld1.64         {d2-d3}, [r0,:128]!
        vaddw.u8        q1, q1, d17
        vst1.64         {d6},    [r3,:64], r2
        vqmovun.s16     d0, q0
        vld1.64         {d18},   [r1,:64], r2
        vld1.64         {d4-d5}, [r0,:128]!
        vaddw.u8        q2, q2, d18
        vst1.64         {d0},    [r3,:64], r2
        vqmovun.s16     d2, q1
        vld1.64         {d19},   [r1,:64], r2
        vqmovun.s16     d4, q2
        vld1.64         {d6-d7}, [r0,:128]!
        vaddw.u8        q3, q3, d19
        vst1.64         {d2},    [r3,:64], r2
        vqmovun.s16     d6, q3
        vst1.64         {d4},    [r3,:64], r2
        vst1.64         {d6},    [r3,:64], r2
        bx              lr
        .endfunc

function ff_float_to_int16_neon, export=1
        subs            r2,  r2,  #8
        vld1.64         {d0-d1},  [r1,:128]!
        vcvt.s32.f32    q8,  q0,  #16
        vld1.64         {d2-d3},  [r1,:128]!
        vcvt.s32.f32    q9,  q1,  #16
        beq             3f
        bics            ip,  r2,  #15
        beq             2f
1:      subs            ip,  ip,  #16
        vshrn.s32       d4,  q8,  #16
        vld1.64         {d0-d1},  [r1,:128]!
        vcvt.s32.f32    q0,  q0,  #16
        vshrn.s32       d5,  q9,  #16
        vld1.64         {d2-d3},  [r1,:128]!
        vcvt.s32.f32    q1,  q1,  #16
        vshrn.s32       d6,  q0,  #16
        vst1.64         {d4-d5},  [r0,:128]!
        vshrn.s32       d7,  q1,  #16
        vld1.64         {d16-d17},[r1,:128]!
        vcvt.s32.f32    q8,  q8,  #16
        vld1.64         {d18-d19},[r1,:128]!
        vcvt.s32.f32    q9,  q9,  #16
        vst1.64         {d6-d7},  [r0,:128]!
        bne             1b
        ands            r2,  r2,  #15
        beq             3f
2:      vld1.64         {d0-d1},  [r1,:128]!
        vshrn.s32       d4,  q8,  #16
        vcvt.s32.f32    q0,  q0,  #16
        vld1.64         {d2-d3},  [r1,:128]!
        vshrn.s32       d5,  q9,  #16
        vcvt.s32.f32    q1,  q1,  #16
        vshrn.s32       d6,  q0,  #16
        vst1.64         {d4-d5},  [r0,:128]!
        vshrn.s32       d7,  q1,  #16
        vst1.64         {d6-d7},  [r0,:128]!
        bx              lr
3:      vshrn.s32       d4,  q8,  #16
        vshrn.s32       d5,  q9,  #16
        vst1.64         {d4-d5},  [r0,:128]!
        bx              lr
        .endfunc

function ff_float_to_int16_interleave_neon, export=1
        cmp             r3, #2
        ldrlt           r1, [r1]
        blt             ff_float_to_int16_neon
        bne             4f

        ldr             r3, [r1]
        ldr             r1, [r1, #4]

        subs            r2,  r2,  #8
        vld1.64         {d0-d1},  [r3,:128]!
        vcvt.s32.f32    q8,  q0,  #16
        vld1.64         {d2-d3},  [r3,:128]!
        vcvt.s32.f32    q9,  q1,  #16
        vld1.64         {d20-d21},[r1,:128]!
        vcvt.s32.f32    q10, q10, #16
        vld1.64         {d22-d23},[r1,:128]!
        vcvt.s32.f32    q11, q11, #16
        beq             3f
        bics            ip,  r2,  #15
        beq             2f
1:      subs            ip,  ip,  #16
        vld1.64         {d0-d1},  [r3,:128]!
        vcvt.s32.f32    q0,  q0,  #16
        vsri.32         q10, q8,  #16
        vld1.64         {d2-d3},  [r3,:128]!
        vcvt.s32.f32    q1,  q1,  #16
        vld1.64         {d24-d25},[r1,:128]!
        vcvt.s32.f32    q12, q12, #16
        vld1.64         {d26-d27},[r1,:128]!
        vsri.32         q11, q9,  #16
        vst1.64         {d20-d21},[r0,:128]!
        vcvt.s32.f32    q13, q13, #16
        vst1.64         {d22-d23},[r0,:128]!
        vsri.32         q12, q0,  #16
        vld1.64         {d16-d17},[r3,:128]!
        vsri.32         q13, q1,  #16
        vst1.64         {d24-d25},[r0,:128]!
        vcvt.s32.f32    q8,  q8,  #16
        vld1.64         {d18-d19},[r3,:128]!
        vcvt.s32.f32    q9,  q9,  #16
        vld1.64         {d20-d21},[r1,:128]!
        vcvt.s32.f32    q10, q10, #16
        vld1.64         {d22-d23},[r1,:128]!
        vcvt.s32.f32    q11, q11, #16
        vst1.64         {d26-d27},[r0,:128]!
        bne             1b
        ands            r2,  r2,  #15
        beq             3f
2:      vsri.32         q10, q8,  #16
        vld1.64         {d0-d1},  [r3,:128]!
        vcvt.s32.f32    q0,  q0,  #16
        vld1.64         {d2-d3},  [r3,:128]!
        vcvt.s32.f32    q1,  q1,  #16
        vld1.64         {d24-d25},[r1,:128]!
        vcvt.s32.f32    q12, q12, #16
        vsri.32         q11, q9,  #16
        vld1.64         {d26-d27},[r1,:128]!
        vcvt.s32.f32    q13, q13, #16
        vst1.64         {d20-d21},[r0,:128]!
        vsri.32         q12, q0,  #16
        vst1.64         {d22-d23},[r0,:128]!
        vsri.32         q13, q1,  #16
        vst1.64         {d24-d27},[r0,:128]!
        bx              lr
3:      vsri.32         q10, q8,  #16
        vsri.32         q11, q9,  #16
        vst1.64         {d20-d23},[r0,:128]!
        bx              lr

4:      push            {r4-r8,lr}
        cmp             r3,  #4
        lsl             ip,  r3,  #1
        blt             4f

        @ 4 channels
5:      ldmia           r1!, {r4-r7}
        mov             lr,  r2
        mov             r8,  r0
        vld1.64         {d16-d17},[r4,:128]!
        vcvt.s32.f32    q8,  q8,  #16
        vld1.64         {d18-d19},[r5,:128]!
        vcvt.s32.f32    q9,  q9,  #16
        vld1.64         {d20-d21},[r6,:128]!
        vcvt.s32.f32    q10, q10, #16
        vld1.64         {d22-d23},[r7,:128]!
        vcvt.s32.f32    q11, q11, #16
6:      subs            lr,  lr,  #8
        vld1.64         {d0-d1},  [r4,:128]!
        vcvt.s32.f32    q0,  q0,  #16
        vsri.32         q9,  q8,  #16
        vld1.64         {d2-d3},  [r5,:128]!
        vcvt.s32.f32    q1,  q1,  #16
        vsri.32         q11, q10, #16
        vld1.64         {d4-d5},  [r6,:128]!
        vcvt.s32.f32    q2,  q2,  #16
        vzip.32         d18, d22
        vld1.64         {d6-d7},  [r7,:128]!
        vcvt.s32.f32    q3,  q3,  #16
        vzip.32         d19, d23
        vst1.64         {d18},    [r8], ip
        vsri.32         q1,  q0,  #16
        vst1.64         {d22},    [r8], ip
        vsri.32         q3,  q2,  #16
        vst1.64         {d19},    [r8], ip
        vzip.32         d2,  d6
        vst1.64         {d23},    [r8], ip
        vzip.32         d3,  d7
        beq             7f
        vld1.64         {d16-d17},[r4,:128]!
        vcvt.s32.f32    q8,  q8,  #16
        vst1.64         {d2},     [r8], ip
        vld1.64         {d18-d19},[r5,:128]!
        vcvt.s32.f32    q9,  q9,  #16
        vst1.64         {d6},     [r8], ip
        vld1.64         {d20-d21},[r6,:128]!
        vcvt.s32.f32    q10, q10, #16
        vst1.64         {d3},     [r8], ip
        vld1.64         {d22-d23},[r7,:128]!
        vcvt.s32.f32    q11, q11, #16
        vst1.64         {d7},     [r8], ip
        b               6b
7:      vst1.64         {d2},     [r8], ip
        vst1.64         {d6},     [r8], ip
        vst1.64         {d3},     [r8], ip
        vst1.64         {d7},     [r8], ip
        subs            r3,  r3,  #4
        popeq           {r4-r8,pc}
        cmp             r3,  #4
        add             r0,  r0,  #8
        bge             5b

        @ 2 channels
4:      cmp             r3,  #2
        blt             4f
        ldmia           r1!, {r4-r5}
        mov             lr,  r2
        mov             r8,  r0
        tst             lr,  #8
        vld1.64         {d16-d17},[r4,:128]!
        vcvt.s32.f32    q8,  q8,  #16
        vld1.64         {d18-d19},[r5,:128]!
        vcvt.s32.f32    q9,  q9,  #16
        vld1.64         {d20-d21},[r4,:128]!
        vcvt.s32.f32    q10, q10, #16
        vld1.64         {d22-d23},[r5,:128]!
        vcvt.s32.f32    q11, q11, #16
        beq             6f
        subs            lr,  lr,  #8
        beq             7f
        vsri.32         d18, d16, #16
        vsri.32         d19, d17, #16
        vld1.64         {d16-d17},[r4,:128]!
        vcvt.s32.f32    q8,  q8,  #16
        vst1.32         {d18[0]}, [r8], ip
        vsri.32         d22, d20, #16
        vst1.32         {d18[1]}, [r8], ip
        vsri.32         d23, d21, #16
        vst1.32         {d19[0]}, [r8], ip
        vst1.32         {d19[1]}, [r8], ip
        vld1.64         {d18-d19},[r5,:128]!
        vcvt.s32.f32    q9,  q9,  #16
        vst1.32         {d22[0]}, [r8], ip
        vst1.32         {d22[1]}, [r8], ip
        vld1.64         {d20-d21},[r4,:128]!
        vcvt.s32.f32    q10, q10, #16
        vst1.32         {d23[0]}, [r8], ip
        vst1.32         {d23[1]}, [r8], ip
        vld1.64         {d22-d23},[r5,:128]!
        vcvt.s32.f32    q11, q11, #16
6:      subs            lr,  lr,  #16
        vld1.64         {d0-d1},  [r4,:128]!
        vcvt.s32.f32    q0,  q0,  #16
        vsri.32         d18, d16, #16
        vld1.64         {d2-d3},  [r5,:128]!
        vcvt.s32.f32    q1,  q1,  #16
        vsri.32         d19, d17, #16
        vld1.64         {d4-d5},  [r4,:128]!
        vcvt.s32.f32    q2,  q2,  #16
        vld1.64         {d6-d7},  [r5,:128]!
        vcvt.s32.f32    q3,  q3,  #16
        vst1.32         {d18[0]}, [r8], ip
        vsri.32         d22, d20, #16
        vst1.32         {d18[1]}, [r8], ip
        vsri.32         d23, d21, #16
        vst1.32         {d19[0]}, [r8], ip
        vsri.32         d2,  d0,  #16
        vst1.32         {d19[1]}, [r8], ip
        vsri.32         d3,  d1,  #16
        vst1.32         {d22[0]}, [r8], ip
        vsri.32         d6,  d4,  #16
        vst1.32         {d22[1]}, [r8], ip
        vsri.32         d7,  d5,  #16
        vst1.32         {d23[0]}, [r8], ip
        vst1.32         {d23[1]}, [r8], ip
        beq             6f
        vld1.64         {d16-d17},[r4,:128]!
        vcvt.s32.f32    q8,  q8,  #16
        vst1.32         {d2[0]},  [r8], ip
        vst1.32         {d2[1]},  [r8], ip
        vld1.64         {d18-d19},[r5,:128]!
        vcvt.s32.f32    q9,  q9,  #16
        vst1.32         {d3[0]},  [r8], ip
        vst1.32         {d3[1]},  [r8], ip
        vld1.64         {d20-d21},[r4,:128]!
        vcvt.s32.f32    q10, q10, #16
        vst1.32         {d6[0]},  [r8], ip
        vst1.32         {d6[1]},  [r8], ip
        vld1.64         {d22-d23},[r5,:128]!
        vcvt.s32.f32    q11, q11, #16
        vst1.32         {d7[0]},  [r8], ip
        vst1.32         {d7[1]},  [r8], ip
        bgt             6b
6:      vst1.32         {d2[0]},  [r8], ip
        vst1.32         {d2[1]},  [r8], ip
        vst1.32         {d3[0]},  [r8], ip
        vst1.32         {d3[1]},  [r8], ip
        vst1.32         {d6[0]},  [r8], ip
        vst1.32         {d6[1]},  [r8], ip
        vst1.32         {d7[0]},  [r8], ip
        vst1.32         {d7[1]},  [r8], ip
        b               8f
7:      vsri.32         d18, d16, #16
        vsri.32         d19, d17, #16
        vst1.32         {d18[0]}, [r8], ip
        vsri.32         d22, d20, #16
        vst1.32         {d18[1]}, [r8], ip
        vsri.32         d23, d21, #16
        vst1.32         {d19[0]}, [r8], ip
        vst1.32         {d19[1]}, [r8], ip
        vst1.32         {d22[0]}, [r8], ip
        vst1.32         {d22[1]}, [r8], ip
        vst1.32         {d23[0]}, [r8], ip
        vst1.32         {d23[1]}, [r8], ip
8:      subs            r3,  r3,  #2
        add             r0,  r0,  #4
        popeq           {r4-r8,pc}

        @ 1 channel
4:      ldr             r4,  [r1],#4
        tst             r2,  #8
        mov             lr,  r2
        mov             r5,  r0
        vld1.64         {d0-d1},  [r4,:128]!
        vcvt.s32.f32    q0,  q0,  #16
        vld1.64         {d2-d3},  [r4,:128]!
        vcvt.s32.f32    q1,  q1,  #16
        bne             8f
6:      subs            lr,  lr,  #16
        vld1.64         {d4-d5},  [r4,:128]!
        vcvt.s32.f32    q2,  q2,  #16
        vld1.64         {d6-d7},  [r4,:128]!
        vcvt.s32.f32    q3,  q3,  #16
        vst1.16         {d0[1]},  [r5,:16], ip
        vst1.16         {d0[3]},  [r5,:16], ip
        vst1.16         {d1[1]},  [r5,:16], ip
        vst1.16         {d1[3]},  [r5,:16], ip
        vst1.16         {d2[1]},  [r5,:16], ip
        vst1.16         {d2[3]},  [r5,:16], ip
        vst1.16         {d3[1]},  [r5,:16], ip
        vst1.16         {d3[3]},  [r5,:16], ip
        beq             7f
        vld1.64         {d0-d1},  [r4,:128]!
        vcvt.s32.f32    q0,  q0,  #16
        vld1.64         {d2-d3},  [r4,:128]!
        vcvt.s32.f32    q1,  q1,  #16
7:      vst1.16         {d4[1]},  [r5,:16], ip
        vst1.16         {d4[3]},  [r5,:16], ip
        vst1.16         {d5[1]},  [r5,:16], ip
        vst1.16         {d5[3]},  [r5,:16], ip
        vst1.16         {d6[1]},  [r5,:16], ip
        vst1.16         {d6[3]},  [r5,:16], ip
        vst1.16         {d7[1]},  [r5,:16], ip
        vst1.16         {d7[3]},  [r5,:16], ip
        bgt             6b
        pop             {r4-r8,pc}
8:      subs            lr,  lr,  #8
        vst1.16         {d0[1]},  [r5,:16], ip
        vst1.16         {d0[3]},  [r5,:16], ip
        vst1.16         {d1[1]},  [r5,:16], ip
        vst1.16         {d1[3]},  [r5,:16], ip
        vst1.16         {d2[1]},  [r5,:16], ip
        vst1.16         {d2[3]},  [r5,:16], ip
        vst1.16         {d3[1]},  [r5,:16], ip
        vst1.16         {d3[3]},  [r5,:16], ip
        popeq           {r4-r8,pc}
        vld1.64         {d0-d1},  [r4,:128]!
        vcvt.s32.f32    q0,  q0,  #16
        vld1.64         {d2-d3},  [r4,:128]!
        vcvt.s32.f32    q1,  q1,  #16
        b               6b
        .endfunc

function ff_vector_fmul_neon, export=1
        mov             r3,  r0
        subs            r2,  r2,  #8
        vld1.64         {d0-d3},  [r0,:128]!
        vld1.64         {d4-d7},  [r1,:128]!
        vmul.f32        q8,  q0,  q2
        vmul.f32        q9,  q1,  q3
        beq             3f
        bics            ip,  r2,  #15
        beq             2f
1:      subs            ip,  ip,  #16
        vld1.64         {d0-d1},  [r0,:128]!
        vld1.64         {d4-d5},  [r1,:128]!
        vmul.f32        q10, q0,  q2
        vld1.64         {d2-d3},  [r0,:128]!
        vld1.64         {d6-d7},  [r1,:128]!
        vmul.f32        q11, q1,  q3
        vst1.64         {d16-d19},[r3,:128]!
        vld1.64         {d0-d1},  [r0,:128]!
        vld1.64         {d4-d5},  [r1,:128]!
        vmul.f32        q8,  q0,  q2
        vld1.64         {d2-d3},  [r0,:128]!
        vld1.64         {d6-d7},  [r1,:128]!
        vmul.f32        q9,  q1,  q3
        vst1.64         {d20-d23},[r3,:128]!
        bne             1b
        ands            r2,  r2,  #15
        beq             3f
2:      vld1.64         {d0-d1},  [r0,:128]!
        vld1.64         {d4-d5},  [r1,:128]!
        vst1.64         {d16-d17},[r3,:128]!
        vmul.f32        q8,  q0,  q2
        vld1.64         {d2-d3},  [r0,:128]!
        vld1.64         {d6-d7},  [r1,:128]!
        vst1.64         {d18-d19},[r3,:128]!
        vmul.f32        q9,  q1,  q3
3:      vst1.64         {d16-d19},[r3,:128]!
        bx              lr
        .endfunc

function ff_vector_fmul_window_neon, export=1
        vld1.32         {d16[],d17[]}, [sp,:32]
        push            {r4,r5,lr}
        ldr             lr,  [sp, #16]
        sub             r2,  r2,  #8
        sub             r5,  lr,  #2
        add             r2,  r2,  r5, lsl #2
        add             r4,  r3,  r5, lsl #3
        add             ip,  r0,  r5, lsl #3
        mov             r5,  #-16
        vld1.64         {d0,d1},  [r1,:128]!
        vld1.64         {d2,d3},  [r2,:128], r5
        vld1.64         {d4,d5},  [r3,:128]!
        vld1.64         {d6,d7},  [r4,:128], r5
1:      subs            lr,  lr,  #4
        vmov            q11, q8
        vmla.f32        d22, d0,  d4
        vmov            q10, q8
        vmla.f32        d23, d1,  d5
        vrev64.32       q3,  q3
        vmla.f32        d20, d0,  d7
        vrev64.32       q1,  q1
        vmla.f32        d21, d1,  d6
        beq             2f
        vmla.f32        d22, d3,  d7
        vld1.64         {d0,d1},  [r1,:128]!
        vmla.f32        d23, d2,  d6
        vld1.64         {d18,d19},[r2,:128], r5
        vmls.f32        d20, d3,  d4
        vld1.64         {d24,d25},[r3,:128]!
        vmls.f32        d21, d2,  d5
        vld1.64         {d6,d7},  [r4,:128], r5
        vmov            q1,  q9
        vrev64.32       q11, q11
        vmov            q2,  q12
        vswp            d22, d23
        vst1.64         {d20,d21},[r0,:128]!
        vst1.64         {d22,d23},[ip,:128], r5
        b               1b
2:      vmla.f32        d22, d3,  d7
        vmla.f32        d23, d2,  d6
        vmls.f32        d20, d3,  d4
        vmls.f32        d21, d2,  d5
        vrev64.32       q11, q11
        vswp            d22, d23
        vst1.64         {d20,d21},[r0,:128]!
        vst1.64         {d22,d23},[ip,:128], r5
        pop             {r4,r5,pc}
        .endfunc
