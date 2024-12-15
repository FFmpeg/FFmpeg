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
        .elseif \w <= 4 && \vlen == 128
                vsetivli        zero, \w, e8, mf4, ta, ma
        .elseif \w <= 8 && \vlen == 128
                vsetivli        zero, \w, e8, mf2, ta, ma
        .elseif \w <= 16 && \vlen == 128
                vsetivli        zero, \w, e8, m1, ta, ma
        .elseif \w <= 32 && \vlen == 128
                li              t0, \w
                vsetvli         zero, t0, e8, m2, ta, ma
        .elseif \w <= 4 && \vlen == 256
                vsetivli        zero, \w, e8, mf8, ta, ma
        .elseif \w <= 8 && \vlen == 256
                vsetivli        zero, \w, e8, mf4, ta, ma
        .elseif \w <= 16 && \vlen == 256
                vsetivli        zero, \w, e8, mf2, ta, ma
        .elseif \w <= 32 && \vlen == 256
                li              t0, \w
                vsetvli         zero, t0, e8, m1, ta, ma
        .elseif \w <= 64 && \vlen == 256
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
        .elseif \w <= 4 && \vlen == 128
                vsetivli        zero, \w, e16, mf2, ta, ma
        .elseif \w <= 8 && \vlen == 128
                vsetivli        zero, \w, e16, m1, ta, ma
        .elseif \w <= 16 && \vlen == 128
                vsetivli        zero, \w, e16, m2, ta, ma
        .elseif \w <= 32 && \vlen == 128
                li              t0, \w
                vsetvli         zero, t0, e16, m4, ta, ma
        .elseif \w <= 4 && \vlen == 256
                vsetivli        zero, \w, e16, mf4, ta, ma
        .elseif \w <= 8 && \vlen == 256
                vsetivli        zero, \w, e16, mf2, ta, ma
        .elseif \w <= 16 && \vlen == 256
                vsetivli        zero, \w, e16, m1, ta, ma
        .elseif \w <= 32 && \vlen == 256
                li              t0, \w
                vsetvli         zero, t0, e16, m2, ta, ma
        .elseif \w <= 64 && \vlen == 256
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
        .elseif \w <= 4 && \vlen == 128
                vsetivli        zero, \w, e32, m1, ta, ma
        .elseif \w <= 8 && \vlen == 128
                vsetivli        zero, \w, e32, m2, ta, ma
        .elseif \w <= 16 && \vlen == 128
                vsetivli        zero, \w, e32, m4, ta, ma
        .elseif \w <= 4 && \vlen == 256
                vsetivli        zero, \w, e32, mf2, ta, ma
        .elseif \w <= 8 && \vlen == 256
                vsetivli        zero, \w, e32, m1, ta, ma
        .elseif \w <= 16 && \vlen == 256
                vsetivli        zero, \w, e32, m2, ta, ma
        .elseif \w <= 32 && \vlen == 256
                li              t0, \w
                vsetvli         zero, t0, e32, m4, ta, ma
        .else
                li              t0, \w
                vsetvli         zero, t0, e32, m8, ta, ma
        .endif
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
