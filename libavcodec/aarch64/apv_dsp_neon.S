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
#include "neon.S"

/* Dequant one row in 16x8
 * Input levels at x2 + off, qmatrix at x3 + off (function args)
 * Output row in out vector
 */
.macro dequant out, off
        ldr             q16, [x2, \off]
        ldr             q17, [x3, \off]
        smull           v18.4s, v16.4h, v17.4h
        smull2          v19.4s, v16.8h, v17.8h
        sshl            v18.4s, v18.4s, v24.4s
        sshl            v19.4s, v19.4s, v24.4s
        add             v18.4s, v18.4s, v25.4s
        add             v19.4s, v19.4s, v25.4s
        sshl            v18.4s, v18.4s, v26.4s
        sshl            v19.4s, v19.4s, v26.4s
        sqxtn           \out\().4h, v18.4s
        sqxtn2          \out\().8h, v19.4s
.endm


/* Vertical butterfly of 8x16x8 -> 16x32x4
 * Input rows in v0-v7
 * Output rows in pairs v16-v23
 */
.macro butterfly_mul
        movi            v16.8h, #64
        mvni            v17.8h, #63 /* -64 */
        movi            v18.8h, #84
        movi            v19.8h, #35
        mvni            v20.8h, #83 /* -84 */
        smull           v24.4s, v0.4h, v16.4h
        smlal           v24.4s, v4.4h, v16.4h
        smull2          v25.4s, v0.8h, v16.8h
        smlal2          v25.4s, v4.8h, v16.8h
        smull           v26.4s, v0.4h, v16.4h
        smlal           v26.4s, v4.4h, v17.4h
        smull2          v27.4s, v0.8h, v16.8h
        smlal2          v27.4s, v4.8h, v17.8h
        smull           v28.4s, v2.4h, v18.4h
        smlal           v28.4s, v6.4h, v19.4h
        smull2          v29.4s, v2.8h, v18.8h
        smlal2          v29.4s, v6.8h, v19.8h
        smull           v30.4s, v2.4h, v19.4h
        smlal           v30.4s, v6.4h, v20.4h
        smull2          v31.4s, v2.8h, v19.8h
        smlal2          v31.4s, v6.8h, v20.8h
        movi            v0.8h, #89
        movi            v2.8h, #75
        movi            v4.8h, #50
        movi            v6.8h, #18
        smull           v16.4s, v1.4h, v0.4h
        smull2          v17.4s, v1.8h, v0.8h
        smlal           v16.4s, v3.4h, v2.4h
        smlal2          v17.4s, v3.8h, v2.8h
        smlal           v16.4s, v5.4h, v4.4h
        smlal2          v17.4s, v5.8h, v4.8h
        smlal           v16.4s, v7.4h, v6.4h
        smlal2          v17.4s, v7.8h, v6.8h
        smull           v18.4s, v1.4h, v2.4h
        smull2          v19.4s, v1.8h, v2.8h
        smull           v20.4s, v1.4h, v4.4h
        smull2          v21.4s, v1.8h, v4.8h
        smlal           v20.4s, v5.4h, v6.4h
        smlal2          v21.4s, v5.8h, v6.8h
        smlal           v20.4s, v7.4h, v2.4h
        smlal2          v21.4s, v7.8h, v2.8h
        smull           v22.4s, v1.4h, v6.4h
        smull2          v23.4s, v1.8h, v6.8h
        smlal           v22.4s, v5.4h, v2.4h
        smlal2          v23.4s, v5.8h, v2.8h
        mvni            v0.8h, #17 /* -18 */
        mvni            v2.8h, #88 /* -89 */
        mvni            v4.8h, #49 /* -50 */
        smlal           v18.4s, v3.4h, v0.4h
        smlal2          v19.4s, v3.8h, v0.8h
        smlal           v18.4s, v5.4h, v2.4h
        smlal2          v19.4s, v5.8h, v2.8h
        smlal           v18.4s, v7.4h, v4.4h
        smlal2          v19.4s, v7.8h, v4.8h
        smlal           v20.4s, v3.4h, v2.4h
        smlal2          v21.4s, v3.8h, v2.8h
        smlal           v22.4s, v3.4h, v4.4h
        smlal2          v23.4s, v3.8h, v4.8h
        smlal           v22.4s, v7.4h, v2.4h
        smlal2          v23.4s, v7.8h, v2.8h
