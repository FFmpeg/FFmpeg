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

.macro avg w, vlen, id
\id\w\vlen:
.if \w < 128
        vsetvlstatic16    \w, \vlen
        addi              t0, a2, 128*2
        addi              t1, a3, 128*2
        add               t2, a0, a1
        vle16.v           v0, (a2)
        vle16.v           v8, (a3)
        addi              a5, a5, -2
        vle16.v           v16, (t0)
        vle16.v           v24, (t1)
        vadd.vv           v8, v8, v0
        vadd.vv           v24, v24, v16
        vmax.vx           v8, v8, zero
        vmax.vx           v24, v24, zero
        vsetvlstatic8     \w, \vlen
        addi              a2, a2, 128*4
        vnclipu.wi        v8, v8, 7
        vnclipu.wi        v24, v24, 7
        addi              a3, a3, 128*4
        vse8.v            v8, (a0)
        vse8.v            v24, (t2)
        sh1add            a0, a1, a0
.else
        addi              a5, a5, -1
        mv                t1, a0
        mv                t2, a2
        mv                t3, a3
        mv                t4, a4
1:
        vsetvli           t0, a4, e16, m8, ta, ma
        sub               a4, a4, t0
        vle16.v           v0, (a2)
        vle16.v           v8, (a3)
        vadd.vv           v8, v8, v0
        vmax.vx           v8, v8, zero
        vsetvli           zero, zero, e8, m4, ta, ma
        vnclipu.wi        v8, v8, 7
        vse8.v            v8, (a0)
        sh1add            a2, t0, a2
        sh1add            a3, t0, a3
        add               a0, a0, t0
        bnez              a4, 1b
        add               a0, t1, a1
        addi              a2, t2, 128*2
        addi              a3, t3, 128*2
        mv                a4, t4
.endif
        bnez              a5, \id\w\vlen\()b
        ret
.endm

.macro func_avg vlen
func ff_vvc_avg_8_rvv_\vlen\(), zve32x, zbb, zba
        lpad    0
        POW2_JMP_TABLE    1, \vlen
        csrwi             vxrm, 0
        POW2_J            \vlen, 1, a4
        .irp w,2,4,8,16,32,64,128
        avg               \w, \vlen, 1
        .endr
endfunc
.endm

func_avg 128
func_avg 256

#if (__riscv_xlen == 64)
.macro w_avg w, vlen, id
\id\w\vlen:
.if \w <= 32 || (\w == 64 && \vlen == 256)
        vsetvlstatic16    \w, \vlen
        addi              t0, a2, 128*2
        addi              t1, a3, 128*2
        vle16.v           v0, (a2)
        vle16.v           v4, (a3)
        addi              a5, a5, -2
        vle16.v           v8, (t0)
        vle16.v           v12, (t1)
        vwmul.vx          v16, v0, a7
        vwmul.vx          v24, v8, a7
        vwmacc.vx         v16, t3, v4
        vwmacc.vx         v24, t3, v12
        vsetvlstatic32    \w, \vlen
        add               t2, a0, a1
        vadd.vx           v16, v16, t4
        vadd.vx           v24, v24, t4
        vsetvlstatic16    \w, \vlen
        vnsrl.wx          v16, v16, t6
        vnsrl.wx          v24, v24, t6
        vmax.vx           v16, v16, zero
        vmax.vx           v24, v24, zero
        vsetvlstatic8     \w, \vlen
        addi              a2, a2, 128*4
        vnclipu.wi        v16, v16, 0
        vnclipu.wi        v24, v24, 0
        vse8.v            v16, (a0)
        addi              a3, a3, 128*4
        vse8.v            v24, (t2)
        sh1add            a0, a1, a0
.else
        addi              a5, a5, -1
        mv                t1, a0
        mv                t2, a2
        mv                t5, a3
        mv                a6, a4
1:
        vsetvli           t0, a4, e16, m4, ta, ma
        sub               a4, a4, t0
        vle16.v           v0, (a2)
        vle16.v           v4, (a3)
        vwmul.vx          v16, v0, a7
        vwmacc.vx         v16, t3, v4
        vsetvli           zero, zero, e32, m8, ta, ma
        vadd.vx           v16, v16, t4
        vsetvli           zero, zero, e16, m4, ta, ma
        vnsrl.wx          v16, v16, t6
        vmax.vx           v16, v16, zero
        vsetvli           zero, zero, e8, m2, ta, ma
        vnclipu.wi        v16, v16, 0
        vse8.v            v16, (a0)
        sh1add            a2, t0, a2
        sh1add            a3, t0, a3
        add               a0, a0, t0
        bnez              a4, 1b
        add               a0, t1, a1
        addi              a2, t2, 128*2
        addi              a3, t5, 128*2
        mv                a4, a6
.endif
        bnez              a5, \id\w\vlen\()b
        ret
.endm

