/*
 * Copyright (c) 2024 Institue of Software Chinese Academy of Sciences (ISCAS).
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

.macro vsetvlstatic8 len
.if \len <= 4
        vsetivli        zero, \len, e8, mf4, ta, ma
.elseif \len <= 8
        vsetivli        zero, \len, e8, mf2, ta, ma
.elseif \len <= 16
        vsetivli        zero, \len, e8, m1, ta, ma
.elseif \len <= 31
        vsetivli        zero, \len, e8, m2, ta, ma
.endif
.endm

.macro vsetvlstatic16 len
.if \len <= 4
        vsetivli        zero, \len, e16, mf2, ta, ma
.elseif \len <= 8
        vsetivli        zero, \len, e16, m1, ta, ma
.elseif \len <= 16
        vsetivli        zero, \len, e16, m2, ta, ma
.endif
.endm

#if __riscv_xlen >= 64
func ff_vp8_luma_dc_wht_rvv, zve64x
        lpad    0
        vsetivli    zero, 1, e64, m1, ta, ma
        vlseg4e64.v v4, (a1)
        vsetivli    zero, 4, e16, mf2, ta, ma
        vwadd.vv    v1, v5, v6
        addi        t1, sp, -48
        vwadd.vv    v0, v4, v7
        addi        t2, sp, -32
        vwsub.vv    v2, v5, v6
        addi        t3, sp, -16
        vwsub.vv    v3, v4, v7
        addi        sp, sp, -64
        vsetvli     zero, zero, e32, m1, ta, ma
        vadd.vv     v4, v0, v1
        vadd.vv     v5, v3, v2
        vse32.v     v4, (sp)
        vsub.vv     v6, v0, v1
        vse32.v     v5, (t1)
        vsub.vv     v7, v3, v2
        vse32.v     v6, (t2)
        vse32.v     v7, (t3)
        vlseg4e32.v v4, (sp)
        vadd.vv     v0, v4, v7
        sd          zero,   (a1)
        vadd.vv     v1, v5, v6
        sd          zero,  8(a1)
        vsub.vv     v2, v5, v6
        sd          zero, 16(a1)
        vsub.vv     v3, v4, v7
        sd          zero, 24(a1)
        vadd.vi     v0, v0, 3 # rounding mode not supported, do it manually
        li          t0, 4 * 16 * 2
        vadd.vi     v3, v3, 3
        addi        t1, a0, 16 * 2
        vadd.vv     v4, v0, v1
        addi        t2, a0, 16 * 2 * 2
        vadd.vv     v5, v3, v2
        addi        t3, a0, 16 * 2 * 3
        vsub.vv     v6, v0, v1
        vsub.vv     v7, v3, v2
        vsetvli     zero, zero, e16, mf2, ta, ma
        vnsra.wi    v0, v4, 3
        addi        sp, sp, 64
        vnsra.wi    v1, v5, 3
        vsse16.v    v0, (a0), t0
        vnsra.wi    v2, v6, 3
        vsse16.v    v1, (t1), t0
        vnsra.wi    v3, v7, 3
        vsse16.v    v2, (t2), t0
        vsse16.v    v3, (t3), t0
        ret
endfunc

func ff_vp8_idct_add_rvv, zve32x
        lpad    0
        csrwi       vxrm, 0
        vsetivli    zero, 4, e16, mf2, ta, ma
        addi        a3, a1, 1 * 4 * 2
        addi        a4, a1, 2 * 4 * 2
        addi        a5, a1, 3 * 4 * 2
        li          t1, 20091
        li          t2, 35468
        jal         t0, 1f
        vsseg4e16.v v0, (a1)
        jal         t0, 1f
        vlsseg4e8.v v4, (a0), a2
        vssra.vi    v0, v0, 3
        sd          zero,   (a1)
        vssra.vi    v1, v1, 3
        sd          zero,  8(a1)
        vssra.vi    v2, v2, 3
        sd          zero, 16(a1)
        vssra.vi    v3, v3, 3
        sd          zero, 24(a1)
        vsetvli     zero, zero, e8, mf4, ta, ma
        vwaddu.wv   v0, v0, v4
        vwaddu.wv   v1, v1, v5
        vwaddu.wv   v2, v2, v6
        vwaddu.wv   v3, v3, v7
        vsetvli     zero, zero, e16, mf2, ta, ma
        vmax.vx     v0, v0, zero
        vmax.vx     v1, v1, zero
        vmax.vx     v2, v2, zero
        vmax.vx     v3, v3, zero
        vsetvli     zero, zero, e8, mf4, ta, ma
        vnclipu.wi  v4, v0, 0
        vnclipu.wi  v5, v1, 0
        vnclipu.wi  v6, v2, 0
        vnclipu.wi  v7, v3, 0
        vssseg4e8.v v4, (a0), a2
        ret
1:
        vle16.v    v0, (a1)
        vle16.v    v2, (a4)
        vle16.v    v1, (a3)
        vle16.v    v3, (a5)
        vadd.vv    v4, v0, v2 # t0
        vsub.vv    v5, v0, v2 # t1
        vmulhsu.vx v8, v3, t1
        vmulhsu.vx v6, v1, t2
        vadd.vv    v8, v8, v3
        vmulhsu.vx v7, v1, t1
        vmulhsu.vx v9, v3, t2
        vadd.vv    v7, v7, v1
        vsub.vv    v6, v6, v8 # t2
        vadd.vv    v7, v7, v9 # t3
        vadd.vv    v1, v5, v6
        vsub.vv    v2, v5, v6
        vadd.vv    v0, v4, v7
        vsub.vv    v3, v4, v7
        jr         t0
endfunc
#endif

func ff_vp8_idct_dc_add_rvv, zve32x
        lpad    0
        lh      a3, (a1)
        addi    a3, a3, 4
        srai    a3, a3, 3
        # fall through
endfunc

# a3 = DC
func ff_vp78_idct_dc_add_rvv, zve32x
        vsetivli   zero, 4, e8, mf4, ta, ma
        sh         zero, (a1)
        vlse32.v   v8, (a0), a2
        vsetivli   zero, 16, e8, m1, ta, ma
        bgez       a3, 1f

        # block[0] < 0
        neg        a3, a3
        vssubu.vx  v8, v8, a3
        vsetivli   zero, 4, e8, mf4, ta, ma
        vsse32.v   v8, (a0), a2
        ret

1:      # block[0] >= 0
        vsaddu.vx  v8, v8, a3
        vsetivli   zero, 4, e8, mf4, ta, ma
        vsse32.v   v8, (a0), a2
        ret
endfunc

func ff_vp8_idct_dc_add4y_rvv, zve32x
        lpad    0
        li       t0, 32
        vsetivli zero, 4, e16, mf2, ta, ma
        li       t1, 4 - (128 << 3)
        vlse16.v v8, (a1), t0
        vadd.vx  v8, v8, t1
        vsra.vi  v8, v8, 3
        # fall through
endfunc

        .variant_cc ff_vp78_idct_dc_add4y_rvv
# v8 = [dc0 - 128, dc1 - 128, dc2 - 128, dc3 - 128]
func ff_vp78_idct_dc_add4y_rvv, zve32x
        vsetivli    zero, 16, e16, m2, ta, ma
        vid.v       v4
        li          a4, 4
        vsrl.vi     v4, v4, 2
        li          t1, 128
        vrgather.vv v0, v8, v4 # replicate each DC four times
        vsetvli     zero, zero, e8, m1, ta, ma
1:
        vle8.v      v8, (a0)
        addi        a4, a4, -1
        vwaddu.wv   v16, v0, v8
        sh          zero, (a1)
        vnclip.wi   v8, v16, 0
        addi        a1, a1, 32
        vxor.vx     v8, v8, t1
        vse8.v      v8, (a0)
        add         a0, a0, a2
        bnez        a4, 1b

        ret
endfunc

func ff_vp8_idct_dc_add4uv_rvv, zve32x
        lpad    0
        li       t0, 32
        vsetivli zero, 4, e16, mf2, ta, ma
        li       t1, 4 - (128 << 3)
        vlse16.v v8, (a1), t0
        vadd.vx  v8, v8, t1
        vsra.vi  v8, v8, 3
        # fall through
endfunc

        .variant_cc ff_vp78_idct_dc_add4uv_rvv
func ff_vp78_idct_dc_add4uv_rvv, zve64x
        vsetivli    zero, 16, e16, m2, ta, ma
        vid.v       v4
        li          a4, 4
        vsrl.vi     v4, v4, 2
        li          t1, 128
        vrgather.vv v0, v8, v4 # replicate each DC four times
        slli        t2, a2, 2
        vsetivli    zero, 2, e64, m1, ta, ma
1:
        vlse64.v    v8, (a0), t2
        addi        a4, a4, -1
        vsetivli    zero, 16, e8, m1, ta, ma
        vwaddu.wv   v16, v0, v8
        sh          zero, (a1)
        vnclip.wi   v8, v16, 0
        addi        a1, a1, 32
        vxor.vx     v8, v8, t1
        vsetivli    zero, 2, e64, m1, ta, ma
        vsse64.v    v8, (a0), t2
        add         a0, a0, a2
        bnez        a4, 1b

        ret
endfunc

.macro bilin_load_h dst mn
        addi            t5, a2, 1
        vle8.v          \dst, (a2)
        vle8.v          v2, (t5)
        vwmulu.vx       v28, \dst, t1
        vwmaccu.vx      v28, \mn, v2
        vwaddu.wx       v24, v28, t4
        vnsra.wi        \dst, v24, 3
.endm

.macro put_vp8_bilin_h_v type mn
func ff_put_vp8_bilin4_\type\()_rvv, zve32x
        lpad    0
        vsetvlstatic8   4
.Lbilin_\type:
        li              t1, 8
        li              t4, 4
        sub             t1, t1, \mn
1:
        add             t0, a2, a3
        add             t2, a0, a1
        addi            a4, a4, -2
.ifc \type,v
        add             t3, t0, a3
.else
        addi            t5, a2, 1
        addi            t3, t0, 1
        vle8.v          v2, (t5)
.endif
        vle8.v          v0, (a2)
        vle8.v          v4, (t0)
        vle8.v          v6, (t3)
        vwmulu.vx       v28, v0, t1
        vwmulu.vx       v26, v4, t1
.ifc \type,v
        vwmaccu.vx      v28, \mn, v4
.else
        vwmaccu.vx      v28, \mn, v2
.endif
        vwmaccu.vx      v26, \mn, v6
        vwaddu.wx       v24, v28, t4
        vwaddu.wx       v22, v26, t4
        vnsra.wi        v30, v24, 3
        vnsra.wi        v0, v22, 3
        vse8.v          v30, (a0)
        vse8.v          v0, (t2)
        add             a2, t0, a3
        add             a0, t2, a1
        bnez            a4, 1b

        ret
endfunc
.endm

put_vp8_bilin_h_v h a5
put_vp8_bilin_h_v v a6

func ff_put_vp8_bilin4_hv_rvv, zve32x
        lpad    0
        vsetvlstatic8   4
.Lbilin_hv:
        li              t3, 8
        sub             t1, t3, a5
        sub             t2, t3, a6
        li              t4, 4
        bilin_load_h    v4, a5
        add             a2, a2, a3
1:
        addi            a4, a4, -1
        vwmulu.vx       v20, v4, t2
        bilin_load_h    v4, a5
        vwmaccu.vx      v20, a6, v4
        vwaddu.wx       v24, v20, t4
        vnsra.wi        v0, v24, 3
        vse8.v          v0, (a0)
        add             a2, a2, a3
        add             a0, a0, a1
        bnez            a4, 1b

        ret
endfunc

.irp len,16,8
func ff_put_vp8_bilin\len\()_h_rvv, zve32x
        lpad    0
        vsetvlstatic8 \len
        j             .Lbilin_h
endfunc

func ff_put_vp8_bilin\len\()_v_rvv, zve32x
        lpad    0
        vsetvlstatic8 \len
        j             .Lbilin_v
endfunc

func ff_put_vp8_bilin\len\()_hv_rvv, zve32x
        lpad    0
        vsetvlstatic8 \len
        j             .Lbilin_hv
endfunc
.endr

const subpel_filters, align=0
        .byte 0,  -6, 123,  12,  -1, 0
        .byte 2, -11, 108,  36,  -8, 1
        .byte 0,  -9,  93,  50,  -6, 0
        .byte 3, -16,  77,  77, -16, 3
        .byte 0,  -6,  50,  93,  -9, 0
        .byte 1,  -8,  36, 108, -11, 2
        .byte 0,  -1,  12, 123,  -6, 0
endconst

.macro epel_filter size type regtype
.ifc \type,v
        addi            \regtype\()0, a6, -1
.else
        addi            \regtype\()0, a5, -1
.endif
        lla             \regtype\()2, subpel_filters
        sh1add          \regtype\()0, \regtype\()0, \regtype\()0
        sh1add          \regtype\()0, \regtype\()0, \regtype\()2
        .irp n,1,2,3,4
        lb              \regtype\n, \n(\regtype\()0)
        .endr
.ifc \size,6
        lb              \regtype\()5, 5(\regtype\()0)
        lb              \regtype\()0, (\regtype\()0)
.endif
.endm

.macro epel_load dst len size type from_mem regtype
.ifc \type,v
        sub             t6, a2, a3
        add             a7, a2, a3
.else
        addi            t6, a2, -1
        addi            a7, a2, 1
.endif

.if \from_mem
        vle8.v          v24, (a2)
        vle8.v          v22, (t6)
        vle8.v          v26, (a7)
.ifc \type,v
        add             a7, a7, a3
.else
        addi            a7, a7, 1
.endif
        vle8.v          v28, (a7)
        vwmulu.vx       v16, v24, \regtype\()2
        vwmulu.vx       v20, v26, \regtype\()3
.ifc \size,6
.ifc \type,v
        sub             t6, t6, a3
        add             a7, a7, a3
.else
        addi            t6, t6, -1
        addi            a7, a7, 1
.endif
        vle8.v          v24, (t6)
        vle8.v          v26, (a7)
        vwmaccu.vx      v16, \regtype\()0, v24
        vwmaccu.vx      v16, \regtype\()5, v26
.endif
        vwmaccsu.vx     v16, \regtype\()1, v22
        vwmaccsu.vx     v16, \regtype\()4, v28
.else
        vwmulu.vx       v16, v4, \regtype\()2
        vwmulu.vx       v20, v6, \regtype\()3
        .ifc \size,6
        vwmaccu.vx      v16, \regtype\()0, v0
        vwmaccu.vx      v16, \regtype\()5, v10
        .endif
        vwmaccsu.vx     v16, \regtype\()1, v2
        vwmaccsu.vx     v16, \regtype\()4, v8
.endif
        li              t6, 64
        vwadd.wx        v16, v16, t6
        vsetvlstatic16  \len
        vwadd.vv        v24, v16, v20
        vnsra.wi        v24, v24, 7
        vmax.vx         v24, v24, zero
        vsetvlstatic8   \len
        vnclipu.wi      \dst, v24, 0
.endm

.macro epel_load_inc dst len size type from_mem regtype
        epel_load       \dst \len \size \type \from_mem \regtype
        add             a2, a2, a3
.endm

.macro epel len size type
func ff_put_vp8_epel\len\()_\type\()\size\()_rvv, zve32x, zba
        lpad    0
        epel_filter     \size \type t
        vsetvlstatic8   \len
1:
        addi            a4, a4, -1
        epel_load_inc   v30 \len \size \type 1 t
        vse8.v          v30, (a0)
        add             a0, a0, a1
        bnez            a4, 1b

        ret
endfunc
.endm

.macro epel_hv len hsize vsize
func ff_put_vp8_epel\len\()_h\hsize\()v\vsize\()_rvv, zve32x, zba
        lpad    0
#if __riscv_xlen == 64
        addi    sp, sp, -48
        .irp n,0,1,2,3,4,5
        sd      s\n, (\n * 8)(sp)
        .endr
#else
        addi    sp, sp, -32
        .irp n,0,1,2,3,4,5
        sw      s\n, (\n * 4)(sp)
        .endr
#endif
        sub             a2, a2, a3
        epel_filter     \hsize h t
        epel_filter     \vsize v s
        vsetvlstatic8   \len
.if \hsize == 6 || \vsize == 6
        sub             a2, a2, a3
        epel_load_inc   v0 \len \hsize h 1 t
.endif
        epel_load_inc   v2 \len \hsize h 1 t
        epel_load_inc   v4 \len \hsize h 1 t
        epel_load_inc   v6 \len \hsize h 1 t
        epel_load_inc   v8 \len \hsize h 1 t
.if \hsize == 6 || \vsize == 6
        epel_load_inc   v10 \len \hsize h 1 t
.endif
        addi            a4, a4, -1
1:
        addi            a4, a4, -1
        epel_load       v30 \len \vsize v 0 s
        vse8.v          v30, (a0)
.if \hsize == 6 || \vsize == 6
        vmv.v.v         v0, v2
.endif
        vmv.v.v         v2, v4
        vmv.v.v         v4, v6
        vmv.v.v         v6, v8
.if \hsize == 6 || \vsize == 6
        vmv.v.v         v8, v10
        epel_load_inc   v10 \len \hsize h 1 t
.else
        epel_load_inc   v8 \len 4 h 1 t
.endif
        add             a0, a0, a1
        bnez            a4, 1b
        epel_load       v30 \len \vsize v 0 s
        vse8.v          v30, (a0)

#if __riscv_xlen == 64
        .irp n,0,1,2,3,4,5
        ld      s\n, (\n * 8)(sp)
        .endr
        addi    sp, sp, 48
#else
        .irp n,0,1,2,3,4,5
        lw      s\n, (\n * 4)(sp)
        .endr
        addi    sp, sp, 32
#endif

        ret
endfunc
.endm

.irp len,16,8,4
epel \len 6 h
epel \len 4 h
epel \len 6 v
epel \len 4 v
#if __riscv_xlen <= 64
epel_hv \len 6 6
epel_hv \len 4 4
epel_hv \len 6 4
epel_hv \len 4 6
#endif
.endr
