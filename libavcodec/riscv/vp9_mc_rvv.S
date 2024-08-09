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

.macro bilin_load dst, mn, type
.ifc \type,v
        add             t4, a2, a3
.else
        addi            t4, a2, 1
.endif
        vle8.v          v4, (a2)
        vle8.v          v0, (t4)
        vwmulu.vx       v16, v0, \mn
        vwmaccsu.vx     v16, t1, v4
        vwadd.wx        v16, v16, t3
        vnsra.wi        v16, v16, 4
        vadd.vv         \dst, v16, v4
        add             a2, a2, a3
.endm

.macro bilin_h_v op, type, mn
func ff_\op\()_vp9_bilin_64\type\()_rvv, zve32x
        lpad    0
        vsetvlstatic8   64, t0, 64
.ifc \op,avg
        csrwi           vxrm, 0
.endif
        li              t3, 8
        neg             t1, \mn
1:
        addi            a4, a4, -1
        bilin_load      v0, \mn, \type
.ifc \op,avg
        vle8.v          v16, (a0)
        vaaddu.vv       v0, v0, v16
.endif
        vse8.v          v0, (a0)
        add             a0, a0, a1
        bnez            a4, 1b
        ret

.Lbilin_\type\op:
.ifc \op,avg
        csrwi           vxrm, 0
.endif
        li              t4, 8
        neg             t1, \mn
1:
        addi            a4, a4, -2
        add             t6, a0, a1
        add             t0, a2, a3
        vle8.v          v8, (a2)
        vle8.v          v4, (t0)
.ifc \type,v
        add             t2, t0, a3
        vwmulu.vx       v16, v4, \mn
.else
        addi            t3, a2, 1
        addi            t2, t0, 1
        vle8.v          v0, (t3)
        vwmulu.vx       v16, v0, \mn
.endif
        vle8.v          v12, (t2)
        vwmulu.vx       v20, v12, \mn
        vwmaccsu.vx     v16, t1, v8
        vwmaccsu.vx     v20, t1, v4
        vwadd.wx        v16, v16, t4
        vwadd.wx        v20, v20, t4
        vnsra.wi        v16, v16, 4
        vnsra.wi        v20, v20, 4
        vadd.vv         v0, v16, v8
        vadd.vv         v12, v20, v4
.ifc \op,avg
        vle8.v          v16, (a0)
        vle8.v          v20, (t6)
        vaaddu.vv       v0, v0, v16
        vaaddu.vv       v12, v12, v20
.endif
        vse8.v          v0, (a0)
        vse8.v          v12, (t6)
        add             a2, t0, a3
        add             a0, t6, a1
        bnez            a4, 1b

        ret
endfunc
.endm

.macro bilin_hv op
func ff_\op\()_vp9_bilin_64hv_rvv, zve32x
        lpad    0
        vsetvlstatic8   64, t0, 64
.Lbilin_hv\op:
.ifc \op,avg
        csrwi           vxrm, 0
.endif
        neg             t1, a5
        neg             t2, a6
        li              t3, 8
        bilin_load      v24, a5, h
1:
        addi            a4, a4, -2
        bilin_load      v8, a5, h
        vwmulu.vx       v16, v8, a6
        vwmaccsu.vx     v16, t2, v24
        vwadd.wx        v16, v16, t3
        vnsra.wi        v16, v16, 4
        vadd.vv         v12, v16, v24
        add             t5, a0, a1
        bilin_load      v24, a5, h
        vwmulu.vx       v16, v24, a6
        vwmaccsu.vx     v16, t2, v8
        vwadd.wx        v16, v16, t3
        vnsra.wi        v16, v16, 4
        vadd.vv         v0, v16, v8
.ifc \op,avg
        vle8.v          v8, (a0)
        vle8.v          v16, (t5)
        vaaddu.vv       v12, v12, v8
        vaaddu.vv       v0, v0, v16
.endif
        vse8.v          v12, (a0)
        vse8.v          v0, (t5)
        add             a0, t5, a1
        bnez            a4, 1b

        ret
endfunc
.endm

.irp len, 64, 32, 16, 8, 4
        copy_avg \len
.endr

bilin_h_v  put, h, a5
bilin_h_v  avg, h, a5
bilin_h_v  put, v, a6
bilin_h_v  avg, v, a6
bilin_hv   put
bilin_hv   avg

.macro func_bilin_h_v len, op, type
func ff_\op\()_vp9_bilin_\len\()\type\()_rvv, zve32x
        lpad    0
        vsetvlstatic8   \len, t0, 64
        j               .Lbilin_\type\()\op
endfunc
.endm

.irp len, 32, 16, 8, 4
        .irp op, put, avg
                .irp type, h, v, hv
                        func_bilin_h_v \len, \op, \type
                .endr
        .endr
.endr
