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
1:
        vlse64.v v16, (a1), a2
        vsetvli zero, t0, e8, m4, ta, ma
        vwcvtu.x.x.v v8, v16
        vse16.v v8, (a0)
        ret
endfunc

func ff_get_pixels_unaligned_8_rvv, zve64x
        lpad    0
        andi    t1, a1, 7
        vsetivli zero, 8, e64, m4, ta, ma
        li      t0, 8 * 8
        beqz    t1, 1b
        andi    a1, a1, -8
        slli    t2, t1, 3
        addi    t1, a1, 8
        sub     t3, t0, t2
        vlse64.v v16, (a1), a2
        vlse64.v v24, (t1), a2
        vsrl.vx v16, v16, t2
        vsll.vx v24, v24, t3
        vor.vv  v16, v16, v24
        vsetvli zero, t0, e8, m4, ta, ma
        vwcvtu.x.x.v v8, v16
        vse16.v v8, (a0)
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
        vsetivli    zero, 8, e8, mf2, ta, ma
        vlsseg8e8.v v16, (a1), a3
        vlsseg8e8.v v24, (a2), a3
        vwsubu.vv   v8, v16, v24
        vwsubu.vv   v9, v17, v25
        vwsubu.vv   v10, v18, v26
        vwsubu.vv   v11, v19, v27
        vwsubu.vv   v12, v20, v28
        vwsubu.vv   v13, v21, v29
        vwsubu.vv   v14, v22, v30
        vwsubu.vv   v15, v23, v31
        vsseg8e16.v v8, (a0)
        ret
endfunc
