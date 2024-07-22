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

func ff_vector_fmul_window_scaled_rvv, zve64x, zba
        lpad    0
        csrwi   vxrm, 0
        vsetvli t0, zero, e16, m1, ta, ma
        sh2add  a2, a4, a2
        vid.v   v0
        sh3add  t3, a4, a3
        vadd.vi v0, v0, 1
        sh2add  t0, a4, a0
1:
        vsetvli t2, a4, e16, m1, ta, ma
        slli    t4, t2, 2
        slli    t1, t2, 1
        vrsub.vx v2, v0, t2
        sub     t3, t3, t4
        vsetvli zero, zero, e32, m2, ta, ma
        sub     a2, a2, t4
        vle32.v v8, (t3)
        sub     t0, t0, t1
        vle32.v v4, (a2)
        sub     a4, a4, t2
        vrgatherei16.vv v28, v8, v2
        vle32.v v16, (a1)
        add     a1, a1, t4
        vrgatherei16.vv v20, v4, v2
        vle32.v v24, (a3)
        add     a3, a3, t4
        vwmul.vv v12, v16, v28
        vwmul.vv v8, v16, v24
        // vwnmsac.vv does _not_ exist so multiply & subtract separately
        vwmul.vv v4, v20, v24
        vwmacc.vv v8, v20, v28
        vsetvli zero, zero, e64, m4, ta, ma
        vsub.vv v12, v12, v4
        vsetvli zero, zero, e32, m2, ta, ma
        vnclip.wi v16, v8, 31
        vnclip.wi v20, v12, 31
        vsetvli zero, zero, e16, m1, ta, ma
        vnclip.wx v8, v16, a5
        vnclip.wx v12, v20, a5
        vrgatherei16.vv v16, v8, v2
        vse16.v v12, (a0)
        add     a0, a0, t1
        vse16.v v16, (t0)
        bnez    a4, 1b

        ret
endfunc

func ff_vector_fmul_window_fixed_rvv, zve64x, zba
        lpad    0
        csrwi   vxrm, 0
        vsetvli t0, zero, e16, m1, ta, ma
        sh2add  a2, a4, a2
        vid.v   v0
        sh3add  t3, a4, a3
        vadd.vi v0, v0, 1
        sh3add  t0, a4, a0
1:
        vsetvli t2, a4, e16, m1, ta, ma
        slli    t4, t2, 2
        vrsub.vx v2, v0, t2
        sub     t3, t3, t4
        vsetvli zero, zero, e32, m2, ta, ma
        sub     a2, a2, t4
        vle32.v v8, (t3)
        sub     t0, t0, t4
        vle32.v v4, (a2)
        sub     a4, a4, t2
        vrgatherei16.vv v28, v8, v2
        vle32.v v16, (a1)
        add     a1, a1, t4
        vrgatherei16.vv v20, v4, v2
        vle32.v v24, (a3)
        add     a3, a3, t4
        vwmul.vv v12, v16, v28
        vwmul.vv v8, v16, v24
        // vwnmsac.vv does _not_ exist so multiply & subtract separately
        vwmul.vv v4, v20, v24
        vwmacc.vv v8, v20, v28
        vsetvli zero, zero, e64, m4, ta, ma
        vsub.vv v12, v12, v4
        vsetvli zero, zero, e32, m2, ta, ma
        vnclip.wi v16, v8, 31
        vnclip.wi v20, v12, 31
        vrgatherei16.vv v8, v16, v2
        vse32.v v20, (a0)
        add     a0, a0, t4
        vse32.v v8, (t0)
        bnez    a4, 1b

        ret
endfunc

func ff_vector_fmul_fixed_rvv, zve32x, zba
        lpad    0
        csrwi   vxrm, 0
1:
        vsetvli t0, a3, e32, m4, ta, ma
        vle32.v v16, (a1)
        sub     a3, a3, t0
        vle32.v v24, (a2)
        sh2add  a1, t0, a1
        vsmul.vv v8, v16, v24
        sh2add  a2, t0, a2
        vse32.v v8, (a0)
        sh2add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_vector_fmul_reverse_fixed_rvv, zve32x, zba
        csrwi   vxrm, 0
        // e16/m4 and e32/m8 are possible but slow the gathers down.
        vsetvli t0, zero, e16, m1, ta, ma
        sh2add  a2, a3, a2
        vid.v   v0
        vadd.vi v0, v0, 1
1:
        vsetvli t0, a3, e16, m1, ta, ma
        slli    t1, t0, 2
        vrsub.vx v4, v0, t0 // v4[i] = [VL-1, VL-2... 1, 0]
        sub     a2, a2, t1
        vsetvli zero, zero, e32, m2, ta, ma
        vle32.v v8, (a2)
        sub     a3, a3, t0
        vle32.v v16, (a1)
        add     a1, a1, t1
        vrgatherei16.vv v24, v8, v4 // v24 = reverse(v8)
        vsmul.vv v16, v16, v24
        vse32.v v16, (a0)
        add     a0, a0, t1
        bnez    a3, 1b

        ret
endfunc

func ff_vector_fmul_add_fixed_rvv, zve32x, zba
        lpad    0
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

func ff_scalarproduct_fixed_rvv, zve64x, zba
        lpad    0
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
func ff_butterflies_fixed_rvv, zve32x, zba
        lpad    0
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
