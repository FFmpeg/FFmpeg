/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
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

        .variant_cc ff_h264_weight_pixels_simple_8_rvv
func ff_h264_weight_pixels_simple_8_rvv, zve32x, b
        csrwi   vxrm, 0
        sll     a5, a5, a3
1:
        vsetvli     zero, t6, e16, m2, ta, ma
        add     t0, a0, a1
        vle8.v      v8, (a0)
        addi    a2, a2, -2
        vle8.v      v9, (t0)
        vzext.vf2   v24, v8
        vzext.vf2   v26, v9
        vmul.vx     v16, v24, a4
        vmul.vx     v18, v26, a4
        vsadd.vx    v16, v16, a5
        vsadd.vx    v18, v18, a5
        vmax.vx     v16, v16, zero
        vmax.vx     v18, v18, zero
        vsetvli     zero, zero, e8, m1, ta, ma
        vnclipu.wx  v8, v16, a3
        vnclipu.wx  v9, v18, a3
        vse8.v      v8, (a0)
        vse8.v      v9, (t0)
        sh1add  a0, a1, a0
        bnez    a2, 1b

        ret
endfunc

        .variant_cc ff_h264_biweight_pixels_simple_8_rvv
func ff_h264_biweight_pixels_simple_8_rvv, zve32x
        csrwi   vxrm, 2
        addi    a7, a7, 1
        ori     a7, a7, 1
        sll     a7, a7, a4
        addi    a4, a4, 1
1:
        vsetvli zero, t6, e16, m2, ta, ma
        vle8.v  v8, (a0)
        addi    a3, a3, -1
        vle8.v  v12, (a1)
        add     a1, a1, a2
        vmv.v.x v16, a7
        vsetvli     zero, zero, e8, m1, ta, ma
        vwmaccsu.vx v16, a5, v8
        vwmaccsu.vx v16, a6, v12
        vsetvli     zero, zero, e16, m2, ta, ma
        vmax.vx v16, v16, zero
        vsetvli zero, zero, e8, m1, ta, ma
        vnclipu.wx  v8, v16, a4
        vse8.v  v8, (a0)
        add     a0, a0, a2
        bnez    a3, 1b

        ret
endfunc

.macro  h264_weight depth, w, b=
func ff_h264_weight_pixels\w\()_\depth\()_rvv, zve64x
        lpad    0
        .ifb    \b
        li      t6, \w
        j       ff_h264_weight_pixels_simple_\depth\()_rvv
        .else
        csrwi   vxrm, 0
        sll     a5, a5, a3
1:
        vsetvli     t1, a2, e\b, m2, ta, ma
        vlse\b\().v v8, (a0), a1
        vsetvli     t0, zero, e16, m4, ta, ma
        vzext.vf2   v24, v8
        sub     a2, a2, t1
        vmul.vx     v16, v24, a4
        mul     t2, t1, a1
        vsadd.vx    v16, v16, a5
        vmax.vx     v16, v16, zero
        vsetvli     zero, zero, e8, m2, ta, ma
        vnclipu.wx  v8, v16, a3
        vsetvli     zero, t1, e\b, m2, ta, ma
        vsse\b\().v v8, (a0), a1
        add     a0, a0, t2
        bnez    a2, 1b

        ret
        .endif
endfunc

func ff_h264_biweight_pixels\w\()_\depth\()_rvv, zve64x
        lpad    0
        li      t6, \w
        .ifb    \b
        j       ff_h264_biweight_pixels_simple_8_rvv
        .else
        csrwi   vxrm, 2
        addi    a7, a7, 1
        ori     a7, a7, 1
        sll     a7, a7, a4
        addi    a4, a4, 1
1:
        vsetvli     t1, a3, e\b, m2, ta, ma
        vlse\b\().v v8, (a0), a2
        sub     a3, a3, t1
        vlse\b\().v v12, (a1), a2
        mul     t2, t1, a2
        vsetvli     t0, zero, e16, m4, ta, ma
        vmv.v.x     v16, a7
        vsetvli     zero, zero, e8, m2, ta, ma
        vwmaccsu.vx v16, a5, v8
        add     a1, a1, t2
        vwmaccsu.vx v16, a6, v12
        vsetvli     zero, zero, e16, m4, ta, ma
        vmax.vx     v16, v16, zero
        vsetvli     zero, zero, e8, m2, ta, ma
        vnclipu.wx  v8, v16, a4
        vsetvli     zero, t1, e\b, m2, ta, ma
        vsse\b\().v v8, (a0), a2
        add     a0, a0, t2
        .endif
        bnez    a3, 1b

        ret
