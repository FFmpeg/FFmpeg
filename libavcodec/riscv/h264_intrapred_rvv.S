/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright © 2025 Tristan Matthews
 * Partly based on h264_pred.c Copyright (c) 2023 SiFive, Inc.
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

.macro vsetvlstatic8 len vlen lastlen=0
.if \lastlen == \len
/* These are for when we don't want to change VL */
.if \vlen >= 256
.if \len <= 4
        vsetvli        zero, zero, e8, mf8, ta, ma
.elseif \len <= 8
        vsetvli        zero, zero, e8, mf4, ta, ma
.elseif \len <= 16
        vsetvli        zero, zero, e8, mf2, ta, ma
.elseif \len <= 31
        vsetvli        zero, zero, e8, m1, ta, ma
.endif
.else
.if \len <= 4
        vsetvli        zero, zero, e8, mf4, ta, ma
.elseif \len <= 8
        vsetvli        zero, zero, e8, mf2, ta, ma
.elseif \len <= 16
        vsetvli        zero, zero, e8, m1, ta, ma
.elseif \len <= 31
        vsetvli        zero, zero, e8, m2, ta, ma
.endif
.endif
.else
.if \vlen >= 256
.if \len <= 4
        vsetivli        zero, \len, e8, mf8, ta, ma
.elseif \len <= 8
        vsetivli        zero, \len, e8, mf4, ta, ma
.elseif \len <= 16
        vsetivli        zero, \len, e8, mf2, ta, ma
.elseif \len <= 31
        vsetivli        zero, \len, e8, m1, ta, ma
.endif
.else
.if \len <= 4
        vsetivli        zero, \len, e8, mf4, ta, ma
.elseif \len <= 8
        vsetivli        zero, \len, e8, mf2, ta, ma
.elseif \len <= 16
        vsetivli        zero, \len, e8, m1, ta, ma
.elseif \len <= 31
        vsetivli        zero, \len, e8, m2, ta, ma
.endif
.endif
.endif
.endm

.macro vsetvlstatic16 len vlen lastlen=0
.if \lastlen == \len
/* These are for when we don't want to change VL */
.if \vlen >= 256
.if \len <= 4
        vsetvli        zero, zero, e16, mf4, ta, ma
.elseif \len <= 8
        vsetvli        zero, zero, e16, mf2, ta, ma
.elseif \len <= 16
        vsetvli        zero, zero, e16, m1, ta, ma
.endif
.else
.if \len <= 4
        vsetvli        zero, zero, e16, mf2, ta, ma
.elseif \len <= 8
        vsetvli        zero, zero, e16, m1, ta, ma
.elseif \len <= 16
        vsetvli        zero, zero, e16, m2, ta, ma
.endif
.endif
.else
.if \vlen >= 256
.if \len <= 4
        vsetivli        zero, \len, e16, mf4, ta, ma
.elseif \len <= 8
        vsetivli        zero, \len, e16, mf2, ta, ma
.elseif \len <= 16
        vsetivli        zero, \len, e16, m1, ta, ma
.endif
.else
.if \len <= 4
        vsetivli        zero, \len, e16, mf2, ta, ma
.elseif \len <= 8
        vsetivli        zero, \len, e16, m1, ta, ma
.elseif \len <= 16
        vsetivli        zero, \len, e16, m2, ta, ma
.endif
.endif
.endif
.endm

.macro pred8x8_horizontal_8_rvv vlen
func ff_pred8x8_horizontal_8_rvv_vl\vlen, zve32x
        lpad    0
        vsetvlstatic8 8 \vlen

        addi    t0, a0, -1
        vlse8.v v2, (t0), a1

        mv      t0, a0
        vrgather.vi v0, v2, 0
        vrgather.vi v1, v2, 1
        vse8.v  v0, (t0)
        add     t0, t0, a1
        vse8.v  v1, (t0)
        add     t0, t0, a1
        vrgather.vi v0, v2, 2
        vrgather.vi v1, v2, 3
        vse8.v  v0, (t0)
        add     t0, t0, a1
        vse8.v  v1, (t0)
        add     t0, t0, a1
        vrgather.vi v0, v2, 4
        vrgather.vi v1, v2, 5
        vse8.v  v0, (t0)
        add     t0, t0, a1
        vse8.v  v1, (t0)
        add     t0, t0, a1
        vrgather.vi v0, v2, 6
        vrgather.vi v1, v2, 7
        vse8.v  v0, (t0)
        add     t0, t0, a1
        vse8.v  v1, (t0)

        ret
endfunc
.endm

pred8x8_horizontal_8_rvv 256
pred8x8_horizontal_8_rvv 128

