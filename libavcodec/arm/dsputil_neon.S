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

.macro  pixels16        rnd=1, avg=0
  .if \avg
        mov             r12, r0
  .endif
1:      vld1.8          {q0},     [r1], r2
        vld1.8          {q1},     [r1], r2
        vld1.8          {q2},     [r1], r2
        pld             [r1, r2, lsl #2]
        vld1.8          {q3},     [r1], r2
        pld             [r1]
        pld             [r1, r2]
        pld             [r1, r2, lsl #1]
  .if \avg
        vld1.8          {q8},     [r12,:128], r2
        vrhadd.u8       q0,  q0,  q8
        vld1.8          {q9},     [r12,:128], r2
        vrhadd.u8       q1,  q1,  q9
        vld1.8          {q10},    [r12,:128], r2
        vrhadd.u8       q2,  q2,  q10
        vld1.8          {q11},    [r12,:128], r2
        vrhadd.u8       q3,  q3,  q11
  .endif
        subs            r3,  r3,  #4
        vst1.64         {q0},     [r0,:128], r2
        vst1.64         {q1},     [r0,:128], r2
        vst1.64         {q2},     [r0,:128], r2
        vst1.64         {q3},     [r0,:128], r2
        bne             1b
        bx              lr
.endm

.macro  pixels16_x2     rnd=1, avg=0
1:      vld1.8          {d0-d2},  [r1], r2
        vld1.8          {d4-d6},  [r1], r2
        pld             [r1]
        pld             [r1, r2]
        subs            r3,  r3,  #2
        vext.8          q1,  q0,  q1,  #1
        avg             q0,  q0,  q1
        vext.8          q3,  q2,  q3,  #1
        avg             q2,  q2,  q3
  .if \avg
        vld1.8          {q1},     [r0,:128], r2
        vld1.8          {q3},     [r0,:128]
        vrhadd.u8       q0,  q0,  q1
        vrhadd.u8       q2,  q2,  q3
        sub             r0,  r0,  r2
  .endif
        vst1.8          {q0},     [r0,:128], r2
        vst1.8          {q2},     [r0,:128], r2
        bne             1b
        bx              lr
.endm

.macro  pixels16_y2     rnd=1, avg=0
        sub             r3,  r3,  #2
        vld1.8          {q0},     [r1], r2
        vld1.8          {q1},     [r1], r2
1:      subs            r3,  r3,  #2
        avg             q2,  q0,  q1
        vld1.8          {q0},     [r1], r2
        avg             q3,  q0,  q1
        vld1.8          {q1},     [r1], r2
        pld             [r1]
        pld             [r1, r2]
  .if \avg
        vld1.8          {q8},     [r0,:128], r2
        vld1.8          {q9},     [r0,:128]
        vrhadd.u8       q2,  q2,  q8
        vrhadd.u8       q3,  q3,  q9
        sub             r0,  r0,  r2
  .endif
        vst1.8          {q2},     [r0,:128], r2
        vst1.8          {q3},     [r0,:128], r2
        bne             1b

        avg             q2,  q0,  q1
        vld1.8          {q0},     [r1], r2
        avg             q3,  q0,  q1
  .if \avg
        vld1.8          {q8},     [r0,:128], r2
        vld1.8          {q9},     [r0,:128]
        vrhadd.u8       q2,  q2,  q8
        vrhadd.u8       q3,  q3,  q9
        sub             r0,  r0,  r2
  .endif
        vst1.8          {q2},     [r0,:128], r2
        vst1.8          {q3},     [r0,:128], r2

        bx              lr
.endm

.macro  pixels16_xy2    rnd=1, avg=0
        sub             r3,  r3,  #2
        vld1.8          {d0-d2},  [r1], r2
        vld1.8          {d4-d6},  [r1], r2
NRND    vmov.i16        q13, #1
        pld             [r1]
        pld             [r1, r2]
        vext.8          q1,  q0,  q1,  #1
        vext.8          q3,  q2,  q3,  #1
        vaddl.u8        q8,  d0,  d2
        vaddl.u8        q10, d1,  d3
        vaddl.u8        q9,  d4,  d6
        vaddl.u8        q11, d5,  d7
1:      subs            r3,  r3,  #2
        vld1.8          {d0-d2},  [r1], r2
        vadd.u16        q12, q8,  q9
        pld             [r1]
NRND    vadd.u16        q12, q12, q13
        vext.8          q15, q0,  q1,  #1
        vadd.u16        q1 , q10, q11
        shrn            d28, q12, #2
NRND    vadd.u16        q1,  q1,  q13
        shrn            d29, q1,  #2
  .if \avg
        vld1.8          {q8},     [r0,:128]
        vrhadd.u8       q14, q14, q8
  .endif
        vaddl.u8        q8,  d0,  d30
        vld1.8          {d2-d4},  [r1], r2
        vaddl.u8        q10, d1,  d31
        vst1.8          {q14},    [r0,:128], r2
        vadd.u16        q12, q8,  q9
        pld             [r1, r2]
NRND    vadd.u16        q12, q12, q13
        vext.8          q2,  q1,  q2,  #1
        vadd.u16        q0,  q10, q11
        shrn            d30, q12, #2
NRND    vadd.u16        q0,  q0,  q13
        shrn            d31, q0,  #2
  .if \avg
        vld1.8          {q9},     [r0,:128]
        vrhadd.u8       q15, q15, q9
  .endif
        vaddl.u8        q9,  d2,  d4
        vaddl.u8        q11, d3,  d5
        vst1.8          {q15},    [r0,:128], r2
        bgt             1b

        vld1.8          {d0-d2},  [r1], r2
        vadd.u16        q12, q8,  q9
NRND    vadd.u16        q12, q12, q13
        vext.8          q15, q0,  q1,  #1
        vadd.u16        q1 , q10, q11
        shrn            d28, q12, #2
NRND    vadd.u16        q1,  q1,  q13
        shrn            d29, q1,  #2
  .if \avg
        vld1.8          {q8},     [r0,:128]
        vrhadd.u8       q14, q14, q8
  .endif
        vaddl.u8        q8,  d0,  d30
        vaddl.u8        q10, d1,  d31
        vst1.8          {q14},    [r0,:128], r2
        vadd.u16        q12, q8,  q9
NRND    vadd.u16        q12, q12, q13
        vadd.u16        q0,  q10, q11
        shrn            d30, q12, #2
NRND    vadd.u16        q0,  q0,  q13
        shrn            d31, q0,  #2
  .if \avg
        vld1.8          {q9},     [r0,:128]
        vrhadd.u8       q15, q15, q9
  .endif
        vst1.8          {q15},    [r0,:128], r2

        bx              lr
.endm

.macro  pixels8         rnd=1, avg=0
1:      vld1.8          {d0},     [r1], r2
        vld1.8          {d1},     [r1], r2
        vld1.8          {d2},     [r1], r2
        pld             [r1, r2, lsl #2]
        vld1.8          {d3},     [r1], r2
        pld             [r1]
        pld             [r1, r2]
        pld             [r1, r2, lsl #1]
  .if \avg
        vld1.8          {d4},     [r0,:64], r2
        vrhadd.u8       d0,  d0,  d4
        vld1.8          {d5},     [r0,:64], r2
        vrhadd.u8       d1,  d1,  d5
        vld1.8          {d6},     [r0,:64], r2
        vrhadd.u8       d2,  d2,  d6
        vld1.8          {d7},     [r0,:64], r2
        vrhadd.u8       d3,  d3,  d7
        sub             r0,  r0,  r2,  lsl #2
  .endif
        subs            r3,  r3,  #4
        vst1.8          {d0},     [r0,:64], r2
        vst1.8          {d1},     [r0,:64], r2
        vst1.8          {d2},     [r0,:64], r2
        vst1.8          {d3},     [r0,:64], r2
        bne             1b
        bx              lr
.endm

.macro  pixels8_x2      rnd=1, avg=0
1:      vld1.8          {q0},     [r1], r2
        vext.8          d1,  d0,  d1,  #1
        vld1.8          {q1},     [r1], r2
        vext.8          d3,  d2,  d3,  #1
        pld             [r1]
        pld             [r1, r2]
        subs            r3,  r3,  #2
        vswp            d1,  d2
        avg             q0,  q0,  q1
  .if \avg
        vld1.8          {d4},     [r0,:64], r2
        vld1.8          {d5},     [r0,:64]
        vrhadd.u8       q0,  q0,  q2
        sub             r0,  r0,  r2
  .endif
        vst1.8          {d0},     [r0,:64], r2
        vst1.8          {d1},     [r0,:64], r2
        bne             1b
        bx              lr
.endm

.macro  pixels8_y2      rnd=1, avg=0
        sub             r3,  r3,  #2
        vld1.8          {d0},     [r1], r2
        vld1.8          {d1},     [r1], r2
1:      subs            r3,  r3,  #2
        avg             d4,  d0,  d1
        vld1.8          {d0},     [r1], r2
        avg             d5,  d0,  d1
        vld1.8          {d1},     [r1], r2
        pld             [r1]
        pld             [r1, r2]
  .if \avg
        vld1.8          {d2},     [r0,:64], r2
        vld1.8          {d3},     [r0,:64]
        vrhadd.u8       q2,  q2,  q1
        sub             r0,  r0,  r2
  .endif
        vst1.8          {d4},     [r0,:64], r2
        vst1.8          {d5},     [r0,:64], r2
        bne             1b

        avg             d4,  d0,  d1
        vld1.8          {d0},     [r1], r2
        avg             d5,  d0,  d1
  .if \avg
        vld1.8          {d2},     [r0,:64], r2
        vld1.8          {d3},     [r0,:64]
        vrhadd.u8       q2,  q2,  q1
        sub             r0,  r0,  r2
  .endif
        vst1.8          {d4},     [r0,:64], r2
        vst1.8          {d5},     [r0,:64], r2

        bx              lr
.endm

.macro  pixels8_xy2     rnd=1, avg=0
        sub             r3,  r3,  #2
        vld1.8          {q0},     [r1], r2
        vld1.8          {q1},     [r1], r2
NRND    vmov.i16        q11, #1
        pld             [r1]
        pld             [r1, r2]
        vext.8          d4,  d0,  d1,  #1
        vext.8          d6,  d2,  d3,  #1
        vaddl.u8        q8,  d0,  d4
        vaddl.u8        q9,  d2,  d6
1:      subs            r3,  r3,  #2
        vld1.8          {q0},     [r1], r2
        pld             [r1]
        vadd.u16        q10, q8,  q9
        vext.8          d4,  d0,  d1,  #1
NRND    vadd.u16        q10, q10, q11
        vaddl.u8        q8,  d0,  d4
        shrn            d5,  q10, #2
        vld1.8          {q1},     [r1], r2
        vadd.u16        q10, q8,  q9
        pld             [r1, r2]
  .if \avg
        vld1.8          {d7},     [r0,:64]
        vrhadd.u8       d5,  d5,  d7
  .endif
NRND    vadd.u16        q10, q10, q11
        vst1.8          {d5},     [r0,:64], r2
        shrn            d7,  q10, #2
  .if \avg
        vld1.8          {d5},     [r0,:64]
        vrhadd.u8       d7,  d7,  d5
  .endif
        vext.8          d6,  d2,  d3,  #1
        vaddl.u8        q9,  d2,  d6
        vst1.8          {d7},     [r0,:64], r2
        bgt             1b

        vld1.8          {q0},     [r1], r2
        vadd.u16        q10, q8,  q9
        vext.8          d4,  d0,  d1,  #1
NRND    vadd.u16        q10, q10, q11
        vaddl.u8        q8,  d0,  d4
        shrn            d5,  q10, #2
        vadd.u16        q10, q8,  q9
  .if \avg
        vld1.8          {d7},     [r0,:64]
        vrhadd.u8       d5,  d5,  d7
  .endif
NRND    vadd.u16        q10, q10, q11
        vst1.8          {d5},     [r0,:64], r2
        shrn            d7,  q10, #2
  .if \avg
        vld1.8          {d5},     [r0,:64]
        vrhadd.u8       d7,  d7,  d5
  .endif
        vst1.8          {d7},     [r0,:64], r2

        bx              lr
.endm

.macro  pixfunc         pfx, name, suf, rnd=1, avg=0
  .if \rnd
    .macro avg  rd, rn, rm
        vrhadd.u8       \rd, \rn, \rm
    .endm
    .macro shrn rd, rn, rm
        vrshrn.u16      \rd, \rn, \rm
    .endm
    .macro NRND insn:vararg
    .endm
  .else
    .macro avg  rd, rn, rm
        vhadd.u8        \rd, \rn, \rm
    .endm
    .macro shrn rd, rn, rm
        vshrn.u16       \rd, \rn, \rm
    .endm
    .macro NRND insn:vararg
        \insn
    .endm
  .endif
function ff_\pfx\name\suf\()_neon, export=1
        \name           \rnd, \avg
endfunc
        .purgem         avg
        .purgem         shrn
        .purgem         NRND
.endm

.macro  pixfunc2        pfx, name, avg=0
        pixfunc         \pfx, \name,          rnd=1, avg=\avg
        pixfunc         \pfx, \name, _no_rnd, rnd=0, avg=\avg
.endm

function ff_put_h264_qpel16_mc00_neon, export=1
        mov             r3,  #16
endfunc

        pixfunc         put_, pixels16,     avg=0
        pixfunc2        put_, pixels16_x2,  avg=0
        pixfunc2        put_, pixels16_y2,  avg=0
        pixfunc2        put_, pixels16_xy2, avg=0

function ff_avg_h264_qpel16_mc00_neon, export=1
        mov             r3,  #16
endfunc

        pixfunc         avg_, pixels16,     avg=1
        pixfunc2        avg_, pixels16_x2,  avg=1
        pixfunc2        avg_, pixels16_y2,  avg=1
        pixfunc2        avg_, pixels16_xy2, avg=1

function ff_put_h264_qpel8_mc00_neon, export=1
        mov             r3,  #8
endfunc

        pixfunc         put_, pixels8,     avg=0
        pixfunc2        put_, pixels8_x2,  avg=0
        pixfunc2        put_, pixels8_y2,  avg=0
        pixfunc2        put_, pixels8_xy2, avg=0

function ff_avg_h264_qpel8_mc00_neon, export=1
        mov             r3,  #8
endfunc

        pixfunc         avg_, pixels8,     avg=1
        pixfunc         avg_, pixels8_x2,  avg=1
        pixfunc         avg_, pixels8_y2,  avg=1
        pixfunc         avg_, pixels8_xy2, avg=1

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
