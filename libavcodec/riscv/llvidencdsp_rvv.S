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

func ff_llvidenc_diff_bytes_rvv, zve32x
        lpad    0
1:
        vsetvli t0, a3, e8, m8, ta, ma
        vle8.v  v0, (a1)
        sub     a3, a3, t0
        vle8.v  v8, (a2)
        add     a1, t0, a1
        vsub.vv v8, v0, v8
        add     a2, t0, a2
        vse8.v  v8, (a0)
        add     a0, t0, a0
        bnez    a3, 1b

        ret
endfunc

func ff_llvidenc_sub_median_pred_rvv, zve32x
        lpad    0
        lw      t4, (a4)
        lw      t5, (a5)
1:
        vsetvli t3, a3, e8, m4, ta, ma
        vle8.v  v16, (a1)            # src1
        sub     a3, a3, t3
        vle8.v  v24, (a2)            # src2
        add     a1, t3, a1
        vslide1up.vx    v20, v16, t5 # lt
        add     a2, t3, a2
        vslide1up.vx    v28, v24, t4 # l
        lbu     t5, -1(a1)
        vsub.vv     v20, v16, v20
        lbu     t4, -1(a2)
        vmaxu.vv    v8, v28, v16
        vadd.vv     v20, v28, v20    # l + src1 - lt
        vminu.vv    v12, v28, v16
        vminu.vv    v8, v8, v20
        vmaxu.vv    v8, v12, v8      # mid_pred(...)
        vsub.vv     v8, v24, v8
        vse8.v  v8, (a0)
        add     a0, t3, a0
        bnez    a3, 1b

        sw      t4, (a4)
        sw      t5, (a5)
        ret
endfunc

func ff_llvidenc_sub_left_predict_rvv, zve32x
        lpad    0
        li      a5, -0x80
        sub     a2, a2, a3
1:
        mv      t3, a3
        addi    a4, a4, -1
2:
        vsetvli t0, t3, e8, m8, ta, ma
        vle8.v  v16, (a1)
        sub     t3, t3, t0
        vslide1up.vx    v24, v16, a5
        add     a1, a1, t0
        vsub.vv v8, v16, v24
        lb      a5, -1(a1)
        vse8.v  v8, (a0)
        add     a0, a0, t0
        bnez    t3, 2b

        add     a1, a1, a2
        bnez    a4, 1b

        ret
endfunc
