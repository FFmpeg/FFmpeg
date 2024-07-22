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

#include "asm.S"

func ff_lls_update_covariance_rvv, zve64d, zbb
        lpad    0
        vtype_vli     t0, a2, t1, e64, ta, ma
        vsetvl        zero, a2, t0
        vle64.v       v8, (a1)
1:
        vfmv.f.s      ft0, v8
        vle64.v       v16, (a0)
        vfmacc.vf     v16, ft0, v8
        addi          a2, a2, -1
        vslidedown.vi v8, v8, 1
        vse64.v       v16, (a0)
        addi          a0, a0, (36 + 1) * 8 # 1 row + 1 element
        bnez          a2, 1b

        ret
endfunc
