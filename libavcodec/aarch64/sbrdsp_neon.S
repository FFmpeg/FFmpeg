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

const factors, align=4
        .float 1.0, -1.0, 1.0, -1.0
endconst

const phi_noise_0, align=4
        .float 1.0, 0.0, 1.0, 0.0
endconst

const phi_noise_1, align=4
        .float 0.0,  1.0,  0.0, -1.0
        .float 0.0, -1.0,  0.0,  1.0
endconst

const phi_noise_2, align=4
        .float -1.0, 0.0, -1.0, 0.0
endconst

const phi_noise_3, align=4
        .float 0.0, -1.0,  0.0,  1.0
        .float 0.0,  1.0,  0.0, -1.0
endconst

function ff_sbr_sum64x5_neon, export=1
        add             x1, x0, #64*4
        add             x2, x0, #128*4
        add             x3, x0, #192*4
        add             x4, x0, #256*4
        mov             x5, #64
1:      ld1             {v0.4s}, [x0]
        ld1             {v1.4s}, [x1], #16
        fadd            v0.4s, v0.4s, v1.4s
        ld1             {v2.4s}, [x2], #16
        fadd            v0.4s, v0.4s, v2.4s
        ld1             {v3.4s}, [x3], #16
        fadd            v0.4s, v0.4s, v3.4s
        ld1             {v4.4s}, [x4], #16
        fadd            v0.4s, v0.4s, v4.4s
        st1             {v0.4s}, [x0], #16
        subs            x5, x5, #4
        b.gt            1b
        ret
endfunc

function ff_sbr_sum_square_neon, export=1
        movi            v0.4s, #0
1:      ld1             {v1.4s}, [x0], #16
        fmla            v0.4s, v1.4s, v1.4s
        subs            w1, w1, #2
        b.gt            1b
        faddp           v0.4s, v0.4s, v0.4s
        faddp           v0.4s, v0.4s, v0.4s
        ret
endfunc

function ff_sbr_neg_odd_64_neon, export=1
        mov             x1, x0
        movi            v5.4s, #1<<7, lsl #24
        ld2             {v0.4s, v1.4s}, [x0], #32
        eor             v1.16b, v1.16b, v5.16b
        ld2             {v2.4s, v3.4s}, [x0], #32
.rept 3
        st2             {v0.4s, v1.4s}, [x1], #32
        eor             v3.16b, v3.16b, v5.16b
        ld2             {v0.4s, v1.4s}, [x0], #32
        st2             {v2.4s, v3.4s}, [x1], #32
        eor             v1.16b, v1.16b, v5.16b
        ld2             {v2.4s, v3.4s}, [x0], #32
.endr
        eor             v3.16b, v3.16b, v5.16b
        st2             {v0.4s, v1.4s}, [x1], #32
        st2             {v2.4s, v3.4s}, [x1], #32
        ret
endfunc

function ff_sbr_qmf_pre_shuffle_neon, export=1
        add             x1, x0, #60*4
        add             x2, x0, #64*4
        mov             x3, #-16
        mov             x4, #-4
        movi            v6.4s, #1<<7, lsl #24
        ld1             {v0.2s}, [x0], #8
        st1             {v0.2s}, [x2], #8
.rept 7
        ld1             {v1.4s}, [x1], x3
        ld1             {v2.4s}, [x0], #16
        eor             v1.16b, v1.16b, v6.16b
        rev64           v1.4s, v1.4s
        ext             v1.16b, v1.16b, v1.16b, #8
        st2             {v1.4s, v2.4s}, [x2], #32
.endr
        add             x1, x1, #8
        ld1             {v1.2s}, [x1], x4
        ld1             {v2.2s}, [x0], #8
        ld1             {v1.s}[3], [x1]
        ld1             {v2.s}[2], [x0]
        eor             v1.16b, v1.16b, v6.16b
        rev64           v1.4s, v1.4s
        st2             {v1.2s, v2.2s}, [x2], #16
        st2             {v1.s, v2.s}[2], [x2]
        ret
endfunc

