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

#include "config.h"
#include "asm.S"

// (a0) = (a1) * (a2) [0..a3-1]
func ff_vector_fmul_rvv, zve32f
1:
        vsetvli  t0, a3, e32, m1, ta, ma
        vle32.v  v16, (a1)
        sub      a3, a3, t0
        vle32.v  v24, (a2)
        sh2add   a1, t0, a1
        vfmul.vv v16, v16, v24
        sh2add   a2, t0, a2
        vse32.v  v16, (a0)
        sh2add   a0, t0, a0
        bnez     a3, 1b

        ret
endfunc

// (a0) += (a1) * fa0 [0..a2-1]
func ff_vector_fmac_scalar_rvv, zve32f
NOHWF   fmv.w.x   fa0, a2
NOHWF   mv        a2, a3
1:
        vsetvli   t0, a2, e32, m1, ta, ma
        slli      t1, t0, 2
        vle32.v   v24, (a1)
        sub       a2, a2, t0
        vle32.v   v16, (a0)
        sh2add    a1, t0, a1
        vfmacc.vf v16, fa0, v24
        vse32.v   v16, (a0)
        sh2add    a0, t0, a0
        bnez      a2, 1b

        ret
endfunc

// (a0) = (a1) * fa0 [0..a2-1]
func ff_vector_fmul_scalar_rvv, zve32f
NOHWF   fmv.w.x  fa0, a2
NOHWF   mv       a2, a3
1:
        vsetvli  t0, a2, e32, m1, ta, ma
        vle32.v  v16, (a1)
        sub      a2, a2, t0
        vfmul.vf v16, v16, fa0
        sh2add   a1, t0, a1
        vse32.v  v16, (a0)
        sh2add   a0, t0, a0
        bnez     a2, 1b

        ret
endfunc

// (a0) = (a1) * (a2) + (a3) [0..a4-1]
func ff_vector_fmul_add_rvv, zve32f
1:
        vsetvli   t0, a4, e32, m1, ta, ma
        vle32.v   v8, (a1)
        sub       a4, a4, t0
        vle32.v   v16, (a2)
        sh2add    a1, t0, a1
        vle32.v   v24, (a3)
        sh2add    a2, t0, a2
        vfmadd.vv v8, v16, v24
        sh2add    a3, t0, a3
        vse32.v   v8, (a0)
        sh2add    a0, t0, a0
        bnez      a4, 1b

        ret
endfunc

// (a0) = (a0) + (a1), (a1) = (a0) - (a1) [0..a2-1]
func ff_butterflies_float_rvv, zve32f
1:
        vsetvli  t0, a2, e32, m1, ta, ma
        vle32.v  v16, (a0)
        sub      a2, a2, t0
        vle32.v  v24, (a1)
        vfadd.vv v0, v16, v24
        vfsub.vv v8, v16, v24
        vse32.v  v0, (a0)
        sh2add   a0, t0, a0
        vse32.v  v8, (a1)
        sh2add   a1, t0, a1
        bnez     a2, 1b

        ret
endfunc

// (a0) = (a1) * (a2) [0..a3-1]
func ff_vector_dmul_rvv, zve64d
1:
        vsetvli  t0, a3, e64, m1, ta, ma
        vle64.v  v16, (a1)
        sub      a3, a3, t0
        vle64.v  v24, (a2)
        sh3add   a1, t0, a1
        vfmul.vv v16, v16, v24
        sh3add   a2, t0, a2
        vse64.v  v16, (a0)
        sh3add   a0, t0, a0
        bnez     a3, 1b

        ret
endfunc

// (a0) += (a1) * fa0 [0..a2-1]
func ff_vector_dmac_scalar_rvv, zve64d
NOHWD   fmv.d.x   fa0, a2
NOHWD   mv        a2, a3
1:
        vsetvli   t0, a2, e64, m1, ta, ma
        vle64.v   v24, (a1)
        sub       a2, a2, t0
        vle64.v   v16, (a0)
        sh3add    a1, t0, a1
        vfmacc.vf v16, fa0, v24
        vse64.v   v16, (a0)
        sh3add    a0, t0, a0
        bnez      a2, 1b

        ret
endfunc

// (a0) = (a1) * fa0 [0..a2-1]
func ff_vector_dmul_scalar_rvv, zve64d
NOHWD   fmv.d.x  fa0, a2
NOHWD   mv       a2, a3
1:
        vsetvli  t0, a2, e64, m1, ta, ma
        vle64.v  v16, (a1)
        sub      a2, a2, t0
        vfmul.vf v16, v16, fa0
        sh3add   a1, t0, a1
        vse64.v  v16, (a0)
        sh3add   a0, t0, a0
        bnez     a2, 1b

        ret
endfunc
