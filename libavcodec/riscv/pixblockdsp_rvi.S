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

func ff_get_pixels_8_rvi
        lpad    0
.irp row, 0, 1, 2, 3, 4, 5, 6, 7
        ld      t0,    (a1)
        add     a1, a1, a2
        sd      zero, ((\row * 16) + 0)(a0)
        addi    t6, t6, -1
        sd      zero, ((\row * 16) + 8)(a0)
        srli    t1, t0, 8
        sb      t0, ((\row * 16) + 0)(a0)
        srli    t2, t0, 16
        sb      t1, ((\row * 16) + 2)(a0)
        srli    t3, t0, 24
        sb      t2, ((\row * 16) + 4)(a0)
        srli    t4, t0, 32
        sb      t3, ((\row * 16) + 6)(a0)
        srli    t1, t0, 40
        sb      t4, ((\row * 16) + 8)(a0)
        srli    t2, t0, 48
        sb      t1, ((\row * 16) + 10)(a0)
        srli    t3, t0, 56
        sb      t2, ((\row * 16) + 12)(a0)
        sb      t3, ((\row * 16) + 14)(a0)
.endr
        ret
endfunc

func ff_get_pixels_16_rvi
        lpad    0
.irp row, 0, 1, 2, 3, 4, 5, 6, 7
        ld      t0, 0(a1)
        ld      t1, 8(a1)
        add     a1, a1, a2
        sd      t0, ((\row * 16) + 0)(a0)
        sd      t1, ((\row * 16) + 8)(a0)
.endr
        ret
endfunc
