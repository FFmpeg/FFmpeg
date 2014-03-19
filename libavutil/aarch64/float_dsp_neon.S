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
        ld1             {v0.4S, v1.4S}, [x1], #32
        ld1             {v2.4S, v3.4S}, [x1], #32
        ld1             {v4.4S, v5.4S}, [x2], #32
        ld1             {v6.4S, v7.4S}, [x2], #32
        fmul            v16.4S, v0.4S,  v4.4S
        fmul            v17.4S, v1.4S,  v5.4S
        fmul            v18.4S, v2.4S,  v6.4S
        fmul            v19.4S, v3.4S,  v7.4S
        st1             {v16.4S, v17.4S}, [x0], #32
        st1             {v18.4S, v19.4S}, [x0], #32
        b.ne            1b
        ret
endfunc

function ff_vector_fmac_scalar_neon, export=1
        mov             x3,  #-32
1:      subs            w2,  w2,  #16
        ld1             {v16.4S, v17.4S}, [x0], #32
        ld1             {v18.4S, v19.4S}, [x0], x3
        ld1             {v4.4S,  v5.4S},  [x1], #32
        ld1             {v6.4S,  v7.4S},  [x1], #32
        fmla            v16.4S, v4.4S,  v0.S[0]
        fmla            v17.4S, v5.4S,  v0.S[0]
        fmla            v18.4S, v6.4S,  v0.S[0]
        fmla            v19.4S, v7.4S,  v0.S[0]
        st1             {v16.4S, v17.4S}, [x0], #32
        st1             {v18.4S, v19.4S}, [x0], #32
        b.ne            1b
        ret
endfunc

function ff_vector_fmul_scalar_neon, export=1
        mov             w4,  #15
        bics            w3,  w2,  w4
        dup             v16.4S, v0.S[0]
        b.eq            3f
        ld1             {v0.4S, v1.4S}, [x1], #32
1:      subs            w3,  w3,  #16
        fmul            v0.4S,  v0.4S,  v16.4S
        ld1             {v2.4S, v3.4S}, [x1], #32
        fmul            v1.4S,  v1.4S,  v16.4S
        fmul            v2.4S,  v2.4S,  v16.4S
        st1             {v0.4S, v1.4S}, [x0], #32
        fmul            v3.4S,  v3.4S,  v16.4S
        b.eq            2f
        ld1             {v0.4S, v1.4S}, [x1], #32
        st1             {v2.4S, v3.4S}, [x0], #32
        b               1b
2:      ands            w2,  w2,  #15
        st1             {v2.4S, v3.4S}, [x0], #32
        b.eq            4f
3:      ld1             {v0.4S}, [x1], #16
        fmul            v0.4S,  v0.4S,  v16.4S
        st1             {v0.4S}, [x0], #16
        subs            w2,  w2,  #4
        b.gt            3b
4:      ret
endfunc

function ff_vector_dmul_scalar_neon, export=1
        dup             v16.2D, v0.D[0]
        ld1             {v0.2D, v1.2D}, [x1], #32
1:      subs            w2,  w2,  #8
        fmul            v0.2D,  v0.2D,  v16.2D
        ld1             {v2.2D, v3.2D}, [x1], #32
        fmul            v1.2D,  v1.2D,  v16.2D
        fmul            v2.2D,  v2.2D,  v16.2D
        st1             {v0.2D, v1.2D}, [x0], #32
        fmul            v3.2D,  v3.2D,  v16.2D
        ld1             {v0.2D, v1.2D}, [x1], #32
        st1             {v2.2D, v3.2D}, [x0], #32
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
        ld1             {v0.4S},  [x1], #16     // s0
        ld1             {v2.4S},  [x3], #16     // wi
        ld1             {v1.4S},  [x2], x7      // s1
