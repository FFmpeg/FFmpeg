/*
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 * Contributed by jinbo <jinbo@loongson.cn>
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

.extern ff_hevc_qpel_filters
.extern ff_hevc_epel_filters

.macro LOAD_VAR bit
    addi.w         t1,     a5,      6  //shift
    addi.w         t3,     zero,    1  //one
    sub.w          t4,     t1,      t3
    sll.w          t3,     t3,      t4 //offset
.if \bit == 128
    vreplgr2vr.w   vr1,    a6          //wx
    vreplgr2vr.w   vr2,    t3          //offset
    vreplgr2vr.w   vr3,    t1          //shift
    vreplgr2vr.w   vr4,    a7          //ox
.else
    xvreplgr2vr.w  xr1,    a6
    xvreplgr2vr.w  xr2,    t3
    xvreplgr2vr.w  xr3,    t1
    xvreplgr2vr.w  xr4,    a7
.endif
.endm

.macro HEVC_PEL_UNI_W_PIXELS8_LSX src0, dst0, w
    vldrepl.d      vr0,    \src0,   0
    vsllwil.hu.bu  vr0,    vr0,     0
    vexth.wu.hu    vr5,    vr0
    vsllwil.wu.hu  vr0,    vr0,     0
    vslli.w        vr0,    vr0,     6
    vslli.w        vr5,    vr5,     6
    vmul.w         vr0,    vr0,     vr1
    vmul.w         vr5,    vr5,     vr1
    vadd.w         vr0,    vr0,     vr2
    vadd.w         vr5,    vr5,     vr2
    vsra.w         vr0,    vr0,     vr3
    vsra.w         vr5,    vr5,     vr3
    vadd.w         vr0,    vr0,     vr4
    vadd.w         vr5,    vr5,     vr4
    vssrani.h.w    vr5,    vr0,     0
    vssrani.bu.h   vr5,    vr5,     0
.if \w == 6
    fst.s          f5,     \dst0,   0
    vstelm.h       vr5,    \dst0,   4,     2
.else
    fst.d          f5,     \dst0,   0
.endif
.endm

.macro HEVC_PEL_UNI_W_PIXELS8x2_LASX src0, dst0, w
    vldrepl.d      vr0,    \src0,   0
    add.d          t2,     \src0,   a3
    vldrepl.d      vr5,    t2,      0
    xvpermi.q      xr0,    xr5,     0x02
    xvsllwil.hu.bu xr0,    xr0,     0
    xvexth.wu.hu   xr5,    xr0
    xvsllwil.wu.hu xr0,    xr0,     0
    xvslli.w       xr0,    xr0,     6
    xvslli.w       xr5,    xr5,     6
    xvmul.w        xr0,    xr0,     xr1
    xvmul.w        xr5,    xr5,     xr1
    xvadd.w        xr0,    xr0,     xr2
    xvadd.w        xr5,    xr5,     xr2
    xvsra.w        xr0,    xr0,     xr3
    xvsra.w        xr5,    xr5,     xr3
    xvadd.w        xr0,    xr0,     xr4
    xvadd.w        xr5,    xr5,     xr4
    xvssrani.h.w   xr5,    xr0,     0
    xvpermi.q      xr0,    xr5,     0x01
    xvssrani.bu.h  xr0,    xr5,     0
    add.d          t3,     \dst0,   a1
.if \w == 6
    vstelm.w       vr0,    \dst0,   0,     0
    vstelm.h       vr0,    \dst0,   4,     2
    vstelm.w       vr0,    t3,      0,     2
    vstelm.h       vr0,    t3,      4,     6
.else
    vstelm.d       vr0,    \dst0,   0,     0
    vstelm.d       vr0,    t3,      0,     1
.endif
.endm

.macro HEVC_PEL_UNI_W_PIXELS16_LSX src0, dst0
    vld            vr0,    \src0,   0
    vexth.hu.bu    vr7,    vr0
    vexth.wu.hu    vr8,    vr7
    vsllwil.wu.hu  vr7,    vr7,     0
    vsllwil.hu.bu  vr5,    vr0,     0
    vexth.wu.hu    vr6,    vr5
    vsllwil.wu.hu  vr5,    vr5,     0
    vslli.w        vr5,    vr5,     6
    vslli.w        vr6,    vr6,     6
    vslli.w        vr7,    vr7,     6
    vslli.w        vr8,    vr8,     6
    vmul.w         vr5,    vr5,     vr1
    vmul.w         vr6,    vr6,     vr1
    vmul.w         vr7,    vr7,     vr1
    vmul.w         vr8,    vr8,     vr1
    vadd.w         vr5,    vr5,     vr2
    vadd.w         vr6,    vr6,     vr2
    vadd.w         vr7,    vr7,     vr2
    vadd.w         vr8,    vr8,     vr2
    vsra.w         vr5,    vr5,     vr3
    vsra.w         vr6,    vr6,     vr3
    vsra.w         vr7,    vr7,     vr3
    vsra.w         vr8,    vr8,     vr3
    vadd.w         vr5,    vr5,     vr4
    vadd.w         vr6,    vr6,     vr4
    vadd.w         vr7,    vr7,     vr4
    vadd.w         vr8,    vr8,     vr4
    vssrani.h.w    vr6,    vr5,     0
    vssrani.h.w    vr8,    vr7,     0
    vssrani.bu.h   vr8,    vr6,     0
    vst            vr8,    \dst0,   0
.endm

.macro HEVC_PEL_UNI_W_PIXELS16_LASX src0, dst0
    vld            vr0,    \src0,   0
    xvpermi.d      xr0,    xr0,     0xd8
    xvsllwil.hu.bu xr0,    xr0,     0
    xvexth.wu.hu   xr6,    xr0
    xvsllwil.wu.hu xr5,    xr0,     0
    xvslli.w       xr5,    xr5,     6
    xvslli.w       xr6,    xr6,     6
    xvmul.w        xr5,    xr5,     xr1
    xvmul.w        xr6,    xr6,     xr1
    xvadd.w        xr5,    xr5,     xr2
    xvadd.w        xr6,    xr6,     xr2
    xvsra.w        xr5,    xr5,     xr3
    xvsra.w        xr6,    xr6,     xr3
    xvadd.w        xr5,    xr5,     xr4
    xvadd.w        xr6,    xr6,     xr4
    xvssrani.h.w   xr6,    xr5,     0
    xvpermi.q      xr7,    xr6,     0x01
    xvssrani.bu.h  xr7,    xr6,     0
    vst            vr7,    \dst0,   0
.endm

.macro HEVC_PEL_UNI_W_PIXELS32_LASX src0, dst0, w
.if \w == 16
    vld            vr0,    \src0,   0
    add.d          t2,     \src0,   a3
    vld            vr5,    t2,      0
    xvpermi.q      xr0,    xr5,     0x02
.else //w=24/32
    xvld           xr0,    \src0,   0
.endif
    xvexth.hu.bu   xr7,    xr0
    xvexth.wu.hu   xr8,    xr7
    xvsllwil.wu.hu xr7,    xr7,     0
    xvsllwil.hu.bu xr5,    xr0,     0
    xvexth.wu.hu   xr6,    xr5
    xvsllwil.wu.hu xr5,    xr5,     0
    xvslli.w       xr5,    xr5,     6
    xvslli.w       xr6,    xr6,     6
    xvslli.w       xr7,    xr7,     6
    xvslli.w       xr8,    xr8,     6
    xvmul.w        xr5,    xr5,     xr1
    xvmul.w        xr6,    xr6,     xr1
    xvmul.w        xr7,    xr7,     xr1
    xvmul.w        xr8,    xr8,     xr1
    xvadd.w        xr5,    xr5,     xr2
    xvadd.w        xr6,    xr6,     xr2
    xvadd.w        xr7,    xr7,     xr2
    xvadd.w        xr8,    xr8,     xr2
    xvsra.w        xr5,    xr5,     xr3
    xvsra.w        xr6,    xr6,     xr3
    xvsra.w        xr7,    xr7,     xr3
    xvsra.w        xr8,    xr8,     xr3
    xvadd.w        xr5,    xr5,     xr4
    xvadd.w        xr6,    xr6,     xr4
    xvadd.w        xr7,    xr7,     xr4
    xvadd.w        xr8,    xr8,     xr4
    xvssrani.h.w   xr6,    xr5,     0
    xvssrani.h.w   xr8,    xr7,     0
    xvssrani.bu.h  xr8,    xr6,     0
.if \w == 16
    vst            vr8,    \dst0,   0
    add.d          t2,     \dst0,   a1
    xvpermi.q      xr8,    xr8,     0x01
    vst            vr8,    t2,      0
.elseif \w == 24
    vst            vr8,    \dst0,   0
    xvstelm.d      xr8,    \dst0,   16,    2
.else
    xvst           xr8,    \dst0,   0
.endif
.endm

/*
 * void FUNC(put_hevc_pel_uni_w_pixels)(uint8_t *_dst, ptrdiff_t _dststride,
 *                                      const uint8_t *_src, ptrdiff_t _srcstride,
 *                                      int height, int denom, int wx, int ox,
 *                                      intptr_t mx, intptr_t my, int width)
 */
function ff_hevc_put_hevc_pel_uni_w_pixels4_8_lsx
    LOAD_VAR 128
    srli.w         t0,     a4,      1
.LOOP_PIXELS4:
    vldrepl.w      vr0,    a2,      0
    add.d          t1,     a2,      a3
    vldrepl.w      vr5,    t1,      0
    vsllwil.hu.bu  vr0,    vr0,     0
    vsllwil.wu.hu  vr0,    vr0,     0
    vsllwil.hu.bu  vr5,    vr5,     0
    vsllwil.wu.hu  vr5,    vr5,     0
    vslli.w        vr0,    vr0,     6
    vslli.w        vr5,    vr5,     6
    vmul.w         vr0,    vr0,     vr1
    vmul.w         vr5,    vr5,     vr1
    vadd.w         vr0,    vr0,     vr2
    vadd.w         vr5,    vr5,     vr2
    vsra.w         vr0,    vr0,     vr3
    vsra.w         vr5,    vr5,     vr3
    vadd.w         vr0,    vr0,     vr4
    vadd.w         vr5,    vr5,     vr4
    vssrani.h.w    vr5,    vr0,     0
    vssrani.bu.h   vr5,    vr5,     0
    fst.s          f5,     a0,      0
    add.d          t2,     a0,      a1
    vstelm.w       vr5,    t2,      0,     1
    alsl.d         a2,     a3,      a2,    1
    alsl.d         a0,     a1,      a0,    1
    addi.w         t0,     t0,      -1
    bnez           t0,     .LOOP_PIXELS4
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels6_8_lsx
    LOAD_VAR 128
.LOOP_PIXELS6:
    HEVC_PEL_UNI_W_PIXELS8_LSX      a2,    a0,    6
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS6
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels6_8_lasx
    LOAD_VAR 256
    srli.w         t0,     a4,      1
.LOOP_PIXELS6_LASX:
    HEVC_PEL_UNI_W_PIXELS8x2_LASX   a2,    a0,    6
    alsl.d         a2,     a3,      a2,    1
    alsl.d         a0,     a1,      a0,    1
    addi.w         t0,     t0,      -1
    bnez           t0,     .LOOP_PIXELS6_LASX
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels8_8_lsx
    LOAD_VAR 128
.LOOP_PIXELS8:
    HEVC_PEL_UNI_W_PIXELS8_LSX      a2,    a0,    8
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS8
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels8_8_lasx
    LOAD_VAR 256
    srli.w         t0,     a4,      1
.LOOP_PIXELS8_LASX:
    HEVC_PEL_UNI_W_PIXELS8x2_LASX   a2,    a0,    8
    alsl.d         a2,     a3,      a2,    1
    alsl.d         a0,     a1,      a0,    1
    addi.w         t0,     t0,      -1
    bnez           t0,     .LOOP_PIXELS8_LASX
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels12_8_lsx
    LOAD_VAR 128
.LOOP_PIXELS12:
    vld            vr0,    a2,      0
    vexth.hu.bu    vr7,    vr0
    vsllwil.wu.hu  vr7,    vr7,     0
    vsllwil.hu.bu  vr5,    vr0,     0
    vexth.wu.hu    vr6,    vr5
    vsllwil.wu.hu  vr5,    vr5,     0
    vslli.w        vr5,    vr5,     6
    vslli.w        vr6,    vr6,     6
    vslli.w        vr7,    vr7,     6
    vmul.w         vr5,    vr5,     vr1
    vmul.w         vr6,    vr6,     vr1
    vmul.w         vr7,    vr7,     vr1
    vadd.w         vr5,    vr5,     vr2
    vadd.w         vr6,    vr6,     vr2
    vadd.w         vr7,    vr7,     vr2
    vsra.w         vr5,    vr5,     vr3
    vsra.w         vr6,    vr6,     vr3
    vsra.w         vr7,    vr7,     vr3
    vadd.w         vr5,    vr5,     vr4
    vadd.w         vr6,    vr6,     vr4
    vadd.w         vr7,    vr7,     vr4
    vssrani.h.w    vr6,    vr5,     0
    vssrani.h.w    vr7,    vr7,     0
    vssrani.bu.h   vr7,    vr6,     0
    fst.d          f7,     a0,      0
    vstelm.w       vr7,    a0,      8,     2
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS12
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels12_8_lasx
    LOAD_VAR 256
.LOOP_PIXELS12_LASX:
    vld            vr0,    a2,      0
    xvpermi.d      xr0,    xr0,     0xd8
    xvsllwil.hu.bu xr0,    xr0,     0
    xvexth.wu.hu   xr6,    xr0
    xvsllwil.wu.hu xr5,    xr0,     0
    xvslli.w       xr5,    xr5,     6
    xvslli.w       xr6,    xr6,     6
    xvmul.w        xr5,    xr5,     xr1
    xvmul.w        xr6,    xr6,     xr1
    xvadd.w        xr5,    xr5,     xr2
    xvadd.w        xr6,    xr6,     xr2
    xvsra.w        xr5,    xr5,     xr3
    xvsra.w        xr6,    xr6,     xr3
    xvadd.w        xr5,    xr5,     xr4
    xvadd.w        xr6,    xr6,     xr4
    xvssrani.h.w   xr6,    xr5,     0
    xvpermi.q      xr7,    xr6,     0x01
    xvssrani.bu.h  xr7,    xr6,     0
    fst.d          f7,     a0,      0
    vstelm.w       vr7,    a0,      8,     2
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS12_LASX
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels16_8_lsx
    LOAD_VAR 128
.LOOP_PIXELS16:
    HEVC_PEL_UNI_W_PIXELS16_LSX     a2,    a0
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS16
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels16_8_lasx
    LOAD_VAR 256
    srli.w         t0,     a4,      1
.LOOP_PIXELS16_LASX:
    HEVC_PEL_UNI_W_PIXELS32_LASX    a2,    a0,   16
    alsl.d         a2,     a3,      a2,    1
    alsl.d         a0,     a1,      a0,    1
    addi.w         t0,     t0,      -1
    bnez           t0,     .LOOP_PIXELS16_LASX
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels24_8_lsx
    LOAD_VAR 128
.LOOP_PIXELS24:
    HEVC_PEL_UNI_W_PIXELS16_LSX     a2,    a0
    addi.d         t0,     a2,      16
    addi.d         t1,     a0,      16
    HEVC_PEL_UNI_W_PIXELS8_LSX      t0,    t1,   8
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS24
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels24_8_lasx
    LOAD_VAR 256
.LOOP_PIXELS24_LASX:
    HEVC_PEL_UNI_W_PIXELS32_LASX    a2,    a0,   24
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS24_LASX
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels32_8_lsx
    LOAD_VAR 128
.LOOP_PIXELS32:
    HEVC_PEL_UNI_W_PIXELS16_LSX     a2,    a0
    addi.d         t0,     a2,      16
    addi.d         t1,     a0,      16
    HEVC_PEL_UNI_W_PIXELS16_LSX     t0,    t1
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS32
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels32_8_lasx
    LOAD_VAR 256
.LOOP_PIXELS32_LASX:
    HEVC_PEL_UNI_W_PIXELS32_LASX    a2,    a0,    32
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS32_LASX
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels48_8_lsx
    LOAD_VAR 128
.LOOP_PIXELS48:
    HEVC_PEL_UNI_W_PIXELS16_LSX     a2,    a0
    addi.d         t0,     a2,      16
    addi.d         t1,     a0,      16
    HEVC_PEL_UNI_W_PIXELS16_LSX     t0,    t1
    addi.d         t0,     a2,      32
    addi.d         t1,     a0,      32
    HEVC_PEL_UNI_W_PIXELS16_LSX     t0,    t1
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS48
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels48_8_lasx
    LOAD_VAR 256
.LOOP_PIXELS48_LASX:
    HEVC_PEL_UNI_W_PIXELS32_LASX    a2,    a0,    32
    addi.d         t0,     a2,      32
    addi.d         t1,     a0,      32
    HEVC_PEL_UNI_W_PIXELS16_LASX    t0,    t1
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS48_LASX
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels64_8_lsx
    LOAD_VAR 128
.LOOP_PIXELS64:
    HEVC_PEL_UNI_W_PIXELS16_LSX     a2,    a0
    addi.d         t0,     a2,      16
    addi.d         t1,     a0,      16
    HEVC_PEL_UNI_W_PIXELS16_LSX     t0,    t1
    addi.d         t0,     a2,      32
    addi.d         t1,     a0,      32
    HEVC_PEL_UNI_W_PIXELS16_LSX     t0,    t1
    addi.d         t0,     a2,      48
    addi.d         t1,     a0,      48
    HEVC_PEL_UNI_W_PIXELS16_LSX     t0,    t1
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS64
endfunc

function ff_hevc_put_hevc_pel_uni_w_pixels64_8_lasx
    LOAD_VAR 256
.LOOP_PIXELS64_LASX:
    HEVC_PEL_UNI_W_PIXELS32_LASX    a2,    a0,    32
    addi.d         t0,     a2,      32
    addi.d         t1,     a0,      32
    HEVC_PEL_UNI_W_PIXELS32_LASX    t0,    t1,    32
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.w         a4,     a4,      -1
    bnez           a4,     .LOOP_PIXELS64_LASX
endfunc

.macro  vhaddw.d.h  in0
    vhaddw.w.h  \in0,  \in0,  \in0
    vhaddw.d.w  \in0,  \in0,  \in0
.endm

.macro  xvhaddw.d.h  in0
    xvhaddw.w.h  \in0,  \in0,  \in0
    xvhaddw.d.w  \in0,  \in0,  \in0
.endm

/*
 * void FUNC(put_hevc_qpel_uni_w_v)(uint8_t *_dst,  ptrdiff_t _dststride,
 *                                  const uint8_t *_src, ptrdiff_t _srcstride,
 *                                  int height, int denom, int wx, int ox,
 *                                  intptr_t mx, intptr_t my, int width)
 */