.endm

/* Column combine and renormalise 16x32x4 -> 8x16x8
 * Input rows in pairs v16-v31
 * Output rows in v0-v7
 */
.macro col_combine
        add             v2.4s, v24.4s, v28.4s
        add             v3.4s, v25.4s, v29.4s
        add             v0.4s, v16.4s, v2.4s
        add             v4.4s, v17.4s, v3.4s
        sqrshrn         v0.4h, v0.4s, #7
        sqrshrn2        v0.8h, v4.4s, #7
        sub             v7.4s, v2.4s, v16.4s
        sub             v4.4s, v3.4s, v17.4s
        sqrshrn         v7.4h, v7.4s, #7
        sqrshrn2        v7.8h, v4.4s, #7
        sub             v2.4s, v24.4s, v28.4s
        sub             v1.4s, v25.4s, v29.4s
        add             v3.4s, v22.4s, v2.4s
        add             v4.4s, v23.4s, v1.4s
        sqrshrn         v3.4h, v3.4s, #7
        sqrshrn2        v3.8h, v4.4s, #7
        sub             v4.4s, v2.4s, v22.4s
        sub             v1.4s, v1.4s, v23.4s
        sqrshrn         v4.4h, v4.4s, #7
        sqrshrn2        v4.8h, v1.4s, #7
        add             v2.4s, v26.4s, v30.4s
        add             v1.4s, v27.4s, v31.4s
        add             v17.4s, v19.4s, v1.4s
        add             v1.4s,  v18.4s, v2.4s
        sqrshrn         v1.4h, v1.4s,  #7
        sqrshrn2        v1.8h, v17.4s, #7
        sub             v6.4s, v2.4s, v18.4s
        add             v17.4s, v27.4s, v31.4s
        sub             v17.4s, v17.4s, v19.4s
        sqrshrn         v6.4h, v6.4s,  #7
        sqrshrn2        v6.8h, v17.4s, #7
        sub             v2.4s,  v26.4s, v30.4s
        sub             v18.4s, v27.4s, v31.4s
        add             v2.4s,  v20.4s, v2.4s
        add             v18.4s, v21.4s, v18.4s
        sqrshrn         v2.4h, v2.4s,  #7
        sqrshrn2        v2.8h, v18.4s, #7
        sub             v5.4s,  v26.4s, v30.4s
        sub             v5.4s,  v5.4s,  v20.4s
        sub             v18.4s, v27.4s, v31.4s
        sub             v18.4s, v18.4s, v21.4s
        sqrshrn         v5.4h, v5.4s,  #7
        sqrshrn2        v5.8h, v18.4s, #7
.endm

/* Row combine and output scaling 16x32x4 -> 8x16x8
 * Input rows in pairs v16-v31
 * Output rows in v0-v7
 */
