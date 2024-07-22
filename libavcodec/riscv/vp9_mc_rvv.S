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

.macro vsetvlstatic8 len an maxlen mn=m4
.if \len == 4
        vsetivli        zero, \len, e8, mf4, ta, ma
.elseif \len == 8
        vsetivli        zero, \len, e8, mf2, ta, ma
.elseif \len == 16
        vsetivli        zero, \len, e8, m1, ta, ma
.elseif \len == 32
        li              \an, \len
        vsetvli         zero, \an, e8, m2, ta, ma
.elseif \len == 64
        li              \an, \maxlen
        vsetvli         zero, \an, e8, \mn, ta, ma
.endif
.endm

.macro copy_avg len
func ff_vp9_avg\len\()_rvv, zve32x
        lpad    0
        csrwi           vxrm, 0
        vsetvlstatic8   \len, t0, 64
1:
        vle8.v          v8, (a2)
        vle8.v          v16, (a0)
        vaaddu.vv       v8, v8, v16
        addi            a4, a4, -1
        vse8.v          v8, (a0)
        add             a2, a2, a3
        add             a0, a0, a1
        bnez            a4, 1b
        ret
endfunc
.endm

.irp len, 64, 32, 16, 8, 4
        copy_avg \len
.endr
