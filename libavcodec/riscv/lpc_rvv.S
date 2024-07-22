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

#if __riscv_xlen >= 64
func ff_lpc_apply_welch_window_rvv, zve64d, zba
        lpad    0
        vsetvli   t0, zero, e64, m8, ta, ma
        vid.v     v0
        addi      t2, a1, -1
        vfcvt.f.xu.v v0, v0
        li        t3, 2
        fcvt.d.l  ft2, t2
        srai      t1, a1, 1
        fcvt.d.l  ft3, t3
        li        t4, 1
        fdiv.d    ft0, ft3, ft2    # ft0 = c = 2. / (len - 1)
        fcvt.d.l  fa1, t4         # fa1 = 1.
        fsub.d    ft1, ft0, fa1
        vfrsub.vf v0, v0, ft1    # v0[i] = c - i - 1.
1:
        vsetvli   t0, t1, e64, m8, ta, ma
        vfmul.vv  v16, v0, v0  # no fused multipy-add as v0 is reused
        sub       t1, t1, t0
        vle32.v   v8, (a0)
        fcvt.d.l  ft2, t0
        vfrsub.vf v16, v16, fa1  # v16 = 1. - w * w
        sh2add    a0, t0, a0
        vsetvli   zero, zero, e32, m4, ta, ma
        vfwcvt.f.x.v v24, v8
        vsetvli   zero, zero, e64, m8, ta, ma
        vfsub.vf  v0, v0, ft2     # v0 -= vl
        vfmul.vv  v8, v24, v16
        vse64.v   v8, (a2)
        sh3add    a2, t0, a2
        bnez      t1, 1b

        andi      t1, a1, 1
        beqz      t1, 2f

        sd        zero, (a2)
        addi      a0, a0, 4
        addi      a2, a2, 8
2:
        vsetvli   t0, zero, e64, m8, ta, ma
        vid.v     v0
        srai      t1, a1, 1
        vfcvt.f.xu.v v0, v0
        fcvt.d.l  ft1, t1
        fsub.d    ft1, ft0, ft1    # ft1 = c - (len / 2)
        vfadd.vf  v0, v0, ft1     # v0[i] = c - (len / 2) + i
3:
        vsetvli   t0, t1, e64, m8, ta, ma
        vfmul.vv  v16, v0, v0
        sub       t1, t1, t0
        vle32.v   v8, (a0)
        fcvt.d.l  ft2, t0
        vfrsub.vf v16, v16, fa1  # v16 = 1. - w * w
        sh2add    a0, t0, a0
        vsetvli   zero, zero, e32, m4, ta, ma
        vfwcvt.f.x.v v24, v8
        vsetvli   zero, zero, e64, m8, ta, ma
        vfadd.vf  v0, v0, ft2     # v0 += vl
        vfmul.vv  v8, v24, v16
        vse64.v   v8, (a2)
        sh3add    a2, t0, a2
        bnez      t1, 3b

        ret
endfunc

func ff_lpc_compute_autocorr_rvv, zve64d, b
        lpad    0
        vtype_vli t1, a2, t2, e64, ta, ma, 1
        addi      a2, a2, 1
        li        t0, 1
        vsetvl    zero, a2, t1
        fcvt.d.l  ft0, t0
        vle64.v   v0, (a0)
        sh3add    a0, a2, a0   # data += lag
        vfmv.v.f  v16, ft0
        bge       a2, a1, 2f
1:
        vfmv.f.s  ft0, v0
        fld       ft1, (a0)    # ft1 = data[lag + i]
        vfmacc.vf v16, ft0, v0 # v16[j] += data[i] * data[i + j]
        addi      a1, a1, -1
        vfslide1down.vf v0, v0, ft1
        addi      a0, a0, 8
        bgt       a1, a2, 1b   # while (len > lag);
2:
        vfmv.f.s  ft0, v0
        vsetvli   zero, a1, e64, m8, tu, ma
        vfmacc.vf v16, ft0, v0
        addi      a1, a1, -1
        vslide1down.vx v0, v0, zero
        bnez      a1, 2b       # while (len > 0);

        vsetvli   zero, a2, e64, m8, ta, ma
        vse64.v   v16, (a3)
        ret
endfunc
#endif