endfunc
.endm

h264_weight 8, 2, 16
h264_weight 8, 4, 32
h264_weight 8, 8, 64
h264_weight 8, 16

        .global ff_h264_weight_funcs_8_rvv
        .hidden ff_h264_weight_funcs_8_rvv
const ff_h264_weight_funcs_8_rvv, relocate=1
        .irp    w, 16, 8, 4, 2
#if __riscv_xlen == 32
        .word   ff_h264_weight_pixels\w\()_8_rvv
        .word   ff_h264_biweight_pixels\w\()_8_rvv
#elif __riscv_xlen == 64
        .dword  ff_h264_weight_pixels\w\()_8_rvv
        .dword  ff_h264_biweight_pixels\w\()_8_rvv
#else
        .qword  ff_h264_weight_pixels\w\()_8_rvv
        .qword  ff_h264_biweight_pixels\w\()_8_rvv
#endif
        .endr
endconst

.macro loop_filter type, inners, e8mul, e16mul
        .variant_cc ff_h264_loop_filter_\type\()_8_rvv
func ff_h264_loop_filter_\type\()_8_rvv, zve32x
        # p2: v8, p1: v9, p0: v10, q0: v11, q1: v12, q2: v13
        # alpha: a2, beta: a3, tc_orig: v6
        csrwi        vxrm, 0
.ifc \type, luma
        vaaddu.vv    v14, v10, v11 # (p0 + q0 + 1) / 2
.endif
        vwsubu.vv    v16, v9, v12
.ifc \type, luma
        vwaddu.vv    v18, v8, v14
        vwaddu.vv    v20, v13, v14
.endif
        vnsra.wi     v24, v16, 2   # (p1 - q1) / 4
.ifc \type, luma
        vnsrl.wi     v14, v18, 1
        vnsrl.wi     v15, v20, 1
        vneg.v       v5, v6        # -tc_orig
.endif
        vwsubu.vv    v22, v11, v10 # q0 - p0
.ifc \type, luma
        vwsubu.vv    v18, v14, v9
        vwsubu.vv    v20, v15, v12
.endif
        vwadd.wv     v16, v22, v24
        vmsge.vi     v7, v6, 0     # tc_orig >= 0
.ifc \type, luma
        vnclip.wi    v14, v18, 0
        vnclip.wi    v15, v20, 0
.endif
        vnclip.wi    v16, v16, 1   # clip8((q0 - p0 + (p1 - q1) / 4 + 1) >> 1)
.ifc \type, luma
        vmin.vv      v14, v14, v6
        vmin.vv      v15, v15, v6
        vmax.vv      v14, v14, v5  # clip(p2 + ... - p1, +/-tc_orig)
        vmax.vv      v15, v15, v5  # clip(q2 + ... - q1, +/-tc_orig)
.endif
        vwsubu.vv    v20, v10, v11
        vwsubu.vv    v24, v9, v10
        vwsubu.vv    v26, v10, v9
        vwsubu.vv    v28, v12, v11
        vwsubu.vv    v30, v11, v12
.ifc \type, luma
        vwsubu.vv    v0, v8, v10
        vwsubu.vv    v2, v10, v8
        vwsubu.vv    v4, v13, v11
        vwsubu.vv    v18, v11, v13
.endif
        vsetvli      zero, zero, e16, \e16mul, ta, ma
        vmax.vv      v20, v20, v22 # abs(p0 - q0)
        vmax.vv      v24, v24, v26 # abs(p1 - p0)
        vmax.vv      v28, v28, v30 # abs(q1 - q0)
.ifc \type, luma
        vmax.vv      v22, v0, v2   # abs(p2 - p0)
        vmax.vv      v26, v4, v18  # abs(q2 - q0)
.endif
        vmslt.vx     v1, v20, a2
        vmslt.vx     v2, v24, a3
        vmand.mm     v7, v7, v1
        vmslt.vx     v3, v28, a3
        vmand.mm     v7, v7, v2
.ifc \type, luma
        vmslt.vx     v0, v22, a3
        vmand.mm     v7, v7, v3    # whether to update p0 and q0
        vmslt.vx     v1, v26, a3
        vmand.mm     v0, v0, v7
