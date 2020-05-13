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

function ff_get_pixels_neon, export=1
        mov             w3,  #8
1:
        ld1             {v0.8b}, [x1], x2
        subs            w3,  w3,  #2
        ld1             {v1.8b}, [x1], x2
        uxtl            v0.8h,   v0.8b
        uxtl            v1.8h,   v1.8b
        st1             {v0.8h, v1.8h}, [x0], #32
        b.gt            1b

        ret
endfunc

function ff_diff_pixels_neon, export=1
        mov             w4,  #8
1:
        ld1             {v0.8b}, [x1], x3
        ld1             {v1.8b}, [x2], x3
        subs            w4,  w4,  #2
        ld1             {v2.8b}, [x1], x3
        usubl           v0.8h,   v0.8b,   v1.8b
        ld1             {v3.8b}, [x2], x3
        usubl           v1.8h,   v2.8b,   v3.8b
        st1             {v0.8h, v1.8h}, [x0], #32
        b.gt            1b

        ret
endfunc
