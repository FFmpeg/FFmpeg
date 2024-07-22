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
        lpad    0
        li      t1, 0x00ff00ff
        j       1f
endfunc

func ff_shuffle_bytes_2103_rvv, zve32x, zba
        lpad    0
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
        lpad    0
        li      t1, 24
        li      t2, 8
        j       3f
endfunc

func ff_shuffle_bytes_3012_rvv, zve32x, zba
        lpad    0
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

func ff_interleave_bytes_rvv, zve32x, zba
        lpad    0
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
        bnez       t3, 2b

        add     a0, a0, a5
        add     a1, a1, a6
        add     a2, a2, a7
        bnez    a4, 1b

        ret
endfunc

func ff_deinterleave_bytes_rvv, zve32x, zba
        lpad    0
1:
        mv      t0, a0
        mv      t1, a1
        mv      t2, a2
        mv      t3, a3
        addi    a4, a4, -1
2:
        vsetvli    t4, t3, e8, m4, ta, ma
        sub        t3, t3, t4
        vlseg2e8.v v8, (t0)
        sh1add     t0, t4, t0
        vse8.v     v8, (t1)
        add        t1, t4, t1
        vse8.v     v12, (t2)
        add        t2, t4, t2
        bnez       t3, 2b

        add     a0, a0, a5
        add     a1, a1, a6
        add     a2, a2, a7
        bnez    a4, 1b

        ret
endfunc

.macro yuy2_to_i422p luma, chroma
        lpad    0
        srai    t4, a4, 1 // pixel width -> chroma width
        lw      t6, (sp)
        slli    t5, a4, 1 // pixel width -> (source) byte width
        sub     a6, a6, a4
        sub     a7, a7, t4
        sub     t6, t6, t5
        vsetvli t2, zero, e8, m4, ta, ma
1:
        mv      t4, a4
        addi    a5, a5, -1
2:
        min     t0, t2, t4 // ensure even VL on penultimate iteration
        vsetvli t0, t0, e8, m4, ta, ma
        vlseg2e8.v v16, (a3)
        srli    t1, t0, 1
        vsetvli zero, t1, e8, m2, ta, ma
        vnsrl.wi   v24, \chroma, 0 // U
        sub     t4, t4, t0
        vnsrl.wi   v28, \chroma, 8 // V
        sh1add  a3, t0, a3
        vse8.v  v24, (a1)
        add     a1, t1, a1
        vse8.v  v28, (a2)
        add     a2, t1, a2
        vsetvli zero, t0, e8, m4, ta, ma
        vse8.v  \luma, (a0)
        add     a0, t0, a0
        bnez    t4, 2b

        add     a3, a3, t6
        add     a0, a0, a6
        add     a1, a1, a7
        add     a2, a2, a7
        bnez    a5, 1b

        ret
.endm

func ff_uyvytoyuv422_rvv, zve32x, b
        yuy2_to_i422p v20, v16
endfunc

func ff_yuyvtoyuv422_rvv, zve32x, b
        yuy2_to_i422p v16, v20
endfunc
