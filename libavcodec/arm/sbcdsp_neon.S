/*
 * Bluetooth low-complexity, subband codec (SBC)
 *
 * Copyright (C) 2017  Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (C) 2008-2010  Nokia Corporation
 * Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 * Copyright (C) 2004-2005  Henryk Ploetz <henryk@ploetzli.ch>
 * Copyright (C) 2005-2006  Brad Midgley <bmidgley@xmission.com>
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

/**
 * @file
 * SBC ARM NEON optimizations
 */

#include "libavutil/arm/asm.S"
#include "neon.S"

#define SBC_PROTO_FIXED_SCALE 16

function ff_sbc_analyze_4_neon, export=1
        /* TODO: merge even and odd cases (or even merge all four calls to this
         * function) in order to have only aligned reads from 'in' array
         * and reduce number of load instructions */
        vld1.16         {d4, d5}, [r0, :64]!
        vld1.16         {d8, d9}, [r2, :128]!

        vmull.s16       q0, d4, d8
        vld1.16         {d6,  d7}, [r0, :64]!
        vmull.s16       q1, d5, d9
        vld1.16         {d10, d11}, [r2, :128]!

        vmlal.s16       q0, d6, d10
        vld1.16         {d4, d5}, [r0, :64]!
        vmlal.s16       q1, d7, d11
        vld1.16         {d8, d9}, [r2, :128]!

        vmlal.s16       q0, d4, d8
        vld1.16         {d6,  d7}, [r0, :64]!
        vmlal.s16       q1, d5, d9
        vld1.16         {d10, d11}, [r2, :128]!

        vmlal.s16       q0, d6, d10
        vld1.16         {d4, d5}, [r0, :64]!
        vmlal.s16       q1, d7, d11
        vld1.16         {d8, d9}, [r2, :128]!

        vmlal.s16       q0, d4, d8
        vmlal.s16       q1, d5, d9

        vpadd.s32       d0, d0, d1
        vpadd.s32       d1, d2, d3

        vrshrn.s32      d0, q0, SBC_PROTO_FIXED_SCALE

        vld1.16         {d2, d3, d4, d5}, [r2, :128]!

        vdup.i32        d1, d0[1]  /* TODO: can be eliminated */
        vdup.i32        d0, d0[0]  /* TODO: can be eliminated */

        vmull.s16       q3, d2, d0
        vmull.s16       q4, d3, d0
        vmlal.s16       q3, d4, d1
        vmlal.s16       q4, d5, d1

        vpadd.s32       d0, d6, d7 /* TODO: can be eliminated */
        vpadd.s32       d1, d8, d9 /* TODO: can be eliminated */

        vst1.32         {d0, d1}, [r1, :128]

        bx              lr
endfunc

