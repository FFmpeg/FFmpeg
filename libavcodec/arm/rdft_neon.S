/*
 * ARM NEON optimised RDFT
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
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

#include "asm.S"

        preserve8

function ff_rdft_calc_neon, export=1
        push            {r4-r8,lr}

        ldr             r6,  [r0, #4]           @ inverse
        mov             r4,  r0
        mov             r5,  r1

        lsls            r6,  r6,  #31
        bne             1f
        add             r0,  r4,  #20
        bl              X(ff_fft_permute_neon)
        add             r0,  r4,  #20
        mov             r1,  r5
        bl              X(ff_fft_calc_neon)
1:
        ldr             r12, [r4, #0]           @ nbits
        mov             r2,  #1
        lsl             r12, r2,  r12
        add             r0,  r5,  #8
        add             r1,  r5,  r12, lsl #2
        lsr             r12, r12, #2
        ldr             r2,  [r4, #12]          @ tcos
        sub             r12, r12, #2
        ldr             r3,  [r4, #16]          @ tsin
        mov             r7,  r0
        sub             r1,  r1,  #8
        mov             lr,  r1
        mov             r8,  #-8
        vld1.32         {d0},     [r0,:64]!     @ d1[0,1]
        vld1.32         {d1},     [r1,:64], r8  @ d2[0,1]
        vld1.32         {d4},     [r2,:64]!     @ tcos[i]
        vld1.32         {d5},     [r3,:64]!     @ tsin[i]
        vmov.f32        d18, #0.5               @ k1
        vdup.32         d19, r6
        pld             [r0, #32]
        veor            d19, d18, d19           @ k2
        vmov.i32        d16, #0
        vmov.i32        d17, #1<<31
        pld             [r1, #-32]
        vtrn.32         d16, d17
        pld             [r2, #32]
        vrev64.32       d16, d16                @ d16=1,0 d17=0,1
        pld             [r3, #32]
2:
        veor            q1,  q0,  q8            @ -d1[0],d1[1], d2[0],-d2[1]
        vld1.32         {d24},    [r0,:64]!     @  d1[0,1]
        vadd.f32        d0,  d0,  d3            @  d1[0]+d2[0], d1[1]-d2[1]
        vld1.32         {d25},    [r1,:64], r8  @  d2[0,1]
        vadd.f32        d1,  d2,  d1            @ -d1[0]+d2[0], d1[1]+d2[1]
        veor            q3,  q12, q8            @ -d1[0],d1[1], d2[0],-d2[1]
        pld             [r0, #32]
        vmul.f32        q10, q0,  q9            @  ev.re, ev.im, od.im, od.re
        pld             [r1, #-32]
        vadd.f32        d0,  d24, d7            @  d1[0]+d2[0], d1[1]-d2[1]
        vadd.f32        d1,  d6,  d25           @ -d1[0]+d2[0], d1[1]+d2[1]
        vmul.f32        q11, q0,  q9            @  ev.re, ev.im, od.im, od.re
        veor            d7,  d21, d16           @ -od.im, od.re
        vrev64.32       d3,  d21                @  od.re, od.im
        veor            d6,  d20, d17           @  ev.re,-ev.im
        veor            d2,  d3,  d16           @ -od.re, od.im
        vmla.f32        d20, d3,  d4[1]
        vmla.f32        d20, d7,  d5[1]
        vmla.f32        d6,  d2,  d4[1]
        vmla.f32        d6,  d21, d5[1]
        vld1.32         {d4},     [r2,:64]!     @  tcos[i]
        veor            d7,  d23, d16           @ -od.im, od.re
        vld1.32         {d5},     [r3,:64]!     @  tsin[i]
        veor            d24, d22, d17           @  ev.re,-ev.im
        vrev64.32       d3,  d23                @  od.re, od.im
        pld             [r2, #32]
        veor            d2,  d3,  d16           @ -od.re, od.im
        pld             [r3, #32]
        vmla.f32        d22, d3,  d4[0]
        vmla.f32        d22, d7,  d5[0]
        vmla.f32        d24, d2,  d4[0]
        vmla.f32        d24, d23, d5[0]
        vld1.32         {d0},     [r0,:64]!     @  d1[0,1]
        vld1.32         {d1},     [r1,:64], r8  @  d2[0,1]
        vst1.32         {d20},    [r7,:64]!
        vst1.32         {d6},     [lr,:64], r8
        vst1.32         {d22},    [r7,:64]!
        vst1.32         {d24},    [lr,:64], r8
        subs            r12, r12, #2
        bgt             2b

        veor            q1,  q0,  q8            @ -d1[0],d1[1], d2[0],-d2[1]
        vadd.f32        d0,  d0,  d3            @  d1[0]+d2[0], d1[1]-d2[1]
        vadd.f32        d1,  d2,  d1            @ -d1[0]+d2[0], d1[1]+d2[1]
        ldr             r2,  [r4, #8]           @  sign_convention
        vmul.f32        q10, q0,  q9            @  ev.re, ev.im, od.im, od.re
        add             r0,  r0,  #4
        bfc             r2,  #0,  #31
        vld1.32         {d0[0]},  [r0,:32]
        veor            d7,  d21, d16           @ -od.im, od.re
        vrev64.32       d3,  d21                @  od.re, od.im
        veor            d6,  d20, d17           @  ev.re,-ev.im
        vld1.32         {d22},    [r5,:64]
        vdup.32         d1,  r2
        vmov            d23, d22
        veor            d2,  d3,  d16           @ -od.re, od.im
        vtrn.32         d22, d23
        veor            d0,  d0,  d1
        veor            d23, d23, d17
        vmla.f32        d20, d3,  d4[1]
        vmla.f32        d20, d7,  d5[1]
        vmla.f32        d6,  d2,  d4[1]
        vmla.f32        d6,  d21, d5[1]
        vadd.f32        d22, d22, d23
        vst1.32         {d20},    [r7,:64]
        vst1.32         {d6},     [lr,:64]
        vst1.32         {d0[0]},  [r0,:32]
        vst1.32         {d22},    [r5,:64]

        cmp             r6,  #0
        it              eq
        popeq           {r4-r8,pc}

        vmul.f32        d22, d22, d18
        vst1.32         {d22},    [r5,:64]
        add             r0,  r4,  #20
        mov             r1,  r5
        bl              X(ff_fft_permute_neon)
        add             r0,  r4,  #20
        mov             r1,  r5
        pop             {r4-r8,lr}
        b               X(ff_fft_calc_neon)
endfunc
