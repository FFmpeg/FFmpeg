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

func ff_bswap16_buf_rvv, zve32x, zba
        lpad    0
1:
        vsetvli t0, a2, e16, m8, ta, ma
        vle16.v v8, (a1)
        sub     a2, a2, t0
        vsll.vi v16, v8, 8
        sh1add  a1, t0, a1
        vsrl.vi v24, v8, 8
        vor.vv  v8, v16, v24
        vse16.v v8, (a0)
        sh1add  a0, t0, a0
        bnez    a2, 1b

        ret
endfunc
