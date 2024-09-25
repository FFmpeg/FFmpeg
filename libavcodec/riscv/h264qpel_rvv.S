/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Niklas Haas
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

.macro  lx rd, addr
#if (__riscv_xlen == 32)
        lw      \rd, \addr
#elif (__riscv_xlen == 64)
        ld      \rd, \addr
#else
        lq      \rd, \addr
#endif
.endm

.macro  sx rd, addr
#if (__riscv_xlen == 32)
        sw      \rd, \addr
#elif (__riscv_xlen == 64)
        sd      \rd, \addr
#else
        sq      \rd, \addr
#endif
.endm

        /* output is unclipped; clobbers v26-v31 plus t0 and t02 */
.macro lowpass_h         vdst, src
        addi             t4, \src, 3
        lbu              t5, 2(\src)
        vle8.v           v31, (t4)
        lbu              t4, 1(\src)
        vslide1up.vx     v30, v31, t5
        lbu              t5, 0(\src)
        vslide1up.vx     v29, v30, t4
        lbu              t4, -1(\src)
        vslide1up.vx     v28, v29, t5
        lbu              t5, -2(\src)
        vslide1up.vx     v27, v28, t4
        vslide1up.vx     v26, v27, t5
        vwaddu.vv        \vdst, v26, v31
        vwmaccu.vx       \vdst, t6, v28
        vwmaccu.vx       \vdst, t6, v29
        vwmaccsu.vx      \vdst, a7, v27
        vwmaccsu.vx      \vdst, a7, v30
.endm

        /* output is unclipped */
.macro lowpass_v         vdst, vsrc0, vsrc1, vsrc2, vsrc3, vsrc4, vsrc5, signed=0
    .if \signed
        vwadd.vv         \vdst, \vsrc0, \vsrc5
        vwmacc.vx        \vdst, t6, \vsrc2
        vwmacc.vx        \vdst, t6, \vsrc3
        vwmacc.vx        \vdst, a7, \vsrc1
        vwmacc.vx        \vdst, a7, \vsrc4
    .else
        vwaddu.vv        \vdst, \vsrc0, \vsrc5
        vwmaccu.vx       \vdst, t6, \vsrc2
        vwmaccu.vx       \vdst, t6, \vsrc3
        vwmaccsu.vx      \vdst, a7, \vsrc1
        vwmaccsu.vx      \vdst, a7, \vsrc4
    .endif
.endm

.macro qpel_mc00         op, dst, src, stride, size
func ff_\op\()_h264_qpel_pixels, zve32x
1:      add              t1, a2, a1
        add              t2, a2, t1
        add              t3, a2, t2
        vle8.v           v0, (a1)
        vle8.v           v1, (t1)
        vle8.v           v2, (t2)
        vle8.v           v3, (t3)
        addi             a4, a4, -4
        add              a1, a2, t3
        add              t1, a2, a0
        add              t2, a2, t1
        add              t3, a2, t2
    .ifc \op, avg
        vle8.v           v4, (a0)
        vle8.v           v5, (t1)
        vle8.v           v6, (t2)
        vle8.v           v7, (t3)
        vaaddu.vv        v0, v0, v4
        vaaddu.vv        v1, v1, v5
        vaaddu.vv        v2, v2, v6
        vaaddu.vv        v3, v3, v7
    .endif
        vse8.v           v0, (a0)
        vse8.v           v1, (t1)
        vse8.v           v2, (t2)
        vse8.v           v3, (t3)
        add              a0, a2, t3
        bnez             a4, 1b
        jr               t0
endfunc
.endm

        qpel_mc00        put, a0, a1, a2, a4
        qpel_mc00        avg, a0, a1, a2, a4

