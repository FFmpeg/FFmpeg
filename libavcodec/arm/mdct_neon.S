/*
 * ARM NEON optimised MDCT
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

#define ff_fft_calc_neon X(ff_fft_calc_neon)

function ff_imdct_half_neon, export=1
        push            {r4-r8,lr}

        mov             r12, #1
        ldr             lr,  [r0, #20]          @ mdct_bits
        ldr             r4,  [r0, #24]          @ tcos
        ldr             r3,  [r0, #8]           @ revtab
        lsl             r12, r12, lr            @ n  = 1 << nbits
        lsr             lr,  r12, #2            @ n4 = n >> 2
        add             r7,  r2,  r12,  lsl #1
        mov             r12, #-16
        sub             r7,  r7,  #16

        vld2.32         {d16-d17},[r7,:128],r12 @ d16=x,n1 d17=x,n0
        vld2.32         {d0-d1},  [r2,:128]!    @ d0 =m0,x d1 =m1,x
        vrev64.32       d17, d17
        vld2.32         {d2,d3},  [r4,:128]!    @ d2=c0,c1 d3=s0,s2
        vmul.f32        d6,  d17, d2
        vmul.f32        d7,  d0,  d2
1:
        subs            lr,  lr,  #2
        ldr             r6,  [r3], #4
        vmul.f32        d4,  d0,  d3
        vmul.f32        d5,  d17, d3
        vsub.f32        d4,  d6,  d4
        vadd.f32        d5,  d5,  d7
        uxth            r8,  r6,  ror #16
        uxth            r6,  r6
        add             r8,  r1,  r8,  lsl #3
        add             r6,  r1,  r6,  lsl #3
        beq             1f
        vld2.32         {d16-d17},[r7,:128],r12
        vld2.32         {d0-d1},  [r2,:128]!
        vrev64.32       d17, d17
        vld2.32         {d2,d3},  [r4,:128]!    @ d2=c0,c1 d3=s0,s2
        vmul.f32        d6,  d17, d2
        vmul.f32        d7,  d0,  d2
        vst2.32         {d4[0],d5[0]}, [r6,:64]
        vst2.32         {d4[1],d5[1]}, [r8,:64]
        b               1b
1:
        vst2.32         {d4[0],d5[0]}, [r6,:64]
        vst2.32         {d4[1],d5[1]}, [r8,:64]

        mov             r4,  r0
        mov             r6,  r1
        bl              ff_fft_calc_neon

        mov             r12, #1
        ldr             lr,  [r4, #20]          @ mdct_bits
        ldr             r4,  [r4, #24]          @ tcos
        lsl             r12, r12, lr            @ n  = 1 << nbits
        lsr             lr,  r12, #3            @ n8 = n >> 3

        add             r4,  r4,  lr,  lsl #3
        add             r6,  r6,  lr,  lsl #3
        sub             r1,  r4,  #16
        sub             r3,  r6,  #16

        mov             r7,  #-16
        mov             r8,  r6
        mov             r0,  r3

        vld2.32         {d0-d1},  [r3,:128], r7 @ d0 =i1,r1 d1 =i0,r0
        vld2.32         {d20-d21},[r6,:128]!    @ d20=i2,r2 d21=i3,r3
        vld2.32         {d16,d18},[r1,:128], r7 @ d16=c1,c0 d18=s1,s0
1:
        subs            lr,  lr,  #2
        vmul.f32        d7,  d0,  d18
        vld2.32         {d17,d19},[r4,:128]!    @ d17=c2,c3 d19=s2,s3
        vmul.f32        d4,  d1,  d18
        vmul.f32        d5,  d21, d19
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
        vld2.32         {d0-d1},  [r3,:128], r7
        vld2.32         {d20-d21},[r6,:128]!
        vld2.32         {d16,d18},[r1,:128], r7 @ d16=c1,c0 d18=s1,s0
        vrev64.32       q3,  q3
        vst2.32         {d4,d6},  [r0,:128], r7
        vst2.32         {d5,d7},  [r8,:128]!
        b               1b
1:
        vrev64.32       q3,  q3
        vst2.32         {d4,d6},  [r0,:128]
        vst2.32         {d5,d7},  [r8,:128]

        pop             {r4-r8,pc}
endfunc

function ff_imdct_calc_neon, export=1
        push            {r4-r6,lr}

        ldr             r3,  [r0, #20]
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
endfunc

function ff_mdct_calc_neon, export=1
        push            {r4-r10,lr}

        mov             r12, #1
        ldr             lr,  [r0, #20]          @ mdct_bits
        ldr             r4,  [r0, #24]          @ tcos
        ldr             r3,  [r0, #8]           @ revtab
        lsl             lr,  r12, lr            @ n  = 1 << nbits
        add             r7,  r2,  lr            @ in4u
        sub             r9,  r7,  #16           @ in4d
        add             r2,  r7,  lr,  lsl #1   @ in3u
        add             r8,  r9,  lr,  lsl #1   @ in3d
        add             r5,  r4,  lr,  lsl #1
        sub             r5,  r5,  #16
        sub             r3,  r3,  #4
        mov             r12, #-16

        vld2.32         {d16,d18},[r9,:128],r12 @ in0u0,in0u1 in4d1,in4d0
        vld2.32         {d17,d19},[r8,:128],r12 @ in2u0,in2u1 in3d1,in3d0
        vld2.32         {d0, d2}, [r7,:128]!    @ in4u0,in4u1 in2d1,in2d0
        vrev64.32       q9,  q9                 @ in4d0,in4d1 in3d0,in3d1
        vld2.32         {d1, d3}, [r2,:128]!    @ in3u0,in3u1 in1d1,in1d0
        vsub.f32        d0,  d18, d0            @ in4d-in4u      I
        vld2.32         {d20,d21},[r4,:128]!    @ c0,c1 s0,s1
        vrev64.32       q1,  q1                 @ in2d0,in2d1 in1d0,in1d1
        vld2.32         {d30,d31},[r5,:128],r12 @ c2,c3 s2,s3
        vadd.f32        d1,  d1,  d19           @ in3u+in3d     -R
        vsub.f32        d16, d16, d2            @ in0u-in2d      R
        vadd.f32        d17, d17, d3            @ in2u+in1d     -I
1:
        vmul.f32        d7,  d0,  d21           @  I*s
A       ldr             r10, [r3, lr, lsr #1]
T       lsr             r10, lr,  #1
T       ldr             r10, [r3, r10]
        vmul.f32        d6,  d1,  d20           @ -R*c
        ldr             r6,  [r3, #4]!
        vmul.f32        d4,  d1,  d21           @ -R*s
        vmul.f32        d5,  d0,  d20           @  I*c
        vmul.f32        d24, d16, d30           @  R*c
        vmul.f32        d25, d17, d31           @ -I*s
        vmul.f32        d22, d16, d31           @  R*s
        vmul.f32        d23, d17, d30           @  I*c
        subs            lr,  lr,  #16
        vsub.f32        d6,  d6,  d7            @ -R*c-I*s
        vadd.f32        d7,  d4,  d5            @ -R*s+I*c
        vsub.f32        d24, d25, d24           @ I*s-R*c
        vadd.f32        d25, d22, d23           @ R*s-I*c
        beq             1f
        mov             r12, #-16
        vld2.32         {d16,d18},[r9,:128],r12 @ in0u0,in0u1 in4d1,in4d0
        vld2.32         {d17,d19},[r8,:128],r12 @ in2u0,in2u1 in3d1,in3d0
        vneg.f32        d7,  d7                 @  R*s-I*c
        vld2.32         {d0, d2}, [r7,:128]!    @ in4u0,in4u1 in2d1,in2d0
        vrev64.32       q9,  q9                 @ in4d0,in4d1 in3d0,in3d1
        vld2.32         {d1, d3}, [r2,:128]!    @ in3u0,in3u1 in1d1,in1d0
        vsub.f32        d0,  d18, d0            @ in4d-in4u      I
        vld2.32         {d20,d21},[r4,:128]!    @ c0,c1 s0,s1
        vrev64.32       q1,  q1                 @ in2d0,in2d1 in1d0,in1d1
        vld2.32         {d30,d31},[r5,:128],r12 @ c2,c3 s2,s3
        vadd.f32        d1,  d1,  d19           @ in3u+in3d     -R
        vsub.f32        d16, d16, d2            @ in0u-in2d      R
        vadd.f32        d17, d17, d3            @ in2u+in1d     -I
        uxth            r12, r6,  ror #16
        uxth            r6,  r6
        add             r12, r1,  r12, lsl #3
        add             r6,  r1,  r6,  lsl #3
        vst2.32         {d6[0],d7[0]}, [r6,:64]
        vst2.32         {d6[1],d7[1]}, [r12,:64]
        uxth            r6,  r10, ror #16
        uxth            r10, r10
        add             r6 , r1,  r6,  lsl #3
        add             r10, r1,  r10, lsl #3
        vst2.32         {d24[0],d25[0]},[r10,:64]
        vst2.32         {d24[1],d25[1]},[r6,:64]
        b               1b
1:
        vneg.f32        d7,  d7                 @  R*s-I*c
        uxth            r12, r6,  ror #16
        uxth            r6,  r6
        add             r12, r1,  r12, lsl #3
        add             r6,  r1,  r6,  lsl #3
        vst2.32         {d6[0],d7[0]}, [r6,:64]
        vst2.32         {d6[1],d7[1]}, [r12,:64]
        uxth            r6,  r10, ror #16
        uxth            r10, r10
        add             r6 , r1,  r6,  lsl #3
        add             r10, r1,  r10, lsl #3
        vst2.32         {d24[0],d25[0]},[r10,:64]
        vst2.32         {d24[1],d25[1]},[r6,:64]

        mov             r4,  r0
        mov             r6,  r1
        bl              ff_fft_calc_neon

        mov             r12, #1
        ldr             lr,  [r4, #20]          @ mdct_bits
        ldr             r4,  [r4, #24]          @ tcos
        lsl             r12, r12, lr            @ n  = 1 << nbits
        lsr             lr,  r12, #3            @ n8 = n >> 3

        add             r4,  r4,  lr,  lsl #3
        add             r6,  r6,  lr,  lsl #3
        sub             r1,  r4,  #16
        sub             r3,  r6,  #16

        mov             r7,  #-16
        mov             r8,  r6
        mov             r0,  r3

        vld2.32         {d0-d1},  [r3,:128], r7 @ d0 =r1,i1 d1 =r0,i0
        vld2.32         {d20-d21},[r6,:128]!    @ d20=r2,i2 d21=r3,i3
        vld2.32         {d16,d18},[r1,:128], r7 @ c1,c0 s1,s0
1:
        subs            lr,  lr,  #2
        vmul.f32        d7,  d0,  d18           @ r1*s1,r0*s0
        vld2.32         {d17,d19},[r4,:128]!    @ c2,c3 s2,s3
        vmul.f32        d4,  d1,  d18           @ i1*s1,i0*s0
        vmul.f32        d5,  d21, d19           @ i2*s2,i3*s3
        vmul.f32        d6,  d20, d19           @ r2*s2,r3*s3
        vmul.f32        d24, d0,  d16           @ r1*c1,r0*c0
        vmul.f32        d25, d20, d17           @ r2*c2,r3*c3
        vmul.f32        d22, d21, d17           @ i2*c2,i3*c3
        vmul.f32        d23, d1,  d16           @ i1*c1,i0*c0
        vadd.f32        d4,  d4,  d24           @ i1*s1+r1*c1,i0*s0+r0*c0
        vadd.f32        d5,  d5,  d25           @ i2*s2+r2*c2,i3*s3+r3*c3
        vsub.f32        d6,  d22, d6            @ i2*c2-r2*s2,i3*c3-r3*s3
        vsub.f32        d7,  d23, d7            @ i1*c1-r1*s1,i0*c0-r0*s0
        vneg.f32        q2,  q2
        beq             1f
        vld2.32         {d0-d1},  [r3,:128], r7
        vld2.32         {d20-d21},[r6,:128]!
        vld2.32         {d16,d18},[r1,:128], r7 @ c1,c0 s1,s0
        vrev64.32       q3,  q3
        vst2.32         {d4,d6},  [r0,:128], r7
        vst2.32         {d5,d7},  [r8,:128]!
        b               1b
1:
        vrev64.32       q3,  q3
        vst2.32         {d4,d6},  [r0,:128]
        vst2.32         {d5,d7},  [r8,:128]

        pop             {r4-r10,pc}
endfunc
