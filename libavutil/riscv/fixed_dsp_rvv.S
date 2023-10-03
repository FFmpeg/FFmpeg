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

#include "asm.S"


func ff_vector_fmul_add_fixed_rvv, zve32x
        csrwi   vxrm, 0
1:
        vsetvli t0, a4, e32, m8, ta, ma
        vle32.v v16, (a1)
        sub     a4, a4, t0
        vle32.v v24, (a2)
        sh2add  a1, t0, a1
        vsmul.vv v8, v16, v24
        sh2add  a2, t0, a2
        vle32.v v0,(a3)
        sh2add  a3, t0, a3
        vadd.vv v8, v8, v0
        vse32.v v8, (a0)
        sh2add  a0, t0, a0
        bnez    a4, 1b

        ret
endfunc

func ff_scalarproduct_fixed_rvv, zve64x
        li      t1, 1 << 30
        vsetvli t0, zero, e64, m8, ta, ma
        vmv.v.x v8, zero
        vmv.s.x v0, t1
1:
        vsetvli t0, a2, e32, m4, tu, ma
        vle32.v v16, (a0)
        sub     a2, a2, t0
        vle32.v v20, (a1)
        sh2add  a0, t0, a0
        vwmacc.vv v8, v16, v20
        sh2add  a1, t0, a1
        bnez    a2, 1b

        vsetvli t0, zero, e64, m8, ta, ma
        vredsum.vs v0, v8, v0
        vmv.x.s a0, v0
        srai    a0, a0, 31
        ret
endfunc

// (a0) = (a0) + (a1), (a1) = (a0) - (a1) [0..a2-1]
func ff_butterflies_fixed_rvv, zve32x
1:
        vsetvli t0, a2, e32, m4, ta, ma
        vle32.v v16, (a0)
        sub     a2, a2, t0
        vle32.v v24, (a1)
        vadd.vv v0, v16, v24
        vsub.vv v8, v16, v24
        vse32.v v0, (a0)
        sh2add  a0, t0, a0
        vse32.v v8, (a1)
        sh2add  a1, t0, a1
        bnez    a2, 1b

        ret
endfunc
