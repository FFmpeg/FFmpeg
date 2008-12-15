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
