/*
 * Copyright © 2023 Rémi Denis-Courmont.
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

func ff_restore_rgb_planes_rvv, zve32x, zba
        lpad    0
        li      t1, -0x80
        sub     a3, a3, a6
        sub     a4, a4, a6
        sub     a5, a5, a6
1:
        mv      t6, a6
        addi    a7, a7, -1
2:
        vsetvli t0, t6, e8, m8, ta, ma
        vle8.v  v16, (a1)
        sub     t6, t6, t0
        vle8.v  v8, (a0)
        vadd.vx v16, v16, t1
        add     a1, t0, a1
        vle8.v  v24, (a2)
        vadd.vv v8, v8, v16
        vadd.vv v24, v24, v16
        vse8.v  v8, (a0)
        add     a0, t0, a0
        vse8.v  v24, (a2)
        add     a2, t0, a2
        bnez    t6, 2b

        add     a0, a3, a0
        add     a1, a4, a1
        add     a2, a5, a2
        bnez    a7, 1b

        ret
endfunc

func ff_restore_rgb_planes10_rvv, zve32x, zba
        lpad    0
        li      t1, -0x200
        li      t2, 0x3FF
        sub     a3, a3, a6
        sub     a4, a4, a6
        sub     a5, a5, a6
1:
        mv      t6, a6
        addi    a7, a7, -1
2:
        vsetvli t0, t6, e16, m8, ta, ma
        vle16.v v16, (a1)
        sub     t6, t6, t0
        vle16.v v8, (a0)
        vadd.vx v16, v16, t1
        sh1add  a1, t0, a1
        vle16.v v24, (a2)
        vadd.vv v8, v8, v16
        vadd.vv v24, v24, v16
        vand.vx v8, v8, t2
        vand.vx v24, v24, t2
        vse16.v v8, (a0)
        sh1add  a0, t0, a0
        vse16.v v24, (a2)
        sh1add  a2, t0, a2
        bnez    t6, 2b

        sh1add  a0, a3, a0
        sh1add  a1, a4, a1
        sh1add  a2, a5, a2
        bnez    a7, 1b

        ret
endfunc
