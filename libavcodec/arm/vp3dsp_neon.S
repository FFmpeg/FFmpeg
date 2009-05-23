/*
 * Copyright (c) 2009 David Conrad
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

.macro vp3_loop_filter
    vsubl.u8        q3,  d18, d17
    vsubl.u8        q2,  d16, d19
    vadd.i16        q1,  q3,  q3
    vadd.i16        q2,  q2,  q3
    vadd.i16        q0,  q1,  q2
    vrshr.s16       q0,  q0,  #3
    vmovl.u8        q9,  d18
    vdup.u16        q15, r2

    vabs.s16        q1,  q0
    vshr.s16        q0,  q0,  #15
    vqsub.u16       q2,  q15, q1
    vqsub.u16       q3,  q2,  q1
    vsub.i16        q1,  q2,  q3
    veor            q1,  q1,  q0
    vsub.i16        q0,  q1,  q0

    vaddw.u8        q2,  q0,  d17
    vsub.i16        q3,  q9,  q0
    vqmovun.s16     d0,  q2
    vqmovun.s16     d1,  q3
.endm

function ff_vp3_v_loop_filter_neon, export=1
    sub             ip,  r0,  r1
    sub             r0,  r0,  r1,  lsl #1
    vld1.64         {d16}, [r0,:64], r1
    vld1.64         {d17}, [r0,:64], r1
    vld1.64         {d18}, [r0,:64], r1
    vld1.64         {d19}, [r0,:64], r1
    ldrb            r2,    [r2, #129*4]

    vp3_loop_filter

    vst1.64         {d0},  [ip,:64], r1
    vst1.64         {d1},  [ip,:64], r1
    bx              lr
.endfunc

function ff_vp3_h_loop_filter_neon, export=1
    sub             ip,  r0,  #1
    sub             r0,  r0,  #2
    vld1.32         {d16[]},  [r0], r1
    vld1.32         {d17[]},  [r0], r1
    vld1.32         {d18[]},  [r0], r1
    vld1.32         {d19[]},  [r0], r1
    vld1.32         {d16[1]}, [r0], r1
    vld1.32         {d17[1]}, [r0], r1
    vld1.32         {d18[1]}, [r0], r1
    vld1.32         {d19[1]}, [r0], r1
    ldrb            r2,  [r2, #129*4]

    vtrn.8          d16, d17
    vtrn.8          d18, d19
    vtrn.16         d16, d18
    vtrn.16         d17, d19

    vp3_loop_filter

    vtrn.8          d0,  d1

    vst1.16         {d0[0]}, [ip], r1
    vst1.16         {d1[0]}, [ip], r1
    vst1.16         {d0[1]}, [ip], r1
    vst1.16         {d1[1]}, [ip], r1
    vst1.16         {d0[2]}, [ip], r1
    vst1.16         {d1[2]}, [ip], r1
    vst1.16         {d0[3]}, [ip], r1
    vst1.16         {d1[3]}, [ip], r1
    bx              lr
.endfunc
