/*
 * Copyright © 2024 Rémi Denis-Courmont.
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

func ff_h263_dct_unquantize_intra_rvv
        lpad    0
        addi    a1, a1, 2
        beqz    a4, 1f
        slli    a3, a3, 1
        mv      a4, zero
        j       2f
endfunc

func ff_h263_dct_unquantize_inter_rvv, zve32x, zba
        lpad    0
        addi    a2, a2, 1
1:
        addi    a4, a3, -1
        slli    a3, a3, 1
        ori     a4, a4, 1
2:
        vsetvli t0, a2, e16, m8, ta, mu
        vle16.v v8, (a1)
        sub     a2, a2, t0
        vmv.v.x v16, a4
        vmslt.vi    v0, v8, 0
        vneg.v  v16, v16, v0.t
        vmsne.vi    v0, v8, 0
        vmadd.vx    v8, a3, v16, v0.t
        vse16.v v8, (a1)
        sh1add  a1, t0, a1
        bnez    a2, 2b

        ret
endfunc