.macro pred8x8_plane_8_rvv vlen
func ff_pred8x8_plane_8_rvv_vl\vlen, zve32x, zba
        lpad    0

        vsetvlstatic16 8 \vlen

        vid.v   v4

        vsetvlstatic8 4 \vlen 8

        sub     t0, a0, a1
        addi    t1, t0, 4
        vle8.v  v8, (t1)

        addi    t2, t0, -1

        li t3, -1
        addi t4, t2, 3
        vlse8.v v10, (t4), t3                # load in reverse order

        slli    t3, a1, 2
        addi    t4, t3, -1
        add     t5, a0, t4

        vlse8.v v11, (t5), a1
        neg     a2, a1                       # a2 = -stride
        sh1add  t4, a1, a1                   # t4 = 3*stride
        add t4, t2, t4
        vlse8.v v13, (t4), a2                # load in reverse order

        vwsubu.vv v18, v8, v10
        vwsubu.vv v20, v11, v13

        vsetvlstatic16 4 \vlen 4

        vmadd.vv v18, v4, v18

        vmadd.vv v20, v4, v20

        vmv.s.x v22, zero
        vwredsum.vs v22, v20, v22

        vmv.s.x v23, zero
        vwredsum.vs v23, v18, v23

        vmv.x.s t6, v23
        slli t1, t6, 4
        add  t6, t6, t1
        addi t6, t6, 16
        srai t6, t6, 5

        vmv.x.s t5, v22
        slli t1, t5, 4
        add  t5, t5, t1
        addi t5, t5, 16
        srai t5, t5, 5

        add t2, t6, t5
        sh1add t3, t2, t2

        slli a2, a1, 3
        sub a2, a2, a1
        addi a2, a2, -1
        add a2, a2, a0
        lbu  a3, (a2)

        addi a2, a0, 7
        sub a2, a2, a1
        lbu a4, (a2)

        add a5, a4, a3
        addi a5, a5, 1
        slli a5, a5, 4

        sub a5, a5, t3                       # linear combination of H, V and src

        vsetvlstatic16 8 \vlen 4

        vmul.vx v18, v4, t6

        .irp n, 19, 20, 21, 22, 23, 24, 25, 26
        vadd.vx v\n, v18, a5
        add a5, a5, t5
        .endr

        .irp n, 19, 20, 21, 22, 23, 24, 25, 26
        vmax.vx v\n, v\n, zero
        .endr

        .irp n, 19, 20, 21, 22, 23, 24, 25, 26
        vsra.vi v\n, v\n, 5
        .endr

        vsetvlstatic8 8 \vlen 8

        vnclipu.wi v19, v19, 0
        vnclipu.wi v20, v20, 0
        vse8.v  v19, (a0)
        add t1, a0, a1
        vnclipu.wi v21, v21, 0
        vse8.v  v20, (t1)
        add t1, t1, a1
        vnclipu.wi v22, v22, 0
        vse8.v  v21, (t1)
        add t1, t1, a1
        vnclipu.wi v23, v23, 0
        vse8.v  v22, (t1)
        add t1, t1, a1
        vnclipu.wi v24, v24, 0
        vse8.v  v23, (t1)
        add t1, t1, a1
        vnclipu.wi v25, v25, 0
        vse8.v  v24, (t1)
        add t1, t1, a1
        vnclipu.wi v26, v26, 0
        vse8.v  v25, (t1)
        add t1, t1, a1
        vse8.v  v26, (t1)

        ret
endfunc
.endm

pred8x8_plane_8_rvv 256
pred8x8_plane_8_rvv 128

.macro pred16x16_horizontal_8_rvv vlen
func ff_pred16x16_horizontal_8_rvv_vl\vlen, zve32x
        lpad    0
        vsetvlstatic8 16 \vlen

        addi    t0, a0, -1
        vlse8.v v2, (t0), a1

        mv      t0, a0
        vrgather.vi v0, v2, 0
        vrgather.vi v1, v2, 1
        vse8.v  v0, (t0)
        add     t0, t0, a1
        vse8.v  v1, (t0)
        add     t0, t0, a1
        vrgather.vi v0, v2, 2
        vrgather.vi v1, v2, 3
        vse8.v  v0, (t0)
        add     t0, t0, a1
        vse8.v  v1, (t0)
        add     t0, t0, a1
        vrgather.vi v0, v2, 4
        vrgather.vi v1, v2, 5
        vse8.v  v0, (t0)
        add     t0, t0, a1
        vse8.v  v1, (t0)
        add     t0, t0, a1
        vrgather.vi v0, v2, 6
        vrgather.vi v1, v2, 7
        vse8.v  v0, (t0)
        add     t0, t0, a1
        vse8.v  v1, (t0)
        add     t0, t0, a1
        vrgather.vi v0, v2, 8
        vrgather.vi v1, v2, 9
        vse8.v  v0, (t0)
        add     t0, t0, a1
        vse8.v  v1, (t0)
        add     t0, t0, a1
        vrgather.vi v0, v2, 10
        vrgather.vi v1, v2, 11
        vse8.v  v0, (t0)
        add     t0, t0, a1
        vse8.v  v1, (t0)
        add     t0, t0, a1
        vrgather.vi v0, v2, 12
        vrgather.vi v1, v2, 13
        vse8.v  v0, (t0)
        add     t0, t0, a1
        vse8.v  v1, (t0)
        add     t0, t0, a1
        vrgather.vi v0, v2, 14
        vrgather.vi v1, v2, 15
        vse8.v  v0, (t0)
        add     t0, t0, a1
        vse8.v  v1, (t0)

        ret