.macro row_combine
        movi            v1.4s, #0
        dup             v7.4s, w17
        add             v2.4s, v24.4s, v28.4s
        add             v3.4s, v25.4s, v29.4s
        add             v0.4s, v16.4s, v2.4s
        add             v4.4s, v17.4s, v3.4s
        add             v0.4s, v0.4s,  v5.4s
        add             v4.4s, v4.4s,  v5.4s
        sshl            v0.4s, v0.4s, v7.4s
        sshl            v4.4s, v4.4s, v7.4s
        smin            v0.4s, v0.4s, v6.4s
        smin            v4.4s, v4.4s, v6.4s
        smax            v0.4s, v0.4s, v1.4s
        smax            v4.4s, v4.4s, v1.4s
        uqxtn           v0.4h, v0.4s
        uqxtn2          v0.8h, v4.4s
        sub             v4.4s, v3.4s, v17.4s
        sub             v7.4s, v2.4s, v16.4s
        dup             v16.4s, w17
        add             v7.4s, v7.4s, v5.4s
        add             v4.4s, v4.4s, v5.4s
        sshl            v7.4s, v7.4s, v16.4s
        sshl            v4.4s, v4.4s, v16.4s
        smin            v7.4s, v7.4s, v6.4s
        smin            v4.4s, v4.4s, v6.4s
        smax            v7.4s, v7.4s, v1.4s
        smax            v4.4s, v4.4s, v1.4s
        uqxtn           v7.4h, v7.4s
        uqxtn2          v7.8h, v4.4s
        sub             v2.4s,  v24.4s, v28.4s
        sub             v17.4s, v25.4s, v29.4s
        add             v3.4s,  v22.4s, v2.4s
        add             v17.4s, v23.4s, v17.4s
        add             v3.4s,  v3.4s,  v5.4s
        add             v17.4s, v17.4s, v5.4s
        sshl            v3.4s,  v3.4s,  v16.4s
        sshl            v17.4s, v17.4s, v16.4s
        smin            v3.4s,  v3.4s,  v6.4s
        smin            v17.4s, v17.4s, v6.4s
        smax            v3.4s,  v3.4s,  v1.4s
        smax            v17.4s, v17.4s, v1.4s
        uqxtn           v3.4h, v3.4s
        uqxtn2          v3.8h, v17.4s
        sub             v4.4s,  v2.4s,  v22.4s
        sub             v17.4s, v25.4s, v29.4s
        sub             v17.4s, v17.4s, v23.4s
        add             v4.4s,  v4.4s,  v5.4s
        add             v17.4s, v17.4s, v5.4s
        sshl            v4.4s,  v4.4s,  v16.4s
        sshl            v17.4s, v17.4s, v16.4s
        smin            v4.4s,  v4.4s,  v6.4s
        smin            v17.4s, v17.4s, v6.4s
        smax            v4.4s,  v4.4s,  v1.4s
        smax            v17.4s, v17.4s, v1.4s
        uqxtn           v4.4h, v4.4s
        uqxtn2          v4.8h, v17.4s
        dup             v28.4s, w10
        dup             v29.4s, w13
        sub             v22.4s, v29.4s, v29.4s
        add             v2.4s, v26.4s, v30.4s
        add             v1.4s, v27.4s, v31.4s
        add             v17.4s, v19.4s, v1.4s
        add             v1.4s,  v18.4s, v2.4s
        add             v1.4s,  v1.4s,  v28.4s
        add             v17.4s, v17.4s, v28.4s
        sshl            v1.4s,  v1.4s,  v16.4s
        sshl            v17.4s, v17.4s, v16.4s
        smin            v1.4s,  v1.4s,  v29.4s
        smin            v17.4s, v17.4s, v29.4s
        smax            v1.4s,  v1.4s,  v22.4s
        smax            v17.4s, v17.4s, v22.4s
        uqxtn           v1.4h, v1.4s
        uqxtn2          v1.8h, v17.4s
        sub             v6.4s, v2.4s, v18.4s
        add             v17.4s, v27.4s, v31.4s
        sub             v17.4s, v17.4s, v19.4s
        add             v6.4s,  v6.4s,  v28.4s
        add             v17.4s, v17.4s, v28.4s
        sshl            v6.4s,  v6.4s,  v16.4s
        sshl            v17.4s, v17.4s, v16.4s
        smin            v6.4s,  v6.4s,  v29.4s
        smin            v17.4s, v17.4s, v29.4s
        smax            v6.4s,  v6.4s,  v22.4s
        smax            v17.4s, v17.4s, v22.4s
        uqxtn           v6.4h, v6.4s
        uqxtn2          v6.8h, v17.4s
        sub             v2.4s,  v26.4s, v30.4s
        sub             v18.4s, v27.4s, v31.4s
        add             v17.4s, v21.4s, v18.4s
        add             v2.4s,  v20.4s, v2.4s
        add             v2.4s,  v2.4s,  v28.4s
        add             v17.4s, v17.4s, v28.4s
        sshl            v2.4s,  v2.4s,  v16.4s
        sshl            v17.4s, v17.4s, v16.4s
        smin            v2.4s,  v2.4s,  v29.4s
        smin            v17.4s, v17.4s, v29.4s
        smax            v2.4s,  v2.4s,  v22.4s
        smax            v17.4s, v17.4s, v22.4s
        uqxtn           v2.4h, v2.4s
        uqxtn2          v2.8h, v17.4s
        sub             v18.4s, v27.4s, v31.4s
        sub             v18.4s, v18.4s, v21.4s
        sub             v5.4s,  v26.4s, v30.4s
        sub             v5.4s,  v5.4s,  v20.4s
        add             v5.4s,  v5.4s,  v28.4s
        add             v18.4s, v18.4s, v28.4s
        sshl            v5.4s,  v5.4s,  v16.4s
        sshl            v18.4s, v18.4s, v16.4s
        smin            v5.4s,  v5.4s,  v29.4s
        smin            v18.4s, v18.4s, v29.4s
        smax            v5.4s,  v5.4s,  v22.4s
        smax            v18.4s, v18.4s, v22.4s
        uqxtn           v5.4h, v5.4s
        uqxtn2          v5.8h, v18.4s