1:      ld1             {v3.4S},  [x6], x7      // wj
        subs            x4,  x4,  #4
        fmul            v17.4S, v0.4S,  v2.4S   // s0 * wi
        rev64           v4.4S,  v1.4S
        rev64           v5.4S,  v3.4S
        rev64           v17.4S, v17.4S
        ext             v4.16B,  v4.16B,  v4.16B,  #8 // s1_r
        ext             v5.16B,  v5.16B,  v5.16B,  #8 // wj_r
        ext             v17.16B, v17.16B, v17.16B, #8 // (s0 * wi)_rev
        fmul            v16.4S, v0.4S,  v5.4S  // s0 * wj_r
        fmla            v17.4S, v1.4S,  v3.4S  // (s0 * wi)_rev + s1 * wj
        b.eq            2f
        ld1             {v0.4S},  [x1], #16
        fmls            v16.4S, v4.4S,  v2.4S  // s0 * wj_r - s1_r * wi
        st1             {v17.4S}, [x5], x7
        ld1             {v2.4S},  [x3], #16
        ld1             {v1.4S},  [x2], x7
        st1             {v16.4S}, [x0], #16
        b               1b
2:
        fmls            v16.4S, v4.4S,  v2.4S  // s0 * wj_r - s1_r * wi
        st1             {v17.4S}, [x5], x7
        st1             {v16.4S}, [x0], #16
        ret
endfunc

function ff_vector_fmul_add_neon, export=1
        ld1             {v0.4S, v1.4S},  [x1], #32
        ld1             {v2.4S, v3.4S},  [x2], #32
        ld1             {v4.4S, v5.4S},  [x3], #32
1:      subs            w4,  w4,  #8
        fmla            v4.4S,  v0.4S,  v2.4S
        fmla            v5.4S,  v1.4S,  v3.4S
        b.eq            2f
        ld1             {v0.4S, v1.4S},  [x1], #32
        ld1             {v2.4S, v3.4S},  [x2], #32
        st1             {v4.4S, v5.4S},  [x0], #32
        ld1             {v4.4S, v5.4S},  [x3], #32
        b               1b
2:      st1             {v4.4S, v5.4S},  [x0], #32
        ret
endfunc

function ff_vector_fmul_reverse_neon, export=1
        sxtw            x3,  w3
        add             x2,  x2,  x3,  lsl #2
        sub             x2,  x2,  #32
        mov             x4, #-32
        ld1             {v2.4S, v3.4S},  [x2], x4
        ld1             {v0.4S, v1.4S},  [x1], #32
1:      subs            x3,  x3,  #8
        rev64           v3.4S,  v3.4S
        rev64           v2.4S,  v2.4S
        ext             v3.16B, v3.16B, v3.16B,  #8
        ext             v2.16B, v2.16B, v2.16B,  #8
        fmul            v16.4S, v0.4S,  v3.4S
        fmul            v17.4S, v1.4S,  v2.4S
        b.eq            2f
        ld1             {v2.4S, v3.4S},  [x2], x4
        ld1             {v0.4S, v1.4S},  [x1], #32
        st1             {v16.4S, v17.4S},  [x0], #32
        b               1b
2:      st1             {v16.4S, v17.4S},  [x0], #32
        ret
endfunc

function ff_butterflies_float_neon, export=1
1:      ld1             {v0.4S}, [x0]
        ld1             {v1.4S}, [x1]
        subs            w2,  w2,  #4
        fsub            v2.4S,   v0.4S,  v1.4S
        fadd            v3.4S,   v0.4S,  v1.4S
        st1             {v2.4S}, [x1],   #16
        st1             {v3.4S}, [x0],   #16
        b.gt            1b
        ret
endfunc

function ff_scalarproduct_float_neon, export=1
        movi            v2.4S,  #0
1:      ld1             {v0.4S}, [x0],   #16
        ld1             {v1.4S}, [x1],   #16
        subs            w2,      w2,     #4
        fmla            v2.4S,   v0.4S,  v1.4S
        b.gt            1b
        faddp           v0.4S,   v2.4S,  v2.4S
        faddp           s0,      v0.2S
        ret
endfunc
