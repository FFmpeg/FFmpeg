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

func ff_vorbis_inverse_coupling_rvv, zve32f
        fmv.w.x   ft0, zero
1:
        vsetvli   t0, a2, e32, m1, ta, ma
        vle32.v   v16, (a1)
        sub       a2, a2, t0
        vle32.v   v24, (a0)
        vfmax.vf  v8, v16, ft0
        vfmin.vf  v16, v16, ft0
        vfsgnj.vv v8, v8, v24
        vfsgnj.vv v16, v16, v24
        vfsub.vv  v8, v24, v8
        vfsub.vv  v24, v24, v16
        vse32.v   v8, (a1)
        sh2add    a1, t0, a1
        vse32.v   v24, (a0)
        sh2add    a0, t0, a0
        bnez      a2, 1b

        ret
endfunc
