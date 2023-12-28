/*
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 * Contributed by Hecai Yuan <yuanhecai@loongson.cn>
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

#include "loongson_asm.S"

.macro fr_store
    addi.d        sp,       sp,       -64
    fst.d         f24,      sp,       0
    fst.d         f25,      sp,       8
    fst.d         f26,      sp,       16
    fst.d         f27,      sp,       24
    fst.d         f28,      sp,       32
    fst.d         f29,      sp,       40
    fst.d         f30,      sp,       48
    fst.d         f31,      sp,       56
.endm

.macro fr_recover
    fld.d         f24,      sp,       0
    fld.d         f25,      sp,       8
    fld.d         f26,      sp,       16
    fld.d         f27,      sp,       24
    fld.d         f28,      sp,       32
    fld.d         f29,      sp,       40
    fld.d         f30,      sp,       48
    fld.d         f31,      sp,       56
    addi.d        sp,       sp,       64
.endm

.extern gt32x32_cnst1

.extern gt32x32_cnst2

.extern gt8x8_cnst

.extern gt32x32_cnst0

.macro idct_16x32_step1_lasx
    xvldrepl.w    xr20,     t1,       0
    xvldrepl.w    xr21,     t1,       4
    xvldrepl.w    xr22,     t1,       8
    xvldrepl.w    xr23,     t1,       12

    xvmulwev.w.h  xr16,     xr8,      xr20
    xvmaddwod.w.h xr16,     xr8,      xr20
    xvmulwev.w.h  xr17,     xr9,      xr20
    xvmaddwod.w.h xr17,     xr9,      xr20

    xvmaddwev.w.h xr16,     xr10,     xr21
    xvmaddwod.w.h xr16,     xr10,     xr21
    xvmaddwev.w.h xr17,     xr11,     xr21
    xvmaddwod.w.h xr17,     xr11,     xr21

    xvmaddwev.w.h xr16,     xr12,     xr22
    xvmaddwod.w.h xr16,     xr12,     xr22
    xvmaddwev.w.h xr17,     xr13,     xr22
    xvmaddwod.w.h xr17,     xr13,     xr22

    xvmaddwev.w.h xr16,     xr14,     xr23
    xvmaddwod.w.h xr16,     xr14,     xr23
    xvmaddwev.w.h xr17,     xr15,     xr23
    xvmaddwod.w.h xr17,     xr15,     xr23

    xvld          xr0,      t2,       0
    xvld          xr1,      t2,       32

    xvadd.w       xr18,     xr0,      xr16
    xvadd.w       xr19,     xr1,      xr17
    xvsub.w       xr0,      xr0,      xr16
    xvsub.w       xr1,      xr1,      xr17

    xvst          xr18,     t2,       0
    xvst          xr19,     t2,       32
    xvst          xr0,      t3,       0
    xvst          xr1,      t3,       32
.endm

.macro idct_16x32_step2_lasx in0, in1, in2, in3, in4, in5, in6, in7, out0, out1

    xvldrepl.w    xr20,     t1,       0
    xvldrepl.w    xr21,     t1,       4
    xvldrepl.w    xr22,     t1,       8
    xvldrepl.w    xr23,     t1,       12

    xvmulwev.w.h  \out0,    \in0,     xr20
    xvmaddwod.w.h \out0,    \in0,     xr20
    xvmulwev.w.h  \out1,    \in1,     xr20
    xvmaddwod.w.h \out1,    \in1,     xr20
    xvmaddwev.w.h \out0,    \in2,     xr21
    xvmaddwod.w.h \out0,    \in2,     xr21
    xvmaddwev.w.h \out1,    \in3,     xr21
    xvmaddwod.w.h \out1,    \in3,     xr21
    xvmaddwev.w.h \out0,    \in4,     xr22
    xvmaddwod.w.h \out0,    \in4,     xr22
    xvmaddwev.w.h \out1,    \in5,     xr22
    xvmaddwod.w.h \out1,    \in5,     xr22
    xvmaddwev.w.h \out0,    \in6,     xr23
    xvmaddwod.w.h \out0,    \in6,     xr23
    xvmaddwev.w.h \out1,    \in7,     xr23  // sum0_r
    xvmaddwod.w.h \out1,    \in7,     xr23  // sum0_l
.endm

    /* loop for all columns of filter constants */
