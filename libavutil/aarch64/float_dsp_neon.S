/*
 * ARM NEON optimised Float DSP functions
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2014 Janne Grunau <janne-libav@jannau.net>
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
#include "asm.S"

function ff_vector_fmul_neon, export=1
1:      subs            w3,  w3,  #16
        ld1             {v0.4s, v1.4s}, [x1], #32
        ld1             {v2.4s, v3.4s}, [x1], #32
        ld1             {v4.4s, v5.4s}, [x2], #32
        ld1             {v6.4s, v7.4s}, [x2], #32
        fmul            v16.4s, v0.4s,  v4.4s
        fmul            v17.4s, v1.4s,  v5.4s
        fmul            v18.4s, v2.4s,  v6.4s
        fmul            v19.4s, v3.4s,  v7.4s
        st1             {v16.4s, v17.4s}, [x0], #32
        st1             {v18.4s, v19.4s}, [x0], #32
        b.ne            1b
        ret
endfunc

function ff_vector_fmac_scalar_neon, export=1
        mov             x3,  #-32
1:      subs            w2,  w2,  #16
        ld1             {v16.4s, v17.4s}, [x0], #32
        ld1             {v18.4s, v19.4s}, [x0], x3
        ld1             {v4.4s,  v5.4s},  [x1], #32
        ld1             {v6.4s,  v7.4s},  [x1], #32
        fmla            v16.4s, v4.4s,  v0.s[0]
        fmla            v17.4s, v5.4s,  v0.s[0]
        fmla            v18.4s, v6.4s,  v0.s[0]
        fmla            v19.4s, v7.4s,  v0.s[0]
        st1             {v16.4s, v17.4s}, [x0], #32
        st1             {v18.4s, v19.4s}, [x0], #32
        b.ne            1b
        ret
endfunc

function ff_vector_fmul_scalar_neon, export=1
        mov             w4,  #15
        bics            w3,  w2,  w4
        dup             v16.4s, v0.s[0]
        b.eq            3f
        ld1             {v0.4s, v1.4s}, [x1], #32
1:      subs            w3,  w3,  #16
        fmul            v0.4s,  v0.4s,  v16.4s
        ld1             {v2.4s, v3.4s}, [x1], #32
        fmul            v1.4s,  v1.4s,  v16.4s
        fmul            v2.4s,  v2.4s,  v16.4s
        st1             {v0.4s, v1.4s}, [x0], #32
        fmul            v3.4s,  v3.4s,  v16.4s
        b.eq            2f
        ld1             {v0.4s, v1.4s}, [x1], #32
        st1             {v2.4s, v3.4s}, [x0], #32
        b               1b
2:      ands            w2,  w2,  #15
        st1             {v2.4s, v3.4s}, [x0], #32
        b.eq            4f
3:      ld1             {v0.4s}, [x1], #16
        fmul            v0.4s,  v0.4s,  v16.4s
        st1             {v0.4s}, [x0], #16
        subs            w2,  w2,  #4
        b.gt            3b
4:      ret
endfunc

function ff_vector_dmul_scalar_neon, export=1
        dup             v16.2d, v0.d[0]
        ld1             {v0.2d, v1.2d}, [x1], #32
1:      subs            w2,  w2,  #8
        fmul            v0.2d,  v0.2d,  v16.2d
        ld1             {v2.2d, v3.2d}, [x1], #32
        fmul            v1.2d,  v1.2d,  v16.2d
        fmul            v2.2d,  v2.2d,  v16.2d
        st1             {v0.2d, v1.2d}, [x0], #32
        fmul            v3.2d,  v3.2d,  v16.2d
        ld1             {v0.2d, v1.2d}, [x1], #32
        st1             {v2.2d, v3.2d}, [x0], #32
        b.gt            1b
        ret
endfunc

function ff_vector_fmul_window_neon, export=1
        sxtw            x4,  w4                 // len
        sub             x2,  x2,  #8
        sub             x5,  x4,  #2
        add             x2,  x2,  x5, lsl #2    // src1 + 4 * (len - 4)
        add             x6,  x3,  x5, lsl #3    // win  + 8 * (len - 2)
        add             x5,  x0,  x5, lsl #3    // dst  + 8 * (len - 2)
        mov             x7,  #-16
        ld1             {v0.4s},  [x1], #16     // s0
        ld1             {v2.4s},  [x3], #16     // wi
        ld1             {v1.4s},  [x2], x7      // s1
1:      ld1             {v3.4s},  [x6], x7      // wj
        subs            x4,  x4,  #4
        fmul            v17.4s, v0.4s,  v2.4s   // s0 * wi
        rev64           v4.4s,  v1.4s
        rev64           v5.4s,  v3.4s
        rev64           v17.4s, v17.4s
        ext             v4.16b,  v4.16b,  v4.16b,  #8 // s1_r
        ext             v5.16b,  v5.16b,  v5.16b,  #8 // wj_r
        ext             v17.16b, v17.16b, v17.16b, #8 // (s0 * wi)_rev
        fmul            v16.4s, v0.4s,  v5.4s  // s0 * wj_r
        fmla            v17.4s, v1.4s,  v3.4s  // (s0 * wi)_rev + s1 * wj
        b.eq            2f
        ld1             {v0.4s},  [x1], #16
        fmls            v16.4s, v4.4s,  v2.4s  // s0 * wj_r - s1_r * wi
        st1             {v17.4s}, [x5], x7
        ld1             {v2.4s},  [x3], #16
        ld1             {v1.4s},  [x2], x7
        st1             {v16.4s}, [x0], #16
        b               1b
2:
        fmls            v16.4s, v4.4s,  v2.4s  // s0 * wj_r - s1_r * wi
        st1             {v17.4s}, [x5], x7
        st1             {v16.4s}, [x0], #16
        ret
endfunc

function ff_vector_fmul_add_neon, export=1
        ld1             {v0.4s, v1.4s},  [x1], #32
        ld1             {v2.4s, v3.4s},  [x2], #32
        ld1             {v4.4s, v5.4s},  [x3], #32
1:      subs            w4,  w4,  #8
        fmla            v4.4s,  v0.4s,  v2.4s
        fmla            v5.4s,  v1.4s,  v3.4s
        b.eq            2f
        ld1             {v0.4s, v1.4s},  [x1], #32
        ld1             {v2.4s, v3.4s},  [x2], #32
        st1             {v4.4s, v5.4s},  [x0], #32
        ld1             {v4.4s, v5.4s},  [x3], #32
        b               1b
2:      st1             {v4.4s, v5.4s},  [x0], #32
        ret
endfunc

function ff_vector_fmul_reverse_neon, export=1
        sxtw            x3,  w3
        add             x2,  x2,  x3,  lsl #2
        sub             x2,  x2,  #32
        mov             x4, #-32
        ld1             {v2.4s, v3.4s},  [x2], x4
        ld1             {v0.4s, v1.4s},  [x1], #32
1:      subs            x3,  x3,  #8
        rev64           v3.4s,  v3.4s
        rev64           v2.4s,  v2.4s
        ext             v3.16b, v3.16b, v3.16b,  #8
        ext             v2.16b, v2.16b, v2.16b,  #8
        fmul            v16.4s, v0.4s,  v3.4s
        fmul            v17.4s, v1.4s,  v2.4s
        b.eq            2f
        ld1             {v2.4s, v3.4s},  [x2], x4
        ld1             {v0.4s, v1.4s},  [x1], #32
        st1             {v16.4s, v17.4s},  [x0], #32
        b               1b
2:      st1             {v16.4s, v17.4s},  [x0], #32
        ret
endfunc

function ff_butterflies_float_neon, export=1
1:      ld1             {v0.4s}, [x0]
        ld1             {v1.4s}, [x1]
        subs            w2,  w2,  #4
        fsub            v2.4s,   v0.4s,  v1.4s
        fadd            v3.4s,   v0.4s,  v1.4s
        st1             {v2.4s}, [x1],   #16
        st1             {v3.4s}, [x0],   #16
        b.gt            1b
        ret
endfunc

function ff_scalarproduct_float_neon, export=1
        movi            v2.4s,  #0
1:      ld1             {v0.4s}, [x0],   #16
        ld1             {v1.4s}, [x1],   #16
        subs            w2,      w2,     #4
        fmla            v2.4s,   v0.4s,  v1.4s
        b.gt            1b
        faddp           v0.4s,   v2.4s,  v2.4s
        faddp           s0,      v0.2s
        ret
endfunc
