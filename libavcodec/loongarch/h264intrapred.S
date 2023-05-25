/*
 * Loongson LSX optimized h264intrapred
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 * Contributed by Lu Wang <wanglu@loongson.cn>
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

const shufa
.byte 6, 5, 4, 3, 2, 1, 0
endconst

const mulk
.byte 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8, 0
endconst

const mulh
.byte 0, 0, 1, 0,  2,  0,  3, 0,  4, 0,  5, 0,  6, 0,  7, 0
.byte 8, 0, 9, 0, 10,  0, 11, 0, 12, 0, 13, 0, 14, 0, 15, 0
endconst

.macro PRED16X16_PLANE
    slli.d        t6,    a1,    1
    slli.d        t4,    a1,    3
    addi.d        t0,    a0,    7
    sub.d         t0,    t0,    a1
    add.d         t1,    a0,    t4
    addi.d        t1,    t1,    -1
    sub.d         t2,    t1,    t6

    ld.bu         t3,    t0,    1
    ld.bu         t4,    t0,    -1
    ld.bu         t5,    t1,    0
    ld.bu         t7,    t2,    0
    sub.d         t3,    t3,    t4
    sub.d         t4,    t5,    t7

    la.local      t5,    mulk
    vld           vr0,   t5,    0
    fld.d         f1,    t0,    2
    fld.d         f2,    t0,    -8
    la.local      t5,    shufa
    fld.d         f3,    t5,    0
    vshuf.b       vr2,   vr2,   vr2,   vr3
    vilvl.b       vr1,   vr1,   vr2
    vhsubw.hu.bu  vr1,   vr1,   vr1
    vmul.h        vr0,   vr0,   vr1
    vhaddw.w.h    vr1,   vr0,   vr0
    vhaddw.d.w    vr0,   vr1,   vr1
    vhaddw.q.d    vr1,   vr0,   vr0
    vpickve2gr.w  t5,    vr1,   0
    add.d         t3,    t3,    t5
//2
    sub.d         t2,    t2,    a1
    ld.bu         t8,    t2,    0
    ldx.bu        t7,    t1,    a1
    sub.d         t5,    t7,    t8
    slli.d        t5,    t5,    1

//3&4
    add.d         t1,    t1,    t6
    sub.d         t2,    t2,    a1
    ld.bu         t8,    t2,    0
    ld.bu         t7,    t1,    0
    sub.d         t7,    t7,    t8
    slli.d        t8,    t7,    1
    add.d         t7,    t7,    t8
    add.d         t5,    t5,    t7
    sub.d         t2,    t2,    a1
    ld.bu         t8,    t2,    0
    ldx.bu        t7,    t1,    a1
    sub.d         t7,    t7,    t8
    slli.d        t7,    t7,    2
    add.d         t5,    t5,    t7

//5&6
    add.d         t1,    t1,    t6
    sub.d         t2,    t2,    a1
    ld.bu         t8,    t2,    0
    ld.bu         t7,    t1,    0
    sub.d         t7,    t7,    t8
    slli.d        t8,    t7,    2
    add.d         t7,    t7,    t8
    add.d         t5,    t5,    t7
    sub.d         t2,    t2,    a1
    ld.bu         t8,    t2,    0
    ldx.bu        t7,    t1,    a1
    sub.d         t7,    t7,    t8
    slli.d        t8,    t7,    1
    slli.d        t7,    t7,    2
    add.d         t7,    t7,    t8
    add.d         t5,    t5,    t7

//7&8
    add.d         t1,    t1,    t6
    sub.d         t2,    t2,    a1
    ld.bu         t8,    t2,    0
    ld.bu         t7,    t1,    0
    sub.d         t7,    t7,    t8
    slli.d        t8,    t7,    3
    sub.d         t7,    t8,    t7
    add.d         t5,    t5,    t7
    sub.d         t2,    t2,    a1
    ld.bu         t8,    t2,    0
    ldx.bu        t7,    t1,    a1
    sub.d         t7,    t7,    t8
    slli.d        t7,    t7,    3
    add.d         t5,    t5,    t7
    add.d         t4,    t4,    t5
    add.d         t1,    t1,    a1
.endm

.macro PRED16X16_PLANE_END
    ld.bu         t7,    t1,    0
    ld.bu         t8,    t2,    16
    add.d         t5,    t7,    t8
    addi.d        t5,    t5,    1
    slli.d        t5,    t5,    4
    add.d         t7,    t3,    t4
    slli.d        t8,    t7,    3
    sub.d         t7,    t8,    t7
    sub.d         t5,    t5,    t7

    la.local      t8,    mulh
    vld           vr3,   t8,    0
    slli.d        t8,    t3,    3
    vreplgr2vr.h  vr4,   t3
    vreplgr2vr.h  vr9,   t8
    vmul.h        vr5,   vr3,   vr4

.rept 16
    move          t7,    t5
    add.d         t5,    t5,    t4
    vreplgr2vr.h  vr6,   t7
    vadd.h        vr7,   vr6,   vr5
    vadd.h        vr8,   vr9,   vr7
    vssrani.bu.h  vr8,   vr7,   5
    vst           vr8,   a0,    0
    add.d         a0,    a0,    a1
.endr
.endm

.macro PRED16X16_PLANE_END_LASX
    ld.bu         t7,    t1,    0
    ld.bu         t8,    t2,    16
    add.d         t5,    t7,    t8
    addi.d        t5,    t5,    1
    slli.d        t5,    t5,    4
    add.d         t7,    t3,    t4
    slli.d        t8,    t7,    3
    sub.d         t7,    t8,    t7
    sub.d         t5,    t5,    t7

    la.local      t8,    mulh
    xvld          xr3,   t8,    0
    xvreplgr2vr.h xr4,   t3
    xvmul.h       xr5,   xr3,   xr4

.rept 8
    move          t7,    t5
    add.d         t5,    t5,    t4
    xvreplgr2vr.h xr6,   t7
    xvreplgr2vr.h xr8,   t5
    add.d         t5,    t5,    t4
    xvadd.h       xr7,   xr6,   xr5
    xvadd.h       xr9,   xr8,   xr5

    xvssrani.bu.h xr9,   xr7,   5
    vstelm.d      vr9,   a0,    0,    0
    xvstelm.d     xr9,   a0,    8,    2
    add.d         a0,    a0,    a1
    vstelm.d      vr9,   a0,    0,    1
    xvstelm.d     xr9,   a0,    8,    3
    add.d         a0,    a0,    a1
.endr
.endm

/* void ff_h264_pred16x16_plane_h264_8_lsx(uint8_t *src, ptrdiff_t stride)
 */