.macro idct_16x32_step3_lasx round
    xvadd.w       xr16,     xr16,     xr30
    xvadd.w       xr17,     xr17,     xr31

    xvld          xr0,      t2,       0
    xvld          xr1,      t2,       32

    xvadd.w       xr30,     xr0,      xr16
    xvadd.w       xr31,     xr1,      xr17
    xvsub.w       xr16,     xr0,      xr16
    xvsub.w       xr17,     xr1,      xr17
    xvssrarni.h.w xr31,     xr30,     \round
    xvssrarni.h.w xr17,     xr16,     \round
    xvst          xr31,     t4,       0
    xvst          xr17,     t5,       0
.endm

.macro idct_16x32_lasx buf_pitch, round
    addi.d        t2,       sp,       64

    addi.d        t0,       a0,       \buf_pitch*4*2

    // 4 12 20 28
    xvld          xr0,      t0,       0
    xvld          xr1,      t0,       \buf_pitch*8*2
    xvld          xr2,      t0,       \buf_pitch*16*2
    xvld          xr3,      t0,       \buf_pitch*24*2

    xvilvl.h      xr10,     xr1,      xr0
    xvilvh.h      xr11,     xr1,      xr0
    xvilvl.h      xr12,     xr3,      xr2
    xvilvh.h      xr13,     xr3,      xr2

    la.local      t1,       gt32x32_cnst2

    xvldrepl.w    xr20,     t1,       0
    xvldrepl.w    xr21,     t1,       4
    xvmulwev.w.h  xr14,     xr10,     xr20
    xvmaddwod.w.h xr14,     xr10,     xr20
    xvmulwev.w.h  xr15,     xr11,     xr20
    xvmaddwod.w.h xr15,     xr11,     xr20
    xvmaddwev.w.h xr14,     xr12,     xr21
    xvmaddwod.w.h xr14,     xr12,     xr21
    xvmaddwev.w.h xr15,     xr13,     xr21
    xvmaddwod.w.h xr15,     xr13,     xr21

    xvldrepl.w    xr20,     t1,       8
    xvldrepl.w    xr21,     t1,       12
    xvmulwev.w.h  xr16,     xr10,     xr20
    xvmaddwod.w.h xr16,     xr10,     xr20
    xvmulwev.w.h  xr17,     xr11,     xr20
    xvmaddwod.w.h xr17,     xr11,     xr20
    xvmaddwev.w.h xr16,     xr12,     xr21
    xvmaddwod.w.h xr16,     xr12,     xr21
    xvmaddwev.w.h xr17,     xr13,     xr21
    xvmaddwod.w.h xr17,     xr13,     xr21

    xvldrepl.w    xr20,     t1,       16
    xvldrepl.w    xr21,     t1,       20
    xvmulwev.w.h  xr18,     xr10,     xr20
    xvmaddwod.w.h xr18,     xr10,     xr20
    xvmulwev.w.h  xr19,     xr11,     xr20
    xvmaddwod.w.h xr19,     xr11,     xr20
    xvmaddwev.w.h xr18,     xr12,     xr21
    xvmaddwod.w.h xr18,     xr12,     xr21
    xvmaddwev.w.h xr19,     xr13,     xr21
    xvmaddwod.w.h xr19,     xr13,     xr21

    xvldrepl.w    xr20,     t1,       24
    xvldrepl.w    xr21,     t1,       28
    xvmulwev.w.h  xr22,     xr10,     xr20
    xvmaddwod.w.h xr22,     xr10,     xr20
    xvmulwev.w.h  xr23,     xr11,     xr20
    xvmaddwod.w.h xr23,     xr11,     xr20
    xvmaddwev.w.h xr22,     xr12,     xr21
    xvmaddwod.w.h xr22,     xr12,     xr21
    xvmaddwev.w.h xr23,     xr13,     xr21
    xvmaddwod.w.h xr23,     xr13,     xr21

    /* process coeff 0, 8, 16, 24 */
    la.local      t1,       gt8x8_cnst

    xvld          xr0,      a0,       0
    xvld          xr1,      a0,       \buf_pitch*8*2
    xvld          xr2,      a0,       \buf_pitch*16*2
    xvld          xr3,      a0,       \buf_pitch*24*2

    xvldrepl.w    xr20,     t1,       0
    xvldrepl.w    xr21,     t1,       4

    xvilvl.h      xr10,     xr2,      xr0
    xvilvh.h      xr11,     xr2,      xr0
    xvilvl.h      xr12,     xr3,      xr1
    xvilvh.h      xr13,     xr3,      xr1

    xvmulwev.w.h  xr4,      xr10,     xr20
    xvmaddwod.w.h xr4,      xr10,     xr20  // sum0_r
    xvmulwev.w.h  xr5,      xr11,     xr20
    xvmaddwod.w.h xr5,      xr11,     xr20  // sum0_l
    xvmulwev.w.h  xr6,      xr12,     xr21
    xvmaddwod.w.h xr6,      xr12,     xr21  // tmp1_r
    xvmulwev.w.h  xr7,      xr13,     xr21
    xvmaddwod.w.h xr7,      xr13,     xr21  // tmp1_l

    xvsub.w       xr0,      xr4,      xr6   // sum1_r
    xvadd.w       xr1,      xr4,      xr6   // sum0_r
    xvsub.w       xr2,      xr5,      xr7   // sum1_l
    xvadd.w       xr3,      xr5,      xr7   // sum0_l

    // HEVC_EVEN16_CALC
    xvsub.w       xr24,     xr1,      xr14  // 7
    xvsub.w       xr25,     xr3,      xr15
    xvadd.w       xr14,     xr1,      xr14  // 0
    xvadd.w       xr15,     xr3,      xr15
    xvst          xr24,     t2,       7*16*4     // 448=16*28=7*16*4
    xvst          xr25,     t2,       7*16*4+32  // 480
    xvst          xr14,     t2,       0
    xvst          xr15,     t2,       32

    xvsub.w       xr26,     xr0,      xr22  // 4
    xvsub.w       xr27,     xr2,      xr23
    xvadd.w       xr22,     xr0,      xr22  // 3
    xvadd.w       xr23,     xr2,      xr23
    xvst          xr26,     t2,       4*16*4     // 256=4*16*4
    xvst          xr27,     t2,       4*16*4+32  // 288
    xvst          xr22,     t2,       3*16*4     // 192=3*16*4
    xvst          xr23,     t2,       3*16*4+32  // 224

    xvldrepl.w    xr20,     t1,       16
    xvldrepl.w    xr21,     t1,       20

    xvmulwev.w.h  xr4,      xr10,     xr20
    xvmaddwod.w.h xr4,      xr10,     xr20
    xvmulwev.w.h  xr5,      xr11,     xr20
    xvmaddwod.w.h xr5,      xr11,     xr20
    xvmulwev.w.h  xr6,      xr12,     xr21
    xvmaddwod.w.h xr6,      xr12,     xr21
    xvmulwev.w.h  xr7,      xr13,     xr21
    xvmaddwod.w.h xr7,      xr13,     xr21

    xvsub.w       xr0,      xr4,      xr6   // sum1_r
    xvadd.w       xr1,      xr4,      xr6   // sum0_r
    xvsub.w       xr2,      xr5,      xr7   // sum1_l
    xvadd.w       xr3,      xr5,      xr7   // sum0_l

    // HEVC_EVEN16_CALC
    xvsub.w       xr24,     xr1,      xr16  // 6
    xvsub.w       xr25,     xr3,      xr17
    xvadd.w       xr16,     xr1,      xr16  // 1
    xvadd.w       xr17,     xr3,      xr17
    xvst          xr24,     t2,       6*16*4      // 384=6*16*4
    xvst          xr25,     t2,       6*16*4+32   // 416
    xvst          xr16,     t2,       1*16*4      // 64=1*16*4
    xvst          xr17,     t2,       1*16*4+32   // 96

    xvsub.w       xr26,     xr0,      xr18  // 5
    xvsub.w       xr27,     xr2,      xr19
    xvadd.w       xr18,     xr0,      xr18  // 2
    xvadd.w       xr19,     xr2,      xr19
    xvst          xr26,     t2,       5*16*4      // 320=5*16*4
    xvst          xr27,     t2,       5*16*4+32   // 352
    xvst          xr18,     t2,       2*16*4      // 128=2*16*4
    xvst          xr19,     t2,       2*16*4+32   // 160

    /* process coeff 2 6 10 14 18 22 26 30 */
    addi.d        t0,       a0,       \buf_pitch*2*2

    xvld          xr0,      t0,       0
    xvld          xr1,      t0,       \buf_pitch*4*2
    xvld          xr2,      t0,       \buf_pitch*8*2
    xvld          xr3,      t0,       \buf_pitch*12*2

    xvld          xr4,      t0,       \buf_pitch*16*2
    xvld          xr5,      t0,       \buf_pitch*20*2
    xvld          xr6,      t0,       \buf_pitch*24*2
    xvld          xr7,      t0,       \buf_pitch*28*2

    xvilvl.h      xr8,      xr1,      xr0
    xvilvh.h      xr9,      xr1,      xr0
    xvilvl.h      xr10,     xr3,      xr2
    xvilvh.h      xr11,     xr3,      xr2
    xvilvl.h      xr12,     xr5,      xr4
    xvilvh.h      xr13,     xr5,      xr4
    xvilvl.h      xr14,     xr7,      xr6
    xvilvh.h      xr15,     xr7,      xr6

    la.local      t1,       gt32x32_cnst1

    addi.d        t2,       sp,       64
    addi.d        t3,       sp,       64+960   // 30*32

    idct_16x32_step1_lasx

