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

func ff_llvidenc_sub_median_pred_rvb, zbb
        lpad    0
        lbu     t4, (a4)    # l
        lbu     t5, (a5)    # lt
        add     a3, a0, a3
1:
        lbu     t1, (a1)    # src1
        addi    a1, a1, 1
        add     t6, t4, t1
        max     t3, t4, t1
        sub     t6, t6, t5  # l + src1 - lt
        min     t0, t4, t1
        andi    t6, t6, 0xff
        mv      t5, t1
        min     t3, t3, t6
        lbu     t4, (a2)    # src2
        addi    a0, a0, 1
        max     t6, t0, t3  # mid_pred(...)
        sub     t0, t4, t6
        addi    a2, a2, 1
        sb      t0, -1(a0)
        bne     a0, a3, 1b

        sw      t4, (a4)
        sw      t5, (a5)
        ret
endfunc
