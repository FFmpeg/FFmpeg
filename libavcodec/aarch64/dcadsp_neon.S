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

const lfe_fir1_rev_tbl
        .byte           12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3
endconst

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
        movrel          x9, lfe_fir1_rev_tbl
        mov             x10, #(-16*4)
        lsr             x3, x3, #2
        sub             x1, x1, #(3*4)
        ldr             q23, [x9]
.Louter1:
        ld1             {v4.4s}, [x1]
        scvtf           v4.4s, v4.4s

        mov             x4, x2
        add             x5, x2, #((256-16)*4)
        mov             x6, x0
        add             x7, x0, #(64*4)
        mov             w8, #16
.Linner1:
        ld4             {v0.4s, v1.4s, v2.4s, v3.4s}, [x4], #64
        ld4             {v18.4s, v19.4s, v20.4s, v21.4s}, [x5], x10

        fmul            v17.4s, v0.4s, v4.s[3]
        fmla            v17.4s, v1.4s, v4.s[2]
        fmla            v17.4s, v2.4s, v4.s[1]
        fmla            v17.4s, v3.4s, v4.s[0]

        fmul            v22.4s, v21.4s, v4.s[3]
        fmla            v22.4s, v20.4s, v4.s[2]
        fmla            v22.4s, v19.4s, v4.s[1]
        fmla            v22.4s, v18.4s, v4.s[0]

        subs            w8, w8, #1

        tbl             v22.16b, {v22.16b}, v23.16b

        str             q17, [x6], #16
        str             q22, [x7], #16

        b.gt            .Linner1

        subs            x3, x3, #1
        add             x1, x1, #4
        add             x0, x0, #(128*4)
        b.gt            .Louter1
        ret
endfunc

function ff_lfe_x96_fixed_neon, export=1
        ldr             w4, [x2]
        dup             v2.4s, w4
        mov             w4, #0x013F
        movk            w4, #0x0020, lsl #16
        dup             v3.4s, w4
        mov             w4, #0xFEC1
        movk            w4, #0x005F, lsl #16
        dup             v4.4s, w4
.Lx96d_loop:
        ld1             {v0.4s}, [x1], #16
        subs            x3, x3, #4
        ext             v1.16b, v2.16b, v0.16b, #12
        smull           v5.2d, v0.2s, v3.2s
        smull2          v6.2d, v0.4s, v3.4s
        smlal           v5.2d, v1.2s, v4.2s
        smlal2          v6.2d, v1.4s, v4.4s
        smull           v7.2d,  v0.2s, v4.2s
        smull2          v16.2d, v0.4s, v4.4s
        smlal           v7.2d,  v1.2s, v3.2s
        smlal2          v16.2d, v1.4s, v3.4s
        mov             v2.16b, v0.16b
        sqrshrn         v17.2s, v5.2d, #23
        sqrshrn2        v17.4s, v6.2d, #23
        sqrshrn         v18.2s, v7.2d, #23
        sqrshrn2        v18.4s, v16.2d, #23
        sqshl           v17.4s, v17.4s, #8
        sqshl           v18.4s, v18.4s, #8
        sshr            v17.4s, v17.4s, #8
        sshr            v18.4s, v18.4s, #8
        zip1            v19.4s, v17.4s, v18.4s
        zip2            v20.4s, v17.4s, v18.4s
        st1             {v19.4s, v20.4s}, [x0], #32
        b.gt            .Lx96d_loop
        st1             {v2.s}[3], [x2]
        ret
endfunc

