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

.macro vsetvlstatic8 w, vlen
        .if \w == 2 && \vlen == 128
                vsetivli        zero, \w, e8, mf8, ta, ma
        .elseif \w == 4 && \vlen == 128
                vsetivli        zero, \w, e8, mf4, ta, ma
        .elseif \w == 8 && \vlen == 128
                vsetivli        zero, \w, e8, mf2, ta, ma
        .elseif \w == 16 && \vlen == 128
                vsetivli        zero, \w, e8, m1, ta, ma
        .elseif \w == 32 && \vlen == 128
                li              t0, \w
                vsetvli         zero, t0, e8, m2, ta, ma
        .elseif \w <= 4 && \vlen == 256
                vsetivli        zero, \w, e8, mf8, ta, ma
        .elseif \w == 8 && \vlen == 256
                vsetivli        zero, \w, e8, mf4, ta, ma
        .elseif \w == 16 && \vlen == 256
                vsetivli        zero, \w, e8, mf2, ta, ma
        .elseif \w == 32 && \vlen == 256
                li              t0, \w
                vsetvli         zero, t0, e8, m1, ta, ma
        .elseif \w == 64 && \vlen == 256
                li              t0, \w
                vsetvli         zero, t0, e8, m2, ta, ma
        .else
                li              t0, \w
                vsetvli         zero, t0, e8, m4, ta, ma
        .endif
.endm

.macro vsetvlstatic16 w, vlen
        .if \w == 2 && \vlen == 128
                vsetivli        zero, \w, e16, mf4, ta, ma
        .elseif \w == 4 && \vlen == 128
                vsetivli        zero, \w, e16, mf2, ta, ma
        .elseif \w == 8 && \vlen == 128
                vsetivli        zero, \w, e16, m1, ta, ma
        .elseif \w == 16 && \vlen == 128
                vsetivli        zero, \w, e16, m2, ta, ma
        .elseif \w == 32 && \vlen == 128
                li              t0, \w
                vsetvli         zero, t0, e16, m4, ta, ma
        .elseif \w <= 4 && \vlen == 256
                vsetivli        zero, \w, e16, mf4, ta, ma
        .elseif \w == 8 && \vlen == 256
                vsetivli        zero, \w, e16, mf2, ta, ma
        .elseif \w == 16 && \vlen == 256
                vsetivli        zero, \w, e16, m1, ta, ma
        .elseif \w == 32 && \vlen == 256
                li              t0, \w
                vsetvli         zero, t0, e16, m2, ta, ma
        .elseif \w == 64 && \vlen == 256
                li              t0, \w
                vsetvli         zero, t0, e16, m4, ta, ma
        .else
                li              t0, \w
                vsetvli         zero, t0, e16, m8, ta, ma
        .endif
.endm

.macro vsetvlstatic32 w, vlen
        .if \w == 2
                vsetivli        zero, \w, e32, mf2, ta, ma
        .elseif \w == 4 && \vlen == 128
                vsetivli        zero, \w, e32, m1, ta, ma
        .elseif \w == 8 && \vlen == 128
                vsetivli        zero, \w, e32, m2, ta, ma
        .elseif \w == 16 && \vlen == 128
                vsetivli        zero, \w, e32, m4, ta, ma
        .elseif \w == 4 && \vlen == 256
                vsetivli        zero, \w, e32, mf2, ta, ma
        .elseif \w == 8 && \vlen == 256
                vsetivli        zero, \w, e32, m1, ta, ma
        .elseif \w == 16 && \vlen == 256
                vsetivli        zero, \w, e32, m2, ta, ma
        .elseif \w == 32 && \vlen == 256
                li              t0, \w
                vsetvli         zero, t0, e32, m4, ta, ma
        .else
                li              t0, \w
                vsetvli         zero, t0, e32, m8, ta, ma
        .endif
.endm

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


.macro AVG_JMP_TABLE id, vlen
const jmp_table_\id\vlen
        .4byte \id\()2\vlen\()f - jmp_table_\id\vlen
        .4byte \id\()4\vlen\()f - jmp_table_\id\vlen
        .4byte \id\()8\vlen\()f - jmp_table_\id\vlen
        .4byte \id\()16\vlen\()f - jmp_table_\id\vlen
        .4byte \id\()32\vlen\()f - jmp_table_\id\vlen
        .4byte \id\()64\vlen\()f - jmp_table_\id\vlen
        .4byte \id\()128\vlen\()f - jmp_table_\id\vlen
endconst
.endm

.macro AVG_J vlen, id
        clz               t1, a4
        neg               t1, t1
        lla               t5, jmp_table_\id\vlen
        sh2add            t1, t1, t5
        lw                t1, ((__riscv_xlen-2)<<2)(t1)
        add               t1, t1, t5
        jr                t1
.endm

.macro func_avg vlen
func ff_vvc_avg_8_rvv_\vlen\(), zve32x, zbb, zba
        lpad    0
        AVG_JMP_TABLE     1, \vlen
        csrwi             vxrm, 0
        AVG_J             \vlen, 1
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
        AVG_JMP_TABLE     2, \vlen
        csrwi             vxrm, 0
        addi              t6, a6, 7
        ld                t3, (sp)
        ld                t4, 8(sp)
        ld                t5, 16(sp)
        addi              t4, t4, 1       // o0 + o1 + 1
        add               t4, t4, t5
        addi              t5, t6, -1      // shift - 1
        sll               t4, t4, t5
        AVG_J             \vlen, 2
        .irp w,2,4,8,16,32,64,128
        w_avg             \w, \vlen, 2
        .endr
endfunc
.endm

func_w_avg 128
func_w_avg 256
#endif