function ff_hevc_put_hevc_qpel_uni_w_v4_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0  //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    fld.s          f6,     a2,      0  //0
    fldx.s         f7,     a2,      a3 //1
    fldx.s         f8,     a2,      t0 //2
    add.d          a2,     a2,      t1
    fld.s          f9,     a2,      0  //3
    fldx.s         f10,    a2,      a3 //4
    fldx.s         f11,    a2,      t0 //5
    fldx.s         f12,    a2,      t1 //6
    add.d          a2,     a2,      t2
    vilvl.b        vr6,    vr7,     vr6
    vilvl.b        vr7,    vr9,     vr8
    vilvl.b        vr8,    vr11,    vr10
    vilvl.b        vr9,    vr13,    vr12
    vilvl.h        vr6,    vr7,     vr6
    vilvl.h        vr7,    vr9,     vr8
    vilvl.w        vr8,    vr7,     vr6
    vilvh.w        vr9,    vr7,     vr6
.LOOP_V4:
    fld.s          f13,    a2,      0  //7
    fldx.s         f14,    a2,      a3 //8 next loop
    add.d          a2,     a2,      t0
    vextrins.b     vr8,    vr13,    0x70
    vextrins.b     vr8,    vr13,    0xf1
    vextrins.b     vr9,    vr13,    0x72
    vextrins.b     vr9,    vr13,    0xf3
    vbsrl.v        vr10,   vr8,     1
    vbsrl.v        vr11,   vr9,     1
    vextrins.b     vr10,   vr14,    0x70
    vextrins.b     vr10,   vr14,    0xf1
    vextrins.b     vr11,   vr14,    0x72
    vextrins.b     vr11,   vr14,    0xf3
    vdp2.h.bu.b    vr6,    vr8,     vr5 //QPEL_FILTER(src, stride)
    vdp2.h.bu.b    vr7,    vr9,     vr5
    vdp2.h.bu.b    vr12,   vr10,    vr5
    vdp2.h.bu.b    vr13,   vr11,    vr5
    vbsrl.v        vr8,    vr10,    1
    vbsrl.v        vr9,    vr11,    1
    vhaddw.d.h     vr6
    vhaddw.d.h     vr7
    vhaddw.d.h     vr12
    vhaddw.d.h     vr13
    vpickev.w      vr6,    vr7,     vr6
    vpickev.w      vr12,   vr13,    vr12
    vmulwev.w.h    vr6,    vr6,     vr1 //QPEL_FILTER(src, stride) * wx
    vmulwev.w.h    vr12,   vr12,    vr1
    vadd.w         vr6,    vr6,     vr2
    vsra.w         vr6,    vr6,     vr3
    vadd.w         vr6,    vr6,     vr4
    vadd.w         vr12,   vr12,    vr2
    vsra.w         vr12,   vr12,    vr3
    vadd.w         vr12,   vr12,    vr4
    vssrani.h.w    vr12,   vr6,     0
    vssrani.bu.h   vr12,   vr12,    0
    fst.s          f12,    a0,      0
    add.d          a0,     a0,      a1
    vstelm.w       vr12,   a0,      0,     1
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -2
    bnez           a4,     .LOOP_V4
endfunc

function ff_hevc_put_hevc_qpel_uni_w_v6_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    fld.d          f6,     a2,      0
    fldx.d         f7,     a2,      a3
    fldx.d         f8,     a2,      t0
    add.d          a2,     a2,      t1
    fld.d          f9,     a2,      0
    fldx.d         f10,    a2,      a3
    fldx.d         f11,    a2,      t0
    fldx.d         f12,    a2,      t1
    add.d          a2,     a2,      t2
    vilvl.b        vr6,    vr7,     vr6 //transpose 8x6 to 3x16
    vilvl.b        vr7,    vr9,     vr8
    vilvl.b        vr8,    vr11,    vr10
    vilvl.b        vr9,    vr13,    vr12
    vilvl.h        vr10,   vr7,     vr6
    vilvh.h        vr11,   vr7,     vr6
    vilvl.h        vr12,   vr9,     vr8
    vilvh.h        vr13,   vr9,     vr8
    vilvl.w        vr6,    vr12,    vr10
    vilvh.w        vr7,    vr12,    vr10
    vilvl.w        vr8,    vr13,    vr11
.LOOP_V6:
    fld.d          f13,    a2,      0
    add.d          a2,     a2,      a3
    vextrins.b     vr6,    vr13,    0x70
    vextrins.b     vr6,    vr13,    0xf1
    vextrins.b     vr7,    vr13,    0x72
    vextrins.b     vr7,    vr13,    0xf3
    vextrins.b     vr8,    vr13,    0x74
    vextrins.b     vr8,    vr13,    0xf5
    vdp2.h.bu.b    vr10,   vr6,     vr5 //QPEL_FILTER(src, stride)
    vdp2.h.bu.b    vr11,   vr7,     vr5
    vdp2.h.bu.b    vr12,   vr8,     vr5
    vbsrl.v        vr6,    vr6,     1
    vbsrl.v        vr7,    vr7,     1
    vbsrl.v        vr8,    vr8,     1
    vhaddw.d.h     vr10
    vhaddw.d.h     vr11
    vhaddw.d.h     vr12
    vpickev.w      vr10,   vr11,    vr10
    vpickev.w      vr11,   vr13,    vr12
    vmulwev.w.h    vr10,   vr10,    vr1 //QPEL_FILTER(src, stride) * wx
    vmulwev.w.h    vr11,   vr11,    vr1
    vadd.w         vr10,   vr10,    vr2
    vadd.w         vr11,   vr11,    vr2
    vsra.w         vr10,   vr10,    vr3
    vsra.w         vr11,   vr11,    vr3
    vadd.w         vr10,   vr10,    vr4
    vadd.w         vr11,   vr11,    vr4
    vssrani.h.w    vr11,   vr10,    0
    vssrani.bu.h   vr11,   vr11,    0
    fst.s          f11,    a0,      0
    vstelm.h       vr11,   a0,      4,    2
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_V6
endfunc

// transpose 8x8b to 4x16b
.macro TRANSPOSE8X8B_LSX in0, in1, in2, in3, in4, in5, in6, in7, \
                         out0, out1, out2, out3
    vilvl.b        \in0,    \in1,     \in0
    vilvl.b        \in1,    \in3,     \in2
    vilvl.b        \in2,    \in5,     \in4
    vilvl.b        \in3,    \in7,     \in6
    vilvl.h        \in4,    \in1,     \in0
    vilvh.h        \in5,    \in1,     \in0
    vilvl.h        \in6,    \in3,     \in2
    vilvh.h        \in7,    \in3,     \in2
    vilvl.w        \out0,   \in6,     \in4
    vilvh.w        \out1,   \in6,     \in4
    vilvl.w        \out2,   \in7,     \in5
    vilvh.w        \out3,   \in7,     \in5
.endm

.macro PUT_HEVC_QPEL_UNI_W_V8_LSX in0, in1, in2, in3, out0, out1, pos
.if \pos == 0
    vextrins.b     \in0,    vr13,    0x70 //insert the 8th load
    vextrins.b     \in0,    vr13,    0xf1
    vextrins.b     \in1,    vr13,    0x72
    vextrins.b     \in1,    vr13,    0xf3
    vextrins.b     \in2,    vr13,    0x74
    vextrins.b     \in2,    vr13,    0xf5
    vextrins.b     \in3,    vr13,    0x76
    vextrins.b     \in3,    vr13,    0xf7
.else// \pos == 8
    vextrins.b     \in0,    vr13,    0x78
    vextrins.b     \in0,    vr13,    0xf9
    vextrins.b     \in1,    vr13,    0x7a
    vextrins.b     \in1,    vr13,    0xfb
    vextrins.b     \in2,    vr13,    0x7c
    vextrins.b     \in2,    vr13,    0xfd
    vextrins.b     \in3,    vr13,    0x7e
    vextrins.b     \in3,    vr13,    0xff
.endif
    vdp2.h.bu.b    \out0,   \in0,    vr5 //QPEL_FILTER(src, stride)
    vdp2.h.bu.b    \out1,   \in1,    vr5
    vdp2.h.bu.b    vr12,    \in2,    vr5
    vdp2.h.bu.b    vr20,    \in3,    vr5
    vbsrl.v        \in0,    \in0,    1 //Back up previous 7 loaded datas,
    vbsrl.v        \in1,    \in1,    1 //so just need to insert the 8th
    vbsrl.v        \in2,    \in2,    1 //load in the next loop.
    vbsrl.v        \in3,    \in3,    1
    vhaddw.d.h     \out0
    vhaddw.d.h     \out1
    vhaddw.d.h     vr12
    vhaddw.d.h     vr20
    vpickev.w      \out0,   \out1,   \out0
    vpickev.w      \out1,   vr20,    vr12
    vmulwev.w.h    \out0,   \out0,   vr1 //QPEL_FILTER(src, stride) * wx
    vmulwev.w.h    \out1,   \out1,   vr1
    vadd.w         \out0,   \out0,   vr2
    vadd.w         \out1,   \out1,   vr2
    vsra.w         \out0,   \out0,   vr3
    vsra.w         \out1,   \out1,   vr3
    vadd.w         \out0,   \out0,   vr4
    vadd.w         \out1,   \out1,   vr4
.endm

function ff_hevc_put_hevc_qpel_uni_w_v8_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    fld.d          f6,     a2,      0
    fldx.d         f7,     a2,      a3
    fldx.d         f8,     a2,      t0
    add.d          a2,     a2,      t1
    fld.d          f9,     a2,      0
    fldx.d         f10,    a2,      a3
    fldx.d         f11,    a2,      t0
    fldx.d         f12,    a2,      t1
    add.d          a2,     a2,      t2
    TRANSPOSE8X8B_LSX vr6, vr7, vr8, vr9, vr10, vr11, vr12, vr13, \
                      vr6, vr7, vr8, vr9
.LOOP_V8:
    fld.d          f13,    a2,      0 //the 8th load
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_V8_LSX vr6, vr7, vr8, vr9, vr10, vr11, 0
    vssrani.h.w    vr11,   vr10,    0
    vssrani.bu.h   vr11,   vr11,    0
    fst.d          f11,    a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_V8
endfunc

.macro PUT_HEVC_UNI_W_V8_LASX w
    fld.d          f6,     a2,      0
    fldx.d         f7,     a2,      a3
    fldx.d         f8,     a2,      t0
    add.d          a2,     a2,      t1
    fld.d          f9,     a2,      0
    fldx.d         f10,    a2,      a3
    fldx.d         f11,    a2,      t0
    fldx.d         f12,    a2,      t1
    add.d          a2,     a2,      t2
    TRANSPOSE8X8B_LSX vr6, vr7, vr8, vr9, vr10, vr11, vr12, vr13, \
                      vr6, vr7, vr8, vr9
    xvpermi.q      xr6,    xr7,     0x02
    xvpermi.q      xr8,    xr9,     0x02
.LOOP_V8_LASX_\w:
    fld.d          f13,    a2,      0 // 0 1 2 3 4 5 6 7 the 8th load
    add.d          a2,     a2,      a3
    vshuf4i.h      vr13,   vr13,    0xd8
    vbsrl.v        vr14,   vr13,    4
    xvpermi.q      xr13,   xr14,    0x02 //0 1 4 5 * * * * 2 3 6 7 * * * *
    xvextrins.b    xr6,    xr13,    0x70 //begin to insert the 8th load
    xvextrins.b    xr6,    xr13,    0xf1
    xvextrins.b    xr8,    xr13,    0x72
    xvextrins.b    xr8,    xr13,    0xf3
    xvdp2.h.bu.b   xr20,   xr6,     xr5 //QPEL_FILTER(src, stride)
    xvdp2.h.bu.b   xr21,   xr8,     xr5
    xvbsrl.v       xr6,    xr6,     1
    xvbsrl.v       xr8,    xr8,     1
    xvhaddw.d.h    xr20
    xvhaddw.d.h    xr21
    xvpickev.w     xr20,   xr21,    xr20
    xvpermi.d      xr20,   xr20,    0xd8
    xvmulwev.w.h   xr20,   xr20,    xr1 //QPEL_FILTER(src, stride) * wx
    xvadd.w        xr20,   xr20,    xr2
    xvsra.w        xr20,   xr20,    xr3
    xvadd.w        xr10,   xr20,    xr4
    xvpermi.q      xr11,   xr10,    0x01
    vssrani.h.w    vr11,   vr10,    0
    vssrani.bu.h   vr11,   vr11,    0
    fst.d          f11,    a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_V8_LASX_\w
.endm

function ff_hevc_put_hevc_qpel_uni_w_v8_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    PUT_HEVC_UNI_W_V8_LASX 8
endfunc

.macro PUT_HEVC_QPEL_UNI_W_V16_LSX w
    vld            vr6,    a2,      0
    vldx           vr7,    a2,      a3
    vldx           vr8,    a2,      t0
    add.d          a2,     a2,      t1
    vld            vr9,    a2,      0
    vldx           vr10,   a2,      a3
    vldx           vr11,   a2,      t0
    vldx           vr12,   a2,      t1
    add.d          a2,     a2,      t2
.if \w > 8
    vilvh.d        vr14,   vr14,    vr6
    vilvh.d        vr15,   vr15,    vr7
    vilvh.d        vr16,   vr16,    vr8
    vilvh.d        vr17,   vr17,    vr9
    vilvh.d        vr18,   vr18,    vr10
    vilvh.d        vr19,   vr19,    vr11
    vilvh.d        vr20,   vr20,    vr12
.endif
    TRANSPOSE8X8B_LSX vr6,  vr7,  vr8,  vr9,  vr10, vr11, vr12, vr13, \
                      vr6,  vr7,  vr8,  vr9
.if \w > 8
    TRANSPOSE8X8B_LSX vr14, vr15, vr16, vr17, vr18, vr19, vr20, vr21, \
                      vr14, vr15, vr16, vr17
.endif
.LOOP_HORI_16_\w:
    vld            vr13,   a2,      0
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_V8_LSX vr6,  vr7,  vr8,  vr9,  vr10, vr11, 0
.if \w > 8
    PUT_HEVC_QPEL_UNI_W_V8_LSX vr14, vr15, vr16, vr17, vr18, vr19, 8
.endif
    vssrani.h.w    vr11,   vr10,    0
.if \w > 8
    vssrani.h.w    vr19,   vr18,    0
    vssrani.bu.h   vr19,   vr11,    0
.else
    vssrani.bu.h   vr11,   vr11,    0
.endif
.if \w == 8
    fst.d          f11,    a0,      0
.elseif \w == 12
    fst.d          f19,    a0,      0
    vstelm.w       vr19,   a0,      8,    2
.else
    vst            vr19,   a0,      0
.endif
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_HORI_16_\w
.endm

function ff_hevc_put_hevc_qpel_uni_w_v16_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    PUT_HEVC_QPEL_UNI_W_V16_LSX 16
endfunc

.macro PUT_HEVC_QPEL_UNI_W_V16_LASX w
    vld            vr6,    a2,      0
    vldx           vr7,    a2,      a3
    vldx           vr8,    a2,      t0
    add.d          a2,     a2,      t1
    vld            vr9,    a2,      0
    vldx           vr10,   a2,      a3
    vldx           vr11,   a2,      t0
    vldx           vr12,   a2,      t1
    add.d          a2,     a2,      t2
    xvpermi.q      xr6,    xr10,    0x02 //pack and transpose the 8x16 to 4x32 begin
    xvpermi.q      xr7,    xr11,    0x02
    xvpermi.q      xr8,    xr12,    0x02
    xvpermi.q      xr9,    xr13,    0x02
    xvilvl.b       xr14,   xr7,     xr6 //0 2
    xvilvh.b       xr15,   xr7,     xr6 //1 3
    xvilvl.b       xr16,   xr9,     xr8 //0 2
    xvilvh.b       xr17,   xr9,     xr8 //1 3
    xvpermi.d      xr14,   xr14,    0xd8
    xvpermi.d      xr15,   xr15,    0xd8
    xvpermi.d      xr16,   xr16,    0xd8
    xvpermi.d      xr17,   xr17,    0xd8
    xvilvl.h       xr6,    xr16,    xr14
    xvilvh.h       xr7,    xr16,    xr14
    xvilvl.h       xr8,    xr17,    xr15
    xvilvh.h       xr9,    xr17,    xr15
    xvilvl.w       xr14,   xr7,     xr6 //0 1 4 5
    xvilvh.w       xr15,   xr7,     xr6 //2 3 6 7
    xvilvl.w       xr16,   xr9,     xr8 //8 9 12 13
    xvilvh.w       xr17,   xr9,     xr8 //10 11 14 15 end
.LOOP_HORI_16_LASX_\w:
    vld            vr13,   a2,      0 //the 8th load
    add.d          a2,     a2,      a3
    vshuf4i.w      vr13,   vr13,    0xd8
    vbsrl.v        vr12,   vr13,    8
    xvpermi.q      xr13,   xr12,    0x02
    xvextrins.b    xr14,   xr13,    0x70 //inset the 8th load
    xvextrins.b    xr14,   xr13,    0xf1
    xvextrins.b    xr15,   xr13,    0x72
    xvextrins.b    xr15,   xr13,    0xf3
    xvextrins.b    xr16,   xr13,    0x74
    xvextrins.b    xr16,   xr13,    0xf5
    xvextrins.b    xr17,   xr13,    0x76
    xvextrins.b    xr17,   xr13,    0xf7
    xvdp2.h.bu.b   xr6,    xr14,    xr5 //QPEL_FILTER(src, stride)
    xvdp2.h.bu.b   xr7,    xr15,    xr5
    xvdp2.h.bu.b   xr8,    xr16,    xr5
    xvdp2.h.bu.b   xr9,    xr17,    xr5
    xvhaddw.d.h    xr6
    xvhaddw.d.h    xr7
    xvhaddw.d.h    xr8
    xvhaddw.d.h    xr9
    xvbsrl.v       xr14,   xr14,    1 //Back up previous 7 loaded datas,
    xvbsrl.v       xr15,   xr15,    1 //so just need to insert the 8th
    xvbsrl.v       xr16,   xr16,    1 //load in next loop.
    xvbsrl.v       xr17,   xr17,    1
    xvpickev.w     xr6,    xr7,     xr6 //0 1 2 3 4 5 6 7
    xvpickev.w     xr7,    xr9,     xr8 //8 9 10 11 12 13 14 15
    xvmulwev.w.h   xr6,    xr6,     xr1 //QPEL_FILTER(src, stride) * wx
    xvmulwev.w.h   xr7,    xr7,     xr1
    xvadd.w        xr6,    xr6,     xr2
    xvadd.w        xr7,    xr7,     xr2
    xvsra.w        xr6,    xr6,     xr3
    xvsra.w        xr7,    xr7,     xr3
    xvadd.w        xr6,    xr6,     xr4
    xvadd.w        xr7,    xr7,     xr4
    xvssrani.h.w   xr7,    xr6,     0 //0 1 2 3  8 9 10 11  4 5 6 7 12 13 14 15
    xvpermi.q      xr6,    xr7,     0x01
    vssrani.bu.h   vr6,    vr7,     0
    vshuf4i.w      vr6,    vr6,     0xd8
.if \w == 12
    fst.d          f6,     a0,      0
    vstelm.w       vr6,    a0,      8,   2