// Lanes hold four consecutive LFE samples. Outputs:
// ra = pcm[j], rb = pcm[32 + j], rc = pcm[31 - j], rd = pcm[63 - j]
.macro lfe_fir_fixed_pair ra, rb, rc, rd
        ldp             q8,  q9,  [x8],  #32
        ldp             q10, q11, [x9],  #-32
        smull           v0.2d,  v16.2s, v8.s[0]
        smull2          v1.2d,  v16.4s, v8.s[0]
        smull           v2.2d,  v16.2s, v11.s[3]
        smull2          v3.2d,  v16.4s, v11.s[3]
        smull           v4.2d,  v16.2s, v10.s[0]
        smull2          v5.2d,  v16.4s, v10.s[0]
        smull           v6.2d,  v16.2s, v9.s[3]
        smull2          v7.2d,  v16.4s, v9.s[3]
        smlal           v0.2d,  v17.2s, v8.s[1]
        smlal2          v1.2d,  v17.4s, v8.s[1]
        smlal           v2.2d,  v17.2s, v11.s[2]
        smlal2          v3.2d,  v17.4s, v11.s[2]
        smlal           v4.2d,  v17.2s, v10.s[1]
        smlal2          v5.2d,  v17.4s, v10.s[1]
        smlal           v6.2d,  v17.2s, v9.s[2]
        smlal2          v7.2d,  v17.4s, v9.s[2]
        smlal           v0.2d,  v18.2s, v8.s[2]
        smlal2          v1.2d,  v18.4s, v8.s[2]
        smlal           v2.2d,  v18.2s, v11.s[1]
        smlal2          v3.2d,  v18.4s, v11.s[1]
        smlal           v4.2d,  v18.2s, v10.s[2]
        smlal2          v5.2d,  v18.4s, v10.s[2]
        smlal           v6.2d,  v18.2s, v9.s[1]
        smlal2          v7.2d,  v18.4s, v9.s[1]
        smlal           v0.2d,  v19.2s, v8.s[3]
        smlal2          v1.2d,  v19.4s, v8.s[3]
        smlal           v2.2d,  v19.2s, v11.s[0]
        smlal2          v3.2d,  v19.4s, v11.s[0]
        smlal           v4.2d,  v19.2s, v10.s[3]
        smlal2          v5.2d,  v19.4s, v10.s[3]
        smlal           v6.2d,  v19.2s, v9.s[0]
        smlal2          v7.2d,  v19.4s, v9.s[0]
        smlal           v0.2d,  v20.2s, v9.s[0]
        smlal2          v1.2d,  v20.4s, v9.s[0]
        smlal           v2.2d,  v20.2s, v10.s[3]
        smlal2          v3.2d,  v20.4s, v10.s[3]
        smlal           v4.2d,  v20.2s, v11.s[0]
        smlal2          v5.2d,  v20.4s, v11.s[0]
        smlal           v6.2d,  v20.2s, v8.s[3]
        smlal2          v7.2d,  v20.4s, v8.s[3]
        smlal           v0.2d,  v21.2s, v9.s[1]
        smlal2          v1.2d,  v21.4s, v9.s[1]
        smlal           v2.2d,  v21.2s, v10.s[2]
        smlal2          v3.2d,  v21.4s, v10.s[2]
        smlal           v4.2d,  v21.2s, v11.s[1]
        smlal2          v5.2d,  v21.4s, v11.s[1]
        smlal           v6.2d,  v21.2s, v8.s[2]
        smlal2          v7.2d,  v21.4s, v8.s[2]
        smlal           v0.2d,  v22.2s, v9.s[2]
        smlal2          v1.2d,  v22.4s, v9.s[2]
        smlal           v2.2d,  v22.2s, v10.s[1]
        smlal2          v3.2d,  v22.4s, v10.s[1]
        smlal           v4.2d,  v22.2s, v11.s[2]
        smlal2          v5.2d,  v22.4s, v11.s[2]
        smlal           v6.2d,  v22.2s, v8.s[1]
        smlal2          v7.2d,  v22.4s, v8.s[1]
        smlal           v0.2d,  v23.2s, v9.s[3]
        smlal2          v1.2d,  v23.4s, v9.s[3]
        smlal           v2.2d,  v23.2s, v10.s[0]
        smlal2          v3.2d,  v23.4s, v10.s[0]
        smlal           v4.2d,  v23.2s, v11.s[3]
        smlal2          v5.2d,  v23.4s, v11.s[3]
        smlal           v6.2d,  v23.2s, v8.s[0]
        smlal2          v7.2d,  v23.4s, v8.s[0]
        sqrshrn         \ra\().2s, v0.2d, #23
        sqrshrn         \rb\().2s, v2.2d, #23
        sqrshrn         \rc\().2s, v4.2d, #23
        sqrshrn         \rd\().2s, v6.2d, #23
        sqrshrn2        \ra\().4s, v1.2d, #23
        sqrshrn2        \rb\().4s, v3.2d, #23
        sqrshrn2        \rc\().4s, v5.2d, #23
        sqrshrn2        \rd\().4s, v7.2d, #23
        sqshl           \ra\().4s, \ra\().4s, #8
        sqshl           \rb\().4s, \rb\().4s, #8
        sqshl           \rc\().4s, \rc\().4s, #8
        sqshl           \rd\().4s, \rd\().4s, #8
        sshr            \ra\().4s, \ra\().4s, #8
        sshr            \rb\().4s, \rb\().4s, #8
        sshr            \rc\().4s, \rc\().4s, #8
        sshr            \rd\().4s, \rd\().4s, #8
