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
1:      ld1         {v0.4S,v1.4S}, [x1], #32
        fmul        v0.4S, v0.4S, v0.4S
        fmul        v1.4S, v1.4S, v1.4S
        faddp       v2.4S, v0.4S, v1.4S
        ld1         {v3.4S}, [x0]
        fadd        v3.4S, v3.4S, v2.4S
        st1         {v3.4S}, [x0], #16
        subs        w2, w2, #4
        b.gt        1b
        ret
endfunc

function ff_ps_mul_pair_single_neon, export=1
1:      ld1         {v0.4S,v1.4S}, [x1], #32
        ld1         {v2.4S},       [x2], #16
        zip1        v3.4S, v2.4S, v2.4S
        zip2        v4.4S, v2.4S, v2.4S
        fmul        v0.4S, v0.4S, v3.4S
        fmul        v1.4S, v1.4S, v4.4S
        st1         {v0.4S,v1.4S}, [x0], #32
        subs        w3, w3, #4
        b.gt        1b
        ret
endfunc

function ff_ps_stereo_interpolate_neon, export=1
        ld1         {v0.4S}, [x2]
        ld1         {v1.4S}, [x3]
        zip1        v4.4S, v0.4S, v0.4S
        zip2        v5.4S, v0.4S, v0.4S
        zip1        v6.4S, v1.4S, v1.4S
        zip2        v7.4S, v1.4S, v1.4S
1:      ld1         {v2.2S}, [x0]
        ld1         {v3.2S}, [x1]
        fadd        v4.4S, v4.4S, v6.4S
        fadd        v5.4S, v5.4S, v7.4S
        mov         v2.D[1], v2.D[0]
        mov         v3.D[1], v3.D[0]
        fmul        v2.4S, v2.4S, v4.4S
        fmla        v2.4S, v3.4S, v5.4S
        st1         {v2.D}[0], [x0], #8
        st1         {v2.D}[1], [x1], #8
        subs        w4, w4, #1
        b.gt        1b
        ret
endfunc

function ff_ps_stereo_interpolate_ipdopd_neon, export=1
        ld1         {v0.4S,v1.4S}, [x2]
        ld1         {v6.4S,v7.4S}, [x3]
        fneg        v2.4S, v1.4S
        fneg        v3.4S, v7.4S
        zip1        v16.4S, v0.4S, v0.4S
        zip2        v17.4S, v0.4S, v0.4S
        zip1        v18.4S, v2.4S, v1.4S
        zip2        v19.4S, v2.4S, v1.4S
        zip1        v20.4S, v6.4S, v6.4S
        zip2        v21.4S, v6.4S, v6.4S
        zip1        v22.4S, v3.4S, v7.4S
        zip2        v23.4S, v3.4S, v7.4S
1:      ld1         {v2.2S}, [x0]
        ld1         {v3.2S}, [x1]
        fadd        v16.4S, v16.4S, v20.4S
        fadd        v17.4S, v17.4S, v21.4S
        mov         v2.D[1], v2.D[0]
        mov         v3.D[1], v3.D[0]
        fmul        v4.4S, v2.4S, v16.4S
        fmla        v4.4S, v3.4S, v17.4S
        fadd        v18.4S, v18.4S, v22.4S
        fadd        v19.4S, v19.4S, v23.4S
        ext         v2.16B, v2.16B, v2.16B, #4
        ext         v3.16B, v3.16B, v3.16B, #4
        fmla        v4.4S, v2.4S, v18.4S
        fmla        v4.4S, v3.4S, v19.4S
        st1         {v4.D}[0], [x0], #8
        st1         {v4.D}[1], [x1], #8
        subs        w4, w4, #1
        b.gt        1b
        ret
endfunc

function ff_ps_hybrid_analysis_neon, export=1
        lsl         x3, x3, #3
        ld2         {v0.4S,v1.4S}, [x1], #32
        ld2         {v2.2S,v3.2S}, [x1], #16
        ld1         {v24.2S},      [x1], #8
        ld2         {v4.2S,v5.2S}, [x1], #16
        ld2         {v6.4S,v7.4S}, [x1]
        rev64       v6.4S, v6.4S
        rev64       v7.4S, v7.4S
        ext         v6.16B, v6.16B, v6.16B, #8
        ext         v7.16B, v7.16B, v7.16B, #8
        rev64       v4.2S, v4.2S
        rev64       v5.2S, v5.2S
        mov         v2.D[1], v3.D[0]
        mov         v4.D[1], v5.D[0]
        mov         v5.D[1], v2.D[0]
        mov         v3.D[1], v4.D[0]
        fadd        v16.4S, v0.4S, v6.4S
        fadd        v17.4S, v1.4S, v7.4S
        fsub        v18.4S, v1.4S, v7.4S
        fsub        v19.4S, v0.4S, v6.4S
        fadd        v22.4S, v2.4S, v4.4S
        fsub        v23.4S, v5.4S, v3.4S
        trn1        v20.2D, v22.2D, v23.2D      // {re4+re8, re5+re7, im8-im4, im7-im5}
        trn2        v21.2D, v22.2D, v23.2D      // {im4+im8, im5+im7, re4-re8, re5-re7}
1:      ld2         {v2.4S,v3.4S}, [x2], #32
        ld2         {v4.2S,v5.2S}, [x2], #16
        ld1         {v6.2S},       [x2], #8
        add         x2, x2, #8
        mov         v4.D[1], v5.D[0]
        mov         v6.S[1], v6.S[0]
        fmul        v6.2S, v6.2S, v24.2S
        fmul        v0.4S, v2.4S, v16.4S
        fmul        v1.4S, v2.4S, v17.4S
        fmls        v0.4S, v3.4S, v18.4S
        fmla        v1.4S, v3.4S, v19.4S
        fmla        v0.4S, v4.4S, v20.4S
        fmla        v1.4S, v4.4S, v21.4S
        faddp       v0.4S, v0.4S, v1.4S
        faddp       v0.4S, v0.4S, v0.4S
        fadd        v0.2S, v0.2S, v6.2S
        st1         {v0.2S}, [x0], x3
        subs        w4, w4, #1
        b.gt        1b
        ret
endfunc
