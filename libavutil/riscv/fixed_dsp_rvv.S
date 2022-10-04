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

#include "asm.S"

// (a0) = (a0) + (a1), (a1) = (a0) - (a1) [0..a2-1]
func ff_butterflies_fixed_rvv, zve32x
1:
        vsetvli t0, a2, e32, m1, ta, ma
        vle32.v v16, (a0)
        sub     a2, a2, t0
        vle32.v v24, (a1)
        vadd.vv v0, v16, v24
        vsub.vv v8, v16, v24
        vse32.v v0, (a0)
        sh2add  a0, t0, a0
        vse32.v v8, (a1)
        sh2add  a1, t0, a1
        bnez    a2, 1b

        ret
endfunc
