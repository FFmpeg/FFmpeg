/*
 * Copyright (c) 2012 Mans Rullgard
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

function ff_sbr_sum64x5_neon, export=1
        push            {lr}
        add             r1,  r0,  # 64*4
        add             r2,  r0,  #128*4
        add             r3,  r0,  #192*4
        add             lr,  r0,  #256*4
        mov             r12, #64
1:
        vld1.32         {q0},     [r0,:128]
        vld1.32         {q1},     [r1,:128]!
        vadd.f32        q0,  q0,  q1
        vld1.32         {q2},     [r2,:128]!
        vadd.f32        q0,  q0,  q2
        vld1.32         {q3},     [r3,:128]!
        vadd.f32        q0,  q0,  q3
        vld1.32         {q8},     [lr,:128]!
        vadd.f32        q0,  q0,  q8
        vst1.32         {q0},     [r0,:128]!
        subs            r12, #4
        bgt             1b
        pop             {pc}
endfunc

function ff_sbr_sum_square_neon, export=1
        vmov.f32        q0,  #0.0
1:
        vld1.32         {q1},     [r0,:128]!
        vmla.f32        q0,  q1,  q1
        subs            r1,  r1,  #2
        bgt             1b
        vadd.f32        d0,  d0,  d1
        vpadd.f32       d0,  d0,  d0
NOVFP   vmov.32         r0,  d0[0]
        bx              lr
endfunc

function ff_sbr_neg_odd_64_neon, export=1
        mov             r1,  r0
        vmov.i32        q8,  #1<<31
        vld2.32         {q0,q1},  [r0,:128]!
        veor            q1,  q1,  q8
        vld2.32         {q2,q3},  [r0,:128]!
    .rept 3
        vst2.32         {q0,q1},  [r1,:128]!
        veor            q3,  q3,  q8
        vld2.32         {q0,q1},  [r0,:128]!
        vst2.32         {q2,q3},  [r1,:128]!
        veor            q1,  q1,  q8
        vld2.32         {q2,q3},  [r0,:128]!
    .endr
        veor            q3,  q3,  q8
        vst2.32         {q0,q1},  [r1,:128]!
        vst2.32         {q2,q3},  [r1,:128]!
        bx              lr
endfunc

function ff_sbr_qmf_pre_shuffle_neon, export=1
        add             r1,  r0,  #60*4
        add             r2,  r0,  #64*4
        vld1.32         {d0},     [r0,:64]!
        vst1.32         {d0},     [r2,:64]!
        mov             r3,  #-16
        mov             r12, #24
        vmov.i32        q8,  #1<<31
        vld1.32         {q0},     [r1,:128], r3
        vld1.32         {d2},     [r0,:64]!
1:
        vld1.32         {d3,d4},  [r0,:128]!
        vrev64.32       q0,  q0
        vld1.32         {q9},     [r1,:128], r3
        veor            q0,  q0,  q8
        vld1.32         {d5,d6},  [r0,:128]!
        vswp            d0,  d1
        vrev64.32       q9,  q9
        vst2.32         {q0,q1},  [r2,:64]!
        vmov            q10, q2
        veor            q9,  q9,  q8
        vmov            d2,  d6
        vswp            d18, d19
        vld1.32         {q0},     [r1,:128], r3
        vst2.32         {q9,q10}, [r2,:64]!
        subs            r12, r12, #8
        bgt             1b
        vld1.32         {d3,d4},  [r0,:128]!
        vrev64.32       q0,  q0
        vld1.32         {q9},     [r1,:128], r3
        veor            q0,  q0,  q8
        vld1.32         {d5},     [r0,:64]!
        vswp            d0,  d1
        vrev64.32       q9,  q9
        vst2.32         {q0,q1},  [r2,:64]!
        vswp            d4,  d5
        veor            q1,  q9,  q8
        vst2.32         {d3,d5},  [r2,:64]!
        vst2.32         {d2[0],d4[0]}, [r2,:64]!
        bx              lr
endfunc

function ff_sbr_qmf_post_shuffle_neon, export=1
        add             r2,  r1,  #60*4
        mov             r3,  #-16
        mov             r12, #32
        vmov.i32        q8,  #1<<31
        vld1.32         {q0},     [r2,:128], r3
        vld1.32         {q1},     [r1,:128]!
1:
        pld             [r2, #-32]
        vrev64.32       q0,  q0
        vswp            d2,  d3
        veor            q0,  q0,  q8
        vld1.32         {q2},     [r2,:128], r3
        vld1.32         {q3},     [r1,:128]!
        vst2.32         {d1,d3},  [r0,:128]!
        vst2.32         {d0,d2},  [r0,:128]!
        pld             [r2, #-32]
        vrev64.32       q2,  q2
        vswp            d6,  d7
        veor            q2,  q2,  q8
        vld1.32         {q0},     [r2,:128], r3
        vld1.32         {q1},     [r1,:128]!
        vst2.32         {d5,d7},  [r0,:128]!
        vst2.32         {d4,d6},  [r0,:128]!
        subs            r12, r12, #8
        bgt             1b
        bx              lr
endfunc

function ff_sbr_qmf_deint_neg_neon, export=1
        add             r1,  r1,  #60*4
        add             r2,  r0,  #62*4
        mov             r3,  #-16
        mov             r12, #32
        vmov.i32        d2,  #1<<31
1:
        vld2.32         {d0,d1},  [r1,:128], r3
        veor            d0,  d0,  d2
        vrev64.32       d1,  d1
        vst1.32         {d0},     [r2,:64]
        vst1.32         {d1},     [r0,:64]!
        sub             r2,  r2,  #8
        subs            r12, r12, #2
        bgt             1b
        bx              lr
endfunc

function ff_sbr_qmf_deint_bfly_neon, export=1
        push            {lr}
        add             r2,  r2,  #60*4
        add             r3,  r0,  #124*4
        mov             r12, #64
        mov             lr,  #-16
1:
        vld1.32         {q0},     [r1,:128]!
        vld1.32         {q1},     [r2,:128], lr
        vrev64.32       q2,  q0
        vrev64.32       q3,  q1
        vadd.f32        d3,  d4,  d3
        vadd.f32        d2,  d5,  d2
        vsub.f32        d0,  d0,  d7
        vsub.f32        d1,  d1,  d6
        vst1.32         {q1},     [r3,:128], lr
        vst1.32         {q0},     [r0,:128]!
        subs            r12, r12, #4
        bgt             1b
        pop             {pc}
endfunc

function ff_sbr_hf_g_filt_neon, export=1
        ldr             r12, [sp]
        add             r1,  r1,  r12, lsl #3
        mov             r12, #40*2*4
        sub             r3,  r3,  #1
        vld2.32         {d2[],d3[]},[r2,:64]!
        vld1.32         {d0},     [r1,:64], r12
1:
        vld1.32         {d1},     [r1,:64], r12
        vmul.f32        q3,  q0,  q1
        vld2.32         {d2[],d3[]},[r2,:64]!
        vld1.32         {d0},     [r1,:64], r12
        vst1.32         {q3},     [r0,:64]!
        subs            r3,  r3,  #2
        bgt             1b
        it              lt
        bxlt            lr
        vmul.f32        d0,  d0,  d2
        vst1.32         {d0},     [r0,:64]!
        bx              lr
endfunc

function ff_sbr_hf_gen_neon, export=1
NOVFP   vld1.32         {d1[]},   [sp,:32]
VFP     vdup.32         d1,  d0[0]
        vmul.f32        d0,  d1,  d1
        vld1.32         {d3},     [r2,:64]
        vld1.32         {d2},     [r3,:64]
        vmul.f32        q0,  q0,  q1
        ldrd            r2,  r3,  [sp, #4*!HAVE_VFP_ARGS]
        vtrn.32         d0,  d1
        vneg.f32        d18, d1
        vtrn.32         d18, d1
        add             r0,  r0,  r2,  lsl #3
        add             r1,  r1,  r2,  lsl #3
        sub             r1,  r1,  #2*8
        sub             r3,  r3,  r2
        vld1.32         {q1},     [r1,:128]!
1:
        vld1.32         {q3},     [r1,:128]!
        vrev64.32       q2,  q1
        vmov            q8,  q3
        vrev64.32       d20, d3
        vrev64.32       d21, d6
        vmla.f32        q3,  q1,  d0[0]
        vmla.f32        d6,  d4,  d18
        vmla.f32        d7,  d20, d18
        vmla.f32        d6,  d3,  d0[1]
        vmla.f32        d7,  d16, d0[1]
        vmla.f32        d6,  d5,  d1
        vmla.f32        d7,  d21, d1
        vmov            q1,  q8
        vst1.32         {q3},     [r0,:128]!
        subs            r3,  r3,  #2
        bgt             1b
        bx              lr
endfunc

function ff_sbr_autocorrelate_neon, export=1
        vld1.32         {q0},     [r0,:128]!
        vmov.f32        q1,  #0.0
        vmov.f32        q3,  #0.0
        vmov.f32        d20, #0.0
        vmul.f32        d21, d1,  d1
        vmov            q8,  q0
        vmov            q11, q0
        mov             r12, #36
1:
        vld1.32         {q2},     [r0,:128]!
        vrev64.32       q12, q2
        vmla.f32        q10, q2,  q2
        vmla.f32        d2,  d1,  d4
        vmla.f32        d3,  d1,  d24
        vmla.f32        d6,  d0,  d4
        vmla.f32        d7,  d0,  d24
        vmla.f32        d2,  d4,  d5
        vmla.f32        d3,  d4,  d25
        vmla.f32        d6,  d1,  d5
        vmla.f32        d7,  d1,  d25
        vmov            q0,  q2
        subs            r12, r12, #2
        bgt             1b
        vld1.32         {q2},     [r0,:128]!
        vrev64.32       q12, q2
        vmla.f32        d2,  d1,  d4
        vmla.f32        d3,  d1,  d24
        vmla.f32        d6,  d0,  d4
        vmla.f32        d7,  d0,  d24
        vadd.f32        d20, d20, d21
        vrev64.32       d18, d17
        vmla.f32        d6,  d1,  d5
        vmla.f32        d7,  d1,  d25
        vmov            q0,  q1
        vmla.f32        d0,  d16, d17
        vmla.f32        d1,  d16, d18
        vmla.f32        d2,  d4,  d5
        vmla.f32        d3,  d4,  d25
        vneg.f32        s15, s15
        vmov            d21, d20
        vpadd.f32       d0,  d0,  d2
        vpadd.f32       d7,  d6,  d7
        vtrn.32         d1,  d3
        vsub.f32        d6,  d1,  d3
        vmla.f32        d20, d22, d22
        vmla.f32        d21, d4,  d4
        vtrn.32         d0,  d6
        vpadd.f32       d20, d20, d21
        vst1.32         {q3},     [r1,:128]!
        vst1.32         {d20[1]}, [r1,:32]
        add             r1,  r1,  #2*4
        vst1.32         {d0},     [r1,:64]
        add             r1,  r1,  #4*4
        vst1.32         {d20[0]}, [r1,:32]
        bx              lr
endfunc

function ff_sbr_hf_apply_noise_0_neon, export=1
        vmov.i32        d3,  #0
.Lhf_apply_noise_0:
        push            {r4,lr}
        movrelx         r4,  X(ff_sbr_noise_table)
        ldr             r12, [sp, #12]
        add             r3,  r3,  #1
        bfc             r3,  #9,  #23
        sub             r12, r12, #1
1:
        add             lr,  r4,  r3,  lsl #3
        vld2.32         {q0},     [r0,:64]
        vld2.32         {q3},     [lr,:64]
        vld1.32         {d2},     [r1,:64]!
        vld1.32         {d18},    [r2,:64]!
        vceq.f32        d16, d2,  #0
        veor            d2,  d2,  d3
        vmov            q2,  q0
        vmla.f32        d0,  d6,  d18
        vmla.f32        d1,  d7,  d18
        vadd.f32        d4,  d4,  d2
        add             r3,  r3,  #2
        bfc             r3,  #9,  #23
        vbif            d0,  d4,  d16
        vbif            d1,  d5,  d16
        vst2.32         {q0},     [r0,:64]!
        subs            r12, r12, #2
        bgt             1b
        blt             2f
        add             lr,  r4,  r3,  lsl #3
        vld1.32         {d0},     [r0,:64]
        vld1.32         {d6},     [lr,:64]
        vld1.32         {d2[]},   [r1,:32]!
        vld1.32         {d3[]},   [r2,:32]!
        vceq.f32        d4,  d2,  #0
        veor            d2,  d2,  d3
        vmov            d1,  d0
        vmla.f32        d0,  d6,  d3
        vadd.f32        s2,  s2,  s4
        vbif            d0,  d1,  d4
        vst1.32         {d0},     [r0,:64]!
2:
        pop             {r4,pc}
endfunc

function ff_sbr_hf_apply_noise_1_neon, export=1
        ldr             r12, [sp]
        push            {r4,lr}
        lsl             r12, r12, #31
        eor             lr,  r12, #1<<31
        vmov            d3,  r12, lr
.Lhf_apply_noise_1:
        movrelx         r4,  X(ff_sbr_noise_table)
        ldr             r12, [sp, #12]
        add             r3,  r3,  #1
        bfc             r3,  #9,  #23
        sub             r12, r12, #1
1:
        add             lr,  r4,  r3,  lsl #3
        vld2.32         {q0},     [r0,:64]
        vld2.32         {q3},     [lr,:64]
        vld1.32         {d2},     [r1,:64]!
        vld1.32         {d18},    [r2,:64]!
        vceq.f32        d16, d2,  #0
        veor            d2,  d2,  d3
        vmov            q2,  q0
        vmla.f32        d0,  d6,  d18
        vmla.f32        d1,  d7,  d18
        vadd.f32        d5,  d5,  d2
        add             r3,  r3,  #2
        bfc             r3,  #9,  #23
        vbif            d0,  d4,  d16
        vbif            d1,  d5,  d16
        vst2.32         {q0},     [r0,:64]!
        subs            r12, r12, #2
        bgt             1b
        blt             2f
        add             lr,  r4,  r3,  lsl #3
        vld1.32         {d0},     [r0,:64]
        vld1.32         {d6},     [lr,:64]
        vld1.32         {d2[]},   [r1,:32]!
        vld1.32         {d18[]},  [r2,:32]!
        vceq.f32        d4,  d2,  #0
        veor            d2,  d2,  d3
        vmov            d1,  d0
        vmla.f32        d0,  d6,  d18
        vadd.f32        s3,  s3,  s5
        vbif            d0,  d1,  d4
        vst1.32         {d0},     [r0,:64]!
2:
        pop             {r4,pc}
endfunc

function ff_sbr_hf_apply_noise_2_neon, export=1
        vmov.i32        d3,  #1<<31
        b               .Lhf_apply_noise_0
endfunc

function ff_sbr_hf_apply_noise_3_neon, export=1
        ldr             r12, [sp]
        push            {r4,lr}
        lsl             r12, r12, #31
        eor             lr,  r12, #1<<31
        vmov            d3,  lr, r12
        b               .Lhf_apply_noise_1
endfunc
