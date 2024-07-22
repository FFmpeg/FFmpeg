/*
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

func ff_h264_add_pixels4_8_rvv, zve32x
        lpad    0
        vsetivli        zero, 4, e8, mf4, ta, ma
        vlse32.v        v8, (a0), a2
        vsetivli        zero, 4 * 4, e8, m1, ta, ma
        vle16.v         v16, (a1)
        .equ    offset, 0
        .rept   256 / __riscv_xlen
        sx      zero, offset(a1)
        .equ    offset, offset + (__riscv_xlen / 8)
        .endr
        vncvt.x.x.w     v24, v16
        vadd.vv         v8, v8, v24
        vsetivli        zero, 4, e8, mf4, ta, ma
        vsse32.v        v8, (a0), a2
        ret
endfunc

func ff_h264_add_pixels4_16_rvv, zve64x
        lpad    0
        vsetivli        zero, 4, e16, mf2, ta, ma
        vlse64.v        v8, (a0), a2
        vsetivli        zero, 4 * 4, e16, m2, ta, ma
        vle32.v         v16, (a1)
        .equ    offset, 0
        .rept   512 / __riscv_xlen
        sx      zero, offset(a1)
        .equ    offset, offset + (__riscv_xlen / 8)
        .endr
        vncvt.x.x.w     v24, v16
        vadd.vv         v8, v8, v24
        vsetivli        zero, 4, e16, mf2, ta, ma
        vsse64.v        v8, (a0), a2
        ret
endfunc

func ff_h264_add_pixels8_8_rvv, zve64x
        lpad    0
        li      t0, 8 * 8
        vsetivli        zero, 8, e8, mf2, ta, ma
        vlse64.v        v8, (a0), a2
        vsetvli         zero, t0, e8, m4, ta, ma
        vle16.v         v16, (a1)
        .equ    offset, 0
        .rept   1024 / __riscv_xlen
        sx      zero, offset(a1)
        .equ    offset, offset + (__riscv_xlen / 8)
        .endr
        vncvt.x.x.w     v24, v16
        vadd.vv         v8, v8, v24
        vsetivli        zero, 8, e8, mf2, ta, ma
        vsse64.v        v8, (a0), a2
        ret
endfunc

func ff_h264_add_pixels8_16_rvv, zve32x
        lpad    0
        li      t0, 8
        vsetivli    zero, 8, e16, m1, ta, ma
1:
        vle32.v     v16, (a1)
        addi    t0, t0, -1
        vle16.v     v8, (a0)
        .equ    offset, 0
        .rept   256 / __riscv_xlen
        sx      zero, offset(a1)
        .equ    offset, offset + (__riscv_xlen / 8)
        .endr
        vncvt.x.x.w v24, v16
        addi    a1, a1, 8 * 4
        vadd.vv     v8, v8, v24
        vse16.v     v8, (a0)
        add     a0, a0, a2
        bnez    t0, 1b

        ret
endfunc
