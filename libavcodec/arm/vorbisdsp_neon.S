/*
 * ARM NEON optimised DSP functions
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

#include "libavutil/arm/asm.S"

function ff_vorbis_inverse_coupling_neon, export=1
        vmov.i32        q10, #1<<31
        subs            r2,  r2,  #4
        mov             r3,  r0
        mov             r12, r1
        beq             3f

        vld1.32         {d24-d25},[r1,:128]!
        vld1.32         {d22-d23},[r0,:128]!
        vcle.s32        q8,  q12, #0
        vand            q9,  q11, q10
        veor            q12, q12, q9
        vand            q2,  q12, q8
        vbic            q3,  q12, q8
        vadd.f32        q12, q11, q2
        vsub.f32        q11, q11, q3
1:      vld1.32         {d2-d3},  [r1,:128]!
        vld1.32         {d0-d1},  [r0,:128]!
        vcle.s32        q8,  q1,  #0
        vand            q9,  q0,  q10
        veor            q1,  q1,  q9
        vst1.32         {d24-d25},[r3, :128]!
        vst1.32         {d22-d23},[r12,:128]!
        vand            q2,  q1,  q8
        vbic            q3,  q1,  q8
        vadd.f32        q1,  q0,  q2
        vsub.f32        q0,  q0,  q3
        subs            r2,  r2,  #8
        ble             2f
        vld1.32         {d24-d25},[r1,:128]!
        vld1.32         {d22-d23},[r0,:128]!
        vcle.s32        q8,  q12, #0
        vand            q9,  q11, q10
        veor            q12, q12, q9
        vst1.32         {d2-d3},  [r3, :128]!
        vst1.32         {d0-d1},  [r12,:128]!
        vand            q2,  q12, q8
        vbic            q3,  q12, q8
        vadd.f32        q12, q11, q2
        vsub.f32        q11, q11, q3
        b               1b

2:      vst1.32         {d2-d3},  [r3, :128]!
        vst1.32         {d0-d1},  [r12,:128]!
        it              lt
        bxlt            lr

3:      vld1.32         {d2-d3},  [r1,:128]
        vld1.32         {d0-d1},  [r0,:128]
        vcle.s32        q8,  q1,  #0
        vand            q9,  q0,  q10
        veor            q1,  q1,  q9
        vand            q2,  q1,  q8
        vbic            q3,  q1,  q8
        vadd.f32        q1,  q0,  q2
        vsub.f32        q0,  q0,  q3
        vst1.32         {d2-d3},  [r0,:128]!
        vst1.32         {d0-d1},  [r1,:128]!
        bx              lr
endfunc
