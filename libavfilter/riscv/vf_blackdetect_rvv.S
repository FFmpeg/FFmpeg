/*
 * Copyright © 2025 Rémi Denis-Courmont.
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

func ff_count_pixels_8_rvv, zve32x
        lpad    0
        mv      a5, zero
        sub     a1, a1, a2
1:
        mv      t2, a2
        addi    a3, a3, -1
2:
        vsetvli t1, t2, e8, m8, ta, ma
        vle8.v  v8, (a0)
        sub     t2, t2, t1
        vmsleu.vx   v0, v8, a4
        add     a0, t1, a0
        vcpop.m t3, v0
        add     a5, t3, a5
        bnez    t2, 2b

        add     a0, a0, a1
        bnez    a3, 1b

        mv      a0, a5
        ret
endfunc

func ff_count_pixels_16_rvv, zve32x, zba
        lpad    0
        slli    t2, a2, 1
        mv      a5, zero
        sub     a1, a1, t2
1:
        mv      t2, a2
        addi    a3, a3, -1
2:
        vsetvli t1, t2, e16, m8, ta, ma
        vle16.v v8, (a0)
        sub     t2, t2, t1
        vmsleu.vx   v0, v8, a4
        sh1add  a0, t1, a0
        vcpop.m t3, v0
        add     a5, t3, a5
        bnez    t2, 2b

        add     a0, a0, a1
        bnez    a3, 1b

        mv      a0, a5
        ret
endfunc
