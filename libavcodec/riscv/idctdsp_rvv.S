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

func ff_put_pixels_clamped_rvv, zve64x
        lpad    0
        li      t0, 8 * 8
        vsetvli zero, t0, e16, m8, ta, ma
        vle16.v v24, (a0)
        /* RVV only has signed-signed and unsigned-unsigned clipping.
         * We need two steps for signed-to-unsigned clipping. */
        vmax.vx v24, v24, zero
        vsetvli zero, zero, e8, m4, ta, ma
        vnclipu.wi v16, v24, 0
        vsetivli zero, 8, e8, mf2, ta, ma
        vsse64.v v16, (a1), a2
        ret
endfunc

func ff_put_signed_pixels_clamped_rvv, zve64x
        lpad    0
        li      t0, 8 * 8
        vsetvli zero, t0, e8, m4, ta, ma
        vle16.v v24, (a0)
        li      t1, 128
        vnclip.wi v16, v24, 0
        vadd.vx v16, v16, t1
        vsetivli zero, 8, e8, mf2, ta, ma
        vsse64.v v16, (a1), a2
        ret
endfunc

func ff_add_pixels_clamped_rvv, zve64x
        lpad    0
        vsetivli zero, 8, e8, mf2, ta, ma
        li      t0, 8 * 8
        vlse64.v v16, (a1), a2
        vsetvli zero, t0, e8, m4, ta, ma
        vle16.v v24, (a0)
        vwaddu.wv v24, v24, v16
        vsetvli zero, zero, e16, m8, ta, ma
        vmax.vx v24, v24, zero
        vsetvli zero, zero, e8, m4, ta, ma
        vnclipu.wi v16, v24, 0
        vsetivli zero, 8, e8, mf2, ta, ma
        vsse64.v v16, (a1), a2
        ret
endfunc
