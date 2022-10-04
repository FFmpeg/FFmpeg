/*
 * Copyright © 2022 Rémi Denis-Courmont.
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

func ff_put_pixels_clamped_rvv, zve32x
        vsetivli    zero, 8, e16, m1, ta, ma
        vlseg8e16.v v24, (a0)
1:
        /* RVV only has signed-signed and unsigned-unsigned clipping.
         * We need two steps for signed-to-unsigned clipping. */
        vsetvli     t0, zero, e16, m8, ta, ma
        vmax.vx     v24, v24, zero

        vsetivli    zero, 8, e8, mf2, ta, ma
        vnclipu.wi  v16, v24, 0
        vnclipu.wi  v17, v25, 0
        vnclipu.wi  v18, v26, 0
        vnclipu.wi  v19, v27, 0
        vnclipu.wi  v20, v28, 0
        vnclipu.wi  v21, v29, 0
        vnclipu.wi  v22, v30, 0
        vnclipu.wi  v23, v31, 0
        vssseg8e8.v v16, (a1), a2
        ret
endfunc

func ff_put_signed_pixels_clamped_rvv, zve32x
        vsetivli    zero, 8, e16, m1, ta, ma
        vlseg8e16.v v24, (a0)

        li          t1, 128
        vsetivli    zero, 8, e8, mf2, ta, ma
        vnclip.wi   v16, v24, 0
        vnclip.wi   v17, v25, 0
        vnclip.wi   v18, v26, 0
        vnclip.wi   v19, v27, 0
        vnclip.wi   v20, v28, 0
        vnclip.wi   v21, v29, 0
        vnclip.wi   v22, v30, 0
        vnclip.wi   v23, v31, 0
        vsetvli     t0, zero, e8, m8, ta, ma
        vadd.vx     v16, v16, t1
        vsetivli    zero, 8, e8, mf2, ta, ma
        vssseg8e8.v v16, (a1), a2
        ret
endfunc

func ff_add_pixels_clamped_rvv, zve32x
        vsetivli    zero, 8, e8, mf2, ta, ma
        vlseg8e16.v v24, (a0)
        vlsseg8e8.v v16, (a1), a2
        vwaddu.wv   v24, v24, v16
        vwaddu.wv   v25, v25, v17
        vwaddu.wv   v26, v26, v18
        vwaddu.wv   v27, v27, v19
        vwaddu.wv   v28, v28, v20
        vwaddu.wv   v29, v29, v21
        vwaddu.wv   v30, v30, v22
        vwaddu.wv   v31, v31, v23
        j           1b
endfunc