function ff_sbr_qmf_post_shuffle_neon, export=1
        add             x2, x1, #60*4
        mov             x3, #-16
        mov             x4, #32
        movi            v6.4s, #1<<7, lsl #24
1:      ld1             {v0.4s}, [x2], x3
        ld1             {v1.4s}, [x1], #16
        eor             v0.16b, v0.16b, v6.16b
        rev64           v0.4s, v0.4s
        ext             v0.16b, v0.16b, v0.16b, #8
        st2             {v0.4s, v1.4s}, [x0], #32
        subs            x4, x4, #4
        b.gt            1b
        ret
endfunc

function ff_sbr_qmf_deint_neg_neon, export=1
        add             x1, x1, #56*4
        add             x2, x0, #60*4
        mov             x3, #-32
        mov             x4, #32
        movi            v2.4s, #1<<7, lsl #24
1:      ld2             {v0.4s, v1.4s}, [x1], x3
        eor             v0.16b, v0.16b, v2.16b
        rev64           v1.4s, v1.4s
        ext             v1.16b, v1.16b, v1.16b, #8
        st1             {v0.4s}, [x2]
        st1             {v1.4s}, [x0], #16
        sub             x2, x2, #16
        subs            x4, x4, #4
        b.gt            1b
        ret
endfunc

function ff_sbr_qmf_deint_bfly_neon, export=1
        add             x2, x2, #60*4
        add             x3, x0, #124*4
        mov             x4, #64
        mov             x5, #-16
1:      ld1             {v0.4s}, [x1], #16
        ld1             {v1.4s}, [x2], x5
        rev64           v2.4s, v0.4s
        ext             v2.16b, v2.16b, v2.16b, #8
        rev64           v3.4s, v1.4s
        ext             v3.16b, v3.16b, v3.16b, #8
        fadd            v1.4s, v1.4s, v2.4s
        fsub            v0.4s, v0.4s, v3.4s
        st1             {v0.4s}, [x0], #16
        st1             {v1.4s}, [x3], x5
        subs            x4, x4, #4
        b.gt            1b
        ret
endfunc

function ff_sbr_hf_gen_neon, export=1
        sxtw            x4, w4
        sxtw            x5, w5
        movrel          x6, factors
        ld1             {v7.4s}, [x6]
        dup             v1.4s, v0.s[0]
        mov             v2.8b, v1.8b
        mov             v2.s[2], v7.s[0]
        mov             v2.s[3], v7.s[0]
        fmul            v1.4s, v1.4s, v2.4s
        ld1             {v0.d}[0], [x3]
        ld1             {v0.d}[1], [x2]
        fmul            v0.4s, v0.4s, v1.4s
        fmul            v1.4s, v0.4s, v7.4s
        rev64           v0.4s, v0.4s
        sub             x7, x5, x4
        add             x0, x0, x4, lsl #3
        add             x1, x1, x4, lsl #3
        sub             x1, x1, #16
1:      ld1             {v2.4s}, [x1], #16
        ld1             {v3.2s}, [x1]
        fmul            v4.4s, v2.4s, v1.4s
        fmul            v5.4s, v2.4s, v0.4s
        faddp           v4.4s, v4.4s, v4.4s
        faddp           v5.4s, v5.4s, v5.4s
        faddp           v4.4s, v4.4s, v4.4s
        faddp           v5.4s, v5.4s, v5.4s
        mov             v4.s[1], v5.s[0]
        fadd            v4.2s, v4.2s, v3.2s
        st1             {v4.2s}, [x0], #8
        sub             x1, x1, #8
        subs            x7, x7, #1
        b.gt            1b
        ret
endfunc

function ff_sbr_hf_g_filt_neon, export=1
        sxtw            x3, w3
        sxtw            x4, w4
        mov             x5, #40*2*4
        add             x1, x1, x4, lsl #3
1:      ld1             {v0.2s}, [x1], x5
        ld1             {v1.s}[0], [x2], #4
        fmul            v2.4s, v0.4s, v1.s[0]
        st1             {v2.2s}, [x0], #8
        subs            x3, x3, #1
        b.gt            1b
        ret
endfunc