endfunc
.endm

pred16x16_horizontal_8_rvv 256
pred16x16_horizontal_8_rvv 128

.macro pred16x16_vertical_8_rvv vlen
func ff_pred16x16_vertical_8_rvv_vl\vlen, zve32x
        lpad    0
        vsetvlstatic8 16 \vlen

        sub     t0, a0, a1

        vle8.v  v0, (t0)

        vse8.v  v0, (a0)

        add t1, a0, a1
        vse8.v  v0, (t1)

        .rept 14
        add t1, t1, a1
        vse8.v  v0, (t1)
        .endr

        ret
endfunc
.endm

pred16x16_vertical_8_rvv 256
pred16x16_vertical_8_rvv 128

.macro pred16x16_128_dc_8 vlen
func ff_pred16x16_128_dc_8_rvv_vl\vlen, zve32x
        lpad    0
        vsetvlstatic8 16 \vlen

        li t0, 128
        vmv.v.x v0, t0

        slli a2, a1, 1
        vse8.v  v0, (a0)

        add t2, a0, a1
        vse8.v  v0, (t2)

        add t1, a0, a2
        add t2, t2, a2

        .rept 6
        vse8.v  v0, (t1)
        vse8.v  v0, (t2)

        add t1, t1, a2
        add t2, t2, a2
        .endr

        vse8.v  v0, (t1)
        vse8.v  v0, (t2)

        ret
endfunc
.endm

pred16x16_128_dc_8 256
pred16x16_128_dc_8 128

.macro pred16x16_dc_8_rvv vlen
func ff_pred16x16_dc_8_rvv_vl\vlen, zve32x
        lpad    0
        vsetvlstatic16 16 \vlen

        addi t0, a0, -1
        vlse8.v v0, (t0), a1

        sub     t1, a0, a1
        vle8.v  v1, (t1)

        vmv.s.x v2, zero

        vsetvlstatic8 16 \vlen 16

        vwredsumu.vs v2, v0, v2 # sum = sum(left)
        vwredsumu.vs v2, v1, v2 # sum += sum(top)

        vsetvlstatic16 8 \vlen 16

        li t0, 16
        vadd.vx v2, v2, t0
        vsrl.vi v3, v2, 5

        vsetvlstatic8 16 \vlen 8

        vrgather.vi v4, v3, 0

        vse8.v v4, (a0)
        add t1, a0, a1

        .rept 15
        vse8.v v4, (t1)
        add t1, t1, a1
        .endr

        ret
endfunc
.endm

pred16x16_dc_8_rvv 256
pred16x16_dc_8_rvv 128

.macro pred16x16_left_dc_8_rvv vlen
func ff_pred16x16_left_dc_8_rvv_vl\vlen, zve32x
        lpad    0
        vsetvlstatic16 16 \vlen

        addi t2, a0, -1
        vlse8.v v1, (t2), a1

        vmv.s.x v2, zero

        vsetvlstatic8 16 \vlen 16
        vwredsumu.vs v2, v1, v2

        vsetvlstatic16 8 \vlen 16

        vadd.vi v2, v2, 8
        vsrl.vi v3, v2, 4

        vsetvlstatic8 16 \vlen 8

        vrgather.vi v4, v3, 0

        slli a2, a1, 1
        add t4, a0, a1

        vse8.v v4, (a0)
        vse8.v v4, (t4)
        add t1, a0, a2
        add t4, t4, a2

        .rept 6
        vse8.v v4, (t1)
        vse8.v v4, (t4)
        add t1, t1, a2
        add t4, t4, a2
        .endr

        vse8.v v4, (t1)
        vse8.v v4, (t4)

        ret
endfunc
.endm

pred16x16_left_dc_8_rvv 256
pred16x16_left_dc_8_rvv 128

