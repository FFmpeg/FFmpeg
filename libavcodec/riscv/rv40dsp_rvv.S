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

const rv40_bias
    .byte  0, 16, 32, 16
    .byte 32, 28, 32, 28
    .byte  0, 32, 16, 32
    .byte 32, 28, 32, 28
endconst

.macro  do_chroma_mc type unroll
        csrwi           vxrm, 0
        addi            sp, sp, -16
#if __riscv_xlen == 32
        sw              s2, (sp)
#elif __riscv_xlen == 64
        sd              s2, (sp)
#else
        sq              s2, (sp)
#endif
        lla             t4, rv40_bias
        srli            t5, a5, 1
        sh2add          t4, t5, t4
        srli            t5, a4, 1
        add             t5, t4, t5
        slli            t2, a5, 3
        lb              s2, (t5)
        mul             t1, a5, a4
        sh3add          a5, a4, t2
        slli            a4, a4, 3
        sub             a5, t1, a5
        sub             a7, a4, t1
        addi            a6, a5, 64
        sub             t0, t2, t1
        vsetvli         t3, t6, e8, m1, ta, mu
        beqz            t1, 2f
        blez            a3, 8f
        li              t4, 0
        li              t2, 0
        li              t5, 1
        addi            a5, t3, 1
        slli            t3, a2, (1 + \unroll)
1:                                # if (xy != 0)
        add             a4, a1, t4
        vsetvli         zero, a5, e8, m1, ta, ma
  .ifc \unroll,1
        addi            t2, t2, 4
  .else
        addi            t2, t2, 2
  .endif
        vle8.v          v10, (a4)
        add             a4, a4, a2
        vslide1down.vx  v11, v10, t5
        vsetvli         zero, t6, e8, m1, ta, ma
        vwmulu.vx       v8, v10, a6
        vwmaccu.vx      v8, a7, v11
        vsetvli         zero, a5, e8, m1, ta, ma
        vle8.v          v12, (a4)
        vsetvli         zero, t6, e8, m1, ta, ma
        add             a4, a4, a2
        vwmaccu.vx      v8, t0, v12
        vsetvli         zero, a5, e8, m1, ta, ma
        vslide1down.vx  v13, v12, t5
        vsetvli         zero, t6, e8, m1, ta, ma
        vwmulu.vx       v10, v12, a6
        vwmaccu.vx      v8, t1, v13
        vwmaccu.vx      v10, a7, v13
        vsetvli         zero, a5, e8, m1, ta, ma
        vle8.v          v14, (a4)
        vsetvli         zero, t6, e8, m1, ta, ma
        add             a4, a4, a2
        vwmaccu.vx      v10, t0, v14
        vsetvli         zero, a5, e8, m1, ta, ma
        vslide1down.vx  v15, v14, t5
        vsetvli         zero, t6, e8, m1, ta, ma
        vwmulu.vx       v12, v14, a6
        vwmaccu.vx      v10, t1, v15
        vwmaccu.vx      v12, a7, v15
        vwaddu.wx       v20, v8, s2
        vnsrl.wi        v15, v20, 6
  .ifc \type,avg
        vle8.v          v9, (a0)
        vaaddu.vv       v15, v15, v9
  .endif
        vse8.v          v15, (a0)
        add             a0, a0, a2
        vwaddu.wx       v20, v10, s2
        vnsrl.wi        v8, v20, 6
  .ifc \type,avg
        vle8.v          v9, (a0)
        vaaddu.vv       v8, v8, v9
  .endif
        add             t4, t4, t3
        vse8.v          v8, (a0)
        add             a0, a0, a2
  .ifc \unroll,1
        vsetvli         zero, a5, e8, m1, ta, ma
        vle8.v          v14, (a4)
        vsetvli         zero, t6, e8, m1, ta, ma
        add             a4, a4, a2
        vwmaccu.vx      v12, t0, v14
        vsetvli         zero, a5, e8, m1, ta, ma
        vslide1down.vx  v15, v14, t5
        vsetvli         zero, t6, e8, m1, ta, ma
        vwmulu.vx       v16, v14, a6
        vwmaccu.vx      v12, t1, v15
        vwmaccu.vx      v16, a7, v15
        vsetvli         zero, a5, e8, m1, ta, ma
        vle8.v          v14, (a4)
        vsetvli         zero, t6, e8, m1, ta, ma
        vwmaccu.vx      v16, t0, v14
        vsetvli         zero, a5, e8, m1, ta, ma
        vslide1down.vx  v14, v14, t5
        vsetvli         zero, t6, e8, m1, ta, ma
        vwmaccu.vx      v16, t1, v14
        vwaddu.wx       v20, v12, s2
        vnsrl.wi        v8, v20, 6
  .ifc \type,avg
        vle8.v          v9, (a0)
        vaaddu.vv       v8, v8, v9
  .endif
        vse8.v          v8, (a0)
        add             a0, a0, a2
        vwaddu.wx       v20, v16, s2
        vnsrl.wi        v8, v20, 6
  .ifc \type,avg
        vle8.v          v9, (a0)
        vaaddu.vv       v8, v8, v9
  .endif
        vse8.v          v8, (a0)
        add             a0, a0, a2
  .endif
        blt             t2, a3, 1b
        j               8f
