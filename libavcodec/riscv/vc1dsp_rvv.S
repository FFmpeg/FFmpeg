/*
 * Copyright (c) 2023 Institue of Software Chinese Academy of Sciences (ISCAS).
 * Copyright (c) 2024 Rémi Denis-Courmont.
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

        .variant_cc ff_vc1_inv_trans_8_rvv
func ff_vc1_inv_trans_8_rvv, zve32x
        li      t4, 12
        vsll.vi v18, v6, 4
        li      t2, 6
        vmul.vx v8, v0, t4
        li      t5, 15
        vmul.vx v10, v4, t4
        li      t3, 9
        # t[2..5] = [6 9 12 15]
        vsll.vi v12, v2, 4
        vmul.vx v14, v6, t2
        vmul.vx v16, v2, t2
        vadd.vv v26, v12, v14 # t3
        vadd.vv v24, v8, v10  # t1
        vsub.vv v25, v8, v10  # t2
        vsub.vv v27, v16, v18 # t4
        vadd.vv v28, v24, v26 # t5
        vsub.vv v31, v24, v26 # t8
        vadd.vv v29, v25, v27 # t6
        vsub.vv v30, v25, v27 # t7
        vsll.vi v20, v1, 4
        vmul.vx v21, v3, t5
        vmul.vx v22, v5, t3
        vsll.vi v23, v7, 2
        vadd.vv v20, v20, v21
        vadd.vv v22, v22, v23
        vsll.vi v21, v3, 2
        vadd.vv v16, v20, v22 # t1
        vmul.vx v20, v1, t5
        vsll.vi v22, v5, 4
        vmul.vx v23, v7, t3
        vsub.vv v20, v20, v21
        vadd.vv v22, v22, v23
        vsll.vi v21, v3, 4
        vsub.vv v17, v20, v22 # t2
        vmul.vx v20, v1, t3
        vsll.vi v22, v5, 2
        vmul.vx v23, v7, t5
        vsub.vv v20, v20, v21
        vadd.vv v22, v22, v23
        vmul.vx v21, v3, t3
        vadd.vv v18, v20, v22 # t3
        vsll.vi v20, v1, 2
        vmul.vx v22, v5, t5
        vsll.vi v23, v7, 4
        vsub.vv v20, v20, v21
        vsub.vv v22, v22, v23
        vadd.vv v0, v28, v16
        vadd.vv v19, v20, v22 # t4
        vadd.vv v1, v29, v17
        vadd.vv v2, v30, v18
        vadd.vv v3, v31, v19
        vsub.vv v4, v31, v19
        vsub.vv v5, v30, v18
        vsub.vv v6, v29, v17
        vsub.vv v7, v28, v16
        jr      t0
endfunc

        .variant_cc ff_vc1_inv_trans_4_rvv
func ff_vc1_inv_trans_4_rvv, zve32x
        li      t3, 17
        vmul.vx v8, v0, t3
        li      t4, 22
        vmul.vx v10, v2, t3
        li      t2, 10
        vmul.vx v14, v1, t4
        vadd.vv v24, v8, v10  # t1
        vsub.vv v25, v8, v10  # t2
        vmul.vx v16, v3, t2
        vmul.vx v18, v3, t4
        vmul.vx v20, v1, t2
        vadd.vv v26, v14, v16 # t3
        vsub.vv v27, v18, v20 # t4
        vadd.vv v0, v24, v26
        vsub.vv v1, v25, v27
        vadd.vv v2, v25, v27
        vsub.vv v3, v24, v26
        .irp    n,0,1,2,3
        vssra.vx v\n, v\n, t1 # + 4 >> 3 or + 64 >> 7
        .endr
        jr      t0
endfunc

func ff_vc1_inv_trans_8x8_rvv, zve32x
        csrwi    vxrm, 0
        vsetivli zero, 8, e16, m1, ta, ma
        addi     a1, a0, 1 * 8 * 2
        vle16.v  v0, (a0)
        addi     a2, a0, 2 * 8 * 2
        vle16.v  v1, (a1)
        addi     a3, a0, 3 * 8 * 2
        vle16.v  v2, (a2)
        addi     a4, a0, 4 * 8 * 2
        vle16.v  v3, (a3)
        addi     a5, a0, 5 * 8 * 2
        vle16.v  v4, (a4)
        addi     a6, a0, 6 * 8 * 2
        vle16.v  v5, (a5)
        addi     a7, a0, 7 * 8 * 2
        vle16.v  v6, (a6)
        vle16.v  v7, (a7)
        jal      t0, ff_vc1_inv_trans_8_rvv
        .irp n,0,1,2,3,4,5,6,7
        vssra.vi v\n, v\n, 3
        .endr
        vsseg8e16.v v0, (a0)
        .irp n,0,1,2,3,4,5,6,7
        vle16.v  v\n, (a\n)
        .endr
        jal      t0, ff_vc1_inv_trans_8_rvv
        vadd.vi  v4, v4, 1
        vadd.vi  v5, v5, 1
        vssra.vi v4, v4, 7
        vssra.vi v5, v5, 7
        vse16.v  v4, (a4)
        vadd.vi  v6, v6, 1
        vse16.v  v5, (a5)
        vadd.vi  v7, v7, 1
        vssra.vi v6, v6, 7
        vssra.vi v7, v7, 7
        vse16.v  v6, (a6)
        vssra.vi v0, v0, 7
        vse16.v  v7, (a7)
        vssra.vi v1, v1, 7
        vse16.v  v0, (a0)
        vssra.vi v2, v2, 7
        vse16.v  v1, (a1)
        vssra.vi v3, v3, 7
        vse16.v  v2, (a2)
        vse16.v  v3, (a3)
        ret
endfunc

func ff_vc1_inv_trans_8x4_rvv, zve32x
        csrwi       vxrm, 0
        vsetivli    zero, 4, e16, mf2, ta, ma
        vlseg8e16.v v0, (a2)
        jal         t0, ff_vc1_inv_trans_8_rvv
        vsseg8e16.v v0, (a2)
        addi        a3, a2, 1 * 8 * 2
        vsetivli    zero, 8, e16, m1, ta, ma
        vle16.v     v0, (a2)
        addi        a4, a2, 2 * 8 * 2
        vle16.v     v1, (a3)
        addi        a5, a2, 3 * 8 * 2
        vle16.v     v2, (a4)
        vle16.v     v3, (a5)
        .irp    n,0,1,2,3
        # shift 4 vectors of 8 elems after transpose instead of 8 of 4
        vssra.vi    v\n, v\n, 3
        .endr
        li          t1, 7
        jal         t0, ff_vc1_inv_trans_4_rvv
        add         a3, a1, a0
        vle8.v      v8, (a0)
        add         a4, a1, a3
        vle8.v      v9, (a3)
        add         a5, a1, a4
        vle8.v      v10, (a4)
        vle8.v      v11, (a5)
        vsetvli     zero, zero, e8, mf2, ta, ma
        vwaddu.wv   v0, v0, v8
        vwaddu.wv   v1, v1, v9
        vwaddu.wv   v2, v2, v10
        vwaddu.wv   v3, v3, v11
        vsetvli     zero, zero, e16, m1, ta, ma
        .irp    n,0,1,2,3
        vmax.vx     v\n, v\n, zero
        .endr
        vsetvli     zero, zero, e8, mf2, ta, ma
        vnclipu.wi  v8, v0, 0
        vnclipu.wi  v9, v1, 0
        vse8.v      v8, (a0)
        vnclipu.wi  v10, v2, 0
        vse8.v      v9, (a3)
        vnclipu.wi  v11, v3, 0
        vse8.v      v10, (a4)
        vse8.v      v11, (a5)
        ret
endfunc

func ff_vc1_inv_trans_4x8_rvv, zve32x
        li           a3, 8 * 2
        csrwi        vxrm, 0
        vsetivli     zero, 8, e16, m1, ta, ma
        vlsseg4e16.v v0, (a2), a3
        li           t1, 3
        jal          t0, ff_vc1_inv_trans_4_rvv
        vssseg4e16.v v0, (a2), a3
        vsetivli     zero, 4, e16, mf2, ta, ma
        addi         t1, a2, 1 * 8 * 2
        vle16.v      v0, (a2)
        addi         t2, a2, 2 * 8 * 2
        vle16.v      v1, (t1)
        addi         t3, a2, 3 * 8 * 2
        vle16.v      v2, (t2)
        addi         t4, a2, 4 * 8 * 2
        vle16.v      v3, (t3)
        addi         t5, a2, 5 * 8 * 2
        vle16.v      v4, (t4)
        addi         t6, a2, 6 * 8 * 2
        vle16.v      v5, (t5)
        addi         t1, a2, 7 * 8 * 2
        vle16.v      v6, (t6)
        vle16.v      v7, (t1)

        jal          t0, ff_vc1_inv_trans_8_rvv
        vadd.vi      v4, v4, 1
        add          t0, a1, a0
        vadd.vi      v5, v5, 1
        vadd.vi      v6, v6, 1
        add          t1, a1, t0
        vadd.vi      v7, v7, 1
        vssra.vi     v0, v0, 7
        add          t2, a1, t1
        vssra.vi     v1, v1, 7
        vssra.vi     v2, v2, 7
        add          t3, a1, t2
        vssra.vi     v3, v3, 7
        vssra.vi     v4, v4, 7
        add          t4, a1, t3
        vssra.vi     v5, v5, 7
        vssra.vi     v6, v6, 7
        add          t5, a1, t4
        vssra.vi     v7, v7, 7
        vle8.v       v8, (a0)
        add          t6, a1, t5
        vle8.v       v9, (t0)
        vle8.v       v10, (t1)
        vle8.v       v11, (t2)
        vle8.v       v12, (t3)
        vle8.v       v13, (t4)
        vle8.v       v14, (t5)
        vle8.v       v15, (t6)
        vsetvli      zero, zero, e8, mf4, ta, ma
        vwaddu.wv    v0, v0, v8
        vwaddu.wv    v1, v1, v9
        vwaddu.wv    v2, v2, v10
        vwaddu.wv    v3, v3, v11
        vwaddu.wv    v4, v4, v12
        vwaddu.wv    v5, v5, v13
        vwaddu.wv    v6, v6, v14
        vwaddu.wv    v7, v7, v15
        vsetvli      zero, zero, e16, mf2, ta, ma
        .irp    n,0,1,2,3,4,5,6,7
        vmax.vx      v\n, v\n, zero
        .endr
        vsetvli      zero, zero, e8, mf4, ta, ma
        vnclipu.wi   v8, v0, 0
        vnclipu.wi   v9, v1, 0
        vse8.v       v8, (a0)
        vnclipu.wi   v10, v2, 0
        vse8.v       v9, (t0)
        vnclipu.wi   v11, v3, 0
        vse8.v       v10, (t1)
        vnclipu.wi   v12, v4, 0
        vse8.v       v11, (t2)
        vnclipu.wi   v13, v5, 0
        vse8.v       v12, (t3)
        vnclipu.wi   v14, v6, 0
        vse8.v       v13, (t4)
        vnclipu.wi   v15, v7, 0
        vse8.v       v14, (t5)
        vse8.v       v15, (t6)
        ret
endfunc

func ff_vc1_inv_trans_4x4_rvv, zve32x
        li           a3, 8 * 2
        csrwi        vxrm, 0
        vsetivli     zero, 4, e16, mf2, ta, ma
        vlsseg4e16.v v0, (a2), a3
        li           t1, 3
        jal          t0, ff_vc1_inv_trans_4_rvv
        vssseg4e16.v v0, (a2), a3
        addi         t1, a2, 2 * 4 * 2
        vle16.v      v0, (a2)
        addi         t2, a2, 4 * 4 * 2
        vle16.v      v1, (t1)
        addi         t3, a2, 6 * 4 * 2
        vle16.v      v2, (t2)
        vle16.v      v3, (t3)
        li           t1, 7
        jal          t0, ff_vc1_inv_trans_4_rvv
        add          t1, a1, a0
        vle8.v       v8, (a0)
        add          t2, a1, t1
        vle8.v       v9, (t1)
        add          t3, a1, t2
        vle8.v       v10, (t2)
        vle8.v       v11, (t3)
        vsetvli      zero, zero, e8, mf4, ta, ma
        vwaddu.wv    v0, v0, v8
        vwaddu.wv    v1, v1, v9
        vwaddu.wv    v2, v2, v10
        vwaddu.wv    v3, v3, v11
        vsetvli      zero, zero, e16, mf2, ta, ma
        .irp    n,0,1,2,3
        vmax.vx      v\n, v\n, zero
        .endr
        vsetvli      zero, zero, e8, mf4, ta, ma
        vnclipu.wi   v8, v0, 0
        vnclipu.wi   v9, v1, 0
        vse8.v       v8, (a0)
        vnclipu.wi   v10, v2, 0
        vse8.v       v9, (t1)
        vnclipu.wi   v11, v3, 0
        vse8.v       v10, (t2)
        vse8.v       v11, (t3)
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
        li       t0, 16
        vsetivli zero, 16, e8, m1, ta, ma
        j        1f
endfunc

func ff_avg_pixels8x8_rvv, zve32x
        li        t0, 8
        vsetivli  zero, 8, e8, mf2, ta, ma
1:
        csrwi     vxrm, 0
2:
        vle8.v    v16, (a1)
        addi      t0, t0, -1
        vle8.v    v8, (a0)
        add       a1, a1, a2
        vaaddu.vv v16, v16, v8
        vse8.v    v16, (a0)
        add       a0, a0, a2
        bnez      t0, 2b

        ret
endfunc

func ff_vc1_unescape_buffer_rvv, zve32x
        vsetivli       zero, 2, e8, m1, ta, ma
        vmv.v.i        v8, -1
        li             t4, 1
        vmv.v.i        v12, -1
        li             t3, -1
        mv             t5, a2
        blez           a1, 3f
1:
        vsetvli        t0, a1, e8, m4, ta, ma
        vle8.v         v16, (a0)
        vslideup.vi    v8, v16, 2
        addi           t0, t0, -1 # we cannot fully process the last element
        vslideup.vi    v12, v16, 1
        vslide1down.vx v20, v16, t3
        vsetvli        zero, t0, e8, m4, ta, ma
        vmseq.vi       v0, v8, 0
        vmseq.vi       v1, v12, 0
        vmseq.vi       v2, v16, 3
        vmand.mm       v0, v0, v1
        vmsltu.vi      v3, v20, 4
        vmand.mm       v0, v0, v2
        vmand.mm       v0, v0, v3
        vfirst.m       t2, v0
        bgez           t2, 4f # found an escape byte?

        vse8.v         v16, (a2)
        addi           t2, t0, -2
        add            a2, a2, t0
2:
        vslidedown.vx  v8, v16, t2
        sub            a1, a1, t0
        vslidedown.vi  v12, v8, 1
        add            a0, a0, t0
        bgtu           a1, t4, 1b // size > 1

        lb             t0, (a0)
        sb             t0, (a2) # copy last byte (cannot be escaped)
        addi           a2, a2, 1
3:
        sub            a0, a2, t5
        ret
4:
        vsetvli        zero, t2, e8, m4, ta, ma
        vse8.v         v16, (a2)
        addi           t0, t2, 1
        add            a2, a2, t2
        addi           t2, t2, -1
        vsetvli        zero, t0, e8, m4, ta, ma
        j              2b
endfunc
