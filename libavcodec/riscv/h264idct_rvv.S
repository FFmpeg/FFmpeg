/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 J. Dekker <jdek@itanimul.li>
 * Copyright © 2024 Rémi Denis-Courmont.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "libavutil/riscv/asm.S"

        .macro  sx rd, addr
#if (__riscv_xlen == 32)
        sw      \rd, \addr
#elif (__riscv_xlen == 64)
        sd      \rd, \addr
#else
        sq      \rd, \addr
#endif
        .endm

        .variant_cc ff_h264_idct4_rvv
func ff_h264_idct4_rvv, zve32x
        vsra.vi v5, v1, 1
        vsra.vi v7, v3, 1
        vadd.vv v8, v0, v2   # z0
        vsub.vv v9, v0, v2   # z1
        vsub.vv v10, v5, v3  # z2
        vadd.vv v11, v1, v7  # z3
        vadd.vv v1, v9, v10
        vsub.vv v2, v9, v10
        vadd.vv v0, v8, v11
        vsub.vv v3, v8, v11
        jr      t0
endfunc

func ff_h264_idct_add_8_rvv, zve32x
        lpad    0
        csrwi       vxrm, 0
.Lidct4_add_8_rvv:
        vsetivli    zero, 4, e16, mf2, ta, ma
        addi        t1, a1, 1 * 4 * 2
        vle16.v     v0, (a1)
        addi        t2, a1, 2 * 4 * 2
        vle16.v     v1, (t1)
        addi        t3, a1, 3 * 4 * 2
        vle16.v     v2, (t2)
        vle16.v     v3, (t3)
        jal         t0, ff_h264_idct4_rvv
        vse16.v     v0, (a1)
        vse16.v     v1, (t1)
        vse16.v     v2, (t2)
        vse16.v     v3, (t3)
        vlseg4e16.v v0, (a1)
        .equ    offset, 0
        .rept   256 / __riscv_xlen
        sx      zero, offset(a1)
        .equ    offset, offset + (__riscv_xlen / 8)
        .endr
        jal         t0, ff_h264_idct4_rvv
        add         t1, a0, a2
        vle8.v      v4, (a0)
        add         t2, t1, a2
        vle8.v      v5, (t1)
        add         t3, t2, a2
        vle8.v      v6, (t2)
        vle8.v      v7, (t3)
        .irp    n,0,1,2,3
        vssra.vi    v\n, v\n, 6
        .endr
        vsetvli     zero, zero, e8, mf4, ta, ma
        vwaddu.wv   v0, v0, v4
        vwaddu.wv   v1, v1, v5
        vwaddu.wv   v2, v2, v6
        vwaddu.wv   v3, v3, v7
        vsetvli     zero, zero, e16, mf2, ta, ma
        .irp    n,0,1,2,3
        vmax.vx     v\n, v\n, zero
        .endr
        vsetvli     zero, zero, e8, mf4, ta, ma
        vnclipu.wi  v4, v0, 0
        vnclipu.wi  v5, v1, 0
        vnclipu.wi  v6, v2, 0
        vnclipu.wi  v7, v3, 0
        vse8.v      v4, (a0)
        vse8.v      v5, (t1)
        vse8.v      v6, (t2)
        vse8.v      v7, (t3)
        ret
endfunc

func ff_h264_idct_add_16_rvv, zve32x
        csrwi       vxrm, 0