.rept 7
    addi.d        t1,       t1,       16
    addi.d        t2,       t2,       64
    addi.d        t3,       t3,       -64
    idct_16x32_step1_lasx
.endr

    addi.d        t0,       a0,       \buf_pitch*2

    xvld          xr0,      t0,       0
    xvld          xr1,      t0,       \buf_pitch*2*2
    xvld          xr2,      t0,       \buf_pitch*4*2
    xvld          xr3,      t0,       \buf_pitch*6*2
    xvld          xr4,      t0,       \buf_pitch*8*2
    xvld          xr5,      t0,       \buf_pitch*10*2
    xvld          xr6,      t0,       \buf_pitch*12*2
    xvld          xr7,      t0,       \buf_pitch*14*2

    xvilvl.h      xr8,      xr1,      xr0
    xvilvh.h      xr9,      xr1,      xr0
    xvilvl.h      xr10,     xr3,      xr2
    xvilvh.h      xr11,     xr3,      xr2
    xvilvl.h      xr12,     xr5,      xr4
    xvilvh.h      xr13,     xr5,      xr4
    xvilvl.h      xr14,     xr7,      xr6
    xvilvh.h      xr15,     xr7,      xr6

    la.local      t1,       gt32x32_cnst0

    idct_16x32_step2_lasx xr8, xr9, xr10, xr11, xr12, xr13, \
                          xr14, xr15, xr16, xr17

    addi.d        t0,       a0,       \buf_pitch*16*2+\buf_pitch*2

    xvld          xr0,      t0,       0
    xvld          xr1,      t0,       \buf_pitch*2*2
    xvld          xr2,      t0,       \buf_pitch*4*2
    xvld          xr3,      t0,       \buf_pitch*6*2
    xvld          xr4,      t0,       \buf_pitch*8*2
    xvld          xr5,      t0,       \buf_pitch*10*2
    xvld          xr6,      t0,       \buf_pitch*12*2
    xvld          xr7,      t0,       \buf_pitch*14*2

    xvilvl.h      xr18,     xr1,      xr0
    xvilvh.h      xr19,     xr1,      xr0
    xvilvl.h      xr24,     xr3,      xr2
    xvilvh.h      xr25,     xr3,      xr2
    xvilvl.h      xr26,     xr5,      xr4
    xvilvh.h      xr27,     xr5,      xr4
    xvilvl.h      xr28,     xr7,      xr6
    xvilvh.h      xr29,     xr7,      xr6

    addi.d        t1,       t1,       16
    idct_16x32_step2_lasx xr18, xr19, xr24, xr25, xr26, xr27, \
                          xr28, xr29, xr30, xr31

    addi.d        t4,       a0,       0
    addi.d        t5,       a0,       \buf_pitch*31*2
    addi.d        t2,       sp,       64

    idct_16x32_step3_lasx \round

