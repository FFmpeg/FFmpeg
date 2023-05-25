/*
 * Loongson LASX optimized h264idct
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin <yinshiyou-hf@loongson.cn>
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

/*
 * #define FUNC2(a, b, c)  FUNC3(a, b, c)
 * #define FUNCC(a) FUNC2(a, BIT_DEPTH, _c)
 * void FUNCC(ff_h264_idct_add)(uint8_t *_dst, int16_t *_block, int stride)
 * LSX optimization is enough for this function.
 */
function ff_h264_idct_add_8_lsx
    fld.d         f0,     a1,    0
    fld.d         f1,     a1,    8
    fld.d         f2,     a1,    16
    fld.d         f3,     a1,    24
    vxor.v        vr7,    vr7,   vr7
    add.d         t2,     a2,    a2
    add.d         t3,     t2,    a2
    vst           vr7,    a1,    0
    vst           vr7,    a1,    16

    vadd.h        vr4,    vr0,   vr2
    vsub.h        vr5,    vr0,   vr2
    vsrai.h       vr6,    vr1,   1
    vsrai.h       vr7,    vr3,   1
    vsub.h        vr6,    vr6,   vr3
    vadd.h        vr7,    vr1,   vr7
    LSX_BUTTERFLY_4_H vr4, vr5, vr6, vr7,  vr0, vr1, vr2, vr3
    LSX_TRANSPOSE4x4_H vr0, vr1, vr2, vr3,  vr0, vr1, vr2, vr3,  vr4, vr5
    vadd.h        vr4,    vr0,   vr2
    vsub.h        vr5,    vr0,   vr2
    vsrai.h       vr6,    vr1,   1
    vsrai.h       vr7,    vr3,   1
    vsub.h        vr6,    vr6,   vr3
    vadd.h        vr7,    vr1,   vr7
    LSX_BUTTERFLY_4_H vr4, vr5, vr6, vr7,  vr0, vr1, vr2, vr3

    fld.s         f4,     a0,    0
    fldx.s        f5,     a0,    a2
    fldx.s        f6,     a0,    t2
    fldx.s        f7,     a0,    t3

    vsrari.h      vr0,    vr0,   6
    vsrari.h      vr1,    vr1,   6
    vsrari.h      vr2,    vr2,   6
    vsrari.h      vr3,    vr3,   6

    vsllwil.hu.bu vr4,    vr4,   0
    vsllwil.hu.bu vr5,    vr5,   0
    vsllwil.hu.bu vr6,    vr6,   0
    vsllwil.hu.bu vr7,    vr7,   0
    vadd.h        vr0,    vr0,   vr4
    vadd.h        vr1,    vr1,   vr5
    vadd.h        vr2,    vr2,   vr6
    vadd.h        vr3,    vr3,   vr7
    vssrarni.bu.h vr1,    vr0,   0
    vssrarni.bu.h vr3,    vr2,   0

    vbsrl.v       vr0,    vr1,   8
    vbsrl.v       vr2,    vr3,   8
    fst.s         f1,     a0,    0
    fstx.s        f0,     a0,    a2
    fstx.s        f3,     a0,    t2
    fstx.s        f2,     a0,    t3
endfunc

/*
 * #define FUNC2(a, b, c)  FUNC3(a, b, c)
 * #define FUNCC(a) FUNC2(a, BIT_DEPTH, _c)
 * void FUNCC(ff_h264_idct8_add)(uint8_t *_dst, int16_t *_block, int stride)
 */
