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

func ff_vc1_inv_trans_8x8_dc_rvv, zve64x
        lh            t2, (a2)
        vsetivli      zero, 8, e8, mf2, ta, ma
        vlse64.v      v0, (a0), a1
        sh1add        t2, t2, t2
        addi          t2, t2, 1
        srai          t2, t2, 1
        sh1add        t2, t2, t2
        addi          t2, t2, 16
        srai          t2, t2, 5
        li            t0, 8*8
        vsetvli       zero, t0, e16, m8, ta, ma
        vzext.vf2     v8, v0
        vadd.vx       v8, v8, t2
        vmax.vx       v8, v8, zero
        vsetvli       zero, zero, e8, m4, ta, ma
        vnclipu.wi    v0, v8, 0
        vsetivli      zero, 8, e8, mf2, ta, ma
        vsse64.v      v0, (a0), a1
        ret
endfunc

func ff_vc1_inv_trans_4x8_dc_rvv, zve32x
        lh            t2, (a2)
        vsetivli      zero, 8, e8, mf2, ta, ma
        vlse32.v      v0, (a0), a1
        slli          t1, t2, 4
        add           t2, t2, t1
        addi          t2, t2, 4
        srai          t2, t2, 3
        sh1add        t2, t2, t2
        slli          t2, t2, 2
        addi          t2, t2, 64
        srai          t2, t2, 7
        li            t0, 4*8
        vsetvli       zero, t0, e16, m4, ta, ma
        vzext.vf2     v4, v0
        vadd.vx       v4, v4, t2
        vmax.vx       v4, v4, zero
        vsetvli       zero, zero, e8, m2, ta, ma
        vnclipu.wi    v0, v4, 0
        vsetivli      zero, 8, e8, mf2, ta, ma
        vsse32.v      v0, (a0), a1
        ret
endfunc

func ff_vc1_inv_trans_8x4_dc_rvv, zve64x
        lh            t2, (a2)
        vsetivli      zero, 4, e8, mf4, ta, ma
        vlse64.v      v0, (a0), a1
        sh1add        t2, t2, t2
        addi          t2, t2, 1
        srai          t2, t2, 1
        slli          t1, t2, 4
        add           t2, t2, t1
        addi          t2, t2, 64
        srai          t2, t2, 7
        li            t0, 8*4
        vsetvli       zero, t0, e16, m4, ta, ma
        vzext.vf2     v4, v0
        vadd.vx       v4, v4, t2
        vmax.vx       v4, v4, zero
        vsetvli       zero, zero, e8, m2, ta, ma
        vnclipu.wi    v0, v4, 0
        vsetivli      zero, 4, e8, mf4, ta, ma
        vsse64.v      v0, (a0), a1
        ret
endfunc

func ff_vc1_inv_trans_4x4_dc_rvv, zve32x
        lh            t2, (a2)
        vsetivli      zero, 4, e8, mf4, ta, ma
        vlse32.v      v0, (a0), a1
        slli          t1, t2, 4
        add           t2, t2, t1
        addi          t2, t2, 4
        srai          t2, t2, 3
        slli          t1, t2, 4
        add           t2, t2, t1
        addi          t2, t2, 64
        srai          t2, t2, 7
        vsetivli      zero, 4*4, e16, m2, ta, ma
        vzext.vf2     v2, v0
        vadd.vx       v2, v2, t2
        vmax.vx       v2, v2, zero
        vsetvli       zero, zero, e8, m1, ta, ma
        vnclipu.wi    v0, v2, 0
        vsetivli      zero, 4, e8, mf4, ta, ma
        vsse32.v      v0, (a0), a1
        ret
endfunc