.else
        vmand.mm     v0, v7, v3    # whether to update p0 and q0
.endif
        vsetvli      zero, zero, e8, \e8mul, ta, mu
.ifc \type, luma
        vadd.vi      v6, v6, 1, v0.t     # tc++
        vadd.vv      v9, v9, v14, v0.t   # p1'
        vmand.mm     v0, v1, v7
        vadd.vi      v6, v6, 1, v0.t     # tc++
        vadd.vv      v12, v12, v15, v0.t # q1'
        vmmv.m       v0, v7
.endif
        vneg.v       v5, v6              # -tc
        vmin.vv      v16, v16, v6
        vwcvtu.x.x.v v18, v10
        vmax.vv      v16, v16, v5
        vwcvtu.x.x.v v20, v11
        vwadd.wv     v18, v18, v16
        vwsub.wv     v20, v20, v16
        vsetvli      zero, zero, e16, \e16mul, ta, ma
        vmax.vx      v18, v18, zero
        vmax.vx      v20, v20, zero
        vsetvli      zero, zero, e8, \e8mul, ta, mu
        vnclipu.wi   v10, v18, 0, v0.t   # p0'
        vnclipu.wi   v11, v20, 0, v0.t   # q0'
        jr           t0
endfunc

func ff_h264_v_loop_filter_\type\()_8_rvv, zve32x
        lpad    0
.ifc \type, luma
        vsetivli  zero, 4, e32, m1, ta, ma
        vle8.v    v4, (a4)
        li        t0, 0x01010101
        vzext.vf4 v6, v4
.else
        vsetivli  zero, 4, e16, mf2, ta, ma
        vle8.v    v4, (a4)
        li        t0, 0x0101
        vzext.vf2 v6, v4
.endif
        sub       t3, a0, a1
        vmul.vx   v6, v6, t0
        vsetivli  zero, 4 * \inners, e8, \e8mul, ta, ma
        vle8.v    v11, (a0)
        sub       t2, t3, a1
        vle8.v    v10, (t3)
        add       t5, a0, a1
        vle8.v    v9, (t2)
.ifc \type, luma
        sub       t1, t2, a1
.endif
        vle8.v    v12, (t5)
.ifc \type, luma
        add       t6, t5, a1
        vle8.v    v8, (t1)
        vle8.v    v13, (t6)
.endif
        jal       t0, ff_h264_loop_filter_\type\()_8_rvv
.ifc \type, luma
        vse8.v    v9, (t2)
        vse8.v    v12, (t5)
.endif
        vse8.v    v10, (t3)
        vse8.v    v11, (a0)
        ret
endfunc

        .variant_cc ff_h264_loop_filter_\type\()_intra_8_rvv
func ff_h264_loop_filter_\type\()_intra_8_rvv, zve32x
        # p3: v8, p2: v9, p1: v10, p0: v11, q0: v12, q1: v13, q2: v14, q3: v15
        # alpha: a2, beta: a3
        csrwi   vxrm, 0
        srai    a4, a2, 2
        vwsubu.vv   v16, v11, v12
        addi    a4, a4, 2
        vwsubu.vv   v18, v12, v11
        vwsubu.vv   v20, v10, v11
        vwsubu.vv   v22, v11, v10
        vwsubu.vv   v24, v13, v12
        vwsubu.vv   v26, v12, v13
        vwsubu.vv   v28, v11, v9
        vwsubu.vv   v30, v9, v11
        vwsubu.vv   v4, v14, v12
        vwsubu.vv   v6, v12, v14
        vsetvli     zero, zero, e16, \e16mul, ta, ma
        vmax.vv     v16, v16, v18 # abs(p0 - q0)
        vmax.vv     v20, v20, v22 # abs(p1 - p0)
        vmslt.vx    v18, v16, a2
        vmax.vv     v24, v24, v26 # abs(q1 - q0)
        vmslt.vx    v22, v20, a3
.ifc \type, luma
        vmax.vv     v28, v28, v30 # abs(p2 - p0)
.endif
        vmand.mm    v18, v18, v22
        vmslt.vx    v23, v24, a3
