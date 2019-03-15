/*
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

           // 0.85..^1    0.85..^2    0.85..^3    0.85..^4
const tab_st, align=4
        .word 0x3f599a00, 0x3f38f671, 0x3f1d382a, 0x3f05a32f
endconst
const tab_x0, align=4
        .word 0x0,        0x3f599a00, 0x3f38f671, 0x3f1d382a
endconst
const tab_x1, align=4
        .word 0x0,        0x0,        0x3f599a00, 0x3f38f671
endconst
const tab_x2, align=4
        .word 0x0,        0x0,        0x0,        0x3f599a00
endconst

function ff_opus_deemphasis_neon, export=1
        movrel  x4, tab_st
        ld1    {v4.4s}, [x4]
        movrel  x4, tab_x0
        ld1    {v5.4s}, [x4]
        movrel  x4, tab_x1
        ld1    {v6.4s}, [x4]
        movrel  x4, tab_x2
        ld1    {v7.4s}, [x4]

        fmul v0.4s, v4.4s, v0.s[0]

1:      ld1  {v1.4s, v2.4s}, [x1], #32

        fmla v0.4s, v5.4s, v1.s[0]
        fmul v3.4s, v7.4s, v2.s[2]

        fmla v0.4s, v6.4s, v1.s[1]
        fmla v3.4s, v6.4s, v2.s[1]

        fmla v0.4s, v7.4s, v1.s[2]
        fmla v3.4s, v5.4s, v2.s[0]

        fadd v1.4s, v1.4s, v0.4s
        fadd v2.4s, v2.4s, v3.4s

        fmla v2.4s, v4.4s, v1.s[3]

        st1  {v1.4s, v2.4s}, [x0], #32
        fmul v0.4s, v4.4s, v2.s[3]

        subs w2, w2, #8
        b.gt 1b

        mov s0, v2.s[3]

        ret
endfunc

function ff_opus_postfilter_neon, export=1
        ld1 {v0.4s}, [x2]
        dup v1.4s, v0.s[1]
        dup v2.4s, v0.s[2]
        dup v0.4s, v0.s[0]

        add w1, w1, #2
        sub x1, x0, x1, lsl #2

        ld1 {v3.4s}, [x1]
        fmul v3.4s, v3.4s, v2.4s

1:      add x1, x1, #4
        ld1 {v4.4s}, [x1]
        add x1, x1, #4
        ld1 {v5.4s}, [x1]
        add x1, x1, #4
        ld1 {v6.4s}, [x1]
        add x1, x1, #4
        ld1 {v7.4s}, [x1]

        fmla v3.4s, v7.4s, v2.4s
        fadd v6.4s, v6.4s, v4.4s

        ld1 {v8.4s}, [x0]
        fmla v8.4s, v5.4s, v0.4s

        fmul v6.4s, v6.4s, v1.4s
        fadd v6.4s, v6.4s, v3.4s

        fadd v8.4s, v8.4s, v6.4s
        fmul v3.4s, v7.4s, v2.4s

        st1  {v8.4s}, [x0], #16

        subs w3, w3, #4
        b.gt 1b

        ret
endfunc