2:
        bnez            a4, 4f
        beqz            t2, 4f
        blez            a3, 8f
        li              a4, 0
        li              t1, 0
        slli            a7, a2, (1 + \unroll)
3:                                # if ((x8 - xy) == 0 && (y8 -xy) != 0)
        add             a5, a1, a4
        vsetvli         zero, zero, e8, m1, ta, ma
  .ifc \unroll,1
        addi            t1, t1, 4
  .else
        addi            t1, t1, 2
  .endif
        vle8.v          v8, (a5)
        add             a5, a5, a2
        add             t2, a5, a2
        vwmulu.vx       v10, v8, a6
        vle8.v          v8, (a5)
        vwmulu.vx       v12, v8, a6
        vle8.v          v9, (t2)
        add             t2, t2, a2
        add             a5, t2, a2
        vwmaccu.vx      v10, t0, v8
        add             a4, a4, a7
        vwmaccu.vx      v12, t0, v9
        vwaddu.wx       v20, v10, s2
        vnsrl.wi        v15, v20, 6
        vwmulu.vx       v10, v9, a6
        vwaddu.wx       v20, v12, s2
        vnsrl.wi        v9, v20, 6
  .ifc \type,avg
        vle8.v          v16, (a0)
        vaaddu.vv       v15, v15, v16
  .endif
        vse8.v          v15, (a0)
        add             a0, a0, a2
  .ifc \type,avg
        vle8.v          v16, (a0)
        vaaddu.vv       v9, v9, v16
  .endif
        vse8.v          v9, (a0)
        add             a0, a0, a2
  .ifc \unroll,1
        vle8.v          v8, (t2)
        vle8.v          v14, (a5)
        vwmaccu.vx      v10, t0, v8
        vwmulu.vx       v12, v8, a6
        vwaddu.wx       v20, v10, s2
        vnsrl.wi        v8, v20, 6
        vwmaccu.vx      v12, t0, v14
  .ifc \type,avg
        vle8.v          v16, (a0)
        vaaddu.vv       v8, v8, v16
  .endif
        vse8.v          v8, (a0)
        add             a0, a0, a2
        vwaddu.wx       v20, v12, s2
        vnsrl.wi        v8, v20, 6
  .ifc \type,avg
        vle8.v          v16, (a0)
        vaaddu.vv       v8, v8, v16
  .endif
        vse8.v          v8, (a0)
        add             a0, a0, a2
  .endif
        blt             t1, a3, 3b
        j               8f
4:
        beqz            a4, 6f
        bnez            t2, 6f
        blez            a3, 8f
        li              a4, 0
        li              t2, 0
        addi            t0, t3, 1
        slli            t1, a2, (1 + \unroll)
5:                               # if ((x8 - xy) != 0 && (y8 -xy) == 0)
        add             a5, a1, a4
        vsetvli         zero, t0, e8, m1, ta, ma
  .ifc \unroll,1
        addi            t2, t2, 4
  .else
        addi            t2, t2, 2
  .endif
        vle8.v          v8, (a5)
        add             a5, a5, a2
        vslide1down.vx  v9, v8, t5
        vsetvli         zero, t6, e8, m1, ta, ma
        vwmulu.vx       v10, v8, a6
        vwmaccu.vx      v10, a7, v9
        vsetvli         zero, t0, e8, m1, ta, ma
        vle8.v          v8, (a5)
        add             a5, a5, a2
        vslide1down.vx  v9, v8, t5
        vsetvli         zero, t6, e8, m1, ta, ma
        vwmulu.vx       v12, v8, a6
        vwmaccu.vx      v12, a7, v9
        vwaddu.wx       v20, v10, s2
        vnsrl.wi        v16, v20, 6
  .ifc \type,avg
        vle8.v          v18, (a0)
        vaaddu.vv       v16, v16, v18
  .endif
        vse8.v          v16, (a0)
        add             a0, a0, a2
        vwaddu.wx       v20, v12, s2
        vnsrl.wi        v10, v20, 6
  .ifc \type,avg
        vle8.v          v18, (a0)
        vaaddu.vv       v10, v10, v18
  .endif
        add             a4, a4, t1
        vse8.v          v10, (a0)
        add             a0, a0, a2
  .ifc \unroll,1
        vsetvli         zero, t0, e8, m1, ta, ma
        vle8.v          v8, (a5)
        add             a5, a5, a2
        vslide1down.vx  v9, v8, t5
        vsetvli         zero, t6, e8, m1, ta, ma
        vwmulu.vx       v14, v8, a6
        vwmaccu.vx      v14, a7, v9
        vsetvli         zero, t0, e8, m1, ta, ma
        vle8.v          v8, (a5)
        vslide1down.vx  v9, v8, t5
        vsetvli         zero, t6, e8, m1, ta, ma
        vwmulu.vx       v12, v8, a6
        vwaddu.wx       v20, v14, s2
        vnsrl.wi        v8, v20, 6
        vwmaccu.vx      v12, a7, v9
  .ifc \type,avg
        vle8.v          v18, (a0)
        vaaddu.vv       v8, v8, v18
  .endif
        vse8.v          v8, (a0)
        add             a0, a0, a2
        vwaddu.wx       v20, v12, s2
        vnsrl.wi        v8, v20, 6
  .ifc \type,avg
        vle8.v          v18, (a0)
        vaaddu.vv       v8, v8, v18
  .endif
        vse8.v          v8, (a0)
        add             a0, a0, a2
  .endif
        blt             t2, a3, 5b
        j               8f
