/*
 * Loongson LSX optimized swscale
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

#include "libavcodec/loongarch/loongson_asm.S"

/* static void yuv2planeX_8_lsx(const int16_t *filter, int filterSize,
 *                                 const int16_t **src, uint8_t *dest, int dstW,
 *                                 const uint8_t *dither, int offset)
 */
function yuv2planeX_8_lsx
    addi.w          t1,     a6,     1
    addi.w          t2,     a6,     2
    addi.w          t3,     a6,     3
    addi.w          t4,     a6,     4
    addi.w          t5,     a6,     5
    addi.w          t6,     a6,     6
    addi.w          t7,     a6,     7
    andi            t0,     a6,     7
    andi            t1,     t1,     7
    andi            t2,     t2,     7
    andi            t3,     t3,     7
    andi            t4,     t4,     7
    andi            t5,     t5,     7
    andi            t6,     t6,     7
    andi            t7,     t7,     7
    ldx.bu          t0,     a5,     t0
    ldx.bu          t1,     a5,     t1
    ldx.bu          t2,     a5,     t2
    ldx.bu          t3,     a5,     t3
    ldx.bu          t4,     a5,     t4
    ldx.bu          t5,     a5,     t5
    ldx.bu          t6,     a5,     t6
    ldx.bu          t7,     a5,     t7
    vreplgr2vr.w    vr0,    t0
    vreplgr2vr.w    vr1,    t1
    vreplgr2vr.w    vr2,    t2
    vreplgr2vr.w    vr3,    t3
    vreplgr2vr.w    vr4,    t4
    vreplgr2vr.w    vr5,    t5
    vreplgr2vr.w    vr6,    t6
    vreplgr2vr.w    vr7,    t7
    vilvl.w         vr0,    vr2,    vr0
    vilvl.w         vr4,    vr6,    vr4
    vilvl.w         vr1,    vr3,    vr1
    vilvl.w         vr5,    vr7,    vr5
    vilvl.d         vr12,   vr4,    vr0
    vilvl.d         vr13,   vr5,    vr1
    li.w            t5,     0
    li.w            t8,     8
    bge             a4,     t8,     .WIDTH8
    blt             zero,   a4,     .WIDTH
    b               .END

.WIDTH8:
    li.d            t1,     0
    li.d            t4,     0
    vslli.w         vr2,    vr12,   12
    vslli.w         vr3,    vr13,   12
    move            t3,     a0

.FILTERSIZE8:
    ldx.d           t2,     a2,     t1
    vldx            vr4,    t2,     t5
    vldrepl.h       vr5,    t3,     0
    vmaddwev.w.h    vr2,    vr4,    vr5
    vmaddwod.w.h    vr3,    vr4,    vr5
    addi.d          t1,     t1,     8
    addi.d          t3,     t3,     2
    addi.d          t4,     t4,     1
    blt             t4,     a1,     .FILTERSIZE8
    vsrai.w         vr2,    vr2,    19
    vsrai.w         vr3,    vr3,    19
    vclip255.w      vr2,    vr2
    vclip255.w      vr3,    vr3
    vpickev.h       vr2,    vr3,    vr2
    vpickev.b       vr2,    vr2,    vr2
    vbsrl.v         vr3,    vr2,    4
    vilvl.b         vr2,    vr3,    vr2
    fst.d           f2,     a3,     0
    addi.d          t5,     t5,     16
    addi.d          a4,     a4,     -8
    addi.d          a3,     a3,     8
    bge             a4,     t8,     .WIDTH8
    blt             zero,   a4,     .WIDTH
    b               .END

.WIDTH:
    li.d            t1,     0
    li.d            t4,     0
    vslli.w         vr2,    vr12,   12
    vslli.w         vr3,    vr13,   12
.FILTERSIZE:
    ldx.d           t2,     a2,     t1
    vldx            vr4,    t2,     t5
    vldrepl.h       vr5,    a0,     0
    vmaddwev.w.h    vr2,    vr4,    vr5
    vmaddwod.w.h    vr3,    vr4,    vr5
    addi.d          t1,     t1,     8
    addi.d          a0,     a0,     2
    addi.d          t4,     t4,     1
    blt             t4,     a1,     .FILTERSIZE
    vsrai.w         vr2,    vr2,    19
    vsrai.w         vr3,    vr3,    19
    vclip255.w      vr2,    vr2
    vclip255.w      vr3,    vr3
    vpickev.h       vr2,    vr3,    vr2
    vpickev.b       vr2,    vr2,    vr2
    vbsrl.v         vr3,    vr2,    4
    vilvl.b         vr2,    vr3,    vr2

.DEST:
    vstelm.b        vr2,    a3,     0,    0
    vbsrl.v         vr2,    vr2,    1
    addi.d          a4,     a4,     -1
    addi.d          a3,     a3,     1
    blt             zero,   a4,     .DEST
.END:
endfunc

/*
 * void yuv2plane1_8_lsx(const int16_t *src, uint8_t *dest, int dstW,
 *                       const uint8_t *dither, int offset)
 */