.ifc \type, luma
        vmax.vv     v4, v4, v6    # abs(q2 - q0)
        vmand.mm    v1, v18, v23  # abs(...) < A && abs(..) < B && abs(..) < B
        vmslt.vx    v3, v16, a4   # abs(p0 - q0) < (alpha / 4) + 2
        vmslt.vx    v6, v28, a3   # abs(p2 - p0) < beta
        vmslt.vx    v7, v4, a3    # abs(q2 - q0) < beta
        vmand.mm    v2, v3, v6
        vmand.mm    v3, v3, v7
.else
        vmand.mm    v0, v18, v23
.endif
        vsetvli     zero, zero, e8, \e8mul, ta, mu
        vwaddu.vv   v22, v11, v13
        vwaddu.vv   v30, v10, v12
        vwaddu.wv   v22, v22, v10
        vwaddu.wv   v30, v30, v13
        vwaddu.wv   v22, v22, v10 #          2p1 + p0 +      q1
        vwaddu.wv   v30, v30, v13 #           p1 +      q0 + 2q1
.ifc \type, luma
        vwaddu.vv   v16, v10, v11
        vwaddu.vv   v20, v8, v9
        vwaddu.wv   v16, v16, v12 #           p1 + p0 + q0
        vwaddu.vv   v24, v11, v12
        vwaddu.vv   v28, v14, v15
        vwaddu.wv   v24, v24, v13 #                p0 + q0 + q1
        vwaddu.wv   v18, v16, v9  #      p2 + p1 + p0 + q0
        vwaddu.wv   v16, v16, v13 #           p1 + p0 + q0 + q1
        vwaddu.wv   v26, v24, v14 #                p0 + q0 + q1 + q2
        vwaddu.wv   v24, v24, v10 #           p1 + p0 + q0 + q1
        vsetvli     zero, zero, e16, \e16mul, ta, ma
        vsll.vi     v20, v20, 1   # 2p3 +2p2
        vadd.vv     v16, v16, v18 #       p2 +2p1 +2p0 +2q0 + q1
        vadd.vv     v20, v18, v20 # 2p3 +3p3 + p1 + p0 + q0
        vsll.vi     v28, v28, 1   #                              2q2 +2q3
        vadd.vv     v24, v24, v26 #           p1 +2p0 +2q0 +2q1 + q2
        vadd.vv     v28, v26, v28 #                p0 + q0 + q1 +3q2 +2q3
        vsetvli     zero, zero, e8, \e8mul, ta, mu
        vmand.mm    v0, v1, v2
        vnclipu.wi  v11, v16, 3, v0.t # p0'
        vnclipu.wi  v10, v18, 2, v0.t # p1'
        vnclipu.wi  v9, v20, 3, v0.t # p2'
        vmandn.mm   v0, v1, v2
.endif
        vnclipu.wi  v11, v22, 2, v0.t # p0'
.ifc \type, luma
        vmand.mm    v0, v1, v3
        vnclipu.wi  v12, v24, 3, v0.t # q0'
        vnclipu.wi  v13, v26, 2, v0.t # q1'
        vnclipu.wi  v14, v28, 3, v0.t # q2'
        vmandn.mm   v0, v1, v3
.endif
        vnclipu.wi  v12, v30, 2, v0.t # q0'
        jr      t0
endfunc

func ff_h264_v_loop_filter_\type\()_intra_8_rvv, zve32x
        lpad    0
        sub       t3, a0, a1
        vsetivli  zero, 4 * \inners, e8, \e8mul, ta, ma
        vle8.v    v12, (a0)
        sub       t2, t3, a1
        vle8.v    v11, (t3)
        add       t4, a0, a1
        vle8.v    v10, (t2)
.ifc \type, luma
        sub       t1, t2, a1
.endif
        vle8.v    v13, (t4)
.ifc \type, luma
        sub       t0, t1, a1
        vle8.v    v9, (t1)
        add       t5, t4, a1
        vle8.v    v8, (t0)
        add       t6, t5, a1
        vle8.v    v14, (t5)
        vle8.v    v15, (t6)
.endif
        jal       t0, ff_h264_loop_filter_\type\()_intra_8_rvv
.ifc \type, luma
        vse8.v    v9, (t1)
        vse8.v    v10, (t2)
.endif
        vse8.v    v11, (t3)
        vse8.v    v12, (a0)
.ifc \type, luma
        vse8.v    v13, (t4)
        vse8.v    v14, (t5)
.endif
        ret
