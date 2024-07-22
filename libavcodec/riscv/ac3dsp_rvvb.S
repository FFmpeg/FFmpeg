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

#include "config.h"
#include "libavutil/riscv/asm.S"

func ff_extract_exponents_rvvb, zve32x, zvbb, zba
        lpad    0
1:
        vsetvli     t0, a2, e32, m8, ta, ma
        vle32.v     v8, (a1)
        sub         a2, a2, t0
        vneg.v      v16, v8
        sh2add      a1, t0, a1
        vmax.vv     v8, v8, v16
        vclz.v      v8, v8
        vsetvli     zero, zero, e16, m4, ta, ma
        vncvt.x.x.w v4, v8
        vsetvli     zero, zero, e8, m2, ta, ma
        vncvt.x.x.w v2, v4
        vadd.vi     v2, v2, 24 - 32
        vse8.v      v2, (a0)
        add         a0, a0, t0
        bnez        a2, 1b

        ret
endfunc
