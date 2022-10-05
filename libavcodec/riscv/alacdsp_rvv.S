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

#if (__riscv_xlen == 64)
func ff_alac_decorrelate_stereo_rvv, zve32x
        ld          a4, 8(a0)
        ld          a0, 0(a0)
1:
        vsetvli     t0, a1, e32, m1, ta, ma
        vle32.v     v24, (a4)
        sub         a1, a1, t0
        vle32.v     v16, (a0)
        vmul.vx     v8, v24, a3
        vadd.vv     v24, v24, v16
        vsra.vx     v8, v8, a2
        vsub.vv     v16, v16, v8
        vsub.vv     v24, v24, v8
        vse32.v     v16, (a4)
        sh2add      a4, t0, a4
        vse32.v     v24, (a0)
        sh2add      a0, t0, a0
        bnez        a1, 1b

        ret
endfunc

func ff_alac_append_extra_bits_mono_rvv, zve32x
        ld      a0, (a0)
        ld      a1, (a1)
1:
        vsetvli t0, a4, e32, m1, ta, ma
        vle32.v v16, (a0)
        sub     a4, a4, t0
        vle32.v v24, (a1)
        sh2add  a1, t0, a1
        vsll.vx v16, v16, a2
        vor.vv  v16, v24, v16
        vse32.v v16, (a0)
        sh2add  a0, t0, a0
        bnez    a4, 1b

        ret
endfunc

func ff_alac_append_extra_bits_stereo_rvv, zve32x
        ld      a6, 8(a0)
        ld      a0,  (a0)
        ld      a7, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a4, e32, m1, ta, ma
        vle32.v v16, (a0)
        sub     a4, a4, t0
        vle32.v v0, (a6)
        vsll.vx v16, v16, a2
        vsll.vx v0, v0, a2
        vle32.v v24, (a1)
        sh2add  a1, t0, a1
        vle32.v v8, (a7)
        sh2add  a7, t0, a7
        vor.vv  v16, v24, v16
        vor.vv  v0, v8, v0
        vse32.v v16, (a0)
        sh2add  a0, t0, a0
        vse32.v v0, (a6)
        sh2add  a6, t0, a6
        bnez    a4, 1b

        ret
endfunc
#endif
