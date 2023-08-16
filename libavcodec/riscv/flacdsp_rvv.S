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

#include "libavutil/riscv/asm.S"

#if (__riscv_xlen == 64)
func ff_flac_decorrelate_ls_16_rvv, zve32x
        ld      a0,  (a0)
        ld      a2, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a3, e32, m8, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v8, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vsll.vx v8, v8, a4
        vsub.vv v8, v0, v8
        vsetvli zero, zero, e16, m4, ta, ma
        vncvt.x.x.w v16, v0
        vncvt.x.x.w v20, v8
        vsseg2e16.v v16, (a0)
        sh2add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_rs_16_rvv, zve32x
        ld      a0,  (a0)
        ld      a2, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a3, e32, m8, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v8, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vsll.vx v8, v8, a4
        vadd.vv v0, v0, v8
        vsetvli zero, zero, e16, m4, ta, ma
        vncvt.x.x.w v16, v0
        vncvt.x.x.w v20, v8
        vsseg2e16.v v16, (a0)
        sh2add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_ls_32_rvv, zve32x
        ld      a0,  (a0)
        ld      a2, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a3, e32, m4, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v4, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vsll.vx v4, v4, a4
        vsub.vv v4, v0, v4
        vsseg2e32.v v0, (a0)
        sh3add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_flac_decorrelate_rs_32_rvv, zve32x
        ld      a0,  (a0)
        ld      a2, 8(a1)
        ld      a1,  (a1)
1:
        vsetvli t0, a3, e32, m4, ta, ma
        vle32.v v0, (a1)
        sub     a3, a3, t0
        vle32.v v4, (a2)
        sh2add  a1, t0, a1
        vsll.vx v0, v0, a4
        sh2add  a2, t0, a2
        vsll.vx v4, v4, a4
        vadd.vv v0, v0, v4
        vsseg2e32.v v0, (a0)
        sh3add  a0, t0, a0
        bnez    a3, 1b

        ret
endfunc
#endif
