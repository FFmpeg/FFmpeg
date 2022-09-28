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
        addi    t1, a0, 3
        addi    t2, a0, 2
        addi    t3, a0, 1
1:
        srai    a2, a2, 2
        li      t4, 4
2:
        vsetvli    t0, a2, e8, m1, ta, ma
        sub        a2, a2, t0
        vlse8.v    v8, (a0), t4
        sh2add     a0, t0, a0
        vlse8.v    v9, (t1), t4
        sh2add     t1, t0, t1
        vlse8.v    v10, (t2), t4
        sh2add     t2, t0, t2
        vlse8.v    v11, (t3), t4
        sh2add     t3, t0, t3
        vsseg4e8.v v8, (a1)
        sh2add     a1, t0, a1
        bnez       a2, 2b

        ret
endfunc

func ff_shuffle_bytes_2103_rvv, zve32x
        addi    t1, a0, 1
        addi    t2, a0, 0
        addi    t3, a0, 3
        addi    a0, a0, 2
        j       1b
endfunc

func ff_shuffle_bytes_1230_rvv, zve32x
        addi    t1, a0, 2
        addi    t2, a0, 3
        addi    t3, a0, 0
        addi    a0, a0, 1
        j       1b
endfunc

func ff_shuffle_bytes_3012_rvv, zve32x
        addi    t1, a0, 0
        addi    t2, a0, 1
        addi    t3, a0, 2
        addi    a0, a0, 3
        j       1b
endfunc

func ff_shuffle_bytes_3210_rvv, zve32x
        addi    t1, a0, 2
        addi    t2, a0, 1
        addi    t3, a0, 0
        addi    a0, a0, 3
        j       1b
endfunc

func ff_interleave_bytes_rvv, zve32x
1:
        mv      t0, a0
        mv      t1, a1
        mv      t2, a2
        mv      t3, a3
        addi    a4, a4, -1
2:
        vsetvli    t4, t3, e8, ta, ma
        sub        t3, t3, t4
        vle8.v     v8, (t0)
        add        t0, t4, t0
        vle8.v     v9, (t1)
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

#if (__riscv_xlen == 64)
.macro yuy2_to_i422p v_y0, v_y1, v_u, v_v
        addi    sp, sp, -16
        sd      s0,   (sp)
        sd      s1,  8(sp)
        addi    a4, a4, 1
        lw      s0, 16(sp)
        srai    a4, a4, 1 // pixel width -> chroma width
        li      s1, 2
1:
        mv      t4, a4
        mv      t3, a3
        mv      t0, a0
        addi    t6, a0, 1
        mv      t1, a1
        mv      t2, a2
        addi    a5, a5, -1
2:
        vsetvli    t5, t4, e8, m1, ta, ma
        sub        t4, t4, t5
        vlseg4e8.v v8, (t3)
        sh2add     t3, t5, t3
        vsse8.v    \v_y0, (t0), s1
        sh1add     t0, t5, t0
        vsse8.v    \v_y1, (t6), s1
        sh1add     t6, t5, t6
        vse8.v     \v_u, (t1)
        add        t1, t5, t1
        vse8.v     \v_v, (t2)
        add        t2, t5, t2
        bnez       t4, 2b

        add     a3, a3, s0
        add     a0, a0, a6
        add     a1, a1, a7
        add     a2, a2, a7
        bnez    a5, 1b

        ld      s1,  8(sp)
        ld      s0,   (sp)
        addi    sp, sp, 16
        ret
.endm

func ff_uyvytoyuv422_rvv, zve32x
        yuy2_to_i422p v9, v11, v8, v10
endfunc

func ff_yuyvtoyuv422_rvv, zve32x
        yuy2_to_i422p v8, v10, v9, v11
endfunc
#endif