.Lidct4_add_16_rvv:
        vsetivli    zero, 4, e32, m1, ta, ma
        addi        t1, a1, 1 * 4 * 4
        vle32.v     v0, (a1)
        addi        t2, a1, 2 * 4 * 4
        vle32.v     v1, (t1)
        addi        t3, a1, 3 * 4 * 4
        vle32.v     v2, (t2)
        vle32.v     v3, (t3)
        jal         t0, ff_h264_idct4_rvv
        vse32.v     v0, (a1)
        vse32.v     v1, (t1)
        vse32.v     v2, (t2)
        vse32.v     v3, (t3)
        vlseg4e32.v v0, (a1)
        .equ    offset, 0
        .rept   512 / __riscv_xlen
        sx      zero, offset(a1)
        .equ    offset, offset + (__riscv_xlen / 8)
        .endr
        jal         t0, ff_h264_idct4_rvv
        add         t1, a0, a2
        vle16.v     v4, (a0)
        add         t2, t1, a2
        vle16.v     v5, (t1)
        add         t3, t2, a2
        vle16.v     v6, (t2)
        vle16.v     v7, (t3)
        .irp    n,0,1,2,3
        vssra.vi    v\n, v\n, 6
        .endr
        vsetvli     zero, zero, e16, mf2, ta, ma
        vwaddu.wv   v0, v0, v4
        vwaddu.wv   v1, v1, v5
        vwaddu.wv   v2, v2, v6
        vwaddu.wv   v3, v3, v7
        vsetvli     zero, zero, e32, m1, ta, ma
        .irp    n,0,1,2,3
        vmax.vx     v\n, v\n, zero
        .endr
        .irp    n,0,1,2,3
        vmin.vx     v\n, v\n, a5
        .endr
        vsetvli     zero, zero, e16, mf2, ta, ma
        vncvt.x.x.w v4, v0
        vncvt.x.x.w v5, v1
        vncvt.x.x.w v6, v2
        vncvt.x.x.w v7, v3
        vse16.v     v4, (a0)
        vse16.v     v5, (t1)
        vse16.v     v6, (t2)
        vse16.v     v7, (t3)
        ret
endfunc

        .variant_cc ff_h264_idct8_rvv
func ff_h264_idct8_rvv, zve32x
        vsra.vi v9, v7, 1
        vsra.vi v11, v3, 1
        vsra.vi v12, v2, 1
        vsra.vi v13, v5, 1
        vsra.vi v14, v6, 1
        vsra.vi v15, v1, 1
        vadd.vv v9, v3, v9
        vsub.vv v11, v1, v11
        vsub.vv v13, v13, v1
        vadd.vv v15, v3, v15
        vsub.vv v9, v5, v9
        vadd.vv v11, v11, v7
        vadd.vv v13, v13, v7
        vadd.vv v15, v15, v5
        vadd.vv v8, v0, v4      # a0
        vsub.vv v9, v9, v7      # a1
        vsub.vv v10, v0, v4     # a2
        vsub.vv v11, v11, v3    # a3
        vsub.vv v12, v12, v6    # a4
        vadd.vv v13, v13, v5    # a5
        vadd.vv v14, v14, v2    # a6
        vadd.vv v15, v15, v1    # a7
        vsra.vi v7, v9, 2
        vsra.vi v5, v11, 2
        vsra.vi v3, v13, 2
        vsra.vi v1, v15, 2
        vadd.vv v0, v8, v14     # b0
        vadd.vv v6, v10, v12    # b2
        vsub.vv v2, v10, v12    # b4
        vsub.vv v4, v8, v14     # b6
        vsub.vv v8, v15, v7     # b7
        vsub.vv v14, v5, v13    # b5
        vadd.vv v12, v1, v9     # b1
        vadd.vv v10, v11, v3    # b3
        vadd.vv v1, v6, v14
        vsub.vv v6, v6, v14
        vsub.vv v7, v0, v8
        vadd.vv v0, v0, v8
        vsub.vv v5, v2, v10
        vadd.vv v2, v2, v10
        vadd.vv v3, v4, v12
        vsub.vv v4, v4, v12
        jr      t0
endfunc

func ff_h264_idct8_add_8_rvv, zve32x
        lpad    0
        csrwi       vxrm, 0
