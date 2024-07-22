/*
 * Copyright © 2022 Rémi Denis-Courmont.
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

func ff_opus_postfilter_rvv, zve32f, b
        lpad    0
        flw     fa0, 0(a2) // g0
        slli    t1, a1, 2
        flw     fa1, 4(a2) // g1
        sub     t0, a0, t1
        flw     fa2, 8(a2) // g2
        addi    t1, t0, -2 * 4 // data - (period + 2) = initial &x4
        vsetivli zero, 4, e32, m4, ta, ma
        addi    t0, t0, 2 * 4 // data - (period - 2) = initial &x0
        vle32.v v16, (t1)
        addi    t3, a1, -2 // maximum parallelism w/o stepping our tail
1:
        vslidedown.vi v8, v16, 2
        min     t1, a3, t3
        vslide1down.vx v12, v16, zero
        vsetvli t1, t1, e32, m4, ta, ma
        vle32.v v0, (t0) // x0
        sub     a3, a3, t1
        vslide1down.vx v4, v8, zero
        sh2add  t0, t1, t0
        vle32.v v28, (a0)
        addi    t2, t1, -4
        vslideup.vi v4, v0, 1
        vslideup.vi v8, v4, 1
        vslideup.vi v12, v8, 1
        vslideup.vi v16, v12, 1
        vfadd.vv v20, v4, v12
        vfadd.vv v24, v0, v16
        vslidedown.vx v16, v0, t2
        vfmacc.vf v28, fa0, v8
        vfmacc.vf v28, fa1, v20
        vfmacc.vf v28, fa2, v24
        vse32.v v28, (a0)
        sh2add  a0, t1, a0
        bnez    a3, 1b

        ret
endfunc
