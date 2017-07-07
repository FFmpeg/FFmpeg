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
1:      ld1             {v0.4S}, [x0]
        ld1             {v1.4S}, [x1], #16
        fadd            v0.4S, v0.4S, v1.4S
        ld1             {v2.4S}, [x2], #16
        fadd            v0.4S, v0.4S, v2.4S
        ld1             {v3.4S}, [x3], #16
        fadd            v0.4S, v0.4S, v3.4S
        ld1             {v4.4S}, [x4], #16
        fadd            v0.4S, v0.4S, v4.4S
        st1             {v0.4S}, [x0], #16
        subs            x5, x5, #4
        b.gt            1b
        ret
endfunc

function ff_sbr_sum_square_neon, export=1
        movi            v0.4S, #0
1:      ld1             {v1.4S}, [x0], #16
        fmla            v0.4S, v1.4S, v1.4S
        subs            w1, w1, #2
        b.gt            1b
        faddp           v0.4S, v0.4S, v0.4S
        faddp           v0.4S, v0.4S, v0.4S
        ret
endfunc

function ff_sbr_neg_odd_64_neon, export=1
        mov             x1, x0
        movi            v5.4S, #1<<7, lsl #24
        ld2             {v0.4S, v1.4S}, [x0], #32
        eor             v1.16B, v1.16B, v5.16B
        ld2             {v2.4S, v3.4S}, [x0], #32
.rept 3
        st2             {v0.4S, v1.4S}, [x1], #32
        eor             v3.16B, v3.16B, v5.16B
        ld2             {v0.4S, v1.4S}, [x0], #32
        st2             {v2.4S, v3.4S}, [x1], #32
        eor             v1.16B, v1.16B, v5.16B
        ld2             {v2.4S, v3.4S}, [x0], #32
.endr
        eor             v3.16B, v3.16B, v5.16B
        st2             {v0.4S, v1.4S}, [x1], #32
        st2             {v2.4S, v3.4S}, [x1], #32
        ret
endfunc

function ff_sbr_qmf_pre_shuffle_neon, export=1
        add             x1, x0, #60*4
        add             x2, x0, #64*4
        mov             x3, #-16
        mov             x4, #-4
        movi            v6.4S, #1<<7, lsl #24
        ld1             {v0.2S}, [x0], #8
        st1             {v0.2S}, [x2], #8
.rept 7
        ld1             {v1.4S}, [x1], x3
        ld1             {v2.4S}, [x0], #16
        eor             v1.16B, v1.16B, v6.16B
        rev64           v1.4S, v1.4S
        ext             v1.16B, v1.16B, v1.16B, #8
        st2             {v1.4S, v2.4S}, [x2], #32
.endr
        add             x1, x1, #8
        ld1             {v1.2S}, [x1], x4
        ld1             {v2.2S}, [x0], #8
        ld1             {v1.S}[3], [x1]
        ld1             {v2.S}[2], [x0]
        eor             v1.16B, v1.16B, v6.16B
        rev64           v1.4S, v1.4S
        st2             {v1.2S, v2.2S}, [x2], #16
        st2             {v1.S, v2.S}[2], [x2]
        ret
endfunc

function ff_sbr_qmf_post_shuffle_neon, export=1
        add             x2, x1, #60*4
        mov             x3, #-16
        mov             x4, #32
        movi            v6.4S, #1<<7, lsl #24
1:      ld1             {v0.4S}, [x2], x3
        ld1             {v1.4S}, [x1], #16
        eor             v0.16B, v0.16B, v6.16B
        rev64           v0.4S, v0.4S
        ext             v0.16B, v0.16B, v0.16B, #8
        st2             {v0.4S, v1.4S}, [x0], #32
        subs            x4, x4, #4
        b.gt            1b
        ret
endfunc

function ff_sbr_qmf_deint_neg_neon, export=1
        add             x1, x1, #56*4
        add             x2, x0, #60*4
        mov             x3, #-32
        mov             x4, #32
        movi            v2.4S, #1<<7, lsl #24
1:      ld2             {v0.4S, v1.4S}, [x1], x3
        eor             v0.16B, v0.16B, v2.16B
        rev64           v1.4S, v1.4S
        ext             v1.16B, v1.16B, v1.16B, #8
        st1             {v0.4S}, [x2]
        st1             {v1.4S}, [x0], #16
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
1:      ld1             {v0.4S}, [x1], #16
        ld1             {v1.4S}, [x2], x5
        rev64           v2.4S, v0.4S
        ext             v2.16B, v2.16B, v2.16B, #8
        rev64           v3.4S, v1.4S
        ext             v3.16B, v3.16B, v3.16B, #8
        fadd            v1.4S, v1.4S, v2.4S
        fsub            v0.4S, v0.4S, v3.4S
        st1             {v0.4S}, [x0], #16
        st1             {v1.4S}, [x3], x5
        subs            x4, x4, #4
        b.gt            1b
        ret
endfunc

