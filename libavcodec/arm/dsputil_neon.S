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

function ff_clear_block_neon, export=1
        vmov.i16        q0,  #0
        .rept           8
        vst1.16         {q0}, [r0,:128]!
        .endr
        bx              lr
endfunc

function ff_clear_blocks_neon, export=1
        vmov.i16        q0,  #0
        .rept           8*6
        vst1.16         {q0}, [r0,:128]!
        .endr
        bx              lr
endfunc

function ff_put_pixels_clamped_neon, export=1
        vld1.16         {d16-d19}, [r0,:128]!
        vqmovun.s16     d0, q8
        vld1.16         {d20-d23}, [r0,:128]!
        vqmovun.s16     d1, q9
        vld1.16         {d24-d27}, [r0,:128]!
        vqmovun.s16     d2, q10
        vld1.16         {d28-d31}, [r0,:128]!
        vqmovun.s16     d3, q11
        vst1.8          {d0},      [r1,:64], r2
        vqmovun.s16     d4, q12
        vst1.8          {d1},      [r1,:64], r2
        vqmovun.s16     d5, q13
        vst1.8          {d2},      [r1,:64], r2
        vqmovun.s16     d6, q14
        vst1.8          {d3},      [r1,:64], r2
        vqmovun.s16     d7, q15
        vst1.8          {d4},      [r1,:64], r2
        vst1.8          {d5},      [r1,:64], r2
        vst1.8          {d6},      [r1,:64], r2
        vst1.8          {d7},      [r1,:64], r2
        bx              lr
endfunc

function ff_put_signed_pixels_clamped_neon, export=1
        vmov.u8         d31, #128
        vld1.16         {d16-d17}, [r0,:128]!
        vqmovn.s16      d0, q8
        vld1.16         {d18-d19}, [r0,:128]!
        vqmovn.s16      d1, q9
        vld1.16         {d16-d17}, [r0,:128]!
        vqmovn.s16      d2, q8
        vld1.16         {d18-d19}, [r0,:128]!
        vadd.u8         d0, d0, d31
        vld1.16         {d20-d21}, [r0,:128]!
        vadd.u8         d1, d1, d31
        vld1.16         {d22-d23}, [r0,:128]!
        vadd.u8         d2, d2, d31
        vst1.8          {d0},      [r1,:64], r2
        vqmovn.s16      d3, q9
        vst1.8          {d1},      [r1,:64], r2
        vqmovn.s16      d4, q10
        vst1.8          {d2},      [r1,:64], r2
        vqmovn.s16      d5, q11
        vld1.16         {d24-d25}, [r0,:128]!
        vadd.u8         d3, d3, d31
        vld1.16         {d26-d27}, [r0,:128]!
        vadd.u8         d4, d4, d31
        vadd.u8         d5, d5, d31
        vst1.8          {d3},      [r1,:64], r2
        vqmovn.s16      d6, q12
        vst1.8          {d4},      [r1,:64], r2
        vqmovn.s16      d7, q13
        vst1.8          {d5},      [r1,:64], r2
        vadd.u8         d6, d6, d31
        vadd.u8         d7, d7, d31
        vst1.8          {d6},      [r1,:64], r2
        vst1.8          {d7},      [r1,:64], r2
        bx              lr
endfunc

function ff_add_pixels_clamped_neon, export=1
        mov             r3, r1
        vld1.8          {d16},   [r1,:64], r2
        vld1.16         {d0-d1}, [r0,:128]!
        vaddw.u8        q0, q0, d16
        vld1.8          {d17},   [r1,:64], r2
        vld1.16         {d2-d3}, [r0,:128]!
        vqmovun.s16     d0, q0
        vld1.8          {d18},   [r1,:64], r2
        vaddw.u8        q1, q1, d17
        vld1.16         {d4-d5}, [r0,:128]!
        vaddw.u8        q2, q2, d18
        vst1.8          {d0},    [r3,:64], r2
        vqmovun.s16     d2, q1
        vld1.8          {d19},   [r1,:64], r2
        vld1.16         {d6-d7}, [r0,:128]!
        vaddw.u8        q3, q3, d19
        vqmovun.s16     d4, q2
        vst1.8          {d2},    [r3,:64], r2
        vld1.8          {d16},   [r1,:64], r2
        vqmovun.s16     d6, q3
        vld1.16         {d0-d1}, [r0,:128]!
        vaddw.u8        q0, q0, d16
        vst1.8          {d4},    [r3,:64], r2
        vld1.8          {d17},   [r1,:64], r2
        vld1.16         {d2-d3}, [r0,:128]!
        vaddw.u8        q1, q1, d17
        vst1.8          {d6},    [r3,:64], r2
        vqmovun.s16     d0, q0
        vld1.8          {d18},   [r1,:64], r2
        vld1.16         {d4-d5}, [r0,:128]!
        vaddw.u8        q2, q2, d18
        vst1.8          {d0},    [r3,:64], r2
        vqmovun.s16     d2, q1
        vld1.8          {d19},   [r1,:64], r2
        vqmovun.s16     d4, q2
        vld1.16         {d6-d7}, [r0,:128]!
        vaddw.u8        q3, q3, d19
        vst1.8          {d2},    [r3,:64], r2
        vqmovun.s16     d6, q3
        vst1.8          {d4},    [r3,:64], r2
        vst1.8          {d6},    [r3,:64], r2
        bx              lr
endfunc

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

function ff_apply_window_int16_neon, export=1
        push            {r4,lr}
        add             r4,  r1,  r3,  lsl #1
        add             lr,  r0,  r3,  lsl #1
        sub             r4,  r4,  #16
        sub             lr,  lr,  #16
        mov             r12, #-16
1:
        vld1.16         {q0},     [r1,:128]!
        vld1.16         {q2},     [r2,:128]!
        vld1.16         {q1},     [r4,:128], r12
        vrev64.16       q3,  q2
        vqrdmulh.s16    q0,  q0,  q2
        vqrdmulh.s16    d2,  d2,  d7
        vqrdmulh.s16    d3,  d3,  d6
        vst1.16         {q0},     [r0,:128]!
        vst1.16         {q1},     [lr,:128], r12
        subs            r3,  r3,  #16
        bgt             1b

        pop             {r4,pc}
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