.else
    vst            vr6,    a0,      0
.endif
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_HORI_16_LASX_\w
.endm

function ff_hevc_put_hevc_qpel_uni_w_v16_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    PUT_HEVC_QPEL_UNI_W_V16_LASX 16
endfunc

function ff_hevc_put_hevc_qpel_uni_w_v12_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    PUT_HEVC_QPEL_UNI_W_V16_LSX 12
endfunc

function ff_hevc_put_hevc_qpel_uni_w_v12_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    PUT_HEVC_QPEL_UNI_W_V16_LASX 12
endfunc

function ff_hevc_put_hevc_qpel_uni_w_v24_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    addi.d         t4,     a0,      0 //save dst
    addi.d         t5,     a2,      0 //save src
    addi.d         t6,     a4,      0
    PUT_HEVC_QPEL_UNI_W_V16_LSX 24
    addi.d         a0,     t4,      16
    addi.d         a2,     t5,      16
    addi.d         a4,     t6,      0
    PUT_HEVC_QPEL_UNI_W_V16_LSX 8
endfunc

function ff_hevc_put_hevc_qpel_uni_w_v24_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    addi.d         t4,     a0,      0 //save dst
    addi.d         t5,     a2,      0 //save src
    addi.d         t6,     a4,      0
    PUT_HEVC_QPEL_UNI_W_V16_LASX 24
    addi.d         a0,     t4,      16
    addi.d         a2,     t5,      16
    addi.d         a4,     t6,      0
    PUT_HEVC_UNI_W_V8_LASX 24
endfunc

function ff_hevc_put_hevc_qpel_uni_w_v32_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    addi.d         t3,     zero,    2
    addi.d         t4,     a0,      0 //save dst
    addi.d         t5,     a2,      0 //save src
    addi.d         t6,     a4,      0
.LOOP_V32:
    PUT_HEVC_QPEL_UNI_W_V16_LSX 32
    addi.d         t3,     t3,      -1
    addi.d         a0,     t4,      16
    addi.d         a2,     t5,      16
    addi.d         a4,     t6,      0
    bnez           t3,     .LOOP_V32
endfunc

function ff_hevc_put_hevc_qpel_uni_w_v32_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    addi.d         t3,     zero,    2
    addi.d         t4,     a0,      0 //save dst
    addi.d         t5,     a2,      0 //save src
    addi.d         t6,     a4,      0
.LOOP_V32_LASX:
    PUT_HEVC_QPEL_UNI_W_V16_LASX 32
    addi.d         t3,     t3,      -1
    addi.d         a0,     t4,      16
    addi.d         a2,     t5,      16
    addi.d         a4,     t6,      0
    bnez           t3,     .LOOP_V32_LASX
endfunc

function ff_hevc_put_hevc_qpel_uni_w_v48_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    addi.d         t3,     zero,    3
    addi.d         t4,     a0,      0 //save dst
    addi.d         t5,     a2,      0 //save src
    addi.d         t6,     a4,      0
.LOOP_V48:
    PUT_HEVC_QPEL_UNI_W_V16_LSX 48
    addi.d         t3,     t3,      -1
    addi.d         a0,     t4,      16
    addi.d         t4,     t4,      16
    addi.d         a2,     t5,      16
    addi.d         t5,     t5,      16
    addi.d         a4,     t6,      0
    bnez           t3,     .LOOP_V48
endfunc

function ff_hevc_put_hevc_qpel_uni_w_v48_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    addi.d         t3,     zero,    3
    addi.d         t4,     a0,      0 //save dst
    addi.d         t5,     a2,      0 //save src
    addi.d         t6,     a4,      0
.LOOP_V48_LASX:
    PUT_HEVC_QPEL_UNI_W_V16_LASX 48
    addi.d         t3,     t3,      -1
    addi.d         a0,     t4,      16
    addi.d         t4,     t4,      16
    addi.d         a2,     t5,      16
    addi.d         t5,     t5,      16
    addi.d         a4,     t6,      0
    bnez           t3,     .LOOP_V48_LASX
endfunc

function ff_hevc_put_hevc_qpel_uni_w_v64_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    addi.d         t3,     zero,    4
    addi.d         t4,     a0,      0 //save dst
    addi.d         t5,     a2,      0 //save src
    addi.d         t6,     a4,      0
.LOOP_V64:
    PUT_HEVC_QPEL_UNI_W_V16_LSX 64
    addi.d         t3,     t3,      -1
    addi.d         a0,     t4,      16
    addi.d         t4,     t4,      16
    addi.d         a2,     t5,      16
    addi.d         t5,     t5,      16
    addi.d         a4,     t6,      0
    bnez           t3,     .LOOP_V64
endfunc

function ff_hevc_put_hevc_qpel_uni_w_v64_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    add.d          t2,     t1,      a3 //stride * 4
    sub.d          a2,     a2,      t1 //src -= stride*3
    addi.d         t3,     zero,    4
    addi.d         t4,     a0,      0 //save dst
    addi.d         t5,     a2,      0 //save src
    addi.d         t6,     a4,      0
.LOOP_V64_LASX:
    PUT_HEVC_QPEL_UNI_W_V16_LASX 64
    addi.d         t3,     t3,      -1
    addi.d         a0,     t4,      16
    addi.d         t4,     t4,      16
    addi.d         a2,     t5,      16
    addi.d         t5,     t5,      16
    addi.d         a4,     t6,      0
    bnez           t3,     .LOOP_V64_LASX
endfunc

.macro PUT_HEVC_QPEL_UNI_W_H8_LSX in0, out0, out1
    vbsrl.v        vr7,    \in0,    1
    vbsrl.v        vr8,    \in0,    2
    vbsrl.v        vr9,    \in0,    3
    vbsrl.v        vr10,   \in0,    4
    vbsrl.v        vr11,   \in0,    5
    vbsrl.v        vr12,   \in0,    6
    vbsrl.v        vr13,   \in0,    7
    vilvl.d        vr6,    vr7,     \in0
    vilvl.d        vr7,    vr9,     vr8
    vilvl.d        vr8,    vr11,    vr10
    vilvl.d        vr9,    vr13,    vr12
    vdp2.h.bu.b    vr10,   vr6,     vr5
    vdp2.h.bu.b    vr11,   vr7,     vr5
    vdp2.h.bu.b    vr12,   vr8,     vr5
    vdp2.h.bu.b    vr13,   vr9,     vr5
    vhaddw.d.h     vr10
    vhaddw.d.h     vr11
    vhaddw.d.h     vr12
    vhaddw.d.h     vr13
    vpickev.w      vr10,   vr11,    vr10
    vpickev.w      vr11,   vr13,    vr12
    vmulwev.w.h    vr10,   vr10,    vr1
    vmulwev.w.h    vr11,   vr11,    vr1
    vadd.w         vr10,   vr10,    vr2
    vadd.w         vr11,   vr11,    vr2
    vsra.w         vr10,   vr10,    vr3
    vsra.w         vr11,   vr11,    vr3
    vadd.w         \out0,  vr10,    vr4
    vadd.w         \out1,  vr11,    vr4
.endm

.macro PUT_HEVC_QPEL_UNI_W_H8_LASX in0, out0
    xvbsrl.v       xr7,    \in0,    4
    xvpermi.q      xr7,    \in0,    0x20
    xvbsrl.v       xr8,    xr7,     1
    xvbsrl.v       xr9,    xr7,     2
    xvbsrl.v       xr10,   xr7,     3
    xvpackev.d     xr7,    xr8,     xr7
    xvpackev.d     xr8,    xr10,    xr9
    xvdp2.h.bu.b   xr10,   xr7,     xr5
    xvdp2.h.bu.b   xr11,   xr8,     xr5
    xvhaddw.d.h    xr10
    xvhaddw.d.h    xr11
    xvpickev.w     xr10,   xr11,    xr10
    xvmulwev.w.h   xr10,   xr10,    xr1
    xvadd.w        xr10,   xr10,    xr2
    xvsra.w        xr10,   xr10,    xr3
    xvadd.w        \out0,  xr10,    xr4
.endm

.macro PUT_HEVC_QPEL_UNI_W_H16_LASX in0, out0
    xvpermi.d      xr6,    \in0,    0x94
    xvbsrl.v       xr7,    xr6,     1
    xvbsrl.v       xr8,    xr6,     2
    xvbsrl.v       xr9,    xr6,     3
    xvbsrl.v       xr10,   xr6,     4
    xvbsrl.v       xr11,   xr6,     5
    xvbsrl.v       xr12,   xr6,     6
    xvbsrl.v       xr13,   xr6,     7
    xvpackev.d     xr6,    xr7,     xr6
    xvpackev.d     xr7,    xr9,     xr8
    xvpackev.d     xr8,    xr11,    xr10
    xvpackev.d     xr9,    xr13,    xr12
    xvdp2.h.bu.b   xr10,   xr6,     xr5
    xvdp2.h.bu.b   xr11,   xr7,     xr5
    xvdp2.h.bu.b   xr12,   xr8,     xr5
    xvdp2.h.bu.b   xr13,   xr9,     xr5
    xvhaddw.d.h    xr10
    xvhaddw.d.h    xr11
    xvhaddw.d.h    xr12
    xvhaddw.d.h    xr13
    xvpickev.w     xr10,   xr11,    xr10
    xvpickev.w     xr11,   xr13,    xr12
    xvmulwev.w.h   xr10,   xr10,    xr1
    xvmulwev.w.h   xr11,   xr11,    xr1
    xvadd.w        xr10,   xr10,    xr2
    xvadd.w        xr11,   xr11,    xr2
    xvsra.w        xr10,   xr10,    xr3
    xvsra.w        xr11,   xr11,    xr3
    xvadd.w        xr10,   xr10,    xr4
    xvadd.w        xr11,   xr11,    xr4
    xvssrani.h.w   xr11,   xr10,    0
    xvpermi.q      \out0,  xr11,    0x01
    xvssrani.bu.h  \out0,  xr11,    0
.endm

/*
 * void FUNC(put_hevc_qpel_uni_w_h)(uint8_t *_dst,  ptrdiff_t _dststride,
 *                                  const uint8_t *_src, ptrdiff_t _srcstride,
 *                                  int height, int denom, int wx, int ox,
 *                                  intptr_t mx, intptr_t my, int width)
 */
function ff_hevc_put_hevc_qpel_uni_w_h4_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H4:
    vld            vr18,   a2,      0
    vldx           vr19,   a2,      a3
    alsl.d         a2,     a3,      a2,   1
    vbsrl.v        vr6,    vr18,    1
    vbsrl.v        vr7,    vr18,    2
    vbsrl.v        vr8,    vr18,    3
    vbsrl.v        vr9,    vr19,    1
    vbsrl.v        vr10,   vr19,    2
    vbsrl.v        vr11,   vr19,    3
    vilvl.d        vr6,    vr6,     vr18
    vilvl.d        vr7,    vr8,     vr7
    vilvl.d        vr8,    vr9,     vr19
    vilvl.d        vr9,    vr11,    vr10
    vdp2.h.bu.b    vr10,   vr6,     vr5
    vdp2.h.bu.b    vr11,   vr7,     vr5
    vdp2.h.bu.b    vr12,   vr8,     vr5
    vdp2.h.bu.b    vr13,   vr9,     vr5
    vhaddw.d.h     vr10
    vhaddw.d.h     vr11
    vhaddw.d.h     vr12
    vhaddw.d.h     vr13
    vpickev.w      vr10,   vr11,    vr10
    vpickev.w      vr11,   vr13,    vr12
    vmulwev.w.h    vr10,   vr10,    vr1
    vmulwev.w.h    vr11,   vr11,    vr1
    vadd.w         vr10,   vr10,    vr2
    vadd.w         vr11,   vr11,    vr2
    vsra.w         vr10,   vr10,    vr3
    vsra.w         vr11,   vr11,    vr3
    vadd.w         vr10,   vr10,    vr4
    vadd.w         vr11,   vr11,    vr4
    vssrani.h.w    vr11,   vr10,    0
    vssrani.bu.h   vr11,   vr11,    0
    fst.s          f11,    a0,      0
    vbsrl.v        vr11,   vr11,    4
    fstx.s         f11,    a0,      a1
    alsl.d         a0,     a1,      a0,   1
    addi.d         a4,     a4,      -2
    bnez           a4,     .LOOP_H4
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h4_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H4_LASX:
    vld            vr18,   a2,      0
    vldx           vr19,   a2,      a3
    alsl.d         a2,     a3,      a2,   1
    xvpermi.q      xr18,   xr19,    0x02
    xvbsrl.v       xr6,    xr18,    1
    xvbsrl.v       xr7,    xr18,    2
    xvbsrl.v       xr8,    xr18,    3
    xvpackev.d     xr6,    xr6,     xr18
    xvpackev.d     xr7,    xr8,     xr7
    xvdp2.h.bu.b   xr10,   xr6,     xr5
    xvdp2.h.bu.b   xr11,   xr7,     xr5
    xvhaddw.d.h    xr10
    xvhaddw.d.h    xr11
    xvpickev.w     xr10,   xr11,    xr10
    xvmulwev.w.h   xr10,   xr10,    xr1
    xvadd.w        xr10,   xr10,    xr2
    xvsra.w        xr10,   xr10,    xr3
    xvadd.w        xr10,   xr10,    xr4
    xvpermi.q      xr11,   xr10,    0x01
    vssrani.h.w    vr11,   vr10,    0
    vssrani.bu.h   vr11,   vr11,    0
    fst.s          f11,    a0,      0
    vbsrl.v        vr11,   vr11,    4
    fstx.s         f11,    a0,      a1
    alsl.d         a0,     a1,      a0,   1
    addi.d         a4,     a4,      -2
    bnez           a4,     .LOOP_H4_LASX
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h6_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H6:
    vld            vr6,    a2,      0
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr6, vr10, vr11
    vssrani.h.w    vr11,   vr10,    0
    vssrani.bu.h   vr11,   vr11,    0
    fst.s          f11,    a0,      0
    vstelm.h       vr11,   a0,      4,   2
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H6
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h6_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H6_LASX:
    vld            vr6,    a2,      0
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H8_LASX xr6, xr10
    xvpermi.q      xr11,   xr10,    0x01
    vssrani.h.w    vr11,   vr10,    0
    vssrani.bu.h   vr11,   vr11,    0
    fst.s          f11,    a0,      0
    vstelm.h       vr11,   a0,      4,   2
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H6_LASX
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h8_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H8:
    vld            vr6,    a2,      0
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr6, vr10, vr11
    vssrani.h.w    vr11,   vr10,    0
    vssrani.bu.h   vr11,   vr11,    0
    fst.d          f11,    a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H8
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h8_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H8_LASX:
    vld            vr6,    a2,      0
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H8_LASX xr6, xr10
    xvpermi.q      xr11,   xr10,    0x01
    vssrani.h.w    vr11,   vr10,    0
    vssrani.bu.h   vr11,   vr11,    0
    fst.d          f11,    a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H8_LASX
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h12_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H12:
    vld            vr6,    a2,      0
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr6, vr14, vr15
    vld            vr6,    a2,      8
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr6, vr16, vr17
    add.d          a2,     a2,      a3
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    fst.d          f17,    a0,      0
    vbsrl.v        vr17,   vr17,    8
    fst.s          f17,    a0,      8
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H12
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h12_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H12_LASX:
    xvld           xr6,    a2,      0
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H16_LASX xr6, xr14
    fst.d          f14,    a0,      0
    vstelm.w       vr14,   a0,      8,    2
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H12_LASX
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h16_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H16:
    vld            vr6,    a2,      0
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr6, vr14, vr15
    vld            vr6,    a2,      8
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr6, vr16, vr17
    add.d          a2,     a2,      a3
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vst            vr17,   a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H16
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h16_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H16_LASX:
    xvld           xr6,    a2,      0
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H16_LASX xr6, xr10
    vst            vr10,   a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H16_LASX
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h24_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H24:
    vld            vr18,   a2,      0
    vld            vr19,   a2,      16
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr18, vr14, vr15
    vshuf4i.d      vr18,   vr19,    0x09
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr18, vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vst            vr17,   a0,      0
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr19, vr14, vr15
    vssrani.h.w    vr15,   vr14,    0
    vssrani.bu.h   vr15,   vr15,    0
    fst.d          f15,    a0,      16
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H24
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h24_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H24_LASX:
    xvld           xr18,   a2,      0
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H16_LASX xr18, xr20
    xvpermi.q      xr19,   xr18,    0x01
    vst            vr20,   a0,      0
    PUT_HEVC_QPEL_UNI_W_H8_LASX xr19, xr20
    xvpermi.q      xr21,   xr20,    0x01
    vssrani.h.w    vr21,   vr20,    0
    vssrani.bu.h   vr21,   vr21,    0
    fst.d          f21,    a0,      16
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H24_LASX
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h32_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H32:
    vld            vr18,   a2,      0
    vld            vr19,   a2,      16
    vld            vr20,   a2,      32
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr18, vr14, vr15
    vshuf4i.d      vr18,   vr19,    0x09
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr18, vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vst            vr17,   a0,      0
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr19, vr14, vr15
    vshuf4i.d      vr19,   vr20,   0x09
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr19, vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vst            vr17,   a0,      16
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H32
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h32_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H32_LASX:
    xvld           xr18,   a2,      0
    xvld           xr19,   a2,      16
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H16_LASX xr18, xr20
    PUT_HEVC_QPEL_UNI_W_H16_LASX xr19, xr21
    xvpermi.q      xr20,   xr21,    0x02
    xvst           xr20,   a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H32_LASX
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h48_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H48:
    vld            vr18,   a2,      0
    vld            vr19,   a2,      16
    vld            vr20,   a2,      32
    vld            vr21,   a2,      48
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr18, vr14, vr15
    vshuf4i.d      vr18,   vr19,    0x09
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr18, vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vst            vr17,   a0,      0
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr19, vr14, vr15
    vshuf4i.d      vr19,   vr20,    0x09
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr19, vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vst            vr17,   a0,      16
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr20, vr14, vr15
    vshuf4i.d      vr20,   vr21,    0x09
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr20, vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vst            vr17,   a0,      32
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H48
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h48_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H48_LASX:
    xvld           xr18,   a2,      0
    xvld           xr19,   a2,      32
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H16_LASX xr18, xr20
    xvpermi.q      xr18,   xr19,    0x03
    PUT_HEVC_QPEL_UNI_W_H16_LASX xr18, xr21
    xvpermi.q      xr20,   xr21,    0x02
    xvst           xr20,   a0,      0
    PUT_HEVC_QPEL_UNI_W_H16_LASX xr19, xr20
    vst            vr20,   a0,      32
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H48_LASX
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h64_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H64:
    vld            vr18,   a2,      0
    vld            vr19,   a2,      16
    vld            vr20,   a2,      32
    vld            vr21,   a2,      48
    vld            vr22,   a2,      64
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr18, vr14, vr15
    vshuf4i.d      vr18,   vr19,    0x09
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr18, vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vst            vr17,   a0,      0
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr19, vr14, vr15
    vshuf4i.d      vr19,   vr20,    0x09
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr19, vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vst            vr17,   a0,      16
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr20, vr14, vr15
    vshuf4i.d      vr20,   vr21,    0x09
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr20, vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vst            vr17,   a0,      32
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr21, vr14, vr15
    vshuf4i.d      vr21,   vr22,    0x09
    PUT_HEVC_QPEL_UNI_W_H8_LSX vr21, vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vst            vr17,   a0,      48
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H64
endfunc