.macro qpel_lowpass      op, ext, lmul, lmul2
func ff_\op\()_h264_qpel_h_lowpass_\lmul\ext, zve32x
1:      add              t1, a3, a1
        add              t2, a3, t1
        add              t3, a3, t2
        lowpass_h        v0, a1
        lowpass_h        v2, t1
        lowpass_h        v4, t2
        lowpass_h        v6, t3
        add              a1, a3, t3
        addi             a4, a4, -4
        vsetvli          zero, zero, e16, \lmul2, ta, ma
        vmax.vx          v0, v0, zero
        vmax.vx          v2, v2, zero
        vmax.vx          v4, v4, zero
        vmax.vx          v6, v6, zero
        vsetvli          zero, zero, e8, \lmul, ta, ma
        vnclipu.wi       v0, v0, 5
        vnclipu.wi       v2, v2, 5
        vnclipu.wi       v4, v4, 5
        vnclipu.wi       v6, v6, 5
    .ifc \ext, _l2
        add              t1, a6, a5
        add              t2, a6, t1
        add              t3, a6, t2
        vle8.v           v8,  (a5)
        vle8.v           v10, (t1)
        vle8.v           v12, (t2)
        vle8.v           v14, (t3)
        add              a5, a2, t3
        vaaddu.vv        v0, v0, v8
        vaaddu.vv        v2, v2, v10
        vaaddu.vv        v4, v4, v12
        vaaddu.vv        v6, v6, v14
    .endif
        add              t1, a2, a0
        add              t2, a2, t1
        add              t3, a2, t2
    .ifc \op, avg
        vle8.v           v1, (a0)
        vle8.v           v3, (t1)
        vle8.v           v5, (t2)
        vle8.v           v7, (t3)
        vaaddu.vv        v0, v0, v1
        vaaddu.vv        v2, v2, v3
        vaaddu.vv        v4, v4, v5
        vaaddu.vv        v6, v6, v7
    .endif
        vse8.v           v0, (a0)
        vse8.v           v2, (t1)
        vse8.v           v4, (t2)
        vse8.v           v6, (t3)
        add              a0, a2, t3
        bnez             a4, 1b
        jr               t0
endfunc

func ff_\op\()_h264_qpel_v_lowpass_\lmul\ext, zve32x
        sub              t1, a1, a3
        sub              t2, t1,   a3
        vle8.v           v2, (a1)
        vle8.v           v1, (t1)
        vle8.v           v0, (t2)
        add              t1, a1, a3
        add              t2,   t1, a3
        add              a1, t2, a3
        vle8.v           v3, (t1)
        vle8.v           v4, (t2)
1:      add              t1, a3, a1
        add              t2, a3, t1
        add              t3, a3, t2
        vle8.v           v5, (a1)
        vle8.v           v6, (t1)
        vle8.v           v7, (t2)
        vle8.v           v8, (t3)
        add              a1, a3, t3
        lowpass_v        v24, v0, v1, v2, v3, v4, v5
        lowpass_v        v26, v1, v2, v3, v4, v5, v6
        lowpass_v        v28, v2, v3, v4, v5, v6, v7
        lowpass_v        v30, v3, v4, v5, v6, v7, v8
        addi             a4, a4, -4
        vsetvli          zero, zero, e16, \lmul2, ta, ma
        vmax.vx          v24, v24, zero
        vmax.vx          v26, v26, zero
        vmax.vx          v28, v28, zero
        vmax.vx          v30, v30, zero
        vsetvli          zero, zero, e8, \lmul, ta, ma
        vnclipu.wi       v24, v24, 5
        vnclipu.wi       v26, v26, 5
        vnclipu.wi       v28, v28, 5
        vnclipu.wi       v30, v30, 5
    .ifc \ext, _l2
        add              t1, a6, a5
        add              t2, a6, t1
        add              t3, a6, t2
        vle8.v           v9, (a5)
        vle8.v           v10, (t1)
        vle8.v           v11, (t2)
        vle8.v           v12, (t3)
        add              a5, a6, t3
        vaaddu.vv        v24, v24, v9
        vaaddu.vv        v26, v26, v10
        vaaddu.vv        v28, v28, v11
        vaaddu.vv        v30, v30, v12
    .endif
        add              t1, a2, a0
        add              t2, a2, t1
        add              t3, a2, t2
    .ifc \op, avg
        vle8.v           v9, (a0)
        vle8.v           v10, (t1)
        vle8.v           v11, (t2)
        vle8.v           v12, (t3)
        vaaddu.vv        v24, v24, v9
        vaaddu.vv        v26, v26, v10
        vaaddu.vv        v28, v28, v11
        vaaddu.vv        v30, v30, v12
    .endif
        vse8.v           v24, (a0)
        vse8.v           v26, (t1)
        vse8.v           v28, (t2)
        vse8.v           v30, (t3)
        add              a0, a2, t3
        vmv.v.v          v0, v4
        vmv.v.v          v1, v5
        vmv.v.v          v2, v6
        vmv.v.v          v3, v7
        vmv.v.v          v4, v8
        bnez             a4, 1b
        jr               t0
endfunc

