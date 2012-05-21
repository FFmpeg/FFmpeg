/*
 * ARM NEON optimised Float DSP functions
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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

#include "config.h"
#include "asm.S"

        preserve8

function ff_vector_fmul_neon, export=1
        subs            r3,  r3,  #8
        vld1.32         {d0-d3},  [r1,:128]!
        vld1.32         {d4-d7},  [r2,:128]!
        vmul.f32        q8,  q0,  q2
        vmul.f32        q9,  q1,  q3
        beq             3f
        bics            ip,  r3,  #15
        beq             2f
1:      subs            ip,  ip,  #16
        vld1.32         {d0-d1},  [r1,:128]!
        vld1.32         {d4-d5},  [r2,:128]!
        vmul.f32        q10, q0,  q2
        vld1.32         {d2-d3},  [r1,:128]!
        vld1.32         {d6-d7},  [r2,:128]!
        vmul.f32        q11, q1,  q3
        vst1.32         {d16-d19},[r0,:128]!
        vld1.32         {d0-d1},  [r1,:128]!
        vld1.32         {d4-d5},  [r2,:128]!
        vmul.f32        q8,  q0,  q2
        vld1.32         {d2-d3},  [r1,:128]!
        vld1.32         {d6-d7},  [r2,:128]!
        vmul.f32        q9,  q1,  q3
        vst1.32         {d20-d23},[r0,:128]!
        bne             1b
        ands            r3,  r3,  #15
        beq             3f
2:      vld1.32         {d0-d1},  [r1,:128]!
        vld1.32         {d4-d5},  [r2,:128]!
        vst1.32         {d16-d17},[r0,:128]!
        vmul.f32        q8,  q0,  q2
        vld1.32         {d2-d3},  [r1,:128]!
        vld1.32         {d6-d7},  [r2,:128]!
        vst1.32         {d18-d19},[r0,:128]!
        vmul.f32        q9,  q1,  q3
3:      vst1.32         {d16-d19},[r0,:128]!
        bx              lr
endfunc
