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

func ff_g722_apply_qmf_rvv, zve32x
        lpad    0
        lla         t0, qmf_coeffs
        vsetivli    zero, 12, e16, m2, ta, ma
        vlseg2e16.v v28, (a0)
        addi        t1, t0, 12 * 2
        vle16.v     v24, (t0)
        vle16.v     v26, (t1)
        vwmul.vv    v16, v28, v24
        vwmul.vv    v20, v30, v26
        vsetivli    zero, 12, e32, m4, ta, ma
        vmv.s.x     v10, zero
        vredsum.vs  v8, v16, v10
        vredsum.vs  v9, v20, v10
        vmv.x.s     t0, v8
        vmv.x.s     t1, v9
        sw          t0, 4(a1)
        sw          t1, 0(a1)
        ret
endfunc

const qmf_coeffs, align=2
        .short     3
        .short   -11
        .short    12
        .short    32
        .short  -210
        .short   951
        .short  3876
        .short  -805
        .short   362
        .short  -156
        .short    53
        .short   -11
        .short   -11
        .short    53
        .short  -156
        .short   362
        .short  -805
        .short  3876
        .short   951
        .short  -210
        .short    32
        .short    12
        .short   -11
        .short     3
endconst
