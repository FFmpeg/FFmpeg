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

.macro  bflies          d0,  d1,  r0,  r1
        vrev64.32       \r0, \d1                @ t5, t6, t1, t2
        vhsub.s16       \r1, \d1, \r0           @ t1-t5, t2-t6, t5-t1, t6-t2
        vhadd.s16       \r0, \d1, \r0           @ t1+t5, t2+t6, t5+t1, t6+t2
        vext.16         \r1, \r1, \r1, #1       @ t2-t6, t5-t1, t6-t2, t1-t5
        vtrn.32         \r0, \r1                @ t1+t5, t2+t6, t2-t6, t5-t1
                                                @ t5,    t6,    t4,    t3
        vhsub.s16       \d1, \d0, \r0
        vhadd.s16       \d0, \d0, \r0
.endm

.macro  transform01     q0,  q1,  d3,  c0,  c1,  r0,  w0,  w1
        vrev32.16       \r0, \d3
        vmull.s16       \w0, \d3, \c0
        vmlal.s16       \w0, \r0, \c1
        vshrn.s32       \d3, \w0, #15
        bflies          \q0, \q1, \w0, \w1
.endm

.macro  transform2      d0,  d1,  d2,  d3,  q0,  q1,  c0,  c1,  c2,  c3, \
                        r0,  r1,  w0,  w1
        vrev32.16       \r0, \d1
        vrev32.16       \r1, \d3
        vmull.s16       \w0, \d1, \c0
        vmlal.s16       \w0, \r0, \c1
        vmull.s16       \w1, \d3, \c2
        vmlal.s16       \w1, \r1, \c3
        vshrn.s32       \d1, \w0, #15
        vshrn.s32       \d3, \w1, #15
        bflies          \q0, \q1, \w0, \w1
.endm

.macro  fft4            d0,  d1,  r0,  r1
        vhsub.s16       \r0, \d0, \d1           @ t3, t4, t8, t7
        vhsub.s16       \r1, \d1, \d0
        vhadd.s16       \d0, \d0, \d1           @ t1, t2, t6, t5
        vmov.i64        \d1, #0xffff00000000
        vbit            \r0, \r1, \d1
        vrev64.16       \r1, \r0                @ t7, t8, t4, t3
        vtrn.32         \r0, \r1                @ t3, t4, t7, t8
        vtrn.32         \d0, \r0                @ t1, t2, t3, t4, t6, t5, t8, t7
        vhsub.s16       \d1, \d0, \r0           @ r2, i2, r3, i1
        vhadd.s16       \d0, \d0, \r0           @ r0, i0, r1, i3
.endm

.macro  fft8            d0,  d1,  d2,  d3,  q0,  q1,  c0,  c1,  r0,  r1, w0, w1
        fft4            \d0, \d1, \r0, \r1
        vtrn.32         \d0, \d1                @ z0, z2, z1, z3
        vhadd.s16       \r0, \d2, \d3           @ t1, t2, t3, t4
        vhsub.s16       \d3, \d2, \d3           @ z5, z7
        vmov            \d2, \r0
        transform01     \q0, \q1, \d3, \c0, \c1, \r0, \w0, \w1
.endm

function fft4_neon
        vld1.16         {d0-d1},  [r0]
        fft4            d0,  d1,  d2,  d3
        vst1.16         {d0-d1},  [r0]
        bx              lr
endfunc

function fft8_neon
        vld1.16         {d0-d3},  [r0,:128]
        movrel          r1,  coefs
        vld1.16         {d30},    [r1,:64]
        vdup.16         d31, d30[0]
        fft8            d0,  d1,  d2,  d3,  q0,  q1,  d31, d30, d20, d21, q8, q9
        vtrn.32         d0,  d1
        vtrn.32         d2,  d3
        vst1.16         {d0-d3},  [r0,:128]
        bx              lr
endfunc

function fft16_neon
        vld1.16         {d0-d3},  [r0,:128]!
        vld1.16         {d4-d7},  [r0,:128]
        movrel          r1,  coefs
        sub             r0,  r0,  #32
        vld1.16         {d28-d31},[r1,:128]
        vdup.16         d31, d28[0]
        fft8            d0,  d1,  d2,  d3,  q0,  q1,  d31, d28, d20, d21, q8, q9
        vswp            d5,  d6
        fft4            q2,  q3,  q8,  q9
        vswp            d5,  d6
        vtrn.32         q0,  q1             @ z0, z4, z2, z6, z1, z5, z3, z7
        vtrn.32         q2,  q3             @ z8, z12,z10,z14,z9, z13,z11,z15
        vswp            d1,  d2
        vdup.16         d31, d28[0]
        transform01     q0,  q2,  d5,  d31, d28, d20, q8, q9
        vdup.16         d26, d29[0]
        vdup.16         d27, d30[0]
        transform2      d2,  d6,  d3,  d7,  q1,  q3,  d26, d30, d27, d29, \
                        d20, d21, q8,  q9
        vtrn.32         q0,  q1
        vtrn.32         q2,  q3
        vst1.16         {d0-d3},  [r0,:128]!
        vst1.16         {d4-d7},  [r0,:128]
        bx              lr
endfunc