function ff_sbr_autocorrelate_neon, export=1
        mov             x2, #38
        movrel          x3, factors
        ld1             {v0.4s}, [x3]
        movi            v1.4s, #0
        movi            v2.4s, #0
        movi            v3.4s, #0
        ld1             {v4.2s}, [x0], #8
        ld1             {v5.2s}, [x0], #8
        fmul            v16.2s, v4.2s, v4.2s
        fmul            v17.2s, v5.2s, v4.s[0]
        fmul            v18.2s, v5.2s, v4.s[1]
1:      ld1             {v5.d}[1], [x0], #8
        fmla            v1.2s, v4.2s, v4.2s
        fmla            v2.4s, v5.4s, v4.s[0]
        fmla            v3.4s, v5.4s, v4.s[1]
        mov             v4.d[0], v5.d[0]
        mov             v5.d[0], v5.d[1]
        subs            x2, x2, #1
        b.gt            1b
        fmul            v19.2s, v4.2s, v4.2s
        fmul            v20.2s, v5.2s, v4.s[0]
        fmul            v21.2s, v5.2s, v4.s[1]
        fadd            v22.4s, v2.4s, v20.4s
        fsub            v22.4s, v22.4s, v17.4s
        fadd            v23.4s, v3.4s, v21.4s
        fsub            v23.4s, v23.4s, v18.4s
        rev64           v23.4s, v23.4s
        fmul            v23.4s, v23.4s, v0.4s
        fadd            v22.4s, v22.4s, v23.4s
        st1             {v22.4s}, [x1], #16
        fadd            v23.2s, v1.2s, v19.2s
        fsub            v23.2s, v23.2s, v16.2s
        faddp           v23.2s, v23.2s, v23.2s
        st1             {v23.s}[0], [x1]
        add             x1, x1, #8
        rev64           v3.2s, v3.2s
        fmul            v3.2s, v3.2s, v0.2s
        fadd            v2.2s, v2.2s, v3.2s
        st1             {v2.2s}, [x1]
        add             x1, x1, #16
        faddp           v1.2s, v1.2s, v1.2s
        st1             {v1.s}[0], [x1]
        ret
endfunc

.macro apply_noise_common
        sxtw            x3, w3
        sxtw            x5, w5
        movrel          x7, X(ff_sbr_noise_table)
        add             x3, x3, #1
1:      and             x3, x3, #0x1ff
        add             x8, x7, x3, lsl #3
        add             x3, x3, #2
        ld1             {v2.4s}, [x0]
        ld1             {v3.2s}, [x1], #8
        ld1             {v4.2s}, [x2], #8
        ld1             {v5.4s}, [x8]
        mov             v6.16b, v2.16b
        zip1            v3.4s, v3.4s, v3.4s
        zip1            v4.4s, v4.4s, v4.4s
        fmla            v6.4s, v1.4s, v3.4s
        fmla            v2.4s, v5.4s, v4.4s
        fcmeq           v7.4s, v3.4s, #0
        bif             v2.16b, v6.16b, v7.16b
        st1             {v2.4s}, [x0], #16
        subs            x5, x5, #2
        b.gt            1b
.endm

function ff_sbr_hf_apply_noise_0_neon, export=1
        movrel          x9, phi_noise_0
        ld1             {v1.4s}, [x9]
        apply_noise_common
        ret
endfunc

function ff_sbr_hf_apply_noise_1_neon, export=1
        movrel          x9, phi_noise_1
        and             x4, x4, #1
        add             x9, x9, x4, lsl #4
        ld1             {v1.4s}, [x9]
        apply_noise_common
        ret
endfunc

function ff_sbr_hf_apply_noise_2_neon, export=1
        movrel          x9, phi_noise_2
        ld1             {v1.4s}, [x9]
        apply_noise_common
        ret
endfunc

function ff_sbr_hf_apply_noise_3_neon, export=1
        movrel          x9, phi_noise_3
        and             x4, x4, #1
        add             x9, x9, x4, lsl #4
        ld1             {v1.4s}, [x9]
        apply_noise_common
        ret
endfunc
