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

func ff_flac_lpc16_rvv, zve32x, b
        lpad    0
        vtype_vli t0, a2, t2, e32, ta, ma
        vsetvl  zero, a2, t0
        vle32.v v8, (a1)
        sub     a4, a4, a2
        vle32.v v16, (a0)
        sh2add  a0, a2, a0
        vmv.s.x v0, zero
1:
        vmul.vv v24, v8, v16
        lw      t0, (a0)
        vredsum.vs v24, v24, v0
        addi    a4, a4, -1
        vmv.x.s t1, v24
        sra     t1, t1, a3
        add     t0, t0, t1
        vslide1down.vx v16, v16, t0
        sw      t0, (a0)
        addi    a0, a0, 4
        bnez    a4, 1b

        ret
endfunc

#if (__riscv_xlen == 64)
func ff_flac_lpc32_rvv, zve64x, zba
        lpad    0
        addi    t2, a2, -16
        ble     t2, zero, ff_flac_lpc32_rvv_simple
        vsetivli zero, 1, e64, m1, ta, ma
        vmv.s.x v0, zero
        vsetvli zero, a2, e32, m8, ta, ma
        vle32.v v8, (a1)
        sub     a4, a4, a2
        vle32.v v16, (a0)
        sh2add  a0, a2, a0
1:
        vsetvli t1, zero, e32, m4, ta, ma
        vwmul.vv v24, v8, v16
        vsetvli zero, t2, e32, m4, tu, ma
        vwmacc.vv v24, v12, v20
        vsetvli t1, zero, e64, m8, ta, ma
        vredsum.vs v24, v24, v0
        lw      t0, (a0)
        addi    a4, a4, -1
        vmv.x.s t1, v24
        vsetvli zero, a2, e32, m8, ta, ma
        sra     t1, t1, a3
        add     t0, t0, t1
        vslide1down.vx v16, v16, t0
        sw      t0, (a0)
        addi    a0, a0, 4
        bnez    a4, 1b

        ret
endfunc

func ff_flac_lpc32_rvv_simple, zve64x, b
        lpad    0
        vtype_vli t3, a2, t1, e64, ta, ma
        vntypei t2, t3
        vsetvl  zero, a2, t3 // e64
        vmv.s.x v0, zero
        vsetvl  zero, zero, t2 // e32
        vle32.v v8, (a1)
        sub     a4, a4, a2
        vle32.v v16, (a0)
        sh2add  a0, a2, a0
1:
        vwmul.vv v24, v8, v16
        vsetvl  zero, zero, t3 // e64
        vredsum.vs v24, v24, v0
        lw      t0, (a0)
        addi    a4, a4, -1
        vmv.x.s t1, v24
        vsetvl  zero, zero, t2 // e32
        sra     t1, t1, a3
        add     t0, t0, t1
        vslide1down.vx v16, v16, t0
        sw      t0, (a0)
        addi    a0, a0, 4
        bnez    a4, 1b

        ret
endfunc

func ff_flac_lpc33_rvv, zve64x, b
        lpad    0
        vtype_vli t0, a3, t1, e64, ta, ma
        vsetvl  zero, a3, t0
        vmv.s.x v0, zero
        sh2add  a1, a3, a1
        vle32.v v24, (a2)
        sub     a5, a5, a3
        vle64.v v16, (a0)
        sh3add  a0, a3, a0
        vsext.vf2 v8, v24
1:
        vmul.vv v24, v8, v16
        lw      t0, (a1)
        addi    a1, a1, 4
        vredsum.vs v24, v24, v0
        addi    a5, a5, -1
        vmv.x.s t1, v24
        sra     t1, t1, a4
        add     t0, t0, t1
        vslide1down.vx v16, v16, t0
        sd      t0, (a0)
        addi    a0, a0, 8
        bnez    a5, 1b

        ret
endfunc
#endif

func ff_flac_wasted32_rvv, zve32x, zba
        lpad    0
