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

func ff_sbr_sum64x5_rvv, zve32f
        li      a5, 64
        addi    a1, a0, 64 * 4
        addi    a2, a0, 128 * 4
        addi    a3, a0, 192 * 4
        addi    a4, a0, 256 * 4
1:
        vsetvli t0, a5, e32, m8, ta, ma
        sub     a5, a5, t0
        vle32.v v0, (a0)
        vle32.v v8, (a1)
        sh2add  a1, t0, a1
        vle32.v v16, (a2)
        vfadd.vv v0, v0, v8
        sh2add  a2, t0, a2
        vle32.v v24, (a3)
        vfadd.vv v0, v0, v16
        sh2add  a3, t0, a3
        vle32.v v8, (a4)
        vfadd.vv v0, v0, v24
        sh2add  a4, t0, a4
        vfadd.vv v0, v0, v8
        vse32.v v0, (a0)
        sh2add  a0, t0, a0
        bnez    a5, 1b

        ret
endfunc

func ff_sbr_sum_square_rvv, zve32f
        vsetvli t0, zero, e32, m8, ta, ma
        slli    a1, a1, 1
        vmv.v.x v8, zero
        vmv.s.x v0, zero
1:
        vsetvli t0, a1, e32, m8, tu, ma
        vle32.v v16, (a0)
        sub     a1, a1, t0
        vfmacc.vv v8, v16, v16
        sh2add  a0, t0, a0
        bnez    a1, 1b

        vfredusum.vs v0, v8, v0
        vfmv.f.s fa0, v0
NOHWF   fmv.x.w  a0, fa0
        ret
endfunc

#if __riscv_xlen >= 64
func ff_sbr_neg_odd_64_rvv, zve64x
        li      a1, 32
        li      t1, 1 << 63
1:
        vsetvli t0, a1, e64, m8, ta, ma
        vle64.v v8, (a0)
        sub     a1, a1, t0
        vxor.vx v8, v8, t1
        vse64.v v8, (a0)
        sh3add  a0, t0, a0
        bnez    t0, 1b

        ret
endfunc
#endif