function ff_hevc_put_hevc_qpel_uni_w_h64_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    xvreplve0.q    xr5,    xr5
    addi.d         a2,     a2,      -3 //src -= 3
.LOOP_H64_LASX:
    xvld           xr18,   a2,      0
    xvld           xr19,   a2,      32
    xvld           xr20,   a2,      64
    add.d          a2,     a2,      a3
    PUT_HEVC_QPEL_UNI_W_H16_LASX xr18, xr21
    xvpermi.q      xr18,   xr19,    0x03
    PUT_HEVC_QPEL_UNI_W_H16_LASX xr18, xr22
    xvpermi.q      xr21,   xr22,    0x02
    xvst           xr21,   a0,      0
    PUT_HEVC_QPEL_UNI_W_H16_LASX xr19, xr21
    xvpermi.q      xr19,   xr20,    0x03
    PUT_HEVC_QPEL_UNI_W_H16_LASX xr19, xr22
    xvpermi.q      xr21,   xr22,    0x02
    xvst           xr21,   a0,      32
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_H64_LASX
endfunc

const shufb
    .byte 0,1,2,3, 1,2,3,4 ,2,3,4,5, 3,4,5,6  //mask for epel_uni_w(128-bit)
    .byte 4,5,6,7, 5,6,7,8 ,6,7,8,9, 7,8,9,10 //mask for epel_uni_w(256-bit)
    .byte 0,1,2,3, 4,5,6,7 ,1,2,3,4, 5,6,7,8  //mask for qpel_uni_h4
    .byte 0,1,1,2, 2,3,3,4 ,4,5,5,6, 6,7,7,8  //mask for qpel_uni_h/v6/8...
    .byte 0,1,2,3, 1,2,3,4 ,2,3,4,5, 3,4,5,6, 4,5,6,7, 5,6,7,8, 6,7,8,9, 7,8,9,10 //epel_uni_w_h16/24/32/48/64
    .byte 0,1,1,2, 2,3,3,4 ,4,5,5,6, 6,7,7,8, 0,1,1,2, 2,3,3,4 ,4,5,5,6, 6,7,7,8  //mask for bi_epel_h16/24/32/48/64
endconst

.macro PUT_HEVC_EPEL_UNI_W_HV4_LSX w
    fld.d          f7,     a2,      0  // start to load src
    fldx.d         f8,     a2,      a3
    alsl.d         a2,     a3,      a2,    1
    fld.d          f9,     a2,      0
    vshuf.b        vr7,    vr7,     vr7,   vr0 // 0123 1234 2345 3456
    vshuf.b        vr8,    vr8,     vr8,   vr0
    vshuf.b        vr9,    vr9,     vr9,   vr0
    vdp2.h.bu.b    vr10,   vr7,     vr5  // EPEL_FILTER(src, 1)
    vdp2.h.bu.b    vr11,   vr8,     vr5
    vdp2.h.bu.b    vr12,   vr9,     vr5
    vhaddw.w.h     vr10,   vr10,    vr10 // tmp[0/1/2/3]
    vhaddw.w.h     vr11,   vr11,    vr11 // vr10,vr11,vr12 corresponding to EPEL_EXTRA
    vhaddw.w.h     vr12,   vr12,    vr12
.LOOP_HV4_\w:
    add.d          a2,     a2,      a3
    fld.d          f14,    a2,      0    // height loop begin
    vshuf.b        vr14,   vr14,    vr14,  vr0
    vdp2.h.bu.b    vr13,   vr14,    vr5
    vhaddw.w.h     vr13,   vr13,    vr13
    vmul.w         vr14,   vr10,    vr16 // EPEL_FILTER(tmp, MAX_PB_SIZE)
    vmadd.w        vr14,   vr11,    vr17
    vmadd.w        vr14,   vr12,    vr18
    vmadd.w        vr14,   vr13,    vr19
    vaddi.wu       vr10,   vr11,    0    //back up previous value
    vaddi.wu       vr11,   vr12,    0
    vaddi.wu       vr12,   vr13,    0
    vsrai.w        vr14,   vr14,    6    // >> 6
    vmul.w         vr14,   vr14,    vr1  // * wx
    vadd.w         vr14,   vr14,    vr2  // + offset
    vsra.w         vr14,   vr14,    vr3  // >> shift
    vadd.w         vr14,   vr14,    vr4  // + ox
    vssrani.h.w    vr14,   vr14,    0
    vssrani.bu.h   vr14,   vr14,    0    // clip
    fst.s          f14,    a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_HV4_\w
.endm

/*
 * void FUNC(put_hevc_epel_uni_w_hv)(uint8_t *_dst, ptrdiff_t _dststride,
 *                                   const uint8_t *_src, ptrdiff_t _srcstride,
 *                                   int height, int denom, int wx, int ox,
 *                                   intptr_t mx, intptr_t my, int width)
 */
function ff_hevc_put_hevc_epel_uni_w_hv4_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    vreplvei.w     vr5,    vr5,     0
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    vreplvei.w     vr16,   vr6,     0
    vreplvei.w     vr17,   vr6,     1
    vreplvei.w     vr18,   vr6,     2
    vreplvei.w     vr19,   vr6,     3
    la.local       t1,     shufb
    vld            vr0,    t1,      0
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    PUT_HEVC_EPEL_UNI_W_HV4_LSX 4
endfunc

.macro PUT_HEVC_EPEL_UNI_W_HV8_LSX w
    vld            vr7,    a2,      0  // start to load src
    vldx           vr8,    a2,      a3
    alsl.d         a2,     a3,      a2,    1
    vld            vr9,    a2,      0
    vshuf.b        vr10,   vr7,     vr7,   vr0 // 0123 1234 2345 3456
    vshuf.b        vr11,   vr8,     vr8,   vr0
    vshuf.b        vr12,   vr9,     vr9,   vr0
    vshuf.b        vr7,    vr7,     vr7,   vr22// 4567 5678 6789 78910
    vshuf.b        vr8,    vr8,     vr8,   vr22
    vshuf.b        vr9,    vr9,     vr9,   vr22
    vdp2.h.bu.b    vr13,   vr10,    vr5  // EPEL_FILTER(src, 1)
    vdp2.h.bu.b    vr14,   vr11,    vr5
    vdp2.h.bu.b    vr15,   vr12,    vr5
    vdp2.h.bu.b    vr23,   vr7,     vr5
    vdp2.h.bu.b    vr20,   vr8,     vr5
    vdp2.h.bu.b    vr21,   vr9,     vr5
    vhaddw.w.h     vr7,    vr13,    vr13
    vhaddw.w.h     vr8,    vr14,    vr14
    vhaddw.w.h     vr9,    vr15,    vr15
    vhaddw.w.h     vr10,   vr23,    vr23
    vhaddw.w.h     vr11,   vr20,    vr20
    vhaddw.w.h     vr12,   vr21,    vr21
.LOOP_HV8_HORI_\w:
    add.d          a2,     a2,      a3
    vld            vr15,   a2,      0
    vshuf.b        vr23,   vr15,    vr15,   vr0
    vshuf.b        vr15,   vr15,    vr15,   vr22
    vdp2.h.bu.b    vr13,   vr23,    vr5
    vdp2.h.bu.b    vr14,   vr15,    vr5
    vhaddw.w.h     vr13,   vr13,    vr13 //789--13
    vhaddw.w.h     vr14,   vr14,    vr14 //101112--14
    vmul.w         vr15,   vr7,     vr16 //EPEL_FILTER(tmp, MAX_PB_SIZE)
    vmadd.w        vr15,   vr8,     vr17
    vmadd.w        vr15,   vr9,     vr18
    vmadd.w        vr15,   vr13,    vr19
    vmul.w         vr20,   vr10,    vr16
    vmadd.w        vr20,   vr11,    vr17
    vmadd.w        vr20,   vr12,    vr18
    vmadd.w        vr20,   vr14,    vr19
    vaddi.wu       vr7,    vr8,     0    //back up previous value
    vaddi.wu       vr8,    vr9,     0
    vaddi.wu       vr9,    vr13,    0
    vaddi.wu       vr10,   vr11,    0
    vaddi.wu       vr11,   vr12,    0
    vaddi.wu       vr12,   vr14,    0
    vsrai.w        vr15,   vr15,    6    // >> 6
    vsrai.w        vr20,   vr20,    6
    vmul.w         vr15,   vr15,    vr1  // * wx
    vmul.w         vr20,   vr20,    vr1
    vadd.w         vr15,   vr15,    vr2  // + offset
    vadd.w         vr20,   vr20,    vr2
    vsra.w         vr15,   vr15,    vr3  // >> shift
    vsra.w         vr20,   vr20,    vr3
    vadd.w         vr15,   vr15,    vr4  // + ox
    vadd.w         vr20,   vr20,    vr4
    vssrani.h.w    vr20,   vr15,    0
    vssrani.bu.h   vr20,   vr20,    0
.if \w > 6
    fst.d          f20,    a0,      0
.else
    fst.s          f20,    a0,      0
    vstelm.h       vr20,   a0,      4,   2
.endif
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_HV8_HORI_\w
.endm

.macro PUT_HEVC_EPEL_UNI_W_HV8_LASX w
    vld            vr7,    a2,      0  // start to load src
    vldx           vr8,    a2,      a3
    alsl.d         a2,     a3,      a2,    1
    vld            vr9,    a2,      0
    xvreplve0.q    xr7,    xr7
    xvreplve0.q    xr8,    xr8
    xvreplve0.q    xr9,    xr9
    xvshuf.b       xr10,   xr7,     xr7,   xr0 // 0123 1234 2345 3456
    xvshuf.b       xr11,   xr8,     xr8,   xr0
    xvshuf.b       xr12,   xr9,     xr9,   xr0
    xvdp2.h.bu.b   xr13,   xr10,    xr5  // EPEL_FILTER(src, 1)
    xvdp2.h.bu.b   xr14,   xr11,    xr5
    xvdp2.h.bu.b   xr15,   xr12,    xr5
    xvhaddw.w.h    xr7,    xr13,    xr13
    xvhaddw.w.h    xr8,    xr14,    xr14
    xvhaddw.w.h    xr9,    xr15,    xr15
.LOOP_HV8_HORI_LASX_\w:
    add.d          a2,     a2,      a3
    vld            vr15,   a2,      0
    xvreplve0.q    xr15,   xr15
    xvshuf.b       xr23,   xr15,    xr15,   xr0
    xvdp2.h.bu.b   xr10,   xr23,    xr5
    xvhaddw.w.h    xr10,   xr10,    xr10
    xvmul.w        xr15,   xr7,     xr16 //EPEL_FILTER(tmp, MAX_PB_SIZE)
    xvmadd.w       xr15,   xr8,     xr17
    xvmadd.w       xr15,   xr9,     xr18
    xvmadd.w       xr15,   xr10,    xr19
    xvaddi.wu      xr7,    xr8,     0    //back up previous value
    xvaddi.wu      xr8,    xr9,     0
    xvaddi.wu      xr9,    xr10,    0
    xvsrai.w       xr15,   xr15,    6    // >> 6
    xvmul.w        xr15,   xr15,    xr1  // * wx
    xvadd.w        xr15,   xr15,    xr2  // + offset
    xvsra.w        xr15,   xr15,    xr3  // >> shift
    xvadd.w        xr15,   xr15,    xr4  // + ox
    xvpermi.q      xr20,   xr15,    0x01
    vssrani.h.w    vr20,   vr15,    0
    vssrani.bu.h   vr20,   vr20,    0
.if \w > 6
    fst.d          f20,    a0,      0
.else
    fst.s          f20,    a0,      0
    vstelm.h       vr20,   a0,      4,   2
.endif
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_HV8_HORI_LASX_\w
.endm

.macro PUT_HEVC_EPEL_UNI_W_HV16_LASX w
    xvld           xr7,    a2,      0  // start to load src
    xvldx          xr8,    a2,      a3
    alsl.d         a2,     a3,      a2,    1
    xvld           xr9,    a2,      0
    xvpermi.d      xr10,   xr7,     0x09 //8..18
    xvpermi.d      xr11,   xr8,     0x09
    xvpermi.d      xr12,   xr9,     0x09
    xvreplve0.q    xr7,    xr7
    xvreplve0.q    xr8,    xr8
    xvreplve0.q    xr9,    xr9
    xvshuf.b       xr13,   xr7,     xr7,   xr0 // 0123 1234 2345 3456
    xvshuf.b       xr14,   xr8,     xr8,   xr0
    xvshuf.b       xr15,   xr9,     xr9,   xr0
    xvdp2.h.bu.b   xr20,   xr13,    xr5  // EPEL_FILTER(src, 1)
    xvdp2.h.bu.b   xr21,   xr14,    xr5
    xvdp2.h.bu.b   xr22,   xr15,    xr5
    xvhaddw.w.h    xr7,    xr20,    xr20
    xvhaddw.w.h    xr8,    xr21,    xr21
    xvhaddw.w.h    xr9,    xr22,    xr22
    xvreplve0.q    xr10,   xr10
    xvreplve0.q    xr11,   xr11
    xvreplve0.q    xr12,   xr12
    xvshuf.b       xr13,   xr10,    xr10,  xr0
    xvshuf.b       xr14,   xr11,    xr11,  xr0
    xvshuf.b       xr15,   xr12,    xr12,  xr0
    xvdp2.h.bu.b   xr20,   xr13,    xr5
    xvdp2.h.bu.b   xr21,   xr14,    xr5
    xvdp2.h.bu.b   xr22,   xr15,    xr5
    xvhaddw.w.h    xr10,   xr20,    xr20
    xvhaddw.w.h    xr11,   xr21,    xr21
    xvhaddw.w.h    xr12,   xr22,    xr22
.LOOP_HV16_HORI_LASX_\w:
    add.d          a2,     a2,      a3
    xvld           xr15,   a2,      0
    xvpermi.d      xr20,   xr15,    0x09 //8...18
    xvreplve0.q    xr15,   xr15
    xvreplve0.q    xr20,   xr20
    xvshuf.b       xr21,   xr15,    xr15,   xr0
    xvshuf.b       xr22,   xr20,    xr20,   xr0
    xvdp2.h.bu.b   xr13,   xr21,    xr5
    xvdp2.h.bu.b   xr14,   xr22,    xr5
    xvhaddw.w.h    xr13,   xr13,    xr13
    xvhaddw.w.h    xr14,   xr14,    xr14
    xvmul.w        xr15,   xr7,     xr16 //EPEL_FILTER(tmp, MAX_PB_SIZE)
    xvmadd.w       xr15,   xr8,     xr17
    xvmadd.w       xr15,   xr9,     xr18
    xvmadd.w       xr15,   xr13,    xr19
    xvmul.w        xr20,   xr10,    xr16
    xvmadd.w       xr20,   xr11,    xr17
    xvmadd.w       xr20,   xr12,    xr18
    xvmadd.w       xr20,   xr14,    xr19
    xvaddi.wu      xr7,    xr8,     0    //back up previous value
    xvaddi.wu      xr8,    xr9,     0
    xvaddi.wu      xr9,    xr13,    0
    xvaddi.wu      xr10,   xr11,    0
    xvaddi.wu      xr11,   xr12,    0
    xvaddi.wu      xr12,   xr14,    0
    xvsrai.w       xr15,   xr15,    6    // >> 6
    xvsrai.w       xr20,   xr20,    6    // >> 6
    xvmul.w        xr15,   xr15,    xr1  // * wx
    xvmul.w        xr20,   xr20,    xr1  // * wx
    xvadd.w        xr15,   xr15,    xr2  // + offset
    xvadd.w        xr20,   xr20,    xr2  // + offset
    xvsra.w        xr15,   xr15,    xr3  // >> shift
    xvsra.w        xr20,   xr20,    xr3  // >> shift
    xvadd.w        xr15,   xr15,    xr4  // + ox
    xvadd.w        xr20,   xr20,    xr4  // + ox
    xvssrani.h.w   xr20,   xr15,    0
    xvpermi.q      xr21,   xr20,    0x01
    vssrani.bu.h   vr21,   vr20,    0
    vpermi.w       vr21,   vr21,    0xd8
.if \w < 16
    fst.d          f21,    a0,      0
    vstelm.w       vr21,   a0,      8,   2
.else
    vst            vr21,   a0,      0
.endif
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_HV16_HORI_LASX_\w
.endm

