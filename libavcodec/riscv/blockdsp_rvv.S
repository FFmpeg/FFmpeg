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

func ff_clear_block_rvv, zve64x
        lpad    0
        vsetivli      zero, 16, e64, m8, ta, ma
        vmv.v.i       v0, 0
        vse64.v       v0, (a0)

        ret
endfunc

func ff_clear_blocks_rvv, zve64x
        lpad    0
        vsetivli      zero, 16, e64, m8, ta, ma
        vmv.v.i       v0, 0

        .rept 5
        vse64.v       v0, (a0)
        addi          a0, a0, 128
        .endr
        vse64.v       v0, (a0)

        ret
endfunc

func ff_fill_block16_rvv, zve32x
        lpad    0
        vsetivli      t0, 16, e8, m1, ta, ma
        vmv.v.x       v8, a1
1:
        addi          a3, a3, -1
        vse8.v        v8, (a0)
        add           a0, a0, a2
        bnez          a3, 1b

        ret
endfunc

func ff_fill_block8_rvv, zve64x
        lpad    0
        vsetvli       t0, zero, e8, m4, ta, ma
        vmv.v.x       v8, a1
        vsetvli       t0, a3, e64, m4, ta, ma
        vsse64.v      v8, (a0), a2

        ret
endfunc
