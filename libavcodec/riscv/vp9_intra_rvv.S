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

.macro avgdc size
        vwredsumu.vs v16, v8, v16
        vsetivli     zero, 1, e16, m1, ta, ma
        vmv.x.s      t1, v16
        addi         t1, t1, 1 << (\size - 1)
        srai         t1, t1, \size
.endm

.macro getdc type size
.ifc \type,top
        vmv.v.x      v16, zero
        vle8.v       v8, (a3)
        avgdc        \size
.else
.ifc \type,left
        vmv.v.x      v16, zero
        vle8.v       v8, (a2)
        avgdc        \size
.else
.ifc \type,dc
        vmv.v.x      v16, zero
        vle8.v       v8, (a2)
        vwredsumu.vs v16, v8, v16
        vle8.v       v8, (a3)
        avgdc        \size
.else
        li           t1, \type
.endif
.endif
.endif
.endm

.macro dc_e32 type size n restore
.ifc \size,32
        li           t0, 32
        vsetvli      zero, t0, e8, m2, ta, ma
.else
        vsetivli     zero, 16, e8, m1, ta, ma
.endif
        getdc        \type \n

.if \restore == 1 && \size == 32
        vsetvli      zero, t0, e8, m2, ta, ma
.elseif \restore == 1 && \size == 16
        vsetivli     zero, 16, e8, m1, ta, ma
.endif
        vmv.v.x      v0, t1

        .rept \size
        vse8.v       v0, (a0)
        add          a0, a0, a1
        .endr

        ret
.endm

.macro dc_e64 type size n restore
        vsetivli     zero, 8, e8, mf2, ta, ma
        getdc        \type \n

        li           t0, 64
        vsetvli      zero, t0, e8, m4, ta, ma
        vmv.v.x      v0, t1
        vsetivli     zero, 8, e8, mf2, ta, ma
        vsse64.v     v0, (a0), a1

        ret
.endm

.macro func_dc name size type n restore ext
func ff_\()\name\()_\()\size\()x\size\()_rvv, \ext
.if \size == 8
        dc_e64 \type \size \n \restore
.else
        dc_e32 \type \size \n \restore
.endif
endfunc
.endm

func_dc dc_127   32  127  0  0  zve32x
func_dc dc_127   16  127  0  0  zve32x
func_dc dc_127   8   127  0  0  zve64x
func_dc dc_128   32  128  0  0  zve32x
func_dc dc_128   16  128  0  0  zve32x
func_dc dc_128   8   128  0  0  zve64x
func_dc dc_129   32  129  0  0  zve32x
func_dc dc_129   16  129  0  0  zve32x
func_dc dc_129   8   129  0  0  zve64x
func_dc dc       32  dc   6  1  zve32x
func_dc dc       16  dc   5  1  zve32x
func_dc dc       8   dc   4  0  zve64x
func_dc dc_left  32  left 5  1  zve32x
func_dc dc_left  16  left 4  1  zve32x
func_dc dc_left  8   left 3  0  zve64x
func_dc dc_top   32  top  5  1  zve32x
func_dc dc_top   16  top  4  1  zve32x
func_dc dc_top   8   top  3  0  zve64x

func ff_h_32x32_rvv, zve32x
        li           t0, 32
        addi         a2, a2, 31
        vsetvli      zero, t0, e8, m2, ta, ma

        .rept 2
        .irp n 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30
        lbu          t1, (a2)
        addi         a2, a2, -1
        vmv.v.x      v\n, t1
        .endr
        .irp n 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30
        vse8.v       v\n, (a0)
        add          a0, a0, a1
        .endr
        .endr

        ret
endfunc

func ff_h_16x16_rvv, zve32x
        addi         a2, a2, 15
        vsetivli     zero, 16, e8, m1, ta, ma

        .irp n 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23
        lbu          t1, (a2)
        addi         a2, a2, -1
        vmv.v.x      v\n, t1
        .endr
        .irp n 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22
        vse8.v       v\n, (a0)
        add          a0, a0, a1
        .endr
        vse8.v       v23, (a0)

        ret
endfunc

func ff_h_8x8_rvv, zve32x
        addi         a2, a2, 7
        vsetivli     zero, 8, e8, mf2, ta, ma

        .irp n 8, 9, 10, 11, 12, 13, 14, 15
        lbu          t1, (a2)
        addi         a2, a2, -1
        vmv.v.x      v\n, t1
        .endr
        .irp n 8, 9, 10, 11, 12, 13, 14
        vse8.v       v\n, (a0)
        add          a0, a0, a1
        .endr
        vse8.v       v15, (a0)

        ret
endfunc
