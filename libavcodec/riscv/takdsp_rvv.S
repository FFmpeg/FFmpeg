/*
 * Copyright (c) 2023 Institue of Software Chinese Academy of Sciences (ISCAS).
 * Copyright (c) 2023 Rémi Denis-Courmont
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

func ff_decorrelate_ls_rvv, zve32x, zba
        lpad    0
1:
        vsetvli  t0, a2, e32, m8, ta, ma
        sub      a2, a2, t0
        vle32.v  v0, (a0)
        sh2add   a0, t0, a0
        vle32.v  v8, (a1)
        vadd.vv  v16, v0, v8
        vse32.v  v16, (a1)
        sh2add   a1, t0, a1
        bnez a2, 1b
        ret
endfunc

func ff_decorrelate_sr_rvv, zve32x, zba
        lpad    0
1:
        vsetvli  t0, a2, e32, m8, ta, ma
        vle32.v  v0, (a0)
        sub      a2, a2, t0
        vle32.v  v8, (a1)
        sh2add   a1, t0, a1
        vsub.vv  v16, v8, v0
        vse32.v  v16, (a0)
        sh2add   a0, t0, a0
        bnez     a2, 1b
        ret
endfunc

func ff_decorrelate_sm_rvv, zve32x, zba
        lpad    0
1:
        vsetvli  t0, a2, e32, m8, ta, ma
        vle32.v  v8, (a1)
        sub      a2, a2, t0
        vle32.v  v0, (a0)
        vsra.vi  v16, v8, 1
        vsub.vv  v0, v0, v16
        vse32.v  v0, (a0)
        sh2add   a0, t0, a0
        vadd.vv  v8, v0, v8
        vse32.v  v8, (a1)
        sh2add   a1, t0, a1
        bnez     a2, 1b

        ret
endfunc

func ff_decorrelate_sf_rvv, zve32x, zba
        lpad    0
        csrwi    vxrm, 0
1:
        vsetvli  t0, a2, e32, m8, ta, ma
        vle32.v  v8, (a1)
        sub      a2, a2, t0
        vsra.vx  v8, v8, a3
        sh2add   a1, t0, a1
        vle32.v  v0, (a0)
        vmul.vx  v8, v8, a4
        vssra.vi v8, v8, 8
        vsll.vx  v8, v8, a3
        vsub.vv  v0, v8, v0
        vse32.v  v0, (a0)
        sh2add   a0, t0, a0
        bnez     a2, 1b

        ret
endfunc
