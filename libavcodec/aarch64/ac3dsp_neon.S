/*
 * Copyright (c) 2011 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2024 Geoff Hill <geoff@geoffhill.org>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "libavutil/aarch64/asm.S"

function ff_ac3_exponent_min_neon, export=1
        cbz             w1, 3f
1:      ld1             {v0.16b}, [x0]
        mov             w3, w1
        add             x4, x0, #256
2:      ld1             {v1.16b}, [x4]
        umin            v0.16b, v0.16b, v1.16b
        add             x4, x4, #256
        subs            w3, w3, #1
        b.gt            2b
        st1             {v0.16b}, [x0], #16
        subs            w2, w2, #16
        b.gt            1b
3:      ret
endfunc

function ff_ac3_extract_exponents_neon, export=1
        movi            v1.4s, #8
1:      ld1             {v0.4s}, [x1], #16
        abs             v0.4s, v0.4s
        clz             v0.4s, v0.4s
        sub             v0.4s, v0.4s, v1.4s
        xtn             v0.4h, v0.4s
        xtn             v0.8b, v0.8h
        st1             {v0.s}[0], [x0], #4
        subs            w2, w2, #4
        b.gt            1b
        ret
endfunc

function ff_float_to_fixed24_neon, export=1
1:      ld1             {v0.4s, v1.4s}, [x1], #32
        fcvtzs          v0.4s, v0.4s, #24
        ld1             {v2.4s, v3.4s}, [x1], #32
        fcvtzs          v1.4s, v1.4s, #24
        fcvtzs          v2.4s, v2.4s, #24
        st1             {v0.4s, v1.4s}, [x0], #32
        fcvtzs          v3.4s, v3.4s, #24
        st1             {v2.4s, v3.4s}, [x0], #32
        subs            w2, w2, #16
        b.ne            1b
        ret
endfunc

function ff_ac3_sum_square_butterfly_int32_neon, export=1
        movi            v0.2d, #0
        movi            v1.2d, #0
        movi            v2.2d, #0
1:      ld1             {v4.2s}, [x1], #8
        ld1             {v5.2s}, [x2], #8
        subs            w3, w3, #2
        smlal           v0.2d, v4.2s, v4.2s // sum of a^2
        smlal           v1.2d, v5.2s, v5.2s // sum of b^2
        sqdmlal         v2.2d, v4.2s, v5.2s // sum of 2ab
        b.gt            1b
        addp            d0, v0.2d
        addp            d1, v1.2d
        addp            d2, v2.2d
        sub             d3, d0, d2 // a^2 + b^2 - 2ab
        add             d2, d0, d2
        add             d3, d3, d1 // a^2 + b^2 + 2ab
        add             d2, d2, d1
        st1             {v0.1d-v3.1d}, [x0]
        ret
endfunc

function ff_ac3_sum_square_butterfly_float_neon, export=1
        movi            v0.4s, #0
        movi            v1.4s, #0
        movi            v2.4s, #0
        movi            v3.4s, #0
1:      ld1             {v30.4s}, [x1], #16
        ld1             {v31.4s}, [x2], #16
        fadd            v16.4s, v30.4s, v31.4s
        fsub            v17.4s, v30.4s, v31.4s
        fmla            v0.4s, v30.4s, v30.4s
        fmla            v1.4s, v31.4s, v31.4s
        fmla            v2.4s, v16.4s, v16.4s
        fmla            v3.4s, v17.4s, v17.4s
        subs            w3, w3, #4
        b.gt            1b
        faddp           v0.4s, v0.4s, v1.4s
        faddp           v2.4s, v2.4s, v3.4s
        faddp           v0.4s, v0.4s, v2.4s
        st1             {v0.4s}, [x0]
        ret
endfunc