function ff_h264_idct8_add_8_lsx
    ld.h          t0,     a1,    0
    add.d         t2,     a2,    a2
    add.d         t3,     t2,    a2
    add.d         t4,     t3,    a2
    add.d         t5,     t4,    a2
    add.d         t6,     t5,    a2
    add.d         t7,     t6,    a2
    addi.w        t0,     t0,    32
    st.h          t0,     a1,    0

    vld           vr0,    a1,    0
    vld           vr1,    a1,    16
    vld           vr2,    a1,    32
    vld           vr3,    a1,    48
    vld           vr4,    a1,    64
    vld           vr5,    a1,    80
    vld           vr6,    a1,    96
    vld           vr7,    a1,    112
    vxor.v        vr8,    vr8,   vr8
    vst           vr8,    a1,    0
    vst           vr8,    a1,    16
    vst           vr8,    a1,    32
    vst           vr8,    a1,    48
    vst           vr8,    a1,    64
    vst           vr8,    a1,    80
    vst           vr8,    a1,    96
    vst           vr8,    a1,    112

    vadd.h        vr18,   vr0,   vr4
    vsub.h        vr19,   vr0,   vr4
    vsrai.h       vr20,   vr2,   1
    vsrai.h       vr21,   vr6,   1
    vsub.h        vr20,   vr20,  vr6
    vadd.h        vr21,   vr21,  vr2
    LSX_BUTTERFLY_4_H vr18, vr19, vr20, vr21,  vr10, vr12, vr14, vr16
    vsrai.h       vr11,   vr7,   1
    vsrai.h       vr13,   vr3,   1
    vsrai.h       vr15,   vr5,   1
    vsrai.h       vr17,   vr1,   1
    vsub.h        vr11,   vr5,   vr11
    vsub.h        vr13,   vr7,   vr13
    vadd.h        vr15,   vr7,   vr15
    vadd.h        vr17,   vr5,   vr17
    vsub.h        vr11,   vr11,  vr7
    vsub.h        vr13,   vr13,  vr3
    vadd.h        vr15,   vr15,  vr5
    vadd.h        vr17,   vr17,  vr1
    vsub.h        vr11,   vr11,  vr3
    vadd.h        vr13,   vr13,  vr1
    vsub.h        vr15,   vr15,  vr1
    vadd.h        vr17,   vr17,  vr3
    vsrai.h       vr18,   vr11,  2
    vsrai.h       vr19,   vr13,  2
    vsrai.h       vr20,   vr15,  2
    vsrai.h       vr21,   vr17,  2
    vadd.h        vr11,   vr11,  vr21
    vadd.h        vr13,   vr13,  vr20
    vsub.h        vr15,   vr19,  vr15
    vsub.h        vr17,   vr17,  vr18
    LSX_BUTTERFLY_8_H vr10, vr16, vr12, vr14, vr13, vr15, vr11, vr17, \
                      vr0,  vr3,  vr1,  vr2,  vr5,  vr6,  vr4,  vr7

    LSX_TRANSPOSE8x8_H vr0,  vr1,  vr2,  vr3,  vr4,  vr5,  vr6,  vr7, \
                       vr0,  vr1,  vr2,  vr3,  vr4,  vr5,  vr6,  vr7, \
                       vr10, vr11, vr12, vr13, vr14, vr15, vr16, vr17
    vexth.w.h     vr20,   vr0
    vexth.w.h     vr21,   vr1
    vexth.w.h     vr22,   vr2
    vexth.w.h     vr23,   vr3
    vexth.w.h     vr8,    vr4
    vexth.w.h     vr9,    vr5
    vexth.w.h     vr18,   vr6
    vexth.w.h     vr19,   vr7
    vsllwil.w.h   vr0,    vr0,   0
    vsllwil.w.h   vr1,    vr1,   0
    vsllwil.w.h   vr2,    vr2,   0
    vsllwil.w.h   vr3,    vr3,   0
    vsllwil.w.h   vr4,    vr4,   0
    vsllwil.w.h   vr5,    vr5,   0
    vsllwil.w.h   vr6,    vr6,   0
    vsllwil.w.h   vr7,    vr7,   0

    vadd.w        vr11,   vr0,   vr4
    vsub.w        vr13,   vr0,   vr4
    vsrai.w       vr15,   vr2,   1
    vsrai.w       vr17,   vr6,   1
    vsub.w        vr15,   vr15,  vr6
    vadd.w        vr17,   vr17,  vr2
    LSX_BUTTERFLY_4_W vr11, vr13, vr15, vr17,  vr10, vr12, vr14, vr16
    vsrai.w       vr11,   vr7,   1
    vsrai.w       vr13,   vr3,   1
    vsrai.w       vr15,   vr5,   1
    vsrai.w       vr17,   vr1,   1
    vsub.w        vr11,   vr5,   vr11
    vsub.w        vr13,   vr7,   vr13
    vadd.w        vr15,   vr7,   vr15
    vadd.w        vr17,   vr5,   vr17
    vsub.w        vr11,   vr11,  vr7
    vsub.w        vr13,   vr13,  vr3
    vadd.w        vr15,   vr15,  vr5
    vadd.w        vr17,   vr17,  vr1
    vsub.w        vr11,   vr11,  vr3
    vadd.w        vr13,   vr13,  vr1
    vsub.w        vr15,   vr15,  vr1
    vadd.w        vr17,   vr17,  vr3
    vsrai.w       vr0,    vr11,  2
    vsrai.w       vr1,    vr13,  2
    vsrai.w       vr2,    vr15,  2
    vsrai.w       vr3,    vr17,  2
    vadd.w        vr11,   vr11,  vr3
    vadd.w        vr13,   vr13,  vr2
    vsub.w        vr15,   vr1,   vr15
    vsub.w        vr17,   vr17,  vr0
    LSX_BUTTERFLY_8_W vr10, vr12, vr14, vr16, vr11, vr13, vr15, vr17, \
                      vr0,  vr1,  vr2,  vr3,  vr4,  vr5,  vr6,  vr7

    vadd.w        vr11,    vr20,  vr8
    vsub.w        vr13,    vr20,  vr8
    vsrai.w       vr15,    vr22,  1
    vsrai.w       vr17,    vr18,  1
    vsub.w        vr15,    vr15,  vr18
    vadd.w        vr17,    vr17,  vr22
    LSX_BUTTERFLY_4_W vr11, vr13, vr15, vr17,  vr10, vr12, vr14, vr16
    vsrai.w       vr11,   vr19,  1
    vsrai.w       vr13,   vr23,  1
    vsrai.w       vr15,   vr9,   1
    vsrai.w       vr17,   vr21,  1
    vsub.w        vr11,   vr9,   vr11
    vsub.w        vr13,   vr19,  vr13
    vadd.w        vr15,   vr19,  vr15
    vadd.w        vr17,   vr9,   vr17
    vsub.w        vr11,   vr11,  vr19
    vsub.w        vr13,   vr13,  vr23
    vadd.w        vr15,   vr15,  vr9
    vadd.w        vr17,   vr17,  vr21
    vsub.w        vr11,   vr11,  vr23
    vadd.w        vr13,   vr13,  vr21
    vsub.w        vr15,   vr15,  vr21
    vadd.w        vr17,   vr17,  vr23
    vsrai.w       vr20,   vr11,  2
    vsrai.w       vr21,   vr13,  2
    vsrai.w       vr22,   vr15,  2
    vsrai.w       vr23,   vr17,  2
    vadd.w        vr11,   vr11,  vr23
    vadd.w        vr13,   vr13,  vr22
    vsub.w        vr15,   vr21,  vr15
    vsub.w        vr17,   vr17,  vr20
    LSX_BUTTERFLY_8_W vr10, vr12, vr14, vr16, vr11, vr13, vr15, vr17, \
                      vr20, vr21, vr22, vr23, vr8, vr9, vr18, vr19

    vld           vr10,   a0,    0
    vldx          vr11,   a0,    a2
    vldx          vr12,   a0,    t2
    vldx          vr13,   a0,    t3
    vldx          vr14,   a0,    t4
    vldx          vr15,   a0,    t5
    vldx          vr16,   a0,    t6
    vldx          vr17,   a0,    t7
    vsrani.h.w    vr20,   vr0,   6
    vsrani.h.w    vr21,   vr1,   6
    vsrani.h.w    vr22,   vr2,   6
    vsrani.h.w    vr23,   vr3,   6
    vsrani.h.w    vr8,    vr4,   6
    vsrani.h.w    vr9,    vr5,   6
    vsrani.h.w    vr18,   vr6,   6
    vsrani.h.w    vr19,   vr7,   6
    vsllwil.hu.bu vr10,   vr10,  0
    vsllwil.hu.bu vr11,   vr11,  0
    vsllwil.hu.bu vr12,   vr12,  0
    vsllwil.hu.bu vr13,   vr13,  0
    vsllwil.hu.bu vr14,   vr14,  0
    vsllwil.hu.bu vr15,   vr15,  0
    vsllwil.hu.bu vr16,   vr16,  0
    vsllwil.hu.bu vr17,   vr17,  0

    vadd.h        vr0,    vr20,  vr10
    vadd.h        vr1,    vr21,  vr11
    vadd.h        vr2,    vr22,  vr12
    vadd.h        vr3,    vr23,  vr13
    vadd.h        vr4,    vr8,   vr14
    vadd.h        vr5,    vr9,   vr15
    vadd.h        vr6,    vr18,  vr16
    vadd.h        vr7,    vr19,  vr17
    vssrarni.bu.h vr1,    vr0,   0
    vssrarni.bu.h vr3,    vr2,   0
    vssrarni.bu.h vr5,    vr4,   0
    vssrarni.bu.h vr7,    vr6,   0
    vbsrl.v       vr0,    vr1,   8
    vbsrl.v       vr2,    vr3,   8
    vbsrl.v       vr4,    vr5,   8
    vbsrl.v       vr6,    vr7,   8
    fst.d         f1,     a0,    0
    fstx.d        f0,     a0,    a2
    fstx.d        f3,     a0,    t2
    fstx.d        f2,     a0,    t3
    fstx.d        f5,     a0,    t4
    fstx.d        f4,     a0,    t5
    fstx.d        f7,     a0,    t6
    fstx.d        f6,     a0,    t7
