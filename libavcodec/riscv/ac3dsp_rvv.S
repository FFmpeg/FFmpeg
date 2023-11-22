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

#include "config.h"
#include "libavutil/riscv/asm.S"

func ff_float_to_fixed24_rvv, zve32f
        li            t1, 1 << 24
        fcvt.s.w      f0, t1
1:
        vsetvli       t0, a2, e32, m8, ta, ma
        sub           a2, a2, t0
        vle32.v       v0, (a1)
        vfmul.vf      v0, v0, f0
        vfcvt.x.f.v   v0, v0
        sh2add        a1, t0, a1
        vse32.v       v0, (a0)
        sh2add        a0, t0, a0
        bnez          a2, 1b

        ret
endfunc