.Lidct8_add_8_rvv:
        vsetivli    zero, 8, e16, m1, ta, ma
        addi        t1, a1, 1 * 8 * 2
        vle16.v     v0, (a1)
        addi        t2, a1, 2 * 8 * 2
        vle16.v     v1, (t1)
        addi        t3, a1, 3 * 8 * 2
        vle16.v     v2, (t2)
        addi        t4, a1, 4 * 8 * 2
        vle16.v     v3, (t3)
        addi        t5, a1, 5 * 8 * 2
        vle16.v     v4, (t4)
        addi        t6, a1, 6 * 8 * 2
        vle16.v     v5, (t5)
        addi        a7, a1, 7 * 8 * 2
        vle16.v     v6, (t6)
        vle16.v     v7, (a7)
        jal         t0, ff_h264_idct8_rvv
        vse16.v     v0, (a1)
        vse16.v     v1, (t1)
        vse16.v     v2, (t2)
        vse16.v     v3, (t3)
        vse16.v     v4, (t4)
        vse16.v     v5, (t5)
        vse16.v     v6, (t6)
        vse16.v     v7, (a7)
        vlseg8e16.v v0, (a1)
        .equ    offset, 0
        .rept   1024 / __riscv_xlen
        sx      zero, offset(a1)
        .equ    offset, offset + (__riscv_xlen / 8)
        .endr
        jal         t0, ff_h264_idct8_rvv
        add         t1, a0, a2
        vle8.v      v16, (a0)
        add         t2, t1, a2
        vle8.v      v17, (t1)
        add         t3, t2, a2
        vle8.v      v18, (t2)
        add         t4, t3, a2
        vle8.v      v19, (t3)
        add         t5, t4, a2
        vle8.v      v20, (t4)
        add         t6, t5, a2
        vle8.v      v21, (t5)
        add         a7, t6, a2
        vle8.v      v22, (t6)
        vle8.v      v23, (a7)
        .irp    n,0,1,2,3,4,5,6,7
        vssra.vi    v\n, v\n, 6
        .endr
        vsetvli     zero, zero, e8, mf2, ta, ma
        vwaddu.wv   v0, v0, v16
        vwaddu.wv   v1, v1, v17
        vwaddu.wv   v2, v2, v18
        vwaddu.wv   v3, v3, v19
        vwaddu.wv   v4, v4, v20
        vwaddu.wv   v5, v5, v21
        vwaddu.wv   v6, v6, v22
        vwaddu.wv   v7, v7, v23
        vsetvli     zero, zero, e16, m1, ta, ma
        .irp    n,0,1,2,3,4,5,6,7
        vmax.vx     v\n, v\n, zero
        .endr
        vsetvli     zero, zero, e8, mf2, ta, ma
        vnclipu.wi  v16, v0, 0
        vnclipu.wi  v17, v1, 0
        vnclipu.wi  v18, v2, 0
        vnclipu.wi  v19, v3, 0
        vnclipu.wi  v20, v4, 0
        vnclipu.wi  v21, v5, 0
        vnclipu.wi  v22, v6, 0
        vnclipu.wi  v23, v7, 0
        vse8.v      v16, (a0)
        vse8.v      v17, (t1)
        vse8.v      v18, (t2)
        vse8.v      v19, (t3)
        vse8.v      v20, (t4)
        vse8.v      v21, (t5)
        vse8.v      v22, (t6)
        vse8.v      v23, (a7)
        ret
endfunc

func ff_h264_idct8_add_16_rvv, zve32x, zba
        csrwi   vxrm, 0
.Lidct8_add_16_rvv:
        li      a4, 8
        vsetivli    a3, 8, e32, m1, ta, ma
1:
        addi    t1, a1, 1 * 8 * 4
        vle32.v     v0, (a1)
        addi    t2, a1, 2 * 8 * 4
        vle32.v     v1, (t1)
        addi    t3, a1, 3 * 8 * 4
        vle32.v     v2, (t2)
        addi    t4, a1, 4 * 8 * 4
        vle32.v     v3, (t3)
        addi    t5, a1, 5 * 8 * 4
        vle32.v     v4, (t4)
        addi    t6, a1, 6 * 8 * 4
        vle32.v     v5, (t5)
        addi    a7, a1, 7 * 8 * 4
        vle32.v     v6, (t6)
        sub     a4, a4, a3
        vle32.v     v7, (a7)
        jal     t0, ff_h264_idct8_rvv
        vse32.v     v0, (a1)
        sh2add  a1, a3, a1
        vse32.v     v1, (t1)
        vse32.v     v2, (t2)
        vse32.v     v3, (t3)
        vse32.v     v4, (t4)
        vse32.v     v5, (t5)
        vse32.v     v6, (t6)
        vse32.v     v7, (a7)
        bnez    a4, 1b

        addi    a1, a1, -8 * 4
        li      a4, 8
        slli    a6, a3, 3 + 2