6:
        blez            a3, 8f
        li              a4, 0
        li              t2, 0
        slli            a7, a2, (1 + \unroll)
7:                               # the final else, none of the above conditions are met
        add             t0, a1, a4
        vsetvli         zero, zero, e8, m1, ta, ma
        add             a5, a0, a4
        add             a4, a4, a7
  .ifc \unroll,1
        addi            t2, t2, 4
  .else
        addi            t2, t2, 2
  .endif
        vle8.v          v8, (t0)
        add             t0, t0, a2
        add             t1, t0, a2
        vwmulu.vx       v10, v8, a6
        vle8.v          v8, (t0)
        add             t0, t1, a2
        vwaddu.wx       v20, v10, s2
        vnsrl.wi        v13, v20, 6
        vwmulu.vx       v10, v8, a6
  .ifc \type,avg
        vle8.v          v18, (a5)
        vaaddu.vv       v13, v13, v18
  .endif
        vse8.v          v13, (a5)
        add             a5, a5, a2
        vwaddu.wx       v20, v10, s2
        vnsrl.wi        v8, v20, 6
  .ifc \type,avg
        vle8.v          v18, (a5)
        vaaddu.vv       v8, v8, v18
  .endif
        vse8.v          v8, (a5)
        add             a5, a5, a2
  .ifc \unroll,1
        vle8.v          v9, (t1)
        vle8.v          v12, (t0)
        vwmulu.vx       v10, v9, a6
        vwaddu.wx       v20, v10, s2
        vnsrl.wi        v8, v20, 6
        vwmulu.vx       v10, v12, a6
  .ifc \type,avg
        vle8.v          v18, (a5)
        vaaddu.vv       v8, v8, v18
  .endif
        vse8.v          v8, (a5)
        add             a5, a5, a2
        vwaddu.wx       v20, v10, s2
        vnsrl.wi        v8, v20, 6
  .ifc \type,avg
        vle8.v          v18, (a5)
        vaaddu.vv       v8, v8, v18
  .endif
        vse8.v          v8, (a5)
  .endif
        blt             t2, a3, 7b
8:
#if __riscv_xlen == 32
        lw              s2, (sp)
#elif __riscv_xlen == 64
        ld              s2, (sp)
#else
        lq              s2, (sp)
#endif
        addi            sp, sp, 16

        ret
.endm

func ff_put_rv40_chroma_mc_rvv, zve32x, zba
11:
        li      a7, 3
        blt     a3, a7, 12f
        do_chroma_mc put 1
12:
        do_chroma_mc put 0
endfunc

func ff_avg_rv40_chroma_mc_rvv, zve32x, zba
21:
        li      a7, 3
        blt     a3, a7, 22f
        do_chroma_mc avg 1
22:
        do_chroma_mc avg 0
endfunc

func ff_put_rv40_chroma_mc8_rvv, zve32x
        lpad    0
        li      t6, 8
        j       11b
endfunc

func ff_put_rv40_chroma_mc4_rvv, zve32x
        lpad    0
        li      t6, 4
        j       11b
endfunc

func ff_avg_rv40_chroma_mc8_rvv, zve32x
        lpad    0
        li      t6, 8
        j       21b
endfunc

func ff_avg_rv40_chroma_mc4_rvv, zve32x
        lpad    0
        li      t6, 4
        j       21b
endfunc
