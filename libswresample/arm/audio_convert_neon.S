/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of libswresample.
 *
 * libswresample is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libswresample is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libswresample; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "libavutil/arm/asm.S"

function swri_oldapi_conv_flt_to_s16_neon, export=1
        subs            r2,  r2,  #8
        vld1.32         {q0},     [r1,:128]!
        vcvt.s32.f32    q8,  q0,  #31
        vld1.32         {q1},     [r1,:128]!
        vcvt.s32.f32    q9,  q1,  #31
        beq             3f
        bics            r12, r2,  #15
        beq             2f
1:      subs            r12, r12, #16
        vqrshrn.s32     d4,  q8,  #16
        vld1.32         {q0},     [r1,:128]!
        vcvt.s32.f32    q0,  q0,  #31
        vqrshrn.s32     d5,  q9,  #16
        vld1.32         {q1},     [r1,:128]!
        vcvt.s32.f32    q1,  q1,  #31
        vqrshrn.s32     d6,  q0,  #16
        vst1.16         {q2},     [r0,:128]!
        vqrshrn.s32     d7,  q1,  #16
        vld1.32         {q8},     [r1,:128]!
        vcvt.s32.f32    q8,  q8,  #31
        vld1.32         {q9},     [r1,:128]!
        vcvt.s32.f32    q9,  q9,  #31
        vst1.16         {q3},     [r0,:128]!
        bne             1b
        ands            r2,  r2,  #15
        beq             3f
2:      vld1.32         {q0},     [r1,:128]!
        vqrshrn.s32     d4,  q8,  #16
        vcvt.s32.f32    q0,  q0,  #31
        vld1.32         {q1},     [r1,:128]!
        vqrshrn.s32     d5,  q9,  #16
        vcvt.s32.f32    q1,  q1,  #31
        vqrshrn.s32     d6,  q0,  #16
        vst1.16         {q2},     [r0,:128]!
        vqrshrn.s32     d7,  q1,  #16
        vst1.16         {q3},     [r0,:128]!
        bx              lr
3:      vqrshrn.s32     d4,  q8,  #16
        vqrshrn.s32     d5,  q9,  #16
        vst1.16         {q2},     [r0,:128]!
        bx              lr
endfunc
