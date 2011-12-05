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

.macro  rv40_weight
        vmovl.u8        q8,  d2
        vmovl.u8        q9,  d3
        vmovl.u8        q10, d4
        vmovl.u8        q11, d5
        vmull.u16       q2,  d16, d0[2]
        vmull.u16       q3,  d17, d0[2]
        vmull.u16       q8,  d18, d0[2]
        vmull.u16       q9,  d19, d0[2]
        vmull.u16       q12, d20, d0[0]
        vmull.u16       q13, d21, d0[0]
        vmull.u16       q14, d22, d0[0]
        vmull.u16       q15, d23, d0[0]
        vshrn.i32       d4,  q2,  #9
        vshrn.i32       d5,  q3,  #9
        vshrn.i32       d6,  q8,  #9
        vshrn.i32       d7,  q9,  #9
        vshrn.i32       d16, q12, #9
        vshrn.i32       d17, q13, #9
        vshrn.i32       d18, q14, #9
        vshrn.i32       d19, q15, #9
        vadd.u16        q2,  q2,  q8
        vadd.u16        q3,  q3,  q9
        vrshrn.i16      d2,  q2,  #5
        vrshrn.i16      d3,  q3,  #5
.endm

/* void ff_rv40_weight_func_16_neon(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                                    int w1, int w2, int stride) */
function ff_rv40_weight_func_16_neon, export=1
        ldr             r12, [sp]
        vmov            d0,  r3,  r12
        ldr             r12, [sp, #4]
        mov             r3,  #16
1:
        vld1.8          {q1},     [r1,:128], r12
        vld1.8          {q2},     [r2,:128], r12
        rv40_weight
        vst1.8          {q1},     [r0,:128], r12
        subs            r3,  r3,  #1
        bne             1b
        bx              lr
endfunc

/* void ff_rv40_weight_func_8_neon(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                                   int w1, int w2, int stride) */
function ff_rv40_weight_func_8_neon, export=1
        ldr             r12, [sp]
        vmov            d0,  r3,  r12
        ldr             r12, [sp, #4]
        mov             r3,  #8
1:
        vld1.8          {d2},     [r1,:64], r12
        vld1.8          {d3},     [r1,:64], r12
        vld1.8          {d4},     [r2,:64], r12
        vld1.8          {d5},     [r2,:64], r12
        rv40_weight
        vst1.8          {d2},     [r0,:64], r12
        vst1.8          {d3},     [r0,:64], r12
        subs            r3,  r3,  #2
        bne             1b
        bx              lr
endfunc
