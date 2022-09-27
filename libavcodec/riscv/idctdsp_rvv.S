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

#include "config.h"
#include "libavutil/riscv/asm.S"

func ff_put_pixels_clamped_rvv, zve32x
        vsetivli    zero, 8, e16, m1, ta, ma
        vlseg8e16.v v24, (a0)
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