1:
        vsetvli t0, a2, e32, m8, ta, ma
        vle32.v v8, (a0)
        sub     a2, a2, t0
        vsll.vx v8, v8, a1
        vse32.v v8, (a0)
        sh2add  a0, t0, a0
        bnez    a2, 1b

        ret
endfunc

func ff_flac_wasted33_rvv, zve64x, zba
        lpad    0
        srli         t0, a2, 5
        li           t1, 1
        bnez         t0, 2f
        sll          a2, t1, a2
1:
        vsetvli      t0, a3, e32, m4, ta, ma
        vle32.v      v8, (a1)
        sub          a3, a3, t0
        vwmulsu.vx   v16, v8, a2
        sh2add       a1, t0, a1
        vse64.v      v16, (a0)
        sh3add       a0, t0, a0
        bnez         a3, 1b

        ret

2:      // Pessimistic case: wasted >= 32
        vsetvli      t0, a3, e64, m8, ta, ma
        vle32.v      v8, (a1)
        sub          a3, a3, t0
        vsext.vf2    v16, v8
        sh2add       a1, t0, a1
        vsll.vx      v16, v16, a2
        vse64.v      v16, (a0)
        sh3add       a0, t0, a0
        bnez         a3, 2b

        ret
endfunc

#if (__riscv_xlen == 64)
func ff_flac_decorrelate_indep2_16_rvv, zve32x, zba
        lpad    0
        ld      a0,  (a0)
        ld      a2, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a3, e32, m8, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v8, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vsll.vx v8, v8, a4
        vsetvli zero, zero, e16, m4, ta, ma
        vncvt.x.x.w v16, v0
        vncvt.x.x.w v20, v8
        vsseg2e16.v v16, (a0)
        sh2add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_indep4_16_rvv, zve32x, zba
        lpad    0
        ld      a0,   (a0)
        ld      a2,  8(a1)
        ld      t1, 16(a1)
        ld      t2, 24(a1)
        ld      a1,   (a1)
1:
        vsetvli t0, a3, e32, m4, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v4, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vle32.v v8, (t1)
        sh2add  t1, t0, t1
        vsll.vx v4, v4, a4
        vle32.v v12, (t2)
        sh2add  t2, t0, t2
        vsll.vx v8, v8, a4
        vsll.vx v12, v12, a4
        vsetvli zero, zero, e16, m2, ta, ma
        vncvt.x.x.w v16, v0
        vncvt.x.x.w v18, v4
        vncvt.x.x.w v20, v8
        vncvt.x.x.w v22, v12
        vsseg4e16.v v16, (a0)
        sh3add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_indep6_16_rvv, zve32x, zba
        lpad    0
        ld      a0,   (a0)
        ld      a2,  8(a1)
        ld      t1, 16(a1)
        ld      t2, 24(a1)
        ld      t3, 32(a1)
        ld      t4, 40(a1)
        ld      a1,   (a1)
1:
        vsetvli t0, a3, e32, m2, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v2, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vle32.v v4, (t1)
        sh2add  t1, t0, t1
        vsll.vx v2, v2, a4
        vle32.v v6, (t2)
        sh2add  t2, t0, t2
        vsll.vx v4, v4, a4
        vle32.v v8, (t3)
        sh2add  t3, t0, t3
        vsll.vx v6, v6, a4
        vle32.v v10, (t4)
        sh2add  t4, t0, t4
        vsll.vx v8, v8, a4
        slli    t0, t0, 2
        vsll.vx v10, v10, a4
        sh1add  t0, t0, t0 // t0 *= 3
        vsetvli zero, zero, e16, m1, ta, ma
        vncvt.x.x.w v16, v0
        vncvt.x.x.w v17, v2
        vncvt.x.x.w v18, v4
        vncvt.x.x.w v19, v6
        vncvt.x.x.w v20, v8
        vncvt.x.x.w v21, v10
        vsseg6e16.v v16, (a0)
        add     a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_indep8_16_rvv, zve32x, zba
        lpad    0
        ld      a0,   (a0)
        ld      a2,  8(a1)
        ld      t1, 16(a1)
        ld      t2, 24(a1)
        ld      t3, 32(a1)
        ld      t4, 40(a1)
        ld      t5, 48(a1)
        ld      t6, 56(a1)
        ld      a1,   (a1)
