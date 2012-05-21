/*
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
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

.macro  vp6_edge_filter
        vdup.16         q3,  r2                 @ t
        vmov.i16        q13, #1
        vsubl.u8        q0,  d20, d18           @ p[   0] - p[-s]
        vsubl.u8        q1,  d16, d22           @ p[-2*s] - p[ s]
        vsubl.u8        q14, d21, d19
        vsubl.u8        q15, d17, d23
        vadd.i16        q2,  q0,  q0            @ 2*(p[0]-p[-s])
        vadd.i16        d29, d28, d28
        vadd.i16        q0,  q0,  q1            @    p[0]-p[-s]  + p[-2*s]-p[s]
        vadd.i16        d28, d28, d30
        vadd.i16        q0,  q0,  q2            @ 3*(p[0]-p[-s]) + p[-2*s]-p[s]
        vadd.i16        d28, d28, d29
        vrshr.s16       q0,  q0,  #3            @ v
        vrshr.s16       d28, d28, #3
        vsub.i16        q8,  q3,  q13           @ t-1
        vabs.s16        q1,  q0                 @ V
        vshr.s16        q2,  q0,  #15           @ s
        vabs.s16        d30, d28
        vshr.s16        d29, d28, #15
        vsub.i16        q12, q1,  q3            @ V-t
        vsub.i16        d31, d30, d6
        vsub.i16        q12, q12, q13           @ V-t-1
        vsub.i16        d31, d31, d26
        vcge.u16        q12, q12, q8            @ V-t-1 >= t-1
        vcge.u16        d31, d31, d16
        vadd.i16        q13, q3,  q3            @ 2*t
        vadd.i16        d16, d6,  d6
        vsub.i16        q13, q13, q1            @ 2*t - V
        vsub.i16        d16, d16, d30
        vadd.i16        q13, q13, q2            @ += s
        vadd.i16        d16, d16, d29
        veor            q13, q13, q2            @ ^= s
        veor            d16, d16, d29
        vbif            q0,  q13, q12
        vbif            d28, d16, d31
        vmovl.u8        q1,  d20
        vmovl.u8        q15, d21
        vaddw.u8        q2,  q0,  d18
        vaddw.u8        q3,  q14, d19
        vsub.i16        q1,  q1,  q0
        vsub.i16        d30, d30, d28
        vqmovun.s16     d18, q2
        vqmovun.s16     d19, q3
        vqmovun.s16     d20, q1
        vqmovun.s16     d21, q15
.endm

function ff_vp6_edge_filter_ver_neon, export=1
        sub             r0,  r0,  r1,  lsl #1
        vld1.8          {q8},     [r0], r1      @ p[-2*s]
        vld1.8          {q9},     [r0], r1      @ p[-s]
        vld1.8          {q10},    [r0], r1      @ p[0]
        vld1.8          {q11},    [r0]          @ p[s]
        vp6_edge_filter
        sub             r0,  r0,  r1,  lsl #1
        sub             r1,  r1,  #8
        vst1.8          {d18},    [r0]!
        vst1.32         {d19[0]}, [r0], r1
        vst1.8          {d20},    [r0]!
        vst1.32         {d21[0]}, [r0]
        bx              lr
endfunc

function ff_vp6_edge_filter_hor_neon, export=1
        sub             r3,  r0,  #1
        sub             r0,  r0,  #2
        vld1.32         {d16[0]}, [r0], r1
        vld1.32         {d18[0]}, [r0], r1
        vld1.32         {d20[0]}, [r0], r1
        vld1.32         {d22[0]}, [r0], r1
        vld1.32         {d16[1]}, [r0], r1
        vld1.32         {d18[1]}, [r0], r1
        vld1.32         {d20[1]}, [r0], r1
        vld1.32         {d22[1]}, [r0], r1
        vld1.32         {d17[0]}, [r0], r1
        vld1.32         {d19[0]}, [r0], r1
        vld1.32         {d21[0]}, [r0], r1
        vld1.32         {d23[0]}, [r0], r1
        vtrn.8          q8,  q9
        vtrn.8          q10, q11
        vtrn.16         q8,  q10
        vtrn.16         q9,  q11
        vp6_edge_filter
        vtrn.8          q9,  q10
        vst1.16         {d18[0]}, [r3], r1
        vst1.16         {d20[0]}, [r3], r1
        vst1.16         {d18[1]}, [r3], r1
        vst1.16         {d20[1]}, [r3], r1
        vst1.16         {d18[2]}, [r3], r1
        vst1.16         {d20[2]}, [r3], r1
        vst1.16         {d18[3]}, [r3], r1
        vst1.16         {d20[3]}, [r3], r1
        vst1.16         {d19[0]}, [r3], r1
        vst1.16         {d21[0]}, [r3], r1
        vst1.16         {d19[1]}, [r3], r1
        vst1.16         {d21[1]}, [r3], r1
        bx              lr
endfunc