endfunc

/*
 * #define FUNC2(a, b, c)  FUNC3(a, b, c)
 * #define FUNCC(a) FUNC2(a, BIT_DEPTH, _c)
 * void FUNCC(ff_h264_idct8_add)(uint8_t *_dst, int16_t *_block, int stride)
 */
function ff_h264_idct8_add_8_lasx
    ld.h          t0,     a1,    0
    add.d         t2,     a2,    a2
    add.d         t3,     t2,    a2
    add.d         t4,     t3,    a2
    add.d         t5,     t4,    a2
    add.d         t6,     t5,    a2
    add.d         t7,     t6,    a2
    addi.w        t0,     t0,    32
    st.h          t0,     a1,    0

    vld           vr0,    a1,    0
    vld           vr1,    a1,    16
    vld           vr2,    a1,    32
    vld           vr3,    a1,    48
    vld           vr4,    a1,    64
    vld           vr5,    a1,    80
    vld           vr6,    a1,    96
    vld           vr7,    a1,    112
    xvxor.v       xr8,    xr8,   xr8
    xvst          xr8,    a1,    0
    xvst          xr8,    a1,    32
    xvst          xr8,    a1,    64
    xvst          xr8,    a1,    96

    vadd.h        vr18,   vr0,   vr4
    vsub.h        vr19,   vr0,   vr4
    vsrai.h       vr20,   vr2,   1
    vsrai.h       vr21,   vr6,   1
    vsub.h        vr20,   vr20,  vr6
    vadd.h        vr21,   vr21,  vr2
    LSX_BUTTERFLY_4_H vr18, vr19, vr20, vr21,  vr10, vr12, vr14, vr16
    vsrai.h       vr11,   vr7,   1
    vsrai.h       vr13,   vr3,   1
    vsrai.h       vr15,   vr5,   1
    vsrai.h       vr17,   vr1,   1
    vsub.h        vr11,   vr5,   vr11
    vsub.h        vr13,   vr7,   vr13
    vadd.h        vr15,   vr7,   vr15
    vadd.h        vr17,   vr5,   vr17
    vsub.h        vr11,   vr11,  vr7
    vsub.h        vr13,   vr13,  vr3
    vadd.h        vr15,   vr15,  vr5
    vadd.h        vr17,   vr17,  vr1
    vsub.h        vr11,   vr11,  vr3
    vadd.h        vr13,   vr13,  vr1
    vsub.h        vr15,   vr15,  vr1
    vadd.h        vr17,   vr17,  vr3
    vsrai.h       vr18,   vr11,  2
    vsrai.h       vr19,   vr13,  2
    vsrai.h       vr20,   vr15,  2
    vsrai.h       vr21,   vr17,  2
    vadd.h        vr11,   vr11,  vr21
    vadd.h        vr13,   vr13,  vr20
    vsub.h        vr15,   vr19,  vr15
    vsub.h        vr17,   vr17,  vr18
    LSX_BUTTERFLY_8_H vr10, vr16, vr12, vr14, vr13, vr15, vr11, vr17, \
                      vr0,  vr3,  vr1,  vr2,  vr5,  vr6,  vr4,  vr7

    LSX_TRANSPOSE8x8_H vr0,  vr1,  vr2,  vr3,  vr4,  vr5,  vr6,  vr7, \
                       vr0,  vr1,  vr2,  vr3,  vr4,  vr5,  vr6,  vr7, \
                       vr10, vr11, vr12, vr13, vr14, vr15, vr16, vr17
    vext2xv.w.h   xr0,    xr0
    vext2xv.w.h   xr1,    xr1
    vext2xv.w.h   xr2,    xr2
    vext2xv.w.h   xr3,    xr3
    vext2xv.w.h   xr4,    xr4
    vext2xv.w.h   xr5,    xr5
    vext2xv.w.h   xr6,    xr6
    vext2xv.w.h   xr7,    xr7

    xvadd.w       xr11,   xr0,   xr4
    xvsub.w       xr13,   xr0,   xr4
    xvsrai.w      xr15,   xr2,   1
    xvsrai.w      xr17,   xr6,   1
    xvsub.w       xr15,   xr15,  xr6
    xvadd.w       xr17,   xr17,  xr2
    LASX_BUTTERFLY_4_W xr11, xr13, xr15, xr17,  xr10, xr12, xr14, xr16
    xvsrai.w      xr11,   xr7,   1
    xvsrai.w      xr13,   xr3,   1
    xvsrai.w      xr15,   xr5,   1
    xvsrai.w      xr17,   xr1,   1
    xvsub.w       xr11,   xr5,   xr11
    xvsub.w       xr13,   xr7,   xr13
    xvadd.w       xr15,   xr7,   xr15
    xvadd.w       xr17,   xr5,   xr17
    xvsub.w       xr11,   xr11,  xr7
    xvsub.w       xr13,   xr13,  xr3
    xvadd.w       xr15,   xr15,  xr5
    xvadd.w       xr17,   xr17,  xr1
    xvsub.w       xr11,   xr11,  xr3
    xvadd.w       xr13,   xr13,  xr1
    xvsub.w       xr15,   xr15,  xr1
    xvadd.w       xr17,   xr17,  xr3
    xvsrai.w      xr0,    xr11,  2
    xvsrai.w      xr1,    xr13,  2
    xvsrai.w      xr2,    xr15,  2
    xvsrai.w      xr3,    xr17,  2
    xvadd.w       xr11,   xr11,  xr3
    xvadd.w       xr13,   xr13,  xr2
    xvsub.w       xr15,   xr1,   xr15
    xvsub.w       xr17,   xr17,  xr0
    LASX_BUTTERFLY_8_W xr10, xr12, xr14, xr16, xr11, xr13, xr15, xr17, \
                       xr0,  xr1,  xr2,  xr3,  xr4,  xr5,  xr6,  xr7

    vld           vr10,   a0,    0
    vldx          vr11,   a0,    a2
    vldx          vr12,   a0,    t2
    vldx          vr13,   a0,    t3
    vldx          vr14,   a0,    t4
    vldx          vr15,   a0,    t5
    vldx          vr16,   a0,    t6
    vldx          vr17,   a0,    t7
    xvldi         xr8,    0x806     //"xvldi.w xr8 6"
    xvsran.h.w    xr0,    xr0,   xr8
    xvsran.h.w    xr1,    xr1,   xr8
    xvsran.h.w    xr2,    xr2,   xr8
    xvsran.h.w    xr3,    xr3,   xr8
    xvsran.h.w    xr4,    xr4,   xr8
    xvsran.h.w    xr5,    xr5,   xr8
    xvsran.h.w    xr6,    xr6,   xr8
    xvsran.h.w    xr7,    xr7,   xr8
    xvpermi.d     xr0,    xr0,   0x08
    xvpermi.d     xr1,    xr1,   0x08
    xvpermi.d     xr2,    xr2,   0x08
    xvpermi.d     xr3,    xr3,   0x08
    xvpermi.d     xr4,    xr4,   0x08
    xvpermi.d     xr5,    xr5,   0x08
    xvpermi.d     xr6,    xr6,   0x08
    xvpermi.d     xr7,    xr7,   0x08

    vsllwil.hu.bu vr10,   vr10,  0
    vsllwil.hu.bu vr11,   vr11,  0
    vsllwil.hu.bu vr12,   vr12,  0
    vsllwil.hu.bu vr13,   vr13,  0
    vsllwil.hu.bu vr14,   vr14,  0
    vsllwil.hu.bu vr15,   vr15,  0
    vsllwil.hu.bu vr16,   vr16,  0
    vsllwil.hu.bu vr17,   vr17,  0

    vadd.h        vr0,    vr0,   vr10
    vadd.h        vr1,    vr1,   vr11
    vadd.h        vr2,    vr2,   vr12
    vadd.h        vr3,    vr3,   vr13
    vadd.h        vr4,    vr4,   vr14
    vadd.h        vr5,    vr5,   vr15
    vadd.h        vr6,    vr6,   vr16
    vadd.h        vr7,    vr7,   vr17
    vssrarni.bu.h vr1,    vr0,   0
    vssrarni.bu.h vr3,    vr2,   0
    vssrarni.bu.h vr5,    vr4,   0
    vssrarni.bu.h vr7,    vr6,   0
    vbsrl.v       vr0,    vr1,   8
    vbsrl.v       vr2,    vr3,   8
    vbsrl.v       vr4,    vr5,   8
    vbsrl.v       vr6,    vr7,   8
    fst.d         f1,     a0,    0
    fstx.d        f0,     a0,    a2
    fstx.d        f3,     a0,    t2
    fstx.d        f2,     a0,    t3
    fstx.d        f5,     a0,    t4
    fstx.d        f4,     a0,    t5
    fstx.d        f7,     a0,    t6
    fstx.d        f6,     a0,    t7
