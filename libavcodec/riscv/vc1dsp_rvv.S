/*
 * Copyright (c) 2023 Institue of Software Chinese Academy of Sciences (ISCAS).
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

func ff_vc1_inv_trans_8x8_dc_rvv, zve64x
        lh            t2, (a2)
        vsetivli      zero, 8, e8, mf2, ta, ma
        vlse64.v      v0, (a0), a1
        sh1add        t2, t2, t2
        addi          t2, t2, 1
        srai          t2, t2, 1
        sh1add        t2, t2, t2
        addi          t2, t2, 16
        srai          t2, t2, 5
        li            t0, 8*8
        vsetvli       zero, t0, e16, m8, ta, ma
        vzext.vf2     v8, v0
        vadd.vx       v8, v8, t2
        vmax.vx       v8, v8, zero
        vsetvli       zero, zero, e8, m4, ta, ma
        vnclipu.wi    v0, v8, 0
        vsetivli      zero, 8, e8, mf2, ta, ma
        vsse64.v      v0, (a0), a1
        ret
endfunc

func ff_vc1_inv_trans_4x8_dc_rvv, zve32x
        lh            t2, (a2)
        vsetivli      zero, 8, e8, mf2, ta, ma
        vlse32.v      v0, (a0), a1
        slli          t1, t2, 4
        add           t2, t2, t1
        addi          t2, t2, 4
        srai          t2, t2, 3
        sh1add        t2, t2, t2
        slli          t2, t2, 2
        addi          t2, t2, 64
        srai          t2, t2, 7
        li            t0, 4*8
        vsetvli       zero, t0, e16, m4, ta, ma
        vzext.vf2     v4, v0
        vadd.vx       v4, v4, t2
        vmax.vx       v4, v4, zero
        vsetvli       zero, zero, e8, m2, ta, ma
        vnclipu.wi    v0, v4, 0
        vsetivli      zero, 8, e8, mf2, ta, ma
        vsse32.v      v0, (a0), a1
        ret
endfunc

func ff_vc1_inv_trans_8x4_dc_rvv, zve64x
        lh            t2, (a2)
        vsetivli      zero, 4, e8, mf4, ta, ma
        vlse64.v      v0, (a0), a1
        sh1add        t2, t2, t2
        addi          t2, t2, 1
        srai          t2, t2, 1
        slli          t1, t2, 4
        add           t2, t2, t1
        addi          t2, t2, 64
        srai          t2, t2, 7
        li            t0, 8*4
        vsetvli       zero, t0, e16, m4, ta, ma
        vzext.vf2     v4, v0
        vadd.vx       v4, v4, t2
        vmax.vx       v4, v4, zero
        vsetvli       zero, zero, e8, m2, ta, ma
        vnclipu.wi    v0, v4, 0
        vsetivli      zero, 4, e8, mf4, ta, ma
        vsse64.v      v0, (a0), a1
        ret
endfunc

func ff_vc1_inv_trans_4x4_dc_rvv, zve32x
        lh            t2, (a2)
        vsetivli      zero, 4, e8, mf4, ta, ma
        vlse32.v      v0, (a0), a1
        slli          t1, t2, 4
        add           t2, t2, t1
        addi          t2, t2, 4
        srai          t2, t2, 3
        slli          t1, t2, 4
        add           t2, t2, t1
        addi          t2, t2, 64
        srai          t2, t2, 7
        vsetivli      zero, 4*4, e16, m2, ta, ma
        vzext.vf2     v2, v0
        vadd.vx       v2, v2, t2
        vmax.vx       v2, v2, zero
        vsetvli       zero, zero, e8, m1, ta, ma
        vnclipu.wi    v0, v2, 0
        vsetivli      zero, 4, e8, mf4, ta, ma
        vsse32.v      v0, (a0), a1
        ret
endfunc

.macro mspel_op op pos n1 n2
        add           t1, \pos, a2
        v\op\()e8.v   v\n1, (\pos)
        sh1add        \pos, a2, \pos
        v\op\()e8.v   v\n2, (t1)
.endm

.macro mspel_op_all op pos a1 a2 a3 a4 a5 a6 a7 a8 a9 a10 a11 a12 a13 a14 a15 a16
        mspel_op      \op \pos \a1 \a2
        mspel_op      \op \pos \a3 \a4
        mspel_op      \op \pos \a5 \a6
        mspel_op      \op \pos \a7 \a8
        mspel_op      \op \pos \a9 \a10
        mspel_op      \op \pos \a11 \a12
        mspel_op      \op \pos \a13 \a14
        mspel_op      \op \pos \a15 \a16
.endm

func ff_avg_pixels16x16_rvv, zve32x
        csrwi         vxrm, 0
        vsetivli      zero, 16, e8, m1, ta, ma
        mspel_op_all  l a1 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
        mspel_op_all  l a0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
        vsetvli       t0, zero, e8, m8, ta, ma
        sub           a0, a0, a2
        vaaddu.vv     v0, v0, v16
        neg           a2, a2
        vaaddu.vv     v8, v8, v24
        vsetivli      zero, 16, e8, m1, ta, ma
        mspel_op_all  s a0 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0

        ret
endfunc

func ff_avg_pixels8x8_rvv, zve64x
        csrwi         vxrm, 0
        li            t0, 64
        vsetivli      zero, 8, e8, mf2, ta, ma
        vlse64.v      v16, (a1), a2
        vlse64.v      v8, (a0), a2
        vsetvli       zero, t0, e8, m4, ta, ma
        vaaddu.vv     v16, v16, v8
        vsetivli      zero, 8, e8, mf2, ta, ma
        vsse64.v      v16, (a0), a2

        ret
endfunc
