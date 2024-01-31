/*
 * Copyright (c) 2024 Institue of Software Chinese Academy of Sciences (ISCAS).
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

func ff_rv34_inv_transform_dc_rvv, zve32x
        lh            t1, 0(a0)
        li            t0, 13 * 13 * 3
        mul           t2, t0, t1
        srai          t2, t2, 11
        vsetivli      zero, 16, e16, m2, ta, ma
        vmv.v.x       v8, t2
        vse16.v       v8, (a0)

        ret
endfunc

func ff_rv34_idct_dc_add_rvv, zve32x
        vsetivli      zero, 4, e8, mf4, ta, ma
        vlse32.v      v0, (a0), a1
        li            t1, 169
        mul           t1, t1, a2
        li            a2, 255
        addi          t1, t1, 512
        srai          t1, t1, 10
        vsetivli      zero, 4*4, e16, m2, ta, ma
        vzext.vf2     v2, v0
        vadd.vx       v2, v2, t1
        vmax.vx       v2, v2, zero
        vsetvli       zero, zero, e8, m1, ta, ma
        vnclipu.wi    v0, v2, 0
        vsetivli      zero, 4, e8, mf4, ta, ma
        vsse32.v      v0, (a0), a1

        ret
endfunc
