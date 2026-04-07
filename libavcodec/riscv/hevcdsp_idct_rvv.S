/*
 * Copyright (c) 2026 ZTE Corporation.
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

func ff_hevc_add_residual_4x4_8_rvv, zve32x
        lpad            0
        mv              t1, a0
        add             t3, a0, a2
        slli            a2, a2, 1
        li              t4, 2
        vsetivli        zero, 4, e8, mf4, ta, ma
1:
        vle8.v          v2, (a0)
        vle8.v          v3, (t3)

        vsetivli        zero, 4, e16, mf2, ta, ma
        vle16.v         v0, (a1)
        vzext.vf2       v4, v2
        addi            a1, a1, 8
        vle16.v         v1, (a1)
        vzext.vf2       v5, v3
        addi            a1, a1, 8

        vsadd.vv        v0, v0, v4
        vsadd.vv        v1, v1, v5

        vmax.vx         v0, v0, zero
        vmax.vx         v1, v1, zero

        vsetivli        zero, 4, e8, mf2, ta, ma
        vnclipu.wi      v0, v0, 0
        vnclipu.wi      v1, v1, 0

        vse8.v          v0, (a0)
        add             a0, a0, a2
        vse8.v          v1, (t3)
        add             t3, t3, a2

        addi            t4, t4, -1
        bnez            t4, 1b
        ret
endfunc

func ff_hevc_add_residual_8x8_8_rvv, zve32x
        lpad            0
        add             t3, a0, a2
        slli            a2, a2, 1
        li              t4, 4
        vsetivli        zero, 8, e8, mf2, ta, ma
1:
        vle8.v          v2, (a0)
        vle8.v          v3, (t3)

        vsetivli        zero, 8, e16, m1, ta, ma
        vle16.v         v0, (a1)
        vzext.vf2       v4, v2
        addi            a1, a1, 16
        vle16.v         v1, (a1)
        vzext.vf2       v5, v3
        addi            a1, a1, 16

        vsadd.vv        v0, v0, v4
        vsadd.vv        v1, v1, v5

        vmax.vx         v0, v0, zero
        vmax.vx         v1, v1, zero

        vsetivli        zero, 8, e8, mf2, ta, ma
        vnclipu.wi      v0, v0, 0
        vnclipu.wi      v1, v1, 0

        vse8.v          v0, (a0)
        add             a0, a0, a2
        vse8.v          v1, (t3)
        add             t3, t3, a2

        addi            t4, t4, -1
        bnez            t4, 1b
        ret
endfunc

func ff_hevc_add_residual_16x16_8_rvv, zve32x
        lpad            0
        li              t4, 16
        vsetivli        zero, 16, e8, m1, ta, ma
1:
        vle8.v          v2, (a0)

        vsetivli        zero, 16, e16, m2, ta, ma
        vle16.v         v0, (a1)
        vzext.vf2       v4, v2
        addi            a1, a1, 32

        vsadd.vv        v0, v0, v4
        vmax.vx         v0, v0, zero
        vsetivli        zero, 16, e8, m1, ta, ma
        vnclipu.wi      v0, v0, 0

        vse8.v          v0, (a0)
        add             a0, a0, a2

        addi            t4, t4, -1
        bnez            t4, 1b
        ret
endfunc

func ff_hevc_add_residual_32x32_8_rvv, zve32x
        lpad            0
        li              t0, 32
        li              t4, 32
        vsetvli         zero, t0, e8, m2, ta, ma
1:
        vle8.v          v4, (a0)

        vsetvli         zero, t0, e16, m4, ta, ma
        vle16.v         v0, (a1)
        vzext.vf2       v8, v4
        addi            a1, a1, 64

        vsadd.vv        v0, v0, v8
        vmax.vx         v0, v0, zero
        vsetvli         zero, t0, e8, m2, ta, ma
        vnclipu.wi      v0, v0, 0

        vse8.v          v0, (a0)
        add             a0, a0, a2

        addi            t4, t4, -1
        bnez            t4, 1b
        ret
endfunc
