/*
 * Copyright (c) 2024 Institue of Software Chinese Academy of Sciences (ISCAS).
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

.macro pix_abs_ret
        vsetivli        zero, 1, e32, m1, ta, ma
        vmv.x.s         a0, v0
        ret
.endm

func ff_pix_abs16_rvv, zve32x
        vsetivli        zero, 1, e32, m1, ta, ma
        vmv.s.x         v0, zero
1:
        vsetivli        zero, 16, e8, m1, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v12, (a2)
        addi            a4, a4, -1
        vwsubu.vv       v16, v4, v12
        add             a1, a1, a3
        vwsubu.vv       v20, v12, v4
        vsetvli         zero, zero, e16, m2, tu, ma
        vmax.vv         v16, v16, v20
        add             a2, a2, a3
        vwredsum.vs     v0, v16, v0
        bnez            a4, 1b

        pix_abs_ret
endfunc

func ff_pix_abs8_rvv, zve32x
        vsetivli        zero, 1, e32, m1, ta, ma
        vmv.s.x         v0, zero
1:
        vsetivli        zero, 8, e8, mf2, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v12, (a2)
        addi            a4, a4, -1
        vwsubu.vv       v16, v4, v12
        add             a1, a1, a3
        vwsubu.vv       v20, v12, v4
        vsetvli         zero, zero, e16, m1, tu, ma
        vmax.vv         v16, v16, v20
        add             a2, a2, a3
        vwredsum.vs     v0, v16, v0
        bnez            a4, 1b

        pix_abs_ret
endfunc