func ff_\op\()_h264_qpel_hv_lowpass_\lmul\ext, zve32x
        sub              t1, a1, a3
        sub              t2, t1,   a3
        lowpass_h        v4, a1
        lowpass_h        v2, t1
        lowpass_h        v0, t2
        add              t1, a1, a3
        add              t2,   t1, a3
        add              a1, t2, a3
        lowpass_h        v6, t1
        lowpass_h        v8, t2
1:      add              t1, a3, a1
        add              t2, a3, t1
        add              t3, a3, t2
        lowpass_h        v10, a1
        lowpass_h        v12, t1
        lowpass_h        v14, t2
        lowpass_h        v16, t3
        vsetvli          zero, zero, e16, \lmul2, ta, ma
        addi             a4, a4, -4
        lowpass_v        v20, v0, v2,  v4,  v6,  v8, v10, signed=1
        lowpass_v        v24, v2, v4,  v6,  v8, v10, v12, signed=1
        lowpass_v        v28, v4, v6,  v8, v10, v12, v14, signed=1
        vnclip.wi        v0, v20, 10
        lowpass_v        v20, v6, v8, v10, v12, v14, v16, signed=1
        vnclip.wi        v2, v24, 10
        vnclip.wi        v4, v28, 10
        vnclip.wi        v6, v20, 10
        vmax.vx          v18, v0, zero
        vmax.vx          v20, v2, zero
        vmax.vx          v22, v4, zero
        vmax.vx          v24, v6, zero
        vmv.v.v          v0, v8
        vmv.v.v          v2, v10
        vmv.v.v          v4, v12
        vmv.v.v          v6, v14
        vmv.v.v          v8, v16
        add              a1, a3, t3
        vsetvli          zero, zero, e8, \lmul, ta, ma
        vnclipu.wi       v18, v18, 0
        vnclipu.wi       v20, v20, 0
        vnclipu.wi       v22, v22, 0
        vnclipu.wi       v24, v24, 0
    .ifc \ext, _l2
        add              t1, a6, a5
        add              t2, a6, t1
        add              t3, a6, t2
        vle8.v           v26, (a5)
        vle8.v           v27, (t1)
        vle8.v           v28, (t2)
        vle8.v           v29, (t3)
        add              a5, a6, t3
        vaaddu.vv        v18, v18, v26
        vaaddu.vv        v20, v20, v27
        vaaddu.vv        v22, v22, v28
        vaaddu.vv        v24, v24, v29
    .endif
        add              t1, a2, a0
        add              t2, a2, t1
        add              t3, a2, t2
    .ifc \op, avg
        vle8.v           v26, (a0)
        vle8.v           v27, (t1)
        vle8.v           v28, (t2)
        vle8.v           v29, (t3)
        vaaddu.vv        v18, v18, v26
        vaaddu.vv        v20, v20, v27
        vaaddu.vv        v22, v22, v28
        vaaddu.vv        v24, v24, v29
    .endif
        vse8.v           v18, (a0)
        vse8.v           v20, (t1)
        vse8.v           v22, (t2)
        vse8.v           v24, (t3)
        add              a0, a2, t3
        bnez             a4, 1b
        jr               t0
endfunc
.endm

/* Note: We could possibly specialize for the width 8 / width 4 cases by
   loading 32 bit integers, but this makes the convolutions more complicated
   to implement, so it's not necessarily any faster. */

.macro h264_qpel         lmul, lmul2
        qpel_lowpass     put,    , \lmul, \lmul2
        qpel_lowpass     put, _l2, \lmul, \lmul2
        qpel_lowpass     avg,    , \lmul, \lmul2
        qpel_lowpass     avg, _l2, \lmul, \lmul2
.endm

        h264_qpel        m1,  m2
        h264_qpel        mf2, m1
        h264_qpel        mf4, mf2
        h264_qpel        mf8, mf4

.macro h264_qpel_1pass   op, case, lmul, size, ext=rvv, dir, offset
func ff_\op\()_h264_qpel\size\()_\case\()_\ext, zve32x
        lpad             0
        vsetivli         zero, \size, e8, \lmul, ta, ma
        csrwi            vxrm, 0
        li               a4, \size
        li               t6, 20
        li               a7, -5
        mv               a3, a2
        mv               t0, ra
.ifnb \offset
    .ifc \dir, v
        add              a5, a1, \offset
    .else
        addi             a5, a1, \offset
    .endif
        mv               a6, a3
        j                ff_\op\()_h264_qpel_\dir\()_lowpass_\lmul\()_l2
