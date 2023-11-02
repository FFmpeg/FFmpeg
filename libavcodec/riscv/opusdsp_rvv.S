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

func ff_opus_postfilter_rvv, zve32f
        flw     fa0, 0(a2) // g0
        slli    t1, a1, 2
        flw     fa1, 4(a2) // g1
        sub     t0, a0, t1
        flw     fa2, 8(a2) // g2
        addi    t0, t0, 2 * 4 // data - (period - 2) = initial &x0

        flw     ft4, -16(t0)
        addi    t3, a1, -2 // maximum parallelism w/o stepping our tail
        flw     ft3, -12(t0)
        flw     ft2,  -8(t0)
        flw     ft1,  -4(t0)
1:
        min     t1, a3, t3
        vsetvli t1, t1, e32, m4, ta, ma
        vle32.v v0, (t0) // x0
        sub     a3, a3, t1
        vle32.v v28, (a0)
        sh2add  t0, t1, t0
        vfslide1up.vf v4, v0, ft1
        addi    t2, t1, -4
        vfslide1up.vf v8, v4, ft2
        vfslide1up.vf v12, v8, ft3
        vfslide1up.vf v16, v12, ft4
        vfadd.vv v20, v4, v12
        vfadd.vv v24, v0, v16
        vslidedown.vx v12, v0, t2
        vfmacc.vf v28, fa0, v8
        vslidedown.vi v4, v12, 2
        vfmacc.vf v28, fa1, v20
        vslide1down.vx v8, v12, zero
        vfmacc.vf v28, fa2, v24
        vslide1down.vx v0, v4, zero
        vse32.v v28, (a0)
        vfmv.f.s ft4, v12
        sh2add  a0, t1, a0
        vfmv.f.s ft2, v4
        vfmv.f.s ft3, v8
        vfmv.f.s ft1, v0
        bnez    a3, 1b

        ret
endfunc