.endm

function ff_lfe_fir_fixed_neon, export=1
        stp             d8,  d9,  [sp, #-32]!
        stp             d10, d11, [sp, #16]
        lsr             x3,  x3,  #3
        sub             x1,  x1,  #(8*4)
        mov             x15, #-136
        mov             x16, #128
        mov             x17, #-120
.Louter_fixed:
        ld1             {v24.4s}, [x1]
        ldr             q20, [x1, #16]
        ldr             q16, [x1, #32]
        add             x1,  x1,  #16
        ext             v17.16b, v20.16b, v16.16b, #12
        ext             v18.16b, v20.16b, v16.16b, #8
        ext             v19.16b, v20.16b, v16.16b, #4
        ext             v21.16b, v24.16b, v20.16b, #12
        ext             v22.16b, v24.16b, v20.16b, #8
        ext             v23.16b, v24.16b, v20.16b, #4

        mov             x8,  x2
        add             x9,  x2,  #(248*4)
        mov             x4,  x0
        add             x5,  x0,  #(64*4)
        add             x6,  x0,  #(128*4)
        add             x7,  x0,  #(192*4)
        add             x10, x0,  #(30*4)
        add             x11, x0,  #(94*4)
        add             x12, x0,  #(158*4)
        add             x13, x0,  #(222*4)
.Linner_fixed:
        lfe_fir_fixed_pair v24, v26, v29, v31
        lfe_fir_fixed_pair v25, v27, v28, v30
        cmp             x8,  x9
        st2             {v24.s, v25.s}[0], [x4],  x16
        st2             {v24.s, v25.s}[1], [x5],  x16
        st2             {v24.s, v25.s}[2], [x6],  x16
        st2             {v24.s, v25.s}[3], [x7],  x16
        st2             {v26.s, v27.s}[0], [x4],  x17
        st2             {v26.s, v27.s}[1], [x5],  x17
        st2             {v26.s, v27.s}[2], [x6],  x17
        st2             {v26.s, v27.s}[3], [x7],  x17
        st2             {v28.s, v29.s}[0], [x10], x16
        st2             {v28.s, v29.s}[1], [x11], x16
        st2             {v28.s, v29.s}[2], [x12], x16
        st2             {v28.s, v29.s}[3], [x13], x16
        st2             {v30.s, v31.s}[0], [x10], x15
        st2             {v30.s, v31.s}[1], [x11], x15
        st2             {v30.s, v31.s}[2], [x12], x15
        st2             {v30.s, v31.s}[3], [x13], x15
        b.lt            .Linner_fixed

        subs            x3,  x3,  #1
        add             x0,  x0,  #(256*4)
        b.gt            .Louter_fixed

        ldp             d10, d11, [sp, #16]
        ldp             d8,  d9,  [sp], #32
        ret
endfunc