endfunc

/*
 * #define FUNC2(a, b, c)  FUNC3(a, b, c)
 * #define FUNCC(a) FUNC2(a, BIT_DEPTH, _c)
 * void FUNCC(ff_h264_idct_dc_add)(uint8_t *_dst, int16_t *_block, int stride)
 * LSX optimization is enough for this function.
 */
function ff_h264_idct_dc_add_8_lsx
    vldrepl.h     vr4,    a1,    0
    add.d         t2,     a2,    a2
    add.d         t3,     t2,    a2
    fld.s         f0,     a0,    0
    fldx.s        f1,     a0,    a2
    fldx.s        f2,     a0,    t2
    fldx.s        f3,     a0,    t3
    st.h          zero,   a1,    0

    vsrari.h      vr4,    vr4,   6
    vilvl.w       vr0,    vr1,   vr0
    vilvl.w       vr1,    vr3,   vr2
    vsllwil.hu.bu vr0,    vr0,   0
    vsllwil.hu.bu vr1,    vr1,   0
    vadd.h        vr0,    vr0,   vr4
    vadd.h        vr1,    vr1,   vr4
    vssrarni.bu.h vr1,    vr0,   0

    vbsrl.v       vr2,    vr1,   4
    vbsrl.v       vr3,    vr1,   8
    vbsrl.v       vr4,    vr1,   12
    fst.s         f1,     a0,    0
    fstx.s        f2,     a0,    a2
    fstx.s        f3,     a0,    t2
    fstx.s        f4,     a0,    t3
