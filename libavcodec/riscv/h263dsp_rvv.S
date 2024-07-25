/*
 * Copyright © 2024 Rémi Denis-Courmont.
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

        .option push
        .option norelax
func ff_h263_h_loop_filter_rvv, zve32x
        lpad    0
        addi        a0, a0, -2
        vsetivli    zero, 8, e8, mf2, ta, ma
        vlsseg4e8.v v8, (a0), a1
        jal         t0, 1f
        vssseg4e8.v v8, (a0), a1
        ret
1:
        auipc       t1, %pcrel_hi(ff_h263_loop_filter_strength)
        vwsubu.vv   v14, v10, v9       # p2 - p1
        add         t1, t1, a2
        vwsubu.vv   v12, v8, v11       # p0 - p3
        vsetvli     zero, zero, e16, m1, ta, mu
        vsll.vi     v14, v14, 2
        lbu         t1, %pcrel_lo(1b)(t1) # strength
        vadd.vv     v16, v12, v14
        # Divide by 8 toward 0. v16 is a signed 10-bit value at this point.
        vsrl.vi     v18, v16, 16 - 3   # v18 = (v16 < 0) ? 7 : 0
        slli        t2, t1, 1          # 2 * strength
        vadd.vv     v16, v16, v18
        # v16 (d) is signed 7-bit, but later arithmetics require 9 bits.
        vsra.vi     v16, v16, 3        # d
        vmv.v.x     v20, t2
        vmslt.vi    v0, v16, 0
        vneg.v      v18, v16
        vneg.v      v20, v20, v0.t     # sign(d) * 2 * strength
        vmax.vv     v18, v16, v18      # |d|
        vsub.vv     v20, v20, v16      # d1 if strength <= |d| <= 2 * strength
        vmsge.vx    v0, v18, t2
        vsrl.vi     v14, v12, 16 - 2   # v14 = (v12 < 0) ? 3 : 0
        vmerge.vxm  v20, v20, zero, v0 # d1 if strength <= |d|
        vadd.vv     v12, v12, v14
        vmsge.vx    v0, v18, t1
        vsra.vi     v12, v12, 2        # (p0 - p3) / 4
        vmerge.vvm  v16, v16, v20, v0  # d1
        vzext.vf2   v24, v8     # p0 as u16 (because vwrsubu.wv does not exist)
        vneg.v      v14, v16
        vzext.vf2   v26, v9            # p1 as u16
        vmax.vv     v14, v16, v14      # |d1|
        vzext.vf2   v28, v10           # p2 as u16
        vsra.vi     v14, v14, 1        # ad1
        vadd.vv     v26, v26, v16      # p1 + d1
        vneg.v      v18, v14           # -ad1
        vmin.vv     v12, v12, v14
        vsub.vv     v28, v28, v16      # p2 - d1
        vmax.vv     v12, v12, v18      # d2
        vmax.vx     v26, v26, zero
        vsub.vv     v24, v24, v12      # p0 - d2
        vmax.vx     v28, v28, zero
        vsetvli     zero, zero, e8, mf2, ta, ma
        vwaddu.wv   v30, v12, v11      # p3 + d2
        vncvt.x.x.w v8, v24
        vnclipu.wi  v9, v26, 0
        vnclipu.wi  v10, v28, 0
        vncvt.x.x.w v11, v30
        jr          t0
endfunc
        .option pop

func ff_h263_v_loop_filter_rvv, zve32x
        lpad    0
        sub         a4, a0, a1
        vsetivli    zero, 8, e8, mf2, ta, ma
        vle8.v      v10, (a0)
        sub         a3, a4, a1
        vle8.v      v9, (a4)
        add         a5, a0, a1
        vle8.v      v8, (a3)
        vle8.v      v11, (a5)
        jal         t0, 1b
        vse8.v      v8, (a3)
        vse8.v      v9, (a4)
        vse8.v      v10, (a0)
        vse8.v      v11, (a5)
        ret
endfunc
