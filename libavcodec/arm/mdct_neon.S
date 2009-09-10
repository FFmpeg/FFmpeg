/*
 * ARM NEON optimised MDCT
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

        .fpu neon
        .text

function ff_imdct_half_neon, export=1
        push            {r4-r8,lr}

        mov             r12, #1
        ldr             lr,  [r0, #4]           @ nbits
        ldr             r4,  [r0, #8]           @ tcos
        ldr             r5,  [r0, #12]          @ tsin
        ldr             r3,  [r0, #24]          @ revtab
        lsl             r12, r12, lr            @ n  = 1 << nbits
        lsr             lr,  r12, #2            @ n4 = n >> 2
        add             r7,  r2,  r12,  lsl #1
        mov             r12,  #-16
        sub             r7,  r7,  #16

        vld1.32         {d16-d17},[r7,:128],r12 @ d16=x,n1 d17=x,n0
        vld1.32         {d0-d1},  [r2,:128]!    @ d0 =m0,x d1 =m1,x
        vld1.32         {d2},     [r4,:64]!     @ d2=c0,c1
        vld1.32         {d3},     [r5,:64]!     @ d3=s0,s1
        vuzp.32         d17, d16
        vuzp.32         d0,  d1
        vmul.f32        d6,  d16, d2
        vmul.f32        d7,  d0,  d2
1:
        subs            lr,  lr,  #2
        ldr             r6,  [r3], #4
        vmul.f32        d4,  d0,  d3
        vmul.f32        d5,  d16, d3
        vsub.f32        d4,  d6,  d4
        vadd.f32        d5,  d5,  d7
        uxtah           r8,  r1,  r6,  ror #16
        uxtah           r6,  r1,  r6
        beq             1f
        vld1.32         {d16-d17},[r7,:128],r12
        vld1.32         {d0-d1},  [r2,:128]!
        vuzp.32         d17, d16
        vld1.32         {d2},     [r4,:64]!
        vuzp.32         d0,  d1
        vmul.f32        d6,  d16, d2
        vld1.32         {d3},     [r5,:64]!
        vmul.f32        d7,  d0,  d2
        vst2.32         {d4[0],d5[0]}, [r6,:64]
        vst2.32         {d4[1],d5[1]}, [r8,:64]
        b               1b
1:
        vst2.32         {d4[0],d5[0]}, [r6,:64]
        vst2.32         {d4[1],d5[1]}, [r8,:64]

        mov             r4,  r0
        mov             r6,  r1
        add             r0,  r0,  #16
        bl              ff_fft_calc_neon

        mov             r12, #1
        ldr             lr,  [r4, #4]           @ nbits
        ldr             r5,  [r4, #12]          @ tsin
        ldr             r4,  [r4, #8]           @ tcos
        lsl             r12, r12, lr            @ n  = 1 << nbits
        lsr             lr,  r12, #3            @ n8 = n >> 3

        add             r4,  r4,  lr,  lsl #2
        add             r5,  r5,  lr,  lsl #2
        add             r6,  r6,  lr,  lsl #3
        sub             r1,  r4,  #8
        sub             r2,  r5,  #8
        sub             r3,  r6,  #16

        mov             r7,  #-16
        mov             r12, #-8
        mov             r8,  r6
        mov             r0,  r3

        vld1.32         {d0-d1},  [r3,:128], r7 @ d0 =i1,r1 d1 =i0,r0
        vld1.32         {d20-d21},[r6,:128]!    @ d20=i2,r2 d21=i3,r3
        vld1.32         {d18},    [r2,:64], r12 @ d18=s1,s0
        vuzp.32         d20, d21
        vuzp.32         d0,  d1
1:
        subs            lr,  lr,  #2
        vmul.f32        d7,  d0,  d18
        vld1.32         {d19},    [r5,:64]!     @ d19=s2,s3
        vmul.f32        d4,  d1,  d18
        vld1.32         {d16},    [r1,:64], r12 @ d16=c1,c0
        vmul.f32        d5,  d21, d19
        vld1.32         {d17},    [r4,:64]!     @ d17=c2,c3
        vmul.f32        d6,  d20, d19
        vmul.f32        d22, d1,  d16
        vmul.f32        d23, d21, d17
        vmul.f32        d24, d0,  d16
        vmul.f32        d25, d20, d17
        vadd.f32        d7,  d7,  d22
        vadd.f32        d6,  d6,  d23
        vsub.f32        d4,  d4,  d24
        vsub.f32        d5,  d5,  d25
        beq             1f
        vld1.32         {d0-d1},  [r3,:128], r7
        vld1.32         {d20-d21},[r6,:128]!
        vld1.32         {d18},    [r2,:64], r12
        vuzp.32         d20, d21
        vuzp.32         d0,  d1
        vrev64.32       q3,  q3
        vtrn.32         d4,  d6
        vtrn.32         d5,  d7
        vswp            d5,  d6
        vst1.32         {d4-d5},  [r0,:128], r7
        vst1.32         {d6-d7},  [r8,:128]!
        b               1b
1:
        vrev64.32       q3,  q3
        vtrn.32         d4,  d6
        vtrn.32         d5,  d7
        vswp            d5,  d6
        vst1.32         {d4-d5},  [r0,:128]
        vst1.32         {d6-d7},  [r8,:128]

        pop             {r4-r8,pc}
.endfunc

function ff_imdct_calc_neon, export=1
        push            {r4-r6,lr}

        ldr             r3,  [r0, #4]
        mov             r4,  #1
        mov             r5,  r1
        lsl             r4,  r4,  r3
        add             r1,  r1,  r4

        bl              ff_imdct_half_neon

        add             r0,  r5,  r4,  lsl #2
        add             r1,  r5,  r4,  lsl #1
        sub             r0,  r0,  #8
        sub             r2,  r1,  #16
        mov             r3,  #-16
        mov             r6,  #-8
        vmov.i32        d30, #1<<31
1:
        vld1.32         {d0-d1},  [r2,:128], r3
        pld             [r0, #-16]
        vrev64.32       q0,  q0
        vld1.32         {d2-d3},  [r1,:128]!
        veor            d4,  d1,  d30
        pld             [r2, #-16]
        vrev64.32       q1,  q1
        veor            d5,  d0,  d30
        vst1.32         {d2},     [r0,:64], r6
        vst1.32         {d3},     [r0,:64], r6
        vst1.32         {d4-d5},  [r5,:128]!
        subs            r4,  r4,  #16
        bgt             1b

        pop             {r4-r6,pc}
.endfunc
