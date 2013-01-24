/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of libswresample.
 *
 * libswresample is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libswresample is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libswresample; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "libavutil/arm/asm.S"

function swri_oldapi_conv_flt_to_s16_neon, export=1
        subs            r2,  r2,  #8
        vld1.32         {q0},     [r1,:128]!
        vcvt.s32.f32    q8,  q0,  #31
        vld1.32         {q1},     [r1,:128]!
        vcvt.s32.f32    q9,  q1,  #31
        beq             3f
        bics            r12, r2,  #15
        beq             2f
1:      subs            r12, r12, #16
        vqrshrn.s32     d4,  q8,  #16
        vld1.32         {q0},     [r1,:128]!
        vcvt.s32.f32    q0,  q0,  #31
        vqrshrn.s32     d5,  q9,  #16
        vld1.32         {q1},     [r1,:128]!
        vcvt.s32.f32    q1,  q1,  #31
        vqrshrn.s32     d6,  q0,  #16
        vst1.16         {q2},     [r0,:128]!
        vqrshrn.s32     d7,  q1,  #16
        vld1.32         {q8},     [r1,:128]!
        vcvt.s32.f32    q8,  q8,  #31
        vld1.32         {q9},     [r1,:128]!
        vcvt.s32.f32    q9,  q9,  #31
        vst1.16         {q3},     [r0,:128]!
        bne             1b
        ands            r2,  r2,  #15
        beq             3f
2:      vld1.32         {q0},     [r1,:128]!
        vqrshrn.s32     d4,  q8,  #16
        vcvt.s32.f32    q0,  q0,  #31
        vld1.32         {q1},     [r1,:128]!
        vqrshrn.s32     d5,  q9,  #16
        vcvt.s32.f32    q1,  q1,  #31
        vqrshrn.s32     d6,  q0,  #16
        vst1.16         {q2},     [r0,:128]!
        vqrshrn.s32     d7,  q1,  #16
        vst1.16         {q3},     [r0,:128]!
        bx              lr
3:      vqrshrn.s32     d4,  q8,  #16
        vqrshrn.s32     d5,  q9,  #16
        vst1.16         {q2},     [r0,:128]!
        bx              lr
endfunc

function swri_oldapi_conv_fltp_to_s16_2ch_neon, export=1
        ldm             r1,  {r1, r3}
        subs            r2,  r2,  #8
        vld1.32         {q0},     [r1,:128]!
        vcvt.s32.f32    q8,  q0,  #31
        vld1.32         {q1},     [r1,:128]!
        vcvt.s32.f32    q9,  q1,  #31
        vld1.32         {q10},    [r3,:128]!
        vcvt.s32.f32    q10, q10, #31
        vld1.32         {q11},    [r3,:128]!
        vcvt.s32.f32    q11, q11, #31
        beq             3f
        bics            r12, r2,  #15
        beq             2f
1:      subs            r12, r12, #16
        vld1.32         {q0},     [r1,:128]!
        vcvt.s32.f32    q0,  q0,  #31
        vsri.32         q10, q8,  #16
        vld1.32         {q1},     [r1,:128]!
        vcvt.s32.f32    q1,  q1,  #31
        vld1.32         {q12},    [r3,:128]!
        vcvt.s32.f32    q12, q12, #31
        vld1.32         {q13},    [r3,:128]!
        vsri.32         q11, q9,  #16
        vst1.16         {q10},    [r0,:128]!
        vcvt.s32.f32    q13, q13, #31
        vst1.16         {q11},    [r0,:128]!
        vsri.32         q12, q0,  #16
        vld1.32         {q8},     [r1,:128]!
        vsri.32         q13, q1,  #16
        vst1.16         {q12},    [r0,:128]!
        vcvt.s32.f32    q8,  q8,  #31
        vld1.32         {q9},     [r1,:128]!
        vcvt.s32.f32    q9,  q9,  #31
        vld1.32         {q10},    [r3,:128]!
        vcvt.s32.f32    q10, q10, #31
        vld1.32         {q11},    [r3,:128]!
        vcvt.s32.f32    q11, q11, #31
        vst1.16         {q13},    [r0,:128]!
        bne             1b
        ands            r2,  r2,  #15
        beq             3f