1:
        vsetvli t0, a3, e32, m2, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v2, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        vle32.v v4, (t1)
        sh2add  a2, t0, a2
        vsll.vx v2, v2, a4
        sh2add  t1, t0, t1
        vle32.v v6, (t2)
        vsll.vx v4, v4, a4
        sh2add  t2, t0, t2
        vle32.v v8, (t3)
        sh2add  t3, t0, t3
        vsll.vx v6, v6, a4
        vle32.v v10, (t4)
        sh2add  t4, t0, t4
        vsll.vx v8, v8, a4
        vle32.v v12, (t5)
        sh2add  t5, t0, t5
        vsll.vx v10, v10, a4
        vle32.v v14, (t6)
        sh2add  t6, t0, t6
        vsll.vx v12, v12, a4
        slli    t0, t0, 4
        vsll.vx v14, v14, a4
        vsetvli zero, zero, e16, m1, ta, ma
        vncvt.x.x.w v16, v0
        vncvt.x.x.w v17, v2
        vncvt.x.x.w v18, v4
        vncvt.x.x.w v19, v6
        vncvt.x.x.w v20, v8
        vncvt.x.x.w v21, v10
        vncvt.x.x.w v22, v12
        vncvt.x.x.w v23, v14
        vsseg8e16.v v16, (a0)
        add     a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_ls_16_rvv, zve32x, zba
        lpad    0
        ld      a0,  (a0)
        ld      a2, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a3, e32, m8, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v8, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vsll.vx v8, v8, a4
        vsub.vv v8, v0, v8
        vsetvli zero, zero, e16, m4, ta, ma
        vncvt.x.x.w v16, v0
        vncvt.x.x.w v20, v8
        vsseg2e16.v v16, (a0)
        sh2add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_rs_16_rvv, zve32x, zba
        lpad    0
        ld      a0,  (a0)
        ld      a2, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a3, e32, m8, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v8, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vsll.vx v8, v8, a4
        vadd.vv v0, v0, v8
        vsetvli zero, zero, e16, m4, ta, ma
        vncvt.x.x.w v16, v0
        vncvt.x.x.w v20, v8
        vsseg2e16.v v16, (a0)
        sh2add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_ms_16_rvv, zve32x, zba
        lpad    0
        ld      a0,  (a0)
        ld      a2, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a3, e32, m8, ta, ma
        vle32.v v8, (a2)
        sub     a3, a3, t0
        vle32.v v0, (a1)
        sh2add  a1, t0, a1
        vsra.vi v16, v8, 1
        sh2add  a2, t0, a2
        vsub.vv v24, v0, v16
        vadd.vv v16, v24, v8
        vsll.vx v8, v24, a4
        vsll.vx v0, v16, a4
        vsetvli zero, zero, e16, m4, ta, ma
        vncvt.x.x.w v0, v0
        vncvt.x.x.w v4, v8
        vsseg2e16.v v0, (a0)
        sh2add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_indep2_32_rvv, zve32x, zba
        lpad    0
        ld      a0,  (a0)
        ld      a2, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a3, e32, m4, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v4, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vsll.vx v4, v4, a4
        vsseg2e32.v v0, (a0)
        sh3add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_indep4_32_rvv, zve32x, zba
        lpad    0
        ld      a0,   (a0)
        ld      a2,  8(a1)
        ld      t1, 16(a1)
        ld      t2, 24(a1)
        ld      a1,   (a1)
