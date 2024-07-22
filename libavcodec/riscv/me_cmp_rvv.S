/*
 * Copyright (c) 2024 Institue of Software Chinese Academy of Sciences (ISCAS).
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

.macro pix_abs_ret
        vsetivli        zero, 1, e32, m1, ta, ma
        vmv.x.s         a0, v0
        ret
.endm

func ff_pix_abs16_rvv, zve32x
        lpad    0
        vsetivli        zero, 1, e32, m1, ta, ma
        vmv.s.x         v0, zero
1:
        vsetivli        zero, 16, e8, m1, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v12, (a2)
        addi            a4, a4, -1
        vwsubu.vv       v16, v4, v12
        add             a1, a1, a3
        vwsubu.vv       v20, v12, v4
        vsetvli         zero, zero, e16, m2, tu, ma
        vmax.vv         v16, v16, v20
        add             a2, a2, a3
        vwredsum.vs     v0, v16, v0
        bnez            a4, 1b

        pix_abs_ret
endfunc

func ff_pix_abs8_rvv, zve32x
        lpad    0
        vsetivli        zero, 1, e32, m1, ta, ma
        vmv.s.x         v0, zero
1:
        vsetivli        zero, 8, e8, mf2, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v12, (a2)
        addi            a4, a4, -1
        vwsubu.vv       v16, v4, v12
        add             a1, a1, a3
        vwsubu.vv       v20, v12, v4
        vsetvli         zero, zero, e16, m1, tu, ma
        vmax.vv         v16, v16, v20
        add             a2, a2, a3
        vwredsum.vs     v0, v16, v0
        bnez            a4, 1b

        pix_abs_ret
endfunc

func ff_pix_abs16_x2_rvv, zve32x
        lpad    0
        csrwi           vxrm, 0
        vsetivli        zero, 1, e32, m1, ta, ma
        li              t5, 1
        vmv.s.x         v0, zero
1:
        vsetivli        zero, 17, e8, m2, tu, ma
        vle8.v          v12, (a2)
        addi            a4, a4, -1
        vslide1down.vx  v24, v12, t5
        vsetivli        zero, 16, e8, m1, tu, ma
        vle8.v          v4, (a1)
        vaaddu.vv       v12, v12, v24
        vwsubu.vv       v16, v4, v12
        add             a1, a1, a3
        vwsubu.vv       v20, v12, v4
        vsetvli         zero, zero, e16, m2, tu, ma
        vmax.vv         v16, v16, v20
        add             a2, a2, a3
        vwredsum.vs     v0, v16, v0
        bnez            a4, 1b

        pix_abs_ret
endfunc

func ff_pix_abs8_x2_rvv, zve32x
        lpad    0
        csrwi           vxrm, 0
        vsetivli        zero, 1, e32, m1, ta, ma
        li              t5, 1
        vmv.s.x         v0, zero
1:
        vsetivli        zero, 9, e8, m1, tu, ma
        vle8.v          v12, (a2)
        addi            a4, a4, -1
        vslide1down.vx  v24, v12, t5
        vsetivli        zero, 8, e8, mf2, tu, ma
        vle8.v          v4, (a1)
        vaaddu.vv       v12, v12, v24
        vwsubu.vv       v16, v4, v12
        add             a1, a1, a3
        vwsubu.vv       v20, v12, v4
        vsetvli         zero, zero, e16, m1, tu, ma
        vmax.vv         v16, v16, v20
        add             a2, a2, a3
        vwredsum.vs     v0, v16, v0
        bnez            a4, 1b

        pix_abs_ret
endfunc

func ff_pix_abs16_y2_rvv, zve32x
        lpad    0
        csrwi           vxrm, 0
        vsetivli        zero, 1, e32, m1, ta, ma
        add             t1, a2, a3
        vmv.s.x         v0, zero
1:
        vsetivli        zero, 16, e8, m1, tu, ma
        vle8.v          v12, (a2)
        vle8.v          v24, (t1)
        addi            a4, a4, -1
        vle8.v          v4, (a1)
        vaaddu.vv       v12, v12, v24
        vwsubu.vv       v16, v4, v12
        vwsubu.vv       v20, v12, v4
        add             a1, a1, a3
        vsetvli         zero, zero, e16, m2, tu, ma
        add             a2, a2, a3
        vmax.vv         v16, v16, v20
        add             t1, t1, a3
        vwredsum.vs     v0, v16, v0
        bnez            a4, 1b

        pix_abs_ret
endfunc

func ff_pix_abs8_y2_rvv, zve32x
        lpad    0
        csrwi           vxrm, 0
        vsetivli        zero, 1, e32, m1, ta, ma
        add             t1, a2, a3
        vmv.s.x         v0, zero
1:
        vsetivli        zero, 8, e8, mf2, tu, ma
        vle8.v          v12, (a2)
        vle8.v          v24, (t1)
        addi            a4, a4, -1
        vle8.v          v4, (a1)
        vaaddu.vv       v12, v12, v24
        vwsubu.vv       v16, v4, v12
        vwsubu.vv       v20, v12, v4
        add             a1, a1, a3
        vsetvli         zero, zero, e16, m1, tu, ma
        add             a2, a2, a3
        vmax.vv         v16, v16, v20
        add             t1, t1, a3
        vwredsum.vs     v0, v16, v0
        bnez            a4, 1b

        pix_abs_ret
endfunc

func ff_sse16_rvv, zve32x
        lpad    0
        vsetivli        t0, 16, e32, m4, ta, ma
        vmv.v.x         v24, zero
        vmv.s.x         v0, zero
1:
        vsetvli         zero, zero, e8, m1, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v12, (a2)
        addi            a4, a4, -1
        vwsubu.vv       v16, v4, v12
        vsetvli         zero, zero, e16, m2, tu, ma
        vwmacc.vv       v24, v16, v16
        add             a1, a1, a3
        add             a2, a2, a3
        bnez            a4, 1b

        vsetvli         zero, zero, e32, m4, tu, ma
        vredsum.vs      v0, v24, v0
        vmv.x.s         a0, v0
        ret
endfunc

func ff_sse8_rvv, zve32x
        lpad    0
        vsetivli        t0, 8, e32, m2, ta, ma
        vmv.v.x         v24, zero
        vmv.s.x         v0, zero
1:
        vsetvli         zero, zero, e8, mf2, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v12, (a2)
        addi            a4, a4, -1
        vwsubu.vv       v16, v4, v12
        vsetvli         zero, zero, e16, m1, tu, ma
        vwmacc.vv       v24, v16, v16
        add             a1, a1, a3
        add             a2, a2, a3
        bnez            a4, 1b

        vsetvli         zero, zero, e32, m2, tu, ma
        vredsum.vs      v0, v24, v0
        vmv.x.s         a0, v0
        ret
endfunc

func ff_sse4_rvv, zve32x
        lpad    0
        vsetivli        t0, 4, e32, m1, ta, ma
        vmv.v.x         v24, zero
        vmv.s.x         v0, zero
1:
        vsetvli         zero, zero, e8, mf4, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v12, (a2)
        addi            a4, a4, -1
        vwsubu.vv       v16, v4, v12
        vsetvli         zero, zero, e16, mf2, tu, ma
        vwmacc.vv       v24, v16, v16
        add             a1, a1, a3
        add             a2, a2, a3
        bnez            a4, 1b

        vsetvli         zero, zero, e32, m1, tu, ma
        vredsum.vs      v0, v24, v0
        vmv.x.s         a0, v0
        ret
endfunc

.macro vabsaddu dst src tmp
        vneg.v          \tmp, \src
        vmax.vv         \tmp, \src, \tmp
        vwaddu.wv       \dst, \dst, \tmp
.endm

.macro  vsad_vsse16 type
        lpad    0
        vsetivli        t0, 16, e32, m4, ta, ma
        addi            a4, a4, -1
        add             t1, a1, a3
        add             t2, a2, a3
        vmv.v.x         v24, zero
        vmv.s.x         v0, zero
1:
        vsetvli         zero, zero, e8, m1, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v8, (t1)
        vle8.v          v12, (a2)
        vle8.v          v16, (t2)
        addi            a4, a4, -1
        vwsubu.vv       v28, v4, v12
        vwsubu.wv       v12, v28, v8
        vwaddu.wv       v28, v12, v16
        vsetvli         zero, zero, e16, m2, tu, ma

.ifc \type,abs
        vabsaddu        v24, v28, v12
.endif
.ifc \type,square
        vwmacc.vv       v24, v28, v28
.endif

        add             a1, a1, a3
        add             a2, a2, a3
        add             t1, t1, a3
        add             t2, t2, a3
        bnez            a4, 1b

        vsetvli         zero, zero, e32, m4, tu, ma
        vredsum.vs      v0, v24, v0
        vmv.x.s         a0, v0
        ret
.endm

.macro  vsad_vsse8 type
        lpad    0
        vsetivli        t0, 8, e32, m2, ta, ma
        addi            a4, a4, -1
        add             t1, a1, a3
        add             t2, a2, a3
        vmv.v.x         v24, zero
        vmv.s.x         v0, zero
1:
        vsetvli         zero, zero, e8, mf2, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v8, (t1)
        vle8.v          v12, (a2)
        vle8.v          v16, (t2)
        addi            a4, a4, -1
        vwsubu.vv       v28, v4, v12
        vwsubu.wv       v12, v28, v8
        vwaddu.wv       v28, v12, v16
        vsetvli         zero, zero, e16, m1, tu, ma

.ifc \type,abs
        vabsaddu        v24, v28, v12
.endif
.ifc \type,square
        vwmacc.vv       v24, v28, v28
.endif

        add             a1, a1, a3
        add             a2, a2, a3
        add             t1, t1, a3
        add             t2, t2, a3
        bnez            a4, 1b

        vsetvli         zero, zero, e32, m2, tu, ma
        vredsum.vs      v0, v24, v0
        vmv.x.s         a0, v0
        ret
.endm

.macro  vsad_vsse_intra16 type
        lpad    0
        vsetivli        t0, 16, e32, m4, ta, ma
        addi            a4, a4, -1
        add             t1, a1, a3
        vmv.v.x         v24, zero
        vmv.s.x         v0, zero
1:
        vsetvli         zero, zero, e8, m1, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v12, (t1)
        addi            a4, a4, -1
        vwsubu.vv       v16, v4, v12
        vsetvli         zero, zero, e16, m2, tu, ma

.ifc \type,abs
        vabsaddu        v24, v16, v12
.endif
.ifc \type,square
        vwmacc.vv       v24, v16, v16
.endif

        add             a1, a1, a3
        add             t1, t1, a3
        bnez            a4, 1b

        vsetvli         zero, zero, e32, m4, tu, ma
        vredsum.vs      v0, v24, v0
        vmv.x.s         a0, v0
        ret
.endm

.macro  vsad_vsse_intra8 type
        lpad    0
        vsetivli        t0, 8, e32, m2, ta, ma
        addi            a4, a4, -1
        add             t1, a1, a3
        vmv.v.x         v24, zero
        vmv.s.x         v0, zero
1:
        vsetvli         zero, zero, e8, mf2, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v12, (t1)
        addi            a4, a4, -1
        vwsubu.vv       v16, v4, v12
        vsetvli         zero, zero, e16, m1, tu, ma

.ifc \type,abs
        vabsaddu        v24, v16, v12
.endif
.ifc \type,square
        vwmacc.vv       v24, v16, v16
.endif

        add             a1, a1, a3
        add             t1, t1, a3
        bnez            a4, 1b

        vsetvli         zero, zero, e32, m2, tu, ma
        vredsum.vs      v0, v24, v0
        vmv.x.s         a0, v0
        ret
.endm

func ff_vsse16_rvv, zve32x
        vsad_vsse16 square
endfunc

func ff_vsse8_rvv, zve32x
        vsad_vsse8 square
endfunc

func ff_vsse_intra16_rvv, zve32x
        vsad_vsse_intra16 square
endfunc

func ff_vsse_intra8_rvv, zve32x
        vsad_vsse_intra8 square
endfunc

func ff_vsad16_rvv, zve32x
        vsad_vsse16 abs
endfunc

func ff_vsad8_rvv, zve32x
        vsad_vsse8 abs
endfunc

func ff_vsad_intra16_rvv, zve32x
        vsad_vsse_intra16 abs
endfunc

func ff_vsad_intra8_rvv, zve32x
        vsad_vsse_intra8 abs
endfunc

func ff_nsse16_rvv, zve32x
        lpad    0

        .macro squarediff16
        vsetivli        zero, 16, e8, m1, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v12, (a2)
        vwsubu.vv       v16, v4, v12
        vsetvli         zero, zero, e16, m2, tu, ma
        vwmacc.vv       v24, v16, v16
        .endm

        .macro gradiff16 srcx srcv
        vsetivli        zero, 16, e8, m1, tu, ma
        vle8.v          v8, (\srcx)
        vslide1down.vx  v0, \srcv, t5
        vslide1down.vx  v16, v8, t5
        vwsubu.vv       v20, \srcv, v0
        vwsubu.wv       v0, v20, v8
        vwaddu.wv       v20, v0, v16
        vsetivli        zero, 15, e16, m2, tu, ma
        vneg.v          v0, v20
        vmax.vv         v0, v20, v0
        .endm

        csrwi           vxrm, 0
        vsetivli        t0, 16, e32, m4, ta, ma
        addi            a4, a4, -1
        li              t5, 1
        vmv.v.x         v24, zero
        vmv.v.x         v28, zero
1:
        add             t1, a1, a3
        add             t2, a2, a3
        addi            a4, a4, -1
        squarediff16
        gradiff16       t1, v4
        vwaddu.wv       v28, v28, v0
        gradiff16       t2, v12
        vwsubu.wv       v28, v28, v0
        add             a1, a1, a3
        add             a2, a2, a3
        bnez            a4, 1b

        squarediff16
        vsetivli        zero, 16, e32, m4, tu, ma
        vmv.s.x         v0, zero
        vmv.s.x         v4, zero
        vredsum.vs      v0, v24, v0
        vredsum.vs      v4, v28, v4
        vmv.x.s         t1, v0
        vmv.x.s         t2, v4
        srai            t3, t2, 31
        xor             t2, t3, t2
        sub             t2, t2, t3
        mul             t2, t2, a0
        add             a0, t2, t1

        ret
endfunc

func ff_nsse8_rvv, zve32x
        lpad    0

        .macro squarediff8
        vsetivli        zero, 8, e8, mf2, tu, ma
        vle8.v          v4, (a1)
        vle8.v          v12, (a2)
        vwsubu.vv       v16, v4, v12
        vsetvli         zero, zero, e16, m1, tu, ma
        vwmacc.vv       v24, v16, v16
        .endm

        .macro gradiff8 srcx srcv
        vsetivli        zero, 8, e8, mf2, tu, ma
        vle8.v          v8, (\srcx)
        vslide1down.vx  v0, \srcv, t5
        vslide1down.vx  v16, v8, t5
        vwsubu.vv       v20, \srcv, v0
        vwsubu.wv       v0, v20, v8
        vwaddu.wv       v20, v0, v16
        vsetivli        zero, 7, e16, m1, tu, ma
        vneg.v          v0, v20
        vmax.vv         v0, v20, v0
        .endm

        csrwi           vxrm, 0
        vsetivli        t0, 8, e32, m2, ta, ma
        addi            a4, a4, -1
        li              t5, 1
        vmv.v.x         v24, zero
        vmv.v.x         v28, zero
1:
        add             t1, a1, a3
        add             t2, a2, a3
        addi            a4, a4, -1
        squarediff8
        gradiff8        t1, v4
        vwaddu.wv       v28, v28, v0
        gradiff8        t2, v12
        vwsubu.wv       v28, v28, v0
        add             a1, a1, a3
        add             a2, a2, a3
        bnez            a4, 1b

        squarediff8
        vsetivli        zero, 8, e32, m2, tu, ma
        vmv.s.x         v0, zero
        vmv.s.x         v4, zero
        vredsum.vs      v0, v24, v0
        vredsum.vs      v4, v28, v4
        vmv.x.s         t1, v0
        vmv.x.s         t2, v4
        srai            t3, t2, 31
        xor             t2, t3, t2
        sub             t2, t2, t3
        mul             t2, t2, a0
        add             a0, t2, t1

        ret
endfunc