2:      vsri.32         q10, q8,  #16
        vld1.32         {q0},     [r1,:128]!
        vcvt.s32.f32    q0,  q0,  #31
        vld1.32         {q1},     [r1,:128]!
        vcvt.s32.f32    q1,  q1,  #31
        vld1.32         {q12},    [r3,:128]!
        vcvt.s32.f32    q12, q12, #31
        vsri.32         q11, q9,  #16
        vld1.32         {q13},    [r3,:128]!
        vcvt.s32.f32    q13, q13, #31
        vst1.16         {q10},    [r0,:128]!
        vsri.32         q12, q0,  #16
        vst1.16         {q11},    [r0,:128]!
        vsri.32         q13, q1,  #16
        vst1.16         {q12-q13},[r0,:128]!
        bx              lr
3:      vsri.32         q10, q8,  #16
        vsri.32         q11, q9,  #16
        vst1.16         {q10-q11},[r0,:128]!
        bx              lr
endfunc

function swri_oldapi_conv_fltp_to_s16_nch_neon, export=1
        cmp             r3,  #2
        itt             lt
        ldrlt           r1,  [r1]
        blt             swri_oldapi_conv_flt_to_s16_neon
        beq             swri_oldapi_conv_fltp_to_s16_2ch_neon

        push            {r4-r8, lr}
        cmp             r3,  #4
        lsl             r12, r3,  #1
        blt             4f

        @ 4 channels
5:      ldm             r1!, {r4-r7}
        mov             lr,  r2
        mov             r8,  r0
        vld1.32         {q8},     [r4,:128]!
        vcvt.s32.f32    q8,  q8,  #31
        vld1.32         {q9},     [r5,:128]!
        vcvt.s32.f32    q9,  q9,  #31
        vld1.32         {q10},    [r6,:128]!
        vcvt.s32.f32    q10, q10, #31
        vld1.32         {q11},    [r7,:128]!
        vcvt.s32.f32    q11, q11, #31
6:      subs            lr,  lr,  #8
        vld1.32         {q0},     [r4,:128]!
        vcvt.s32.f32    q0,  q0,  #31
        vsri.32         q9,  q8,  #16
        vld1.32         {q1},     [r5,:128]!
        vcvt.s32.f32    q1,  q1,  #31
        vsri.32         q11, q10, #16
        vld1.32         {q2},     [r6,:128]!
        vcvt.s32.f32    q2,  q2,  #31
        vzip.32         d18, d22
        vld1.32         {q3},     [r7,:128]!
        vcvt.s32.f32    q3,  q3,  #31
        vzip.32         d19, d23
        vst1.16         {d18},    [r8], r12
        vsri.32         q1,  q0,  #16
        vst1.16         {d22},    [r8], r12
        vsri.32         q3,  q2,  #16
        vst1.16         {d19},    [r8], r12
        vzip.32         d2,  d6
        vst1.16         {d23},    [r8], r12
        vzip.32         d3,  d7
        beq             7f
        vld1.32         {q8},     [r4,:128]!
        vcvt.s32.f32    q8,  q8,  #31
        vst1.16         {d2},     [r8], r12
        vld1.32         {q9},     [r5,:128]!
        vcvt.s32.f32    q9,  q9,  #31
        vst1.16         {d6},     [r8], r12
        vld1.32         {q10},    [r6,:128]!
        vcvt.s32.f32    q10, q10, #31
        vst1.16         {d3},     [r8], r12
        vld1.32         {q11},    [r7,:128]!
        vcvt.s32.f32    q11, q11, #31
        vst1.16         {d7},     [r8], r12
        b               6b
7:      vst1.16         {d2},     [r8], r12
        vst1.16         {d6},     [r8], r12
        vst1.16         {d3},     [r8], r12
        vst1.16         {d7},     [r8], r12
        subs            r3,  r3,  #4
        it              eq
        popeq           {r4-r8, pc}
        cmp             r3,  #4
        add             r0,  r0,  #8
        bge             5b

        @ 2 channels
