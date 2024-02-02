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

.macro vp8_idct_dc_add
        vlse32.v      v0, (a0), a2
        lh            a5, 0(a1)
        sh            zero, 0(a1)
        addi          a5, a5, 4
        srai          t1, a5, 3
        vsetivli      zero, 4*4, e16, m2, ta, ma
        vzext.vf2     v2, v0
        vadd.vx       v2, v2, t1
        vmax.vx       v2, v2, zero
        vsetvli       zero, zero, e8, m1, ta, ma
        vnclipu.wi    v0, v2, 0
        vsetivli      zero, 4, e8, mf4, ta, ma
        vsse32.v      v0, (a0), a2
.endm

.macro vp8_idct_dc_addy
        vp8_idct_dc_add
        addi          a0, a0, 4
        addi          a1, a1, 32
.endm

func ff_vp8_idct_dc_add_rvv, zve32x
        vsetivli      zero, 4, e8, mf4, ta, ma
        vp8_idct_dc_add

        ret
endfunc

func ff_vp8_idct_dc_add4y_rvv, zve32x
        vsetivli      zero, 4, e8, mf4, ta, ma
        .rept 3
        vp8_idct_dc_addy
        .endr
        vp8_idct_dc_add

        ret
endfunc

func ff_vp8_idct_dc_add4uv_rvv, zve32x
        vsetivli      zero, 4, e8, mf4, ta, ma
        vp8_idct_dc_addy
        vp8_idct_dc_add
        addi          a0, a0, -4
        sh2add        a0, a2, a0
        addi          a1, a1, 32
        vp8_idct_dc_addy
        vp8_idct_dc_add

        ret
endfunc
