/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2026 Patryk Darmofalski <darpat039@gmail.com>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/aarch64/asm.S"

function ff_vector_clip_int32_neon, export=1
        dup             v0.4s, w2
        dup             v1.4s, w3
1:
        ld1             {v2.4s, v3.4s}, [x1], #32
        smin            v2.4s, v2.4s, v1.4s
        smin            v3.4s, v3.4s, v1.4s
        smax            v2.4s, v2.4s, v0.4s
        smax            v3.4s, v3.4s, v0.4s
        subs            w4, w4, #8
        st1             {v2.4s, v3.4s}, [x0], #32
        b.ne            1b
        ret
endfunc

function ff_vector_clipf_neon, export=1
        dup             v0.4s, v0.s[0]
        dup             v1.4s, v1.s[0]
1:
        ld1             {v2.4s, v3.4s}, [x1], #32
        fmin            v2.4s, v2.4s, v1.4s
        fmin            v3.4s, v3.4s, v1.4s
        fmaxnm          v2.4s, v2.4s, v0.4s
        fmaxnm          v3.4s, v3.4s, v0.4s
        subs            w2, w2, #8
        st1             {v2.4s, v3.4s}, [x0], #32
        b.gt            1b
        ret
endfunc