function ff_sbr_hf_gen_neon, export=1
        sxtw            x4, w4
        sxtw            x5, w5
        movrel          x6, factors
        ld1             {v7.4S}, [x6]
        dup             v1.4S, v0.S[0]
        mov             v2.8B, v1.8B
        mov             v2.S[2], v7.S[0]
        mov             v2.S[3], v7.S[0]
        fmul            v1.4S, v1.4S, v2.4S
        ld1             {v0.D}[0], [x3]
        ld1             {v0.D}[1], [x2]
        fmul            v0.4S, v0.4S, v1.4S
        fmul            v1.4S, v0.4S, v7.4S
        rev64           v0.4S, v0.4S
        sub             x7, x5, x4
        add             x0, x0, x4, lsl #3
        add             x1, x1, x4, lsl #3
        sub             x1, x1, #16
1:      ld1             {v2.4S}, [x1], #16
        ld1             {v3.2S}, [x1]
        fmul            v4.4S, v2.4S, v1.4S
        fmul            v5.4S, v2.4S, v0.4S
        faddp           v4.4S, v4.4S, v4.4S
        faddp           v5.4S, v5.4S, v5.4S
        faddp           v4.4S, v4.4S, v4.4S
        faddp           v5.4S, v5.4S, v5.4S
        mov             v4.S[1], v5.S[0]
        fadd            v4.2S, v4.2S, v3.2S
        st1             {v4.2S}, [x0], #8
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
1:      ld1             {v0.2S}, [x1], x5
        ld1             {v1.S}[0], [x2], #4
        fmul            v2.4S, v0.4S, v1.S[0]
        st1             {v2.2S}, [x0], #8
        subs            x3, x3, #1
        b.gt            1b
        ret
endfunc

function ff_sbr_autocorrelate_neon, export=1
        mov             x2, #38
        movrel          x3, factors
        ld1             {v0.4S}, [x3]
        movi            v1.4S, #0
        movi            v2.4S, #0
        movi            v3.4S, #0
        ld1             {v4.2S}, [x0], #8
        ld1             {v5.2S}, [x0], #8
        fmul            v16.2S, v4.2S, v4.2S
        fmul            v17.2S, v5.2S, v4.S[0]
        fmul            v18.2S, v5.2S, v4.S[1]
1:      ld1             {v5.D}[1], [x0], #8
        fmla            v1.2S, v4.2S, v4.2S
        fmla            v2.4S, v5.4S, v4.S[0]
        fmla            v3.4S, v5.4S, v4.S[1]
        mov             v4.D[0], v5.D[0]
        mov             v5.D[0], v5.D[1]
        subs            x2, x2, #1
        b.gt            1b
        fmul            v19.2S, v4.2S, v4.2S
        fmul            v20.2S, v5.2S, v4.S[0]
        fmul            v21.2S, v5.2S, v4.S[1]
        fadd            v22.4S, v2.4S, v20.4S
        fsub            v22.4S, v22.4S, v17.4S
        fadd            v23.4S, v3.4S, v21.4S
        fsub            v23.4S, v23.4S, v18.4S
        rev64           v23.4S, v23.4S
        fmul            v23.4S, v23.4S, v0.4S
        fadd            v22.4S, v22.4S, v23.4S
        st1             {v22.4S}, [x1], #16
        fadd            v23.2S, v1.2S, v19.2S
        fsub            v23.2S, v23.2S, v16.2S
        faddp           v23.2S, v23.2S, v23.2S
        st1             {v23.S}[0], [x1]
        add             x1, x1, #8
        rev64           v3.2S, v3.2S
        fmul            v3.2S, v3.2S, v0.2S
        fadd            v2.2S, v2.2S, v3.2S
        st1             {v2.2S}, [x1]
        add             x1, x1, #16
        faddp           v1.2S, v1.2S, v1.2S
        st1             {v1.S}[0], [x1]
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
        ld1             {v2.4S}, [x0]
        ld1             {v3.2S}, [x1], #8
        ld1             {v4.2S}, [x2], #8
        ld1             {v5.4S}, [x8]
        mov             v6.16B, v2.16B
        zip1            v3.4S, v3.4S, v3.4S
        zip1            v4.4S, v4.4S, v4.4S
        fmla            v6.4S, v1.4S, v3.4S
        fmla            v2.4S, v5.4S, v4.4S
        fcmeq           v7.4S, v3.4S, #0.0
        bif             v2.16B, v6.16B, v7.16B
        st1             {v2.4S}, [x0], #16
        subs            x5, x5, #2
        b.gt            1b
.endm

function ff_sbr_hf_apply_noise_0_neon, export=1
        movrel          x9, phi_noise_0
        ld1             {v1.4S}, [x9]
        apply_noise_common
        ret
endfunc

function ff_sbr_hf_apply_noise_1_neon, export=1
        movrel          x9, phi_noise_1
        and             x4, x4, #1
        add             x9, x9, x4, lsl #4
        ld1             {v1.4S}, [x9]
        apply_noise_common
        ret
endfunc

function ff_sbr_hf_apply_noise_2_neon, export=1
        movrel          x9, phi_noise_2
        ld1             {v1.4S}, [x9]
        apply_noise_common
        ret
endfunc

function ff_sbr_hf_apply_noise_3_neon, export=1
        movrel          x9, phi_noise_3
        and             x4, x4, #1
        add             x9, x9, x4, lsl #4
        ld1             {v1.4S}, [x9]
        apply_noise_common
        ret
endfunc
