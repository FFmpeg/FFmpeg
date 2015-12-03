/*
 * ARM NEON optimised Format Conversion Utils
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2015 Janne Grunau  <janne-libav@jannau.net>b
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
#include "libavutil/arm/asm.S"

function ff_int32_to_float_fmul_scalar_neon, export=1
VFP     vdup.32         q0,  d0[0]
VFP     len     .req    r2
NOVFP   vdup.32         q0,  r2
NOVFP   len     .req    r3

        vld1.32         {q1},[r1,:128]!
        vcvt.f32.s32    q3,  q1
        vld1.32         {q2},[r1,:128]!
        vcvt.f32.s32    q8,  q2
1:      subs            len, len, #8
        pld             [r1, #16]
        vmul.f32        q9,  q3,  q0
        vmul.f32        q10, q8,  q0
        beq             2f
        vld1.32         {q1},[r1,:128]!
        vcvt.f32.s32    q3,  q1
        vld1.32         {q2},[r1,:128]!
        vcvt.f32.s32    q8,  q2
        vst1.32         {q9}, [r0,:128]!
        vst1.32         {q10},[r0,:128]!
        b               1b
2:      vst1.32         {q9}, [r0,:128]!
        vst1.32         {q10},[r0,:128]!
        bx              lr
        .unreq  len
endfunc

function ff_int32_to_float_fmul_array8_neon, export=1
        ldr             r0,  [sp]
        lsr             r0,  r0,  #3
        subs            r0,  r0,  #1
        beq             1f
2:
        vld1.32         {q0-q1},   [r2,:128]!
        vld1.32         {q2-q3},   [r2,:128]!
        vld1.32         {d20},     [r3]!
        subs            r0,  r0,  #2
        vcvt.f32.s32    q0,  q0
        vcvt.f32.s32    q1,  q1
        vdup.32         q8,  d20[0]
        vcvt.f32.s32    q2,  q2
        vcvt.f32.s32    q3,  q3
        vmul.f32        q0,  q0,  q8
        vdup.32         q9,  d20[1]
        vmul.f32        q1,  q1,  q8
        vmul.f32        q2,  q2,  q9
        vmul.f32        q3,  q3,  q9
        vst1.32         {q0-q1},   [r1,:128]!
        vst1.32         {q2-q3},   [r1,:128]!
        bgt             2b
        it              lt
        bxlt            lr
1:
        vld1.32         {q0-q1},   [r2,:128]
        vld1.32         {d16[],d17[]},  [r3]
        vcvt.f32.s32    q0,  q0
        vcvt.f32.s32    q1,  q1
        vmul.f32        q0,  q0,  q8
        vmul.f32        q1,  q1,  q8
        vst1.32         {q0-q1},   [r1,:128]
        bx              lr
endfunc
