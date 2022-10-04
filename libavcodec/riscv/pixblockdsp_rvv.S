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

func ff_get_pixels_8_rvv, zve32x
        vsetivli     zero, 8, e8, mf2, ta, ma
        vlsseg8e8.v  v16, (a1), a2
        vwcvtu.x.x.v v8, v16
        vwcvtu.x.x.v v9, v17
        vwcvtu.x.x.v v10, v18
        vwcvtu.x.x.v v11, v19
        vwcvtu.x.x.v v12, v20
        vwcvtu.x.x.v v13, v21
        vwcvtu.x.x.v v14, v22
        vwcvtu.x.x.v v15, v23
        vsseg8e16.v  v8, (a0)
        ret
endfunc

func ff_get_pixels_16_rvv, zve32x
        vsetivli     zero, 8, e16, m1, ta, ma
        vlsseg8e16.v v0, (a1), a2
        vsseg8e16.v  v0, (a0)
        ret
endfunc

func ff_diff_pixels_rvv, zve32x
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