endfunc

/*
 * #define FUNC2(a, b, c)  FUNC3(a, b, c)
 * #define FUNCC(a) FUNC2(a, BIT_DEPTH, _c)
 * void FUNCC(ff_h264_idct8_dc_add)(uint8_t *_dst, int16_t *_block, int stride)
 */
function ff_h264_idct8_dc_add_8_lsx
    vldrepl.h     vr8,    a1,    0
    add.d         t2,     a2,    a2
    add.d         t3,     t2,    a2
    add.d         t4,     t3,    a2
    add.d         t5,     t4,    a2
    add.d         t6,     t5,    a2
    add.d         t7,     t6,    a2

    fld.d         f0,     a0,    0
    fldx.d        f1,     a0,    a2
    fldx.d        f2,     a0,    t2
    fldx.d        f3,     a0,    t3
    fldx.d        f4,     a0,    t4
    fldx.d        f5,     a0,    t5
    fldx.d        f6,     a0,    t6
    fldx.d        f7,     a0,    t7
    st.h          zero,   a1,    0

    vsrari.h      vr8,    vr8,   6
    vsllwil.hu.bu vr0,    vr0,   0
    vsllwil.hu.bu vr1,    vr1,   0
    vsllwil.hu.bu vr2,    vr2,   0
    vsllwil.hu.bu vr3,    vr3,   0
    vsllwil.hu.bu vr4,    vr4,   0
    vsllwil.hu.bu vr5,    vr5,   0
    vsllwil.hu.bu vr6,    vr6,   0
    vsllwil.hu.bu vr7,    vr7,   0
    vadd.h        vr0,    vr0,   vr8
    vadd.h        vr1,    vr1,   vr8
    vadd.h        vr2,    vr2,   vr8
    vadd.h        vr3,    vr3,   vr8
    vadd.h        vr4,    vr4,   vr8
    vadd.h        vr5,    vr5,   vr8
    vadd.h        vr6,    vr6,   vr8
    vadd.h        vr7,    vr7,   vr8
    vssrarni.bu.h vr1,    vr0,   0
    vssrarni.bu.h vr3,    vr2,   0
    vssrarni.bu.h vr5,    vr4,   0
    vssrarni.bu.h vr7,    vr6,   0

    vbsrl.v       vr0,    vr1,   8
    vbsrl.v       vr2,    vr3,   8
    vbsrl.v       vr4,    vr5,   8
    vbsrl.v       vr6,    vr7,   8
    fst.d         f1,     a0,    0
    fstx.d        f0,     a0,    a2
    fstx.d        f3,     a0,    t2
    fstx.d        f2,     a0,    t3
    fstx.d        f5,     a0,    t4
    fstx.d        f4,     a0,    t5
    fstx.d        f7,     a0,    t6
    fstx.d        f6,     a0,    t7
