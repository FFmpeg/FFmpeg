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

func ff_int32_to_float_fmul_scalar_rvv, zve32f, zba
        lpad    0
NOHWF   fmv.w.x     fa0, a2
NOHWF   mv          a2, a3
1:
        vsetvli     t0, a2, e32, m8, ta, ma
        vle32.v     v24, (a1)
        sub         a2, a2, t0
        vfcvt.f.x.v v24, v24
        sh2add      a1, t0, a1
        vfmul.vf    v24, v24, fa0
        vse32.v     v24, (a0)
        sh2add      a0, t0, a0
        bnez        a2, 1b

        ret
endfunc

func ff_int32_to_float_fmul_array8_rvv, zve32f, zba
        lpad    0
        srai        a4, a4, 3

1:      vsetvli     t0, a4, e32, m1, ta, ma
        vle32.v     v24, (a3)
        slli        t2, t0, 2 + 3
        vlseg8e32.v v16, (a2)
        vsetvli     t3, zero, e32, m8, ta, ma
        vfcvt.f.x.v v16, v16
        vsetvli     zero, a4, e32, m1, ta, ma
        vfmul.vv    v16, v16, v24
        sub         a4, a4, t0
        vfmul.vv    v17, v17, v24
        sh2add      a3, t0, a3
        vfmul.vv    v18, v18, v24
        add         a2, a2, t2
        vfmul.vv    v19, v19, v24
        vfmul.vv    v20, v20, v24
        vfmul.vv    v21, v21, v24
        vfmul.vv    v22, v22, v24
        vfmul.vv    v23, v23, v24
        vsseg8e32.v v16, (a1)
        add         a1, a1, t2
        bnez        a4, 1b

        ret
endfunc
