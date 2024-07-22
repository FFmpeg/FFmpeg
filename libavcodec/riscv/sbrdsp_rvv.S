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

func ff_sbr_sum64x5_rvv, zve32f, zba
        lpad    0
        li      a5, 64
        addi    a1, a0, 64 * 4
        addi    a2, a0, 128 * 4
        addi    a3, a0, 192 * 4
        addi    a4, a0, 256 * 4
1:
        vsetvli t0, a5, e32, m8, ta, ma
        sub     a5, a5, t0
        vle32.v v0, (a0)
        vle32.v v8, (a1)
        sh2add  a1, t0, a1
        vle32.v v16, (a2)
        vfadd.vv v0, v0, v8
        sh2add  a2, t0, a2
        vle32.v v24, (a3)
        vfadd.vv v0, v0, v16
        sh2add  a3, t0, a3
        vle32.v v8, (a4)
        vfadd.vv v0, v0, v24
        sh2add  a4, t0, a4
        vfadd.vv v0, v0, v8
        vse32.v v0, (a0)
        sh2add  a0, t0, a0
        bnez    a5, 1b

        ret
endfunc

func ff_sbr_sum_square_rvv, zve32f, zba
        lpad    0
        vsetvli t0, zero, e32, m8, ta, ma
        slli    a1, a1, 1
        vmv.v.x v8, zero
        vmv.s.x v0, zero
1:
        vsetvli t0, a1, e32, m8, tu, ma
        vle32.v v16, (a0)
        sub     a1, a1, t0
        vfmacc.vv v8, v16, v16
        sh2add  a0, t0, a0
        bnez    a1, 1b

        vfredusum.vs v0, v8, v0
        vfmv.f.s fa0, v0
NOHWF   fmv.x.w  a0, fa0
        ret
endfunc

func ff_sbr_autocorrelate_rvv, zve32f
        lpad    0
        vsetvli t0, zero, e32, m4, ta, ma
        vmv.v.x v0, zero
        flw     fa0,   (a0)
        vmv.v.x v4, zero
        flw     fa1,  4(a0)
        vmv.v.x v8, zero
        flw     fa2,  8(a0)
        li      a2, 37
        flw     fa3, 12(a0)
        fmul.s  ft10, fa0, fa0
        flw     fa4, 16(a0)
        fmul.s  ft6, fa0, fa2
        flw     fa5, 20(a0)
        addi    a0, a0, 38 * 8
        fmul.s  ft7, fa0, fa3
        fmul.s  ft2, fa0, fa4
        fmul.s  ft3, fa0, fa5
        flw     fa0,   (a0)
        fmadd.s ft10, fa1, fa1, ft10
        fmadd.s ft6, fa1, fa3, ft6
        flw     fa3, 12(a0)
        fnmsub.s ft7, fa1, fa2, ft7
        flw     fa2,  8(a0)
        fmadd.s ft2, fa1, fa5, ft2
        fnmsub.s ft3, fa1, fa4, ft3
        flw     fa1,  4(a0)
        fmul.s  ft4, fa0, fa0
        fmul.s  ft0, fa0, fa2
        fmul.s  ft1, fa0, fa3
        fmadd.s ft4, fa1, fa1, ft4
        fmadd.s ft0, fa1, fa3, ft0
        fnmsub.s ft1, fa1, fa2, ft1
1:
        vsetvli t0, a2, e32, m2, tu, ma
        slli    t1, t0, 3
        sub     a0, a0, t1
        vlseg2e32.v v16, (a0)
        sub     a2, a2, t0
        vfmacc.vv v0, v16, v16
        vfslide1down.vf v20, v16, fa0
        vfmacc.vv v4, v16, v20
        vfslide1down.vf v22, v18, fa1
        vfmacc.vv v0, v18, v18
        vfslide1down.vf v24, v20, fa2
        vfmacc.vv v4, v18, v22
        vfslide1down.vf v26, v22, fa3
        vfmacc.vv v6, v16, v22
        vfmv.f.s fa0, v16
        vfmacc.vv v8, v16, v24
        vfmv.f.s fa1, v18
        vfmacc.vv v10, v16, v26
        vfmv.f.s fa2, v20
        vfnmsac.vv v6, v18, v20
        vfmv.f.s fa3, v22
        vfmacc.vv v8, v18, v26
        vfnmsac.vv v10, v18, v24
        bnez    a2, 1b

        vsetvli t0, zero, e32, m2, ta, ma
        vfredusum.vs v0, v0, v2
        vfredusum.vs v4, v4, v2
        vfmv.f.s fa0, v0
        vfredusum.vs v6, v6, v2
        vfmv.f.s fa2, v4
        fadd.s   ft4, ft4, fa0
        vfredusum.vs v8, v8, v2
        vfmv.f.s fa3, v6
        fadd.s   ft0, ft0, fa2
        vfredusum.vs v10, v10, v2
        vfmv.f.s fa4, v8
        fadd.s   ft1, ft1, fa3
        vfmv.f.s fa5, v10
        fsw     ft0,   (a1)
        fadd.s  ft2, ft2, fa4
        fsw     ft1,  4(a1)
        fadd.s  ft3, ft3, fa5
        fsw     ft2,  8(a1)
        fadd.s  ft6, ft6, fa2
        fsw     ft3, 12(a1)
        fadd.s  ft7, ft7, fa3
        fsw     ft4, 16(a1)
        fadd.s  ft10, ft10, fa0
        fsw     ft6, 24(a1)
        fsw     ft7, 28(a1)
        fsw     ft10, 40(a1)
        ret