4:      cmp             r3,  #2
        blt             4f
        ldm             r1!, {r4-r5}
        mov             lr,  r2
        mov             r8,  r0
        tst             lr,  #8
        vld1.32         {q8},     [r4,:128]!
        vcvt.s32.f32    q8,  q8,  #31
        vld1.32         {q9},     [r5,:128]!
        vcvt.s32.f32    q9,  q9,  #31
        vld1.32         {q10},    [r4,:128]!
        vcvt.s32.f32    q10, q10, #31
        vld1.32         {q11},    [r5,:128]!
        vcvt.s32.f32    q11, q11, #31
        beq             6f
        subs            lr,  lr,  #8
        beq             7f
        vsri.32         d18, d16, #16
        vsri.32         d19, d17, #16
        vld1.32         {q8},     [r4,:128]!
        vcvt.s32.f32    q8,  q8,  #31
        vst1.32         {d18[0]}, [r8], r12
        vsri.32         d22, d20, #16
        vst1.32         {d18[1]}, [r8], r12
        vsri.32         d23, d21, #16
        vst1.32         {d19[0]}, [r8], r12
        vst1.32         {d19[1]}, [r8], r12
        vld1.32         {q9},     [r5,:128]!
        vcvt.s32.f32    q9,  q9,  #31
        vst1.32         {d22[0]}, [r8], r12
        vst1.32         {d22[1]}, [r8], r12
        vld1.32         {q10},    [r4,:128]!
        vcvt.s32.f32    q10, q10, #31
        vst1.32         {d23[0]}, [r8], r12
        vst1.32         {d23[1]}, [r8], r12
        vld1.32         {q11},    [r5,:128]!
        vcvt.s32.f32    q11, q11, #31
6:      subs            lr,  lr,  #16
        vld1.32         {q0},     [r4,:128]!
        vcvt.s32.f32    q0,  q0,  #31
        vsri.32         d18, d16, #16
        vld1.32         {q1},     [r5,:128]!
        vcvt.s32.f32    q1,  q1,  #31
        vsri.32         d19, d17, #16
        vld1.32         {q2},     [r4,:128]!
        vcvt.s32.f32    q2,  q2,  #31
        vld1.32         {q3},     [r5,:128]!
        vcvt.s32.f32    q3,  q3,  #31
        vst1.32         {d18[0]}, [r8], r12
        vsri.32         d22, d20, #16
        vst1.32         {d18[1]}, [r8], r12
        vsri.32         d23, d21, #16
        vst1.32         {d19[0]}, [r8], r12
        vsri.32         d2,  d0,  #16
        vst1.32         {d19[1]}, [r8], r12
        vsri.32         d3,  d1,  #16
        vst1.32         {d22[0]}, [r8], r12
        vsri.32         d6,  d4,  #16
        vst1.32         {d22[1]}, [r8], r12
        vsri.32         d7,  d5,  #16
        vst1.32         {d23[0]}, [r8], r12
        vst1.32         {d23[1]}, [r8], r12
        beq             6f
        vld1.32         {q8},     [r4,:128]!
        vcvt.s32.f32    q8,  q8,  #31
        vst1.32         {d2[0]},  [r8], r12
        vst1.32         {d2[1]},  [r8], r12
        vld1.32         {q9},     [r5,:128]!
        vcvt.s32.f32    q9,  q9,  #31
        vst1.32         {d3[0]},  [r8], r12
        vst1.32         {d3[1]},  [r8], r12
        vld1.32         {q10},    [r4,:128]!
        vcvt.s32.f32    q10, q10, #31
        vst1.32         {d6[0]},  [r8], r12
        vst1.32         {d6[1]},  [r8], r12
        vld1.32         {q11},    [r5,:128]!
        vcvt.s32.f32    q11, q11, #31
        vst1.32         {d7[0]},  [r8], r12
        vst1.32         {d7[1]},  [r8], r12
        bgt             6b