function ff_sbc_analyze_8_neon, export=1
        /* TODO: merge even and odd cases (or even merge all four calls to this
         * function) in order to have only aligned reads from 'in' array
         * and reduce number of load instructions */
        vld1.16         {d4, d5}, [r0, :64]!
        vld1.16         {d8, d9}, [r2, :128]!

        vmull.s16       q6, d4, d8
        vld1.16         {d6,  d7}, [r0, :64]!
        vmull.s16       q7, d5, d9
        vld1.16         {d10, d11}, [r2, :128]!
        vmull.s16       q8, d6, d10
        vld1.16         {d4, d5}, [r0, :64]!
        vmull.s16       q9, d7, d11
        vld1.16         {d8, d9}, [r2, :128]!

        vmlal.s16       q6, d4, d8
        vld1.16         {d6,  d7}, [r0, :64]!
        vmlal.s16       q7, d5, d9
        vld1.16         {d10, d11}, [r2, :128]!
        vmlal.s16       q8, d6, d10
        vld1.16         {d4, d5}, [r0, :64]!
        vmlal.s16       q9, d7, d11
        vld1.16         {d8, d9}, [r2, :128]!

        vmlal.s16       q6, d4, d8
        vld1.16         {d6,  d7}, [r0, :64]!
        vmlal.s16       q7, d5, d9
        vld1.16         {d10, d11}, [r2, :128]!
        vmlal.s16       q8, d6, d10
        vld1.16         {d4, d5}, [r0, :64]!
        vmlal.s16       q9, d7, d11
        vld1.16         {d8, d9}, [r2, :128]!

        vmlal.s16       q6, d4, d8
        vld1.16         {d6,  d7}, [r0, :64]!
        vmlal.s16       q7, d5, d9
        vld1.16         {d10, d11}, [r2, :128]!
        vmlal.s16       q8, d6, d10
        vld1.16         {d4, d5}, [r0, :64]!
        vmlal.s16       q9, d7, d11
        vld1.16         {d8, d9}, [r2, :128]!

        vmlal.s16       q6, d4, d8
        vld1.16         {d6,  d7}, [r0, :64]!
        vmlal.s16       q7, d5, d9
        vld1.16         {d10, d11}, [r2, :128]!

        vmlal.s16       q8, d6, d10
        vmlal.s16       q9, d7, d11

        vpadd.s32       d0, d12, d13
        vpadd.s32       d1, d14, d15
        vpadd.s32       d2, d16, d17
        vpadd.s32       d3, d18, d19

        vrshr.s32       q0, q0, SBC_PROTO_FIXED_SCALE
        vrshr.s32       q1, q1, SBC_PROTO_FIXED_SCALE
        vmovn.s32       d0, q0
        vmovn.s32       d1, q1

        vdup.i32        d3, d1[1]  /* TODO: can be eliminated */
        vdup.i32        d2, d1[0]  /* TODO: can be eliminated */
        vdup.i32        d1, d0[1]  /* TODO: can be eliminated */
        vdup.i32        d0, d0[0]  /* TODO: can be eliminated */

        vld1.16         {d4, d5}, [r2, :128]!
        vmull.s16       q6, d4, d0
        vld1.16         {d6, d7}, [r2, :128]!
        vmull.s16       q7, d5, d0
        vmull.s16       q8, d6, d0
        vmull.s16       q9, d7, d0

        vld1.16         {d4, d5}, [r2, :128]!
        vmlal.s16       q6, d4, d1
        vld1.16         {d6, d7}, [r2, :128]!
        vmlal.s16       q7, d5, d1
        vmlal.s16       q8, d6, d1
        vmlal.s16       q9, d7, d1

        vld1.16         {d4, d5}, [r2, :128]!
        vmlal.s16       q6, d4, d2
        vld1.16         {d6, d7}, [r2, :128]!
        vmlal.s16       q7, d5, d2
        vmlal.s16       q8, d6, d2
        vmlal.s16       q9, d7, d2

        vld1.16         {d4, d5}, [r2, :128]!
        vmlal.s16       q6, d4, d3
        vld1.16         {d6, d7}, [r2, :128]!
        vmlal.s16       q7, d5, d3
        vmlal.s16       q8, d6, d3
        vmlal.s16       q9, d7, d3

        vpadd.s32       d0, d12, d13 /* TODO: can be eliminated */
        vpadd.s32       d1, d14, d15 /* TODO: can be eliminated */
        vpadd.s32       d2, d16, d17 /* TODO: can be eliminated */
        vpadd.s32       d3, d18, d19 /* TODO: can be eliminated */

        vst1.32         {d0, d1, d2, d3}, [r1, :128]

        bx              lr
endfunc

