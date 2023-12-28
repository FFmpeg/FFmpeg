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
