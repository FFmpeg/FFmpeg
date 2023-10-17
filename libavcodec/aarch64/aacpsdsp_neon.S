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

function ff_ps_add_squares_neon, export=1
1:      ld1             {v0.4s,v1.4s}, [x1], #32
        fmul            v0.4s, v0.4s, v0.4s
        fmul            v1.4s, v1.4s, v1.4s
        faddp           v2.4s, v0.4s, v1.4s
        ld1             {v3.4s}, [x0]
        fadd            v3.4s, v3.4s, v2.4s
        st1             {v3.4s}, [x0], #16
        subs            w2, w2, #4
        b.gt            1b
        ret
endfunc

function ff_ps_mul_pair_single_neon, export=1
1:      ld1             {v0.4s,v1.4s}, [x1], #32
        ld1             {v2.4s},       [x2], #16
        zip1            v3.4s, v2.4s, v2.4s
        zip2            v4.4s, v2.4s, v2.4s
        fmul            v0.4s, v0.4s, v3.4s
        fmul            v1.4s, v1.4s, v4.4s
        st1             {v0.4s,v1.4s}, [x0], #32
        subs            w3, w3, #4
        b.gt            1b
        ret
endfunc

function ff_ps_stereo_interpolate_neon, export=1
        ld1             {v0.4s}, [x2]
        ld1             {v1.4s}, [x3]
        zip1            v4.4s, v0.4s, v0.4s
        zip2            v5.4s, v0.4s, v0.4s
        zip1            v6.4s, v1.4s, v1.4s
        zip2            v7.4s, v1.4s, v1.4s
1:      ld1             {v2.2s}, [x0]
        ld1             {v3.2s}, [x1]
        fadd            v4.4s, v4.4s, v6.4s
        fadd            v5.4s, v5.4s, v7.4s
        mov             v2.d[1], v2.d[0]
        mov             v3.d[1], v3.d[0]
        fmul            v2.4s, v2.4s, v4.4s
        fmla            v2.4s, v3.4s, v5.4s
        st1             {v2.d}[0], [x0], #8
        st1             {v2.d}[1], [x1], #8
        subs            w4, w4, #1
        b.gt            1b
        ret
endfunc

function ff_ps_stereo_interpolate_ipdopd_neon, export=1
        ld1             {v0.4s,v1.4s}, [x2]
        ld1             {v6.4s,v7.4s}, [x3]
        fneg            v2.4s, v1.4s
        fneg            v3.4s, v7.4s
        zip1            v16.4s, v0.4s, v0.4s
        zip2            v17.4s, v0.4s, v0.4s
        zip1            v18.4s, v2.4s, v1.4s
        zip2            v19.4s, v2.4s, v1.4s
        zip1            v20.4s, v6.4s, v6.4s
        zip2            v21.4s, v6.4s, v6.4s
        zip1            v22.4s, v3.4s, v7.4s
        zip2            v23.4s, v3.4s, v7.4s
1:      ld1             {v2.2s}, [x0]
        ld1             {v3.2s}, [x1]
        fadd            v16.4s, v16.4s, v20.4s
        fadd            v17.4s, v17.4s, v21.4s
        mov             v2.d[1], v2.d[0]
        mov             v3.d[1], v3.d[0]
        fmul            v4.4s, v2.4s, v16.4s
        fmla            v4.4s, v3.4s, v17.4s
        fadd            v18.4s, v18.4s, v22.4s
        fadd            v19.4s, v19.4s, v23.4s
        ext             v2.16b, v2.16b, v2.16b, #4
        ext             v3.16b, v3.16b, v3.16b, #4
        fmla            v4.4s, v2.4s, v18.4s
        fmla            v4.4s, v3.4s, v19.4s
        st1             {v4.d}[0], [x0], #8
        st1             {v4.d}[1], [x1], #8
        subs            w4, w4, #1
        b.gt            1b
        ret
endfunc

function ff_ps_hybrid_analysis_neon, export=1
        lsl             x3, x3, #3
        ld2             {v0.4s,v1.4s}, [x1], #32
        ld2             {v2.2s,v3.2s}, [x1], #16
        ld1             {v24.2s},      [x1], #8
        ld2             {v4.2s,v5.2s}, [x1], #16
        ld2             {v6.4s,v7.4s}, [x1]
        rev64           v6.4s, v6.4s
        rev64           v7.4s, v7.4s
        ext             v6.16b, v6.16b, v6.16b, #8
        ext             v7.16b, v7.16b, v7.16b, #8
        rev64           v4.2s, v4.2s
        rev64           v5.2s, v5.2s
        mov             v2.d[1], v3.d[0]
        mov             v4.d[1], v5.d[0]
        mov             v5.d[1], v2.d[0]
        mov             v3.d[1], v4.d[0]
        fadd            v16.4s, v0.4s, v6.4s
        fadd            v17.4s, v1.4s, v7.4s
        fsub            v18.4s, v1.4s, v7.4s
        fsub            v19.4s, v0.4s, v6.4s
        fadd            v22.4s, v2.4s, v4.4s
        fsub            v23.4s, v5.4s, v3.4s
        trn1            v20.2d, v22.2d, v23.2d      // {re4+re8, re5+re7, im8-im4, im7-im5}
        trn2            v21.2d, v22.2d, v23.2d      // {im4+im8, im5+im7, re4-re8, re5-re7}
1:      ld2             {v2.4s,v3.4s}, [x2], #32
        ld2             {v4.2s,v5.2s}, [x2], #16
        ld1             {v6.2s},       [x2], #8
        add             x2, x2, #8
        mov             v4.d[1], v5.d[0]
        mov             v6.s[1], v6.s[0]
        fmul            v6.2s, v6.2s, v24.2s
        fmul            v0.4s, v2.4s, v16.4s
        fmul            v1.4s, v2.4s, v17.4s
        fmls            v0.4s, v3.4s, v18.4s
        fmla            v1.4s, v3.4s, v19.4s
        fmla            v0.4s, v4.4s, v20.4s
        fmla            v1.4s, v4.4s, v21.4s
        faddp           v0.4s, v0.4s, v1.4s
        faddp           v0.4s, v0.4s, v0.4s
        fadd            v0.2s, v0.2s, v6.2s
        st1             {v0.2s}, [x0], x3
        subs            w4, w4, #1
        b.gt            1b
        ret
endfunc