.macro pred16x16_top_dc_8_rvv vlen
func ff_pred16x16_top_dc_8_rvv_vl\vlen, zve32x, zba
        lpad    0
        vsetvlstatic16 16 \vlen

        sub     t0, a0, a1
        vle8.v  v0, (t0)

        vmv.s.x v1, zero

        vsetvlstatic8 16 \vlen 16

        vwredsumu.vs v1, v0, v1

        vsetvlstatic16 8 \vlen 16

        vadd.vi v1, v1, 8
        vsrl.vi v1, v1, 4

        vsetvlstatic8 16 \vlen 8

        vrgather.vi v2, v1, 0

        vse8.v v2, (a0)
        sh1add t1, a1, a0
        add t2, a0, a1
        vse8.v v2, (t2)

        sh1add t2, a1, t2
        vse8.v v2, (t1)
        vse8.v v2, (t2)

        .rept 6
        sh1add t1, a1, t1
        sh1add t2, a1, t2
        vse8.v v2, (t1)
        vse8.v v2, (t2)
        .endr

        ret
endfunc
.endm

pred16x16_top_dc_8_rvv 256
pred16x16_top_dc_8_rvv 128

.macro pred16x16_plane_8_rvv vlen
func ff_pred16x16_plane_8_rvv_vl\vlen, zve32x, zba
        lpad    0

        vsetvlstatic8 8 \vlen

        sub     t0, a0, a1
        addi    t1, t0, 8
        addi    t2, t0, -1
        li      t3, -1
        vle8.v  v3, (t1)
        add     t4, t2, 7
        vlse8.v v5, (t4), t3

        sh3add  t3, a1, a0
        addi    t3, t3, -1

        vlse8.v v6, (t3), a1

        neg a2, a1
        sh3add t4, a1, a2                   # t4 = 7*stride
        add t5, t2, t4
        vlse8.v v8, (t5), a2                # load in reverse order

        vwsubu.vv v13, v3, v5
        vwsubu.vv v16, v6, v8

        vsetvlstatic16 16 \vlen 8

        vid.v   v14

        vsetvlstatic16 8 \vlen 16

        vmadd.vv v13, v14, v13

        vmadd.vv v16, v14, v16

        vmv.s.x v18, zero
        vwredsum.vs v18, v16, v18

        vmv.s.x v19, zero
        vwredsum.vs v19, v13, v19

        vmv.x.s t4, v19
        sh2add t1, t4, t4
        addi t1, t1, 32
        srai t1, t1, 6

        vmv.x.s t5, v18
        sh2add t2, t5, t5
        addi t2, t2, 32
        srai t2, t2, 6

        add t3, t1, t2
        slli t4, t3, 3
        sub t4, t4, t3

        slli t5, a1, 4
        sub t5, t5, a1
        addi t5, t5, -1

        sub t6, a0, a1
        addi t6, t6, 15
        lbu a2, (t6)

        add a3, a0, t5
        lbu a4, (a3)

        add a4, a4, a2
        addi a4, a4, 1
        slli a4, a4, 4

        sub a5, a4, t4                       # a5 = linear combination of H, V and src

        vsetvlstatic16 16 \vlen 8

        vmul.vx v24, v14, t1

        mv t3, a0

        .rept 2

        vsetvlstatic16 16 \vlen 16

        .irp n, 2, 4, 6, 8, 10, 12, 14, 16
        vadd.vx v\n, v24, a5
        add a5, a5, t2
        .endr

        .irp n, 2, 4, 6, 8, 10, 12, 14, 16
        vmax.vx v\n, v\n, zero
        .endr

        .irp n, 2, 4, 6, 8, 10, 12, 14, 16
        vsra.vi v\n, v\n, 5
        .endr

        vsetvlstatic8 16 \vlen 16

        vnclipu.wi v2, v2, 0
        vnclipu.wi v4, v4, 0
        vse8.v  v2, (t3)
        add t3, t3, a1
        vnclipu.wi v6, v6, 0
        vse8.v  v4, (t3)
        add t3, t3, a1
        vnclipu.wi v8, v8, 0
        vse8.v  v6, (t3)
        add t3, t3, a1
        vnclipu.wi v10, v10, 0
        vse8.v  v8, (t3)
        add t3, t3, a1
        vnclipu.wi v12, v12, 0
        vse8.v  v10, (t3)
        add t3, t3, a1
        vnclipu.wi v14, v14, 0
        vse8.v  v12, (t3)
        add t3, t3, a1
        vnclipu.wi v16, v16, 0
        vse8.v  v14, (t3)
        add t3, t3, a1
        vse8.v  v16, (t3)
        add t3, t3, a1
        .endr

        ret
endfunc
.endm

pred16x16_plane_8_rvv 256
pred16x16_plane_8_rvv 128