2:
        vsetvli     zero, zero, e32, m1, ta, ma
        vlseg8e32.v v0, (a1)
        jal     t0, ff_h264_idct8_rvv
        add     t1, a0, a2
        vle16.v     v16, (a0)
        add     t2, t1, a2
        vle16.v     v17, (t1)
        add     t3, t2, a2
        vle16.v     v18, (t2)
        add     t4, t3, a2
        vle16.v     v19, (t3)
        add     t5, t4, a2
        vle16.v     v20, (t4)
        add     t6, t5, a2
        vle16.v     v21, (t5)
        add     a7, t6, a2
        vle16.v     v22, (t6)
        sub     a4, a4, a3
        vle16.v     v23, (a7)
        .irp    n,0,1,2,3,4,5,6,7
        vssra.vi    v\n, v\n, 6
        .endr
        vsetvli     zero, zero, e16, mf2, ta, ma
        vwaddu.wv   v0, v0, v16
        add     a1, a6, a1
        vwaddu.wv   v1, v1, v17
        vwaddu.wv   v2, v2, v18
        vwaddu.wv   v3, v3, v19
        vwaddu.wv   v4, v4, v20
        vwaddu.wv   v5, v5, v21
        vwaddu.wv   v6, v6, v22
        vwaddu.wv   v7, v7, v23
        vsetvli     zero, zero, e32, m1, ta, ma
        .irp    n,0,1,2,3,4,5,6,7
        vmax.vx     v\n, v\n, zero
        .endr
        .irp    n,0,1,2,3,4,5,6,7
        vmin.vx     v\n, v\n, a5
        .endr
        vsetvli     zero, zero, e16, mf2, ta, ma
        vncvt.x.x.w v16, v0
        vncvt.x.x.w v17, v1
        vncvt.x.x.w v18, v2
        vncvt.x.x.w v19, v3
        vncvt.x.x.w v20, v4
        vncvt.x.x.w v21, v5
        vncvt.x.x.w v22, v6
        vncvt.x.x.w v23, v7
        vse16.v     v16, (a0)
        sh1add  a0, a3, a0
        vse16.v     v17, (t1)
        vse16.v     v18, (t2)
        vse16.v     v19, (t3)
        vse16.v     v20, (t4)
        vse16.v     v21, (t5)
        vse16.v     v22, (t6)
        vse16.v     v23, (a7)
        bnez    a4, 2b

        .equ    offset, 0
        .rept   2048 / __riscv_xlen
        sx      zero, offset - 8 * 8 * 4(a1)
        .equ    offset, offset + (__riscv_xlen / 8)
        .endr
        ret
endfunc

.irp    depth, 9, 10, 12, 14
func ff_h264_idct_add_\depth\()_rvv, zve32x
        lpad    0
        li      a5, (1 << \depth) - 1
        j       ff_h264_idct_add_16_rvv
endfunc

func ff_h264_idct8_add_\depth\()_rvv, zve32x
        lpad    0
        li      a5, (1 << \depth) - 1
        j       ff_h264_idct8_add_16_rvv
endfunc
.endr

.macro idct_dc_add8 width
func ff_h264_idct\width\()_dc_add_8_rvv, zve64x, b
        lpad    0
.if \width == 8
        vsetivli        zero, \width, e8, mf2, ta, ma
.else
        vsetivli        zero, \width, e8, mf4, ta, ma
.endif
        lh              t0, 0(a1)
        li              t1, 255
        addi            t0, t0, 32
        srai            t0, t0, 6
        sh              zero, 0(a1)
.if \width == 8
        li              a6, \width * \width
        vlse64.v        v24, (a0), a2
        vsetvli         zero, a6, e8, m4, ta, ma
.else
        vlse32.v        v24, (a0), a2
        vsetivli        zero, \width * \width, e8, m1, ta, ma
.endif
        bgez            t0, 1f

        neg             t0, t0
        minu            t0, t0, t1
        vssubu.vx       v24, v24, t0
 .if \width == 8
        vsetivli        zero, \width, e8, mf2, ta, ma
        vsse64.v        v24, (a0), a2
