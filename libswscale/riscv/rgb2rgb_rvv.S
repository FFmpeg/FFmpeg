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

func ff_shuffle_bytes_0321_rvv, zve32x
        li      t1, 0x00ff00ff
        j       1f
endfunc

func ff_shuffle_bytes_2103_rvv, zve32x
        li      t1, ~0x00ff00ff
1:
        not     t2, t1
        srai    a2, a2, 2
2:
        vsetvli t0, a2, e32, m8, ta, ma
        vle32.v v8, (a0)
        sub     a2, a2, t0
        vand.vx v16, v8, t2
        sh2add  a0, t0, a0
        vand.vx v8, v8, t1
        vsrl.vi v24, v16, 16
        vsll.vi v16, v16, 16
        vor.vv  v8, v8, v24
        vor.vv  v8, v16, v8
        vse32.v v8, (a1)
        sh2add  a1, t0, a1
        bnez    a2, 2b

        ret
endfunc

func ff_shuffle_bytes_1230_rvv, zve32x
        li      t1, 24
        li      t2, 8
        j       3f
endfunc

func ff_shuffle_bytes_3012_rvv, zve32x
        li      t1, 8
        li      t2, 24
3:
        srai    a2, a2, 2
4:
        vsetvli t0, a2, e32, m8, ta, ma
        vle32.v v8, (a0)
        sub     a2, a2, t0
        vsll.vx v16, v8, t1
        sh2add  a0, t0, a0
        vsrl.vx v8, v8, t2
        vor.vv  v16, v16, v8
        vse32.v v16, (a1)
        sh2add  a1, t0, a1
        bnez    a2, 4b

        ret
endfunc

func ff_interleave_bytes_rvv, zve32x
1:
        mv      t0, a0
        mv      t1, a1
        mv      t2, a2
        mv      t3, a3
        addi    a4, a4, -1
2:
        vsetvli    t4, t3, e8, m4, ta, ma
        sub        t3, t3, t4
        vle8.v     v8, (t0)
        add        t0, t4, t0
        vle8.v     v12, (t1)
        add        t1, t4, t1
        vsseg2e8.v v8, (t2)
        sh1add     t2, t4, t2
        bnez       t4, 2b

        add     a0, a0, a5
        add     a1, a1, a6
        add     a2, a2, a7
        bnez    a4, 1b

        ret
endfunc

.macro yuy2_to_i422p y_shift
        addi    a4, a4, 1
        lw      t6, (sp)
        srai    a4, a4, 1 // pixel width -> chroma width
1:
        mv      t4, a4
        mv      t3, a3
        mv      t0, a0
        mv      t1, a1
        mv      t2, a2
        addi    a5, a5, -1
2:
        vsetvli    t5, t4, e8, m2, ta, ma
        vlseg2e16.v v16, (t3)
        sub        t4, t4, t5
        vnsrl.wi   v24, v16, \y_shift // Y0
        sh2add     t3, t5, t3
        vnsrl.wi   v26, v20, \y_shift // Y1
        vnsrl.wi   v28, v16, 8 - \y_shift // U
        vnsrl.wi   v30, v20, 8 - \y_shift // V
        vsseg2e8.v v24, (t0)
        sh1add     t0, t5, t0
        vse8.v     v28, (t1)
        add        t1, t5, t1
        vse8.v     v30, (t2)
        add        t2, t5, t2
        bnez       t4, 2b

        add     a3, a3, t6
        add     a0, a0, a6
        add     a1, a1, a7
        add     a2, a2, a7
        bnez    a5, 1b

        ret
.endm

func ff_uyvytoyuv422_rvv, zve32x
        yuy2_to_i422p 8
endfunc

func ff_yuyvtoyuv422_rvv, zve32x
        yuy2_to_i422p 0
endfunc
