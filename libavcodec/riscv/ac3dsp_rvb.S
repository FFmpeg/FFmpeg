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

func ff_ac3_exponent_min_rvb, zbb
        lpad    0
        beqz    a1, 3f
1:
        addi    a2, a2, -1
        lb      t3, (a0)
        addi    t0, a0, 256
        mv      t1, a1
2:
        lb      t4, (t0)
        addi    t1, t1, -1
        addi    t0, t0, 256
        minu    t3, t3, t4
        bnez    t1, 2b

        sb      t3, (a0)
        addi    a0, a0, 1
        bnez    a2, 1b
3:
        ret
endfunc

func ff_extract_exponents_rvb, zbb
        lpad    0
1:
        lw       t0, (a1)
        addi     a0, a0, 1
        neg      t1, t0
        addi     a1, a1, 4
        max      t0, t0, t1
        addi     a2, a2, -1
        clz      t0, t0
        addi     t0, t0, 24 - __riscv_xlen
        sb       t0, -1(a0)
        bgtz    a2, 1b

        ret
endfunc
