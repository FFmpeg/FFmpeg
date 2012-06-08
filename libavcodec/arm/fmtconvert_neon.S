/*
 * ARM NEON optimised Format Conversion Utils
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

#include "config.h"
#include "libavutil/arm/asm.S"

        preserve8

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
endfunc

function ff_float_to_int16_interleave_neon, export=1
        cmp             r3, #2
        itt             lt
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
        it              eq
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
        it              eq
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
        it              eq
        popeq           {r4-r8,pc}
        vld1.64         {d0-d1},  [r4,:128]!
        vcvt.s32.f32    q0,  q0,  #16
        vld1.64         {d2-d3},  [r4,:128]!
        vcvt.s32.f32    q1,  q1,  #16
        b               6b
endfunc

function ff_int32_to_float_fmul_scalar_neon, export=1
VFP     vdup.32         q0,  d0[0]
VFP     len     .req    r2
NOVFP   vdup.32         q0,  r2
NOVFP   len     .req    r3

        vld1.32         {q1},[r1,:128]!
        vcvt.f32.s32    q3,  q1
        vld1.32         {q2},[r1,:128]!
        vcvt.f32.s32    q8,  q2
1:      subs            len, len, #8
        pld             [r1, #16]
        vmul.f32        q9,  q3,  q0
        vmul.f32        q10, q8,  q0
        beq             2f
        vld1.32         {q1},[r1,:128]!
        vcvt.f32.s32    q3,  q1
        vld1.32         {q2},[r1,:128]!
        vcvt.f32.s32    q8,  q2
        vst1.32         {q9}, [r0,:128]!
        vst1.32         {q10},[r0,:128]!
        b               1b
2:      vst1.32         {q9}, [r0,:128]!
        vst1.32         {q10},[r0,:128]!
        bx              lr
        .unreq  len
endfunc
