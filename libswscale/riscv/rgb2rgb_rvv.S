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

func ff_shuffle_bytes_0321_rvv, zve32x
        addi    t1, a0, 3
        addi    t2, a0, 2
        addi    t3, a0, 1
1:
        srai    a2, a2, 2
        li      t4, 4
2:
        vsetvli    t0, a2, e8, m1, ta, ma
        sub        a2, a2, t0
        vlse8.v    v8, (a0), t4
        sh2add     a0, t0, a0
        vlse8.v    v9, (t1), t4
        sh2add     t1, t0, t1
        vlse8.v    v10, (t2), t4
        sh2add     t2, t0, t2
        vlse8.v    v11, (t3), t4
        sh2add     t3, t0, t3
        vsseg4e8.v v8, (a1)
        sh2add     a1, t0, a1
        bnez       a2, 2b

        ret
endfunc

func ff_shuffle_bytes_2103_rvv, zve32x
        addi    t1, a0, 1
        addi    t2, a0, 0
        addi    t3, a0, 3
        addi    a0, a0, 2
        j       1b
endfunc

func ff_shuffle_bytes_1230_rvv, zve32x
        addi    t1, a0, 2
        addi    t2, a0, 3
        addi    t3, a0, 0
        addi    a0, a0, 1
        j       1b
endfunc

func ff_shuffle_bytes_3012_rvv, zve32x
        addi    t1, a0, 0
        addi    t2, a0, 1
        addi    t3, a0, 2
        addi    a0, a0, 3
        j       1b
endfunc

func ff_shuffle_bytes_3210_rvv, zve32x
        addi    t1, a0, 2
        addi    t2, a0, 1
        addi    t3, a0, 0
        addi    a0, a0, 3
        j       1b
endfunc
