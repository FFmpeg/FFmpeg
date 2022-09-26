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

func ff_ps_add_squares_rvv, zve32f
1:
        vsetvli     t0, a2, e32, m1, ta, ma
        vlseg2e32.v v24, (a1)
        sub         a2, a2, t0
        vle32.v     v16, (a0)
        sh3add      a1, t0, a1
        vfmacc.vv   v16, v24, v24
        vfmacc.vv   v16, v25, v25
        vse32.v     v16, (a0)
        sh2add      a0, t0, a0
        bnez        a2, 1b

        ret
endfunc

func ff_ps_mul_pair_single_rvv, zve32f
1:
        vsetvli     t0, a3, e32, m1, ta, ma
        vlseg2e32.v v24, (a1)
        sub         a3, a3, t0
        vle32.v     v16, (a2)
        sh3add      a1, t0, a1
        vfmul.vv    v24, v24, v16
        sh2add      a2, t0, a2
        vfmul.vv    v25, v25, v16
        vsseg2e32.v v24, (a0)
        sh3add      a0, t0, a0
        bnez        a3, 1b

        ret
endfunc
