/*
 * AArch64 NEON optimised DCA LFE FIR filter functions
 * Copyright (c) 2026 Jeongkeun Kim <variety0724@gmail.com>
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

function ff_lfe_fir0_float_neon, export=1
        lsr             x3, x3, #1
        sub             x1, x1, #(7*4)
.Louter0:
        ld1             {v4.4s, v5.4s}, [x1]
        scvtf           v4.4s, v4.4s
        scvtf           v5.4s, v5.4s

        ext             v6.16b, v5.16b, v5.16b, #8
        rev64           v6.4s,  v6.4s
        ext             v7.16b, v4.16b, v4.16b, #8
        rev64           v7.4s,  v7.4s

        mov             x4, x2
        add             x5, x2, #(248*4)
        mov             x6, x0
        add             x7, x0, #(32*4)
        mov             w8, #32
.Linner0:
        ld1             {v0.4s,  v1.4s},  [x4], #32
        ld1             {v16.4s, v17.4s}, [x5]
        sub             x5, x5, #32
        subs            w8, w8, #1
        fmul            v2.4s, v0.4s,  v6.4s
        fmul            v3.4s, v16.4s, v4.4s
        fmla            v2.4s, v1.4s,  v7.4s
        fmla            v3.4s, v17.4s, v5.4s
        faddp           v2.4s, v2.4s, v2.4s
        faddp           v3.4s, v3.4s, v3.4s
        faddp           s2, v2.2s
        faddp           s3, v3.2s
        str             s2, [x6], #4
        str             s3, [x7], #4
        b.gt            .Linner0

        subs            x3, x3, #1
        add             x1, x1, #4
        add             x0, x0, #(64*4)
        b.gt            .Louter0
        ret
endfunc

function ff_lfe_fir1_float_neon, export=1
        lsr             x3, x3, #2
        sub             x1, x1, #(3*4)
.Louter1:
        ld1             {v4.4s}, [x1]
        scvtf           v4.4s, v4.4s

        ext             v5.16b, v4.16b, v4.16b, #8
        rev64           v5.4s,  v5.4s

        mov             x4, x2
        add             x5, x2, #(252*4)
        mov             x6, x0
        add             x7, x0, #(64*4)
        mov             w8, #64
.Linner1:
        ld1             {v0.4s},  [x4], #16
        ld1             {v16.4s}, [x5]
        sub             x5, x5, #16
        subs            w8, w8, #1
        fmul            v2.4s, v0.4s,  v5.4s
        fmul            v3.4s, v16.4s, v4.4s
        faddp           v2.4s, v2.4s, v2.4s
        faddp           v3.4s, v3.4s, v3.4s
        faddp           s2, v2.2s
        faddp           s3, v3.2s
        str             s2, [x6], #4
        str             s3, [x7], #4
        b.gt            .Linner1

        subs            x3, x3, #1
        add             x1, x1, #4
        add             x0, x0, #(128*4)
        b.gt            .Louter1
        ret
endfunc
