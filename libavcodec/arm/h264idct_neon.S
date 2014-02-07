/*
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

function ff_h264_idct_add_neon, export=1
        vld1.64         {d0-d3},  [r1,:128]
        vmov.i16        q15, #0

        vswp            d1,  d2
        vst1.16         {q15},    [r1,:128]!
        vadd.i16        d4,  d0,  d1
        vst1.16         {q15},    [r1,:128]!
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

        vrshr.s16       q0,  q0,  #6
        vrshr.s16       q1,  q1,  #6

        vaddw.u8        q0,  q0,  d18
        vaddw.u8        q1,  q1,  d19

        vqmovun.s16     d0,  q0
        vqmovun.s16     d1,  q1

        vst1.32         {d0[0]},  [r0,:32], r2
        vst1.32         {d1[1]},  [r0,:32], r2
        vst1.32         {d0[1]},  [r0,:32], r2
        vst1.32         {d1[0]},  [r0,:32], r2

        sub             r1,  r1,  #32
        bx              lr
endfunc

function ff_h264_idct_dc_add_neon, export=1
        mov             r3,       #0
        vld1.16         {d2[],d3[]}, [r1,:16]
        strh            r3,       [r1]
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
endfunc

function ff_h264_idct_add16_neon, export=1
        push            {r4-r8,lr}
        mov             r4,  r0
        mov             r5,  r1
        mov             r1,  r2
        mov             r2,  r3
        ldr             r6,  [sp, #24]
        movrel          r7,  scan8
        mov             ip,  #16
1:      ldrb            r8,  [r7], #1
        ldr             r0,  [r5], #4
        ldrb            r8,  [r6, r8]
        subs            r8,  r8,  #1
        blt             2f
        ldrsh           lr,  [r1]
        add             r0,  r0,  r4
        it              ne
        movne           lr,  #0
        cmp             lr,  #0
        ite             ne
        adrne           lr,  X(ff_h264_idct_dc_add_neon) + CONFIG_THUMB
        adreq           lr,  X(ff_h264_idct_add_neon)    + CONFIG_THUMB
        blx             lr
2:      subs            ip,  ip,  #1
        add             r1,  r1,  #32
        bne             1b
        pop             {r4-r8,pc}
endfunc

function ff_h264_idct_add16intra_neon, export=1
        push            {r4-r8,lr}
        mov             r4,  r0
        mov             r5,  r1
        mov             r1,  r2
        mov             r2,  r3
        ldr             r6,  [sp, #24]
        movrel          r7,  scan8
        mov             ip,  #16
1:      ldrb            r8,  [r7], #1
        ldr             r0,  [r5], #4
        ldrb            r8,  [r6, r8]
        add             r0,  r0,  r4
        cmp             r8,  #0
        ldrsh           r8,  [r1]
        iteet           ne
        adrne           lr,  X(ff_h264_idct_add_neon)    + CONFIG_THUMB
        adreq           lr,  X(ff_h264_idct_dc_add_neon) + CONFIG_THUMB
        cmpeq           r8,  #0
        blxne           lr
        subs            ip,  ip,  #1
        add             r1,  r1,  #32
        bne             1b
        pop             {r4-r8,pc}
endfunc

function ff_h264_idct_add8_neon, export=1
        push            {r4-r10,lr}
        ldm             r0,  {r4,r9}
        add             r5,  r1,  #16*4
        add             r1,  r2,  #16*32
        mov             r2,  r3
        mov             r10, r1
        ldr             r6,  [sp, #32]
        movrel          r7,  scan8+16
        mov             r12, #0
1:      ldrb            r8,  [r7, r12]
        ldr             r0,  [r5, r12, lsl #2]
        ldrb            r8,  [r6, r8]
        add             r0,  r0,  r4
        add             r1,  r10, r12, lsl #5
        cmp             r8,  #0
        ldrsh           r8,  [r1]
        iteet           ne
        adrne           lr,  X(ff_h264_idct_add_neon)    + CONFIG_THUMB
        adreq           lr,  X(ff_h264_idct_dc_add_neon) + CONFIG_THUMB
        cmpeq           r8,  #0
        blxne           lr
        add             r12, r12, #1
        cmp             r12, #4
        itt             eq
        moveq           r12, #16
        moveq           r4,  r9
        cmp             r12, #20
        blt             1b
        pop             {r4-r10,pc}
endfunc

.macro  idct8x8_cols    pass
  .if \pass == 0
        qa      .req    q2
        qb      .req    q14
        vshr.s16        q2,  q10, #1
        vadd.i16        q0,  q8,  q12
        vld1.16         {q14-q15},[r1,:128]
        vst1.16         {q3},     [r1,:128]!
        vst1.16         {q3},     [r1,:128]!
        vsub.i16        q1,  q8,  q12
        vshr.s16        q3,  q14, #1
        vsub.i16        q2,  q2,  q14
        vadd.i16        q3,  q3,  q10
  .else
        qa      .req    q14
        qb      .req    q2
        vtrn.32         q8,  q10
        vtrn.16         q12, q13
        vtrn.32         q9,  q11
        vtrn.32         q12, q2
        vtrn.32         q13, q15
        vswp            d21, d4
        vshr.s16        q14, q10, #1
        vswp            d17, d24
        vshr.s16        q3,  q2,  #1
        vswp            d19, d26
        vadd.i16        q0,  q8,  q12
        vswp            d23, d30
        vsub.i16        q1,  q8,  q12
        vsub.i16        q14, q14, q2
        vadd.i16        q3,  q3,  q10
  .endif
        vadd.i16        q10, q1,  qa
        vsub.i16        q12, q1,  qa
        vadd.i16        q8,  q0,  q3
        vsub.i16        qb,  q0,  q3
        vsub.i16        q0,  q13, q11
        vadd.i16        q1,  q15, q9
        vsub.i16        qa,  q15, q9
        vadd.i16        q3,  q13, q11
        vsub.i16        q0,  q0,  q15
        vsub.i16        q1,  q1,  q11
        vadd.i16        qa,  qa,  q13
        vadd.i16        q3,  q3,  q9
        vshr.s16        q9,  q9,  #1
        vshr.s16        q11, q11, #1
        vshr.s16        q13, q13, #1
        vshr.s16        q15, q15, #1
        vsub.i16        q0,  q0,  q15
        vsub.i16        q1,  q1,  q11
        vadd.i16        qa,  qa,  q13
        vadd.i16        q3,  q3,  q9
        vshr.s16        q9,  q0,  #2
        vshr.s16        q11, q1,  #2
        vshr.s16        q13, qa,  #2
        vshr.s16        q15, q3,  #2
        vsub.i16        q3,  q3,  q9
        vsub.i16        qa,  q11, qa
        vadd.i16        q1,  q1,  q13
        vadd.i16        q0,  q0,  q15
  .if \pass == 0
        vsub.i16        q15, q8,  q3
        vadd.i16        q8,  q8,  q3
        vadd.i16        q9,  q10, q2
        vsub.i16        q2,  q10, q2
        vtrn.16         q8,  q9
        vadd.i16        q10, q12, q1
        vtrn.16         q2,  q15
        vadd.i16        q11, q14, q0
        vsub.i16        q13, q12, q1
        vtrn.16         q10, q11
        vsub.i16        q12, q14, q0
  .else
        vsub.i16        q15, q8,  q3
        vadd.i16        q8,  q8,  q3
        vadd.i16        q9,  q10, q14
        vsub.i16        q14, q10, q14
        vadd.i16        q10, q12, q1
        vsub.i16        q13, q12, q1
        vadd.i16        q11, q2, q0
        vsub.i16        q12, q2, q0
  .endif
        .unreq          qa
        .unreq          qb
.endm

function ff_h264_idct8_add_neon, export=1
        vmov.i16        q3,       #0
        vld1.16         {q8-q9},  [r1,:128]
        vst1.16         {q3},     [r1,:128]!
        vst1.16         {q3},     [r1,:128]!
        vld1.16         {q10-q11},[r1,:128]
        vst1.16         {q3},     [r1,:128]!
        vst1.16         {q3},     [r1,:128]!
        vld1.16         {q12-q13},[r1,:128]
        vst1.16         {q3},     [r1,:128]!
        vst1.16         {q3},     [r1,:128]!

        idct8x8_cols    0
        idct8x8_cols    1

        mov             r3,  r0
        vrshr.s16       q8,  q8,  #6
        vld1.8          {d0},     [r0,:64], r2
        vrshr.s16       q9,  q9,  #6
        vld1.8          {d1},     [r0,:64], r2
        vrshr.s16       q10, q10, #6
        vld1.8          {d2},     [r0,:64], r2
        vrshr.s16       q11, q11, #6
        vld1.8          {d3},     [r0,:64], r2
        vrshr.s16       q12, q12, #6
        vld1.8          {d4},     [r0,:64], r2
        vrshr.s16       q13, q13, #6
        vld1.8          {d5},     [r0,:64], r2
        vrshr.s16       q14, q14, #6
        vld1.8          {d6},     [r0,:64], r2
        vrshr.s16       q15, q15, #6
        vld1.8          {d7},     [r0,:64], r2
        vaddw.u8        q8,  q8,  d0
        vaddw.u8        q9,  q9,  d1
        vaddw.u8        q10, q10, d2
        vqmovun.s16     d0,  q8
        vaddw.u8        q11, q11, d3
        vqmovun.s16     d1,  q9
        vaddw.u8        q12, q12, d4
        vqmovun.s16     d2,  q10
        vst1.8          {d0},     [r3,:64], r2
        vaddw.u8        q13, q13, d5
        vqmovun.s16     d3,  q11
        vst1.8          {d1},     [r3,:64], r2
        vaddw.u8        q14, q14, d6
        vqmovun.s16     d4,  q12
        vst1.8          {d2},     [r3,:64], r2
        vaddw.u8        q15, q15, d7
        vqmovun.s16     d5,  q13
        vst1.8          {d3},     [r3,:64], r2
        vqmovun.s16     d6,  q14
        vqmovun.s16     d7,  q15
        vst1.8          {d4},     [r3,:64], r2
        vst1.8          {d5},     [r3,:64], r2
        vst1.8          {d6},     [r3,:64], r2
        vst1.8          {d7},     [r3,:64], r2

        sub             r1,  r1,  #128
        bx              lr
endfunc

function ff_h264_idct8_dc_add_neon, export=1
        mov             r3,       #0
        vld1.16         {d30[],d31[]},[r1,:16]
        strh            r3,       [r1]
        vld1.32         {d0},     [r0,:64], r2
        vrshr.s16       q15, q15, #6
        vld1.32         {d1},     [r0,:64], r2
        vld1.32         {d2},     [r0,:64], r2
        vaddw.u8        q8,  q15, d0
        vld1.32         {d3},     [r0,:64], r2
        vaddw.u8        q9,  q15, d1
        vld1.32         {d4},     [r0,:64], r2
        vaddw.u8        q10, q15, d2
        vld1.32         {d5},     [r0,:64], r2
        vaddw.u8        q11, q15, d3
        vld1.32         {d6},     [r0,:64], r2
        vaddw.u8        q12, q15, d4
        vld1.32         {d7},     [r0,:64], r2
        vaddw.u8        q13, q15, d5
        vaddw.u8        q14, q15, d6
        vaddw.u8        q15, q15, d7
        vqmovun.s16     d0,  q8
        vqmovun.s16     d1,  q9
        vqmovun.s16     d2,  q10
        vqmovun.s16     d3,  q11
        sub             r0,  r0,  r2, lsl #3
        vst1.32         {d0},     [r0,:64], r2
        vqmovun.s16     d4,  q12
        vst1.32         {d1},     [r0,:64], r2
        vqmovun.s16     d5,  q13
        vst1.32         {d2},     [r0,:64], r2
        vqmovun.s16     d6,  q14
        vst1.32         {d3},     [r0,:64], r2
        vqmovun.s16     d7,  q15
        vst1.32         {d4},     [r0,:64], r2
        vst1.32         {d5},     [r0,:64], r2
        vst1.32         {d6},     [r0,:64], r2
        vst1.32         {d7},     [r0,:64], r2
        bx              lr
endfunc

function ff_h264_idct8_add4_neon, export=1
        push            {r4-r8,lr}
        mov             r4,  r0
        mov             r5,  r1
        mov             r1,  r2
        mov             r2,  r3
        ldr             r6,  [sp, #24]
        movrel          r7,  scan8
        mov             r12, #16
1:      ldrb            r8,  [r7], #4
        ldr             r0,  [r5], #16
        ldrb            r8,  [r6, r8]
        subs            r8,  r8,  #1
        blt             2f
        ldrsh           lr,  [r1]
        add             r0,  r0,  r4
        it              ne
        movne           lr,  #0
        cmp             lr,  #0
        ite             ne
        adrne           lr,  X(ff_h264_idct8_dc_add_neon) + CONFIG_THUMB
        adreq           lr,  X(ff_h264_idct8_add_neon)    + CONFIG_THUMB
        blx             lr
2:      subs            r12, r12, #4
        add             r1,  r1,  #128
        bne             1b
        pop             {r4-r8,pc}
endfunc

const   scan8
        .byte           4+ 1*8, 5+ 1*8, 4+ 2*8, 5+ 2*8
        .byte           6+ 1*8, 7+ 1*8, 6+ 2*8, 7+ 2*8
        .byte           4+ 3*8, 5+ 3*8, 4+ 4*8, 5+ 4*8
        .byte           6+ 3*8, 7+ 3*8, 6+ 4*8, 7+ 4*8
        .byte           4+ 6*8, 5+ 6*8, 4+ 7*8, 5+ 7*8
        .byte           6+ 6*8, 7+ 6*8, 6+ 7*8, 7+ 7*8
        .byte           4+ 8*8, 5+ 8*8, 4+ 9*8, 5+ 9*8
        .byte           6+ 8*8, 7+ 8*8, 6+ 9*8, 7+ 9*8
        .byte           4+11*8, 5+11*8, 4+12*8, 5+12*8
        .byte           6+11*8, 7+11*8, 6+12*8, 7+12*8
        .byte           4+13*8, 5+13*8, 4+14*8, 5+14*8
        .byte           6+13*8, 7+13*8, 6+14*8, 7+14*8
endconst
