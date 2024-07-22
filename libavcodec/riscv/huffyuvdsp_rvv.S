/*
 * Copyright © 2023 Rémi Denis-Courmont.
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

#include "libavutil/riscv/asm.S"

func ff_add_int16_rvv, zve32x, zba
        lpad    0
1:
        vsetvli t0, a3, e16, m8, ta, ma
        vle16.v v16, (a0)
        sub     a3, a3, t0
        vle16.v v24, (a1)
        sh1add  a1, t0, a1
        vadd.vv v16, v16, v24
        vand.vx v16, v16, a2
        vse16.v v16, (a0)
        sh1add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_add_hfyu_left_pred_bgr32_rvv, zve32x, b
        lpad    0
        vtype_ivli t1, 4, e8, ta, ma
        li      t0, 4
        vsetvl  zero, t0, t1
        vle8.v  v8, (a3)
        sh2add  a2, a2, a1
1:
        vle8.v  v0, (a1)
        vadd.vv v8, v8, v0
        addi    a1, a1, 4
        vse8.v  v8, (a0)
        addi    a0, a0, 4
        bne     a2, a1, 1b

        vse8.v  v8, (a3)
        ret
endfunc
