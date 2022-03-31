/*
 * IDCT AArch64 NEON optimisations
 *
 * Copyright (c) 2022 Ben Avison <bavison@riscosopen.org>
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

// Clamp 16-bit signed block coefficients to unsigned 8-bit
// On entry:
//   x0 -> array of 64x 16-bit coefficients
//   x1 -> 8-bit results
//   x2 = row stride for results, bytes
function ff_put_pixels_clamped_neon, export=1
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
        ld1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x0]
        sqxtun          v0.8b, v0.8h
        sqxtun          v1.8b, v1.8h
        sqxtun          v2.8b, v2.8h
        sqxtun          v3.8b, v3.8h
        sqxtun          v4.8b, v4.8h
        st1             {v0.8b}, [x1], x2
        sqxtun          v0.8b, v5.8h
        st1             {v1.8b}, [x1], x2
        sqxtun          v1.8b, v6.8h
        st1             {v2.8b}, [x1], x2
        sqxtun          v2.8b, v7.8h
        st1             {v3.8b}, [x1], x2
        st1             {v4.8b}, [x1], x2
        st1             {v0.8b}, [x1], x2
        st1             {v1.8b}, [x1], x2
        st1             {v2.8b}, [x1]
        ret
endfunc

// Clamp 16-bit signed block coefficients to signed 8-bit (biased by 128)
// On entry:
//   x0 -> array of 64x 16-bit coefficients
//   x1 -> 8-bit results
//   x2 = row stride for results, bytes
function ff_put_signed_pixels_clamped_neon, export=1
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
        movi            v4.8b, #128
        ld1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x0]
        sqxtn           v0.8b, v0.8h
        sqxtn           v1.8b, v1.8h
        sqxtn           v2.8b, v2.8h
        sqxtn           v3.8b, v3.8h
        sqxtn           v5.8b, v16.8h
        add             v0.8b, v0.8b, v4.8b
        sqxtn           v6.8b, v17.8h
        add             v1.8b, v1.8b, v4.8b
        sqxtn           v7.8b, v18.8h
        add             v2.8b, v2.8b, v4.8b
        sqxtn           v16.8b, v19.8h
        add             v3.8b, v3.8b, v4.8b
        st1             {v0.8b}, [x1], x2
        add             v0.8b, v5.8b, v4.8b
        st1             {v1.8b}, [x1], x2
        add             v1.8b, v6.8b, v4.8b
        st1             {v2.8b}, [x1], x2
        add             v2.8b, v7.8b, v4.8b
        st1             {v3.8b}, [x1], x2
        add             v3.8b, v16.8b, v4.8b
        st1             {v0.8b}, [x1], x2
        st1             {v1.8b}, [x1], x2
        st1             {v2.8b}, [x1], x2
        st1             {v3.8b}, [x1]
        ret
endfunc

// Add 16-bit signed block coefficients to unsigned 8-bit
// On entry:
//   x0 -> array of 64x 16-bit coefficients
//   x1 -> 8-bit input and results
//   x2 = row stride for 8-bit input and results, bytes
function ff_add_pixels_clamped_neon, export=1
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
        mov             x3, x1
        ld1             {v4.8b}, [x1], x2
        ld1             {v5.8b}, [x1], x2
        ld1             {v6.8b}, [x1], x2
        ld1             {v7.8b}, [x1], x2
        ld1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x0]
        uaddw           v0.8h, v0.8h, v4.8b
        uaddw           v1.8h, v1.8h, v5.8b
        uaddw           v2.8h, v2.8h, v6.8b
        ld1             {v4.8b}, [x1], x2
        uaddw           v3.8h, v3.8h, v7.8b
        ld1             {v5.8b}, [x1], x2
        sqxtun          v0.8b, v0.8h
        ld1             {v6.8b}, [x1], x2
        sqxtun          v1.8b, v1.8h
        ld1             {v7.8b}, [x1]
        sqxtun          v2.8b, v2.8h
        sqxtun          v3.8b, v3.8h
        uaddw           v4.8h, v16.8h, v4.8b
        st1             {v0.8b}, [x3], x2
        uaddw           v0.8h, v17.8h, v5.8b
        st1             {v1.8b}, [x3], x2
        uaddw           v1.8h, v18.8h, v6.8b
        st1             {v2.8b}, [x3], x2
        uaddw           v2.8h, v19.8h, v7.8b
        sqxtun          v4.8b, v4.8h
        sqxtun          v0.8b, v0.8h
        st1             {v3.8b}, [x3], x2
        sqxtun          v1.8b, v1.8h
        sqxtun          v2.8b, v2.8h
        st1             {v4.8b}, [x3], x2
        st1             {v0.8b}, [x3], x2
        st1             {v1.8b}, [x3], x2
        st1             {v2.8b}, [x3]
        ret
endfunc