.else
        vsetivli        zero, \width, e8, mf4, ta, ma
        vsse32.v        v24, (a0), a2
.endif
        ret
1:
        minu            t0, t0, t1
        vsaddu.vx       v24, v24, t0
.if \width == 8
        vsetivli        zero, \width, e8, mf2, ta, ma
        vsse64.v        v24, (a0), a2
.else
        vsetivli        zero, \width, e8, mf4, ta, ma
        vsse32.v        v24, (a0), a2
.endif
        ret
endfunc
.endm

idct_dc_add8 4
idct_dc_add8 8

.macro idct_dc_add width
func ff_h264_idct\width\()_dc_add_16_rvv, zve64x, zba
.if \width == 8
        vsetivli        zero, \width, e16, m1, ta, ma
.else
        vsetivli        zero, \width, e16, mf2, ta, ma
.endif
        lw              t0, 0(a1)
        addi            t0, t0, 32
        srai            t0, t0, 6
        sw              zero, 0(a1)
        add             t1, a0, a2
        sh1add          t2, a2, a0
        sh1add          t3, a2, t1
.if \width == 8
        sh2add          t4, a2, a0
        sh2add          t5, a2, t1
        sh2add          t6, a2, t2
        sh2add          a7, a2, t3
.endif
        vle16.v         v0, (a0)
        vle16.v         v1, (t1)
        vle16.v         v2, (t2)
        vle16.v         v3, (t3)
.if \width == 8
        vle16.v         v4, (t4)
        vle16.v         v5, (t5)
        vle16.v         v6, (t6)
        vle16.v         v7, (a7)
        vsetvli         a6, zero, e16, m8, ta, ma
.else
        vsetvli         a6, zero, e16, m4, ta, ma
.endif
        vadd.vx         v0, v0, t0
        vmax.vx         v0, v0, zero
        vmin.vx         v0, v0, a5
.if \width == 8
        vsetivli        zero, \width, e16, m1, ta, ma
.else
        vsetivli        zero, \width, e16, mf2, ta, ma
.endif
        vse16.v         v0, (a0)
        vse16.v         v1, (t1)
        vse16.v         v2, (t2)
        vse16.v         v3, (t3)
.if \width == 8
        vse16.v         v4, (t4)
        vse16.v         v5, (t5)
        vse16.v         v6, (t6)
        vse16.v         v7, (a7)
.endif
        ret
endfunc
.endm

idct_dc_add 4
idct_dc_add 8

.irp depth,9,10,12,14
func ff_h264_idct4_dc_add_\depth\()_rvv, zve64x
        lpad    0
        li              a5, (1 << \depth) - 1
        j               ff_h264_idct4_dc_add_16_rvv
endfunc

func ff_h264_idct8_dc_add_\depth\()_rvv, zve64x
        lpad    0
        li              a5, (1 << \depth) - 1
        j               ff_h264_idct8_dc_add_16_rvv
endfunc
.endr

const ff_h264_scan8, align=0
        .byte    014,  015,  024,  025,  016,  017,  026,  027
        .byte    034,  035,  044,  045,  036,  037,  046,  047
        .byte    064,  065,  074,  075,  066,  067,  076,  077
        .byte   0104, 0105, 0114, 0115, 0106, 0107, 0116, 0117
        .byte   0134, 0135, 0144, 0145, 0136, 0137, 0146, 0147
        .byte   0154, 0155, 0164, 0165, 0156, 0157, 0166, 0167
endconst

.macro  idct4_add16 type, depth
func ff_h264_idct_add\type\()_\depth\()_rvv, zve32x, b
.if \depth == 8
        lpad    0
.endif
        csrwi   vxrm, 0
        lla     t0, ff_h264_scan8
        vsetivli  zero, 16, e8, m1, ta, ma
.ifc \type, 16intra
.Lidct4_add4_\depth\()_rvv:
.endif
        li      t1, 32 * (\depth / 8)
        vle8.v    v8, (t0)
.if \depth == 8
        vlse16.v  v16, (a2), t1
.else
        vlse32.v  v16, (a2), t1
.endif
        vluxei8.v v12, (a4), v8
.if \depth == 8
        vsetvli   zero, zero, e16, m2, ta, ma
