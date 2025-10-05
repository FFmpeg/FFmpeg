/*
 * Copyright © 2022 Rémi Denis-Courmont.
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

func ff_get_pixels_8_rvv, zve64x
        lpad    0
        vsetivli zero, 8, e8, mf2, ta, ma
        li      t0, 8 * 8
        vlse64.v v16, (a1), a2
        vsetvli zero, t0, e8, m4, ta, ma
        vwcvtu.x.x.v v8, v16
        vse16.v v8, (a0)
        ret
endfunc

func ff_get_pixels_unaligned_8_rvv, zve32x
        lpad    0
        li      t2, 8
        vsetivli     zero, 8, e8, mf2, ta, ma
1:
        add     t1, a1, a2
        vle8.v  v16, (a1)
        addi    t0, a0, 2 * 8
        vle8.v  v17, (t1)
        addi    t2, t2, -2
        vwcvtu.x.x.v v8, v16
        vwcvtu.x.x.v v9, v17
        vse16.v v8, (a0)
        add     a1, t1, a2
        vse16.v v9, (t0)
        addi    a0, t0, 2 * 8
        bnez    t2, 1b

        ret
endfunc

func ff_diff_pixels_rvv, zve64x
        lpad    0
        vsetivli zero, 8, e8, mf2, ta, ma
        li      t0, 8 * 8
        vlse64.v v16, (a1), a3
        vlse64.v v24, (a2), a3
        vsetvli zero, t0, e8, m4, ta, ma
        vwsubu.vv v8, v16, v24
        vse16.v v8, (a0)
        ret
endfunc

func ff_diff_pixels_unaligned_rvv, zve32x
        lpad    0
        li      t3, 8
        vsetivli    zero, 8, e8, mf2, ta, ma
1:
        vle8.v v16, (a1)
        add     a1, a1, a3
        vle8.v v24, (a2)
        add     a2, a2, a3
        vwsubu.vv   v8, v16, v24
        addi    t3, t3, -1
        vse16.v v8, (a0)
        addi    a0, a0, 2 * 8
        bnez    t3, 1b

        ret
endfunc