function ff_hevc_put_hevc_epel_uni_w_hv6_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    vreplvei.w     vr5,    vr5,     0
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    vreplvei.w     vr16,   vr6,     0
    vreplvei.w     vr17,   vr6,     1
    vreplvei.w     vr18,   vr6,     2
    vreplvei.w     vr19,   vr6,     3
    la.local       t1,     shufb
    vld            vr0,    t1,      0
    vaddi.bu       vr22,   vr0,     4  // update shufb to get high part
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    PUT_HEVC_EPEL_UNI_W_HV8_LSX 6
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv6_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    xvreplve0.w    xr5,    xr5
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    xvreplve0.q    xr6,    xr6
    xvrepl128vei.w xr16,   xr6,     0
    xvrepl128vei.w xr17,   xr6,     1
    xvrepl128vei.w xr18,   xr6,     2
    xvrepl128vei.w xr19,   xr6,     3
    la.local       t1,     shufb
    xvld           xr0,    t1,      0
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    PUT_HEVC_EPEL_UNI_W_HV8_LASX 6
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv8_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    vreplvei.w     vr5,    vr5,     0
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    vreplvei.w     vr16,   vr6,     0
    vreplvei.w     vr17,   vr6,     1
    vreplvei.w     vr18,   vr6,     2
    vreplvei.w     vr19,   vr6,     3
    la.local       t1,     shufb
    vld            vr0,    t1,      0
    vaddi.bu       vr22,   vr0,     4  // update shufb to get high part
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    PUT_HEVC_EPEL_UNI_W_HV8_LSX 8
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv8_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    xvreplve0.w    xr5,    xr5
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    xvreplve0.q    xr6,    xr6
    xvrepl128vei.w xr16,   xr6,     0
    xvrepl128vei.w xr17,   xr6,     1
    xvrepl128vei.w xr18,   xr6,     2
    xvrepl128vei.w xr19,   xr6,     3
    la.local       t1,     shufb
    xvld           xr0,    t1,      0
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    PUT_HEVC_EPEL_UNI_W_HV8_LASX 8
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv12_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    vreplvei.w     vr5,    vr5,     0
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    vreplvei.w     vr16,   vr6,     0
    vreplvei.w     vr17,   vr6,     1
    vreplvei.w     vr18,   vr6,     2
    vreplvei.w     vr19,   vr6,     3
    la.local       t1,     shufb
    vld            vr0,    t1,      0
    vaddi.bu       vr22,   vr0,     4  // update shufb to get high part
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    PUT_HEVC_EPEL_UNI_W_HV8_LSX 12
    addi.d         a0,     t2,      8
    addi.d         a2,     t3,      8
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_HV4_LSX 12
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv12_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    xvreplve0.w    xr5,    xr5
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    xvreplve0.q    xr6,    xr6
    xvrepl128vei.w xr16,   xr6,     0
    xvrepl128vei.w xr17,   xr6,     1
    xvrepl128vei.w xr18,   xr6,     2
    xvrepl128vei.w xr19,   xr6,     3
    la.local       t1,     shufb
    xvld           xr0,    t1,      0
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    PUT_HEVC_EPEL_UNI_W_HV16_LASX 12
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv16_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    vreplvei.w     vr5,    vr5,     0
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    vreplvei.w     vr16,   vr6,     0
    vreplvei.w     vr17,   vr6,     1
    vreplvei.w     vr18,   vr6,     2
    vreplvei.w     vr19,   vr6,     3
    la.local       t1,     shufb
    vld            vr0,    t1,      0
    vaddi.bu       vr22,   vr0,     4  // update shufb to get high part
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    addi.d         t5,     zero,    2
.LOOP_HV16:
    PUT_HEVC_EPEL_UNI_W_HV8_LSX 16
    addi.d         a0,     t2,      8
    addi.d         a2,     t3,      8
    addi.d         a4,     t4,      0
    addi.d         t5,     t5,      -1
    bnez           t5,     .LOOP_HV16
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv16_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    xvreplve0.w    xr5,    xr5
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    xvreplve0.q    xr6,    xr6
    xvrepl128vei.w xr16,   xr6,     0
    xvrepl128vei.w xr17,   xr6,     1
    xvrepl128vei.w xr18,   xr6,     2
    xvrepl128vei.w xr19,   xr6,     3
    la.local       t1,     shufb
    xvld           xr0,    t1,      0
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    PUT_HEVC_EPEL_UNI_W_HV16_LASX 16
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv24_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    vreplvei.w     vr5,    vr5,     0
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    vreplvei.w     vr16,   vr6,     0
    vreplvei.w     vr17,   vr6,     1
    vreplvei.w     vr18,   vr6,     2
    vreplvei.w     vr19,   vr6,     3
    la.local       t1,     shufb
    vld            vr0,    t1,      0
    vaddi.bu       vr22,   vr0,     4  // update shufb to get high part
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    addi.d         t5,     zero,    3
.LOOP_HV24:
    PUT_HEVC_EPEL_UNI_W_HV8_LSX 24
    addi.d         a0,     t2,      8
    addi.d         t2,     t2,      8
    addi.d         a2,     t3,      8
    addi.d         t3,     t3,      8
    addi.d         a4,     t4,      0
    addi.d         t5,     t5,      -1
    bnez           t5,     .LOOP_HV24
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv24_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    xvreplve0.w    xr5,    xr5
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    xvreplve0.q    xr6,    xr6
    xvrepl128vei.w xr16,   xr6,     0
    xvrepl128vei.w xr17,   xr6,     1
    xvrepl128vei.w xr18,   xr6,     2
    xvrepl128vei.w xr19,   xr6,     3
    la.local       t1,     shufb
    xvld           xr0,    t1,      0
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    PUT_HEVC_EPEL_UNI_W_HV16_LASX 24
    addi.d         a0,     t2,      16
    addi.d         a2,     t3,      16
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_HV8_LASX 24
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv32_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    vreplvei.w     vr5,    vr5,     0
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    vreplvei.w     vr16,   vr6,     0
    vreplvei.w     vr17,   vr6,     1
    vreplvei.w     vr18,   vr6,     2
    vreplvei.w     vr19,   vr6,     3
    la.local       t1,     shufb
    vld            vr0,    t1,      0
    vaddi.bu       vr22,   vr0,     4  // update shufb to get high part
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    addi.d         t5,     zero,    4
.LOOP_HV32:
    PUT_HEVC_EPEL_UNI_W_HV8_LSX 32
    addi.d         a0,     t2,      8
    addi.d         t2,     t2,      8
    addi.d         a2,     t3,      8
    addi.d         t3,     t3,      8
    addi.d         a4,     t4,      0
    addi.d         t5,     t5,      -1
    bnez           t5,     .LOOP_HV32
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv32_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    xvreplve0.w    xr5,    xr5
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    xvreplve0.q    xr6,    xr6
    xvrepl128vei.w xr16,   xr6,     0
    xvrepl128vei.w xr17,   xr6,     1
    xvrepl128vei.w xr18,   xr6,     2
    xvrepl128vei.w xr19,   xr6,     3
    la.local       t1,     shufb
    xvld           xr0,    t1,      0
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    addi.d         t5,     zero,    2
.LOOP_HV32_LASX:
    PUT_HEVC_EPEL_UNI_W_HV16_LASX 32
    addi.d         a0,     t2,      16
    addi.d         t2,     t2,      16
    addi.d         a2,     t3,      16
    addi.d         t3,     t3,      16
    addi.d         a4,     t4,      0
    addi.d         t5,     t5,      -1
    bnez           t5,     .LOOP_HV32_LASX
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv48_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    vreplvei.w     vr5,    vr5,     0
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    vreplvei.w     vr16,   vr6,     0
    vreplvei.w     vr17,   vr6,     1
    vreplvei.w     vr18,   vr6,     2
    vreplvei.w     vr19,   vr6,     3
    la.local       t1,     shufb
    vld            vr0,    t1,      0
    vaddi.bu       vr22,   vr0,     4  // update shufb to get high part
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    addi.d         t5,     zero,    6
.LOOP_HV48:
    PUT_HEVC_EPEL_UNI_W_HV8_LSX 48
    addi.d         a0,     t2,      8
    addi.d         t2,     t2,      8
    addi.d         a2,     t3,      8
    addi.d         t3,     t3,      8
    addi.d         a4,     t4,      0
    addi.d         t5,     t5,      -1
    bnez           t5,     .LOOP_HV48
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv48_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    xvreplve0.w    xr5,    xr5
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    xvreplve0.q    xr6,    xr6
    xvrepl128vei.w xr16,   xr6,     0
    xvrepl128vei.w xr17,   xr6,     1
    xvrepl128vei.w xr18,   xr6,     2
    xvrepl128vei.w xr19,   xr6,     3
    la.local       t1,     shufb
    xvld           xr0,    t1,      0
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    addi.d         t5,     zero,    3
.LOOP_HV48_LASX:
    PUT_HEVC_EPEL_UNI_W_HV16_LASX 48
    addi.d         a0,     t2,      16
    addi.d         t2,     t2,      16
    addi.d         a2,     t3,      16
    addi.d         t3,     t3,      16
    addi.d         a4,     t4,      0
    addi.d         t5,     t5,      -1
    bnez           t5,     .LOOP_HV48_LASX
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv64_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    vreplvei.w     vr5,    vr5,     0
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    vreplvei.w     vr16,   vr6,     0
    vreplvei.w     vr17,   vr6,     1
    vreplvei.w     vr18,   vr6,     2
    vreplvei.w     vr19,   vr6,     3
    la.local       t1,     shufb
    vld            vr0,    t1,      0
    vaddi.bu       vr22,   vr0,     4  // update shufb to get high part
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    addi.d         t5,     zero,    8
.LOOP_HV64:
    PUT_HEVC_EPEL_UNI_W_HV8_LSX 64
    addi.d         a0,     t2,      8
    addi.d         t2,     t2,      8
    addi.d         a2,     t3,      8
    addi.d         t3,     t3,      8
    addi.d         a4,     t4,      0
    addi.d         t5,     t5,      -1
    bnez           t5,     .LOOP_HV64
endfunc

function ff_hevc_put_hevc_epel_uni_w_hv64_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  // mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr5,    t1,      t0 // ff_hevc_epel_filters[mx];
    xvreplve0.w    xr5,    xr5
    ld.d           t0,     sp,      8  // my
    slli.w         t0,     t0,      2
    vldx           vr6,    t1,      t0 // ff_hevc_epel_filters[my];
    vsllwil.h.b    vr6,    vr6,     0
    vsllwil.w.h    vr6,    vr6,     0
    xvreplve0.q    xr6,    xr6
    xvrepl128vei.w xr16,   xr6,     0
    xvrepl128vei.w xr17,   xr6,     1
    xvrepl128vei.w xr18,   xr6,     2
    xvrepl128vei.w xr19,   xr6,     3
    la.local       t1,     shufb
    xvld           xr0,    t1,      0
    sub.d          a2,     a2,      a3 // src -= srcstride
    addi.d         a2,     a2,      -1
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    addi.d         t5,     zero,    4
.LOOP_HV64_LASX:
    PUT_HEVC_EPEL_UNI_W_HV16_LASX 64
    addi.d         a0,     t2,      16
    addi.d         t2,     t2,      16
    addi.d         a2,     t3,      16
    addi.d         t3,     t3,      16
    addi.d         a4,     t4,      0
    addi.d         t5,     t5,      -1
    bnez           t5,     .LOOP_HV64_LASX
endfunc

/*
 * void FUNC(put_hevc_qpel_uni_h)(uint8_t *_dst, ptrdiff_t _dststride,
 *                                const uint8_t *_src, ptrdiff_t _srcstride,
 *                                int height, intptr_t mx, intptr_t my,
 *                                int width)
 */
function ff_hevc_put_hevc_uni_qpel_h4_8_lsx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr5,    t1,      t0 //filter
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    vreplgr2vr.h   vr1,    t1
    la.local       t1,     shufb
    vld            vr2,    t1,      32 //mask0 0 1
    vaddi.bu       vr3,    vr2,     2  //mask1 2 3
.LOOP_UNI_H4:
    vld            vr18,   a2,      0
    vldx           vr19,   a2,      a3
    alsl.d         a2,     a3,      a2,   1
    vshuf.b        vr6,    vr18,    vr18,   vr2
    vshuf.b        vr7,    vr18,    vr18,   vr3
    vshuf.b        vr8,    vr19,    vr19,   vr2
    vshuf.b        vr9,    vr19,    vr19,   vr3
    vdp2.h.bu.b    vr10,   vr6,     vr5
    vdp2.h.bu.b    vr11,   vr7,     vr5
    vdp2.h.bu.b    vr12,   vr8,     vr5
    vdp2.h.bu.b    vr13,   vr9,     vr5
    vhaddw.d.h     vr10
    vhaddw.d.h     vr11
    vhaddw.d.h     vr12
    vhaddw.d.h     vr13
    vpickev.w      vr10,   vr11,    vr10
    vpickev.w      vr11,   vr13,    vr12
    vpickev.h      vr10,   vr11,    vr10
    vadd.h         vr10,   vr10,    vr1
    vsrai.h        vr10,   vr10,    6
    vssrani.bu.h   vr10,   vr10,    0
    fst.s          f10,    a0,      0
    vbsrl.v        vr10,   vr10,    4
    fstx.s         f10,    a0,      a1
    alsl.d         a0,     a1,      a0,   1
    addi.d         a4,     a4,      -2
    bnez           a4,     .LOOP_UNI_H4
endfunc

.macro HEVC_UNI_QPEL_H8_LSX in0, out0
    vshuf.b        vr10,   \in0,    \in0,   vr5
    vshuf.b        vr11,   \in0,    \in0,   vr6
    vshuf.b        vr12,   \in0,    \in0,   vr7
    vshuf.b        vr13,   \in0,    \in0,   vr8
    vdp2.h.bu.b    \out0,  vr10,    vr0 //(QPEL_FILTER(src, 1)
    vdp2add.h.bu.b \out0,  vr11,    vr1
    vdp2add.h.bu.b \out0,  vr12,    vr2
    vdp2add.h.bu.b \out0,  vr13,    vr3
    vadd.h         \out0,  \out0,   vr4
    vsrai.h        \out0,  \out0,   6
.endm

.macro HEVC_UNI_QPEL_H16_LASX in0, out0
    xvshuf.b        xr10,   \in0,   \in0,   xr5
    xvshuf.b        xr11,   \in0,   \in0,   xr6
    xvshuf.b        xr12,   \in0,   \in0,   xr7
    xvshuf.b        xr13,   \in0,   \in0,   xr8
    xvdp2.h.bu.b    \out0,  xr10,   xr0 //(QPEL_FILTER(src, 1)
    xvdp2add.h.bu.b \out0,  xr11,   xr1
    xvdp2add.h.bu.b \out0,  xr12,   xr2
    xvdp2add.h.bu.b \out0,  xr13,   xr3
    xvadd.h         \out0,  \out0,  xr4
    xvsrai.h        \out0,  \out0,  6
.endm

function ff_hevc_put_hevc_uni_qpel_h6_8_lsx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    vreplvei.h     vr1,    vr0,     1  //cd...
    vreplvei.h     vr2,    vr0,     2  //ef...
    vreplvei.h     vr3,    vr0,     3  //gh...
    vreplvei.h     vr0,    vr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    vreplgr2vr.h   vr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    vaddi.bu       vr6,    vr5,     2
    vaddi.bu       vr7,    vr5,     4
    vaddi.bu       vr8,    vr5,     6
.LOOP_UNI_H6:
    vld            vr9,    a2,      0
    add.d          a2,     a2,      a3
    HEVC_UNI_QPEL_H8_LSX vr9, vr14
    vssrani.bu.h   vr14,   vr14,    0
    fst.s          f14,    a0,      0
    vstelm.h       vr14,   a0,      4,   2
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H6
endfunc

function ff_hevc_put_hevc_uni_qpel_h8_8_lsx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    vreplvei.h     vr1,    vr0,     1  //cd...
    vreplvei.h     vr2,    vr0,     2  //ef...
    vreplvei.h     vr3,    vr0,     3  //gh...
    vreplvei.h     vr0,    vr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    vreplgr2vr.h   vr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    vaddi.bu       vr6,    vr5,     2
    vaddi.bu       vr7,    vr5,     4
    vaddi.bu       vr8,    vr5,     6
.LOOP_UNI_H8:
    vld            vr9,    a2,      0
    add.d          a2,     a2,      a3
    HEVC_UNI_QPEL_H8_LSX vr9, vr14
    vssrani.bu.h   vr14,   vr14,    0
    fst.d          f14,    a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H8
endfunc

function ff_hevc_put_hevc_uni_qpel_h12_8_lsx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    vreplvei.h     vr1,    vr0,     1  //cd...
    vreplvei.h     vr2,    vr0,     2  //ef...
    vreplvei.h     vr3,    vr0,     3  //gh...
    vreplvei.h     vr0,    vr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    vreplgr2vr.h   vr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    vaddi.bu       vr6,    vr5,     2
    vaddi.bu       vr7,    vr5,     4
    vaddi.bu       vr8,    vr5,     6
.LOOP_UNI_H12:
    vld            vr9,    a2,      0
    HEVC_UNI_QPEL_H8_LSX vr9, vr14
    vld            vr9,    a2,      8
    add.d          a2,     a2,      a3
    HEVC_UNI_QPEL_H8_LSX vr9, vr15
    vssrani.bu.h   vr15,   vr14,    0
    fst.d          f15,    a0,      0
    vstelm.w       vr15,   a0,      8,   2
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H12
endfunc

function ff_hevc_put_hevc_uni_qpel_h12_8_lasx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    xvreplve0.q    xr0,    xr0
    xvrepl128vei.h xr1,    xr0,     1  //cd...
    xvrepl128vei.h xr2,    xr0,     2  //ef...
    xvrepl128vei.h xr3,    xr0,     3  //gh...
    xvrepl128vei.h xr0,    xr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    xvreplgr2vr.h  xr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    xvreplve0.q    xr5,    xr5
    xvaddi.bu      xr6,    xr5,     2
    xvaddi.bu      xr7,    xr5,     4
    xvaddi.bu      xr8,    xr5,     6
.LOOP_UNI_H12_LASX:
    xvld           xr9,    a2,      0
    add.d          a2,     a2,      a3
    xvpermi.d      xr9,    xr9,     0x94 //rearrange data
    HEVC_UNI_QPEL_H16_LASX xr9, xr14
    xvpermi.q      xr15,   xr14,    0x01
    vssrani.bu.h   vr15,   vr14,    0
    fst.d          f15,    a0,      0
    vstelm.w       vr15,   a0,      8,   2
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H12_LASX
endfunc

function ff_hevc_put_hevc_uni_qpel_h16_8_lsx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    vreplvei.h     vr1,    vr0,     1  //cd...
    vreplvei.h     vr2,    vr0,     2  //ef...
    vreplvei.h     vr3,    vr0,     3  //gh...
    vreplvei.h     vr0,    vr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    vreplgr2vr.h   vr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    vaddi.bu       vr6,    vr5,     2
    vaddi.bu       vr7,    vr5,     4
    vaddi.bu       vr8,    vr5,     6
.LOOP_UNI_H16:
    vld            vr9,    a2,      0
    HEVC_UNI_QPEL_H8_LSX vr9, vr14
    vld            vr9,    a2,      8
    add.d          a2,     a2,      a3
    HEVC_UNI_QPEL_H8_LSX vr9, vr15
    vssrani.bu.h   vr15,   vr14,    0
    vst            vr15,   a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H16
endfunc

function ff_hevc_put_hevc_uni_qpel_h16_8_lasx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    xvreplve0.q    xr0,    xr0
    xvrepl128vei.h xr1,    xr0,     1  //cd...
    xvrepl128vei.h xr2,    xr0,     2  //ef...
    xvrepl128vei.h xr3,    xr0,     3  //gh...
    xvrepl128vei.h xr0,    xr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    xvreplgr2vr.h  xr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    xvreplve0.q    xr5,    xr5
    xvaddi.bu      xr6,    xr5,     2
    xvaddi.bu      xr7,    xr5,     4
    xvaddi.bu      xr8,    xr5,     6
.LOOP_UNI_H16_LASX:
    xvld           xr9,    a2,      0
    add.d          a2,     a2,      a3
    xvpermi.d      xr9,    xr9,     0x94 //rearrange data
    HEVC_UNI_QPEL_H16_LASX xr9, xr14
    xvpermi.q      xr15,   xr14,    0x01
    vssrani.bu.h   vr15,   vr14,    0
    vst            vr15,   a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H16_LASX
endfunc

function ff_hevc_put_hevc_uni_qpel_h24_8_lsx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    vreplvei.h     vr1,    vr0,     1  //cd...
    vreplvei.h     vr2,    vr0,     2  //ef...
    vreplvei.h     vr3,    vr0,     3  //gh...
    vreplvei.h     vr0,    vr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    vreplgr2vr.h   vr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    vaddi.bu       vr6,    vr5,     2
    vaddi.bu       vr7,    vr5,     4
    vaddi.bu       vr8,    vr5,     6
.LOOP_UNI_H24:
    vld            vr9,    a2,      0
    HEVC_UNI_QPEL_H8_LSX vr9, vr14
    vld            vr9,    a2,      8
    HEVC_UNI_QPEL_H8_LSX vr9, vr15
    vld            vr9,    a2,      16
    add.d          a2,     a2,      a3
    HEVC_UNI_QPEL_H8_LSX vr9, vr16
    vssrani.bu.h   vr15,   vr14,    0
    vssrani.bu.h   vr16,   vr16,    0
    vst            vr15,   a0,      0
    fst.d          f16,    a0,      16
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H24
endfunc

