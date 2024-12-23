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

.macro vsetvlstatic w, vlen, en, mn1, mn2, mn3, mn4, mn5, mn6, vta=ta
        .if \w == 2 && \vlen == 128
                vsetivli        zero, \w, \en, \mn1, \vta, ma
        .elseif \w <= 4 && \vlen == 128
                vsetivli        zero, \w, \en, \mn2, \vta, ma
        .elseif \w <= 8 && \vlen == 128
                vsetivli        zero, \w, \en, \mn3, \vta, ma
        .elseif \w <= 16 && \vlen == 128
                vsetivli        zero, \w, \en, \mn4, \vta, ma
        .elseif \w <= 32 && \vlen == 128
                li              t0, \w
                vsetvli         zero, t0, \en, \mn5, \vta, ma
        .elseif \w <= 4 && \vlen == 256
                vsetivli        zero, \w, \en, \mn1, \vta, ma
        .elseif \w <= 8 && \vlen == 256
                vsetivli        zero, \w, \en, \mn2, \vta, ma
        .elseif \w <= 16 && \vlen == 256
                vsetivli        zero, \w, \en, \mn3, \vta, ma
        .elseif \w <= 32 && \vlen == 256
                li              t0, \w
                vsetvli         zero, t0, \en, \mn4, \vta, ma
        .elseif \w <= 64 && \vlen == 256
                li              t0, \w
                vsetvli         zero, t0, \en, \mn5, \vta, ma
        .else
                li              t0, \w
                vsetvli         zero, t0, \en, \mn6, \vta, ma
        .endif
.endm

.macro vsetvlstatic8 w, vlen, vta
        vsetvlstatic \w, \vlen, e8, mf8, mf4, mf2, m1, m2, m4, \vta
.endm

.macro vsetvlstatic16 w, vlen, vta
        vsetvlstatic \w, \vlen, e16, mf4, mf2, m1, m2, m4, m8, \vta
.endm

.macro vsetvlstatic32 w, vlen, vta
        vsetvlstatic \w, \vlen, e32, mf2, m1, m2, m4, m8, m8, \vta
.endm

.macro POW2_JMP_TABLE id, vlen
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

.macro POW2_J vlen, id, w
        clz               t1, \w
        neg               t1, t1
        lla               t5, jmp_table_\id\vlen
        sh2add            t1, t1, t5
        lw                t1, ((__riscv_xlen-2)<<2)(t1)
        add               t1, t1, t5
        jr                t1
.endm

.macro put_pixels w, vlen, id, MAX_PB_SIZE
\id\w\vlen:
        vsetvlstatic8     \w, \vlen
        li                t2, 1<<6
.if \w == 128 && \vlen == 128
1:
        addi              t0, a1, 64
        addi              t1, a0, 64*2
        vle8.v            v0, (a1)
        vle8.v            v16, (t0)
        vwmulu.vx         v8, v0, t2
        vwmulu.vx         v24, v16, t2
        vse16.v           v8, (a0)
        vse16.v           v24, (t1)
        add               a1, a1, a2
        addi              a3, a3, -1
        addi              a0, a0, 128*2
        bnez              a3, 1b
.else
1:
        vle8.v            v0, (a1)
        vwmulu.vx         v8, v0, t2
        vse16.v           v8, (a0)
        add               a1, a1, a2
        addi              a3, a3, -1
        addi              a0, a0, \MAX_PB_SIZE<<1
        bnez              a3, 1b
.endif
        ret
.endm

.macro func_put_pixels vlen, MAX_PB_SIZE, name
func ff_\name\()_put_pixels_8_rvv_\vlen\(), zve32x, zbb, zba
        lpad    0
        POW2_JMP_TABLE    3, \vlen
        POW2_J            \vlen, 3, a6
        .irp w,2,4,8,16,32,64,128
        put_pixels        \w, \vlen, 3, \MAX_PB_SIZE
        .endr
endfunc
.endm
