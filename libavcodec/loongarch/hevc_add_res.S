/*
 * Loongson LSX optimized add_residual functions for HEVC decoding
 *
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

/*
 * void ff_hevc_add_residual4x4_lsx(uint8_t *dst, const int16_t *res, ptrdiff_t stride)
 */
.macro ADD_RES_LSX_4x4_8
    vldrepl.w      vr0,    a0,     0
    add.d          t0,     a0,     a2
    vldrepl.w      vr1,    t0,     0
    vld            vr2,    a1,     0

    vilvl.w        vr1,    vr1,    vr0
    vsllwil.hu.bu  vr1,    vr1,    0
    vadd.h         vr1,    vr1,    vr2
    vssrani.bu.h   vr1,    vr1,    0

    vstelm.w       vr1,    a0,     0,    0
    vstelm.w       vr1,    t0,     0,    1
.endm

function ff_hevc_add_residual4x4_8_lsx
    ADD_RES_LSX_4x4_8
    alsl.d         a0,     a2,     a0,   1
    addi.d         a1,     a1,     16
    ADD_RES_LSX_4x4_8
endfunc

/*
 * void ff_hevc_add_residual8x8_8_lsx(uint8_t *dst, const int16_t *res, ptrdiff_t stride)
 */
.macro ADD_RES_LSX_8x8_8
    vldrepl.d      vr0,    a0,     0
    add.d          t0,     a0,     a2
    vldrepl.d      vr1,    t0,     0
    add.d          t1,     t0,     a2
    vldrepl.d      vr2,    t1,     0
    add.d          t2,     t1,     a2
    vldrepl.d      vr3,    t2,     0

    vld            vr4,    a1,     0
    addi.d         t3,     zero,   16
    vldx           vr5,    a1,     t3
    addi.d         t4,     a1,     32
    vld            vr6,    t4,     0
    vldx           vr7,    t4,     t3

    vsllwil.hu.bu  vr0,    vr0,    0
    vsllwil.hu.bu  vr1,    vr1,    0
    vsllwil.hu.bu  vr2,    vr2,    0
    vsllwil.hu.bu  vr3,    vr3,    0
    vadd.h         vr0,    vr0,    vr4
    vadd.h         vr1,    vr1,    vr5
    vadd.h         vr2,    vr2,    vr6
    vadd.h         vr3,    vr3,    vr7
    vssrani.bu.h   vr1,    vr0,    0
    vssrani.bu.h   vr3,    vr2,    0

    vstelm.d       vr1,    a0,     0,     0
    vstelm.d       vr1,    t0,     0,     1
    vstelm.d       vr3,    t1,     0,     0
    vstelm.d       vr3,    t2,     0,     1
.endm

function ff_hevc_add_residual8x8_8_lsx
    ADD_RES_LSX_8x8_8
    alsl.d         a0,     a2,     a0,    2
    addi.d         a1,     a1,     64
    ADD_RES_LSX_8x8_8
endfunc

/*
 * void ff_hevc_add_residual16x16_8_lsx(uint8_t *dst, const int16_t *res, ptrdiff_t stride)
 */
function ff_hevc_add_residual16x16_8_lsx
.rept 8
    vld            vr0,    a0,     0
    vldx           vr2,    a0,     a2

    vld            vr4,    a1,     0
    addi.d         t0,     zero,   16
    vldx           vr5,    a1,     t0
    addi.d         t1,     a1,     32
    vld            vr6,    t1,     0
    vldx           vr7,    t1,     t0

    vexth.hu.bu    vr1,    vr0
    vsllwil.hu.bu  vr0,    vr0,    0
    vexth.hu.bu    vr3,    vr2
    vsllwil.hu.bu  vr2,    vr2,    0
    vadd.h         vr0,    vr0,    vr4
    vadd.h         vr1,    vr1,    vr5
    vadd.h         vr2,    vr2,    vr6
    vadd.h         vr3,    vr3,    vr7

    vssrani.bu.h   vr1,    vr0,    0
    vssrani.bu.h   vr3,    vr2,    0

    vst            vr1,    a0,     0
    vstx           vr3,    a0,     a2

    alsl.d         a0,     a2,     a0,   1
    addi.d         a1,     a1,     64
.endr
endfunc

/*
 * void ff_hevc_add_residual32x32_8_lsx(uint8_t *dst, const int16_t *res, ptrdiff_t stride)
 */
function ff_hevc_add_residual32x32_8_lsx
.rept 32
    vld            vr0,    a0,     0
    addi.w         t0,     zero,   16
    vldx           vr2,    a0,     t0

    vld            vr4,    a1,     0
    vldx           vr5,    a1,     t0
    addi.d         t1,     a1,     32
    vld            vr6,    t1,     0
    vldx           vr7,    t1,     t0

    vexth.hu.bu    vr1,    vr0
    vsllwil.hu.bu  vr0,    vr0,    0
    vexth.hu.bu    vr3,    vr2
    vsllwil.hu.bu  vr2,    vr2,    0
    vadd.h         vr0,    vr0,    vr4
    vadd.h         vr1,    vr1,    vr5
    vadd.h         vr2,    vr2,    vr6
    vadd.h         vr3,    vr3,    vr7

    vssrani.bu.h   vr1,    vr0,    0
    vssrani.bu.h   vr3,    vr2,    0

    vst            vr1,    a0,     0
    vstx           vr3,    a0,     t0

    add.d          a0,     a0,     a2
    addi.d         a1,     a1,     64
.endr
endfunc
