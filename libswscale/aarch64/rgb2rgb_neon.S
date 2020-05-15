/*
 * Copyright (c) 2020 Martin Storsjo
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

#include "libavutil/aarch64/asm.S"

// void ff_interleave_bytes_neon(const uint8_t *src1, const uint8_t *src2,
//                               uint8_t *dest, int width, int height,
//                               int src1Stride, int src2Stride, int dstStride);
function ff_interleave_bytes_neon, export=1
        sub             w5,  w5,  w3
        sub             w6,  w6,  w3
        sub             w7,  w7,  w3, lsl #1
1:
        ands            w8,  w3,  #0xfffffff0 // & ~15
        b.eq            3f
2:
        ld1             {v0.16b}, [x0], #16
        ld1             {v1.16b}, [x1], #16
        subs            w8,  w8,  #16
        st2             {v0.16b, v1.16b}, [x2], #32
        b.gt            2b

        tst             w3,  #15
        b.eq            9f

3:
        tst             w3,  #8
        b.eq            4f
        ld1             {v0.8b}, [x0], #8
        ld1             {v1.8b}, [x1], #8
        st2             {v0.8b, v1.8b}, [x2], #16
4:
        tst             w3,  #4
        b.eq            5f

        ld1             {v0.s}[0], [x0], #4
        ld1             {v1.s}[0], [x1], #4
        zip1            v0.8b,   v0.8b,   v1.8b
        st1             {v0.8b}, [x2], #8

5:
        ands            w8,  w3,  #3
        b.eq            9f
6:
        ldrb            w9,  [x0], #1
        ldrb            w10, [x1], #1
        subs            w8,  w8,  #1
        bfi             w9,  w10, #8,  #8
        strh            w9,  [x2], #2
        b.gt            6b

9:
        subs            w4,  w4,  #1
        b.eq            0f
        add             x0,  x0,  w5, sxtw
        add             x1,  x1,  w6, sxtw
        add             x2,  x2,  w7, sxtw
        b               1b

0:
        ret
endfunc