function ff_hevc_put_hevc_uni_qpel_h24_8_lasx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    xvreplve0.q    xr0,    xr0
    xvrepl128vei.h xr1,    xr0,     1  //cd...
    xvrepl128vei.h xr2,    xr0,     2  //ef...
    xvrepl128vei.h xr3,    xr0,     3  //gh...
    xvrepl128vei.h xr0,    xr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    xvreplgr2vr.h  xr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    xvreplve0.q    xr5,    xr5
    xvaddi.bu      xr6,    xr5,     2
    xvaddi.bu      xr7,    xr5,     4
    xvaddi.bu      xr8,    xr5,     6
.LOOP_UNI_H24_LASX:
    xvld           xr9,    a2,      0
    xvpermi.q      xr19,   xr9,     0x01 //16...23
    add.d          a2,     a2,      a3
    xvpermi.d      xr9,    xr9,     0x94 //rearrange data
    HEVC_UNI_QPEL_H16_LASX xr9, xr14
    xvpermi.q      xr15,   xr14,    0x01
    vssrani.bu.h   vr15,   vr14,    0
    vst            vr15,   a0,      0
    HEVC_UNI_QPEL_H8_LSX vr19, vr16
    vssrani.bu.h   vr16,   vr16,    0
    fst.d          f16,    a0,      16
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H24_LASX
endfunc

function ff_hevc_put_hevc_uni_qpel_h32_8_lsx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    vreplvei.h     vr1,    vr0,     1  //cd...
    vreplvei.h     vr2,    vr0,     2  //ef...
    vreplvei.h     vr3,    vr0,     3  //gh...
    vreplvei.h     vr0,    vr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    vreplgr2vr.h   vr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    vaddi.bu       vr6,    vr5,     2
    vaddi.bu       vr7,    vr5,     4
    vaddi.bu       vr8,    vr5,     6
.LOOP_UNI_H32:
    vld            vr9,    a2,      0
    HEVC_UNI_QPEL_H8_LSX vr9, vr14
    vld            vr9,    a2,      8
    HEVC_UNI_QPEL_H8_LSX vr9, vr15
    vld            vr9,    a2,      16
    HEVC_UNI_QPEL_H8_LSX vr9, vr16
    vld            vr9,    a2,      24
    add.d          a2,     a2,      a3
    HEVC_UNI_QPEL_H8_LSX vr9, vr17
    vssrani.bu.h   vr15,   vr14,    0
    vssrani.bu.h   vr17,   vr16,    0
    vst            vr15,   a0,      0
    vst            vr17,   a0,      16
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H32
endfunc

function ff_hevc_put_hevc_uni_qpel_h32_8_lasx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    xvreplve0.q    xr0,    xr0
    xvrepl128vei.h xr1,    xr0,     1  //cd...
    xvrepl128vei.h xr2,    xr0,     2  //ef...
    xvrepl128vei.h xr3,    xr0,     3  //gh...
    xvrepl128vei.h xr0,    xr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    xvreplgr2vr.h  xr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    xvreplve0.q    xr5,    xr5
    xvaddi.bu      xr6,    xr5,     2
    xvaddi.bu      xr7,    xr5,     4
    xvaddi.bu      xr8,    xr5,     6
.LOOP_UNI_H32_LASX:
    xvld           xr9,    a2,      0
    xvpermi.d      xr9,    xr9,     0x94
    HEVC_UNI_QPEL_H16_LASX xr9, xr14
    xvld           xr9,    a2,      16
    xvpermi.d      xr9,    xr9,     0x94
    HEVC_UNI_QPEL_H16_LASX xr9, xr15
    add.d          a2,     a2,      a3
    xvssrani.bu.h  xr15,   xr14,    0
    xvpermi.d      xr15,   xr15,    0xd8
    xvst           xr15,   a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H32_LASX
endfunc

function ff_hevc_put_hevc_uni_qpel_h48_8_lsx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    vreplvei.h     vr1,    vr0,     1  //cd...
    vreplvei.h     vr2,    vr0,     2  //ef...
    vreplvei.h     vr3,    vr0,     3  //gh...
    vreplvei.h     vr0,    vr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    vreplgr2vr.h   vr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    vaddi.bu       vr6,    vr5,     2
    vaddi.bu       vr7,    vr5,     4
    vaddi.bu       vr8,    vr5,     6
.LOOP_UNI_H48:
    vld            vr9,    a2,      0
    HEVC_UNI_QPEL_H8_LSX vr9, vr14
    vld            vr9,    a2,      8
    HEVC_UNI_QPEL_H8_LSX vr9, vr15
    vld            vr9,    a2,      16
    HEVC_UNI_QPEL_H8_LSX vr9, vr16
    vld            vr9,    a2,      24
    HEVC_UNI_QPEL_H8_LSX vr9, vr17
    vld            vr9,    a2,      32
    HEVC_UNI_QPEL_H8_LSX vr9, vr18
    vld            vr9,    a2,      40
    add.d          a2,     a2,      a3
    HEVC_UNI_QPEL_H8_LSX vr9, vr19
    vssrani.bu.h   vr15,   vr14,    0
    vssrani.bu.h   vr17,   vr16,    0
    vssrani.bu.h   vr19,   vr18,    0
    vst            vr15,   a0,      0
    vst            vr17,   a0,      16
    vst            vr19,   a0,      32
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H48
endfunc

function ff_hevc_put_hevc_uni_qpel_h48_8_lasx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    xvreplve0.q    xr0,    xr0
    xvrepl128vei.h xr1,    xr0,     1  //cd...
    xvrepl128vei.h xr2,    xr0,     2  //ef...
    xvrepl128vei.h xr3,    xr0,     3  //gh...
    xvrepl128vei.h xr0,    xr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    xvreplgr2vr.h  xr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    xvreplve0.q    xr5,    xr5
    xvaddi.bu      xr6,    xr5,     2
    xvaddi.bu      xr7,    xr5,     4
    xvaddi.bu      xr8,    xr5,     6
.LOOP_UNI_H48_LASX:
    xvld           xr9,    a2,      0
    xvpermi.d      xr9,    xr9,     0x94
    HEVC_UNI_QPEL_H16_LASX xr9, xr14
    xvld           xr9,    a2,      16
    xvpermi.d      xr9,    xr9,     0x94
    HEVC_UNI_QPEL_H16_LASX xr9, xr15
    xvld           xr9,    a2,      32
    xvpermi.d      xr9,    xr9,     0x94
    HEVC_UNI_QPEL_H16_LASX xr9, xr16
    add.d          a2,     a2,      a3
    xvssrani.bu.h  xr15,   xr14,    0
    xvpermi.d      xr15,   xr15,    0xd8
    xvst           xr15,   a0,      0
    xvpermi.q      xr17,   xr16,    0x01
    vssrani.bu.h   vr17,   vr16,    0
    vst            vr17,   a0,      32
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H48_LASX
endfunc

function ff_hevc_put_hevc_uni_qpel_h64_8_lasx
    slli.w         t0,     a5,      4
    la.local       t1,     ff_hevc_qpel_filters
    vldx           vr0,    t1,      t0 //filter abcdefgh
    xvreplve0.q    xr0,    xr0
    xvrepl128vei.h xr1,    xr0,     1  //cd...
    xvrepl128vei.h xr2,    xr0,     2  //ef...
    xvrepl128vei.h xr3,    xr0,     3  //gh...
    xvrepl128vei.h xr0,    xr0,     0  //ab...
    addi.d         a2,     a2,      -3 //src -= 3
    addi.w         t1,     zero,    32
    xvreplgr2vr.h  xr4,    t1
    la.local       t1,     shufb
    vld            vr5,    t1,      48
    xvreplve0.q    xr5,    xr5
    xvaddi.bu      xr6,    xr5,     2
    xvaddi.bu      xr7,    xr5,     4
    xvaddi.bu      xr8,    xr5,     6
.LOOP_UNI_H64_LASX:
    xvld           xr9,    a2,      0
    xvpermi.d      xr9,    xr9,     0x94
    HEVC_UNI_QPEL_H16_LASX xr9, xr14
    xvld           xr9,    a2,      16
    xvpermi.d      xr9,    xr9,     0x94
    HEVC_UNI_QPEL_H16_LASX xr9, xr15
    xvld           xr9,    a2,      32
    xvpermi.d      xr9,    xr9,     0x94
    HEVC_UNI_QPEL_H16_LASX xr9, xr16
    xvld           xr9,    a2,      48
    xvpermi.d      xr9,    xr9,     0x94
    HEVC_UNI_QPEL_H16_LASX xr9, xr17
    add.d          a2,     a2,      a3
    xvssrani.bu.h  xr15,   xr14,    0
    xvpermi.d      xr15,   xr15,    0xd8
    xvst           xr15,   a0,      0
    xvssrani.bu.h  xr17,   xr16,    0
    xvpermi.d      xr17,   xr17,    0xd8
    xvst           xr17,   a0,      32
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,     -1
    bnez           a4,     .LOOP_UNI_H64_LASX
endfunc

/*
 * void FUNC(put_hevc_epel_uni_w_v)(uint8_t *_dst, ptrdiff_t _dststride,
 *                                  const uint8_t *_src, ptrdiff_t _srcstride,
 *                                  int height, int denom, int wx, int ox,
 *                                  intptr_t mx, intptr_t my, int width)
 */
function ff_hevc_put_hevc_epel_uni_w_v4_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    fld.s          f6,     a2,      0  //0
    fldx.s         f7,     a2,      a3 //1
    fldx.s         f8,     a2,      t0 //2
    add.d          a2,     a2,      t1
    vilvl.b        vr6,    vr7,     vr6
    vilvl.b        vr7,    vr8,     vr8
    vilvl.h        vr6,    vr7,     vr6
    vreplvei.w     vr0,    vr0,     0
.LOOP_UNI_V4:
    fld.s          f9,     a2,      0  //3
    fldx.s         f10,    a2,      a3 //4
    add.d          a2,     a2,      t0
    vextrins.b     vr6,    vr9,     0x30 //insert the 3th load
    vextrins.b     vr6,    vr9,     0x71
    vextrins.b     vr6,    vr9,     0xb2
    vextrins.b     vr6,    vr9,     0xf3
    vbsrl.v        vr7,    vr6,     1
    vextrins.b     vr7,    vr10,    0x30 //insert the 4th load
    vextrins.b     vr7,    vr10,    0x71
    vextrins.b     vr7,    vr10,    0xb2
    vextrins.b     vr7,    vr10,    0xf3
    vdp2.h.bu.b    vr8,    vr6,     vr0 //EPEL_FILTER(src, stride)
    vdp2.h.bu.b    vr9,    vr7,     vr0
    vhaddw.w.h     vr10,   vr8,     vr8
    vhaddw.w.h     vr11,   vr9,     vr9
    vmulwev.w.h    vr10,   vr10,    vr1 //EPEL_FILTER(src, stride) * wx
    vmulwev.w.h    vr11,   vr11,    vr1
    vadd.w         vr10,   vr10,    vr2 // + offset
    vadd.w         vr11,   vr11,    vr2
    vsra.w         vr10,   vr10,    vr3 // >> shift
    vsra.w         vr11,   vr11,    vr3
    vadd.w         vr10,   vr10,    vr4 // + ox
    vadd.w         vr11,   vr11,    vr4
    vssrani.h.w    vr11,   vr10,    0
    vssrani.bu.h   vr10,   vr11,    0
    vbsrl.v        vr6,    vr7,     1
    fst.s          f10,    a0,      0
    vbsrl.v        vr10,   vr10,    4
    fstx.s         f10,    a0,      a1
    alsl.d         a0,     a1,      a0,    1
    addi.d         a4,     a4,      -2
    bnez           a4,     .LOOP_UNI_V4
endfunc

.macro CALC_EPEL_FILTER_LSX out0, out1
    vdp2.h.bu.b    vr12,   vr10,    vr0 //EPEL_FILTER(src, stride)
    vdp2add.h.bu.b vr12,   vr11,    vr5
    vexth.w.h      vr13,   vr12
    vsllwil.w.h    vr12,   vr12,    0
    vmulwev.w.h    vr12,   vr12,    vr1 //EPEL_FILTER(src, stride) * wx
    vmulwev.w.h    vr13,   vr13,    vr1 //EPEL_FILTER(src, stride) * wx
    vadd.w         vr12,   vr12,    vr2 // + offset
    vadd.w         vr13,   vr13,    vr2
    vsra.w         vr12,   vr12,    vr3 // >> shift
    vsra.w         vr13,   vr13,    vr3
    vadd.w         \out0,  vr12,    vr4 // + ox
    vadd.w         \out1,  vr13,    vr4
.endm

.macro CALC_EPEL_FILTER_LASX out0
    xvdp2.h.bu.b   xr11,   xr12,    xr0 //EPEL_FILTER(src, stride)
    xvhaddw.w.h    xr12,   xr11,    xr11
    xvmulwev.w.h   xr12,   xr12,    xr1 //EPEL_FILTER(src, stride) * wx
    xvadd.w        xr12,   xr12,    xr2 // + offset
    xvsra.w        xr12,   xr12,    xr3 // >> shift
    xvadd.w        \out0,  xr12,    xr4 // + ox
.endm

//w is a label, also can be used as a condition for ".if" statement.
.macro PUT_HEVC_EPEL_UNI_W_V8_LSX w
    fld.d          f6,     a2,      0  //0
    fldx.d         f7,     a2,      a3 //1
    fldx.d         f8,     a2,      t0 //2
    add.d          a2,     a2,      t1
.LOOP_UNI_V8_\w:
    fld.d          f9,     a2,      0  // 3
    add.d          a2,     a2,      a3
    vilvl.b        vr10,   vr7,     vr6
    vilvl.b        vr11,   vr9,     vr8
    vaddi.bu       vr6,    vr7,     0  //back up previous value
    vaddi.bu       vr7,    vr8,     0
    vaddi.bu       vr8,    vr9,     0
    CALC_EPEL_FILTER_LSX vr12, vr13
    vssrani.h.w    vr13,   vr12,    0
    vssrani.bu.h   vr13,   vr13,    0
.if \w < 8
    fst.s          f13,    a0,      0
    vstelm.h       vr13,   a0,      4,   2
.else
    fst.d          f13,    a0,      0
.endif
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_V8_\w
.endm

//w is a label, also can be used as a condition for ".if" statement.
.macro PUT_HEVC_EPEL_UNI_W_V8_LASX w
    fld.d          f6,     a2,      0  //0
    fldx.d         f7,     a2,      a3 //1
    fldx.d         f8,     a2,      t0 //2
    add.d          a2,     a2,      t1
.LOOP_UNI_V8_LASX_\w:
    fld.d          f9,     a2,      0  // 3
    add.d          a2,     a2,      a3
    vilvl.b        vr10,   vr7,     vr6
    vilvl.b        vr11,   vr9,     vr8
    xvilvl.h       xr12,   xr11,    xr10
    xvilvh.h       xr13,   xr11,    xr10
    xvpermi.q      xr12,   xr13,    0x02
    vaddi.bu       vr6,    vr7,     0  //back up previous value
    vaddi.bu       vr7,    vr8,     0
    vaddi.bu       vr8,    vr9,     0
    CALC_EPEL_FILTER_LASX xr12
    xvpermi.q      xr13,   xr12,    0x01
    vssrani.h.w    vr13,   vr12,    0
    vssrani.bu.h   vr13,   vr13,    0
.if \w < 8
    fst.s          f13,    a0,      0
    vstelm.h       vr13,   a0,      4,   2
.else
    fst.d          f13,    a0,      0
.endif
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_V8_LASX_\w
.endm

function ff_hevc_put_hevc_epel_uni_w_v6_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
    PUT_HEVC_EPEL_UNI_W_V8_LSX 6
endfunc

function ff_hevc_put_hevc_epel_uni_w_v6_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.w    xr0,    xr0
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    PUT_HEVC_EPEL_UNI_W_V8_LASX 6
endfunc

function ff_hevc_put_hevc_epel_uni_w_v8_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
    PUT_HEVC_EPEL_UNI_W_V8_LSX 8
endfunc

function ff_hevc_put_hevc_epel_uni_w_v8_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.w    xr0,    xr0
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    PUT_HEVC_EPEL_UNI_W_V8_LASX 8
endfunc

//w is a label, also can be used as a condition for ".if" statement.
.macro PUT_HEVC_EPEL_UNI_W_V16_LSX w
    vld            vr6,    a2,      0  //0
    vldx           vr7,    a2,      a3 //1
    vldx           vr8,    a2,      t0 //2
    add.d          a2,     a2,      t1
.LOOP_UNI_V16_\w:
    vld            vr9,    a2,      0  //3
    add.d          a2,     a2,      a3
    vilvl.b        vr10,   vr7,     vr6
    vilvl.b        vr11,   vr9,     vr8
    CALC_EPEL_FILTER_LSX vr14, vr15
    vilvh.b        vr10,   vr7,     vr6
    vilvh.b        vr11,   vr9,     vr8
    CALC_EPEL_FILTER_LSX vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vaddi.bu       vr6,    vr7,     0  //back up previous value
    vaddi.bu       vr7,    vr8,     0
    vaddi.bu       vr8,    vr9,     0
.if \w < 16
    fst.d          f17,    a0,      0
    vstelm.w       vr17,   a0,      8,    2
.else
    vst            vr17,   a0,      0
.endif
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_V16_\w
.endm

//w is a label, also can be used as a condition for ".if" statement.
.macro PUT_HEVC_EPEL_UNI_W_V16_LASX w
    vld            vr6,    a2,      0  //0
    vldx           vr7,    a2,      a3 //1
    vldx           vr8,    a2,      t0 //2
    add.d          a2,     a2,      t1
.LOOP_UNI_V16_LASX_\w:
    vld            vr9,    a2,      0  //3
    add.d          a2,     a2,      a3
    xvilvl.b       xr10,   xr7,     xr6
    xvilvh.b       xr11,   xr7,     xr6
    xvpermi.q      xr11,   xr10,    0x20
    xvilvl.b       xr12,   xr9,     xr8
    xvilvh.b       xr13,   xr9,     xr8
    xvpermi.q      xr13,   xr12,    0x20
    xvdp2.h.bu.b   xr10,   xr11,    xr0 //EPEL_FILTER(src, stride)
    xvdp2add.h.bu.b xr10,  xr13,    xr5
    xvexth.w.h     xr11,   xr10
    xvsllwil.w.h   xr10,   xr10,    0
    xvmulwev.w.h   xr10,   xr10,    xr1 //EPEL_FILTER(src, stride) * wx
    xvmulwev.w.h   xr11,   xr11,    xr1
    xvadd.w        xr10,   xr10,    xr2 // + offset
    xvadd.w        xr11,   xr11,    xr2
    xvsra.w        xr10,   xr10,    xr3 // >> shift
    xvsra.w        xr11,   xr11,    xr3
    xvadd.w        xr10,   xr10,    xr4 // + wx
    xvadd.w        xr11,   xr11,    xr4
    xvssrani.h.w   xr11,   xr10,    0
    xvpermi.q      xr10,   xr11,    0x01
    vssrani.bu.h   vr10,   vr11,    0
    vaddi.bu       vr6,    vr7,     0  //back up previous value
    vaddi.bu       vr7,    vr8,     0
    vaddi.bu       vr8,    vr9,     0