.rept 15

    addi.d        t1,       t1,       16
    idct_16x32_step2_lasx xr8, xr9, xr10, xr11, xr12, xr13, \
                          xr14, xr15, xr16, xr17

    addi.d        t1,       t1,       16
    idct_16x32_step2_lasx xr18, xr19, xr24, xr25, xr26, xr27, \
                          xr28, xr29, xr30, xr31

    addi.d        t2,       t2,       64
    addi.d        t4,       t4,       \buf_pitch*2
    addi.d        t5,       t5,       -\buf_pitch*2

    idct_16x32_step3_lasx \round
.endr

.endm

function hevc_idct_16x32_column_step1_lasx
    addi.d        sp,       sp,      -1600        // 64+512*3
    fr_store

    idct_16x32_lasx 32, 7

    fr_recover
    addi.d        sp,       sp,      1600
endfunc

function hevc_idct_16x32_column_step2_lasx
    addi.d        sp,       sp,      -1600        // 64+512*3
    fr_store

    idct_16x32_lasx 16, 12

    fr_recover
    addi.d        sp,       sp,      1600
endfunc

function hevc_idct_transpose_32x16_to_16x32_lasx
    fr_store

    xvld          xr0,      a0,       0
    xvld          xr1,      a0,       64
    xvld          xr2,      a0,       128
    xvld          xr3,      a0,       192
    xvld          xr4,      a0,       256
    xvld          xr5,      a0,       320
    xvld          xr6,      a0,       384
    xvld          xr7,      a0,       448

    xvpermi.q     xr8,      xr0,      0x01
    xvpermi.q     xr9,      xr1,      0x01
    xvpermi.q     xr10,     xr2,      0x01
    xvpermi.q     xr11,     xr3,      0x01
    xvpermi.q     xr12,     xr4,      0x01
    xvpermi.q     xr13,     xr5,      0x01
    xvpermi.q     xr14,     xr6,      0x01
    xvpermi.q     xr15,     xr7,      0x01

    LSX_TRANSPOSE8x8_H vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr7, \
                       vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr7, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    LSX_TRANSPOSE8x8_H vr8, vr9, vr10, vr11, vr12, vr13, vr14, vr15, \
                       vr8, vr9, vr10, vr11, vr12, vr13, vr14, vr15, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    addi.d        a0,       a0,       512

    vld           vr24,     a0,       0
    vld           vr25,     a0,       64
    vld           vr26,     a0,       128
    vld           vr27,     a0,       192
    vld           vr28,     a0,       256
    vld           vr29,     a0,       320
    vld           vr30,     a0,       384
    vld           vr31,     a0,       448

    LSX_TRANSPOSE8x8_H vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    xvpermi.q     xr0,      xr24,     0x02
    xvpermi.q     xr1,      xr25,     0x02
    xvpermi.q     xr2,      xr26,     0x02
    xvpermi.q     xr3,      xr27,     0x02
    xvpermi.q     xr4,      xr28,     0x02
    xvpermi.q     xr5,      xr29,     0x02
    xvpermi.q     xr6,      xr30,     0x02
    xvpermi.q     xr7,      xr31,     0x02

    xvst          xr0,      a1,       0
    xvst          xr1,      a1,       32
    xvst          xr2,      a1,       64
    xvst          xr3,      a1,       96
    xvst          xr4,      a1,       128
    xvst          xr5,      a1,       160
    xvst          xr6,      a1,       192
    xvst          xr7,      a1,       224

    addi.d        a1,       a1,       256
    addi.d        a0,       a0,       16

    vld           vr24,     a0,       0
    vld           vr25,     a0,       64
    vld           vr26,     a0,       128
    vld           vr27,     a0,       192
    vld           vr28,     a0,       256
    vld           vr29,     a0,       320
    vld           vr30,     a0,       384
    vld           vr31,     a0,       448

    LSX_TRANSPOSE8x8_H vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    xvpermi.q     xr8,      xr24,     0x02
    xvpermi.q     xr9,      xr25,     0x02
    xvpermi.q     xr10,     xr26,     0x02
    xvpermi.q     xr11,     xr27,     0x02
    xvpermi.q     xr12,     xr28,     0x02
    xvpermi.q     xr13,     xr29,     0x02
    xvpermi.q     xr14,     xr30,     0x02
    xvpermi.q     xr15,     xr31,     0x02

    xvst          xr8,      a1,       0
    xvst          xr9,      a1,       32
    xvst          xr10,     a1,       64
    xvst          xr11,     a1,       96
    xvst          xr12,     a1,       128
    xvst          xr13,     a1,       160
    xvst          xr14,     a1,       192
    xvst          xr15,     a1,       224

    // second
    addi.d        a0,       a0,       32-512-16

    xvld          xr0,      a0,       0
    xvld          xr1,      a0,       64
    xvld          xr2,      a0,       128
    xvld          xr3,      a0,       192
    xvld          xr4,      a0,       256
    xvld          xr5,      a0,       320
    xvld          xr6,      a0,       384
    xvld          xr7,      a0,       448

    xvpermi.q     xr8,      xr0,      0x01
    xvpermi.q     xr9,      xr1,      0x01
    xvpermi.q     xr10,     xr2,      0x01
    xvpermi.q     xr11,     xr3,      0x01
    xvpermi.q     xr12,     xr4,      0x01
    xvpermi.q     xr13,     xr5,      0x01
    xvpermi.q     xr14,     xr6,      0x01
    xvpermi.q     xr15,     xr7,      0x01

    LSX_TRANSPOSE8x8_H vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr7, \
                       vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr7, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    LSX_TRANSPOSE8x8_H vr8, vr9, vr10, vr11, vr12, vr13, vr14, vr15, \
                       vr8, vr9, vr10, vr11, vr12, vr13, vr14, vr15, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    addi.d        a0,       a0,       512

    vld           vr24,     a0,       0
    vld           vr25,     a0,       64
    vld           vr26,     a0,       128
    vld           vr27,     a0,       192
    vld           vr28,     a0,       256
    vld           vr29,     a0,       320
    vld           vr30,     a0,       384
    vld           vr31,     a0,       448

    LSX_TRANSPOSE8x8_H vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    xvpermi.q     xr0,      xr24,     0x02
    xvpermi.q     xr1,      xr25,     0x02
    xvpermi.q     xr2,      xr26,     0x02
    xvpermi.q     xr3,      xr27,     0x02
    xvpermi.q     xr4,      xr28,     0x02
    xvpermi.q     xr5,      xr29,     0x02
    xvpermi.q     xr6,      xr30,     0x02
    xvpermi.q     xr7,      xr31,     0x02

    addi.d        a1,       a1,       256
    xvst          xr0,      a1,       0
    xvst          xr1,      a1,       32
    xvst          xr2,      a1,       64
    xvst          xr3,      a1,       96
    xvst          xr4,      a1,       128
    xvst          xr5,      a1,       160
    xvst          xr6,      a1,       192
    xvst          xr7,      a1,       224

    addi.d        a1,       a1,       256
    addi.d        a0,       a0,       16

    vld           vr24,     a0,       0
    vld           vr25,     a0,       64
    vld           vr26,     a0,       128
    vld           vr27,     a0,       192
    vld           vr28,     a0,       256
    vld           vr29,     a0,       320
    vld           vr30,     a0,       384
    vld           vr31,     a0,       448

    LSX_TRANSPOSE8x8_H vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    xvpermi.q     xr8,      xr24,     0x02
    xvpermi.q     xr9,      xr25,     0x02
    xvpermi.q     xr10,     xr26,     0x02
    xvpermi.q     xr11,     xr27,     0x02
    xvpermi.q     xr12,     xr28,     0x02
    xvpermi.q     xr13,     xr29,     0x02
    xvpermi.q     xr14,     xr30,     0x02
    xvpermi.q     xr15,     xr31,     0x02

    xvst          xr8,      a1,       0
    xvst          xr9,      a1,       32
    xvst          xr10,     a1,       64
    xvst          xr11,     a1,       96
    xvst          xr12,     a1,       128
    xvst          xr13,     a1,       160
    xvst          xr14,     a1,       192
    xvst          xr15,     a1,       224

    fr_recover
