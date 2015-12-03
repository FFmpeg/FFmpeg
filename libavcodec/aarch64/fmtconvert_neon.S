/*
 * ARM NEON optimised Format Conversion Utils
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2015 Janne Grunau  <janne-libav@jannau.net>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
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

function ff_int32_to_float_fmul_array8_neon, export=1
        lsr             w4,  w4,  #3
        subs            w5,  w4,  #1
        b.eq            1f
2:
        ld1             {v0.4s,v1.4s}, [x2], #32
        ld1             {v2.4s,v3.4s}, [x2], #32
        scvtf           v0.4s,  v0.4s
        scvtf           v1.4s,  v1.4s
        ld1             {v16.2s},  [x3], #8
        scvtf           v2.4s,  v2.4s
        scvtf           v3.4s,  v3.4s
        fmul            v4.4s,  v0.4s,  v16.s[0]
        fmul            v5.4s,  v1.4s,  v16.s[0]
        fmul            v6.4s,  v2.4s,  v16.s[1]
        fmul            v7.4s,  v3.4s,  v16.s[1]
        st1             {v4.4s,v5.4s}, [x1], #32
        st1             {v6.4s,v7.4s}, [x1], #32
        subs            w5,  w5,  #2
        b.gt            2b
        b.eq            1f
        ret
1:
        ld1             {v0.4s,v1.4s}, [x2]
        ld1             {v16.s}[0],  [x3]
        scvtf           v0.4s,  v0.4s
        scvtf           v1.4s,  v1.4s
        fmul            v4.4s,  v0.4s,  v16.s[0]
        fmul            v5.4s,  v1.4s,  v16.s[0]
        st1             {v4.4s,v5.4s}, [x1]
        ret
endfunc
