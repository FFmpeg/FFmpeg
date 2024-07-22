/*
 * Copyright (c) 2024 Rémi Denis-Courmont.
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

#if __riscv_xlen >= 64
func ff_vp7_luma_dc_wht_rvv, zve32x, zba
        lpad    0
        li          a2, 4 * 16 * 2
        li          a7, 16 * 2
        jal         t0, 1f
        vsse16.v    v4, (a0), a7
        vsse16.v    v5, (t1), a7
        vsse16.v    v6, (t2), a7
        vsse16.v    v7, (t3), a7
        ret
1:
        csrwi       vxrm, 0
        li          t4, 12540
        vsetivli    zero, 4, e16, mf2, ta, ma
        vlseg4e16.v v0, (a1)
        li          t6, 30274
        vwmul.vx    v8, v1, t4
        li          t5, 23170
        vwmul.vx    v9, v3, t6
        addi        t1, sp, -12 * 2
        vwmul.vx    v10, v1, t6
        addi        t2, sp, -8 * 2
        vwmul.vx    v11, v3, t4
        addi        t3, sp, -4 * 2
        vwadd.vv    v4, v0, v2
        addi        sp, sp, -16 * 2
        vwsub.vv    v5, v0, v2
        vsetvli     zero, zero, e32, m1, ta, ma
        vadd.vv     v7, v10, v11
        vmul.vx     v4, v4, t5
        vsub.vv     v6, v8, v9
        vmul.vx     v5, v5, t5
        vadd.vv     v0, v4, v7
        vsub.vv     v3, v4, v7
        vadd.vv     v1, v5, v6
        vsub.vv     v2, v5, v6
        vsetvli     zero, zero, e16, mf2, ta, ma
        vnsra.wi    v4, v0, 14
        vnsra.wi    v7, v3, 14
        vnsra.wi    v5, v1, 14
        vnsra.wi    v6, v2, 14
        vsseg4e16.v v4, (sp)
        vle16.v     v0, (sp)
        vle16.v     v1, (t1)
        vle16.v     v2, (t2)
        vle16.v     v3, (t3)
        vwmul.vx    v8, v1, t4
        vwmul.vx    v9, v3, t6
        add         t1, a2, a0
        vwmul.vx    v10, v1, t6
        sh1add      t2, a2, a0
        vwmul.vx    v11, v3, t4
        sh1add      a2, a2, a2 # a2 *= 3
        vwadd.vv    v4, v0, v2
        add         t3, a2, a0
        vwsub.vv    v5, v0, v2
        vsetvli     zero, zero, e32, m1, ta, ma
        vmul.vx     v4, v4, t5
        sd          zero,   (a1)
        vadd.vv     v7, v10, v11
        sd          zero,  8(a1)
        vmul.vx     v5, v5, t5
        sd          zero, 16(a1)
        vsub.vv     v6, v8, v9
        sd          zero, 24(a1)
        vadd.vv     v0, v4, v7
        addi        sp, sp, 16 * 2
        vsub.vv     v3, v4, v7
        vadd.vv     v1, v5, v6
        vsub.vv     v2, v5, v6
        vsetvli     zero, zero, e16, mf2, ta, ma
        vnclip.wi   v4, v0, 18
        vnclip.wi   v5, v1, 18
        vnclip.wi   v6, v2, 18
        vnclip.wi   v7, v3, 18
        jr          t0
endfunc

func ff_vp7_idct_add_rvv, zve32x
        lpad    0
        jal         t0, 1b
        csrwi       vxrm, 2
        vsetvli     zero, zero, e8, mf4, ta, ma
        vle8.v      v12, (a0)
        vle8.v      v13, (t1)
        vwaddu.wv   v4, v4, v12
        vle8.v      v14, (t2)
        vwaddu.wv   v5, v5, v13
        vle8.v      v15, (t3)
        vwaddu.wv   v6, v6, v14
        vwaddu.wv   v7, v7, v15
        vsetvli     zero, zero, e16, mf2, ta, ma
        vmax.vx     v4, v4, zero
        vmax.vx     v5, v5, zero
        vmax.vx     v6, v6, zero
        vmax.vx     v7, v7, zero
        vsetvli     zero, zero, e8, mf4, ta, ma
        vnclipu.wi  v0, v4, 0
        vnclipu.wi  v1, v5, 0
        vse8.v      v0, (a0)
        vnclipu.wi  v2, v6, 0
        vse8.v      v1, (t1)
        vnclipu.wi  v3, v7, 0
        vse8.v      v2, (t2)
        vse8.v      v3, (t3)
        ret
endfunc
#endif

.irp type, y, uv
func ff_vp7_idct_dc_add4\type\()_rvv, zve32x
        lpad    0
        li       t0, 32
        vsetivli zero, 4, e16, mf2, ta, ma
        li       t1, 23170
        vlse16.v v8, (a1), t0 # block[0..3][0]
        vwmul.vx v0, v8, t1
        li       t2, 0x20000 - (128 << 18)
        vsetvli  zero, zero, e32, m1, ta, ma
        vsra.vi  v0, v0, 14
        vmul.vx  v0, v0, t1
        vadd.vx  v0, v0, t2
        vsetvli  zero, zero, e16, mf2, ta, ma
        vnsra.wi v8, v0, 18   # 4x DC
        tail     ff_vp78_idct_dc_add4\type\()_rvv
endfunc
.endr
