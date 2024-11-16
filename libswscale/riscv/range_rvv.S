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

func ff_range_lum_to_jpeg_16_rvv, zve32x, zba
        lpad    0
        li       t1, 30189
        li       t2, 19077
        li       t3, -39057361
1:
        vsetvli  t0, a1, e16, m4, ta, ma
        vle16.v  v0, (a0)
        sub      a1, a1, t0
        vmin.vx  v0, v0, t1
        vwmul.vx v8, v0, t2
        vsetvli  zero, zero, e32, m8, ta, ma
        vadd.vx  v8, v8, t3
        vsetvli  zero, zero, e16, m4, ta, ma
        vnsra.wi v0, v8, 14
        vse16.v  v0, (a0)
        sh1add   a0, t0, a0
        bnez     a1, 1b

        ret
endfunc

func ff_range_lum_from_jpeg_16_rvv, zve32x, zba
        lpad    0
        li       t1, 14071
        li       t2, 33561947
1:
        vsetvli  t0, a1, e16, m4, ta, ma
        vle16.v  v0, (a0)
        sub      a1, a1, t0
        vwmul.vx v8, v0, t1
        vsetvli  zero, zero, e32, m8, ta, ma
        vadd.vx  v8, v8, t2
        vsetvli  zero, zero, e16, m4, ta, ma
        vnsra.wi v0, v8, 14
        vse16.v  v0, (a0)
        sh1add   a0, t0, a0
        bnez     a1, 1b

        ret
endfunc

func ff_range_chr_to_jpeg_16_rvv, zve32x, zba
        lpad    0
        li      t1, 30775
        li      t2, 4663
        li      t3, -9289992
1:
        vsetvli  t0, a2, e16, m4, ta, ma
        vle16.v  v0, (a0)
        sub      a2, a2, t0
        vle16.v  v4, (a1)
        vmin.vx  v0, v0, t1
        vmin.vx  v4, v4, t1
        vwmul.vx v8, v0, t2
        vwmul.vx v16, v4, t2
        vsetvli  zero, zero, e32, m8, ta, ma
        vadd.vx  v8, v8, t3
        vadd.vx  v16, v16, t3
        vsetvli  zero, zero, e16, m4, ta, ma
        vnsra.wi v0, v8, 12
        vnsra.wi v4, v16, 12
        vse16.v  v0, (a0)
        sh1add   a0, t0, a0
        vse16.v  v4, (a1)
        sh1add   a1, t0, a1
        bnez     a2, 1b

        ret
endfunc

func ff_range_chr_from_jpeg_16_rvv, zve32x, zba
        lpad    0
        li      t1, 1799
        li      t2, 4081085
1:
        vsetvli  t0, a2, e16, m4, ta, ma
        vle16.v  v0, (a0)
        sub      a2, a2, t0
        vle16.v  v4, (a1)
        vwmul.vx v8, v0, t1
        vwmul.vx v16, v4, t1
        vsetvli  zero, zero, e32, m8, ta, ma
        vadd.vx  v8, v8, t2
        vadd.vx  v16, v16, t2
        vsetvli  zero, zero, e16, m4, ta, ma
        vnsra.wi v0, v8, 11
        vnsra.wi v4, v16, 11
        vse16.v  v0, (a0)
        sh1add   a0, t0, a0
        vse16.v  v4, (a1)
        sh1add   a1, t0, a1
        bnez     a2, 1b

        ret
endfunc
