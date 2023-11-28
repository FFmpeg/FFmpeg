/*
 * Copyright (c) 2023 Institue of Software Chinese Academy of Sciences (ISCAS).
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

func ff_abs_pow34_rvv, zve32f
1:
        vsetvli      t0, a2, e32, m8, ta, ma
        sub          a2, a2, t0
        vle32.v      v0, (a1)
        sh2add       a1, t0, a1
        vfabs.v      v0, v0
        vfsqrt.v     v8, v0
        vfmul.vv     v8, v8, v0
        vfsqrt.v     v8, v8
        vse32.v      v8, (a0)
        sh2add       a0, t0, a0
        bnez         a2, 1b

        ret
endfunc