.endm

/*
 * void ff_apv_decode_transquant_neon(void *output,           = x0
 *                                    ptrdiff_t pitch,        = x1
 *                                    const int16_t *input,   = x2
 *                                    const int16_t *qmatrix, = x3
 *                                    int bit_depth,          = w4
 *                                    int qp_shift)           = w5
 */
function ff_apv_decode_transquant_neon, export=1
        sub             w7, w4, #2       /* w7 = bd_shift */
        movz            w14, #1

        /* Dequant */
        sub             w15, w7, #1
        lsl             w10, w14, w15
        neg             w15, w7
        dup             v24.4s, w5       /* v24 = qp_shift */
        dup             v25.4s, w10      /* v25 = 1 << (bd_shift - 1) */
        dup             v26.4s, w15      /* v26 = -bd_shift */
        dequant         v0, #0
        dequant         v1, #16
        dequant         v2, #32
        dequant         v3, #48
        dequant         v4, #64
        dequant         v5, #80
        dequant         v6, #96
        dequant         v7, #112

        /* Row scaling constants */
        movz            w16, #8, lsl #16 /* w16 = 1 << 19 */
        lsr             w10, w16, w4
        orr             w10, w10, w16    /* w10 = (1 << (19 - bit_depth)) + (1 << 19) */
        lsl             w15, w14, w4
        sub             w13, w15, #1     /* w13 = (1 << bit_depth) - 1 */

        /* Column butterfly and normalise */
        butterfly_mul
        col_combine

        /* Transpose */
        transpose_8x8H  v0, v1, v2, v3, v4, v5, v6, v7, v16, v17

        /* Row butterfly and output scaling */
        butterfly_mul
        dup             v5.4s, w10       /* v5 = output offset */
        dup             v6.4s, w13       /* v6 = output max clip (min clip is zero) */
        sub             w17, w4, #20     /* w17 = bit_depth - 20 (row_combine shift) */
        row_combine

        /* Transpose back */
        transpose_8x8H  v0, v1, v2, v3, v4, v5, v6, v7, v16, v17

        /* Store output */
        st1             {v0.16b}, [x0], x1
        st1             {v1.16b}, [x0], x1
        st1             {v2.16b}, [x0], x1
        st1             {v3.16b}, [x0], x1
        st1             {v4.16b}, [x0], x1
        st1             {v5.16b}, [x0], x1
        st1             {v6.16b}, [x0], x1
        st1             {v7.16b}, [x0]
        ret
endfunc
