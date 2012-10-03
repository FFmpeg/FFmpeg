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

#include "libavutil/arm/asm.S"

function ff_synth_filter_float_neon, export=1
        push            {r3-r11,lr}

        ldr             r4,  [r2]               @ synth_buf_offset
        add             r1,  r1,  r4,  lsl #2   @ synth_buf
        sub             r12, r4,  #32
        bfc             r12, #9,  #23
        bic             r4,  r4,  #63
        str             r12, [r2]

        ldr             r2,  [sp, #12*4]        @ in
        mov             r9,  r1                 @ synth_buf

VFP     vpush           {d0}
        bl              X(ff_imdct_half_neon)
VFP     vpop            {d0}
        pop             {r3}

        ldr             r5,  [sp, #9*4]         @ window
        ldr             r2,  [sp, #10*4]        @ out
NOVFP   vldr            s0,  [sp, #12*4]        @ scale
        add             r8,  r9,  #12*4

        mov             lr,  #64*4
        mov             r1,  #4
1:
        add             r10, r9,  #16*4         @ synth_buf
        add             r11, r8,  #16*4
        add             r0,  r5,  #16*4         @ window
        add             r6,  r5,  #32*4
        add             r7,  r5,  #48*4

        vld1.32         {q10},    [r3,:128]     @ a
        add             r3,  r3,  #16*4
        vld1.32         {q1},     [r3,:128]     @ b
        vmov.f32        q2,  #0.0               @ c
        vmov.f32        q3,  #0.0               @ d

        mov             r12, #512
2:
        vld1.32         {q9},     [r8, :128], lr
        vrev64.32       q9,  q9
        vld1.32         {q8},     [r5, :128], lr
        vmls.f32        d20, d16, d19
        vld1.32         {q11},    [r0, :128], lr
        vmls.f32        d21, d17, d18
        vld1.32         {q12},    [r9, :128], lr
        vmla.f32        d2,  d22, d24
        vld1.32         {q8},     [r6, :128], lr
        vmla.f32        d3,  d23, d25
        vld1.32         {q9},     [r10,:128], lr
        vmla.f32        d4,  d16, d18
        vld1.32         {q12},    [r11,:128], lr
        vmla.f32        d5,  d17, d19
        vrev64.32       q12, q12
        vld1.32         {q11},    [r7, :128], lr
        vmla.f32        d6,  d22, d25
        vmla.f32        d7,  d23, d24
        subs            r12, r12, #64
        beq             3f
        cmp             r12, r4
        bne             2b
        sub             r8,  r8,  #512*4
        sub             r9,  r9,  #512*4
        sub             r10, r10, #512*4
        sub             r11, r11, #512*4
        b               2b
3:
        vmul.f32        q8,  q10, d0[0]
        vmul.f32        q9,  q1,  d0[0]
        vst1.32         {q3},     [r3,:128]
        sub             r3,  r3,  #16*4
        vst1.32         {q2},     [r3,:128]
        vst1.32         {q8},     [r2,:128]
        add             r2,  r2,  #16*4
        vst1.32         {q9},     [r2,:128]

        subs            r1,  r1,  #1
        it              eq
        popeq           {r4-r11,pc}

        cmp             r4,  #0
        itt             eq
        subeq           r8,  r8,  #512*4
        subeq           r9,  r9,  #512*4
        sub             r5,  r5,  #512*4
        sub             r2,  r2,  #12*4         @ out
        add             r3,  r3,  #4*4          @ synth_buf2
        add             r5,  r5,  #4*4          @ window
        add             r9,  r9,  #4*4          @ synth_buf
        sub             r8,  r8,  #4*4          @ synth_buf
        b               1b
endfunc