.if \w < 16
    fst.d          f10,    a0,      0
    vstelm.w       vr10,   a0,      8,    2
.else
    vst            vr10,   a0,      0
.endif
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_V16_LASX_\w
.endm

function ff_hevc_put_hevc_epel_uni_w_v12_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
    PUT_HEVC_EPEL_UNI_W_V16_LSX 12
endfunc

function ff_hevc_put_hevc_epel_uni_w_v12_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.q    xr0,    xr0
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    xvrepl128vei.h xr5,    xr0,     1
    xvrepl128vei.h xr0,    xr0,     0
    PUT_HEVC_EPEL_UNI_W_V16_LASX 12
endfunc

function ff_hevc_put_hevc_epel_uni_w_v16_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
    PUT_HEVC_EPEL_UNI_W_V16_LSX 16
endfunc

function ff_hevc_put_hevc_epel_uni_w_v16_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.q    xr0,    xr0
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    xvrepl128vei.h xr5,    xr0,     1
    xvrepl128vei.h xr0,    xr0,     0
    PUT_HEVC_EPEL_UNI_W_V16_LASX 16
endfunc

function ff_hevc_put_hevc_epel_uni_w_v24_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
    addi.d         t2,     a0,      0 //save init
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LSX 24
    addi.d         a0,     t2,      16 //increase step
    addi.d         a2,     t3,      16
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V8_LSX 24
endfunc

function ff_hevc_put_hevc_epel_uni_w_v24_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.w    xr20,   xr0         //save xr0
    xvreplve0.q    xr0,    xr0
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    xvrepl128vei.h xr5,    xr0,     1
    xvrepl128vei.h xr0,    xr0,     0
    addi.d         t2,     a0,      0 //save init
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LASX 24
    addi.d         a0,     t2,      16 //increase step
    addi.d         a2,     t3,      16
    addi.d         a4,     t4,      0
    xvaddi.bu      xr0,    xr20,    0
    PUT_HEVC_EPEL_UNI_W_V8_LASX 24
endfunc

function ff_hevc_put_hevc_epel_uni_w_v32_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LSX 32
    addi.d         a0,     t2,      16
    addi.d         a2,     t3,      16
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LSX 33
endfunc

function ff_hevc_put_hevc_epel_uni_w_v32_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.q    xr0,    xr0
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    xvrepl128vei.h xr5,    xr0,     1
    xvrepl128vei.h xr0,    xr0,     0
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LASX 32
    addi.d         a0,     t2,      16
    addi.d         a2,     t3,      16
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LASX 33
endfunc

function ff_hevc_put_hevc_epel_uni_w_v48_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LSX 48
    addi.d         a0,     t2,      16
    addi.d         a2,     t3,      16
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LSX 49
    addi.d         a0,     t2,      32
    addi.d         a2,     t3,      32
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LSX 50
endfunc

function ff_hevc_put_hevc_epel_uni_w_v48_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.q    xr0,    xr0
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    xvrepl128vei.h xr5,    xr0,     1
    xvrepl128vei.h xr0,    xr0,     0
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LASX 48
    addi.d         a0,     t2,      16
    addi.d         a2,     t3,      16
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LASX 49
    addi.d         a0,     t2,      32
    addi.d         a2,     t3,      32
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LASX 50
endfunc

function ff_hevc_put_hevc_epel_uni_w_v64_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LSX 64
    addi.d         a0,     t2,      16
    addi.d         a2,     t3,      16
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LSX 65
    addi.d         a0,     t2,      32
    addi.d         a2,     t3,      32
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LSX 66
    addi.d         a0,     t2,      48
    addi.d         a2,     t3,      48
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LSX 67
endfunc

function ff_hevc_put_hevc_epel_uni_w_v64_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      8  //my
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.q    xr0,    xr0
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    sub.d          a2,     a2,      a3 //src -= stride
    xvrepl128vei.h xr5,    xr0,     1
    xvrepl128vei.h xr0,    xr0,     0
    addi.d         t2,     a0,      0
    addi.d         t3,     a2,      0
    addi.d         t4,     a4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LASX 64
    addi.d         a0,     t2,      16
    addi.d         a2,     t3,      16
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LASX 65
    addi.d         a0,     t2,      32
    addi.d         a2,     t3,      32
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LASX 66
    addi.d         a0,     t2,      48
    addi.d         a2,     t3,      48
    addi.d         a4,     t4,      0
    PUT_HEVC_EPEL_UNI_W_V16_LASX 67
endfunc

/*
 * void FUNC(put_hevc_epel_uni_w_h)(uint8_t *_dst, ptrdiff_t _dststride,
 *                                  const uint8_t *_src, ptrdiff_t _srcstride,
 *                                  int height, int denom, int wx, int ox,
 *                                  intptr_t mx, intptr_t my, int width)
 */
function ff_hevc_put_hevc_epel_uni_w_h4_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    vreplvei.w     vr0,    vr0,     0
    la.local       t1,     shufb
    vld            vr5,    t1,      0
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
.LOOP_UNI_W_H4:
    fld.d          f6,     a2,      0
    add.d          a2,     a2,      a3
    vshuf.b        vr6,    vr6,     vr6,   vr5
    vdp2.h.bu.b    vr7,    vr6,     vr0
    vhaddw.w.h     vr7,    vr7,     vr7
    vmulwev.w.h    vr7,    vr7,     vr1
    vadd.w         vr7,    vr7,     vr2
    vsra.w         vr7,    vr7,     vr3
    vadd.w         vr7,    vr7,     vr4
    vssrani.h.w    vr7,    vr7,     0
    vssrani.bu.h   vr7,    vr7,     0
    fst.s          f7,     a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H4
endfunc

function ff_hevc_put_hevc_epel_uni_w_h6_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    vreplvei.w     vr0,    vr0,     0
    la.local       t1,     shufb
    vld            vr6,    t1,      48
    vaddi.bu       vr7,   vr6,      2
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
.LOOP_UNI_W_H6:
    vld            vr8,    a2,      0
    add.d          a2,     a2,      a3
    vshuf.b        vr10,   vr8,     vr8,   vr6
    vshuf.b        vr11,   vr8,     vr8,   vr7
    CALC_EPEL_FILTER_LSX vr14, vr15
    vssrani.h.w    vr15,   vr14,    0
    vssrani.bu.h   vr15,   vr15,    0
    fst.s          f15,    a0,      0
    vstelm.h       vr15,   a0,      4,   2
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H6
endfunc

function ff_hevc_put_hevc_epel_uni_w_h6_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.w    xr0,    xr0
    la.local       t1,     shufb
    xvld           xr6,    t1,      64
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
.LOOP_UNI_W_H6_LASX:
    vld            vr8,    a2,      0
    xvreplve0.q    xr8,    xr8
    add.d          a2,     a2,      a3
    xvshuf.b       xr12,   xr8,     xr8,   xr6
    CALC_EPEL_FILTER_LASX xr14
    xvpermi.q      xr15,   xr14,    0x01
    vssrani.h.w    vr15,   vr14,    0
    vssrani.bu.h   vr15,   vr15,    0
    fst.s          f15,    a0,      0
    vstelm.h       vr15,   a0,      4,   2
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H6_LASX
endfunc

function ff_hevc_put_hevc_epel_uni_w_h8_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    vreplvei.w     vr0,    vr0,     0
    la.local       t1,     shufb
    vld            vr6,    t1,      48
    vaddi.bu       vr7,   vr6,      2
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
.LOOP_UNI_W_H8:
    vld            vr8,    a2,      0
    add.d          a2,     a2,      a3
    vshuf.b        vr10,   vr8,     vr8,   vr6
    vshuf.b        vr11,   vr8,     vr8,   vr7
    CALC_EPEL_FILTER_LSX vr14, vr15
    vssrani.h.w    vr15,   vr14,    0
    vssrani.bu.h   vr15,   vr15,    0
    fst.d          f15,    a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H8
endfunc

function ff_hevc_put_hevc_epel_uni_w_h8_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.w    xr0,    xr0
    la.local       t1,     shufb
    xvld           xr6,    t1,      64
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
.LOOP_UNI_W_H8_LASX:
    vld            vr8,    a2,      0
    xvreplve0.q    xr8,    xr8
    add.d          a2,     a2,      a3
    xvshuf.b       xr12,   xr8,     xr8,   xr6
    CALC_EPEL_FILTER_LASX xr14
    xvpermi.q      xr15,   xr14,    0x01
    vssrani.h.w    vr15,   vr14,    0
    vssrani.bu.h   vr15,   vr15,    0
    fst.d          f15,    a0,      0
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H8_LASX
endfunc

.macro EPEL_UNI_W_H16_LOOP_LSX idx0, idx1, idx2
    vld            vr8,    a2,      \idx0
    vshuf.b        vr10,   vr8,     vr8,   vr6
    vshuf.b        vr11,   vr8,     vr8,   vr7
    CALC_EPEL_FILTER_LSX vr14, vr15
    vld            vr8,    a2,      \idx1
    vshuf.b        vr10,   vr8,     vr8,   vr6
    vshuf.b        vr11,   vr8,     vr8,   vr7
    CALC_EPEL_FILTER_LSX vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    vst            vr17,   a0,     \idx2
.endm

.macro EPEL_UNI_W_H16_LOOP_LASX idx0, idx2, w
    xvld           xr8,    a2,      \idx0
    xvpermi.d      xr9,    xr8,     0x09
    xvreplve0.q    xr8,    xr8
    xvshuf.b       xr12,   xr8,     xr8,   xr6
    CALC_EPEL_FILTER_LASX xr14
    xvreplve0.q    xr8,    xr9
    xvshuf.b       xr12,   xr8,     xr8,   xr6
    CALC_EPEL_FILTER_LASX xr16
    xvssrani.h.w   xr16,   xr14,    0
    xvpermi.q      xr17,   xr16,    0x01
    vssrani.bu.h   vr17,   vr16,    0
    vpermi.w       vr17,   vr17,    0xd8
.if \w == 12
    fst.d          f17,    a0,      0
    vstelm.w       vr17,   a0,      8,   2
.else
    vst            vr17,   a0,      \idx2
.endif
.endm

function ff_hevc_put_hevc_epel_uni_w_h12_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    vreplvei.w     vr0,    vr0,     0
    la.local       t1,     shufb
    vld            vr6,    t1,      48
    vaddi.bu       vr7,   vr6,      2
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
.LOOP_UNI_W_H12:
    vld            vr8,    a2,      0
    vshuf.b        vr10,   vr8,     vr8,   vr6
    vshuf.b        vr11,   vr8,     vr8,   vr7
    CALC_EPEL_FILTER_LSX vr14, vr15
    vld            vr8,    a2,      8
    vshuf.b        vr10,   vr8,     vr8,   vr6
    vshuf.b        vr11,   vr8,     vr8,   vr7
    CALC_EPEL_FILTER_LSX vr16, vr17
    vssrani.h.w    vr15,   vr14,    0
    vssrani.h.w    vr17,   vr16,    0
    vssrani.bu.h   vr17,   vr15,    0
    fst.d          f17,    a0,      0
    vstelm.w       vr17,   a0,      8,   2
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H12
endfunc

function ff_hevc_put_hevc_epel_uni_w_h12_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.w    xr0,    xr0
    la.local       t1,     shufb
    xvld           xr6,    t1,      64
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
.LOOP_UNI_W_H12_LASX:
    EPEL_UNI_W_H16_LOOP_LASX 0, 0, 12
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H12_LASX
endfunc

function ff_hevc_put_hevc_epel_uni_w_h16_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    vreplvei.w     vr0,    vr0,     0
    la.local       t1,     shufb
    vld            vr6,    t1,      48
    vaddi.bu       vr7,   vr6,      2
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
.LOOP_UNI_W_H16:
    EPEL_UNI_W_H16_LOOP_LSX 0, 8, 0
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H16
endfunc

function ff_hevc_put_hevc_epel_uni_w_h16_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.w    xr0,    xr0
    la.local       t1,     shufb
    xvld           xr6,    t1,      64
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
.LOOP_UNI_W_H16_LASX:
    EPEL_UNI_W_H16_LOOP_LASX 0, 0, 16
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H16_LASX
endfunc

function ff_hevc_put_hevc_epel_uni_w_h24_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    vreplvei.w     vr0,    vr0,     0
    la.local       t1,     shufb
    vld            vr6,    t1,      48
    vaddi.bu       vr7,   vr6,      2
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
.LOOP_UNI_W_H24:
    EPEL_UNI_W_H16_LOOP_LSX 0, 8, 0
    vld            vr8,    a2,      16
    add.d          a2,     a2,      a3
    vshuf.b        vr10,   vr8,     vr8,   vr6
    vshuf.b        vr11,   vr8,     vr8,   vr7
    CALC_EPEL_FILTER_LSX vr18, vr19
    vssrani.h.w    vr19,   vr18,    0
    vssrani.bu.h   vr19,   vr19,    0
    fst.d          f19,    a0,      16
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H24
endfunc

function ff_hevc_put_hevc_epel_uni_w_h24_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.w    xr0,    xr0
    la.local       t1,     shufb
    xvld           xr6,    t1,      64
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
.LOOP_UNI_W_H24_LASX:
    EPEL_UNI_W_H16_LOOP_LASX 0, 0, 24
    vld            vr8,    a2,      16
    add.d          a2,     a2,      a3
    xvreplve0.q    xr8,    xr8
    xvshuf.b       xr12,   xr8,     xr8,   xr6
    CALC_EPEL_FILTER_LASX xr14
    xvpermi.q      xr15,   xr14,    0x01
    vssrani.h.w    vr15,   vr14,    0
    vssrani.bu.h   vr15,   vr15,    0
    fst.d          f15,    a0,      16
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H24_LASX
endfunc

function ff_hevc_put_hevc_epel_uni_w_h32_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    vreplvei.w     vr0,    vr0,     0
    la.local       t1,     shufb
    vld            vr6,    t1,      48
    vaddi.bu       vr7,   vr6,      2
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
.LOOP_UNI_W_H32:
    EPEL_UNI_W_H16_LOOP_LSX 0,  8,  0
    EPEL_UNI_W_H16_LOOP_LSX 16, 24, 16
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H32
endfunc

function ff_hevc_put_hevc_epel_uni_w_h32_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.w    xr0,    xr0
    la.local       t1,     shufb
    xvld           xr6,    t1,      64
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
.LOOP_UNI_W_H32_LASX:
    EPEL_UNI_W_H16_LOOP_LASX 0,  0,  32
    EPEL_UNI_W_H16_LOOP_LASX 16, 16, 32
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H32_LASX
endfunc

function ff_hevc_put_hevc_epel_uni_w_h48_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    vreplvei.w     vr0,    vr0,     0
    la.local       t1,     shufb
    vld            vr6,    t1,      48
    vaddi.bu       vr7,   vr6,      2
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
.LOOP_UNI_W_H48:
    EPEL_UNI_W_H16_LOOP_LSX 0,  8,  0
    EPEL_UNI_W_H16_LOOP_LSX 16, 24, 16
    EPEL_UNI_W_H16_LOOP_LSX 32, 40, 32
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H48
endfunc

function ff_hevc_put_hevc_epel_uni_w_h48_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.w    xr0,    xr0
    la.local       t1,     shufb
    xvld           xr6,    t1,      64
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
.LOOP_UNI_W_H48_LASX:
    EPEL_UNI_W_H16_LOOP_LASX 0,  0,  48
    EPEL_UNI_W_H16_LOOP_LASX 16, 16, 48
    EPEL_UNI_W_H16_LOOP_LASX 32, 32, 48
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H48_LASX
endfunc

function ff_hevc_put_hevc_epel_uni_w_h64_8_lsx
    LOAD_VAR 128
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    vreplvei.w     vr0,    vr0,     0
    la.local       t1,     shufb
    vld            vr6,    t1,      48
    vaddi.bu       vr7,   vr6,      2
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
    vreplvei.h     vr5,    vr0,     1
    vreplvei.h     vr0,    vr0,     0
.LOOP_UNI_W_H64:
    EPEL_UNI_W_H16_LOOP_LSX 0,  8,  0
    EPEL_UNI_W_H16_LOOP_LSX 16, 24, 16
    EPEL_UNI_W_H16_LOOP_LSX 32, 40, 32
    EPEL_UNI_W_H16_LOOP_LSX 48, 56, 48
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H64
endfunc

function ff_hevc_put_hevc_epel_uni_w_h64_8_lasx
    LOAD_VAR 256
    ld.d           t0,     sp,      0  //mx
    slli.w         t0,     t0,      2
    la.local       t1,     ff_hevc_epel_filters
    vldx           vr0,    t1,      t0 //filter
    xvreplve0.w    xr0,    xr0
    la.local       t1,     shufb
    xvld           xr6,    t1,      64
    slli.d         t0,     a3,      1  //stride * 2
    add.d          t1,     t0,      a3 //stride * 3
    addi.d         a2,     a2,      -1 //src -= 1
.LOOP_UNI_W_H64_LASX:
    EPEL_UNI_W_H16_LOOP_LASX 0,  0,  64
    EPEL_UNI_W_H16_LOOP_LASX 16, 16, 64
    EPEL_UNI_W_H16_LOOP_LASX 32, 32, 64
    EPEL_UNI_W_H16_LOOP_LASX 48, 48, 64
    add.d          a2,     a2,      a3
    add.d          a0,     a0,      a1
    addi.d         a4,     a4,      -1
    bnez           a4,     .LOOP_UNI_W_H64_LASX
endfunc

/*
 * void FUNC(put_hevc_epel_bi_h)(uint8_t *_dst, ptrdiff_t _dststride,
 *                               const uint8_t *_src, ptrdiff_t _srcstride,
 *                               const int16_t *src2, int height, intptr_t mx,
 *                               intptr_t my, int width)
 */
function ff_hevc_put_hevc_bi_epel_h4_8_lsx
   slli.w          a6,     a6,      2
   la.local        t0,     ff_hevc_epel_filters
   vldx            vr0,    t0,      a6 // filter
   vreplvei.w      vr0,    vr0,     0
   la.local        t0,     shufb
   vld             vr1,    t0,      0 // mask
   addi.d          a2,     a2,     -1 // src -= 1
.LOOP_BI_EPEL_H4:
   vld             vr4,    a4,      0 // src2
   vld             vr5,    a2,      0
   add.d           a2,     a2,      a3
   addi.d          a4,     a4,      128
   vshuf.b         vr5,    vr5,     vr5,    vr1
   vdp2.h.bu.b     vr6,    vr5,     vr0 // EPEL_FILTER(src, 1)
   vsllwil.w.h     vr4,    vr4,     0
   vhaddw.w.h      vr6,    vr6,     vr6
   vadd.w          vr6,    vr6,     vr4 // src2[x]
   vssrani.h.w     vr6,    vr6,     0
   vssrarni.bu.h   vr6,    vr6,     7
   fst.s           f6,     a0,      0
   add.d           a0,     a0,      a1
   addi.d          a5,     a5,     -1
   bnez            a5,     .LOOP_BI_EPEL_H4