function ff_sbc_calc_scalefactors_neon, export=1
        @ parameters
        @ r0 = sb_sample_f
        @ r1 = scale_factor
        @ r2 = blocks
        @ r3 = channels
        @ r4 = subbands
        @ local variables
        @ r5 = in_loop_1
        @ r6 = in
        @ r7 = out_loop_1
        @ r8 = out
        @ r9 = ch
        @ r10 = sb
        @ r11 = inc
        @ r12 = blk

        push            {r1-r2, r4-r12}
        ldr             r4,  [sp, #44]
        mov             r11, #64

        mov             r9,  #0
1:
        add             r5,  r0,  r9, lsl#5
        add             r7,  r1,  r9, lsl#5

        mov             r10,  #0
2:
        add             r6,  r5,  r10, lsl#2
        add             r8,  r7,  r10, lsl#2
        mov             r12, r2

        vmov.s32        q0,  #0
        vmov.s32        q1,  #0x8000            @ 1 << SCALE_OUT_BITS
        vmov.s32        q14, #1
        vmov.s32        q15, #16                @ 31 - SCALE_OUT_BITS
        vadd.s32        q1,  q1,  q14
3:
        vld1.32         {d16, d17}, [r6, :128], r11
        vabs.s32        q8,  q8
        vld1.32         {d18, d19}, [r6, :128], r11
        vabs.s32        q9,  q9
        vld1.32         {d20, d21}, [r6, :128], r11
        vabs.s32        q10, q10
        vld1.32         {d22, d23}, [r6, :128], r11
        vabs.s32        q11, q11
        vmax.s32        q0,  q0,  q8
        vmax.s32        q1,  q1,  q9
        vmax.s32        q0,  q0,  q10
        vmax.s32        q1,  q1,  q11
        subs            r12, r12, #4
        bgt             3b
        vmax.s32        q0,  q0,  q1
        vsub.s32        q0,  q0,  q14
        vclz.s32        q0,  q0
        vsub.s32        q0,  q15, q0
        vst1.32         {d0, d1}, [r8, :128]

        add             r10, r10, #4
        cmp             r10, r4
        blt             2b

        add             r9,  r9,  #1
        cmp             r9,  r3
        blt             1b

        pop             {r1-r2, r4-r12}
        bx              lr
endfunc

/*
 * constants: q13 = (31 - SCALE_OUT_BITS)
 *            q14 = 1
 * input:     q0  - ((1 << SCALE_OUT_BITS) + 1)
 *            r5  - samples for channel 0
 *            r6  - samples for shannel 1
 * output:    q0, q1 - scale factors without joint stereo
 *            q2, q3 - scale factors with joint stereo
 *            q15    - joint stereo selection mask
 */
.macro calc_scalefactors
        vmov.s32        q1,  q0
        vmov.s32        q2,  q0
        vmov.s32        q3,  q0
        mov             r3,  r2
1:
        vld1.32         {d18, d19}, [r6, :128], r11
        vbic.s32        q11, q9,  q14
        vld1.32         {d16, d17}, [r5, :128], r11
        vhadd.s32       q10, q8,  q11
        vhsub.s32       q11, q8,  q11
        vabs.s32        q8,  q8
        vabs.s32        q9,  q9
        vabs.s32        q10, q10
        vabs.s32        q11, q11
        vmax.s32        q0,  q0,  q8
        vmax.s32        q1,  q1,  q9
        vmax.s32        q2,  q2,  q10
        vmax.s32        q3,  q3,  q11
        subs            r3,  r3,  #1
        bgt             1b
        vsub.s32        q0,  q0,  q14
        vsub.s32        q1,  q1,  q14
        vsub.s32        q2,  q2,  q14
        vsub.s32        q3,  q3,  q14
        vclz.s32        q0,  q0
        vclz.s32        q1,  q1
        vclz.s32        q2,  q2
        vclz.s32        q3,  q3
        vsub.s32        q0,  q13, q0
        vsub.s32        q1,  q13, q1
        vsub.s32        q2,  q13, q2
        vsub.s32        q3,  q13, q3
.endm

/*
 * constants: q14 = 1
 * input: q15 - joint stereo selection mask
 *        r5  - value set by calc_scalefactors macro
 *        r6  - value set by calc_scalefactors macro
 */
.macro update_joint_stereo_samples
        sub             r8,  r6,  r11
        sub             r7,  r5,  r11
        sub             r6,  r6,  r11, asl #1
        sub             r5,  r5,  r11, asl #1
        vld1.32         {d18, d19}, [r6, :128]
        vbic.s32        q11, q9,  q14
        vld1.32         {d16, d17}, [r5, :128]
        vld1.32         {d2, d3}, [r8, :128]
        vbic.s32        q3,  q1,  q14
        vld1.32         {d0, d1}, [r7, :128]
        vhsub.s32       q10, q8,  q11
        vhadd.s32       q11, q8,  q11
        vhsub.s32       q2,  q0,  q3
        vhadd.s32       q3,  q0,  q3
        vbif.s32        q10, q9,  q15
        vbif.s32        d22, d16, d30
        sub             r11, r10, r11, asl #1
        sub             r3,  r2,  #2
2:
        vbif.s32        d23, d17, d31
        vst1.32         {d20, d21}, [r6, :128], r11
        vbif.s32        d4,  d2,  d30
        vld1.32         {d18, d19}, [r6, :128]
        vbif.s32        d5,  d3,  d31
        vst1.32         {d22, d23}, [r5, :128], r11
        vbif.s32        d6,  d0,  d30
        vld1.32         {d16, d17}, [r5, :128]
        vbif.s32        d7,  d1,  d31
        vst1.32         {d4, d5}, [r8, :128], r11
        vbic.s32        q11, q9,  q14
        vld1.32         {d2, d3}, [r8, :128]
        vst1.32         {d6, d7}, [r7, :128], r11
        vbic.s32        q3,  q1,  q14
        vld1.32         {d0, d1}, [r7, :128]
        vhsub.s32       q10, q8,  q11
        vhadd.s32       q11, q8,  q11
        vhsub.s32       q2,  q0,  q3
        vhadd.s32       q3,  q0,  q3
        vbif.s32        q10, q9,  q15
        vbif.s32        d22, d16, d30
        subs            r3,  r3,  #2
        bgt             2b
        sub             r11, r10, r11, asr #1
        vbif.s32        d23, d17, d31
        vst1.32         {d20, d21}, [r6, :128]
        vbif.s32        q2,  q1,  q15
        vst1.32         {d22, d23}, [r5, :128]
        vbif.s32        q3,  q0,  q15
        vst1.32         {d4, d5}, [r8, :128]
        vst1.32         {d6, d7}, [r7, :128]
.endm

function ff_sbc_calc_scalefactors_j_neon, export=1
        @ parameters
        @ r0 = in = sb_sample_f
        @ r1 = out = scale_factor
        @ r2 = blocks
        @ r3 = subbands
        @ local variables
        @ r4 = consts = ff_sbcdsp_joint_bits_mask
        @ r5 = in0
        @ r6 = in1
        @ r7 = out0
        @ r8 = out1
        @ r10 = zero
        @ r11 = inc
        @ return r0 = joint

        push            {r3-r11}
        movrelx         r4,  X(ff_sbcdsp_joint_bits_mask)
        mov             r10, #0
        mov             r11, #64

        vmov.s32        q14, #1
        vmov.s32        q13, #16    @ 31 - SCALE_OUT_BITS

        cmp             r3, #4
        bne             8f

4:      @ 4 subbands
        add             r5,  r0,  #0
        add             r6,  r0,  #32
        add             r7,  r1,  #0
        add             r8,  r1,  #32
        vmov.s32        q0,  #0x8000    @ 1 << SCALE_OUT_BITS
        vadd.s32        q0,  q0,  q14

        calc_scalefactors

        @ check whether to use joint stereo for subbands 0, 1, 2
        vadd.s32        q15, q0,  q1
        vadd.s32        q9,  q2,  q3
        vmov.s32        d31[1], r10    @ last subband -> no joint
        vld1.32         {d16, d17}, [r4, :128]!
        vcgt.s32        q15, q15, q9

        @ calculate and save to memory 'joint' variable
        @ update and save scale factors to memory
        vand.s32        q8,  q8,  q15
        vbit.s32        q0,  q2,  q15
        vpadd.s32       d16, d16, d17
        vbit.s32        q1,  q3,  q15
        vpadd.s32       d16, d16, d16
        vst1.32         {d0, d1}, [r7, :128]
        vst1.32         {d2, d3}, [r8, :128]
        vmov.32         r0, d16[0]

        update_joint_stereo_samples
        b               9f

8:      @ 8 subbands
        add             r5,  r0,  #16
        add             r6,  r0,  #48
        add             r7,  r1,  #16
        add             r8,  r1,  #48
        vmov.s32        q0,  #0x8000    @ 1 << SCALE_OUT_BITS
        vadd.s32        q0,  q0,  q14

        calc_scalefactors

        @ check whether to use joint stereo for subbands 4, 5, 6
        vadd.s32        q15, q0,  q1
        vadd.s32        q9,  q2,  q3
        vmov.s32        d31[1], r10    @ last subband -> no joint
        vld1.32         {d16, d17}, [r4, :128]!
        vcgt.s32        q15, q15, q9

        @ calculate part of 'joint' variable and save it to d24
        @ update and save scale factors to memory
        vand.s32        q8,  q8,  q15
        vbit.s32        q0,  q2,  q15
        vpadd.s32       d16, d16, d17
        vbit.s32        q1,  q3,  q15
        vst1.32         {d0, d1}, [r7, :128]
        vst1.32         {d2, d3}, [r8, :128]
        vpadd.s32       d24, d16, d16

        update_joint_stereo_samples

        add             r5,  r0,  #0
        add             r6,  r0,  #32
        add             r7,  r1,  #0
        add             r8,  r1,  #32
        vmov.s32        q0,  #0x8000    @ 1 << SCALE_OUT_BITS
        vadd.s32        q0,  q0,  q14

        calc_scalefactors

        @ check whether to use joint stereo for subbands 0, 1, 2, 3
        vadd.s32        q15, q0,  q1
        vadd.s32        q9,  q2,  q3
        vld1.32         {d16, d17}, [r4, :128]!
        vcgt.s32        q15, q15, q9

        @ combine last part of 'joint' with d24 and save to memory
        @ update and save scale factors to memory
        vand.s32        q8,  q8,  q15
        vbit.s32        q0,  q2,  q15
        vpadd.s32       d16, d16, d17
        vbit.s32        q1,  q3,  q15
        vpadd.s32       d16, d16, d16
        vst1.32         {d0, d1}, [r7, :128]
        vadd.s32        d16, d16, d24
        vst1.32         {d2, d3}, [r8, :128]
        vmov.32         r0,  d16[0]

        update_joint_stereo_samples
9:
        pop             {r3-r11}
        bx              lr
endfunc

function ff_sbc_enc_process_input_4s_neon, export=1
        @ parameters
        @ r0 = positioin
        @ r1 = pcm
        @ r2 = X
        @ r3 = nsamples
        @ r4 = nchannels
        @ local variables
        @ r5 = ff_sbc_input_perm_4
        @ r6 = src / x
        @ r7 = dst / y

        push            {r1, r3-r7}
        ldr             r4,  [sp, #24]
        movrelx         r5,  X(ff_sbc_input_perm_4)

        @ handle X buffer wraparound
        cmp             r0,  r3
        bge             1f                     @ if (position < nsamples)
        add             r7,  r2,  #576         @ &X[0][SBC_X_BUFFER_SIZE - 40]
        add             r6,  r2,  r0, lsl#1    @ &X[0][position]
        vld1.16         {d0, d1, d2, d3}, [r6, :128]!
        vst1.16         {d0, d1, d2, d3}, [r7, :128]!
        vld1.16         {d0, d1, d2, d3}, [r6, :128]!
        vst1.16         {d0, d1, d2, d3}, [r7, :128]!
        vld1.16         {d0}, [r6, :64]!
        vst1.16         {d0}, [r7, :64]!
        cmp             r4,  #1
        ble             2f                     @ if (nchannels > 1)
        add             r7,  r2,  #1232        @ &X[1][SBC_X_BUFFER_SIZE - 40]
        add             r6,  r2,  #656
        add             r6,  r6,  r0, lsl#1    @ &X[1][position]
        vld1.16         {d0, d1, d2, d3}, [r6, :128]!
        vst1.16         {d0, d1, d2, d3}, [r7, :128]!
        vld1.16         {d0, d1, d2, d3}, [r6, :128]!
        vst1.16         {d0, d1, d2, d3}, [r7, :128]!
        vld1.16         {d0}, [r6, :64]!
        vst1.16         {d0}, [r7, :64]!
2:
        mov             r0,  #288              @ SBC_X_BUFFER_SIZE - 40
1:

        add             r6,  r2,  r0, lsl#1    @ &X[0][position]
        add             r7,  r6,  #656         @ &X[1][position]

        cmp             r4,  #1
        ble             8f                     @ if (nchannels > 1)
        tst             r1,  #1
        beq             7f                     @ if (pcm & 1)
        @ poor 'pcm' alignment
        vld1.8          {d0, d1}, [r5, :128]
1:
        sub             r6,  r6,  #16
        sub             r7,  r7,  #16
        sub             r0,  r0,  #8
        vld1.8          {d4, d5}, [r1]!
        vuzp.16         d4,  d5
        vld1.8          {d20, d21}, [r1]!
        vuzp.16         d20, d21
        vswp            d5,  d20
        vtbl.8          d16, {d4, d5}, d0
        vtbl.8          d17, {d4, d5}, d1
        vtbl.8          d18, {d20, d21}, d0
        vtbl.8          d19, {d20, d21}, d1
        vst1.16         {d16, d17}, [r6, :128]
        vst1.16         {d18, d19}, [r7, :128]
        subs            r3,  r3,  #8
        bgt             1b
        b               9f
7:
        @ proper 'pcm' alignment
        vld1.8          {d0, d1}, [r5, :128]
1:
        sub             r6,  r6,  #16
        sub             r7,  r7,  #16
        sub             r0,  r0,  #8
        vld2.16         {d4, d5}, [r1]!
        vld2.16         {d20, d21}, [r1]!
        vswp            d5,  d20
        vtbl.8          d16, {d4, d5}, d0
        vtbl.8          d17, {d4, d5}, d1
        vtbl.8          d18, {d20, d21}, d0
        vtbl.8          d19, {d20, d21}, d1
        vst1.16         {d16, d17}, [r6, :128]
        vst1.16         {d18, d19}, [r7, :128]
        subs            r3,  r3,  #8
        bgt             1b
        b               9f
8:
        @ mono
        vld1.8          {d0, d1}, [r5, :128]
1:
        sub             r6,  r6,  #16
        sub             r0,  r0,  #8
        vld1.8          {d4, d5}, [r1]!
        vtbl.8          d16, {d4, d5}, d0
        vtbl.8          d17, {d4, d5}, d1
        vst1.16         {d16, d17}, [r6, :128]
        subs            r3,  r3,  #8
        bgt             1b
9:
        pop             {r1, r3-r7}
        bx              lr
endfunc

function ff_sbc_enc_process_input_8s_neon, export=1
        @ parameters
        @ r0 = positioin
        @ r1 = pcm
        @ r2 = X
        @ r3 = nsamples
        @ r4 = nchannels
        @ local variables
        @ r5 = ff_sbc_input_perm_8
        @ r6 = src
        @ r7 = dst

        push            {r1, r3-r7}
        ldr             r4,  [sp, #24]
        movrelx         r5,  X(ff_sbc_input_perm_8)

        @ handle X buffer wraparound
        cmp             r0,  r3
        bge             1f                     @ if (position < nsamples)
        add             r7,  r2,  #512         @ &X[0][SBC_X_BUFFER_SIZE - 72]
        add             r6,  r2,  r0, lsl#1    @ &X[0][position]
        vld1.16         {d0, d1, d2, d3}, [r6, :128]!
        vst1.16         {d0, d1, d2, d3}, [r7, :128]!
        vld1.16         {d0, d1, d2, d3}, [r6, :128]!
        vst1.16         {d0, d1, d2, d3}, [r7, :128]!
        vld1.16         {d0, d1, d2, d3}, [r6, :128]!
        vst1.16         {d0, d1, d2, d3}, [r7, :128]!
        vld1.16         {d0, d1, d2, d3}, [r6, :128]!
        vst1.16         {d0, d1, d2, d3}, [r7, :128]!
        vld1.16         {d0, d1}, [r6, :128]!
        vst1.16         {d0, d1}, [r7, :128]!
        cmp             r4,  #1
        ble             2f                     @ if (nchannels > 1)
        add             r7,  r2,  #1168        @ &X[1][SBC_X_BUFFER_SIZE - 72]
        add             r6,  r2,  #656
        add             r6,  r6,  r0, lsl#1    @ &X[1][position]
        vld1.16         {d0, d1, d2, d3}, [r6, :128]!
        vst1.16         {d0, d1, d2, d3}, [r7, :128]!
        vld1.16         {d0, d1, d2, d3}, [r6, :128]!
        vst1.16         {d0, d1, d2, d3}, [r7, :128]!
        vld1.16         {d0, d1, d2, d3}, [r6, :128]!
        vst1.16         {d0, d1, d2, d3}, [r7, :128]!
        vld1.16         {d0, d1, d2, d3}, [r6, :128]!
        vst1.16         {d0, d1, d2, d3}, [r7, :128]!
        vld1.16         {d0, d1}, [r6, :128]!
        vst1.16         {d0, d1}, [r7, :128]!
2:
        mov             r0,  #256              @ SBC_X_BUFFER_SIZE - 72
1:

        add             r6,  r2,  r0, lsl#1    @ &X[0][position]
        add             r7,  r6,  #656         @ &X[1][position]

        cmp             r4,  #1
        ble             8f                     @ if (nchannels > 1)
        tst             r1,  #1
        beq             7f                     @ if (pcm & 1)
        @ poor 'pcm' alignment
        vld1.8          {d0, d1, d2, d3}, [r5, :128]
1:
        sub             r6,  r6,  #32
        sub             r7,  r7,  #32
        sub             r0,  r0,  #16
        vld1.8          {d4, d5, d6, d7}, [r1]!
        vuzp.16         q2,  q3
        vld1.8          {d20, d21, d22, d23}, [r1]!
        vuzp.16         q10, q11
        vswp            q3,  q10
        vtbl.8          d16, {d4, d5, d6, d7}, d0
        vtbl.8          d17, {d4, d5, d6, d7}, d1
        vtbl.8          d18, {d4, d5, d6, d7}, d2
        vtbl.8          d19, {d4, d5, d6, d7}, d3
        vst1.16         {d16, d17, d18, d19}, [r6, :128]
        vtbl.8          d16, {d20, d21, d22, d23}, d0
        vtbl.8          d17, {d20, d21, d22, d23}, d1
        vtbl.8          d18, {d20, d21, d22, d23}, d2
        vtbl.8          d19, {d20, d21, d22, d23}, d3
        vst1.16         {d16, d17, d18, d19}, [r7, :128]
        subs            r3,  r3,  #16
        bgt             1b
        b 9f
7:
        @ proper 'pcm' alignment
        vld1.8          {d0, d1, d2, d3}, [r5, :128]
1:
        sub             r6,  r6,  #32
        sub             r7,  r7,  #32
        sub             r0,  r0,  #16
        vld2.16         {d4, d5, d6, d7}, [r1]!
        vld2.16         {d20, d21, d22, d23}, [r1]!
        vswp            q3,  q10
        vtbl.8          d16, {d4, d5, d6, d7}, d0
        vtbl.8          d17, {d4, d5, d6, d7}, d1
        vtbl.8          d18, {d4, d5, d6, d7}, d2
        vtbl.8          d19, {d4, d5, d6, d7}, d3
        vst1.16         {d16, d17, d18, d19}, [r6, :128]
        vtbl.8          d16, {d20, d21, d22, d23}, d0
        vtbl.8          d17, {d20, d21, d22, d23}, d1
        vtbl.8          d18, {d20, d21, d22, d23}, d2
        vtbl.8          d19, {d20, d21, d22, d23}, d3
        vst1.16         {d16, d17, d18, d19}, [r7, :128]
        subs            r3,  r3,  #16
        bgt             1b
        b               9f
8:
        @ mono
        vld1.8          {d0, d1, d2, d3}, [r5, :128]
1:
        sub             r6,  r6,  #32
        sub             r0,  r0,  #16
        vld1.8          {d4, d5, d6, d7}, [r1]!
        vtbl.8          d16, {d4, d5, d6, d7}, d0
        vtbl.8          d17, {d4, d5, d6, d7}, d1
        vtbl.8          d18, {d4, d5, d6, d7}, d2
        vtbl.8          d19, {d4, d5, d6, d7}, d3
        vst1.16         {d16, d17, d18, d19}, [r6, :128]
        subs            r3,  r3,  #16
        bgt             1b
9:
        pop             {r1, r3-r7}
        bx              lr
endfunc
