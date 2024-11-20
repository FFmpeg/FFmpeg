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

// (a0) = (a1) * (a2) [0..a3-1]
func ff_vector_fmul_rvv, zve32f, zba
        lpad    0
1:
        vsetvli  t0, a3, e32, m8, ta, ma
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
func ff_vector_fmac_scalar_rvv, zve32f, zba
        lpad    0
NOHWF   fmv.w.x   fa0, a2
NOHWF   mv        a2, a3
1:
        vsetvli   t0, a2, e32, m8, ta, ma
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
func ff_vector_fmul_scalar_rvv, zve32f, zba
        lpad    0
NOHWF   fmv.w.x  fa0, a2
NOHWF   mv       a2, a3
1:
        vsetvli  t0, a2, e32, m8, ta, ma
        vle32.v  v16, (a1)
        sub      a2, a2, t0
        vfmul.vf v16, v16, fa0
        sh2add   a1, t0, a1
        vse32.v  v16, (a0)
        sh2add   a0, t0, a0
        bnez     a2, 1b

        ret
endfunc

func ff_vector_fmul_window_rvv, zve32f, zba
        lpad    0
        // a0: dst, a1: src0, a2: src1, a3: window, a4: length
        // e16/m2 and e32/m4 are possible but slower due to gather.
        vsetvli    t0, zero, e16, m1, ta, ma
        sh2add     a2, a4, a2
        vid.v      v0
        sh3add     t3, a4, a3
        vadd.vi    v0, v0, 1
        sh3add     t0, a4, a0
1:
        vsetvli    t2, a4, e16, m1, ta, ma
        slli       t4, t2, 2
        vrsub.vx   v2, v0, t2
        sub        t3, t3, t4
        vsetvli    zero, zero, e32, m2, ta, ma
        sub        a2, a2, t4
        vle32.v    v8, (t3)
        sub        t0, t0, t4
        vle32.v    v4, (a2)
        sub        a4, a4, t2
        vrgatherei16.vv v28, v8, v2
        vle32.v    v16, (a1)
        add        a1, a1, t4
        vrgatherei16.vv v20, v4, v2
        vle32.v    v24, (a3)
        add        a3, a3, t4
        vfmul.vv   v12, v16, v28
        vfmul.vv   v16, v16, v24
        vfnmsac.vv v12, v20, v24
        vfmacc.vv  v16, v20, v28
        vrgatherei16.vv v8, v16, v2
        vse32.v    v12, (a0)
        add        a0, a0, t4
        vse32.v    v8, (t0)
        bnez       a4, 1b

        ret
endfunc

// (a0) = (a1) * (a2) + (a3) [0..a4-1]
func ff_vector_fmul_add_rvv, zve32f, zba
        lpad    0
1:
        vsetvli   t0, a4, e32, m8, ta, ma
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

// TODO factor vrsub, separate last iteration?
// (a0) = (a1) * reverse(a2) [0..a3-1]
func ff_vector_fmul_reverse_rvv, zve32f, zba
        lpad    0
        // e16/m4 and e32/m8 are possible but slower due to gather.
        vsetvli  t0, zero, e16, m1, ta, ma
        sh2add   a2, a3, a2
        vid.v    v0
        vadd.vi  v0, v0, 1
1:
        vsetvli  t0, a3, e16, m1, ta, ma
        slli     t1, t0, 2
        vrsub.vx v4, v0, t0 // v4[i] = [VL-1, VL-2... 1, 0]
        sub      a2, a2, t1
        vsetvli  zero, zero, e32, m2, ta, ma
        vle32.v  v8, (a2)
        sub      a3, a3, t0
        vle32.v  v16, (a1)
        add      a1, a1, t1
        vrgatherei16.vv v24, v8, v4 // v24 = reverse(v8)
        vfmul.vv v16, v16, v24
        vse32.v  v16, (a0)
        add      a0, a0, t1
        bnez     a3, 1b

        ret
endfunc

// (a0) = (a0) + (a1), (a1) = (a0) - (a1) [0..a2-1]
func ff_butterflies_float_rvv, zve32f, zba
        lpad    0
1:
        vsetvli  t0, a2, e32, m8, ta, ma
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

// a0 = (a0).(a1) [0..a2-1]
func ff_scalarproduct_float_rvv, zve32f, zba
        lpad    0
        vsetvli      t0, zero, e32, m8, ta, ma
        vmv.v.x      v8, zero
        vmv.s.x      v0, zero
1:
        vsetvli      t0, a2, e32, m8, tu, ma
        vle32.v      v16, (a0)
        sub          a2, a2, t0
        vle32.v      v24, (a1)
        sh2add       a0, t0, a0
        vfmacc.vv    v8, v16, v24
        sh2add       a1, t0, a1
        bnez         a2, 1b

        vsetvli      t0, zero, e32, m8, ta, ma
        vfredusum.vs v0, v8, v0
        vfmv.f.s     fa0, v0
NOHWF   fmv.x.w  a0, fa0
        ret
endfunc

// (a0) = (a1) * (a2) [0..a3-1]
func ff_vector_dmul_rvv, zve64d, zba
        lpad    0
1:
        vsetvli  t0, a3, e64, m8, ta, ma
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
func ff_vector_dmac_scalar_rvv, zve64d, zba
        lpad    0
#if __riscv_xlen >= 64
NOHWD   fmv.d.x   fa0, a2
#else
NOHWD   addi    sp, sp, -16
NOHWD   sw      a0, 0(sp)
NOHWD   sw      a1, 4(sp)
NOHWD   fld     fa0, (sp)
NOHWD   addi    sp, sp, 16
#endif
NOHWD   mv        a2, a3
1:
        vsetvli   t0, a2, e64, m8, ta, ma
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
func ff_vector_dmul_scalar_rvv, zve64d, zba
        lpad    0
#if __riscv_xlen >= 64
NOHWD   fmv.d.x  fa0, a2
#else
NOHWD   addi    sp, sp, -16
NOHWD   sw      a0, 0(sp)
NOHWD   sw      a1, 4(sp)
NOHWD   fld     fa0, (sp)
NOHWD   addi    sp, sp, 16
#endif
NOHWD   mv       a2, a3
1:
        vsetvli  t0, a2, e64, m8, ta, ma
        vle64.v  v16, (a1)
        sub      a2, a2, t0
        vfmul.vf v16, v16, fa0
        sh3add   a1, t0, a1
        vse64.v  v16, (a0)
        sh3add   a0, t0, a0
        bnez     a2, 1b

        ret
endfunc

func ff_scalarproduct_double_rvv, zve64f, zba
        lpad    0
        vsetvli      t0, zero, e64, m8, ta, ma
        vmv.v.x      v8, zero
        vmv.s.x      v0, zero
1:
        vsetvli      t0, a2, e64, m8, tu, ma
        vle64.v      v16, (a0)
        sub          a2, a2, t0
        vle64.v      v24, (a1)
        sh3add       a0, t0, a0
        vfmacc.vv    v8, v16, v24
        sh3add       a1, t0, a1
        bnez         a2, 1b

        vsetvli      t0, zero, e64, m8, ta, ma
        vfredusum.vs v0, v8, v0
        vfmv.f.s     fa0, v0
NOHWD   fmv.x.w      a0, fa0
        ret
endfunc
