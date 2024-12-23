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

#include "libavcodec/riscv/h26x/asm.S"

.macro func_sad vlen
func ff_vvc_sad_rvv_\vlen, zve32x, zbb, zba
        lpad    0
        slli              t2, a3, 7  // dy * 128
        li                t1, 4*128+4
        add               t3, t2, a2 // dy * 128 + dx
        sub               t1, t1, t2
        sub               t1, t1, a2
        sh1add            a0, t3, a0
        sh1add            a1, t1, a1
        li                t3, 16
        beq               a4, t3, SADVSET\vlen\()16
        .irp w,8,16
SADVSET\vlen\w:
        vsetvlstatic32    \w, \vlen
        vmv.v.i           v0, 0
        vmv.s.x           v24, zero
        vsetvlstatic16    \w, \vlen, tu
SAD\vlen\w:
        addi              a5, a5, -2
        vle16.v           v8, (a0)
        vle16.v           v16, (a1)
        vsub.vv           v8, v8, v16
        vneg.v            v16, v8
        addi              a0, a0, 2 * 128 * 2
        vmax.vv           v8, v8, v16
        vwaddu.wv         v0, v0, v8
        addi              a1, a1, 2 * 128 * 2
        bnez              a5, SAD\vlen\w
        vsetvlstatic32    \w, \vlen
        vredsum.vs        v24, v0, v24
        vmv.x.s           a0, v24
        ret
        .endr
endfunc
.endm

func_sad 256
func_sad 128
