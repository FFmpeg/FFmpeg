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

func ff_scalarproduct_and_madd_int16_rvv, zve32x, zba
        lpad    0
        vsetvli t0, zero, e32, m8, ta, ma
        vmv.v.x v0, zero
1:
        vsetvli t0, a3, e16, m4, tu, ma
        vle16.v v8, (a0)
        sub     a3, a3, t0
        vle16.v v16, (a1)
        sh1add  a1, t0, a1
        vwmacc.vv v0, v8, v16
        vle16.v v24, (a2)
        sh1add  a2, t0, a2
        vmacc.vx v8, a4, v24
        vse16.v v8, (a0)
        sh1add  a0, t0, a0
        bnez    a3, 1b

        vsetvli t0, zero, e32, m8, ta, ma
        vmv.s.x v8, zero
        vredsum.vs v0, v0, v8
        vmv.x.s a0, v0
        ret
endfunc

func ff_scalarproduct_and_madd_int32_rvv, zve32x, zba
        lpad    0
        vsetvli t0, zero, e32, m8, ta, ma
        vmv.v.x v0, zero
1:
        vsetvli t0, a3, e32, m8, tu, ma
        vle16.v v8, (a0)
        sub     a3, a3, t0
        vsext.vf2 v24, v8
        vle32.v v16, (a1)
        sh2add  a1, t0, a1
        vmacc.vv v0, v16, v24
        vsetvli zero, zero, e16, m4, ta, ma
        vle16.v v24, (a2)
        sh1add  a2, t0, a2
        vmacc.vx v8, a4, v24
        vse16.v v8, (a0)
        sh1add  a0, t0, a0
        bnez    a3, 1b

        vsetvli t0, zero, e32, m8, ta, ma
        vmv.s.x v8, zero
        vredsum.vs v0, v0, v8
        vmv.x.s a0, v0
        ret
endfunc