6:      vst1.32         {d2[0]},  [r8], r12
        vst1.32         {d2[1]},  [r8], r12
        vst1.32         {d3[0]},  [r8], r12
        vst1.32         {d3[1]},  [r8], r12
        vst1.32         {d6[0]},  [r8], r12
        vst1.32         {d6[1]},  [r8], r12
        vst1.32         {d7[0]},  [r8], r12
        vst1.32         {d7[1]},  [r8], r12
        b               8f
7:      vsri.32         d18, d16, #16
        vsri.32         d19, d17, #16
        vst1.32         {d18[0]}, [r8], r12
        vsri.32         d22, d20, #16
        vst1.32         {d18[1]}, [r8], r12
        vsri.32         d23, d21, #16
        vst1.32         {d19[0]}, [r8], r12
        vst1.32         {d19[1]}, [r8], r12
        vst1.32         {d22[0]}, [r8], r12
        vst1.32         {d22[1]}, [r8], r12
        vst1.32         {d23[0]}, [r8], r12
        vst1.32         {d23[1]}, [r8], r12
8:      subs            r3,  r3,  #2
        add             r0,  r0,  #4
        it              eq
        popeq           {r4-r8, pc}

        @ 1 channel
4:      ldr             r4,  [r1]
        tst             r2,  #8
        mov             lr,  r2
        mov             r5,  r0
        vld1.32         {q0},     [r4,:128]!
        vcvt.s32.f32    q0,  q0,  #31
        vld1.32         {q1},     [r4,:128]!
        vcvt.s32.f32    q1,  q1,  #31
        bne             8f
6:      subs            lr,  lr,  #16
        vld1.32         {q2},     [r4,:128]!
        vcvt.s32.f32    q2,  q2,  #31
        vld1.32         {q3},     [r4,:128]!
        vcvt.s32.f32    q3,  q3,  #31
        vst1.16         {d0[1]},  [r5,:16], r12
        vst1.16         {d0[3]},  [r5,:16], r12
        vst1.16         {d1[1]},  [r5,:16], r12
        vst1.16         {d1[3]},  [r5,:16], r12
        vst1.16         {d2[1]},  [r5,:16], r12
        vst1.16         {d2[3]},  [r5,:16], r12
        vst1.16         {d3[1]},  [r5,:16], r12
        vst1.16         {d3[3]},  [r5,:16], r12
        beq             7f
        vld1.32         {q0},     [r4,:128]!
        vcvt.s32.f32    q0,  q0,  #31
        vld1.32         {q1},     [r4,:128]!
        vcvt.s32.f32    q1,  q1,  #31
7:      vst1.16         {d4[1]},  [r5,:16], r12
        vst1.16         {d4[3]},  [r5,:16], r12
        vst1.16         {d5[1]},  [r5,:16], r12
        vst1.16         {d5[3]},  [r5,:16], r12
        vst1.16         {d6[1]},  [r5,:16], r12
        vst1.16         {d6[3]},  [r5,:16], r12
        vst1.16         {d7[1]},  [r5,:16], r12
        vst1.16         {d7[3]},  [r5,:16], r12
        bgt             6b
        pop             {r4-r8, pc}
8:      subs            lr,  lr,  #8
        vst1.16         {d0[1]},  [r5,:16], r12
        vst1.16         {d0[3]},  [r5,:16], r12
        vst1.16         {d1[1]},  [r5,:16], r12
        vst1.16         {d1[3]},  [r5,:16], r12
        vst1.16         {d2[1]},  [r5,:16], r12
        vst1.16         {d2[3]},  [r5,:16], r12
        vst1.16         {d3[1]},  [r5,:16], r12
        vst1.16         {d3[3]},  [r5,:16], r12
        it              eq
        popeq           {r4-r8, pc}
        vld1.32         {q0},     [r4,:128]!
        vcvt.s32.f32    q0,  q0,  #31
        vld1.32         {q1},     [r4,:128]!
        vcvt.s32.f32    q1,  q1,  #31
        b               6b
endfunc
