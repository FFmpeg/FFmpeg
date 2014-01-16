/*
 * ARM NEON optimised audio functions
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

#include "libavutil/arm/asm.S"

function ff_vector_clipf_neon, export=1
VFP     vdup.32         q1,  d0[1]
VFP     vdup.32         q0,  d0[0]
NOVFP   vdup.32         q0,  r2
NOVFP   vdup.32         q1,  r3
NOVFP   ldr             r2,  [sp]
        vld1.f32        {q2},[r1,:128]!
        vmin.f32        q10, q2,  q1
        vld1.f32        {q3},[r1,:128]!
        vmin.f32        q11, q3,  q1
1:      vmax.f32        q8,  q10, q0
        vmax.f32        q9,  q11, q0
        subs            r2,  r2,  #8
        beq             2f
        vld1.f32        {q2},[r1,:128]!
        vmin.f32        q10, q2,  q1
        vld1.f32        {q3},[r1,:128]!
        vmin.f32        q11, q3,  q1
        vst1.f32        {q8},[r0,:128]!
        vst1.f32        {q9},[r0,:128]!
        b               1b
2:      vst1.f32        {q8},[r0,:128]!
        vst1.f32        {q9},[r0,:128]!
        bx              lr
endfunc

function ff_vector_clip_int32_neon, export=1
        vdup.32         q0,  r2
        vdup.32         q1,  r3
        ldr             r2,  [sp]
1:
        vld1.32         {q2-q3},  [r1,:128]!
        vmin.s32        q2,  q2,  q1
        vmin.s32        q3,  q3,  q1
        vmax.s32        q2,  q2,  q0
        vmax.s32        q3,  q3,  q0
        vst1.32         {q2-q3},  [r0,:128]!
        subs            r2,  r2,  #8
        bgt             1b
        bx              lr
endfunc
