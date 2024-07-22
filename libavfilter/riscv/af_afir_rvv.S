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

#include "libavutil/riscv/asm.S"

//  void ff_fcmul_add(float *sum, const float *t, const float *c, int len)
func ff_fcmul_add_rvv, zve64f, zba
        lpad    0
        li          t1, 32
1:
        vsetvli     t0, a3, e32, m4, ta, ma
        vle64.v     v24, (a0)
        sub         a3, a3, t0
        vnsrl.wx    v16, v24, zero
        vnsrl.wx    v20, v24, t1
        vle64.v     v24, (a1)
        sh3add      a1, t0, a1
        vnsrl.wx    v0, v24, zero
        vnsrl.wx    v4, v24, t1
        vle64.v     v24, (a2)
        sh3add      a2, t0, a2
        vnsrl.wx    v8, v24, zero
        vnsrl.wx    v12, v24, t1
        vfmacc.vv   v16, v0, v8
        vfmacc.vv   v20, v4, v8
        vfnmsac.vv  v16, v4, v12
        vfmacc.vv   v20, v0, v12
        vsseg2e32.v v16, (a0)
        sh3add      a0, t0, a0
        bgtz        a3, 1b

        flw         fa0, 0(a1)
        flw         fa1, 0(a2)
        flw         fa2, 0(a0)
        fmadd.s     fa2, fa0, fa1, fa2
        fsw         fa2, 0(a0)

        ret
endfunc