.macro func_w_avg vlen
func ff_vvc_w_avg_8_rvv_\vlen\(), zve32x, zbb, zba
        lpad    0
        POW2_JMP_TABLE    2, \vlen
        csrwi             vxrm, 0
        addi              t6, a6, 7
        ld                t3, (sp)
        ld                t4, 8(sp)
        ld                t5, 16(sp)
        addi              t4, t4, 1       // o0 + o1 + 1
        add               t4, t4, t5
        addi              t5, t6, -1      // shift - 1
        sll               t4, t4, t5
        POW2_J            \vlen, 2, a4
        .irp w,2,4,8,16,32,64,128
        w_avg             \w, \vlen, 2
        .endr
endfunc
.endm

func_w_avg 128
func_w_avg 256
#endif

func dmvr zve32x, zbb, zba
        lpad    0
        li                t0, 4
1:
        add               t1, a1, a2
        addi              t4, a0, 128*2
        vle8.v            v0, (a1)
        vle8.v            v4, (t1)
        addi              a3, a3, -2
        vwmulu.vx         v16, v0, t0
        vwmulu.vx         v20, v4, t0
        vse16.v           v16, (a0)
        vse16.v           v20, (t4)
        sh1add            a1, a2, a1
        add               a0, a0, 128*2*2
        bnez              a3, 1b
        ret
endfunc

.macro dmvr_h_v mn, type, w, vlen
func dmvr_\type\vlen\w, zve32x, zbb, zba
        lla               t4, ff_vvc_inter_luma_dmvr_filters
        sh1add            t4, \mn, t4
        lbu               t5, (t4)
        lbu               t6, 1(t4)
1:
        vsetvlstatic8     \w, \vlen
.ifc \type,h
        addi              t0, a1, 1
        addi              t1, a1, 2
.else
        add               t0, a1, a2
        add               t1, t0, a2
.endif
        vle8.v            v0, (a1)
        vle8.v            v4, (t0)
        vle8.v            v8, (t1)
        addi              a3, a3, -2
        addi              t2, a0, 128*2
        vwmulu.vx         v12, v0, t5
        vwmulu.vx         v24, v4, t5
        vwmaccu.vx        v12, t6, v4
        vwmaccu.vx        v24, t6, v8
        vsetvlstatic16    \w, \vlen
        vssrl.vi          v12, v12, 2
        vssrl.vi          v24, v24, 2
        vse16.v           v12, (a0)
        vse16.v           v24, (t2)
        add               a0, a0, 128*4
        sh1add            a1, a2, a1
        bnez              a3, 1b
        ret
endfunc
.endm

.macro dmvr_load_h dst, filter0, filter1, w, vlen
        vsetvlstatic8     \w, \vlen
        addi              a6, a1, 1
        vle8.v            \dst, (a1)
        vle8.v            v2, (a6)
        vwmulu.vx         v4, \dst, \filter0
        vwmaccu.vx        v4, \filter1, v2
        vsetvlstatic16    \w, \vlen
        vssrl.vi          \dst, v4, 2
.endm

.macro dmvr_hv w, vlen
func dmvr_hv\vlen\w, zve32x, zbb, zba
        lla               t0, ff_vvc_inter_luma_dmvr_filters
        sh1add            t1, a4, t0
        sh1add            t2, a5, t0
        lbu               t3, (t1)          // filter[mx][0]
        lbu               t4, 1(t1)         // filter[mx][1]
        lbu               t5, (t2)          // filter[my][0]
        lbu               t6, 1(t2)         // filter[my][1]
        dmvr_load_h       v12, t3, t4, \w, \vlen
        add               a1, a1, a2
1:
        vmul.vx           v28, v12, t5
        addi              a3, a3, -1
        dmvr_load_h       v12, t3, t4, \w, \vlen
        vmacc.vx          v28, t6, v12
        vssrl.vi          v28, v28, 4
        vse16.v           v28, (a0)
        add               a1, a1, a2
        addi              a0, a0, 128*2
        bnez              a3, 1b
        ret
endfunc
.endm

.macro func_dmvr vlen, name
func ff_vvc_\name\()_8_rvv_\vlen\(), zve32x, zbb, zba
        lpad    0
        li                t0, 20
        beq               a6, t0, DMVR\name\vlen\()20
        .irp w,12,20
DMVR\name\vlen\w:
        .ifc \name, dmvr
        vsetvlstatic8     \w, \vlen
        j                 \name
        .else
        csrwi             vxrm, 0
        j                 \name\()\vlen\w
        .endif
        .endr
endfunc
.endm


.irp vlen,256,128
.irp w,12,20
dmvr_h_v a4, h, \w, \vlen
dmvr_h_v a5, v, \w, \vlen
dmvr_hv \w, \vlen
.endr
func_dmvr \vlen, dmvr
func_dmvr \vlen, dmvr_h
func_dmvr \vlen, dmvr_v
func_dmvr \vlen, dmvr_hv
.endr

func_put_pixels 256, 128, vvc
func_put_pixels 128, 128, vvc
