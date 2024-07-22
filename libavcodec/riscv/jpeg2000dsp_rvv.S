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

func ff_ict_float_rvv, zve32f, zba
        lpad    0
        lla     t0, ff_jpeg2000_f_ict_params
        flw     ft0,  0(t0)
        flw     ft1,  4(t0)
        flw     ft2,  8(t0)
        flw     ft3, 12(t0)
1:
        vsetvli t0, a3, e32, m8, ta, ma
        vle32.v v8, (a0)
        sub     a3, a3, t0
        vle32.v v16, (a1)
        vle32.v v24, (a2)
        vfmul.vf v0, v16, ft1
        vfmadd.vf v16, ft3, v8
        vfsub.vv v0, v8, v0
        vfmacc.vf v8, ft0, v24
        vfnmsac.vf v0, ft2, v24
        vse32.v v8, (a0)
        sh2add  a0, t0, a0
        vse32.v v0, (a1)
        sh2add  a1, t0, a1
        vse32.v v16, (a2)
        sh2add  a2, t0, a2
        bnez    a3, 1b

        ret
endfunc

func ff_rct_int_rvv, zve32x, zba
        lpad    0
1:
        vsetvli t0, a3, e32, m8, ta, ma
        vle32.v v16, (a1)
        sub     a3, a3, t0
        vle32.v v24, (a2)
        vle32.v v8, (a0)
        vadd.vv v0, v16, v24
        vsra.vi v0, v0, 2
        vsub.vv v0, v8, v0
        vadd.vv v8, v0, v24
        vadd.vv v24, v0, v16
        vse32.v v8, (a0)
        sh2add  a0, t0, a0
        vse32.v v0, (a1)
        sh2add  a1, t0, a1
        vse32.v v24, (a2)
        sh2add  a2, t0, a2
        bnez    a3, 1b

        ret
endfunc
