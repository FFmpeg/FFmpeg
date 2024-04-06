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