endfunc

.macro PUT_HEVC_BI_EPEL_H8_LSX in0, in1, in2, in3, out0
   vshuf.b         vr6,    \in1,    \in0,   \in2
   vshuf.b         vr7,    \in1,    \in0,   \in3
   vdp2.h.bu.b     vr8,    vr6,     vr0 // EPEL_FILTER(src, 1)
   vdp2add.h.bu.b  vr8,    vr7,     vr1 // EPEL_FILTER(src, 1)
   vsadd.h         \out0,  vr8,     vr4 // src2[x]
.endm

.macro PUT_HEVC_BI_EPEL_H16_LASX in0, in1, in2, in3, out0
   xvshuf.b         xr6,    \in1,    \in0,   \in2
   xvshuf.b         xr7,    \in1,    \in0,   \in3
   xvdp2.h.bu.b     xr8,    xr6,     xr0 // EPEL_FILTER(src, 1)
   xvdp2add.h.bu.b  xr8,    xr7,     xr1 // EPEL_FILTER(src, 1)
   xvsadd.h         \out0,  xr8,     xr4 // src2[x]
.endm

function ff_hevc_put_hevc_bi_epel_h6_8_lsx
   slli.w          a6,     a6,      2
   la.local        t0,     ff_hevc_epel_filters
   vldx            vr0,    t0,      a6 // filter
   vreplvei.h      vr1,    vr0,     1
   vreplvei.h      vr0,    vr0,     0
   la.local        t0,     shufb
   vld             vr2,    t0,      48// mask
   vaddi.bu        vr3,    vr2,     2
   addi.d          a2,     a2,     -1 // src -= 1
.LOOP_BI_EPEL_H6:
   vld             vr4,    a4,      0 // src2
   vld             vr5,    a2,      0
   add.d           a2,     a2,      a3
   addi.d          a4,     a4,      128
   PUT_HEVC_BI_EPEL_H8_LSX vr5, vr5, vr2, vr3, vr7
   vssrarni.bu.h   vr7,    vr7,     7
   fst.s           f7,     a0,      0
   vstelm.h        vr7,    a0,      4,   2
   add.d           a0,     a0,      a1
   addi.d          a5,     a5,     -1
   bnez            a5,     .LOOP_BI_EPEL_H6
endfunc

function ff_hevc_put_hevc_bi_epel_h8_8_lsx
   slli.w          a6,     a6,      2
   la.local        t0,     ff_hevc_epel_filters
   vldx            vr0,    t0,      a6 // filter
   vreplvei.h      vr1,    vr0,     1
   vreplvei.h      vr0,    vr0,     0
   la.local        t0,     shufb
   vld             vr2,    t0,      48// mask
   vaddi.bu        vr3,    vr2,     2
   addi.d          a2,     a2,     -1 // src -= 1
.LOOP_BI_EPEL_H8:
   vld             vr4,    a4,      0 // src2
   vld             vr5,    a2,      0
   add.d           a2,     a2,      a3
   addi.d          a4,     a4,      128
   PUT_HEVC_BI_EPEL_H8_LSX vr5, vr5, vr2, vr3, vr7
   vssrarni.bu.h   vr7,    vr7,     7
   fst.d           f7,     a0,      0
   add.d           a0,     a0,      a1
   addi.d          a5,     a5,     -1
   bnez            a5,     .LOOP_BI_EPEL_H8
endfunc

function ff_hevc_put_hevc_bi_epel_h12_8_lsx
   slli.w          a6,     a6,      2
   la.local        t0,     ff_hevc_epel_filters
   vldx            vr0,    t0,      a6 // filter
   vreplvei.h      vr1,    vr0,     1
   vreplvei.h      vr0,    vr0,     0
   la.local        t0,     shufb
   vld             vr2,    t0,      48// mask
   vaddi.bu        vr3,    vr2,     2
   addi.d          a2,     a2,     -1 // src -= 1
.LOOP_BI_EPEL_H12:
   vld             vr4,    a4,      0 // src2
   vld             vr5,    a2,      0
   PUT_HEVC_BI_EPEL_H8_LSX vr5, vr5, vr2, vr3, vr11
   vld             vr5,    a2,      8
   vld             vr4,    a4,      16
   PUT_HEVC_BI_EPEL_H8_LSX vr5, vr5, vr2, vr3, vr12
   vssrarni.bu.h   vr12,   vr11,    7
   fst.d           f12,    a0,      0
   vstelm.w        vr12,   a0,      8,   2
   add.d           a2,     a2,      a3
   addi.d          a4,     a4,      128
   add.d           a0,     a0,      a1
   addi.d          a5,     a5,     -1
   bnez            a5,     .LOOP_BI_EPEL_H12
endfunc

function ff_hevc_put_hevc_bi_epel_h12_8_lasx
   slli.w          a6,     a6,      2
   la.local        t0,     ff_hevc_epel_filters
   vldx            vr0,    t0,      a6 // filter
   xvreplve0.q     xr0,    xr0
   xvrepl128vei.h  xr1,    xr0,     1
   xvrepl128vei.h  xr0,    xr0,     0
   la.local        t0,     shufb
   xvld            xr2,    t0,      96// mask
   xvaddi.bu       xr3,    xr2,     2
   addi.d          a2,     a2,     -1 // src -= 1
.LOOP_BI_EPEL_H12_LASX:
   xvld            xr4,    a4,      0 // src2
   xvld            xr5,    a2,      0
   xvpermi.d       xr5,    xr5,     0x94
   PUT_HEVC_BI_EPEL_H16_LASX xr5, xr5, xr2, xr3, xr9
   xvpermi.q       xr10,   xr9,     0x01
   vssrarni.bu.h   vr10,   vr9,     7
   fst.d           f10,    a0,      0
   vstelm.w        vr10,   a0,      8,  2
   add.d           a2,     a2,      a3
   addi.d          a4,     a4,      128
   add.d           a0,     a0,      a1
   addi.d          a5,     a5,     -1
   bnez            a5,     .LOOP_BI_EPEL_H12_LASX
endfunc

function ff_hevc_put_hevc_bi_epel_h16_8_lsx
   slli.w          a6,     a6,      2
   la.local        t0,     ff_hevc_epel_filters
   vldx            vr0,    t0,      a6 // filter
   vreplvei.h      vr1,    vr0,     1
   vreplvei.h      vr0,    vr0,     0
   la.local        t0,     shufb
   vld             vr2,    t0,      48// mask
   vaddi.bu        vr3,    vr2,     2
   addi.d          a2,     a2,     -1 // src -= 1
.LOOP_BI_EPEL_H16:
   vld             vr4,    a4,      0 // src2
   vld             vr5,    a2,      0
   PUT_HEVC_BI_EPEL_H8_LSX vr5, vr5, vr2, vr3, vr11
   vld             vr5,    a2,      8
   vld             vr4,    a4,      16
   PUT_HEVC_BI_EPEL_H8_LSX vr5, vr5, vr2, vr3, vr12
   vssrarni.bu.h   vr12,   vr11,    7
   vst             vr12,   a0,      0
   add.d           a2,     a2,      a3
   addi.d          a4,     a4,      128
   add.d           a0,     a0,      a1
   addi.d          a5,     a5,     -1
   bnez            a5,     .LOOP_BI_EPEL_H16
endfunc

function ff_hevc_put_hevc_bi_epel_h16_8_lasx
   slli.w          a6,     a6,      2
   la.local        t0,     ff_hevc_epel_filters
   vldx            vr0,    t0,      a6 // filter
   xvreplve0.q     xr0,    xr0
   xvrepl128vei.h  xr1,    xr0,     1
   xvrepl128vei.h  xr0,    xr0,     0
   la.local        t0,     shufb
   xvld            xr2,    t0,      96// mask
   xvaddi.bu       xr3,    xr2,     2
   addi.d          a2,     a2,     -1 // src -= 1
.LOOP_BI_EPEL_H16_LASX:
   xvld            xr4,    a4,      0 // src2
   xvld            xr5,    a2,      0
   xvpermi.d       xr5,    xr5,     0x94
   PUT_HEVC_BI_EPEL_H16_LASX xr5, xr5, xr2, xr3, xr9
   xvpermi.q       xr10,   xr9,     0x01
   vssrarni.bu.h   vr10,   vr9,     7
   vst             vr10,   a0,      0
   add.d           a2,     a2,      a3
   addi.d          a4,     a4,      128
   add.d           a0,     a0,      a1
   addi.d          a5,     a5,     -1
   bnez            a5,     .LOOP_BI_EPEL_H16_LASX
endfunc

function ff_hevc_put_hevc_bi_epel_h32_8_lasx
   slli.w          a6,     a6,      2
   la.local        t0,     ff_hevc_epel_filters
   vldx            vr0,    t0,      a6 // filter
   xvreplve0.q     xr0,    xr0
   xvrepl128vei.h  xr1,    xr0,     1
   xvrepl128vei.h  xr0,    xr0,     0
   la.local        t0,     shufb
   xvld            xr2,    t0,      96// mask
   xvaddi.bu       xr3,    xr2,     2
   addi.d          a2,     a2,     -1 // src -= 1
.LOOP_BI_EPEL_H32_LASX:
   xvld            xr4,    a4,      0 // src2
   xvld            xr5,    a2,      0
   xvpermi.q       xr15,   xr5,     0x01
   xvpermi.d       xr5,    xr5,     0x94
   PUT_HEVC_BI_EPEL_H16_LASX xr5, xr5, xr2, xr3, xr9
   xvld            xr4,    a4,      32
   xvld            xr15,   a2,      16
   xvpermi.d       xr15,   xr15,    0x94
   PUT_HEVC_BI_EPEL_H16_LASX xr15, xr15, xr2, xr3, xr11
   xvssrarni.bu.h  xr11,   xr9,     7
   xvpermi.d       xr11,   xr11,    0xd8
   xvst            xr11,   a0,      0
   add.d           a2,     a2,      a3
   addi.d          a4,     a4,      128
   add.d           a0,     a0,      a1
   addi.d          a5,     a5,     -1
   bnez            a5,     .LOOP_BI_EPEL_H32_LASX
endfunc

function ff_hevc_put_hevc_bi_epel_h48_8_lsx
   slli.w          a6,     a6,      2
   la.local        t0,     ff_hevc_epel_filters
   vldx            vr0,    t0,      a6// filter
   vreplvei.h      vr1,    vr0,     1
   vreplvei.h      vr0,    vr0,     0
   la.local        t0,     shufb
   vld             vr2,    t0,      48// mask
   vaddi.bu        vr3,    vr2,     2
   vaddi.bu        vr21,   vr2,     8
   vaddi.bu        vr22,   vr2,     10
   addi.d          a2,     a2,     -1 // src -= 1
.LOOP_BI_EPEL_H48:
   vld             vr4,    a4,      0 // src2
   vld             vr5,    a2,      0
   vld             vr9,    a2,      16
   vld             vr10,   a2,      32
   vld             vr11,   a2,      48
   PUT_HEVC_BI_EPEL_H8_LSX vr5, vr5, vr2, vr3, vr12
   vld             vr4,    a4,      16
   PUT_HEVC_BI_EPEL_H8_LSX vr5, vr9, vr21, vr22, vr13
   vld             vr4,    a4,      32
   PUT_HEVC_BI_EPEL_H8_LSX vr9, vr9, vr2, vr3, vr14
   vld             vr4,    a4,      48
   PUT_HEVC_BI_EPEL_H8_LSX vr9, vr10, vr21, vr22, vr15
   vld             vr4,    a4,      64
   PUT_HEVC_BI_EPEL_H8_LSX vr10, vr10, vr2, vr3, vr16
   vld             vr4,    a4,      80
   PUT_HEVC_BI_EPEL_H8_LSX vr10, vr11, vr21, vr22, vr17
   vssrarni.bu.h   vr13,   vr12,    7
   vssrarni.bu.h   vr15,   vr14,    7
   vssrarni.bu.h   vr17,   vr16,    7
   vst             vr13,   a0,      0
   vst             vr15,   a0,      16
   vst             vr17,   a0,      32
   add.d           a2,     a2,      a3
   addi.d          a4,     a4,      128
   add.d           a0,     a0,      a1
   addi.d          a5,     a5,     -1
   bnez            a5,     .LOOP_BI_EPEL_H48
endfunc

function ff_hevc_put_hevc_bi_epel_h48_8_lasx
   slli.w          a6,     a6,      2
   la.local        t0,     ff_hevc_epel_filters
   vldx            vr0,    t0,      a6 // filter
   xvreplve0.q     xr0,    xr0
   xvrepl128vei.h  xr1,    xr0,     1
   xvrepl128vei.h  xr0,    xr0,     0
   la.local        t0,     shufb
   xvld            xr2,    t0,      96// mask
   xvaddi.bu       xr3,    xr2,     2
   addi.d          a2,     a2,     -1 // src -= 1
.LOOP_BI_EPEL_H48_LASX:
   xvld            xr4,    a4,      0 // src2
   xvld            xr5,    a2,      0
   xvld            xr9,    a2,      32
   xvpermi.d       xr10,   xr9,     0x94
   xvpermi.q       xr9,    xr5,     0x21
   xvpermi.d       xr9,    xr9,     0x94
   xvpermi.d       xr5,    xr5,     0x94
   PUT_HEVC_BI_EPEL_H16_LASX xr5, xr5, xr2, xr3, xr11
   xvld            xr4,    a4,      32
   PUT_HEVC_BI_EPEL_H16_LASX xr9, xr9, xr2, xr3, xr12
   xvld            xr4,    a4,      64
   PUT_HEVC_BI_EPEL_H16_LASX xr10, xr10, xr2, xr3, xr13
   xvssrarni.bu.h  xr12,   xr11,    7
   xvpermi.d       xr12,   xr12,    0xd8
   xvpermi.q       xr14,   xr13,    0x01
   vssrarni.bu.h   vr14,   vr13,    7
   xvst            xr12,   a0,      0
   vst             vr14,   a0,      32
   add.d           a2,     a2,      a3
   addi.d          a4,     a4,      128
   add.d           a0,     a0,      a1
   addi.d          a5,     a5,     -1
   bnez            a5,     .LOOP_BI_EPEL_H48_LASX
endfunc

function ff_hevc_put_hevc_bi_epel_h64_8_lsx
   slli.w          a6,     a6,      2
   la.local        t0,     ff_hevc_epel_filters
   vldx            vr0,    t0,      a6// filter
   vreplvei.h      vr1,    vr0,     1
   vreplvei.h      vr0,    vr0,     0
   la.local        t0,     shufb
   vld             vr2,    t0,      48// mask
   vaddi.bu        vr3,    vr2,     2
   vaddi.bu        vr21,   vr2,     8
   vaddi.bu        vr22,   vr2,     10
   addi.d          a2,     a2,     -1 // src -= 1
.LOOP_BI_EPEL_H64:
   vld             vr4,    a4,      0 // src2
   vld             vr5,    a2,      0
   vld             vr9,    a2,      16
   vld             vr10,   a2,      32
   vld             vr11,   a2,      48
   vld             vr12,   a2,      64
   PUT_HEVC_BI_EPEL_H8_LSX vr5, vr5, vr2, vr3, vr13
   vld             vr4,    a4,      16
   PUT_HEVC_BI_EPEL_H8_LSX vr5, vr9, vr21, vr22, vr14
   vld             vr4,    a4,      32
   PUT_HEVC_BI_EPEL_H8_LSX vr9, vr9, vr2, vr3, vr15
   vld             vr4,    a4,      48
   PUT_HEVC_BI_EPEL_H8_LSX vr9, vr10, vr21, vr22, vr16
   vld             vr4,    a4,      64
   PUT_HEVC_BI_EPEL_H8_LSX vr10, vr10, vr2, vr3, vr17
   vld             vr4,    a4,      80
   PUT_HEVC_BI_EPEL_H8_LSX vr10, vr11, vr21, vr22, vr18
   vld             vr4,    a4,      96
   PUT_HEVC_BI_EPEL_H8_LSX vr11, vr11, vr2, vr3, vr19
   vld             vr4,    a4,      112
   PUT_HEVC_BI_EPEL_H8_LSX vr11, vr12, vr21, vr22, vr20
   vssrarni.bu.h   vr14,   vr13,    7
   vssrarni.bu.h   vr16,   vr15,    7
   vssrarni.bu.h   vr18,   vr17,    7
   vssrarni.bu.h   vr20,   vr19,    7
   vst             vr14,   a0,      0
   vst             vr16,   a0,      16
   vst             vr18,   a0,      32
   vst             vr20,   a0,      48
   add.d           a2,     a2,      a3
   addi.d          a4,     a4,      128
   add.d           a0,     a0,      a1
   addi.d          a5,     a5,     -1
   bnez            a5,     .LOOP_BI_EPEL_H64
endfunc

function ff_hevc_put_hevc_bi_epel_h64_8_lasx
   slli.w          a6,     a6,      2
   la.local        t0,     ff_hevc_epel_filters
   vldx            vr0,    t0,      a6 // filter
   xvreplve0.q     xr0,    xr0
   xvrepl128vei.h  xr1,    xr0,     1
   xvrepl128vei.h  xr0,    xr0,     0
   la.local        t0,     shufb
   xvld            xr2,    t0,      96// mask
   xvaddi.bu       xr3,    xr2,     2
   addi.d          a2,     a2,     -1 // src -= 1
.LOOP_BI_EPEL_H64_LASX:
   xvld            xr4,    a4,      0 // src2
   xvld            xr5,    a2,      0
   xvld            xr9,    a2,      32
   xvld            xr11,   a2,      48
   xvpermi.d       xr11,   xr11,    0x94
   xvpermi.d       xr10,   xr9,     0x94
   xvpermi.q       xr9,    xr5,     0x21
   xvpermi.d       xr9,    xr9,     0x94
   xvpermi.d       xr5,    xr5,     0x94
   PUT_HEVC_BI_EPEL_H16_LASX xr5, xr5, xr2, xr3, xr12
   xvld            xr4,    a4,      32
   PUT_HEVC_BI_EPEL_H16_LASX xr9, xr9, xr2, xr3, xr13
   xvld            xr4,    a4,      64
   PUT_HEVC_BI_EPEL_H16_LASX xr10, xr10, xr2, xr3, xr14
   xvld            xr4,    a4,      96
   PUT_HEVC_BI_EPEL_H16_LASX xr11, xr11, xr2, xr3, xr15
   xvssrarni.bu.h  xr13,   xr12,    7
   xvssrarni.bu.h  xr15,   xr14,    7
   xvpermi.d       xr13,   xr13,    0xd8
   xvpermi.d       xr15,   xr15,    0xd8
   xvst            xr13,   a0,      0
   xvst            xr15,   a0,      32
   add.d           a2,     a2,      a3
   addi.d          a4,     a4,      128
   add.d           a0,     a0,      a1
   addi.d          a5,     a5,     -1
   bnez            a5,     .LOOP_BI_EPEL_H64_LASX
endfunc
