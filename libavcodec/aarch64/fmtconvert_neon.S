/*
 * ARM NEON optimised Format Conversion Utils
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2015 Janne Grunau  <janne-libav@jannau.net>
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
#include "libavutil/aarch64/asm.S"

function ff_int32_to_float_fmul_scalar_neon, export=1
        ld1             {v1.4s,v2.4s}, [x1], #32
        scvtf           v1.4s,  v1.4s
        scvtf           v2.4s,  v2.4s
1:
        subs            w2,  w2,  #8
        fmul            v3.4s,  v1.4s,  v0.s[0]
        fmul            v4.4s,  v2.4s,  v0.s[0]
        b.le            2f
        ld1             {v1.4s,v2.4s}, [x1], #32
        st1             {v3.4s,v4.4s}, [x0], #32
        scvtf           v1.4s,  v1.4s
        scvtf           v2.4s,  v2.4s
        b               1b
2:
        st1             {v3.4s,v4.4s}, [x0]
        ret
endfunc
