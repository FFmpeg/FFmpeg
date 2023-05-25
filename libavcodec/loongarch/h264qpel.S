/*
 * Loongson LSX optimized h264qpel
 *
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

.macro VLD_QPEL8_H_SSRANI_LSX in0, in1, in2, in3, in4
    vld           vr0,    \in4,   0
    vldx          vr1,    \in4,   a2
    QPEL8_H_LSX   \in0,   \in1
    vssrani.bu.h  \in0,   \in2,   5
    vssrani.bu.h  \in1,   \in3,   5
.endm

.macro VLDX_QPEL8_H_SSRANI_LSX in0, in1, in2, in3, in4
    vldx          vr0,    \in4,   t1
    vldx          vr1,    \in4,   t2
    QPEL8_H_LSX   \in0,   \in1
    vssrani.bu.h  \in0,   \in2,   5
    vssrani.bu.h  \in1,   \in3,   5
.endm

.macro VLD_DOUBLE_QPEL8_H_SSRANI_LSX in0, in1, in2, in3, in4, in5, in6, in7, in8
    vld           vr0,    \in8,   0
    vldx          vr1,    \in8,   a2
    QPEL8_H_LSX   \in0,   \in1
    vssrani.bu.h  \in0,   \in4,   5
    vssrani.bu.h  \in1,   \in5,   5
    vldx          vr0,    \in8,   t1
    vldx          vr1,    \in8,   t2
    QPEL8_H_LSX   \in2,   \in3
    vssrani.bu.h  \in2,   \in6,   5
    vssrani.bu.h  \in3,   \in7,   5
.endm

function ff_put_h264_qpel16_mc00_lsx
    slli.d        t0,     a2,     1
    add.d         t1,     t0,     a2
    slli.d        t2,     t0,     1
.rept 4
    vld           vr0,    a1,     0
    vldx          vr1,    a1,     a2
    vldx          vr2,    a1,     t0
    vldx          vr3,    a1,     t1
    add.d         a1,     a1,     t2
    vst           vr0,    a0,     0
    vstx          vr1,    a0,     a2
    vstx          vr2,    a0,     t0
    vstx          vr3,    a0,     t1
    add.d         a0,     a0,     t2
.endr
endfunc

.macro QPEL8_H_LSX out0, out1
    vbsrl.v       vr2,    vr0,    1
    vbsrl.v       vr3,    vr1,    1
    vbsrl.v       vr4,    vr0,    2
    vbsrl.v       vr5,    vr1,    2
    vbsrl.v       vr6,    vr0,    3
    vbsrl.v       vr7,    vr1,    3
    vbsrl.v       vr8,    vr0,    4
    vbsrl.v       vr9,    vr1,    4
    vbsrl.v       vr10,   vr0,    5
    vbsrl.v       vr11,   vr1,    5

    vilvl.b       vr6,    vr4,    vr6
    vilvl.b       vr7,    vr5,    vr7
    vilvl.b       vr8,    vr2,    vr8
    vilvl.b       vr9,    vr3,    vr9
    vilvl.b       vr10,   vr0,    vr10
    vilvl.b       vr11,   vr1,    vr11
    vhaddw.hu.bu  vr6,    vr6,    vr6
    vhaddw.hu.bu  vr7,    vr7,    vr7
    vhaddw.hu.bu  vr8,    vr8,    vr8
    vhaddw.hu.bu  vr9,    vr9,    vr9
    vhaddw.hu.bu  vr10,   vr10,   vr10
    vhaddw.hu.bu  vr11,   vr11,   vr11
    vmul.h        vr2,    vr6,    vr20
    vmul.h        vr3,    vr7,    vr20
    vmul.h        vr4,    vr8,    vr21
    vmul.h        vr5,    vr9,    vr21
    vssub.h       vr2,    vr2,    vr4
    vssub.h       vr3,    vr3,    vr5
    vsadd.h       vr2,    vr2,    vr10
    vsadd.h       vr3,    vr3,    vr11
    vsadd.h       \out0,  vr2,    vr22
    vsadd.h       \out1,  vr3,    vr22
.endm

.macro VLD_DOUBLE_QPEL8_H_LSX in0, in1, in2, in3, in4
    vld           vr0,    \in4,   0
    vldx          vr1,    \in4,   a2
    QPEL8_H_LSX   \in0,   \in1
    vldx          vr0,    \in4,   t1
    vldx          vr1,    \in4,   t2
    QPEL8_H_LSX   \in2,   \in3
.endm

.macro put_h264_qpel16 in0
function ff_put_h264_qpel16_mc\in0\()_lsx
.ifc \in0, 10
    addi.d        t8,     a1,     0
.else
    addi.d        t8,     a1,     1
.endif
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410
    slli.d        t1,     a2,     1
    add.d         t2,     t1,     a2
    addi.d        t0,     a1,     -2   // t0 = src - 2
    addi.d        a1,     t0,     8    // a1 = t0 + 8
.rept 4
    VLD_DOUBLE_QPEL8_H_LSX vr12, vr13, vr14, vr15, t0
    VLD_QPEL8_H_SSRANI_LSX vr2, vr3, vr12, vr13, a1
    vld           vr10,   t8,     0
    vldx          vr11,   t8,     a2
    vavgr.bu      vr0,    vr2,    vr10
    vavgr.bu      vr1,    vr3,    vr11
    vst           vr0,    a0,     0
    vstx          vr1,    a0,     a2
    VLDX_QPEL8_H_SSRANI_LSX vr4, vr5, vr14, vr15, a1
    vldx          vr12,   t8,     t1
    vldx          vr13,   t8,     t2
    vavgr.bu      vr2,    vr4,    vr12
    vavgr.bu      vr3,    vr5,    vr13
    vstx          vr2,    a0,     t1
    vstx          vr3,    a0,     t2
    alsl.d        a0,     a2,     a0,    2
    alsl.d        t8,     a2,     t8,    2
    alsl.d        a1,     a2,     a1,    2
    alsl.d        t0,     a2,     t0,    2
.endr
endfunc
.endm

put_h264_qpel16 10
put_h264_qpel16 30

function ff_put_h264_qpel16_mc20_lsx
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410
    slli.d        t1,     a2,     1
    add.d         t2,     t1,     a2
    addi.d        t0,     a1,     -2   // t0 = src - 2
    addi.d        a1,     t0,     8    // a1 = t0 + 8
.rept 4
    VLD_DOUBLE_QPEL8_H_LSX vr12, vr13, vr14, vr15, t0
    VLD_QPEL8_H_SSRANI_LSX vr2, vr3, vr12, vr13, a1
    vst           vr2,    a0,     0
    vstx          vr3,    a0,     a2
    VLDX_QPEL8_H_SSRANI_LSX vr4, vr5, vr14, vr15, a1
    vstx          vr4,    a0,     t1
    vstx          vr5,    a0,     t2
    alsl.d        a0,     a2,     a0,    2
    alsl.d        a1,     a2,     a1,    2
    alsl.d        t0,     a2,     t0,    2
.endr
endfunc

.macro QPEL8_V_LSX in0, in1, in2, in3, in4, in5, in6
    vilvl.b       vr7,    \in3,   \in2
    vilvl.b       vr8,    \in4,   \in3
    vilvl.b       vr9,    \in4,   \in1
    vilvl.b       vr10,   \in5,   \in2
    vilvl.b       vr11,   \in5,   \in0
    vilvl.b       vr12,   \in6,   \in1
    vhaddw.hu.bu  vr7,    vr7,    vr7
    vhaddw.hu.bu  vr8,    vr8,    vr8
    vhaddw.hu.bu  vr9,    vr9,    vr9
    vhaddw.hu.bu  vr10,   vr10,   vr10
    vhaddw.hu.bu  vr11,   vr11,   vr11
    vhaddw.hu.bu  vr12,   vr12,   vr12
    vmul.h        vr7,    vr7,    vr20
    vmul.h        vr8,    vr8,    vr20
    vmul.h        vr9,    vr9,    vr21
    vmul.h        vr10,   vr10,   vr21
    vssub.h       vr7,    vr7,    vr9
    vssub.h       vr8,    vr8,    vr10
    vsadd.h       vr7,    vr7,    vr11
    vsadd.h       vr8,    vr8,    vr12
    vsadd.h       vr7,    vr7,    vr22
    vsadd.h       vr8,    vr8,    vr22

    vilvh.b       vr13,   \in3,   \in2
    vilvh.b       vr14,   \in4,   \in3
    vilvh.b       vr15,   \in4,   \in1
    vilvh.b       vr16,   \in5,   \in2
    vilvh.b       vr17,   \in5,   \in0
    vilvh.b       vr18,   \in6,   \in1
    vhaddw.hu.bu  vr13,   vr13,   vr13
    vhaddw.hu.bu  vr14,   vr14,   vr14
    vhaddw.hu.bu  vr15,   vr15,   vr15
    vhaddw.hu.bu  vr16,   vr16,   vr16
    vhaddw.hu.bu  vr17,   vr17,   vr17
    vhaddw.hu.bu  vr18,   vr18,   vr18
    vmul.h        vr13,   vr13,   vr20
    vmul.h        vr14,   vr14,   vr20
    vmul.h        vr15,   vr15,   vr21
    vmul.h        vr16,   vr16,   vr21
    vssub.h       vr13,   vr13,   vr15
    vssub.h       vr14,   vr14,   vr16
    vsadd.h       vr13,   vr13,   vr17
    vsadd.h       vr14,   vr14,   vr18
    vsadd.h       vr13,   vr13,   vr22
    vsadd.h       vr14,   vr14,   vr22
    vssrani.bu.h  vr13,   vr7,    5
    vssrani.bu.h  vr14,   vr8,    5
.endm

.macro put_h264_qpel16_mc1 in0
function ff_put_h264_qpel16_mc\in0\()_lsx
    slli.d        t0,     a2,     1
    add.d         t1,     t0,     a2
    sub.d         t2,     a1,     t0  // t2 = src - 2 * stride
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410

    vld           vr0,    t2,     0
    vldx          vr1,    t2,     a2
    vldx          vr2,    t2,     t0
    vldx          vr3,    t2,     t1
    alsl.d        t2,     a2,     t2,    2  // t2 = t2 + 4 * stride
    vld           vr4,    t2,     0
    vldx          vr5,    t2,     a2
    vldx          vr6,    t2,     t0
    QPEL8_V_LSX vr0, vr1, vr2, vr3, vr4, vr5, vr6
.ifc \in0, 01
    vavgr.bu      vr13,   vr2,    vr13
    vavgr.bu      vr14,   vr3,    vr14
.else
    vavgr.bu      vr13,   vr3,    vr13
    vavgr.bu      vr14,   vr4,    vr14
.endif
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2

    vldx          vr0,    t2,     t1
    alsl.d        t2,     a2,     t2,    2  // t2 = t2 + 4 *stride
    vld           vr1,    t2,     0
    QPEL8_V_LSX vr2, vr3, vr4, vr5, vr6, vr0, vr1
.ifc \in0, 01
    vavgr.bu      vr13,   vr4,    vr13
    vavgr.bu      vr14,   vr5,    vr14
.else
    vavgr.bu      vr13,   vr5,    vr13
    vavgr.bu      vr14,   vr6,    vr14
.endif
    vstx          vr13,   a0,     t0
    vstx          vr14,   a0,     t1

    alsl.d        a0,     a2,     a0,    2  // dst = dst + 4 * stride

    vldx          vr2,    t2,     a2
    vldx          vr3,    t2,     t0
    QPEL8_V_LSX vr4, vr5, vr6, vr0, vr1, vr2, vr3
.ifc \in0, 01
    vavgr.bu      vr13,   vr6,    vr13
    vavgr.bu      vr14,   vr0,    vr14
.else
    vavgr.bu      vr13,   vr0,    vr13
    vavgr.bu      vr14,   vr1,    vr14
.endif
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2

    vldx          vr4,    t2,     t1
    alsl.d        t2,     a2,     t2,    2 // t2 = t2 + 4 * stride
    vld           vr5,    t2,     0
    QPEL8_V_LSX vr6, vr0, vr1, vr2, vr3, vr4, vr5
.ifc \in0, 01
    vavgr.bu      vr13,   vr1,    vr13
    vavgr.bu      vr14,   vr2,    vr14
.else
    vavgr.bu      vr13,   vr2,    vr13
    vavgr.bu      vr14,   vr3,    vr14
.endif
    vstx          vr13,   a0,     t0
    vstx          vr14,   a0,     t1

    alsl.d        a0,     a2,     a0,    2  // dst = dst + 4 * stride

    vldx          vr6,    t2,     a2
    vldx          vr0,    t2,     t0
    QPEL8_V_LSX vr1, vr2, vr3, vr4, vr5, vr6, vr0
.ifc \in0, 01
    vavgr.bu      vr13,   vr3,    vr13
    vavgr.bu      vr14,   vr4,    vr14
.else
    vavgr.bu      vr13,   vr4,    vr13
    vavgr.bu      vr14,   vr5,    vr14
.endif
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2

    vldx          vr1,    t2,     t1
    alsl.d        t2,     a2,     t2,    2  // t2 = t2 + 4 * stride
    vld           vr2,    t2,     0
    QPEL8_V_LSX vr3, vr4, vr5, vr6, vr0, vr1, vr2
.ifc \in0, 01
    vavgr.bu      vr13,   vr5,    vr13
    vavgr.bu      vr14,   vr6,    vr14
.else
    vavgr.bu      vr13,   vr6,    vr13
    vavgr.bu      vr14,   vr0,    vr14
.endif
    vstx          vr13,   a0,     t0
    vstx          vr14,   a0,     t1

    alsl.d        a0,     a2,     a0,    2  // dst = dst + 4 * stride

    vldx          vr3,    t2,     a2
    vldx          vr4,    t2,     t0
    QPEL8_V_LSX vr5, vr6, vr0, vr1, vr2, vr3, vr4
.ifc \in0, 01
    vavgr.bu      vr13,   vr0,    vr13
    vavgr.bu      vr14,   vr1,    vr14
.else
    vavgr.bu      vr13,   vr1,    vr13
    vavgr.bu      vr14,   vr2,    vr14
.endif
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2

    vldx          vr5,    t2,     t1
    alsl.d        t2,     a2,     t2,    2 // t2 = t2 + 4 * stride
    vld           vr6,    t2,     0
    QPEL8_V_LSX vr0, vr1, vr2, vr3, vr4, vr5, vr6
.ifc \in0, 01
    vavgr.bu      vr13,   vr2,    vr13
    vavgr.bu      vr14,   vr3,    vr14
.else
    vavgr.bu      vr13,   vr3,    vr13
    vavgr.bu      vr14,   vr4,    vr14
.endif
    vstx          vr13,   a0,     t0
    vstx          vr14,   a0,     t1
endfunc
.endm

put_h264_qpel16_mc1 01
put_h264_qpel16_mc1 03

.macro VST_QPEL8_V_LOWPASS_LSX in0, in1, in2, in3, in4, in5, in6, in7, in8
    QPEL8_V_LSX \in0, \in1, \in2, \in3, \in4, \in5, \in6
    vavgr.bu      vr13,   \in7,   vr13
    vavgr.bu      vr14,   \in8,   vr14
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2
.endm

.macro VSTX_QPEL8_V_LOWPASS_LSX in0, in1, in2, in3, in4, in5, in6, in7, in8
    QPEL8_V_LSX \in0, \in1, \in2, \in3, \in4, \in5, \in6
    vavgr.bu      vr13,   \in7,   vr13
    vavgr.bu      vr14,   \in8,   vr14
    vstx          vr13,   a0,     t1
    vstx          vr14,   a0,     t2
.endm

function ff_put_h264_qpel16_mc11_lsx
    addi.d        sp,     sp,     -64
    fst.d         f24,    sp,     0
    fst.d         f25,    sp,     8
    fst.d         f26,    sp,     16
    fst.d         f27,    sp,     24
    fst.d         f28,    sp,     32
    fst.d         f29,    sp,     40
    fst.d         f30,    sp,     48
    fst.d         f31,    sp,     56
    slli.d        t1,     a2,     1
    add.d         t2,     t1,     a2
    slli.d        t6,     t1,     1
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410
    sub.d         t4,     a1,     t1   // t4 = src - 2 * stride
    addi.d        t0,     a1,     -2   // t0 = src - 2
    addi.d        a1,     t0,     8    // a1 = t0 + 8
.rept 2
    VLD_DOUBLE_QPEL8_H_LSX vr12, vr13, vr14, vr15, t0
    alsl.d        t0,     a2,     t0,    2
    VLD_DOUBLE_QPEL8_H_LSX vr16, vr17, vr18, vr19, t0
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr23, vr24, vr25, vr26, vr12, vr13, \
                                  vr14, vr15, a1
    alsl.d        a1,     a2,     a1,    2
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr27, vr28, vr29, vr30, vr16, vr17, \
                                  vr18, vr19, a1

    vld           vr0,    t4,     0      // t4 = src - 2 * stride
    vldx          vr1,    t4,     a2
    vldx          vr2,    t4,     t1
    vldx          vr3,    t4,     t2
    alsl.d        t4,     a2,     t4,    2  // src + 2 *stride
    vld           vr4,    t4,     0
    vldx          vr5,    t4,     a2
    vldx          vr6,    t4,     t1
    VST_QPEL8_V_LOWPASS_LSX vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr23, vr24
    vldx          vr0,    t4,     t2
    alsl.d        t4,     a2,     t4,    2  // src + 6 *stride
    vld           vr1,    t4,     0
    VSTX_QPEL8_V_LOWPASS_LSX vr2, vr3, vr4, vr5, vr6, vr0, vr1, vr25, vr26
    alsl.d        a0,     a2,     a0,    2  // dst = dst + 4 * stride
    vldx          vr2,    t4,     a2
    vldx          vr3,    t4,     t1
    VST_QPEL8_V_LOWPASS_LSX vr4, vr5, vr6, vr0, vr1, vr2, vr3, vr27, vr28
    vldx          vr4,    t4,     t2
    alsl.d        t4,     a2,     t4,    2  // src + 10 *stride
    vld           vr5,    t4,     0
    VSTX_QPEL8_V_LOWPASS_LSX vr6, vr0, vr1, vr2, vr3, vr4, vr5, vr29, vr30
    alsl.d        t0,     a2,     t0,    2
    alsl.d        a1,     a2,     a1,    2   // a1 = src + 8 * stride
    alsl.d        a0,     a2,     a0,    2   // dst = dst + 8 * stride
    sub.d         t4,     t4,     t6
.endr
    fld.d         f24,    sp,     0
    fld.d         f25,    sp,     8
    fld.d         f26,    sp,     16
    fld.d         f27,    sp,     24
    fld.d         f28,    sp,     32
    fld.d         f29,    sp,     40
    fld.d         f30,    sp,     48
    fld.d         f31,    sp,     56
    addi.d        sp,     sp,     64
endfunc

function ff_avg_h264_qpel16_mc00_lsx
    slli.d        t0,     a2,     1
    add.d         t1,     t0,     a2
    slli.d        t2,     t0,     1
    addi.d        t3,     a0,     0
.rept 4
    vld           vr0,    a1,     0
    vldx          vr1,    a1,     a2
    vldx          vr2,    a1,     t0
    vldx          vr3,    a1,     t1
    add.d         a1,     a1,     t2
    vld           vr8,    t3,     0
    vldx          vr9,    t3,     a2
    vldx          vr10,   t3,     t0
    vldx          vr11,   t3,     t1
    add.d         t3,     t3,     t2
    vavgr.bu      vr0,    vr8,    vr0
    vavgr.bu      vr1,    vr9,    vr1
    vavgr.bu      vr2,    vr10,   vr2
    vavgr.bu      vr3,    vr11,   vr3
    vst           vr0,    a0,     0
    vstx          vr1,    a0,     a2
    vstx          vr2,    a0,     t0
    vstx          vr3,    a0,     t1
    add.d         a0,     a0,     t2
.endr
endfunc

.macro put_h264_qpel16_mc in0
function ff_put_h264_qpel16_mc\in0\()_lsx
    addi.d        sp,     sp,     -64
    fst.d         f24,    sp,     0
    fst.d         f25,    sp,     8
    fst.d         f26,    sp,     16
    fst.d         f27,    sp,     24
    fst.d         f28,    sp,     32
    fst.d         f29,    sp,     40
    fst.d         f30,    sp,     48
    fst.d         f31,    sp,     56
    slli.d        t1,     a2,     1
    add.d         t2,     t1,     a2
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410
    addi.d        t0,     a1,     -2   // t0 = src - 2

.ifc \in0, 33
    add.d         t0,     t0,     a2
.endif
    add.d         t3,     a1,     zero // t3 = src
    sub.d         t4,     a1,     t1   // t4 = src - 2 * stride
    addi.d        t4,     t4,     1

    VLD_DOUBLE_QPEL8_H_LSX vr12, vr13, vr14, vr15, t0
    alsl.d        a1,     a2,     t0,    2
    VLD_DOUBLE_QPEL8_H_LSX vr16, vr17, vr18, vr19, a1
    addi.d        a1,     t0,     8
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr23, vr24, vr25, vr26, vr12, vr13, \
                                  vr14, vr15, a1
    alsl.d        a1,     a2,     a1,    2
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr27, vr28, vr29, vr30, vr16, vr17, \
                                  vr18, vr19, a1
    vld           vr0,    t4,     0      // t4 = src - 2 * stride + 1
    vldx          vr1,    t4,     a2
    vldx          vr2,    t4,     t1
    vldx          vr3,    t4,     t2
    alsl.d        t4,     a2,     t4,    2
    vld           vr4,    t4,     0
    vldx          vr5,    t4,     a2
    vldx          vr6,    t4,     t1
    VST_QPEL8_V_LOWPASS_LSX vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr23, vr24
    vldx          vr0,    t4,     t2
    alsl.d        t4,     a2,     t4,    2
    vld           vr1,    t4,     0
    VSTX_QPEL8_V_LOWPASS_LSX vr2, vr3, vr4, vr5, vr6, vr0, vr1, vr25, vr26
    add.d         t6,     t4,     zero     // t6 = src + 6 * stride
    alsl.d        a0,     a2,     a0,    2  // dst = dst + 4 * stride
    vldx          vr2,    t4,     a2
    vldx          vr3,    t4,     t1
    VST_QPEL8_V_LOWPASS_LSX vr4, vr5, vr6, vr0, vr1, vr2, vr3, vr27, vr28
    vldx          vr4,    t4,     t2
    alsl.d        t4,     a2,     t4,    2
    vld           vr5,    t4,     0
    VSTX_QPEL8_V_LOWPASS_LSX vr6, vr0, vr1, vr2, vr3, vr4, vr5, vr29, vr30
    alsl.d        a1,     a2,     t0,    3  // a1 = src + 8 * stride
    addi.d        t5,     a1,     8         // a1 = src + 8 * stride + 8
    VLD_DOUBLE_QPEL8_H_LSX vr12, vr13, vr14, vr15, a1
    alsl.d        a1,     a2,     a1,    2
    VLD_DOUBLE_QPEL8_H_LSX vr16, vr17, vr18, vr19, a1
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr23, vr24, vr25, vr26, vr12, vr13, \
                                  vr14, vr15, t5
    alsl.d        t5,     a2,     t5,    2
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr27, vr28, vr29, vr30, vr16, vr17, \
                                  vr18, vr19, t5
    alsl.d        a0,     a2,     a0,    2   // dst = dst + 4 * stride

    // t6 = src + 6 * stride + 1
    vld           vr0,    t6,     0
    vldx          vr1,    t6,     a2
    vldx          vr2,    t6,     t1
    vldx          vr3,    t6,     t2
    alsl.d        t6,     a2,     t6,    2
    vld           vr4,    t6,     0
    vldx          vr5,    t6,     a2
    vldx          vr6,    t6,     t1
    VST_QPEL8_V_LOWPASS_LSX vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr23, vr24
    vldx          vr0,    t6,     t2
    alsl.d        t6,     a2,     t6,    2
    vld           vr1,    t6,     0
    VSTX_QPEL8_V_LOWPASS_LSX vr2, vr3, vr4, vr5 ,vr6, vr0, vr1, vr25, vr26
    alsl.d        a0,     a2,     a0,    2    // dst = dst + 4 *stride
    vldx          vr2,    t6,     a2
    vldx          vr3,    t6,     t1
    VST_QPEL8_V_LOWPASS_LSX vr4, vr5, vr6, vr0, vr1, vr2, vr3, vr27, vr28
    vldx          vr4,    t6,     t2
    alsl.d        t6,     a2,     t6,    2
    vld           vr5,    t6,     0
    VSTX_QPEL8_V_LOWPASS_LSX vr6, vr0, vr1, vr2, vr3, vr4, vr5, vr29, vr30

    fld.d         f24,    sp,     0
    fld.d         f25,    sp,     8
    fld.d         f26,    sp,     16
    fld.d         f27,    sp,     24
    fld.d         f28,    sp,     32
    fld.d         f29,    sp,     40
    fld.d         f30,    sp,     48
    fld.d         f31,    sp,     56
    addi.d        sp,     sp,     64
endfunc
.endm

put_h264_qpel16_mc 33
put_h264_qpel16_mc 31

function ff_put_h264_qpel16_mc13_lsx
    slli.d        t1,     a2,     1
    add.d         t2,     t1,     a2
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410
    addi.d        sp,     sp,     -64
    fst.d         f24,    sp,     0
    fst.d         f25,    sp,     8
    fst.d         f26,    sp,     16
    fst.d         f27,    sp,     24
    fst.d         f28,    sp,     32
    fst.d         f29,    sp,     40
    fst.d         f30,    sp,     48
    fst.d         f31,    sp,     56
    addi.d        t0,     a1,     -2   // t0 = src - 2
    add.d         t0,     t0,     a2
    add.d         t3,     a1,     zero // t3 = src
    sub.d         t4,     a1,     t1   // t4 = src - 2 * stride

    VLD_DOUBLE_QPEL8_H_LSX vr12, vr13, vr14, vr15, t0
    alsl.d        a1,     a2,     t0,    2
    VLD_DOUBLE_QPEL8_H_LSX vr16, vr17, vr18, vr19, a1
    addi.d        a1,     t0,     8
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr23, vr24, vr25, vr26, vr12, vr13, \
                                  vr14, vr15, a1
    alsl.d        a1,     a2,     a1,    2
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr27, vr28, vr29, vr30, vr16, vr17, \
                                  vr18, vr19, a1
    vld           vr0,    t4,     0      // t4 = src - 2 * stride + 1
    vldx          vr1,    t4,     a2
    vldx          vr2,    t4,     t1
    vldx          vr3,    t4,     t2
    alsl.d        t4,     a2,     t4,    2
    vld           vr4,    t4,     0
    vldx          vr5,    t4,     a2
    vldx          vr6,    t4,     t1
    VST_QPEL8_V_LOWPASS_LSX vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr23, vr24
    vldx          vr0,    t4,     t2
    alsl.d        t4,     a2,     t4,    2
    vld           vr1,    t4,     0
    VSTX_QPEL8_V_LOWPASS_LSX vr2, vr3, vr4, vr5, vr6, vr0, vr1, vr25, vr26
    add.d         t6,     t4,     zero
    alsl.d        a0,     a2,     a0,    2  // dst = dst + 4 * stride
    vldx          vr2,    t4,     a2
    vldx          vr3,    t4,     t1
    VST_QPEL8_V_LOWPASS_LSX vr4, vr5, vr6, vr0, vr1, vr2, vr3, vr27, vr28
    vldx          vr4,    t4,     t2
    alsl.d        t4,     a2,     t4,    2
    vld           vr5,    t4,     0
    VSTX_QPEL8_V_LOWPASS_LSX vr6, vr0, vr1, vr2, vr3, vr4, vr5, vr29, vr30
    alsl.d        a1,     a2,     t0,    3  // a1 = src + 8 * stride
    addi.d        t5,     a1,     8         // a1 = src + 8 * stride + 8
    VLD_DOUBLE_QPEL8_H_LSX vr12, vr13, vr14, vr15, a1
    alsl.d        a1,     a2,     a1,    2
    VLD_DOUBLE_QPEL8_H_LSX vr16, vr17, vr18, vr19, a1
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr23, vr24, vr25, vr26, vr12, vr13, \
                                  vr14, vr15, t5
    alsl.d        t5,     a2,     t5,    2
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr27, vr28, vr29, vr30, vr16, vr17, \
                                  vr18, vr19, t5
    alsl.d        a0,     a2,     a0,    2   // dst = dst + 4 * stride

    vld           vr0,    t6,     0          // // t6 = src + 6 * stride + 1
    vldx          vr1,    t6,     a2
    vldx          vr2,    t6,     t1
    vldx          vr3,    t6,     t2
    alsl.d        t6,     a2,     t6,    2
    vld           vr4,    t6,     0
    vldx          vr5,    t6,     a2
    vldx          vr6,    t6,     t1
    VST_QPEL8_V_LOWPASS_LSX vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr23, vr24
    vldx          vr0,    t6,     t2
    alsl.d        t6,     a2,     t6,    2
    vld           vr1,    t6,     0
    VSTX_QPEL8_V_LOWPASS_LSX vr2, vr3, vr4, vr5, vr6, vr0, vr1, vr25, vr26
    alsl.d        a0,     a2,     a0,    2    // dst = dst + 4 *stride
    vldx          vr2,    t6,     a2
    vldx          vr3,    t6,     t1
    VST_QPEL8_V_LOWPASS_LSX vr4, vr5, vr6, vr0, vr1, vr2, vr3, vr27, vr28
    vldx          vr4,    t6,     t2
    alsl.d        t6,     a2,     t6,    2
    vld           vr5,    t6,     0
    VSTX_QPEL8_V_LOWPASS_LSX vr6, vr0, vr1, vr2, vr3, vr4, vr5, vr29, vr30
    fld.d         f24,    sp,     0
    fld.d         f25,    sp,     8
    fld.d         f26,    sp,     16
    fld.d         f27,    sp,     24
    fld.d         f28,    sp,     32
    fld.d         f29,    sp,     40
    fld.d         f30,    sp,     48
    fld.d         f31,    sp,     56
    addi.d        sp,     sp,     64
endfunc

function ff_avg_h264_qpel16_mc10_lsx
    addi.d        t0,     a0,     0   // t0 = dst
    addi.d        t4,     a1,     -2  // t1 = src - 2
    addi.d        t5,     t4,     8
    slli.d        t1,     a2,     1
    add.d         t2,     a2,     t1
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410
.rept 2
    VLD_DOUBLE_QPEL8_H_LSX vr12, vr13, vr14, vr15, t4
    alsl.d        t4,     a2,     t4,    2
    VLD_DOUBLE_QPEL8_H_LSX vr16, vr17, vr18, vr19, t4
    VLD_QPEL8_H_SSRANI_LSX vr2, vr3, vr12, vr13, t5
    vld           vr0,    a1,     0
    vldx          vr1,    a1,     a2
    vld           vr12,   t0,     0
    vldx          vr13,   t0,     a2
    vavgr.bu      vr0,    vr0,    vr2
    vavgr.bu      vr1,    vr1,    vr3
    vavgr.bu      vr0,    vr0,    vr12
    vavgr.bu      vr1,    vr1,    vr13
    vst           vr0,    a0,     0
    vstx          vr1,    a0,     a2
    VLDX_QPEL8_H_SSRANI_LSX vr2, vr3, vr14, vr15, t5
    vldx          vr0,    a1,     t1
    vldx          vr1,    a1,     t2
    vldx          vr12,   t0,     t1
    vldx          vr13,   t0,     t2
    vavgr.bu      vr0,    vr0,    vr2
    vavgr.bu      vr1,    vr1,    vr3
    vavgr.bu      vr0,    vr0,    vr12
    vavgr.bu      vr1,    vr1,    vr13
    vstx          vr0,    a0,     t1
    vstx          vr1,    a0,     t2
    alsl.d        t5,     a2,     t5,    2
    alsl.d        a1,     a2,     a1,    2
    alsl.d        t0,     a2,     t0,    2
    alsl.d        a0,     a2,     a0,    2
    VLD_QPEL8_H_SSRANI_LSX vr2, vr3, vr16, vr17, t5
    vld           vr0,    a1,     0
    vldx          vr1,    a1,     a2
    vld           vr12,   t0,     0
    vldx          vr13,   t0,     a2
    vavgr.bu      vr0,    vr0,    vr2
    vavgr.bu      vr1,    vr1,    vr3
    vavgr.bu      vr0,    vr0,    vr12
    vavgr.bu      vr1,    vr1,    vr13
    vst           vr0,    a0,     0
    vstx          vr1,    a0,     a2
    VLDX_QPEL8_H_SSRANI_LSX vr2, vr3, vr18, vr19, t5
    vldx          vr0,    a1,     t1
    vldx          vr1,    a1,     t2
    vldx          vr12,   t0,     t1
    vldx          vr13,   t0,     t2
    vavgr.bu      vr0,    vr0,    vr2
    vavgr.bu      vr1,    vr1,    vr3
    vavgr.bu      vr0,    vr0,    vr12
    vavgr.bu      vr1,    vr1,    vr13
    vstx          vr0,    a0,     t1
    vstx          vr1,    a0,     t2
    alsl.d        t5,     a2,     t5,    2
    alsl.d        a1,     a2,     a1,    2
    alsl.d        t0,     a2,     t0,    2
    alsl.d        a0,     a2,     a0,    2
    alsl.d        t4,     a2,     t4,    2   // src + 8 * stride -2
.endr
endfunc

function ff_avg_h264_qpel16_mc30_lsx
    addi.d        t0,     a0,     0   // t0 = dst
    addi.d        t4,     a1,     -2  // t1 = src - 2
    addi.d        t5,     t4,     8
    addi.d        a1,     a1,     1   // a1 = a1 + 1
    slli.d        t1,     a2,     1
    add.d         t2,     a2,     t1
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410
.rept 2
    VLD_DOUBLE_QPEL8_H_LSX vr12, vr13, vr14, vr15, t4
    alsl.d        t4,     a2,     t4,    2
    VLD_DOUBLE_QPEL8_H_LSX vr16, vr17, vr18, vr19, t4
    VLD_QPEL8_H_SSRANI_LSX vr2, vr3, vr12, vr13, t5
    vld           vr0,    a1,     0
    vldx          vr1,    a1,     a2
    vld           vr12,   t0,     0
    vldx          vr13,   t0,     a2
    vavgr.bu      vr0,    vr0,    vr2
    vavgr.bu      vr1,    vr1,    vr3
    vavgr.bu      vr0,    vr0,    vr12
    vavgr.bu      vr1,    vr1,    vr13
    vst           vr0,    a0,     0
    vstx          vr1,    a0,     a2
    VLDX_QPEL8_H_SSRANI_LSX vr2, vr3, vr14, vr15, t5
    vldx          vr0,    a1,     t1
    vldx          vr1,    a1,     t2
    vldx          vr12,   t0,     t1
    vldx          vr13,   t0,     t2
    vavgr.bu      vr0,    vr0,    vr2
    vavgr.bu      vr1,    vr1,    vr3
    vavgr.bu      vr0,    vr0,    vr12
    vavgr.bu      vr1,    vr1,    vr13
    vstx          vr0,    a0,     t1
    vstx          vr1,    a0,     t2
    alsl.d        t5,     a2,     t5,    2
    alsl.d        a1,     a2,     a1,    2
    alsl.d        t0,     a2,     t0,    2
    alsl.d        a0,     a2,     a0,    2
    VLD_QPEL8_H_SSRANI_LSX vr2, vr3, vr16, vr17, t5
    vld           vr0,    a1,     0
    vldx          vr1,    a1,     a2
    vld           vr12,   t0,     0
    vldx          vr13,   t0,     a2
    vavgr.bu      vr0,    vr0,    vr2
    vavgr.bu      vr1,    vr1,    vr3
    vavgr.bu      vr0,    vr0,    vr12
    vavgr.bu      vr1,    vr1,    vr13
    vst           vr0,    a0,     0
    vstx          vr1,    a0,     a2
    VLDX_QPEL8_H_SSRANI_LSX vr2, vr3, vr18, vr19, t5
    vldx          vr0,    a1,     t1
    vldx          vr1,    a1,     t2
    vldx          vr12,   t0,     t1
    vldx          vr13,   t0,     t2
    vavgr.bu      vr0,    vr0,    vr2
    vavgr.bu      vr1,    vr1,    vr3
    vavgr.bu      vr0,    vr0,    vr12
    vavgr.bu      vr1,    vr1,    vr13
    vstx          vr0,    a0,     t1
    vstx          vr1,    a0,     t2
    alsl.d        t5,     a2,     t5,    2
    alsl.d        a1,     a2,     a1,    2
    alsl.d        t0,     a2,     t0,    2
    alsl.d        a0,     a2,     a0,    2
    alsl.d        t4,     a2,     t4,    2   // t1 = src + 8 * stride -2
.endr
endfunc

function ff_put_h264_qpel16_mc02_lsx
    slli.d        t0,     a2,     1
    add.d         t1,     t0,     a2
    sub.d         t2,     a1,     t0  // t2 = src - 2 * stride
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410

    vld           vr0,    t2,     0
    vldx          vr1,    t2,     a2
    vldx          vr2,    t2,     t0
    vldx          vr3,    t2,     t1
    alsl.d        t2,     a2,     t2,    2  // t2 = t2 + 4 * stride
    vld           vr4,    t2,     0
    vldx          vr5,    t2,     a2
    vldx          vr6,    t2,     t0
    QPEL8_V_LSX   vr0, vr1, vr2, vr3, vr4, vr5, vr6
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2
    vldx          vr0,    t2,     t1
    alsl.d        t2,     a2,     t2,    2  // t2 = t2 + 4 *stride
    vld           vr1,    t2,     0
    QPEL8_V_LSX   vr2, vr3, vr4, vr5, vr6, vr0, vr1
    vstx          vr13,   a0,     t0
    vstx          vr14,   a0,     t1
    alsl.d        a0,     a2,     a0,    2  // dst = dst + 4 * stride
    vldx          vr2,    t2,     a2
    vldx          vr3,    t2,     t0
    QPEL8_V_LSX   vr4, vr5, vr6, vr0, vr1, vr2, vr3
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2
    vldx          vr4,    t2,     t1
    alsl.d        t2,     a2,     t2,    2 // t2 = t2 + 4 * stride
    vld           vr5,    t2,     0
    QPEL8_V_LSX   vr6, vr0, vr1, vr2, vr3, vr4, vr5
    vstx          vr13,   a0,     t0
    vstx          vr14,   a0,     t1

    alsl.d        a0,     a2,     a0,    2  // dst = dst + 4 * stride

    vldx          vr6,    t2,     a2
    vldx          vr0,    t2,     t0
    QPEL8_V_LSX   vr1, vr2, vr3, vr4, vr5, vr6, vr0
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2
    vldx          vr1,    t2,     t1
    alsl.d        t2,     a2,     t2,    2  // t2 = t2 + 4 * stride
    vld           vr2,    t2,     0
    QPEL8_V_LSX   vr3, vr4, vr5, vr6, vr0, vr1, vr2
    vstx          vr13,   a0,     t0
    vstx          vr14,   a0,     t1
    alsl.d        a0,     a2,     a0,    2  // dst = dst + 4 * stride
    vldx          vr3,    t2,     a2
    vldx          vr4,    t2,     t0
    QPEL8_V_LSX   vr5, vr6, vr0, vr1, vr2, vr3, vr4
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2
    vldx          vr5,    t2,     t1
    alsl.d        t2,     a2,     t2,    2 // t2 = t2 + 4 * stride
    vld           vr6,    t2,     0
    QPEL8_V_LSX   vr0, vr1, vr2, vr3, vr4, vr5, vr6
    vstx          vr13,   a0,     t0
    vstx          vr14,   a0,     t1
endfunc

.macro avc_luma_hv_qrt_and_aver_dst_16x16_lsx
    addi.d        sp,     sp,     -64
    fst.d         f24,    sp,     0
    fst.d         f25,    sp,     8
    fst.d         f26,    sp,     16
    fst.d         f27,    sp,     24
    fst.d         f28,    sp,     32
    fst.d         f29,    sp,     40
    fst.d         f30,    sp,     48
    fst.d         f31,    sp,     56
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410

    VLD_DOUBLE_QPEL8_H_LSX vr12, vr13, vr14, vr15, t0
    alsl.d        a1,     a2,     t0,    2
    VLD_DOUBLE_QPEL8_H_LSX vr16, vr17, vr18, vr19, a1
    addi.d        a1,     t0,     8
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr23, vr24, vr25, vr26, vr12, vr13, \
                                  vr14, vr15, a1
    alsl.d        a1,     a2,     a1,    2
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr27, vr28, vr29, vr30, vr16, vr17, \
                                  vr18, vr19, a1
    vld           vr0,    t4,     0      // t4 = src - 2 * stride + 1
    vldx          vr1,    t4,     a2
    vldx          vr2,    t4,     t1
    vldx          vr3,    t4,     t2
    alsl.d        t4,     a2,     t4,    2
    vld           vr4,    t4,     0
    vldx          vr5,    t4,     a2
    vldx          vr6,    t4,     t1
    QPEL8_V_LSX   vr0, vr1, vr2, vr3, vr4, vr5, vr6
    vld           vr0,    t8,     0
    vldx          vr1,    t8,     a2
    vavgr.bu      vr13,   vr23,   vr13
    vavgr.bu      vr14,   vr24,   vr14
    vavgr.bu      vr13,   vr13,   vr0
    vavgr.bu      vr14,   vr14,   vr1
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2
    vldx          vr0,    t4,     t2
    alsl.d        t4,     a2,     t4,    2
    vld           vr1,    t4,     0
    QPEL8_V_LSX   vr2, vr3, vr4, vr5, vr6, vr0, vr1
    vldx          vr2,    t8,     t1
    vldx          vr3,    t8,     t2
    vavgr.bu      vr13,   vr25,   vr13
    vavgr.bu      vr14,   vr26,   vr14
    vavgr.bu      vr13,   vr13,   vr2
    vavgr.bu      vr14,   vr14,   vr3
    add.d         t6,     t4,     zero     // t6 = src + 6 * stride
    vstx          vr13,   a0,     t1
    vstx          vr14,   a0,     t2
    alsl.d        a0,     a2,     a0,    2  // dst = dst + 4 * stride
    alsl.d        t8,     a2,     t8,    2
    vldx          vr2,    t4,     a2
    vldx          vr3,    t4,     t1
    QPEL8_V_LSX   vr4, vr5, vr6, vr0, vr1, vr2, vr3
    vld           vr4,    t8,     0
    vldx          vr5,    t8,     a2
    vavgr.bu      vr13,   vr27,   vr13
    vavgr.bu      vr14,   vr28,   vr14
    vavgr.bu      vr13,   vr13,   vr4
    vavgr.bu      vr14,   vr14,   vr5
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2
    vldx          vr4,    t4,     t2
    alsl.d        t4,     a2,     t4,    2
    vld           vr5,    t4,     0
    QPEL8_V_LSX   vr6, vr0, vr1, vr2, vr3, vr4, vr5
    vldx          vr6,    t8,     t1
    vldx          vr0,    t8,     t2
    vavgr.bu      vr13,   vr29,   vr13
    vavgr.bu      vr14,   vr30,   vr14
    vavgr.bu      vr13,   vr13,   vr6
    vavgr.bu      vr14,   vr14,   vr0
    vstx          vr13,   a0,     t1
    vstx          vr14,   a0,     t2
    alsl.d        a1,     a2,     t0,    3  // a1 = src + 8 * stride
    addi.d        t5,     a1,     8         // a1 = src + 8 * stride + 8
    VLD_DOUBLE_QPEL8_H_LSX vr12, vr13, vr14, vr15, a1
    alsl.d        a1,     a2,     a1,    2
    VLD_DOUBLE_QPEL8_H_LSX vr16, vr17, vr18, vr19, a1
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr23, vr24, vr25, vr26, vr12, vr13, \
                                  vr14, vr15, t5
    alsl.d        t5,     a2,     t5,    2
    VLD_DOUBLE_QPEL8_H_SSRANI_LSX vr27, vr28, vr29, vr30, vr16, vr17, \
                                  vr18, vr19, t5
    alsl.d        a0,     a2,     a0,    2   // dst = dst + 4 * stride
    alsl.d        t8,     a2,     t8,    2
    // t6 = src + 6 * stride + 1
    vld           vr0,    t6,     0
    vldx          vr1,    t6,     a2
    vldx          vr2,    t6,     t1
    vldx          vr3,    t6,     t2
    alsl.d        t6,     a2,     t6,    2
    vld           vr4,    t6,     0
    vldx          vr5,    t6,     a2
    vldx          vr6,    t6,     t1
    QPEL8_V_LSX   vr0, vr1, vr2, vr3, vr4, vr5, vr6
    vld           vr0,    t8,     0
    vldx          vr1,    t8,     a2
    vavgr.bu      vr13,   vr23,   vr13
    vavgr.bu      vr14,   vr24,   vr14
    vavgr.bu      vr13,   vr13,   vr0
    vavgr.bu      vr14,   vr14,   vr1
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2
    vldx          vr0,    t6,     t2
    alsl.d        t6,     a2,     t6,    2
    vld           vr1,    t6,     0
    QPEL8_V_LSX   vr2, vr3, vr4, vr5, vr6, vr0, vr1
    vldx          vr2,    t8,     t1
    vldx          vr3,    t8,     t2
    vavgr.bu      vr13,   vr25,   vr13
    vavgr.bu      vr14,   vr26,   vr14
    vavgr.bu      vr13,   vr13,   vr2
    vavgr.bu      vr14,   vr14,   vr3
    vstx          vr13,   a0,     t1
    vstx          vr14,   a0,     t2
    alsl.d        a0,     a2,     a0,    2    // dst = dst + 4 *stride
    alsl.d        t8,     a2,     t8,    2
    vldx          vr2,    t6,     a2
    vldx          vr3,    t6,     t1
    QPEL8_V_LSX   vr4, vr5, vr6, vr0, vr1, vr2, vr3
    vld           vr4,    t8,     0
    vldx          vr5,    t8,     a2
    vavgr.bu      vr13,   vr27,   vr13
    vavgr.bu      vr14,   vr28,   vr14
    vavgr.bu      vr13,   vr13,   vr4
    vavgr.bu      vr14,   vr14,   vr5
    vst           vr13,   a0,     0
    vstx          vr14,   a0,     a2
    vldx          vr4,    t6,     t2
    alsl.d        t6,     a2,     t6,    2
    vld           vr5,    t6,     0
    QPEL8_V_LSX   vr6, vr0, vr1, vr2, vr3, vr4, vr5
    vldx          vr6,    t8,     t1
    vldx          vr0,    t8,     t2
    vavgr.bu      vr13,   vr29,   vr13
    vavgr.bu      vr14,   vr30,   vr14
    vavgr.bu      vr13,   vr13,   vr6
    vavgr.bu      vr14,   vr14,   vr0
    vstx          vr13,   a0,     t1
    vstx          vr14,   a0,     t2
    fld.d         f24,    sp,     0
    fld.d         f25,    sp,     8
    fld.d         f26,    sp,     16
    fld.d         f27,    sp,     24
    fld.d         f28,    sp,     32
    fld.d         f29,    sp,     40
    fld.d         f30,    sp,     48
    fld.d         f31,    sp,     56
    addi.d        sp,     sp,     64
.endm

function ff_avg_h264_qpel16_mc33_lsx
    slli.d        t1,     a2,     1
    add.d         t2,     t1,     a2
    addi.d        t0,     a1,     -2   // t0 = src - 2
    add.d         t0,     t0,     a2   // t0 = src + stride - 2
    add.d         t3,     a1,     zero // t3 = src
    sub.d         t4,     a1,     t1   // t4 = src - 2 * stride
    addi.d        t4,     t4,     1
    addi.d        t8,     a0,     0
    avc_luma_hv_qrt_and_aver_dst_16x16_lsx
endfunc

function ff_avg_h264_qpel16_mc11_lsx
    slli.d        t1,     a2,     1
    add.d         t2,     t1,     a2
    addi.d        t0,     a1,     -2   // t0 = src - 2
    add.d         t3,     a1,     zero // t3 = src
    sub.d         t4,     a1,     t1   // t4 = src - 2 * stride
    addi.d        t8,     a0,     0
    avc_luma_hv_qrt_and_aver_dst_16x16_lsx
endfunc

function ff_avg_h264_qpel16_mc31_lsx
    slli.d        t1,     a2,     1
    add.d         t2,     t1,     a2
    addi.d        t0,     a1,     -2   // t0 = src - 2
    add.d         t3,     a1,     zero // t3 = src
    sub.d         t4,     a1,     t1   // t4 = src - 2 * stride
    addi.d        t4,     t4,     1
    addi.d        t8,     a0,     0
    avc_luma_hv_qrt_and_aver_dst_16x16_lsx
endfunc

function ff_avg_h264_qpel16_mc13_lsx
    slli.d        t1,     a2,     1
    add.d         t2,     t1,     a2
    addi.d        t0,     a1,     -2   // t0 = src - 2
    add.d         t0,     t0,     a2
    add.d         t3,     a1,     zero // t3 = src
    sub.d         t4,     a1,     t1   // t4 = src - 2 * stride
    addi.d        t8,     a0,     0
    avc_luma_hv_qrt_and_aver_dst_16x16_lsx
endfunc

function ff_avg_h264_qpel16_mc20_lsx
    slli.d        t1,     a2,     1
    add.d         t2,     t1,     a2
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410
    addi.d        t0,     a1,     -2   // t0 = src - 2
    addi.d        t5,     a0,     0
    addi.d        a1,     t0,     8
.rept 4
    VLD_DOUBLE_QPEL8_H_LSX vr12, vr13, vr14, vr15, t0
    VLD_QPEL8_H_SSRANI_LSX vr2, vr3, vr12, vr13, a1
    vld           vr0,    t5,     0
    vldx          vr1,    t5,     a2
    vavgr.bu      vr0,    vr0,    vr2
    vavgr.bu      vr1,    vr1,    vr3
    vst           vr0,    a0,     0
    vstx          vr1,    a0,     a2
    add.d         a1,     a1,     t1
    VLD_QPEL8_H_SSRANI_LSX vr2, vr3, vr14, vr15, a1
    vldx          vr0,    t5,     t1
    vldx          vr1,    t5,     t2
    vavgr.bu      vr0,    vr0,    vr2
    vavgr.bu      vr1,    vr1,    vr3
    vstx          vr0,    a0,     t1
    vstx          vr1,    a0,     t2
    alsl.d        t0,     a2,     t0,    2
    alsl.d        t5,     a2,     t5,    2
    alsl.d        a0,     a2,     a0,    2
    alsl.d        a1,     a2,     a1,    1
.endr
endfunc

.macro QPEL8_HV_H_LSX out0, out1
    vbsrl.v       vr2,    vr0,    1
    vbsrl.v       vr3,    vr1,    1
    vbsrl.v       vr4,    vr0,    2
    vbsrl.v       vr5,    vr1,    2
    vbsrl.v       vr6,    vr0,    3
    vbsrl.v       vr7,    vr1,    3
    vbsrl.v       vr8,    vr0,    4
    vbsrl.v       vr9,    vr1,    4
    vbsrl.v       vr10,   vr0,    5
    vbsrl.v       vr11,   vr1,    5
    vilvl.b       vr6,    vr4,    vr6
    vilvl.b       vr7,    vr5,    vr7
    vilvl.b       vr8,    vr2,    vr8
    vilvl.b       vr9,    vr3,    vr9
    vilvl.b       vr10,   vr0,    vr10
    vilvl.b       vr11,   vr1,    vr11
    vhaddw.hu.bu  vr6,    vr6,    vr6
    vhaddw.hu.bu  vr7,    vr7,    vr7
    vhaddw.hu.bu  vr8,    vr8,    vr8
    vhaddw.hu.bu  vr9,    vr9,    vr9
    vhaddw.hu.bu  vr10,   vr10,   vr10
    vhaddw.hu.bu  vr11,   vr11,   vr11
    vmul.h        vr2,    vr6,    vr20
    vmul.h        vr3,    vr7,    vr20
    vmul.h        vr4,    vr8,    vr21
    vmul.h        vr5,    vr9,    vr21
    vssub.h       vr2,    vr2,    vr4
    vssub.h       vr3,    vr3,    vr5
    vsadd.h       \out0,  vr2,    vr10
    vsadd.h       \out1,  vr3,    vr11
.endm

.macro QPEL8_HV_V_LSX in0, in1, in2, in3, in4, in5, in6, out0, out1, out2, out3
    vilvl.h       vr0,    \in2,   \in3
    vilvl.h       vr1,    \in3,   \in4  // tmp0
    vilvl.h       vr2,    \in1,   \in4
    vilvl.h       vr3,    \in2,   \in5  // tmp2
    vilvl.h       vr4,    \in0,   \in5
    vilvl.h       vr5,    \in1,   \in6  // tmp4
    vhaddw.w.h    vr0,    vr0,    vr0
    vhaddw.w.h    vr1,    vr1,    vr1
    vhaddw.w.h    vr2,    vr2,    vr2
    vhaddw.w.h    vr3,    vr3,    vr3
    vhaddw.w.h    vr4,    vr4,    vr4
    vhaddw.w.h    vr5,    vr5,    vr5
    vmul.w        vr0,    vr0,    vr22
    vmul.w        vr1,    vr1,    vr22
    vmul.w        vr2,    vr2,    vr23
    vmul.w        vr3,    vr3,    vr23
    vssub.w       vr0,    vr0,    vr2
    vssub.w       vr1,    vr1,    vr3
    vsadd.w       vr0,    vr0,    vr4
    vsadd.w       vr1,    vr1,    vr5
    vsadd.w       \out0,  vr0,    vr24
    vsadd.w       \out1,  vr1,    vr24
    vilvh.h       vr0,    \in2,   \in3
    vilvh.h       vr1,    \in3,   \in4  // tmp0
    vilvh.h       vr2,    \in1,   \in4
    vilvh.h       vr3,    \in2,   \in5  // tmp2
    vilvh.h       vr4,    \in0,   \in5
    vilvh.h       vr5,    \in1,   \in6  // tmp4
    vhaddw.w.h    vr0,    vr0,    vr0
    vhaddw.w.h    vr1,    vr1,    vr1
    vhaddw.w.h    vr2,    vr2,    vr2
    vhaddw.w.h    vr3,    vr3,    vr3
    vhaddw.w.h    vr4,    vr4,    vr4
    vhaddw.w.h    vr5,    vr5,    vr5
    vmul.w        vr0,    vr0,    vr22
    vmul.w        vr1,    vr1,    vr22
    vmul.w        vr2,    vr2,    vr23
    vmul.w        vr3,    vr3,    vr23
    vssub.w       vr0,    vr0,    vr2
    vssub.w       vr1,    vr1,    vr3
    vsadd.w       vr0,    vr0,    vr4
    vsadd.w       vr1,    vr1,    vr5
    vsadd.w       \out2,  vr0,    vr24
    vsadd.w       \out3,  vr1,    vr24
    vssrani.hu.w  \out2,  \out0,  10
    vssrani.hu.w  \out3,  \out1,  10
    vssrani.bu.h  \out3,  \out2,  0
.endm

.macro h264_qpel8_hv_lowpass_core_lsx in0, in1, type
    vld           vr0,    \in0,  0
    vldx          vr1,    \in0,  a3
    QPEL8_HV_H_LSX vr12, vr13 // a b$
    vldx          vr0,    \in0,  t1
    vldx          vr1,    \in0,  t2
    QPEL8_HV_H_LSX vr14, vr15 // c d$

    alsl.d        \in0,   a3,    \in0,   2

    vld           vr0,    \in0,  0
    vldx          vr1,    \in0,  a3
    QPEL8_HV_H_LSX vr16, vr17 // e f$
    vldx          vr0,    \in0,  t1
    vldx          vr1,    \in0,  t2
    QPEL8_HV_H_LSX vr18, vr19 // g h$
    QPEL8_HV_V_LSX vr12, vr13, vr14, vr15, vr16, vr17, vr18, vr6, vr7, vr0, vr1
.ifc \type, avg
    fld.d         f2,     t3,      0
    fldx.d        f3,     t3,      a2
    vilvl.d       vr2,    vr3,     vr2
    vavgr.bu      vr1,    vr2,     vr1
.endif
    vstelm.d      vr1,    \in1,    0,     0
    add.d         \in1,   \in1,    a2
    vstelm.d      vr1,    \in1,    0,     1

    alsl.d        \in0,    a3,    \in0,   2

    // tmp8
    vld           vr0,    \in0,   0
    vldx          vr1,    \in0,   a3
    QPEL8_HV_H_LSX vr12, vr13
    QPEL8_HV_V_LSX vr14, vr15, vr16, vr17, vr18, vr19, vr12, vr6, vr7, vr0, vr1
.ifc \type, avg
    fldx.d        f2,     t3,      t5
    fldx.d        f3,     t3,      t6
    vilvl.d       vr2,    vr3,     vr2
    vavgr.bu      vr1,    vr2,     vr1
.endif
    add.d         \in1,   \in1,   a2
    vstelm.d      vr1,    \in1,   0,     0
    add.d         \in1,   \in1,   a2
    vstelm.d      vr1,    \in1,   0,     1

    // tmp10
    vldx          vr0,    \in0,   t1
    vldx          vr1,    \in0,   t2
    QPEL8_HV_H_LSX vr14, vr15
    QPEL8_HV_V_LSX vr16, vr17, vr18, vr19, vr12, vr13, vr14, vr6, vr7, vr0, vr1
.ifc \type, avg
    alsl.d        t3,     a2,      t3,   2
    fld.d         f2,     t3,      0
    fldx.d        f3,     t3,      a2
    vilvl.d       vr2,    vr3,     vr2
    vavgr.bu      vr1,    vr2,     vr1
.endif
    add.d         \in1,   \in1,   a2
    vstelm.d      vr1,    \in1,   0,     0
    add.d         \in1,   \in1,   a2
    vstelm.d      vr1,    \in1,   0,     1

    // tmp12
    alsl.d        \in0,   a3,     \in0,  2

    vld           vr0,    \in0,   0
    vldx          vr1,    \in0,   a3
    QPEL8_HV_H_LSX vr16, vr17
    QPEL8_HV_V_LSX vr18, vr19, vr12, vr13, vr14, vr15, vr16, vr6, vr7, vr0, vr1
.ifc \type, avg
    fldx.d        f2,     t3,     t5
    fldx.d        f3,     t3,     t6
    vilvl.d       vr2,    vr3,    vr2
    vavgr.bu      vr1,    vr2,    vr1
.endif
    add.d         \in1,   \in1,   a2
    vstelm.d      vr1,    \in1,   0,     0
    add.d         \in1,   \in1,   a2
    vstelm.d      vr1,    \in1,   0,     1
.endm

function put_h264_qpel8_hv_lowpass_lsx
    slli.d        t1,     a3,     1
    add.d         t2,     t1,     a3
    addi.d        sp,     sp,     -8
    fst.d         f24,    sp,     0
    addi.d        t0,     a1,     -2   // t0 = src - 2
    sub.d         t0,     t0,     t1   // t0 = t0 - 2 * stride
    vldi          vr20,   0x414   // h_20
    vldi          vr21,   0x405   // h_5
    vldi          vr22,   0x814   // w_20
    vldi          vr23,   0x805   // w_5
    addi.d        t4,     zero,   512
    vreplgr2vr.w  vr24,   t4      // w_512
    h264_qpel8_hv_lowpass_core_lsx t0, a0, put
    fld.d         f24,    sp,     0
    addi.d        sp,     sp,     8
endfunc

function put_h264_qpel8_h_lowpass_lsx
    slli.d        t1,     a3,     1
    add.d         t2,     t1,     a3
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410
    addi.d        t0,     a1,     -2   // t0 = src - 2
    add.d         t3,     a1,     zero // t3 = src
.rept 2
    vld           vr0,    t0,     0
    vldx          vr1,    t0,     a3
    QPEL8_H_LSX   vr12, vr13
    vssrani.bu.h  vr13,   vr12,   5
    vstelm.d      vr13,   a0,     0,    0
    add.d         a0,     a0,     a2
    vstelm.d      vr13,   a0,     0,    1
    add.d         a0,     a0,     a2
    vldx          vr0,    t0,     t1
    vldx          vr1,    t0,     t2
    QPEL8_H_LSX   vr12, vr13
    vssrani.bu.h  vr13,   vr12,   5
    vstelm.d      vr13,   a0,     0,    0
    add.d         a0,     a0,     a2
    vstelm.d      vr13,   a0,     0,    1
    add.d         a0,     a0,     a2
    alsl.d        t0,     a3,     t0,    2
.endr
endfunc

function put_pixels16_l2_8_lsx
    slli.d        t0,     a4,     1
    add.d         t1,     t0,     a4
    slli.d        t2,     t0,     1
    slli.d        t3,     a3,     1
    add.d         t4,     t3,     a3
    slli.d        t5,     t3,     1
.rept 4
    vld           vr0,    a1,     0
    vldx          vr1,    a1,     a4
    vldx          vr2,    a1,     t0
    vldx          vr3,    a1,     t1
    add.d         a1,     a1,     t2
    vld           vr8,    a2,     0x00
    vld           vr9,    a2,     0x10
    vld           vr10,   a2,     0x20
    vld           vr11,   a2,     0x30
    addi.d        a2,     a2,     0x40
    vavgr.bu      vr0,    vr8,    vr0
    vavgr.bu      vr1,    vr9,    vr1
    vavgr.bu      vr2,    vr10,   vr2
    vavgr.bu      vr3,    vr11,   vr3
    vst           vr0,    a0,     0
    vstx          vr1,    a0,     a3
    vstx          vr2,    a0,     t3
    vstx          vr3,    a0,     t4
    add.d         a0,     a0,     t5
.endr
endfunc

.macro QPEL8_V1_LSX in0, in1, in2, in3, in4, in5, in6
    vilvl.b       vr7,    \in3,   \in2
    vilvl.b       vr8,    \in4,   \in3
    vilvl.b       vr9,    \in4,   \in1
    vilvl.b       vr10,   \in5,   \in2
    vilvl.b       vr11,   \in5,   \in0
    vilvl.b       vr12,   \in6,   \in1
    vhaddw.hu.bu  vr7,    vr7,    vr7
    vhaddw.hu.bu  vr8,    vr8,    vr8
    vhaddw.hu.bu  vr9,    vr9,    vr9
    vhaddw.hu.bu  vr10,   vr10,   vr10
    vhaddw.hu.bu  vr11,   vr11,   vr11
    vhaddw.hu.bu  vr12,   vr12,   vr12
    vmul.h        vr7,    vr7,    vr20
    vmul.h        vr8,    vr8,    vr20
    vmul.h        vr9,    vr9,    vr21
    vmul.h        vr10,   vr10,   vr21
    vssub.h       vr7,    vr7,    vr9
    vssub.h       vr8,    vr8,    vr10
    vsadd.h       vr7,    vr7,    vr11
    vsadd.h       vr8,    vr8,    vr12
    vsadd.h       vr7,    vr7,    vr22
    vsadd.h       vr8,    vr8,    vr22
    vssrani.bu.h  vr8,    vr7,    5
.endm

.macro h264_qpel8_v_lowpass_lsx type
function \type\()_h264_qpel8_v_lowpass_lsx
    slli.d        t0,     a3,     1
    add.d         t1,     t0,     a3
    sub.d         t2,     a1,     t0  // t2 = src - 2 * stride
.ifc \type, avg
    addi.d        t3,     a0,     0
    slli.d        t4,     a2,     1
    add.d         t5,     t4,     a2
.endif
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410

    fld.d         f0,     t2,     0
    fldx.d        f1,     t2,     a3
    fldx.d        f2,     t2,     t0
    fldx.d        f3,     t2,     t1
    alsl.d        t2,     a3,     t2,    2  // t2 = t2 + 4 * stride
    fld.d         f4,     t2,     0
    fldx.d        f5,     t2,     a3
    fldx.d        f6,     t2,     t0
    QPEL8_V1_LSX  vr0, vr1, vr2, vr3, vr4, vr5, vr6
.ifc \type, avg
    fld.d         f0,     t3,     0
    fldx.d        f1,     t3,     a2
    vilvl.d       vr0,    vr1,    vr0
    vavgr.bu      vr8,    vr8,    vr0
.endif
    vstelm.d      vr8,    a0,     0,    0
    add.d         a0,     a0,     a2
    vstelm.d      vr8,    a0,     0,    1
    add.d         a0,     a0,     a2

    fldx.d        f0,     t2,     t1
    alsl.d        t2,     a3,     t2,   2  // t2 = t2 + 4 *stride
    fld.d         f1,     t2,     0
    QPEL8_V1_LSX  vr2, vr3, vr4, vr5, vr6, vr0, vr1
.ifc \type, avg
    fldx.d        f2,     t3,     t4
    fldx.d        f3,     t3,     t5
    vilvl.d       vr2,    vr3,    vr2
    vavgr.bu      vr8,    vr8,    vr2
.endif
    vstelm.d      vr8,    a0,     0,    0
    add.d         a0,     a0,     a2
    vstelm.d      vr8,    a0,     0,    1
    add.d         a0,     a0,     a2

    alsl.d        t3,     a2,     t3,   2

    fldx.d        f2,     t2,     a3
    fldx.d        f3,     t2,     t0
    QPEL8_V1_LSX  vr4, vr5, vr6, vr0, vr1, vr2, vr3
.ifc \type, avg
    fld.d         f4,     t3,     0
    fldx.d        f5,     t3,     a2
    vilvl.d       vr4,    vr5,    vr4
    vavgr.bu      vr8,    vr8,    vr4
.endif
    vstelm.d      vr8,    a0,     0,    0
    add.d         a0,     a0,     a2
    vstelm.d      vr8,    a0,     0,    1
    add.d         a0,     a0,     a2

    fldx.d        f4,     t2,     t1
    alsl.d        t2,     a3,     t2,   2 // t2 = t2 + 4 * stride
    fld.d         f5,     t2,     0
    QPEL8_V1_LSX  vr6, vr0, vr1, vr2, vr3, vr4, vr5
.ifc \type, avg
    fldx.d        f6,     t3,     t4
    fldx.d        f0,     t3,     t5
    vilvl.d       vr6,    vr0,    vr6
    vavgr.bu      vr8,    vr8,    vr6
.endif
    vstelm.d      vr8,    a0,     0,    0
    add.d         a0,     a0,     a2
    vstelm.d      vr8,    a0,     0,    1
endfunc
.endm

h264_qpel8_v_lowpass_lsx put
h264_qpel8_v_lowpass_lsx avg

function avg_pixels16_l2_8_lsx
    slli.d        t0,     a4,     1
    add.d         t1,     t0,     a4
    slli.d        t2,     t0,     1
    slli.d        t3,     a3,     1
    add.d         t4,     t3,     a3
    slli.d        t5,     t3,     1
    addi.d        t6,     a0,     0
.rept 4
    vld           vr0,    a1,     0
    vldx          vr1,    a1,     a4
    vldx          vr2,    a1,     t0
    vldx          vr3,    a1,     t1
    add.d         a1,     a1,     t2
    vld           vr8,    a2,     0x00
    vld           vr9,    a2,     0x10
    vld           vr10,   a2,     0x20
    vld           vr11,   a2,     0x30
    addi.d        a2,     a2,     0x40
    vavgr.bu      vr0,    vr8,    vr0
    vavgr.bu      vr1,    vr9,    vr1
    vavgr.bu      vr2,    vr10,   vr2
    vavgr.bu      vr3,    vr11,   vr3
    vld           vr8,    t6,     0
    vldx          vr9,    t6,     a3
    vldx          vr10,   t6,     t3
    vldx          vr11,   t6,     t4
    add.d         t6,     t6,     t5
    vavgr.bu      vr0,    vr8,    vr0
    vavgr.bu      vr1,    vr9,    vr1
    vavgr.bu      vr2,    vr10,   vr2
    vavgr.bu      vr3,    vr11,   vr3
    vst           vr0,    a0,     0
    vstx          vr1,    a0,     a3
    vstx          vr2,    a0,     t3
    vstx          vr3,    a0,     t4
    add.d         a0,     a0,     t5
.endr
endfunc

function avg_h264_qpel8_hv_lowpass_lsx
    slli.d        t1,     a3,     1
    add.d         t2,     t1,     a3
    slli.d        t5,     a2,     1
    add.d         t6,     a2,     t5
    addi.d        sp,     sp,     -8
    fst.d         f24,    sp,     0
    vldi          vr20,   0x414   // h_20
    vldi          vr21,   0x405   // h_5
    vldi          vr22,   0x814   // w_20
    vldi          vr23,   0x805   // w_5
    addi.d        t4,     zero,   512
    vreplgr2vr.w  vr24,   t4      // w_512
    addi.d        t0,     a1,     -2   // t0 = src - 2
    sub.d         t0,     t0,     t1   // t0 = t0 - 2 * stride
    addi.d        t3,     a0,     0    // t3 = dst
    h264_qpel8_hv_lowpass_core_lsx t0, a0, avg
    fld.d         f24,    sp,     0
    addi.d        sp,     sp,     8
endfunc

function put_pixels8_l2_8_lsx
    slli.d        t0,     a4,     1
    add.d         t1,     t0,     a4
    slli.d        t2,     t0,     1
.rept 2
    vld           vr0,    a1,     0
    vldx          vr1,    a1,     a4
    vldx          vr2,    a1,     t0
    vldx          vr3,    a1,     t1
    add.d         a1,     a1,     t2
    vilvl.d       vr0,    vr1,    vr0
    vilvl.d       vr2,    vr3,    vr2
    vld           vr8,    a2,     0x00
    vld           vr9,    a2,     0x08
    vld           vr10,   a2,     0x10
    vld           vr11,   a2,     0x18
    vilvl.d       vr8,    vr9,    vr8
    vilvl.d       vr10,   vr11,   vr10
    addi.d        a2,     a2,     32
    vavgr.bu      vr0,    vr8,    vr0
    vavgr.bu      vr2,    vr10,   vr2
    vstelm.d      vr0,    a0,     0,     0
    add.d         a0,     a0,     a3
    vstelm.d      vr0,    a0,     0,     1
    add.d         a0,     a0,     a3
    vstelm.d      vr2,    a0,     0,     0
    add.d         a0,     a0,     a3
    vstelm.d      vr2,    a0,     0,     1
    add.d         a0,     a0,     a3
.endr
endfunc

function ff_put_h264_qpel8_mc00_lsx
    slli.d        t0,     a2,     1
    add.d         t1,     t0,     a2
    slli.d        t2,     t0,     1
    ld.d          t3,     a1,     0x0
    ldx.d         t4,     a1,     a2
    ldx.d         t5,     a1,     t0
    ldx.d         t6,     a1,     t1
    st.d          t3,     a0,     0x0
    stx.d         t4,     a0,     a2
    stx.d         t5,     a0,     t0
    stx.d         t6,     a0,     t1
    add.d         a1,     a1,     t2
    add.d         a0,     a0,     t2
    ld.d          t3,     a1,     0x0
    ldx.d         t4,     a1,     a2
    ldx.d         t5,     a1,     t0
    ldx.d         t6,     a1,     t1
    st.d          t3,     a0,     0x0
    stx.d         t4,     a0,     a2
    stx.d         t5,     a0,     t0
    stx.d         t6,     a0,     t1
endfunc

function ff_avg_h264_qpel8_mc00_lsx
    slli.d        t0,     a2,     1
    add.d         t1,     t0,     a2
    slli.d        t2,     t0,     1
    addi.d        t3,     a0,     0
.rept 2
    vld           vr0,    a1,     0
    vldx          vr1,    a1,     a2
    vldx          vr2,    a1,     t0
    vldx          vr3,    a1,     t1
    add.d         a1,     a1,     t2
    vilvl.d       vr0,    vr1,    vr0
    vilvl.d       vr2,    vr3,    vr2
    vld           vr8,    t3,     0
    vldx          vr9,    t3,     a2
    vldx          vr10,   t3,     t0
    vldx          vr11,   t3,     t1
    add.d         t3,     t3,     t2
    vilvl.d       vr8,    vr9,    vr8
    vilvl.d       vr10,   vr11,   vr10
    vavgr.bu      vr0,    vr8,    vr0
    vavgr.bu      vr2,    vr10,   vr2
    vstelm.d      vr0,    a0,     0,     0
    add.d         a0,     a0,     a2
    vstelm.d      vr0,    a0,     0,     1
    add.d         a0,     a0,     a2
    vstelm.d      vr2,    a0,     0,     0
    add.d         a0,     a0,     a2
    vstelm.d      vr2,    a0,     0,     1
    add.d         a0,     a0,     a2
.endr
endfunc

function avg_pixels8_l2_8_lsx
    slli.d        t0,     a4,     1
    add.d         t1,     t0,     a4
    slli.d        t2,     t0,     1
    addi.d        t3,     a0,     0
    slli.d        t4,     a3,     1
    add.d         t5,     t4,     a3
    slli.d        t6,     t4,     1
.rept 2
    vld           vr0,    a1,     0
    vldx          vr1,    a1,     a4
    vldx          vr2,    a1,     t0
    vldx          vr3,    a1,     t1
    add.d         a1,     a1,     t2
    vilvl.d       vr0,    vr1,    vr0
    vilvl.d       vr2,    vr3,    vr2
    vld           vr8,    a2,     0x00
    vld           vr9,    a2,     0x08
    vld           vr10,   a2,     0x10
    vld           vr11,   a2,     0x18
    addi.d        a2,     a2,     0x20
    vilvl.d       vr8,    vr9,    vr8
    vilvl.d       vr10,   vr11,   vr10
    vavgr.bu      vr0,    vr8,    vr0
    vavgr.bu      vr2,    vr10,   vr2
    vld           vr8,    t3,     0
    vldx          vr9,    t3,     a3
    vldx          vr10,   t3,     t4
    vldx          vr11,   t3,     t5
    add.d         t3,     t3,     t6
    vilvl.d       vr8,    vr9,    vr8
    vilvl.d       vr10,   vr11,   vr10
    vavgr.bu      vr0,    vr8,    vr0
    vavgr.bu      vr2,    vr10,   vr2
    vstelm.d      vr0,    a0,     0,     0
    add.d         a0,     a0,     a3
    vstelm.d      vr0,    a0,     0,     1
    add.d         a0,     a0,     a3
    vstelm.d      vr2,    a0,     0,     0
    add.d         a0,     a0,     a3
    vstelm.d      vr2,    a0,     0,     1
    add.d         a0,     a0,     a3
.endr
endfunc

function avg_h264_qpel8_h_lowpass_lsx
    slli.d        t1,     a3,     1
    add.d         t2,     t1,     a3
    slli.d        t5,     a2,     1
    add.d         t6,     t5,     a2
    vldi          vr20,   0x414
    vldi          vr21,   0x405
    vldi          vr22,   0x410
    addi.d        t0,     a1,     -2   // t0 = src - 2
    add.d         t3,     a1,     zero // t3 = src
    addi.d        t4,     a0,     0    // t4 = dst
.rept 4
    vld           vr0,    t0,     0
    vldx          vr1,    t0,     a3
    QPEL8_H_LSX   vr12, vr13
    vssrani.bu.h  vr13,   vr12,   5
    fld.d         f0,     t4,     0
    fldx.d        f1,     t4,     a2
    vilvl.d       vr0,    vr1,    vr0
    vavgr.bu      vr13,   vr13,   vr0
    vstelm.d      vr13,   a0,     0,    0
    add.d         a0,     a0,     a2
    vstelm.d      vr13,   a0,     0,    1
    add.d         a0,     a0,     a2
    add.d         t0,     t0,     t1
    add.d         t4,     t4,     t1
.endr
endfunc
