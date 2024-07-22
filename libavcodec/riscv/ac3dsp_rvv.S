/*
 * Copyright (c) 2023 Institue of Software Chinese Academy of Sciences (ISCAS).
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
#include "libavutil/riscv/asm.S"

func ff_ac3_exponent_min_rvv, zve32x
        lpad    0
        beqz     a1, 3f
1:
        vsetvli  t2, a2, e8, m8, ta, ma
        vle8.v   v8, (a0)
        addi     t0, a0, 256
        sub      a2, a2, t2
        mv       t1, a1
2:
        vle8.v   v16, (t0)
        addi     t1, t1, -1
        vminu.vv v8, v8, v16
        addi     t0, t0, 256
        bnez     t1, 2b

        vse8.v   v8, (a0)
        add      a0, a0, t2
        bnez     a2, 1b
3:
        ret
endfunc

func ff_float_to_fixed24_rvv, zve32f, zba
        lpad    0
        li            t1, 1 << 24
        fcvt.s.w      f0, t1
1:
        vsetvli       t0, a2, e32, m8, ta, ma
        sub           a2, a2, t0
        vle32.v       v0, (a1)
        vfmul.vf      v0, v0, f0
        vfcvt.x.f.v   v0, v0
        sh2add        a1, t0, a1
        vse32.v       v0, (a0)
        sh2add        a0, t0, a0
        bnez          a2, 1b

        ret
endfunc

#if __riscv_xlen >= 64
func ff_sum_square_butterfly_int32_rvv, zve64x, zba
        lpad    0
        vsetvli    t0, zero, e64, m8, ta, ma
        vmv.v.x    v0, zero
        vmv.v.x    v8, zero
1:
        vsetvli    t0, a3, e32, m2, tu, ma
        vle32.v    v16, (a1)
        sub        a3, a3, t0
        vle32.v    v20, (a2)
        sh2add     a1, t0, a1
        vadd.vv    v24, v16, v20
        sh2add     a2, t0, a2
        vsub.vv    v28, v16, v20
        vwmacc.vv  v0, v16, v16
        vwmacc.vv  v4, v20, v20
        vwmacc.vv  v8, v24, v24
        vwmacc.vv  v12, v28, v28
        bnez       a3, 1b

        vsetvli    t0, zero, e64, m4, ta, ma
        vmv.s.x    v16, zero
        vmv.s.x    v17, zero
        vredsum.vs v16, v0, v16
        vmv.s.x    v18, zero
        vredsum.vs v17, v4, v17
        vmv.s.x    v19, zero
        vredsum.vs v18, v8, v18
        vmv.x.s    t0, v16
        vredsum.vs v19, v12, v19
        vmv.x.s    t1, v17
        sd         t0,   (a0)
        vmv.x.s    t2, v18
        sd         t1,  8(a0)
        vmv.x.s    t3, v19
        sd         t2, 16(a0)
        sd         t3, 24(a0)
        ret
endfunc
#endif

func ff_sum_square_butterfly_float_rvv, zve32f, zba
        lpad    0
        vsetvli     t0, zero, e32, m8, ta, ma
        vmv.v.x     v0, zero
        vmv.v.x     v8, zero
1:
        vsetvli     t0, a3, e32, m4, tu, ma
        vle32.v     v16, (a1)
        sub         a3, a3, t0
        vle32.v     v20, (a2)
        sh2add      a1, t0, a1
        vfadd.vv    v24, v16, v20
        sh2add      a2, t0, a2
        vfsub.vv    v28, v16, v20
        vfmacc.vv   v0, v16, v16
        vfmacc.vv   v4, v20, v20
        vfmacc.vv   v8, v24, v24
        vfmacc.vv   v12, v28, v28
        bnez        a3, 1b

        vsetvli     t0, zero, e32, m4, ta, ma
        vmv.s.x     v16, zero
        vmv.s.x     v17, zero
        vfredsum.vs v16, v0, v16
        vmv.s.x     v18, zero
        vfredsum.vs v17, v4, v17
        vmv.s.x     v19, zero
        vfredsum.vs v18, v8, v18
        vfmv.f.s    ft0, v16
        vfredsum.vs v19, v12, v19
        vfmv.f.s    ft1, v17
        fsw         ft0,   (a0)
        vfmv.f.s    ft2, v18
        fsw         ft1,  4(a0)
        vfmv.f.s    ft3, v19
        fsw         ft2,  8(a0)
        fsw         ft3, 12(a0)
        ret
endfunc