.else
        j                ff_\op\()_h264_qpel_\dir\()_lowpass_\lmul\()
.endif
endfunc
.endm

.macro h264_qpel_2pass   op, case, lmul, size, ext=rvv, dir1, dir2, off1=0, off2
func ff_\op\()_h264_qpel\size\()_\case\()_\ext, zve32x
        lpad             0
        vsetivli         zero, \size, e8, \lmul, ta, ma
        csrwi            vxrm, 0
        addi             sp, sp, (-(__riscv_xlen >> 2))
        li               a4, \size
        li               t6, 20
        li               a7, -5
        sx               a0, 0(sp)
        sx               a1, (__riscv_xlen >> 3)(sp)
    .ifc \off1, a2
        add              a1, a1, \off1
    .elseif \off1
        addi             a1, a1, \off1
    .endif
        mv               a3, a2
    .ifc \op, avg
        // Use temporary array on stack for the first pass
        addi             a0, sp, -(\size * \size)
        li               a2, \size
    .endif
        jal              t0, ff_put_h264_qpel_\dir1\()_lowpass_\lmul
        lx               a0, 0(sp)
        lx               a1, (__riscv_xlen >> 3)(sp)
    .ifc \op, put
        // Directly reuse the first pass output buffer
        mv               a5, a0
        mv               a6, a2
    .else
        addi             a5, sp, -(\size * \size)
        li               a6, \size
        mv               a2, a3
    .endif
    .ifnb \off2
        addi             a1, a1, \off2
    .endif
        li               a4, \size
        mv               t0, ra
        addi             sp, sp, 16
        j                ff_\op\()_h264_qpel_\dir2\()_lowpass_\lmul\()_l2
endfunc
.endm

.macro ff_h264_qpel_fns  op, lmul, size, ext=rvv
func ff_\op\()_h264_qpel\size\()_mc00_\ext, zve32x
        lpad             0
        vsetivli         zero, \size, e8, \lmul, ta, ma
        csrwi            vxrm, 0
        li               a4, \size
        mv               t0, ra
        j                ff_\op\()_h264_qpel_pixels
endfunc

        h264_qpel_1pass  \op, mc20, \lmul, \size, \ext, h
        h264_qpel_1pass  \op, mc02, \lmul, \size, \ext, v
        h264_qpel_1pass  \op, mc10, \lmul, \size, \ext, h, 0
        h264_qpel_1pass  \op, mc30, \lmul, \size, \ext, h, 1
        h264_qpel_1pass  \op, mc01, \lmul, \size, \ext, v, zero
        h264_qpel_1pass  \op, mc03, \lmul, \size, \ext, v, a2
        h264_qpel_1pass  \op, mc22, \lmul, \size, \ext, hv

        h264_qpel_2pass  \op, mc11, \lmul, \size, \ext, h, v
        h264_qpel_2pass  \op, mc21, \lmul, \size, \ext, h, hv
        h264_qpel_2pass  \op, mc12, \lmul, \size, \ext, v, hv
        h264_qpel_2pass  \op, mc31, \lmul, \size, \ext, h, v, off2=1
        h264_qpel_2pass  \op, mc13, \lmul, \size, \ext, h, v, a2
        h264_qpel_2pass  \op, mc33, \lmul, \size, \ext, h, v, a2, 1
        h264_qpel_2pass  \op, mc23, \lmul, \size, \ext, h, hv, a2
        h264_qpel_2pass  \op, mc32, \lmul, \size, \ext, v, hv, 1
.endm

        ff_h264_qpel_fns put, mf2, 16, rvv256
        ff_h264_qpel_fns put, mf4, 8,  rvv256
        /* ff_h264_qpel_fns put, mf8, 4,  rvv256 */

        ff_h264_qpel_fns avg, mf2, 16, rvv256
        ff_h264_qpel_fns avg, mf4, 8,  rvv256
        /* ff_h264_qpel_fns avg, mf8, 4,  rvv256 */

        ff_h264_qpel_fns put, m1,  16, rvv
        ff_h264_qpel_fns put, mf2, 8,  rvv
        ff_h264_qpel_fns put, mf4, 4,  rvv

        ff_h264_qpel_fns avg, m1,  16, rvv
        ff_h264_qpel_fns avg, mf2, 8,  rvv
        ff_h264_qpel_fns avg, mf4, 4,  rvv
