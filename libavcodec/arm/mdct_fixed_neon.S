/*
 * Copyright (c) 2011 Mans Rullgard <mans@mansr.com>
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

.macro  prerot          dst, rt
        lsr             r3,  r6,  #2            @ n4
        add             \rt, r4,  r6,  lsr #1   @ revtab + n4
        add             r9,  r3,  r3,  lsl #1   @ n3
        add             r8,  r7,  r6            @ tcos + n4
        add             r3,  r2,  r6,  lsr #1   @ in + n4
        add             r9,  r2,  r9,  lsl #1   @ in + n3
        sub             r8,  r8,  #16
        sub             r10, r3,  #16
        sub             r11, r9,  #16
        mov             r12, #-16
1:
        vld2.16         {d0,d1},  [r9, :128]!
        vld2.16         {d2,d3},  [r11,:128], r12
        vld2.16         {d4,d5},  [r3, :128]!
        vld2.16         {d6,d7},  [r10,:128], r12
        vld2.16         {d16,d17},[r7, :128]!   @ cos, sin
        vld2.16         {d18,d19},[r8, :128], r12
        vrev64.16       q1,  q1
        vrev64.16       q3,  q3
        vrev64.16       q9,  q9
        vneg.s16        d0,  d0
        vneg.s16        d2,  d2
        vneg.s16        d16, d16
        vneg.s16        d18, d18
        vhsub.s16       d0,  d0,  d3            @ re
        vhsub.s16       d4,  d7,  d4            @ im
        vhsub.s16       d6,  d6,  d5
        vhsub.s16       d2,  d2,  d1
        vmull.s16       q10, d0,  d16
        vmlsl.s16       q10, d4,  d17
        vmull.s16       q11, d0,  d17
        vmlal.s16       q11, d4,  d16
        vmull.s16       q12, d6,  d18
        vmlsl.s16       q12, d2,  d19
        vmull.s16       q13, d6,  d19
        vmlal.s16       q13, d2,  d18
        vshrn.s32       d0,  q10, #15
        vshrn.s32       d1,  q11, #15
        vshrn.s32       d2,  q12, #15
        vshrn.s32       d3,  q13, #15
        vzip.16         d0,  d1
        vzip.16         d2,  d3
        ldrh            lr,  [r4], #2
        ldrh            r2,  [\rt, #-2]!
        add             lr,  \dst, lr,  lsl #2
        add             r2,  \dst, r2,  lsl #2
        vst1.32         {d0[0]},  [lr,:32]
        vst1.32         {d2[0]},  [r2,:32]
        ldrh            lr,  [r4], #2
        ldrh            r2,  [\rt, #-2]!
        add             lr,  \dst, lr,  lsl #2
        add             r2,  \dst, r2,  lsl #2
        vst1.32         {d0[1]},  [lr,:32]
        vst1.32         {d2[1]},  [r2,:32]
        ldrh            lr,  [r4], #2
        ldrh            r2,  [\rt, #-2]!
        add             lr,  \dst, lr,  lsl #2
        add             r2,  \dst, r2,  lsl #2
        vst1.32         {d1[0]},  [lr,:32]
        vst1.32         {d3[0]},  [r2,:32]
        ldrh            lr,  [r4], #2
        ldrh            r2,  [\rt, #-2]!
        add             lr,  \dst, lr,  lsl #2
        add             r2,  \dst, r2,  lsl #2
        vst1.32         {d1[1]},  [lr,:32]
        vst1.32         {d3[1]},  [r2,:32]
        subs            r6,  r6,  #32
        bgt             1b
.endm

function ff_mdct_fixed_calc_neon, export=1
        push            {r1,r4-r11,lr}

        ldr             r4,  [r0, #8]           @ revtab
        ldr             r6,  [r0, #16]          @ mdct_size; n
        ldr             r7,  [r0, #24]          @ tcos

        prerot          r1,  r5

        mov             r4,  r0
        bl              X(ff_fft_fixed_calc_neon)

        pop             {r5}
        mov             r12, #-16
        ldr             r6,  [r4, #16]          @ mdct_size; n
        ldr             r7,  [r4, #24]          @ tcos
        add             r5,  r5,  r6,  lsr #1
        add             r7,  r7,  r6,  lsr #1
        sub             r1,  r5,  #16
        sub             r2,  r7,  #16
1:
        vld2.16         {d4,d5},  [r7,:128]!
        vld2.16         {d6,d7},  [r2,:128], r12
        vld2.16         {d0,d1},  [r5,:128]
        vld2.16         {d2,d3},  [r1,:128]
        vrev64.16       q3,  q3
        vrev64.16       q1,  q1
        vneg.s16        q3,  q3
        vneg.s16        q2,  q2
        vmull.s16       q11, d2,  d6
        vmlal.s16       q11, d3,  d7
        vmull.s16       q8,  d0,  d5
        vmlsl.s16       q8,  d1,  d4
        vmull.s16       q9,  d0,  d4
        vmlal.s16       q9,  d1,  d5
        vmull.s16       q10, d2,  d7
        vmlsl.s16       q10, d3,  d6
        vshrn.s32       d0,  q11, #15
        vshrn.s32       d1,  q8,  #15
        vshrn.s32       d2,  q9,  #15
        vshrn.s32       d3,  q10, #15
        vrev64.16       q0,  q0
        vst2.16         {d2,d3},  [r5,:128]!
        vst2.16         {d0,d1},  [r1,:128], r12
        subs            r6,  r6,  #32
        bgt             1b

        pop             {r4-r11,pc}
endfunc

function ff_mdct_fixed_calcw_neon, export=1
        push            {r1,r4-r11,lr}

        ldrd            r4,  r5,  [r0, #8]      @ revtab, tmp_buf
        ldr             r6,  [r0, #16]          @ mdct_size; n
        ldr             r7,  [r0, #24]          @ tcos

        prerot          r5,  r1

        mov             r4,  r0
        mov             r1,  r5
        bl              X(ff_fft_fixed_calc_neon)

        pop             {r7}
        mov             r12, #-16
        ldr             r6,  [r4, #16]          @ mdct_size; n
        ldr             r9,  [r4, #24]          @ tcos
        add             r5,  r5,  r6,  lsr #1
        add             r7,  r7,  r6
        add             r9,  r9,  r6,  lsr #1
        sub             r3,  r5,  #16
        sub             r1,  r7,  #16
        sub             r2,  r9,  #16
1:
        vld2.16         {d4,d5},  [r9,:128]!
        vld2.16         {d6,d7},  [r2,:128], r12
        vld2.16         {d0,d1},  [r5,:128]!
        vld2.16         {d2,d3},  [r3,:128], r12
        vrev64.16       q3,  q3
        vrev64.16       q1,  q1
        vneg.s16        q3,  q3
        vneg.s16        q2,  q2
        vmull.s16       q8,  d2,  d6
        vmlal.s16       q8,  d3,  d7
        vmull.s16       q9,  d0,  d5
        vmlsl.s16       q9,  d1,  d4
        vmull.s16       q10, d0,  d4
        vmlal.s16       q10, d1,  d5
        vmull.s16       q11, d2,  d7
        vmlsl.s16       q11, d3,  d6
        vrev64.32       q8,  q8
        vrev64.32       q9,  q9
        vst2.32         {q10,q11},[r7,:128]!
        vst2.32         {d16,d18},[r1,:128], r12
        vst2.32         {d17,d19},[r1,:128], r12
        subs            r6,  r6,  #32
        bgt             1b

        pop             {r4-r11,pc}
endfunc