endfunc

function hevc_idct_transpose_16x32_to_32x16_lasx
    fr_store

    xvld          xr0,      a0,       0
    xvld          xr1,      a0,       32
    xvld          xr2,      a0,       64
    xvld          xr3,      a0,       96
    xvld          xr4,      a0,       128
    xvld          xr5,      a0,       160
    xvld          xr6,      a0,       192
    xvld          xr7,      a0,       224

    xvpermi.q     xr8,      xr0,      0x01
    xvpermi.q     xr9,      xr1,      0x01
    xvpermi.q     xr10,     xr2,      0x01
    xvpermi.q     xr11,     xr3,      0x01
    xvpermi.q     xr12,     xr4,      0x01
    xvpermi.q     xr13,     xr5,      0x01
    xvpermi.q     xr14,     xr6,      0x01
    xvpermi.q     xr15,     xr7,      0x01

    LSX_TRANSPOSE8x8_H vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr7, \
                       vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr7, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    LSX_TRANSPOSE8x8_H vr8, vr9, vr10, vr11, vr12, vr13, vr14, vr15, \
                       vr8, vr9, vr10, vr11, vr12, vr13, vr14, vr15, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    addi.d        a0,       a0,       256

    vld          vr24,      a0,       0
    vld          vr25,      a0,       32
    vld          vr26,      a0,       64
    vld          vr27,      a0,       96
    vld          vr28,      a0,       128
    vld          vr29,      a0,       160
    vld          vr30,      a0,       192
    vld          vr31,      a0,       224

    LSX_TRANSPOSE8x8_H vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    xvpermi.q     xr0,      xr24,     0x02
    xvpermi.q     xr1,      xr25,     0x02
    xvpermi.q     xr2,      xr26,     0x02
    xvpermi.q     xr3,      xr27,     0x02
    xvpermi.q     xr4,      xr28,     0x02
    xvpermi.q     xr5,      xr29,     0x02
    xvpermi.q     xr6,      xr30,     0x02
    xvpermi.q     xr7,      xr31,     0x02

    xvst          xr0,      a1,       0
    xvst          xr1,      a1,       64
    xvst          xr2,      a1,       128
    xvst          xr3,      a1,       192
    xvst          xr4,      a1,       256
    xvst          xr5,      a1,       320
    xvst          xr6,      a1,       384
    xvst          xr7,      a1,       448

    addi.d        a1,       a1,       512
    addi.d        a0,       a0,       16

    vld           vr24,     a0,       0
    vld           vr25,     a0,       32
    vld           vr26,     a0,       64
    vld           vr27,     a0,       96
    vld           vr28,     a0,       128
    vld           vr29,     a0,       160
    vld           vr30,     a0,       192
    vld           vr31,     a0,       224

    LSX_TRANSPOSE8x8_H vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    xvpermi.q     xr8,      xr24,     0x02
    xvpermi.q     xr9,      xr25,     0x02
    xvpermi.q     xr10,     xr26,     0x02
    xvpermi.q     xr11,     xr27,     0x02
    xvpermi.q     xr12,     xr28,     0x02
    xvpermi.q     xr13,     xr29,     0x02
    xvpermi.q     xr14,     xr30,     0x02
    xvpermi.q     xr15,     xr31,     0x02

    xvst          xr8,      a1,       0
    xvst          xr9,      a1,       64
    xvst          xr10,     a1,       128
    xvst          xr11,     a1,       192
    xvst          xr12,     a1,       256
    xvst          xr13,     a1,       320
    xvst          xr14,     a1,       384
    xvst          xr15,     a1,       448

    // second
    addi.d        a0,       a0,       256-16

    xvld          xr0,      a0,       0
    xvld          xr1,      a0,       32
    xvld          xr2,      a0,       64
    xvld          xr3,      a0,       96
    xvld          xr4,      a0,       128
    xvld          xr5,      a0,       160
    xvld          xr6,      a0,       192
    xvld          xr7,      a0,       224

    xvpermi.q     xr8,      xr0,      0x01
    xvpermi.q     xr9,      xr1,      0x01
    xvpermi.q     xr10,     xr2,      0x01
    xvpermi.q     xr11,     xr3,      0x01
    xvpermi.q     xr12,     xr4,      0x01
    xvpermi.q     xr13,     xr5,      0x01
    xvpermi.q     xr14,     xr6,      0x01
    xvpermi.q     xr15,     xr7,      0x01

    LSX_TRANSPOSE8x8_H vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr7, \
                       vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr7, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    LSX_TRANSPOSE8x8_H vr8, vr9, vr10, vr11, vr12, vr13, vr14, vr15, \
                       vr8, vr9, vr10, vr11, vr12, vr13, vr14, vr15, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    addi.d        a0,       a0,       256

    vld           vr24,     a0,       0
    vld           vr25,     a0,       32
    vld           vr26,     a0,       64
    vld           vr27,     a0,       96
    vld           vr28,     a0,       128
    vld           vr29,     a0,       160
    vld           vr30,     a0,       192
    vld           vr31,     a0,       224

    LSX_TRANSPOSE8x8_H vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    xvpermi.q     xr0,      xr24,     0x02
    xvpermi.q     xr1,      xr25,     0x02
    xvpermi.q     xr2,      xr26,     0x02
    xvpermi.q     xr3,      xr27,     0x02
    xvpermi.q     xr4,      xr28,     0x02
    xvpermi.q     xr5,      xr29,     0x02
    xvpermi.q     xr6,      xr30,     0x02
    xvpermi.q     xr7,      xr31,     0x02

    addi.d        a1,       a1,       -512+32

    xvst          xr0,      a1,       0
    xvst          xr1,      a1,       64
    xvst          xr2,      a1,       128
    xvst          xr3,      a1,       192
    xvst          xr4,      a1,       256
    xvst          xr5,      a1,       320
    xvst          xr6,      a1,       384
    xvst          xr7,      a1,       448

    addi.d        a1,       a1,       512
    addi.d        a0,       a0,       16

    vld           vr24,     a0,       0
    vld           vr25,     a0,       32
    vld           vr26,     a0,       64
    vld           vr27,     a0,       96
    vld           vr28,     a0,       128
    vld           vr29,     a0,       160
    vld           vr30,     a0,       192
    vld           vr31,     a0,       224

    LSX_TRANSPOSE8x8_H vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr24, vr25, vr26, vr27, vr28, vr29, vr30, vr31, \
                       vr16, vr17, vr18, vr19, vr20, vr21, vr22, vr23

    xvpermi.q     xr8,      xr24,     0x02
    xvpermi.q     xr9,      xr25,     0x02
    xvpermi.q     xr10,     xr26,     0x02
    xvpermi.q     xr11,     xr27,     0x02
    xvpermi.q     xr12,     xr28,     0x02
    xvpermi.q     xr13,     xr29,     0x02
    xvpermi.q     xr14,     xr30,     0x02
    xvpermi.q     xr15,     xr31,     0x02

    xvst          xr8,      a1,       0
    xvst          xr9,      a1,       64
    xvst          xr10,     a1,       128
    xvst          xr11,     a1,       192
    xvst          xr12,     a1,       256
    xvst          xr13,     a1,       320
    xvst          xr14,     a1,       384
    xvst          xr15,     a1,       448

    fr_recover
