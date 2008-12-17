/*
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

        .fpu neon

        .text

function ff_h264_idct_add_neon, export=1
        mov             r3,  #(1<<5)
        vmov.i16        d16, #0
        vmov.16         d16[0],   r3
        vld1.64         {d0-d3},  [r1,:128]
        vadd.i16        d0,  d0,  d16

        vswp            d1,  d2
        vadd.i16        d4,  d0,  d1
        vshr.s16        q8,  q1,  #1
        vsub.i16        d5,  d0,  d1
        vadd.i16        d6,  d2,  d17
        vsub.i16        d7,  d16, d3
        vadd.i16        q0,  q2,  q3
        vsub.i16        q1,  q2,  q3

        vtrn.16         d0,  d1
        vtrn.16         d3,  d2
        vtrn.32         d0,  d3
        vtrn.32         d1,  d2

        vadd.i16        d4,  d0,  d3
        vld1.32         {d18[0]}, [r0,:32], r2
        vswp            d1,  d3
        vshr.s16        q8,  q1,  #1
        vld1.32         {d19[1]}, [r0,:32], r2
        vsub.i16        d5,  d0,  d1
        vld1.32         {d18[1]}, [r0,:32], r2
        vadd.i16        d6,  d16, d3
        vld1.32         {d19[0]}, [r0,:32], r2
        vsub.i16        d7,  d2,  d17
        sub             r0,  r0,  r2, lsl #2
        vadd.i16        q0,  q2,  q3
        vsub.i16        q1,  q2,  q3

        vshr.s16        q0,  q0,  #6
        vshr.s16        q1,  q1,  #6

        vaddw.u8        q0,  q0,  d18
        vaddw.u8        q1,  q1,  d19

        vqmovun.s16     d0,  q0
        vqmovun.s16     d1,  q1

        vst1.32         {d0[0]},  [r0,:32], r2
        vst1.32         {d1[1]},  [r0,:32], r2
        vst1.32         {d0[1]},  [r0,:32], r2
        vst1.32         {d1[0]},  [r0,:32], r2

        bx              lr
        .endfunc

function ff_h264_idct_dc_add_neon, export=1
        vld1.16         {d2[],d3[]}, [r1,:16]
        vrshr.s16       q1,  q1,  #6
        vld1.32         {d0[0]},  [r0,:32], r2
        vld1.32         {d0[1]},  [r0,:32], r2
        vaddw.u8        q2,  q1,  d0
        vld1.32         {d1[0]},  [r0,:32], r2
        vld1.32         {d1[1]},  [r0,:32], r2
        vaddw.u8        q1,  q1,  d1
        vqmovun.s16     d0,  q2
        vqmovun.s16     d1,  q1
        sub             r0,  r0,  r2, lsl #2
        vst1.32         {d0[0]},  [r0,:32], r2
        vst1.32         {d0[1]},  [r0,:32], r2
        vst1.32         {d1[0]},  [r0,:32], r2
        vst1.32         {d1[1]},  [r0,:32], r2
        bx              lr
        .endfunc