function ff_h264_pred16x16_plane_h264_8_lsx
    PRED16X16_PLANE

    slli.d        t7,    t3,    2
    add.d         t3,    t3,    t7
    addi.d        t3,    t3,    32
    srai.d        t3,    t3,    6
    slli.d        t7,    t4,    2
    add.d         t4,    t4,    t7
    addi.d        t4,    t4,    32
    srai.d        t4,    t4,    6

    PRED16X16_PLANE_END
endfunc

/* void ff_h264_pred16x16_plane_rv40_8_lsx(uint8_t *src, ptrdiff_t stride)
 */
function ff_h264_pred16x16_plane_rv40_8_lsx
    PRED16X16_PLANE

    srai.d        t7,    t3,    2
    add.d         t3,    t3,    t7
    srai.d        t3,    t3,    4
    srai.d        t7,    t4,    2
    add.d         t4,    t4,    t7
    srai.d        t4,    t4,    4

    PRED16X16_PLANE_END
endfunc

/* void ff_h264_pred16x16_plane_svq3_8_lsx(uint8_t *src, ptrdiff_t stride)
 */
function ff_h264_pred16x16_plane_svq3_8_lsx
    PRED16X16_PLANE

    li.d          t6,    4
    li.d          t7,    5
    li.d          t8,    16
    div.d         t3,    t3,    t6
    mul.d         t3,    t3,    t7
    div.d         t3,    t3,    t8
    div.d         t4,    t4,    t6
    mul.d         t4,    t4,    t7
    div.d         t4,    t4,    t8
    move          t7,    t3
    move          t3,    t4
    move          t4,    t7

    PRED16X16_PLANE_END
endfunc

/* void ff_h264_pred16x16_plane_h264_8_lasx(uint8_t *src, ptrdiff_t stride)
 */
function ff_h264_pred16x16_plane_h264_8_lasx
    PRED16X16_PLANE

    slli.d        t7,    t3,    2
    add.d         t3,    t3,    t7
    addi.d        t3,    t3,    32
    srai.d        t3,    t3,    6
    slli.d        t7,    t4,    2
    add.d         t4,    t4,    t7
    addi.d        t4,    t4,    32
    srai.d        t4,    t4,    6

    PRED16X16_PLANE_END_LASX
endfunc

/* void ff_h264_pred16x16_plane_rv40_8_lasx(uint8_t *src, ptrdiff_t stride)
 */
function ff_h264_pred16x16_plane_rv40_8_lasx
    PRED16X16_PLANE

    srai.d        t7,    t3,    2
    add.d         t3,    t3,    t7
    srai.d        t3,    t3,    4
    srai.d        t7,    t4,    2
    add.d         t4,    t4,    t7
    srai.d        t4,    t4,    4

    PRED16X16_PLANE_END_LASX
endfunc

/* void ff_h264_pred16x16_plane_svq3_8_lasx(uint8_t *src, ptrdiff_t stride)
 */
function ff_h264_pred16x16_plane_svq3_8_lasx
    PRED16X16_PLANE

    li.d          t5,    4
    li.d          t7,    5
    li.d          t8,    16
    div.d         t3,    t3,    t5
    mul.d         t3,    t3,    t7
    div.d         t3,    t3,    t8
    div.d         t4,    t4,    t5
    mul.d         t4,    t4,    t7
    div.d         t4,    t4,    t8
    move          t7,    t3
    move          t3,    t4
    move          t4,    t7

    PRED16X16_PLANE_END_LASX
endfunc
