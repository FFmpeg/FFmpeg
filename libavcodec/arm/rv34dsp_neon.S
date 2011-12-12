/*
 * Copyright (c) 2011 Janne Grunau <janne-libav@jannau.net>
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

#include "asm.S"

.macro rv34_inv_transform
        mov             r1,  #16
        vld1.16         {d28}, [r0,:64], r1     @ block[i+8*0]
        vld1.16         {d29}, [r0,:64], r1     @ block[i+8*1]
        vld1.16         {d30}, [r0,:64], r1     @ block[i+8*2]
        vld1.16         {d31}, [r0,:64], r1     @ block[i+8*3]
        vmov.s16        d0,  #13
        vshll.s16       q12, d29, #3
        vshll.s16       q13, d29, #4
        vshll.s16       q9,  d31, #3
        vshll.s16       q1,  d31, #4
        vmull.s16       q10, d28, d0
        vmlal.s16       q10, d30, d0
        vmull.s16       q11, d28, d0
        vmlsl.s16       q11, d30, d0
        vsubw.s16       q12, q12, d29   @ z2 = block[i+8*1]*7
        vaddw.s16       q13, q13, d29   @ z3 = block[i+8*1]*17
        vsubw.s16       q9,  q9,  d31
        vaddw.s16       q1,  q1,  d31
        vadd.s32        q13, q13, q9    @ z3 = 17*block[i+8*1] +  7*block[i+8*3]
        vsub.s32        q12, q12, q1    @ z2 = 7*block[i+8*1]  - 17*block[i+8*3]
        vadd.s32        q1,  q10, q13   @ z0 + z3
        vadd.s32        q2,  q11, q12   @ z1 + z2
        vsub.s32        q8,  q10, q13   @ z0 - z3
        vsub.s32        q3,  q11, q12   @ z1 - z2
        vtrn.32         q1,  q2
        vtrn.32         q3,  q8
        vswp            d3,  d6
        vswp            d5,  d16
        vmov.s32        d0,  #13
        vadd.s32        q10, q1,  q3
        vsub.s32        q11, q1,  q3
        vshl.s32        q12, q2,  #3
        vshl.s32        q9,  q2,  #4
        vmul.s32        q13, q11, d0[0]
        vshl.s32        q11, q8,  #4
        vadd.s32        q9,  q9,  q2
        vshl.s32        q15, q8,  #3
        vsub.s32        q12, q12, q2
        vadd.s32        q11, q11, q8
        vmul.s32        q14, q10, d0[0]
        vsub.s32        q8,  q15, q8
        vsub.s32        q12, q12, q11
        vadd.s32        q9,  q9,  q8
        vadd.s32        q2,  q13, q12   @ z1 + z2
        vadd.s32        q1,  q14, q9    @ z0 + z3
        vsub.s32        q3,  q13, q12   @ z1 - z2
        vsub.s32        q15, q14, q9    @ z0 - z3
.endm

/* void ff_rv34_inv_transform_neon(DCTELEM *block); */
function ff_rv34_inv_transform_neon, export=1
        mov             r2,  r0
        rv34_inv_transform
        vrshrn.s32      d1,  q2,  #10   @ (z1 + z2) >> 10
        vrshrn.s32      d0,  q1,  #10   @ (z0 + z3) >> 10
        vrshrn.s32      d2,  q3,  #10   @ (z1 - z2) >> 10
        vrshrn.s32      d3,  q15, #10   @ (z0 - z3) >> 10
        vst4.16         {d0[0], d1[0], d2[0], d3[0]}, [r2,:64], r1
        vst4.16         {d0[1], d1[1], d2[1], d3[1]}, [r2,:64], r1
        vst4.16         {d0[2], d1[2], d2[2], d3[2]}, [r2,:64], r1
        vst4.16         {d0[3], d1[3], d2[3], d3[3]}, [r2,:64], r1
        bx              lr
endfunc

/* void rv34_inv_transform_noround_neon(DCTELEM *block); */
function ff_rv34_inv_transform_noround_neon, export=1
        mov             r2,  r0
        rv34_inv_transform
        vshl.s32        q11, q2,  #1
        vshl.s32        q10, q1,  #1
        vshl.s32        q12, q3,  #1
        vshl.s32        q13, q15, #1
        vadd.s32        q11, q11, q2
        vadd.s32        q10, q10, q1
        vadd.s32        q12, q12, q3
        vadd.s32        q13, q13, q15
        vshrn.s32       d0,  q10, #11   @ (z0 + z3)*3 >> 11
        vshrn.s32       d1,  q11, #11   @ (z1 + z2)*3 >> 11
        vshrn.s32       d2,  q12, #11   @ (z1 - z2)*3 >> 11
        vshrn.s32       d3,  q13, #11   @ (z0 - z3)*3 >> 11
        vst4.16         {d0[0], d1[0], d2[0], d3[0]}, [r2,:64], r1
        vst4.16         {d0[1], d1[1], d2[1], d3[1]}, [r2,:64], r1
        vst4.16         {d0[2], d1[2], d2[2], d3[2]}, [r2,:64], r1
        vst4.16         {d0[3], d1[3], d2[3], d3[3]}, [r2,:64], r1
        bx              lr
endfunc

function ff_rv34_dequant4x4_neon, export=1
        mov             r3,  r0
        mov             r12, #16
        vdup.16         q0,  r2
        vmov.16         d0[0], r1
        vld1.16         {d2},     [r0,:64], r12
        vld1.16         {d4},     [r0,:64], r12
        vld1.16         {d6},     [r0,:64], r12
        vld1.16         {d16},    [r0,:64], r12
        vmull.s16       q1,  d2,  d0
        vmull.s16       q2,  d4,  d1
        vmull.s16       q3,  d6,  d1
        vmull.s16       q8,  d16, d1
        vqrshrn.s32     d2,  q1,  #4
        vqrshrn.s32     d4,  q2,  #4
        vqrshrn.s32     d6,  q3,  #4
        vqrshrn.s32     d16, q8,  #4
        vst1.16         {d2},     [r3,:64], r12
        vst1.16         {d4},     [r3,:64], r12
        vst1.16         {d6},     [r3,:64], r12
        vst1.16         {d16},    [r3,:64], r12
        bx              lr
endfunc