endfunc
function ff_h264_idct8_dc_add_8_lasx
    xvldrepl.h    xr8,    a1,    0
    add.d         t2,     a2,    a2
    add.d         t3,     t2,    a2
    add.d         t4,     t3,    a2
    add.d         t5,     t4,    a2
    add.d         t6,     t5,    a2
    add.d         t7,     t6,    a2

    fld.d         f0,     a0,    0
    fldx.d        f1,     a0,    a2
    fldx.d        f2,     a0,    t2
    fldx.d        f3,     a0,    t3
    fldx.d        f4,     a0,    t4
    fldx.d        f5,     a0,    t5
    fldx.d        f6,     a0,    t6
    fldx.d        f7,     a0,    t7
    st.h          zero,   a1,    0

    xvsrari.h     xr8,    xr8,   6
    xvpermi.q     xr1,    xr0,   0x20
    xvpermi.q     xr3,    xr2,   0x20
    xvpermi.q     xr5,    xr4,   0x20
    xvpermi.q     xr7,    xr6,   0x20
    xvsllwil.hu.bu xr1,   xr1,   0
    xvsllwil.hu.bu xr3,   xr3,   0
    xvsllwil.hu.bu xr5,   xr5,   0
    xvsllwil.hu.bu xr7,   xr7,   0
    xvadd.h       xr1,    xr1,   xr8
    xvadd.h       xr3,    xr3,   xr8
    xvadd.h       xr5,    xr5,   xr8
    xvadd.h       xr7,    xr7,   xr8

    xvssrarni.bu.h xr3,   xr1,   0
    xvssrarni.bu.h xr7,   xr5,   0

    xvpermi.q     xr1,    xr3,   0x11
    xvpermi.q     xr5,    xr7,   0x11
    xvbsrl.v      xr0,    xr1,   8
    xvbsrl.v      xr2,    xr3,   8
    xvbsrl.v      xr4,    xr5,   8
    xvbsrl.v      xr6,    xr7,   8

    fst.d         f3,     a0,    0
    fstx.d        f1,     a0,    a2
    fstx.d        f2,     a0,    t2
    fstx.d        f0,     a0,    t3
    fstx.d        f7,     a0,    t4
    fstx.d        f5,     a0,    t5
    fstx.d        f6,     a0,    t6
    fstx.d        f4,     a0,    t7
