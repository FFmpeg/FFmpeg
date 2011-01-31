/*
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
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

function ff_dca_lfe_fir_neon, export=1
        push            {r4-r6,lr}

        add             r4,  r0,  r3,  lsl #2   @ out2
        add             r5,  r2,  #256*4-16     @ cf1
        sub             r1,  r1,  #12
        cmp             r3,  #32
        moveq           r6,  #256/32
        movne           r6,  #256/64
NOVFP   vldr            s0,  [sp, #16]          @ scale
        mov             lr,  #-16
1:
        vmov.f32        q2,  #0.0               @ v0
        vmov.f32        q3,  #0.0               @ v1
        mov             r12, r6
2:
        vld1.32         {q8},     [r2,:128]!    @ cf0
        vld1.32         {q9},     [r5,:128], lr @ cf1
        vld1.32         {q1},     [r1], lr      @ in
        subs            r12, r12, #4
        vrev64.32       q10, q8
        vmla.f32        q3,  q1,  q9
        vmla.f32        d4,  d2,  d21
        vmla.f32        d5,  d3,  d20
        bne             2b

        add             r1,  r1,  r6,  lsl #2
        subs            r3,  r3,  #1
        vadd.f32        d4,  d4,  d5
        vadd.f32        d6,  d6,  d7
        vpadd.f32       d4,  d4,  d6
        vmul.f32        d5,  d4,  d0[0]
        vst1.32         {d5[0]},  [r0,:32]!
        vst1.32         {d5[1]},  [r4,:32]!
        bne             1b

        pop             {r4-r6,pc}
endfunc
