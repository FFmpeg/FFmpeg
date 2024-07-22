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

func ff_reorder_pixels_rvv, zve32x, zba
        lpad    0
        srai    a2, a2, 1
        add     t1, a1, a2
1:
        vsetvli t0, a2, e8, m4, ta, ma
        vle8.v  v0, (a1)
        sub     a2, a2, t0
        vle8.v  v4, (t1)
        add     a1, t0, a1
        vsseg2e8.v v0, (a0)
        add     t1, t0, t1
        sh1add  a0, t0, a0
        bnez    a2, 1b

        ret
endfunc