function yuv2plane1_8_lsx
    addi.w       t1,    a4,    1
    addi.w       t2,    a4,    2
    addi.w       t3,    a4,    3
    addi.w       t4,    a4,    4
    addi.w       t5,    a4,    5
    addi.w       t6,    a4,    6
    addi.w       t7,    a4,    7
    andi         t0,    a4,    7
    andi         t1,    t1,    7
    andi         t2,    t2,    7
    andi         t3,    t3,    7
    andi         t4,    t4,    7
    andi         t5,    t5,    7
    andi         t6,    t6,    7
    andi         t7,    t7,    7
    ldx.bu       t0,    a3,    t0
    ldx.bu       t1,    a3,    t1
    ldx.bu       t2,    a3,    t2
    ldx.bu       t3,    a3,    t3
    ldx.bu       t4,    a3,    t4
    ldx.bu       t5,    a3,    t5
    ldx.bu       t6,    a3,    t6
    ldx.bu       t7,    a3,    t7
    vinsgr2vr.h  vr1,   t0,    0
    vinsgr2vr.h  vr1,   t1,    1
    vinsgr2vr.h  vr1,   t2,    2
    vinsgr2vr.h  vr1,   t3,    3
    vinsgr2vr.h  vr1,   t4,    4
    vinsgr2vr.h  vr1,   t5,    5
    vinsgr2vr.h  vr1,   t6,    6
    vinsgr2vr.h  vr1,   t7,    7
    andi         t8,    a2,    7
    srli.d       a2,    a2,    3
    beqz         a2,    2f
1:
    vld           vr2,   a0,    0
    addi.d        a0,    a0,    16
    vavg.h        vr4,   vr1,   vr2
    vssrani.bu.h  vr4,   vr4,   6

    fst.d         f4,    a1,    0
    addi.d        a1,    a1,    8
    addi.d        a2,    a2,    -1
    blt           zero,  a2,    1b
2:
    beqz         t8,    4f
    vld          vr2,   a0,     0
    vavg.h       vr4,   vr1,    vr2
    vssrani.bu.h vr4,   vr4,    6
3:
    vstelm.b     vr4,   a1,     0,  0
    vbsrl.v      vr4,   vr4,    1
    addi.d       t8,    t8,     -1
    addi.d       a1,    a1,     1
    bne          t8,    zero,   3b
4:
endfunc

function yuv2plane1_8_lasx
    addi.w       t1,    a4,    1
    addi.w       t2,    a4,    2
    addi.w       t3,    a4,    3
    addi.w       t4,    a4,    4
    addi.w       t5,    a4,    5
    addi.w       t6,    a4,    6
    addi.w       t7,    a4,    7
    andi         t0,    a4,    7
    andi         t1,    t1,    7
    andi         t2,    t2,    7
    andi         t3,    t3,    7
    andi         t4,    t4,    7
    andi         t5,    t5,    7
    andi         t6,    t6,    7
    andi         t7,    t7,    7
    ldx.bu       t0,    a3,    t0
    ldx.bu       t1,    a3,    t1
    ldx.bu       t2,    a3,    t2
    ldx.bu       t3,    a3,    t3
    ldx.bu       t4,    a3,    t4
    ldx.bu       t5,    a3,    t5
    ldx.bu       t6,    a3,    t6
    ldx.bu       t7,    a3,    t7
    vinsgr2vr.h  vr1,   t0,    0
    vinsgr2vr.h  vr1,   t1,    1
    vinsgr2vr.h  vr1,   t2,    2
    vinsgr2vr.h  vr1,   t3,    3
    vinsgr2vr.h  vr1,   t4,    4
    vinsgr2vr.h  vr1,   t5,    5
    vinsgr2vr.h  vr1,   t6,    6
    vinsgr2vr.h  vr1,   t7,    7
    xvpermi.q    xr1,   xr1,   0

    andi         t8,    a2,    15
    srli.d       a2,    a2,    4
    beqz         a2,    2f
1:
    xvld          xr2,   a0,    0
    addi.d        a0,    a0,    32
    xvavg.h       xr4,   xr1,   xr2
    xvssrani.bu.h xr4,   xr4,   6
    xvstelm.d     xr4,   a1,    0,   0
    xvstelm.d     xr4,   a1,    8,   2
    addi.d        a1,    a1,    16
    addi.d        a2,    a2,    -1
    bnez          a2,    1b
2:
    beqz          t8,    4f
    srli.d        t7,    t8,    3
    beq           t7,    zero,  6f

    vld           vr2,   a0,    0
    addi.d        a0,    a0,    16
    vavg.h        vr4,   vr1,   vr2
    vssrani.bu.h  vr4,   vr4,   6
    fst.d         f4,    a1,    0
    addi.d        a1,    a1,    8
6:
    andi          t7,    t8,    7
    beqz          t7,    4f
    vld           vr2,   a0,    0
    vavg.h        vr4,   vr1,   vr2
    vssrani.bu.h  vr4,   vr4,   6
7:
    vstelm.b      vr4,   a1,    0,  0
    vbsrl.v       vr4,   vr4,   1
    addi.d        t7,    t7,    -1
    addi.d        a1,    a1,    1
    bne           t7,    zero,  7b
4:
endfunc
