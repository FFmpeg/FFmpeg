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

#include "config.h"
#include "libavutil/riscv/asm.S"

func ff_bswap32_buf_rvv, zve32x
        li      t4, 4
        addi    t1, a0, 1
        addi    t2, a0, 2
        addi    t3, a0, 3
1:
        vsetvli    t0, a2, e8, m1, ta, ma
        vlseg4e8.v v8, (a1)
        sub        a2, a2, t0
        sh2add     a1, t0, a1
        vsse8.v    v8, (t3), t4
        sh2add     t3, t0, t3
        vsse8.v    v9, (t2), t4
        sh2add     t2, t0, t2
        vsse8.v    v10, (t1), t4
        sh2add     t1, t0, t1
        vsse8.v    v11, (a0), t4
        sh2add     a0, t0, a0
        bnez       a2, 1b

        ret
endfunc

func ff_bswap16_buf_rvv, zve32x
        li      t2, 2
        addi    t1, a0, 1
1:
        vsetvli    t0, a2, e8, m1, ta, ma
        vlseg2e8.v v8, (a1)
        sub        a2, a2, t0
        sh1add     a1, t0, a1
        vsse8.v    v8, (t1), t2
        sh1add     t1, t0, t1
        vsse8.v    v9, (a0), t2
        sh1add     a0, t0, a0
        bnez       a2, 1b

        ret
endfunc
