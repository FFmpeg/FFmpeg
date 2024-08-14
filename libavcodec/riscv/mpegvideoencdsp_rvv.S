/*
 * Copyright © 2024 Rémi Denis-Courmont.
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

.equ    BASIS_SHIFT, 16
.equ    RECON_SHIFT,  6

func ff_try_8x8basis_rvv, zve32x, b
        li      t1, 64
        csrwi   vxrm, 0
        vsetvli     t0, t1, e32, m8, ta, ma
        vmv.v.x     v24, zero
        vmv.s.x     v1, zero
1:
        vsetvli     zero, zero, e16, m4, ta, ma
        vle16.v     v4, (a2)
        sub     t1, t1, t0
        vwmul.vx    v16, v4, a3
        sh1add  a2, t0, a2
        vle16.v     v8, (a0)
        sh1add  a0, t0, a0
        vnclip.wi   v4, v16, BASIS_SHIFT - RECON_SHIFT
        vle16.v     v12, (a1)
        sh1add  a1, t0, a1
        vadd.vv     v4, v8, v4
        vsra.vi     v4, v4, RECON_SHIFT
        vwmul.vv    v16, v12, v4
        vsetvli     zero, zero, e32, m8, ta, ma
        vmul.vv     v16, v16, v16
        vsra.vi     v16, v16, 4
        vadd.vv     v24, v24, v16
        bnez    t1, 1b

        vredsum.vs  v1, v24, v1
        vmv.x.s     a0, v1
        srai    a0, a0, 2
        ret
endfunc

func ff_add_8x8basis_rvv, zve32x, b
        li      t1, 64
        csrwi   vxrm, 0
1:
        vsetvli     t0, t1, e16, m4, ta, ma
        vle16.v     v4, (a1)
        sub     t1, t1, t0
        vwmul.vx    v16, v4, a2
        sh1add  a1, t0, a1
        vle16.v     v8, (a0)
        vnclip.wi   v4, v16, BASIS_SHIFT - RECON_SHIFT
        vadd.vv     v4, v8, v4
        vse16.v     v4, (a0)
        sh1add  a0, t0, a0
        bnez    t1, 1b

        ret
endfunc

func ff_pix_sum_rvv, zve64x, b
        lpad    0
        vsetivli    t0, 16, e16, m1, ta, ma
        slli    t1, t0, 3
        vlsseg2e64.v    v8, (a0), a1 # poor man's vlse128.v
        li      t2, 16
        vmv.s.x     v1, zero
        vsetvli     zero, t1, e8, m4, ta, ma
        vwaddu.vv   v16, v8, v12
        beq     t0, t2, 2f # VLEN >= 256
1:
        sh3add  a0, a1, a0
        vsetivli    zero, 8, e16, m1, ta, ma
        vlsseg2e64.v    v8, (a0), a1
        vsetvli     zero, t1, e8, m4, ta, ma
        vwaddu.wv   v16, v16, v8
        vwaddu.wv   v16, v16, v12
2:
        vsetvli     zero, zero, e16, m8, ta, ma
        vredsum.vs  v1, v16, v1
        vmv.x.s     a0, v1
        zext.h      a0, a0
        ret
endfunc

func ff_pix_norm1_rvv, zve64x
        lpad    0
        li      t1, 16
        vsetvli     t0, zero, e32, m8, ta, ma
        vmv.v.x     v16, zero
        vmv.s.x     v1, zero
1:
        vsetvli     t0, t1, e64, m2, ta, ma
        vlsseg2e64.v    v8, (a0), a1
        slli    t2, t0, 3
        vsetvli     zero, t2, e8, m2, ta, ma
        vwmulu.vv   v24, v8, v8
        sub     t1, t1, t0
        vwmulu.vv   v28, v10, v10
        mul     t3, a1, t0
        vsetvli     zero, zero, e16, m4, tu, ma
        vwaddu.wv   v16, v16, v24
        add     a0, t3, a0
        vwaddu.wv   v16, v16, v28
        bnez    t1, 1b

        vsetvli     zero, zero, e32, m8, ta, ma
        vredsum.vs  v1, v16, v1
        vmv.x.s     a0, v1
        ret
endfunc