.else
        vsetvli   zero, zero, e32, m4, ta, ma
.endif
        vmsne.vi  v1, v16, 0
        vsetvli   zero, zero, e8, m1, ta, ma
.ifc \type, 16
        vmseq.vi  v2, v12, 1
.endif
        vmsne.vi  v0, v12, 0
.ifc \type, 16
        vmand.mm  v1, v1, v2
.endif
        vsetvli   zero, zero, e16, m2, ta, ma
        vmv.x.s   a4, v0
        vmv.x.s   a7, v1
        zext.h  a4, a4
        slli    a7, a7, 16
        mv      t4, a0
        or      a4, a4, a7
        mv      t5, a1
        mv      a1, a2
        mv      a2, a3
        csrr    a3, vl
        mv      a7, ra
1:
        andi    t0, a4, 1
        addi    a3, a3, -1
.ifc \type, 16
        beqz    t0, 3f     # if (nnz)
.endif
        lw      t2, (t5)   # block_offset[i]
        bexti   t1, a4, 16
        add     a0, t4, t2
.ifc \type, 16
        bnez    t1, 2f     # if (nnz == 1 && block[i * 16])
.else
        beqz    t0, 2f     # if (nnzc[scan8[i]])
.endif
        jal     .Lidct4_add_\depth\()_rvv
        j       3f
2:
.ifnc \type, 16
        beqz    t1, 3f    # if (block[i * 16])
.endif
        jal     ff_h264_idct4_dc_add_\depth\()_rvv
3:
        srli    a4, a4, 1
        addi    t5, t5, 4
        addi    a1, a1, 16 * 2 * (\depth / 8)
        bnez    a3, 1b

        mv      ra, a7
        ret
endfunc
.endm

.macro idct4_add8 type, depth
#if (__riscv_xlen == 64)
func ff_h264_idct4_add\type\()_\depth\()_rvv, zve32x
.if \depth == 8
        lpad    0
.endif
        csrwi   vxrm, 0
        addi    sp, sp, -32
        addi    a2, a2, 16 * 16 * 2 * (\depth / 8)       # &block[16 * 16]
        lla     t0, ff_h264_scan8 + 16
        sd      s0,  0(sp)
        sd      ra,  8(sp)
        mv      s0, sp
        sd      a0, 16(sp)
        sd      a4, 24(sp)
        ld      a0,  0(a0)                               # dest[0]
        addi    a1, a1, 16 * 4                           # &block_offset[16]
        vsetivli    zero, 4, e8, m1, ta, ma
        jal     .Lidct4_add4_\depth\()_rvv

        ld      a4, 24(sp)                               # nnzc
        ld      a0, 16(sp)
        mv      a3, a2                                   # stride
        addi    a2, a1, (16 - 4) * 16 * 2 * (\depth / 8) # &block[32 * 16]
        addi    a1, t5, (16 - 4) * 4                     # &block_offset[32]
        ld      a0,  8(a0)                               # dest[1]
        lla     t0, ff_h264_scan8 + 32
.ifc \type, 8_422
        vsetivli    zero, 4, e8, m1, ta, ma
        jal     .Lidct4_add4_\depth\()_rvv

        ld      a4, 24(sp)                               # nnzc
        ld      a0, 16(sp)
        mv      a3, a2                                   # stride
        addi    a2, a1, (-12- 4) * 16 * 2 * (\depth / 8) # &block[20 * 16]
        addi    a1, t5, (-8 - 4) * 4                     # &block_offset[24]
        ld      a0,  0(a0)                               # dest[0]
        lla     t0, ff_h264_scan8 + 24
        vsetivli    zero, 4, e8, m1, ta, ma
        jal     .Lidct4_add4_\depth\()_rvv

        ld      a4, 24(sp)                               # nnzc
        ld      a0, 16(sp)
        mv      a3, a2                                   # stride
        addi    a2, a1, (16 - 4) * 16 * 2 * (\depth / 8) # &block[36 * 16]
        addi    a1, t5, (16 - 4) * 4                     # &block_offset[40]
        ld      a0,  8(a0)                               # dest[1]
        lla     t0, ff_h264_scan8 + 40
