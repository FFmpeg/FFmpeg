/*
 * ARM NEON optimised integer operations
 * Copyright (c) 2009 Kostya Shishkov
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

function ff_scalarproduct_int16_neon, export=1
        vmov.i16        q0,  #0
        vmov.i16        q1,  #0
        vmov.i16        q2,  #0
        vmov.i16        q3,  #0
1:      vld1.16         {d16-d17}, [r0]!
        vld1.16         {d20-d21}, [r1,:128]!
        vmlal.s16       q0,  d16,  d20
        vld1.16         {d18-d19}, [r0]!
        vmlal.s16       q1,  d17,  d21
        vld1.16         {d22-d23}, [r1,:128]!
        vmlal.s16       q2,  d18,  d22
        vmlal.s16       q3,  d19,  d23
        subs            r2,  r2,   #16
        bne             1b

        vpadd.s32       d16, d0,   d1
        vpadd.s32       d17, d2,   d3
        vpadd.s32       d18, d4,   d5
        vpadd.s32       d19, d6,   d7
        vpadd.s32       d0,  d16,  d17
        vpadd.s32       d1,  d18,  d19
        vpadd.s32       d2,  d0,   d1
        vpaddl.s32      d3,  d2
        vmov.32         r0,  d3[0]
        bx              lr
endfunc

@ scalarproduct_and_madd_int16(/*aligned*/v0,v1,v2,order,mul)
function ff_scalarproduct_and_madd_int16_neon, export=1
        vld1.16         {d28[],d29[]}, [sp]
        vmov.i16        q0,  #0
        vmov.i16        q1,  #0
        vmov.i16        q2,  #0
        vmov.i16        q3,  #0
        mov             r12, r0

1:      vld1.16         {d16-d17}, [r0,:128]!
        vld1.16         {d18-d19}, [r1]!
        vld1.16         {d20-d21}, [r2]!
        vld1.16         {d22-d23}, [r0,:128]!
        vld1.16         {d24-d25}, [r1]!
        vld1.16         {d26-d27}, [r2]!
        vmul.s16        q10, q10,  q14
        vmul.s16        q13, q13,  q14
        vmlal.s16       q0,  d16,  d18
        vmlal.s16       q1,  d17,  d19
        vadd.s16        q10, q8,   q10
        vadd.s16        q13, q11,  q13
        vmlal.s16       q2,  d22,  d24
        vmlal.s16       q3,  d23,  d25
        vst1.16         {q10},     [r12,:128]!
        subs            r3,  r3,   #16
        vst1.16         {q13},     [r12,:128]!
        bne             1b

        vpadd.s32       d16, d0,   d1
        vpadd.s32       d17, d2,   d3
        vpadd.s32       d18, d4,   d5
        vpadd.s32       d19, d6,   d7
        vpadd.s32       d0,  d16,  d17
        vpadd.s32       d1,  d18,  d19
        vpadd.s32       d2,  d0,   d1
        vpaddl.s32      d3,  d2
        vmov.32         r0,  d3[0]
        bx              lr
endfunc
