/*
 * Copyright (c) 2012 Mans Rullgard
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

function ff_ps_add_squares_neon, export=1
        mov             r3,  r0
        sub             r2,  r2,  #4
        vld1.32         {q0},     [r1,:128]!
        vmul.f32        q0,  q0,  q0
        vld1.32         {q2},     [r1,:128]!
        vmul.f32        q2,  q2,  q2
        vld1.32         {q1},     [r0,:128]!
1:
        vpadd.f32       d6,  d0,  d1
        vld1.32         {q0},     [r1,:128]!
        vpadd.f32       d7,  d4,  d5
        vmul.f32        q0,  q0,  q0
        vld1.32         {q2},     [r1,:128]!
        vadd.f32        q3,  q1,  q3
        vld1.32         {q1},     [r0,:128]!
        vmul.f32        q2,  q2,  q2
        vst1.32         {q3},     [r3,:128]!
        subs            r2,  r2,  #4
        bgt             1b
        vpadd.f32       d6,  d0,  d1
        vpadd.f32       d7,  d4,  d5
        vadd.f32        q1,  q1,  q3
        vst1.32         {q1},     [r3,:128]!
        bx              lr
endfunc

function ff_ps_mul_pair_single_neon, export=1
        sub             r3,  r3,  #4
        tst             r1,  #8
        bne             2f
        vld1.32         {q0},     [r1,:128]!
1:
        vld1.32         {q3},     [r2,:128]!
        vmul.f32        d4,  d0,  d6[0]
        vmul.f32        d5,  d1,  d6[1]
        vld1.32         {q1},     [r1,:128]!
        vmul.f32        d6,  d2,  d7[0]
        vmul.f32        d7,  d3,  d7[1]
        vld1.32         {q0},     [r1,:128]!
        vst1.32         {q2,q3},  [r0,:128]!
        subs            r3,  r3,  #4
        bgt             1b
        vld1.32         {q3},     [r2,:128]!
        vmul.f32        d4,  d0,  d6[0]
        vmul.f32        d5,  d1,  d6[1]
        vld1.32         {q1},     [r1,:128]!
        vmul.f32        d6,  d2,  d7[0]
        vmul.f32        d7,  d3,  d7[1]
        vst1.32         {q2,q3},  [r0,:128]!
        bx              lr
2:
        vld1.32         {d0},     [r1,:64]!
        vld1.32         {d1,d2},  [r1,:128]!
1:
        vld1.32         {q3},     [r2,:128]!
        vmul.f32        d4,  d0,  d6[0]
        vmul.f32        d5,  d1,  d6[1]
        vld1.32         {d0,d1},  [r1,:128]!
        vmul.f32        d6,  d2,  d7[0]
        vmul.f32        d7,  d0,  d7[1]
        vmov            d0,  d1
        vld1.32         {d1,d2},  [r1,:128]!
        vst1.32         {q2,q3},  [r0,:128]!
        subs            r3,  r3,  #4
        bgt             1b
        vld1.32         {q3},     [r2,:128]!
        vmul.f32        d4,  d0,  d6[0]
        vmul.f32        d5,  d1,  d6[1]
        vld1.32         {d0},     [r1,:64]!
        vmul.f32        d6,  d2,  d7[0]
        vmul.f32        d7,  d0,  d7[1]
        vst1.32         {q2,q3},  [r0,:128]!
        bx              lr
endfunc

function ff_ps_hybrid_synthesis_deint_neon, export=1
        push            {r4-r8,lr}
        add             r0,  r0,  r2,  lsl #2
        add             r1,  r1,  r2,  lsl #5+1+2
        rsb             r2,  r2,  #64
        mov             r5,  #64*4
        mov             lr,  r0
        add             r4,  r0,  #38*64*4
        mov             r12, r3
2:
        vld1.32         {d0,d1},  [r1,:128]!
        vst1.32         {d0[0]},  [lr,:32], r5
        vst1.32         {d0[1]},  [r4,:32], r5
        vst1.32         {d1[0]},  [lr,:32], r5
        vst1.32         {d1[1]},  [r4,:32], r5
        subs            r12, r12, #2
        bgt             2b
        add             r0,  r0,  #4
        sub             r2,  r2,  #1
        tst             r2,  #2
        bne             6f
1:
        mov             lr,  r0
        add             r4,  r0,  #38*64*4
        add             r6,  r1,  #  32*2*4
        add             r7,  r1,  #2*32*2*4
        add             r8,  r1,  #3*32*2*4
        mov             r12, r3
2:
        vld1.32         {d0,d1},  [r1,:128]!
        vld1.32         {d2,d3},  [r6,:128]!
        vld1.32         {d4,d5},  [r7,:128]!
        vld1.32         {d6,d7},  [r8,:128]!
        vst4.32         {d0[0],d2[0],d4[0],d6[0]}, [lr,:128], r5
        vst4.32         {d0[1],d2[1],d4[1],d6[1]}, [r4,:128], r5
        vst4.32         {d1[0],d3[0],d5[0],d7[0]}, [lr,:128], r5
        vst4.32         {d1[1],d3[1],d5[1],d7[1]}, [r4,:128], r5
        subs            r12, r12, #2
        bgt             2b
        add             r0,  r0,  #16
        add             r1,  r1,  #3*32*2*4
        subs            r2,  r2,  #4
        bgt             1b
        pop             {r4-r8,pc}
6:
        mov             lr,  r0
        add             r4,  r0,  #38*64*4
        add             r6,  r1,  #32*2*4
        mov             r12, r3
2:
        vld1.32         {d0,d1},  [r1,:128]!
        vld1.32         {d2,d3},  [r6,:128]!
        vst2.32         {d0[0],d2[0]}, [lr,:64], r5
        vst2.32         {d0[1],d2[1]}, [r4,:64], r5
        vst2.32         {d1[0],d3[0]}, [lr,:64], r5
        vst2.32         {d1[1],d3[1]}, [r4,:64], r5
        subs            r12, r12, #2
        bgt             2b
        add             r0,  r0,  #8
        add             r1,  r1,  #32*2*4
        sub             r2,  r2,  #2
        b               1b
endfunc

function ff_ps_hybrid_analysis_neon, export=1
        vldm            r1,  {d19-d31}
        ldr             r12, [sp]
        lsl             r3,  r3,  #3
        vadd.f32        d16, d19, d31
        vadd.f32        d17, d20, d30
        vsub.f32        d18, d19, d31
        vsub.f32        d19, d20, d30
        vsub.f32        d0,  d21, d29
        vsub.f32        d1,  d22, d28
        vadd.f32        d2,  d21, d29
        vadd.f32        d3,  d22, d28
        vadd.f32        d20, d23, d27
        vadd.f32        d21, d24, d26
        vsub.f32        d22, d23, d27
        vsub.f32        d23, d24, d26
        vmov.i32        d6,  #1<<31
        vmov.i32        d7,  #0
        vmov.f32        q14, #0.0
        vmov.f32        q15, #0.0
        vtrn.32         d6,  d7
        vrev64.32       q9,  q9
        vrev64.32       q0,  q0
        vrev64.32       q11, q11
        veor            q9,  q9,  q3
        veor            q0,  q0,  q3
        veor            q11, q11, q3
        vld1.32         {q13},    [r2,:128]!
        vtrn.32         q8,  q9
        vtrn.32         q1,  q0
        vtrn.32         q10, q11
        sub             r12, r12, #1
        vmla.f32        q14, q8,  q13
        vld1.32         {q2},     [r2,:128]!
        vmla.f32        q15, q9,  q13
1:
        vmla.f32        q14, q1,  q2
        vld1.32         {q13},    [r2,:128]!
        vmla.f32        q15, q0,  q2
        vmla.f32        q14, q10, q13
        vld1.32         {q2},     [r2,:128]!
        vmla.f32        q15, q11, q13
        vld1.32         {q13},    [r2,:128]!
        vadd.f32        d6,  d28, d29
        vadd.f32        d7,  d30, d31
        vmov.f32        q14, #0.0
        vmov.f32        q15, #0.0
        vmla.f32        q14, q8,  q13
        vpadd.f32       d6,  d6,  d7
        vmla.f32        q15, q9,  q13
        vmla.f32        d6,  d25, d4[0]
        vld1.32         {q2},     [r2,:128]!
        vst1.32         {d6},     [r0,:64], r3
        subs            r12, r12, #1
        bgt             1b
        vmla.f32        q14, q1,  q2
        vld1.32         {q13},    [r2,:128]!
        vmla.f32        q15, q0,  q2
        vmla.f32        q14, q10, q13
        vld1.32         {q2},     [r2,:128]!
        vmla.f32        q15, q11, q13
        vadd.f32        d6,  d28, d29
        vadd.f32        d7,  d30, d31
        vpadd.f32       d6,  d6,  d7
        vmla.f32        d6,  d25, d4[0]
        vst1.32         {d6},     [r0,:64], r3
        bx              lr
endfunc

function ff_ps_stereo_interpolate_neon, export=1
        vld1.32         {q0},     [r2]
        vld1.32         {q14},    [r3]
        vadd.f32        q15, q14, q14
        mov             r2,  r0
        mov             r3,  r1
        ldr             r12, [sp]
        vadd.f32        q1,  q0,  q14
        vadd.f32        q0,  q0,  q15
        vld1.32         {q2},     [r0,:64]!
        vld1.32         {q3},     [r1,:64]!
        subs            r12, r12, #1
        beq             2f
1:
        vmul.f32        d16, d4,  d2[0]
        vmul.f32        d17, d5,  d0[0]
        vmul.f32        d18, d4,  d2[1]
        vmul.f32        d19, d5,  d0[1]
        vmla.f32        d16, d6,  d3[0]
        vmla.f32        d17, d7,  d1[0]
        vmla.f32        d18, d6,  d3[1]
        vmla.f32        d19, d7,  d1[1]
        vadd.f32        q1,  q1,  q15
        vadd.f32        q0,  q0,  q15
        vld1.32         {q2},     [r0,:64]!
        vld1.32         {q3},     [r1,:64]!
        vst1.32         {q8},     [r2,:64]!
        vst1.32         {q9},     [r3,:64]!
        subs            r12, r12, #2
        bgt             1b
        it              lt
        bxlt            lr
2:
        vmul.f32        d16, d4,  d2[0]
        vmul.f32        d18, d4,  d2[1]
        vmla.f32        d16, d6,  d3[0]
        vmla.f32        d18, d6,  d3[1]
        vst1.32         {d16},    [r2,:64]!
        vst1.32         {d18},    [r3,:64]!
        bx              lr
endfunc
