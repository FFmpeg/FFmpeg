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

#if __riscv_xlen >= 64
func ff_put_pixels8x8_rvi
        lpad    0
.rept 8
        ld t0, (a1)
        sd t0, (a0)
        add a1, a1, a2
        add a0, a0, a2
.endr

        ret
endfunc

func ff_put_pixels16x16_rvi
        lpad    0
.rept 16
        ld t0, (a1)
        ld t1, 8(a1)
        sd t0, (a0)
        sd t1, 8(a0)
        add a1, a1, a2
        add a0, a0, a2
.endr

        ret
endfunc
#endif