.endif
        ld      ra,  8(sp)
        ld      s0,  0(sp)
        addi    sp, sp, 32
        vsetivli    zero, 4, e8, m1, ta, ma
        j       .Lidct4_add4_\depth\()_rvv
endfunc
#endif
.endm

.irp    depth, 8, 16
idct4_add16 16, \depth
idct4_add16 16intra, \depth
idct4_add8  8, \depth
idct4_add8  8_422, \depth

#if (__riscv_xlen == 64)
func ff_h264_idct8_add4_\depth\()_rvv, zve32x, b
.if \depth == 8
        lpad    0
.endif
        csrwi       vxrm, 0
        addi    sp, sp, -48
        lla     t0, ff_h264_scan8
        sd      s0,   (sp)
        li      t1, 4 * 32 * (\depth / 8)
        mv      s0, sp
        li      t2, 4
        sd      ra,  8(sp)
        sd      s1, 16(sp)
        sd      s2, 24(sp)
        sd      s4, 32(sp)
        sd      s5, 40(sp)
        vsetivli  zero, 4, e8, mf4, ta, ma
        vlse8.v   v8, (t0), t2
.if \depth == 8
        vlse16.v  v16, (a2), t1
.else
        vlse32.v  v16, (a2), t1
.endif
        vluxei8.v v12, (a4), v8
.if \depth == 8
        vsetvli   zero, zero, e16, mf2, ta, ma
.else
        vsetvli   zero, zero, e32, m1, ta, ma
.endif
        vmsne.vi  v1, v16, 0
        vsetvli   zero, zero, e8, mf4, ta, ma
        vmseq.vi  v2, v12, 1
        vmsne.vi  v0, v12, 0
        vmand.mm  v1, v1, v2
        vmv.x.s   s2, v0
        vmv.x.s   a7, v1
        zext.h  s2, s2
        slli    a7, a7, 16
        li      s1, 4
        or      s2, s2, a7
        mv      s4, a0
        mv      s5, a1
        mv      a1, a2
        mv      a2, a3
1:
        andi    t0, s2, 1
        addi    s1, s1, -1
        beqz    t0, 3f     # if (nnz)
        lw      t2, (s5)   # block_offset[i]
        bexti   t1, s2, 16
        add     a0, s4, t2
        bnez    t1, 2f    # if (nnz == 1 && block[i * 16])
        jal     .Lidct8_add_\depth\()_rvv
.if \depth == 8
        j       3f
.else
        j       4f        # idct8_add_16 updates a1
.endif
2:
        jal     ff_h264_idct8_dc_add_\depth\()_rvv
3:
        addi    a1, a1, 4 * 16 * 2 * (\depth / 8)
4:
        srli    s2, s2, 1
        addi    s5, s5, 4 * 4
        bnez    s1, 1b

        ld      s5, 40(sp)
        ld      s4, 32(sp)
        ld      s2, 24(sp)
        ld      s1, 16(sp)
        ld      ra,  8(sp)
        ld      s0,  0(sp)
        addi    sp, sp, 48
        ret
endfunc
#endif
.endr

.irp    depth, 9, 10, 12, 14
func ff_h264_idct_add16_\depth\()_rvv, zve32x
        lpad    0
        li      a5, (1 << \depth) - 1
        j       ff_h264_idct_add16_16_rvv
endfunc

func ff_h264_idct_add16intra_\depth\()_rvv, zve32x
        lpad    0
        li      a5, (1 << \depth) - 1
        j       ff_h264_idct_add16intra_16_rvv
endfunc

#if (__riscv_xlen == 64)
func ff_h264_idct8_add4_\depth\()_rvv, zve32x
        lpad    0
        li      a5, (1 << \depth) - 1
        j       ff_h264_idct8_add4_16_rvv
endfunc

func ff_h264_idct4_add8_\depth\()_rvv, zve32x
        lpad    0
        li      a5, (1 << \depth) - 1
        j       ff_h264_idct4_add8_16_rvv
endfunc

func ff_h264_idct4_add8_422_\depth\()_rvv, zve32x
        lpad    0
        li      a5, (1 << \depth) - 1
        j       ff_h264_idct4_add8_422_16_rvv
endfunc
#endif
.endr