endfunc

function ff_hevc_idct_32x32_lasx

    addi.d        t7,       a0,       0
    addi.d        t6,       a1,       0

    addi.d        sp,       sp,       -8
    st.d          ra,       sp,        0

    bl hevc_idct_16x32_column_step1_lasx

    addi.d        a0,       a0,       32

    bl hevc_idct_16x32_column_step1_lasx

    addi.d        sp,       sp,       -1086      // (16*32+31)*2
    fr_store

    addi.d        t8,       sp,       64+31*2    // tmp_buf_ptr

    addi.d        a0,       t7,       0
    addi.d        a1,       t8,       0
    bl hevc_idct_transpose_32x16_to_16x32_lasx

    addi.d        a0,       t8,       0
    bl hevc_idct_16x32_column_step2_lasx

    addi.d        a0,       t8,       0
    addi.d        a1,       t7,       0
    bl hevc_idct_transpose_16x32_to_32x16_lasx

    // second
    addi.d        a0,       t7,       32*8*2*2
    addi.d        a1,       t8,       0
    bl hevc_idct_transpose_32x16_to_16x32_lasx

    addi.d        a0,       t8,       0
    bl hevc_idct_16x32_column_step2_lasx

    addi.d        a0,       t8,       0
    addi.d        a1,       t7,       32*8*2*2
    bl hevc_idct_transpose_16x32_to_32x16_lasx

    fr_recover
    addi.d        sp,       sp,       1086       // (16*32+31)*2

    ld.d          ra,       sp,       0
    addi.d        sp,       sp,       8

endfunc