1:
        vsetvli t0, a3, e32, m2, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v2, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vle32.v v4, (t1)
        sh2add  t1, t0, t1
        vsll.vx v2, v2, a4
        vle32.v v6, (t2)
        sh2add  t2, t0, t2
        vsll.vx v4, v4, a4
        slli    t0, t0, 4
        vsll.vx v6, v6, a4
        vsseg4e32.v v0, (a0)
        add     a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_indep6_32_rvv, zve32x, zba
        lpad    0
        ld      a0,   (a0)
        ld      a2,  8(a1)
        ld      t1, 16(a1)
        ld      t2, 24(a1)
        ld      t3, 32(a1)
        ld      t4, 40(a1)
        ld      a1,   (a1)
1:
        vsetvli t0, a3, e32, m1, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v1, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vle32.v v2, (t1)
        sh2add  t1, t0, t1
        vsll.vx v1, v1, a4
        vle32.v v3, (t2)
        sh2add  t2, t0, t2
        vsll.vx v2, v2, a4
        vle32.v v4, (t3)
        sh2add  t3, t0, t3
        vsll.vx v3, v3, a4
        vle32.v v5, (t4)
        sh2add  t4, t0, t4
        vsll.vx v4, v4, a4
        slli    t0, t0, 3
        vsll.vx v5, v5, a4
        sh1add  t0, t0, t0 // t0 *= 3
        vsseg6e32.v v0, (a0)
        add     a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_indep8_32_rvv, zve32x, zba
        lpad    0
        ld      a0,   (a0)
        ld      a2,  8(a1)
        ld      t1, 16(a1)
        ld      t2, 24(a1)
        ld      t3, 32(a1)
        ld      t4, 40(a1)
        ld      t5, 48(a1)
        ld      t6, 56(a1)
        ld      a1,   (a1)
1:
        vsetvli t0, a3, e32, m1, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v1, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        vle32.v v2, (t1)
        sh2add  a2, t0, a2
        vsll.vx v1, v1, a4
        sh2add  t1, t0, t1
        vle32.v v3, (t2)
        vsll.vx v2, v2, a4
        sh2add  t2, t0, t2
        vle32.v v4, (t3)
        sh2add  t3, t0, t3
        vsll.vx v3, v3, a4
        vle32.v v5, (t4)
        sh2add  t4, t0, t4
        vsll.vx v4, v4, a4
        vle32.v v6, (t5)
        sh2add  t5, t0, t5
        vsll.vx v5, v5, a4
        vle32.v v7, (t6)
        sh2add  t6, t0, t6
        vsll.vx v6, v6, a4
        slli    t0, t0, 5
        vsll.vx v7, v7, a4
        vsseg8e32.v v0, (a0)
        add     a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_ls_32_rvv, zve32x, zba
        lpad    0
        ld      a0,  (a0)
        ld      a2, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a3, e32, m4, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v4, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vsll.vx v4, v4, a4
        vsub.vv v4, v0, v4
        vsseg2e32.v v0, (a0)
        sh3add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_rs_32_rvv, zve32x, zba
        lpad    0
        ld      a0,  (a0)
        ld      a2, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a3, e32, m4, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v4, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vsll.vx v4, v4, a4
        vadd.vv v0, v0, v4
        vsseg2e32.v v0, (a0)
        sh3add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_ms_32_rvv, zve32x, zba
        lpad    0
        ld      a0,  (a0)
        ld      a2, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a3, e32, m4, ta, ma
        vle32.v v4, (a2)
        sub     a3, a3, t0
        vle32.v v0, (a1)
        sh2add  a1, t0, a1
        vsra.vi v8, v4, 1
        sh2add  a2, t0, a2
        vsub.vv v12, v0, v8
        vadd.vv v8, v12, v4
        vsll.vx v4, v12, a4
        vsll.vx v0, v8, a4
        vsseg2e32.v v0, (a0)
        sh3add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc
#endif