endfunc
.endm

loop_filter luma, 4, m1, m2
loop_filter chroma, 2, mf2, m1

func ff_h264_h_loop_filter_luma_8_rvv, zve32x
        lpad    0
        vsetivli    zero, 4, e32, m1, ta, ma
        vle8.v      v4, (a4)
        li          t0, 0x01010101
        vzext.vf4   v6, v4
        addi        a0, a0, -3
        vmul.vx     v6, v6, t0
        vsetivli    zero, 16, e8, m1, ta, ma
        vlsseg6e8.v v8, (a0), a1
        addi        a0, a0, 1
        jal         t0, ff_h264_loop_filter_luma_8_rvv
        vssseg4e8.v v9, (a0), a1
        ret
endfunc

func ff_h264_h_loop_filter_luma_mbaff_8_rvv, zve32x
        lpad    0
        vsetivli    zero, 4, e16, mf2, ta, ma
        vle8.v      v4, (a4)
        li          t0, 0x0101
        vzext.vf2   v6, v4
        addi        a0, a0, -3
        vmul.vx     v6, v6, t0    # tc_orig
        vsetivli    zero, 8, e8, m1, ta, ma
        vlsseg6e8.v v8, (a0), a1
        addi        a0, a0, 1
        jal         t0, ff_h264_loop_filter_luma_8_rvv
        vssseg4e8.v v9, (a0), a1
        ret
endfunc

func ff_h264_h_loop_filter_chroma_8_rvv, zve32x
        lpad    0
        vsetivli    zero, 4, e16, mf2, ta, ma
        vle8.v      v4, (a4)
        li      t0, 0x0101
        vzext.vf2   v6, v4
        addi    a0, a0, -2
        vmul.vx     v6, v6, t0
        vsetivli    zero, 8, e8, mf2, ta, ma
        vlsseg4e8.v v9, (a0), a1
        addi    a0, a0, 1
        jal     t0, ff_h264_loop_filter_chroma_8_rvv
        vssseg2e8.v v10, (a0), a1
        ret
endfunc

func ff_h264_h_loop_filter_chroma_mbaff_8_rvv, zve32x
        lpad    0
        vsetivli    zero, 4, e8, mf4, ta, ma
        vle8.v      v6, (a4)
        addi    a0, a0, -2
        vsetivli    zero, 4, e8, mf2, ta, ma
        vlsseg4e8.v v9, (a0), a1
        addi    a0, a0, 1
        jal     t0, ff_h264_loop_filter_chroma_8_rvv
        vssseg2e8.v v10, (a0), a1
        ret
endfunc

func ff_h264_h_loop_filter_luma_intra_8_rvv, zve32x
        lpad    0
        addi        a0, a0, -4
        vsetivli    zero, 16, e8, m1, ta, ma
        vlsseg8e8.v v8, (a0), a1
        addi        a0, a0, 1
        jal         t0, ff_h264_loop_filter_luma_intra_8_rvv
        vssseg6e8.v v9, (a0), a1
        ret
endfunc

func ff_h264_h_loop_filter_luma_mbaff_intra_8_rvv, zve32x
        lpad    0
        addi        a0, a0, -4
        vsetivli    zero, 8, e8, m1, ta, ma
        vlsseg8e8.v v8, (a0), a1
        addi        a0, a0, 1
        jal         t0, ff_h264_loop_filter_luma_intra_8_rvv
        vssseg6e8.v v9, (a0), a1
        ret
endfunc

func ff_h264_h_loop_filter_chroma_intra_8_rvv, zve32x
        lpad    0
        addi    a0, a0, -2
        vsetivli    zero, 8, e8, mf2, ta, ma
        vlsseg4e8.v v10, (a0), a1
        addi    a0, a0, 1
        jal     t0, ff_h264_loop_filter_chroma_intra_8_rvv
        vssseg2e8.v v11, (a0), a1
        ret
endfunc

func ff_h264_h_loop_filter_chroma_mbaff_intra_8_rvv, zve32x
        lpad    0
        addi    a0, a0, -2
        vsetivli    zero, 4, e8, mf2, ta, ma
        vlsseg4e8.v v10, (a0), a1
        addi    a0, a0, 1
        jal     t0, ff_h264_loop_filter_chroma_intra_8_rvv
        vssseg2e8.v v11, (a0), a1
        ret
endfunc
