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

func ff_scalarproduct_int16_rvv, zve32x, zba
        lpad    0
        vsetvli     t0, zero, e32, m8, ta, ma
        vmv.v.x     v8, zero
        vmv.s.x     v0, zero
1:
        vsetvli     t0, a2, e16, m4, tu, ma
        vle16.v     v16, (a0)
        sub         a2, a2, t0
        vle16.v     v24, (a1)
        sh1add      a0, t0, a0
        vwmacc.vv   v8, v16, v24
        sh1add      a1, t0, a1
        bnez        a2, 1b

        vsetvli     t0, zero, e32, m8, ta, ma
        vredsum.vs  v0, v8, v0
        vmv.x.s     a0, v0
        ret
endfunc

func ff_vector_clip_int32_rvv, zve32x, zba
1:
        vsetvli t0, a4, e32, m8, ta, ma
        vle32.v v8, (a1)
        sub     a4, a4, t0
        vmax.vx v8, v8, a2
        sh2add  a1, t0, a1
        vmin.vx v8, v8, a3
        vse32.v v8, (a0)
        sh2add  a0, t0, a0
        bnez    a4, 1b

        ret
endfunc

func ff_vector_clipf_rvv, zve32f, zba
        lpad    0
NOHWF   fmv.w.x  fa0, a3
NOHWF   fmv.w.x  fa1, a4
1:
        vsetvli  t0, a2, e32, m8, ta, ma
        vle32.v  v8, (a1)
        sub      a2, a2, t0
        vfmax.vf v8, v8, fa0
        sh2add   a1, t0, a1
        vfmin.vf v8, v8, fa1
        vse32.v  v8, (a0)
        sh2add   a0, t0, a0
        bnez     a2, 1b

        ret
endfunc
