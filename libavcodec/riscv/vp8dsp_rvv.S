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

.macro vsetvlstatic8 len
.if \len <= 4
        vsetivli        zero, \len, e8, mf4, ta, ma
.elseif \len <= 8
        vsetivli        zero, \len, e8, mf2, ta, ma
.elseif \len <= 16
        vsetivli        zero, \len, e8, m1, ta, ma
.elseif \len <= 31
        vsetivli        zero, \len, e8, m2, ta, ma
.endif
.endm

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

.macro bilin_load dst len type mn
.ifc \type,v
        add             t5, a2, a3
.else
        addi            t5, a2, 1
.endif
        vle8.v          \dst, (a2)
        vle8.v          v2, (t5)
        vwmulu.vx       v28, \dst, t1
        vwmaccu.vx      v28, \mn, v2
        vwaddu.wx       v24, v28, t4
        vnsra.wi        \dst, v24, 3
.endm

.macro put_vp8_bilin_h_v len type mn
func ff_put_vp8_bilin\len\()_\type\()_rvv, zve32x
        vsetvlstatic8   \len
        li              t1, 8
        li              t4, 4
        sub             t1, t1, \mn
1:
        addi            a4, a4, -1
        bilin_load      v0, \len, \type, \mn
        vse8.v          v0, (a0)
        add             a2, a2, a3
        add             a0, a0, a1
        bnez            a4, 1b

        ret
endfunc
.endm

.irp len 16,8,4
put_vp8_bilin_h_v \len h a5
put_vp8_bilin_h_v \len v a6
.endr
