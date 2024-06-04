/*
 * Copyright © 2024 Rémi Denis-Courmont.
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

func ff_bgr24ToY_rvv, zve32x
        lw      t1, 8(a5) # BY
        lw      t3, 0(a5) # RY
        j       1f
endfunc

func ff_rgb24ToY_rvv, zve32x
        lw      t1, 0(a5) # RY
        lw      t3, 8(a5) # BY
1:
        lw      t2, 4(a5) # GY
        li      t4, (32 << (15 - 1)) + (1 << (15 - 7))
2:
        vsetvli    t0, a4, e32, m8, ta, ma
        vlseg3e8.v v0, (a1)
        sub        a4, a4, t0
        vzext.vf4  v8, v0
        sh1add     t5, t0, t0 # t1 = 3 * t0
        vzext.vf4  v16, v2
        vzext.vf4  v24, v4
        add        a1, t5, a1
        vmul.vx    v8, v8, t1
        vmacc.vx   v8, t2, v16
        vmacc.vx   v8, t3, v24
        vadd.vx    v8, v8, t4
        vsetvli    zero, zero, e16, m4, ta, ma
        vnsra.wi   v0, v8, 15 - 6
        vse16.v    v0, (a0)
        sh1add     a0, t0, a0
        bnez       a4, 2b

        ret
endfunc
