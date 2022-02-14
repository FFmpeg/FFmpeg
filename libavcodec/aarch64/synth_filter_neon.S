/*
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2015 Janne Grunau <janne-libav@jannau.net>
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

#include "asm-offsets.h"

#include "libavutil/aarch64/asm.S"

.macro inner_loop
        ld1             {v29.4s},  [x9],  x15
        ld1             {v28.4s},  [x8],  x15
        ld1             {v30.4s},  [x10], x15
        ld1             {v31.4s},  [x11], x15
        rev64           v28.4s, v28.4s
        ld1             {v24.4s},  [x4],  x15
        ld1             {v25.4s},  [x5],  x15
        rev64           v31.4s, v31.4s
        ld1             {v26.4s},  [x6],  x15
        fmla            v5.4s,  v25.4s, v29.4s
        ld1             {v27.4s},  [x7],  x15
        ext             v28.16b, v28.16b, v28.16b, #8
        ext             v31.16b, v31.16b, v31.16b, #8
        fmla            v6.4s,  v26.4s, v30.4s
        fmls            v4.4s,  v24.4s, v28.4s
        fmla            v7.4s,  v27.4s, v31.4s
.endm

function ff_synth_filter_float_neon, export=1
        ldr             w7,  [x2]               // *synth_buf_offset
        ldr             x9,  [x0, #IMDCT_HALF]  // imdct_half function pointer
        sxtw            x7,  w7
        stp             x3,  x4,  [sp, #-64]!
        add             x1,  x1,  x7,  lsl #2   // synth_buf
        sub             w8,  w7,  #32
        stp             x5,  x1,  [sp, #16]
        and             x7,  x7,  #~63
        and             w8,  w8,  #511
        AARCH64_SIGN_LINK_REGISTER
        stp             x7,  x30, [sp, #32]
        str             w8,  [x2]
        str             s0,  [sp, #48]

        mov             x2,  x6                 // in

        blr             x9

        ldp             x2,  x4,  [sp]          // synct_buf_2, window
        ldp             x13, x9,  [sp, #16]     // out, synth_buf
        ldp             x0,  x30, [sp, #32]     // *synth_buf_offset
        AARCH64_VALIDATE_LINK_REGISTER
        ldr             s0,  [sp, #48]

        add             x3,  x2,  #16*4         // synct_buf_2 + 16
        add             x14, x13, #16*4         // out + 16
        add             x8,  x9,  #12*4
        mov             x15, #64*4
        mov             x1,  #4
1:
        add             x10, x9,  #16*4         // synth_buf
        add             x11, x8,  #16*4
        add             x5,  x4,  #16*4         // window
        add             x6,  x4,  #32*4
        add             x7,  x4,  #48*4

        ld1             {v4.4s},   [x2]         // a
        ld1             {v5.4s},   [x3]         // b
        movi            v6.4s,  #0              // c
        movi            v7.4s,  #0              // d

        mov             x12, #512
2:
        sub             x12, x12, #64
        cmp             x12, x0
        inner_loop
        b.gt            2b

        sub             x8,  x8,  #512*4
        sub             x9,  x9,  #512*4
        cbz             x12, 4f
        sub             x10, x10, #512*4
        sub             x11, x11, #512*4
3:
        subs            x12, x12, #64
        inner_loop
        b.gt            3b
4:
        subs            x1,  x1,  #1
        fmul            v4.4s,  v4.4s,  v0.s[0]
        fmul            v5.4s,  v5.4s,  v0.s[0]
        st1             {v6.4s},   [x2],  #16
        st1             {v7.4s},   [x3],  #16
        st1             {v4.4s},   [x13], #16
        st1             {v5.4s},   [x14], #16
        b.le            10f

        sub             x4,  x4,  #508*4        // window
        add             x9,  x9,  #4*4          // synth_buf
        sub             x8,  x8,  #4*4          // synth_buf
        b               1b

10:
        add             sp,  sp,  #64
        ret
endfunc