endfunc

/**
 * IDCT transforms the 16 dc values and dequantizes them.
 * @param qmul quantization parameter
 * void FUNCC(ff_h264_luma_dc_dequant_idct)(int16_t *_output, int16_t *_input, int qmul){
 * LSX optimization is enough for this function.
 */
function ff_h264_luma_dc_dequant_idct_8_lsx
    vld           vr0,    a1,    0
    vld           vr1,    a1,    8
    vld           vr2,    a1,    16
    vld           vr3,    a1,    24
    vreplgr2vr.w  vr8,    a2
    LSX_TRANSPOSE4x4_H vr0, vr1, vr2, vr3, vr4, vr5, vr6, vr7, vr9, vr10
    LSX_BUTTERFLY_4_H  vr4, vr6, vr7, vr5, vr0, vr3, vr2, vr1
    LSX_BUTTERFLY_4_H  vr0, vr1, vr2, vr3, vr4, vr7, vr6, vr5
    LSX_TRANSPOSE4x4_H vr4, vr5, vr6, vr7, vr0, vr1, vr2, vr3, vr9, vr10
    LSX_BUTTERFLY_4_H  vr0, vr1, vr3, vr2, vr4, vr7, vr6, vr5
    LSX_BUTTERFLY_4_H  vr4, vr5, vr6, vr7, vr0, vr1, vr2, vr3
    vsllwil.w.h   vr0,    vr0,   0
    vsllwil.w.h   vr1,    vr1,   0
    vsllwil.w.h   vr2,    vr2,   0
    vsllwil.w.h   vr3,    vr3,   0
    vmul.w        vr0,    vr0,   vr8
    vmul.w        vr1,    vr1,   vr8
    vmul.w        vr2,    vr2,   vr8
    vmul.w        vr3,    vr3,   vr8
    vsrarni.h.w   vr1,    vr0,   8
    vsrarni.h.w   vr3,    vr2,   8

    vstelm.h      vr1,    a0,    0,   0
    vstelm.h      vr1,    a0,    32,  4
    vstelm.h      vr1,    a0,    64,  1
    vstelm.h      vr1,    a0,    96,  5
    vstelm.h      vr3,    a0,    128, 0
    vstelm.h      vr3,    a0,    160, 4
    vstelm.h      vr3,    a0,    192, 1
    vstelm.h      vr3,    a0,    224, 5
    addi.d        a0,     a0,    256
    vstelm.h      vr1,    a0,    0,   2
    vstelm.h      vr1,    a0,    32,  6
    vstelm.h      vr1,    a0,    64,  3
    vstelm.h      vr1,    a0,    96,  7
    vstelm.h      vr3,    a0,    128, 2
    vstelm.h      vr3,    a0,    160, 6
    vstelm.h      vr3,    a0,    192, 3
    vstelm.h      vr3,    a0,    224, 7
endfunc
