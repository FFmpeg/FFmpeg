/*
 * Copyright © 2024 Rémi Denis-Courmont.
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

func ff_bgr24ToY_rvv, zve32x
        lpad    0
        lw      t1, 8(a5) # BY
        lw      t3, 0(a5) # RY
        j       1f
endfunc

func ff_rgb24ToY_rvv, zve32x, zba
        lpad    0
        lw      t1, 0(a5) # RY
        lw      t3, 8(a5) # BY
1:
        lw      t2, 4(a5) # GY
        li      t4, (32 << (15 - 1)) + (1 << (15 - 7))
2:
        vsetvli    t0, a4, e32, m8, ta, ma
        vlseg3e8.v v0, (a1)
        sub        a4, a4, t0
        vzext.vf4  v8, v0
        sh1add     t5, t0, t0 # t1 = 3 * t0
        vzext.vf4  v16, v2
        vzext.vf4  v24, v4
        add        a1, t5, a1
        vmul.vx    v8, v8, t1
        vmacc.vx   v8, t2, v16
        vmacc.vx   v8, t3, v24
        vadd.vx    v8, v8, t4
        vsetvli    zero, zero, e16, m4, ta, ma
        vnsra.wi   v0, v8, 15 - 6
        vse16.v    v0, (a0)
        sh1add     a0, t0, a0
        bnez       a4, 2b

        ret
endfunc

func ff_bgr24ToUV_rvv, zve32x
        lpad    0
        lw      t1, 20(a6) # BU
        lw      t4, 32(a6) # BV
        lw      t3, 12(a6) # RU
        lw      t6, 24(a6) # RV
        j       1f
endfunc

func ff_rgb24ToUV_rvv, zve32x, zba
        lpad    0
        lw      t1, 12(a6) # RU
        lw      t4, 24(a6) # RV
        lw      t3, 20(a6) # BU
        lw      t6, 32(a6) # BV
1:
        lw      t2, 16(a6) # GU
        lw      t5, 28(a6) # GV
        li      a7, (256 << (15 - 1)) + (1 << (15 - 7))
2:
        vsetvli    t0, a5, e32, m8, ta, ma
        vlseg3e8.v v0, (a3)
        sub        a5, a5, t0
        vzext.vf4  v16, v0
        sh1add     a6, t0, t0
        vzext.vf4  v24, v2
        vmul.vx    v8, v16, t1
        add        a3, a6, a3
        vmul.vx    v16, v16, t4
        vmacc.vx   v8, t2, v24
        vmacc.vx   v16, t5, v24
        vzext.vf4  v24, v4
        vadd.vx    v8, v8, a7
        vadd.vx    v16, v16, a7
        vmacc.vx   v8, t3, v24
        vmacc.vx   v16, t6, v24
        vsetvli    zero, zero, e16, m4, ta, ma
        vnsra.wi   v0, v8, 15 - 6
        vnsra.wi   v4, v16, 15 - 6
        vse16.v    v0, (a0)
        sh1add     a0, t0, a0
        vse16.v    v4, (a1)
        sh1add     a1, t0, a1
        bnez       a5, 2b

        ret
endfunc

func ff_bgr24ToUV_half_rvv, zve32x
        lpad    0
        lw      t1, 20(a6) # BU
        lw      t4, 32(a6) # BV
        lw      t3, 12(a6) # RU
        lw      t6, 24(a6) # RV
        j       1f
endfunc

func ff_rgb24ToUV_half_rvv, zve32x, zba
        lpad    0
        lw      t1, 12(a6) # RU
        lw      t4, 24(a6) # RV
        lw      t3, 20(a6) # BU
        lw      t6, 32(a6) # BV
1:
        lw      t2, 16(a6) # GU
        lw      t5, 28(a6) # GV
        li      a7, (256 << 15) + (1 << (15 - 6))
2:
        vsetvli    t0, a5, e8, m1, ta, ma
        vlseg6e8.v v0, (a3)
        sh1add     a6, t0, t0
        vwaddu.vv  v8, v0, v3
        sub        a5, a5, t0
        vwaddu.vv  v10, v1, v4
        sh1add     a3, a6, a3
        vwaddu.vv  v12, v2, v5
        vsetvli    zero, zero, e32, m4, ta, ma
        vzext.vf2  v20, v8
        vzext.vf2  v24, v10
        vzext.vf2  v28, v12
        vmul.vx    v0, v20, t1
        vmul.vx    v4, v20, t4
        vmacc.vx   v0, t2, v24
        vmacc.vx   v4, t5, v24
        vmacc.vx   v0, t3, v28
        vmacc.vx   v4, t6, v28
        vadd.vx    v0, v0, a7
        vadd.vx    v4, v4, a7
        vsetvli    zero, zero, e16, m2, ta, ma
        vnsra.wi   v0, v0, 15 - 5
        vnsra.wi   v2, v4, 15 - 5
        vse16.v    v0, (a0)
        sh1add     a0, t0, a0
        vse16.v    v2, (a1)
        sh1add     a1, t0, a1
        bnez       a5, 2b

        ret
endfunc

        .macro  rgba_input chr0, chr1, high
func ff_\chr1\()ToY_rvv, zve32x
        lpad    0
        lw      t1, 8(a5) # BY
        lw      t3, 0(a5) # RY
        j       1f
endfunc

func ff_\chr0\()ToY_rvv, zve32x, zba
        lpad    0
        lw      t1, 0(a5) # RY
        lw      t3, 8(a5) # BY
1:
        lw      t2, 4(a5) # GY
        li      t4, (32 << (15 - 1)) + (1 << (15 - 7))
        li      t5, 0xff
2:
        vsetvli   t0, a4, e32, m8, ta, ma
        vle32.v   v0, (a1)
        sub       a4, a4, t0
        .if     \high
        vsrl.vi   v8, v0, 24
        .else
        vand.vx   v8, v0, t5
        .endif
        sh2add    a1, t0, a1
        vsrl.vi   v16, v0, 8 * (1 + \high)
        vmul.vx   v24, v8, t1
        vand.vx   v16, v16, t5
        vsrl.vi   v8, v0, 8 * (2 - \high)
        vmacc.vx  v24, t2, v16
        vand.vx   v8, v8, t5
        vadd.vx   v24, v24, t4
        vmacc.vx  v24, t3, v8
        vsetvli   zero, zero, e16, m4, ta, ma
        vnsra.wi  v0, v24, 15 - 6
        vse16.v   v0, (a0)
        sh1add    a0, t0, a0
        bnez      a4, 2b

        ret
endfunc

func ff_\chr1\()ToUV_rvv, zve32x
        lpad    0
        lw      t1, 20(a6) # BU
        lw      t4, 32(a6) # BV
        lw      t3, 12(a6) # RU
        lw      t6, 24(a6) # RV
        j       1f
endfunc

func ff_\chr0\()ToUV_rvv, zve32x, zba
        lpad    0
        lw      t1, 12(a6) # RU
        lw      t4, 24(a6) # RV
        lw      t3, 20(a6) # BU
        lw      t6, 32(a6) # BV
1:
        lw      t2, 16(a6) # GU
        lw      t5, 28(a6) # GV
        li      a6, 0xff
        li      a7, (256 << (15 - 1)) + (1 << (15 - 7))
2:
        vsetvli   t0, a5, e32, m8, ta, ma
        vle32.v   v0, (a3)
        sub       a5, a5, t0
        .if     \high
        vsrl.vi   v24, v0, 24
        .else
        vand.vx   v24, v0, a6
        .endif
        sh2add    a3, t0, a3
        vsrl.vi   v8, v0, 8 * (1 + \high)
        vmul.vx   v16, v24, t1
        vand.vx   v8, v8, a6
        vmul.vx   v24, v24, t4
        vmacc.vx  v16, t2, v8
        vsrl.vi   v0, v0, 8 * (2 - \high)
        vmacc.vx  v24, t5, v8
        vand.vx   v0, v0, a6
        vadd.vx   v16, v16, a7
        vadd.vx   v24, v24, a7
        vmacc.vx  v16, t3, v0
        vmacc.vx  v24, t6, v0
        vsetvli   zero, zero, e16, m4, ta, ma
        vnsra.wi  v0, v16, 15 - 6
        vnsra.wi  v4, v24, 15 - 6
        vse16.v   v0, (a0)
        sh1add    a0, t0, a0
        vse16.v   v4, (a1)
        sh1add    a1, t0, a1
        bnez      a5, 2b

        ret
endfunc

func ff_\chr1\()ToUV_half_rvv, zve32x
        lpad    0
        lw      t1, 20(a6) # BU
        lw      t4, 32(a6) # BV
        lw      t3, 12(a6) # RU
        lw      t6, 24(a6) # RV
        j       1f
endfunc

func ff_\chr0\()ToUV_half_rvv, zve32x, zba
        lpad    0
        lw      t1, 12(a6) # RU
        lw      t4, 24(a6) # RV
        lw      t3, 20(a6) # BU
        lw      t6, 32(a6) # BV
1:
        lw      t2, 16(a6) # GU
        lw      t5, 28(a6) # GV
        li      a6, 0xff
        li      a7, (256 << 15) + (1 << (15 - 6))
2:
        vsetvli     t0, a5, e32, m4, ta, ma
        vlseg2e32.v v0, (a3)
        sub         a5, a5, t0
        .if     \high
        vsrl.vi     v8, v0, 24
        vsrl.vi     v12, v4, 24
        .else
        vand.vx     v8, v0, a6
        vand.vx     v12, v4, a6
        .endif
        sh3add      a3, t0, a3
        vsrl.vi     v16, v0, 8 * (1 + \high)
        vsrl.vi     v20, v4, 8 * (1 + \high)
        vsrl.vi     v24, v0, 8 * (2 - \high)
        vsrl.vi     v28, v4, 8 * (2 - \high)
        vand.vx     v16, v16, a6
        vand.vx     v20, v20, a6
        vand.vx     v24, v24, a6
        vand.vx     v28, v28, a6
        vadd.vv     v8, v8, v12
        vadd.vv     v16, v16, v20
        vadd.vv     v24, v24, v28
        vmul.vx     v0, v8, t1
        vmul.vx     v4, v8, t4
        vmacc.vx    v0, t2, v16
        vmacc.vx    v4, t5, v16
        vmacc.vx    v0, t3, v24
        vmacc.vx    v4, t6, v24
        vadd.vx     v0, v0, a7
        vadd.vx     v4, v4, a7
        vsetvli     zero, zero, e16, m2, ta, ma
        vnsra.wi    v0, v0, 15 - 5
        vnsra.wi    v2, v4, 15 - 5
        vse16.v     v0, (a0)
        sh1add      a0, t0, a0
        vse16.v     v2, (a1)
        sh1add      a1, t0, a1
        bnez        a5, 2b

        ret
endfunc
        .endm

rgba_input rgba32, bgra32, 0
rgba_input abgr32, argb32, 1