function fft_pass_neon
        push            {r4,lr}
        movrel          lr,  coefs+24
        vld1.16         {d30},    [lr,:64]
        lsl             r12, r2,  #3
        vmov            d31, d30
        add             r3,  r1,  r2,  lsl #2
        mov             lr,  #-8
        sub             r3,  r3,  #2
        mov             r4,  r0
        vld1.16         {d27[]},  [r3,:16]
        sub             r3,  r3,  #6
        vld1.16         {q0},     [r4,:128], r12
        vld1.16         {q1},     [r4,:128], r12
        vld1.16         {q2},     [r4,:128], r12
        vld1.16         {q3},     [r4,:128], r12
        vld1.16         {d28},    [r1,:64]!
        vld1.16         {d29},    [r3,:64], lr
        vswp            d1,  d2
        vswp            d5,  d6
        vtrn.32         d0,  d1
        vtrn.32         d4,  d5
        vdup.16         d25, d28[1]
        vmul.s16        d27, d27, d31
        transform01     q0,  q2,  d5,  d25, d27, d20, q8,  q9
        b               2f
1:
        mov             r4,  r0
        vdup.16         d26, d29[0]
        vld1.16         {q0},     [r4,:128], r12
        vld1.16         {q1},     [r4,:128], r12
        vld1.16         {q2},     [r4,:128], r12
        vld1.16         {q3},     [r4,:128], r12
        vld1.16         {d28},    [r1,:64]!
        vld1.16         {d29},    [r3,:64], lr
        vswp            d1,  d2
        vswp            d5,  d6
        vtrn.32         d0,  d1
        vtrn.32         d4,  d5
        vdup.16         d24, d28[0]
        vdup.16         d25, d28[1]
        vdup.16         d27, d29[3]
        vmul.s16        q13, q13, q15
        transform2      d0,  d4,  d1,  d5,  q0,  q2,  d24, d26, d25, d27, \
                        d16, d17, q9,  q10
2:
        vtrn.32         d2,  d3
        vtrn.32         d6,  d7
        vdup.16         d24, d28[2]
        vdup.16         d26, d29[2]
        vdup.16         d25, d28[3]
        vdup.16         d27, d29[1]
        vmul.s16        q13, q13, q15
        transform2      d2,  d6,  d3,  d7,  q1,  q3,  d24, d26, d25, d27, \
                        d16, d17, q9,  q10
        vtrn.32         d0,  d1
        vtrn.32         d2,  d3
        vtrn.32         d4,  d5
        vtrn.32         d6,  d7
        vswp            d1,  d2
        vswp            d5,  d6
        mov             r4,  r0
        vst1.16         {q0},     [r4,:128], r12
        vst1.16         {q1},     [r4,:128], r12
        vst1.16         {q2},     [r4,:128], r12
        vst1.16         {q3},     [r4,:128], r12
        add             r0,  r0,  #16
        subs            r2,  r2,  #2
        bgt             1b
        pop             {r4,pc}
endfunc

#define F_SQRT1_2   23170
#define F_COS_16_1  30274
#define F_COS_16_3  12540

const   coefs, align=4
        .short          F_SQRT1_2, -F_SQRT1_2, -F_SQRT1_2,  F_SQRT1_2
        .short          F_COS_16_1,-F_COS_16_1,-F_COS_16_1, F_COS_16_1
        .short          F_COS_16_3,-F_COS_16_3,-F_COS_16_3, F_COS_16_3
        .short          1,         -1,         -1,          1
endconst

.macro  def_fft n, n2, n4
function fft\n\()_neon
        push            {r4, lr}
        mov             r4,  r0
        bl              fft\n2\()_neon
        add             r0,  r4,  #\n4*2*4
        bl              fft\n4\()_neon
        add             r0,  r4,  #\n4*3*4
        bl              fft\n4\()_neon
        mov             r0,  r4
        pop             {r4, lr}
        movrelx         r1,  X(ff_cos_\n\()_fixed)
        mov             r2,  #\n4/2
        b               fft_pass_neon
endfunc
.endm

        def_fft    32,    16,     8
        def_fft    64,    32,    16
        def_fft   128,    64,    32
        def_fft   256,   128,    64
        def_fft   512,   256,   128
        def_fft  1024,   512,   256
        def_fft  2048,  1024,   512
        def_fft  4096,  2048,  1024
        def_fft  8192,  4096,  2048
        def_fft 16384,  8192,  4096
        def_fft 32768, 16384,  8192
        def_fft 65536, 32768, 16384

function ff_fft_fixed_calc_neon, export=1
        ldr             r2,  [r0]
        sub             r2,  r2,  #2
        movrel          r3,  fft_fixed_tab_neon
        ldr             r3,  [r3, r2, lsl #2]
        mov             r0,  r1
        bx              r3
endfunc

const   fft_fixed_tab_neon
        .word fft4_neon
        .word fft8_neon
        .word fft16_neon
        .word fft32_neon
        .word fft64_neon
        .word fft128_neon
        .word fft256_neon
        .word fft512_neon
        .word fft1024_neon
        .word fft2048_neon
        .word fft4096_neon
        .word fft8192_neon
        .word fft16384_neon
        .word fft32768_neon
        .word fft65536_neon
endconst