endfunc

func ff_sbr_hf_gen_rvv, zve32f, zba
        lpad    0
NOHWF   fmv.w.x fa0, a4
NOHWF   mv      a4, a5
NOHWF   mv      a5, a6
        flw     ft2, 0(a2)
        fmul.s  fa1, fa0, fa0 // bw * bw
        sh3add  a1, a5, a1
        flw     ft3, 4(a2)
        fmul.s  fa2, ft2, fa0 // alpha[2]
        sh3add  a0, a5, a0
        flw     ft0, 0(a3)
        fmul.s  fa3, ft3, fa0 // alpha[3]
        sub     a5, a5, a4
        flw     ft1, 4(a3)
        fmul.s  fa0, ft0, fa1 // alpha[0]
        flw     ft0, -16(a1)  // X_low[end - 2][0]
        fmul.s  fa1, ft1, fa1 // alpha[1]
        flw     ft1, -12(a1)  // X_low[end - 2][1]
        flw     ft2,  -8(a1)  // X_low[end - 1][0]
        flw     ft3,  -4(a1)  // X_low[end - 1][1]
        addi    a1, a1, -16
1:
        vsetvli t0, a5, e32, m4, ta, ma
        slli    t1, t0, 3
        sub     a1, a1, t1
        vlseg2e32.v v0, (a1)          // X_low[i - 2]
        sub     a0, a0, t1
        vfslide1down.vf v8, v0, ft0   // X_low[i - 1][0]
        sub     a5, a5, t0
        vfslide1down.vf v12, v4, ft1  // X_low[i - 1][1]
        vfslide1down.vf v16, v8, ft2  // X_low[i    ][0]
        vfslide1down.vf v20, v12, ft3 // X_low[i    ][1]
        vfmacc.vf v16, fa0, v0
        vfmacc.vf v20, fa0, v4
        vfmv.f.s ft0, v0
        vfnmsac.vf v16, fa1, v4
        vfmacc.vf v20, fa1, v0
        vfmv.f.s ft1, v4
        vfmacc.vf v16, fa2, v8
        vfmacc.vf v20, fa2, v12
        vfmv.f.s ft2, v8
        vfnmsac.vf v16, fa3, v12
        vfmacc.vf v20, fa3, v8
        vfmv.f.s ft3, v12
        vsseg2e32.v v16, (a0)
        bnez    a5, 1b

        ret
endfunc

func ff_sbr_hf_g_filt_rvv, zve32f, zba
        lpad    0
        li      t1, 40 * 2 * 4
        sh3add  a1, a4, a1
1:
        vsetvli t0, a3, e32, m4, ta, ma
        vlsseg2e32.v v16, (a1), t1
        mul     t2, t0, t1
        vle32.v v8, (a2)
        sub     a3, a3, t0
        vfmul.vv v16, v16, v8
        add     a1, t2, a1
        vfmul.vv v20, v20, v8
        sh2add  a2, t0, a2
        vsseg2e32.v v16, (a0)
        sh3add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

.macro hf_apply_noise n
        lla     a6, ff_sbr_noise_table + 8
        fmv.s.x ft0, zero
1:
.if \n & 1
        min     t0, t0, a5 // preserve parity of t0 for v4 sign injector
        vsetvli zero, t0, e32, m4, ta, mu
.else
        vsetvli t0, a5, e32, m4, ta, mu
.endif
        sh3add  t6, a3, a6
        vle32.v v8, (a1)      // s_m
        sub     a5, a5, t0
        vle32.v v12, (a2)     // q_filt
        sh2add  a1, t0, a1
        vmfeq.vf v0, v8, ft0  // s_m == 0.f
        vlseg2e32.v v24, (t6) // ff_sbr_noise_table
        sh2add  a2, t0, a2
.if \n == 2
        vfneg.v v8, v8
.endif
.if \n & 1
        vfsgnjx.vv v8, v8, v4 // could equivalent use vxor.vv
.endif
        add     a3, t0, a3
        vlseg2e32.v v16, (a0) // Y
        andi    a3, a3, 0x1ff
.if \n & 1
        vfmul.vv v28, v12, v28
        vfmacc.vv v16, v12, v24, v0.t
        vmerge.vvm v28, v8, v28, v0
        vfadd.vv v20, v20, v28
.else
        vfmul.vv v24, v12, v24
        vfmacc.vv v20, v12, v28, v0.t
        vmerge.vvm v24, v8, v24, v0
        vfadd.vv v16, v16, v24
.endif
        vsseg2e32.v v16, (a0)
        sh3add  a0, t0, a0
        bnez    a5, 1b

        ret
.endm

func ff_sbr_hf_apply_noise_0_rvv, zve32f, b
        lpad    0
        hf_apply_noise 0
endfunc

func ff_sbr_hf_apply_noise_3_rvv, zve32f, b
        lpad    0
       not     a4, a4 // invert parity of kx
       // fall through
endfunc

func ff_sbr_hf_apply_noise_1_rvv, zve32f, b
        lpad    0
        vsetvli t0, zero, e32, m4, ta, ma
        vid.v   v4
        vxor.vx v4, v4, a4
        vsll.vi v4, v4, 31 // v4[i] = (kx & 1) ? -0.f : +0.f
        hf_apply_noise 1
endfunc

func ff_sbr_hf_apply_noise_2_rvv, zve32f, b
        lpad    0
        hf_apply_noise 2
endfunc
