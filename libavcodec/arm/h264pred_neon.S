/*
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
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

        .macro ldcol.8  rd,  rs,  rt,  n=8,  hi=0
.if \n == 8 || \hi == 0
        vld1.8          {\rd[0]}, [\rs], \rt
        vld1.8          {\rd[1]}, [\rs], \rt
        vld1.8          {\rd[2]}, [\rs], \rt
        vld1.8          {\rd[3]}, [\rs], \rt
.endif
.if \n == 8 || \hi == 1
        vld1.8          {\rd[4]}, [\rs], \rt
        vld1.8          {\rd[5]}, [\rs], \rt
        vld1.8          {\rd[6]}, [\rs], \rt
        vld1.8          {\rd[7]}, [\rs], \rt
.endif
        .endm

        .macro add16x8  dq,  dl,  dh,  rl,  rh
        vaddl.u8        \dq, \rl, \rh
        vadd.u16        \dl, \dl, \dh
        vpadd.u16       \dl, \dl, \dl
        vpadd.u16       \dl, \dl, \dl
        .endm

function ff_pred16x16_128_dc_neon, export=1
        vmov.i8         q0,  #128
        b               .L_pred16x16_dc_end
endfunc

function ff_pred16x16_top_dc_neon, export=1
        sub             r2,  r0,  r1
        vld1.8          {q0},     [r2,:128]
        add16x8         q0,  d0,  d1,  d0,  d1
        vrshrn.u16      d0,  q0,  #4
        vdup.8          q0,  d0[0]
        b               .L_pred16x16_dc_end
endfunc

function ff_pred16x16_left_dc_neon, export=1
        sub             r2,  r0,  #1
        ldcol.8         d0,  r2,  r1
        ldcol.8         d1,  r2,  r1
        add16x8         q0,  d0,  d1,  d0,  d1
        vrshrn.u16      d0,  q0,  #4
        vdup.8          q0,  d0[0]
        b               .L_pred16x16_dc_end
endfunc

function ff_pred16x16_dc_neon, export=1
        sub             r2,  r0,  r1
        vld1.8          {q0},     [r2,:128]
        sub             r2,  r0,  #1
        ldcol.8         d2,  r2,  r1
        ldcol.8         d3,  r2,  r1
        vaddl.u8        q0,  d0,  d1
        vaddl.u8        q1,  d2,  d3
        vadd.u16        q0,  q0,  q1
        vadd.u16        d0,  d0,  d1
        vpadd.u16       d0,  d0,  d0
        vpadd.u16       d0,  d0,  d0
        vrshrn.u16      d0,  q0,  #5
        vdup.8          q0,  d0[0]
.L_pred16x16_dc_end:
        mov             r3,  #8
6:      vst1.8          {q0},     [r0,:128], r1
        vst1.8          {q0},     [r0,:128], r1
        subs            r3,  r3,  #1
        bne             6b
        bx              lr
endfunc

function ff_pred16x16_hor_neon, export=1
        sub             r2,  r0,  #1
        mov             r3,  #16
1:      vld1.8          {d0[],d1[]},[r2],      r1
        vst1.8          {q0},       [r0,:128], r1
        subs            r3,  r3,  #1
        bne             1b
        bx              lr
endfunc

function ff_pred16x16_vert_neon, export=1
        sub             r0,  r0,  r1
        vld1.8          {q0},     [r0,:128], r1
        mov             r3,  #8
1:      vst1.8          {q0},     [r0,:128], r1
        vst1.8          {q0},     [r0,:128], r1
        subs            r3,  r3,  #1
        bne             1b
        bx              lr
endfunc

function ff_pred16x16_plane_neon, export=1
        sub             r3,  r0,  r1
        add             r2,  r3,  #8
        sub             r3,  r3,  #1
        vld1.8          {d0},     [r3]
        vld1.8          {d2},     [r2,:64], r1
        ldcol.8         d1,  r3,  r1
        add             r3,  r3,  r1
        ldcol.8         d3,  r3,  r1
        vrev64.8        q0,  q0
        vaddl.u8        q8,  d2,  d3
        vsubl.u8        q2,  d2,  d0
        vsubl.u8        q3,  d3,  d1
        movrel          r3,  p16weight
        vld1.8          {q0},     [r3,:128]
        vmul.s16        q2,  q2,  q0
        vmul.s16        q3,  q3,  q0
        vadd.i16        d4,  d4,  d5
        vadd.i16        d5,  d6,  d7
        vpadd.i16       d4,  d4,  d5
        vpadd.i16       d4,  d4,  d4
        vshl.i16        d5,  d4,  #2
        vaddl.s16       q2,  d4,  d5
        vrshrn.s32      d4,  q2,  #6
        mov             r3,  #0
        vtrn.16         d4,  d5
        vadd.i16        d2,  d4,  d5
        vshl.i16        d3,  d2,  #3
        vrev64.16       d16, d17
        vsub.i16        d3,  d3,  d2
        vadd.i16        d16, d16, d0
        vshl.i16        d2,  d16, #4
        vsub.i16        d2,  d2,  d3
        vshl.i16        d3,  d4,  #4
        vext.16         q0,  q0,  q0,  #7
        vsub.i16        d6,  d5,  d3
        vmov.16         d0[0], r3
        vmul.i16        q0,  q0,  d4[0]
        vdup.16         q1,  d2[0]
        vdup.16         q2,  d4[0]
        vdup.16         q3,  d6[0]
        vshl.i16        q2,  q2,  #3
        vadd.i16        q1,  q1,  q0
        vadd.i16        q3,  q3,  q2
        mov             r3,  #16
1:
        vqshrun.s16     d0,  q1,  #5
        vadd.i16        q1,  q1,  q2
        vqshrun.s16     d1,  q1,  #5
        vadd.i16        q1,  q1,  q3
        vst1.8          {q0},     [r0,:128], r1
        subs            r3,  r3,  #1
        bne             1b
        bx              lr
endfunc

        .section        .rodata
        .align          4
p16weight:
        .short          1,2,3,4,5,6,7,8

        .text

function ff_pred8x8_hor_neon, export=1
        sub             r2,  r0,  #1
        mov             r3,  #8
1:      vld1.8          {d0[]},   [r2],     r1
        vst1.8          {d0},     [r0,:64], r1
        subs            r3,  r3,  #1
        bne             1b
        bx              lr
endfunc

function ff_pred8x8_vert_neon, export=1
        sub             r0,  r0,  r1
        vld1.8          {d0},     [r0,:64], r1
        mov             r3,  #4
1:      vst1.8          {d0},     [r0,:64], r1
        vst1.8          {d0},     [r0,:64], r1
        subs            r3,  r3,  #1
        bne             1b
        bx              lr
endfunc

function ff_pred8x8_plane_neon, export=1
        sub             r3,  r0,  r1
        add             r2,  r3,  #4
        sub             r3,  r3,  #1
        vld1.32         {d0[0]},  [r3]
        vld1.32         {d2[0]},  [r2,:32], r1
        ldcol.8         d0,  r3,  r1,  4,  hi=1
        add             r3,  r3,  r1
        ldcol.8         d3,  r3,  r1,  4
        vaddl.u8        q8,  d2,  d3
        vrev32.8        d0,  d0
        vtrn.32         d2,  d3
        vsubl.u8        q2,  d2,  d0
        movrel          r3,  p16weight
        vld1.16         {q0},     [r3,:128]
        vmul.s16        d4,  d4,  d0
        vmul.s16        d5,  d5,  d0
        vpadd.i16       d4,  d4,  d5
        vpaddl.s16      d4,  d4
        vshl.i32        d5,  d4,  #4
        vadd.s32        d4,  d4,  d5
        vrshrn.s32      d4,  q2,  #5
        mov             r3,  #0
        vtrn.16         d4,  d5
        vadd.i16        d2,  d4,  d5
        vshl.i16        d3,  d2,  #2
        vrev64.16       d16, d16
        vsub.i16        d3,  d3,  d2
        vadd.i16        d16, d16, d0
        vshl.i16        d2,  d16, #4
        vsub.i16        d2,  d2,  d3
        vshl.i16        d3,  d4,  #3
        vext.16         q0,  q0,  q0,  #7
        vsub.i16        d6,  d5,  d3
        vmov.16         d0[0], r3
        vmul.i16        q0,  q0,  d4[0]
        vdup.16         q1,  d2[0]
        vdup.16         q2,  d4[0]
        vdup.16         q3,  d6[0]
        vshl.i16        q2,  q2,  #3
        vadd.i16        q1,  q1,  q0
        vadd.i16        q3,  q3,  q2
        mov             r3,  #8
1:
        vqshrun.s16     d0,  q1,  #5
        vadd.i16        q1,  q1,  q3
        vst1.8          {d0},     [r0,:64], r1
        subs            r3,  r3,  #1
        bne             1b
        bx              lr
endfunc

function ff_pred8x8_128_dc_neon, export=1
        vmov.i8         q0,  #128
        b               .L_pred8x8_dc_end
endfunc

function ff_pred8x8_top_dc_neon, export=1
        sub             r2,  r0,  r1
        vld1.8          {d0},     [r2,:64]
        vpaddl.u8       d0,  d0
        vpadd.u16       d0,  d0,  d0
        vrshrn.u16      d0,  q0,  #2
        vdup.8          d1,  d0[1]
        vdup.8          d0,  d0[0]
        vtrn.32         d0,  d1
        b               .L_pred8x8_dc_end
endfunc

function ff_pred8x8_left_dc_neon, export=1
        sub             r2,  r0,  #1
        ldcol.8         d0,  r2,  r1
        vpaddl.u8       d0,  d0
        vpadd.u16       d0,  d0,  d0
        vrshrn.u16      d0,  q0,  #2
        vdup.8          d1,  d0[1]
        vdup.8          d0,  d0[0]
        b               .L_pred8x8_dc_end
endfunc

function ff_pred8x8_dc_neon, export=1
        sub             r2,  r0,  r1
        vld1.8          {d0},     [r2,:64]
        sub             r2,  r0,  #1
        ldcol.8         d1,  r2,  r1
        vtrn.32         d0,  d1
        vpaddl.u8       q0,  q0
        vpadd.u16       d0,  d0,  d1
        vpadd.u16       d1,  d0,  d0
        vrshrn.u16      d2,  q0,  #3
        vrshrn.u16      d3,  q0,  #2
        vdup.8          d0,  d2[4]
        vdup.8          d1,  d3[3]
        vdup.8          d4,  d3[2]
        vdup.8          d5,  d2[5]
        vtrn.32         q0,  q2
.L_pred8x8_dc_end:
        mov             r3,  #4
        add             r2,  r0,  r1,  lsl #2
6:      vst1.8          {d0},     [r0,:64], r1
        vst1.8          {d1},     [r2,:64], r1
        subs            r3,  r3,  #1
        bne             6b
        bx              lr
endfunc

function ff_pred8x8_l0t_dc_neon, export=1
        sub             r2,  r0,  r1
        vld1.8          {d0},     [r2,:64]
        sub             r2,  r0,  #1
        ldcol.8         d1,  r2,  r1,  4
        vtrn.32         d0,  d1
        vpaddl.u8       q0,  q0
        vpadd.u16       d0,  d0,  d1
        vpadd.u16       d1,  d0,  d0
        vrshrn.u16      d2,  q0,  #3
        vrshrn.u16      d3,  q0,  #2
        vdup.8          d0,  d2[4]
        vdup.8          d1,  d3[0]
        vdup.8          q2,  d3[2]
        vtrn.32         q0,  q2
        b               .L_pred8x8_dc_end
endfunc

function ff_pred8x8_l00_dc_neon, export=1
        sub             r2,  r0,  #1
        ldcol.8         d0,  r2,  r1,  4
        vpaddl.u8       d0,  d0
        vpadd.u16       d0,  d0,  d0
        vrshrn.u16      d0,  q0,  #2
        vmov.i8         d1,  #128
        vdup.8          d0,  d0[0]
        b               .L_pred8x8_dc_end
endfunc

function ff_pred8x8_0lt_dc_neon, export=1
        sub             r2,  r0,  r1
        vld1.8          {d0},     [r2,:64]
        add             r2,  r0,  r1,  lsl #2
        sub             r2,  r2,  #1
        ldcol.8         d1,  r2,  r1,  4,  hi=1
        vtrn.32         d0,  d1
        vpaddl.u8       q0,  q0
        vpadd.u16       d0,  d0,  d1
        vpadd.u16       d1,  d0,  d0
        vrshrn.u16      d3,  q0,  #2
        vrshrn.u16      d2,  q0,  #3
        vdup.8          d0,  d3[0]
        vdup.8          d1,  d3[3]
        vdup.8          d4,  d3[2]
        vdup.8          d5,  d2[5]
        vtrn.32         q0,  q2
        b               .L_pred8x8_dc_end
endfunc

function ff_pred8x8_0l0_dc_neon, export=1
        add             r2,  r0,  r1,  lsl #2
        sub             r2,  r2,  #1
        ldcol.8         d1,  r2,  r1,  4
        vpaddl.u8       d2,  d1
        vpadd.u16       d2,  d2,  d2
        vrshrn.u16      d1,  q1,  #2
        vmov.i8         d0,  #128
        vdup.8          d1,  d1[0]
        b               .L_pred8x8_dc_end
endfunc
