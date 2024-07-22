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

func ff_ssd_int8_vs_int16_rvv, zve32x, zba
        lpad    0
        vsetvli      t0, zero, e32, m8, ta, ma
        vmv.v.x      v24, zero
1:
        vsetvli      t0, a2, e8, m2, tu, ma
        vle16.v      v8, (a1)
        sub          a2, a2, t0
        vle8.v       v0, (a0)
        vwsub.wv     v16, v8, v0
        vsetvli      zero, zero, e16, m4, tu, ma
        add          a0, a0, t0
        vwmacc.vv    v24, v16, v16
        sh1add       a1, t0, a1
        bnez         a2, 1b
        vsetvli      zero, zero, e32, m8, ta, ma
        vmv.s.x      v0, zero
        vredsum.vs   v0, v24, v0
        vmv.x.s      a0, v0

        ret
endfunc
