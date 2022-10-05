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

func ff_opus_postfilter_rvv_128, zve32f
        lvtypei a5, e32, m2, ta, ma
        j       1f
endfunc

func ff_opus_postfilter_rvv_512, zve32f
        lvtypei a5, e32, mf2, ta, ma
        j       1f
endfunc

func ff_opus_postfilter_rvv_1024, zve32f
        lvtypei a5, e32, mf4, ta, ma
        j       1f
endfunc

func ff_opus_postfilter_rvv_256, zve32f
        lvtypei a5, e32, m1, ta, ma
1:
        li           a4, 5
        addi         a1, a1, 2
        slli         a1, a1, 2
        lw           t1, 4(a2)
        vsetivli     zero, 3, e32, m1, ta, ma
        vle32.v      v24, (a2)
        sub          a1, a0, a1      // a1 = &x4 = &data[-(period + 2)]
        vsetvl       zero, a4, a5
        vslide1up.vx v8, v24, t1
        lw           t2, 8(a2)
        vle32.v      v16, (a1)
        vslide1up.vx v24, v8, t2     // v24 = { g[2], g[1], g[0], g[1], g[2] }
2:
        vsetvl  t0, a3, a5
        vle32.v v0, (a0)
        sub     a3, a3, t0
3:
        vsetvl         zero, a4, a5
        lw             t2, 20(a1)
        vfmul.vv       v8, v24, v16
        addi           a0, a0, 4
        vslide1down.vx v16, v16, t2
        addi           a1, a1, 4
        vfredusum.vs   v0, v8, v0
        vsetvl         zero, t0, a5
        vmv.x.s        t1, v0
        addi           t0, t0, -1
        vslide1down.vx v0, v0, zero
        sw             t1, -4(a0)
        bnez           t0, 3b

        bnez    a3, 2b

        ret
endfunc
