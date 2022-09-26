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

func ff_vector_clipf_rvf, f
NOHWF   fmv.w.x fa0, a3
NOHWF   fmv.w.x fa1, a4
1:
        flw     ft0,   (a1)
        flw     ft1,  4(a1)
        fmax.s  ft0, ft0, fa0
        flw     ft2,  8(a1)
        fmax.s  ft1, ft1, fa0
        flw     ft3, 12(a1)
        fmax.s  ft2, ft2, fa0
        addi    a2, a2, -4
        fmax.s  ft3, ft3, fa0
        addi    a1, a1, 16
        fmin.s  ft0, ft0, fa1
        fmin.s  ft1, ft1, fa1
        fsw     ft0,   (a0)
        fmin.s  ft2, ft2, fa1
        fsw     ft1,  4(a0)
        fmin.s  ft3, ft3, fa1
        fsw     ft2,  8(a0)
        fsw     ft3, 12(a0)
        addi    a0, a0, 16
        bnez    a2, 1b

        ret
endfunc
