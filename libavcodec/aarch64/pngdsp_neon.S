/*
 * Copyright (c) 2026 Zhao Zhili <zhilizhao@tencent.com>
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

/* void ff_png_add_bytes_l2_neon(uint8_t *dst, const uint8_t *src1,
 *                               const uint8_t *src2, int w);
 * x0: dst
 * x1: src1
 * x2: src2
 * w3: w
 */
function ff_png_add_bytes_l2_neon, export=1
        bic             w4, w3, #63
        and             w3, w3, #63
        cbz             w4, 2f
1:
        // 64 bytes per loop iteration
        ldp             q0, q1, [x1]
        ldp             q2, q3, [x1, #32]
        subs            w4, w4, #64
        ldp             q4, q5, [x2]
        ldp             q6, q7, [x2, #32]
        add             x1, x1, #64
        add             v0.16b, v0.16b, v4.16b
        add             v1.16b, v1.16b, v5.16b
        add             x2, x2, #64
        add             v2.16b, v2.16b, v6.16b
        add             v3.16b, v3.16b, v7.16b
        st1             {v0.16b - v3.16b}, [x0], #64
        b.ne            1b
2:
        bic             w4, w3, #15
        and             w3, w3, #15
        cbz             w4, 4f
3:
        // 16 bytes per loop iteration
        ld1             {v0.16b}, [x1], #16
        ld1             {v4.16b}, [x2], #16
        subs            w4, w4, #16
        add             v0.16b, v0.16b, v4.16b
        st1             {v0.16b}, [x0], #16
        b.ne            3b
4:
        cbz             w3, 6f
5:
        ldrb            w5, [x1], #1
        ldrb            w6, [x2], #1
        subs            w3, w3, #1
        add             w5, w5, w6
        strb            w5, [x0], #1
        b.ne            5b
6:
        ret
endfunc

/* This is an iterative process where dst[n] depends on dst[n-bpp], so
 * add_paeth_prediction can only process bpp bytes each time.
 *
 * There are three state
 * 1. load: load data from memory
 * 2. shift: simple shift from previous iteration
 * 3. extract: extract data from registers which was loaded in state 1.
 *      Data is assembled by the caller.
 */
.macro add_paeth_prediction, bpp, state
        // load data from memory
.ifc \state,load
        ld1             {v18.16b, v19.16b, v20.16b}, [x2], x7
        ld1             {v21.16b, v22.16b, v23.16b}, [x1], x7
        mov             v1.8b, v18.8b                           // c = top[i - bpp]
        ext             v2.16b, v18.16b, v19.16b, #\bpp         // b = top[i]
        mov             v17.16b, v21.16b                        // src
.endif
        // simple shift from previous iteration
.ifc \state,shift
        mov             v1.8b, v2.8b
        ext             v2.16b, v2.16b, v2.16b, #(\bpp)
        ext             v17.16b, v17.16b, v17.16b, #(\bpp)
.endif
        // Only the first bpp bytes are useful.
        uabd            v4.8b, v2.8b, v1.8b         // pa = abs(b - c)
        uaddl           v7.8h, v1.8b, v1.8b         // 2 * c
        uabd            v3.8b, v0.8b, v1.8b         // pb = abs(a - c)
        uaddl           v5.8h, v0.8b, v2.8b         // a + b

        cmhs            v16.8b, v3.8b, v4.8b        // pb >= pa
        uabd            v5.8h, v5.8h, v7.8h
        umin            v6.8b, v4.8b, v3.8b         // min(pa, pb)
        uqxtn           v5.8b, v5.8h

        bsl             v16.8b, v0.8b, v2.8b        // pb >= pa ? a : b
        cmhs            v6.8b, v5.8b, v6.8b         // pc >= min(pa, pb)
        bsl             v6.8b, v16.8b, v1.8b        // pc >= min ? (a or b) : c

        add             v0.8b, v6.8b, v17.8b
.if \bpp == 3 || \bpp == 4
        str             s0, [x0], #\bpp
.else
        str             d0, [x0], #\bpp
.endif
.endm

/* void ff_png_add_paeth_prediction_neon(uint8_t *dst, const uint8_t *src,
 *                                       const uint8_t *top, int w, int bpp);
 * x0: dst
 * x1: src
 * x2: top
 * w3: w
 * w4: bpp
 */
function ff_png_add_paeth_prediction_neon, export=1
        cmp             w4, #3
        /* Load 48 bytes from memory in each loop.
         * The number of bytes processed in each loop is (48 - bpp)
         */
        mov             w7, #48
        /* Overwrite 1 byte in SIMD when bpp = 3, and 2 bytes when bpp = 6.
         * Let w5 = (w - 2) / (48 -bpp) * (48 -bpp), then fix the overwrite
         * in loop tail.
         */
        sub             w5, w3, #2
        sub             w7, w7, w4              // (48 - bpp)
        udiv            w5, w5, w7
        neg             w6, w4                  // -bpp
        sub             x2, x2, w4, uxtw        // top - bpp
        mul             w5, w5, w7              // w5 = (w - 2) / (48 - bpp) * (48 - bpp)
        sub             w3, w3, w5
        cbz             w5, 2f

        ldr             d0, [x0, w6, sxtw]

        b.gt            40f
30:     // bpp = 3
        // 15 bytes
        add_paeth_prediction 3, state=load
        subs            w5, w5, w7
.rept   4
        add_paeth_prediction 3, state=shift
.endr
        // 15 + 15 = 30 bytes
        ext             v1.16b, v18.16b, v19.16b, #15
        ext             v2.16b, v19.16b, v20.16b, #2
        ext             v17.16b, v21.16b, v22.16b, #15
        add_paeth_prediction 3, state=extract
.rept   4
        add_paeth_prediction 3, state=shift
.endr
        // 30 + 15 = 45 bytes
        ext             v1.16b, v19.16b, v20.16b, #14
        ext             v2.16b, v20.16b, v20.16b, #1
        ext             v17.16b, v22.16b, v23.16b, #14
        add_paeth_prediction 3, state=extract
.rept   4
        add_paeth_prediction 3, state=shift
.endr
        b.ne            30b
        b               2f

40:     // check bpp = 4
        cmp             w4, #4
        b.gt            60f
        // 44 bytes per loop
41:
        // 16 bytes
        add_paeth_prediction 4, state=load
        subs            w5, w5, w7
.rept   3
        add_paeth_prediction 4, state=shift
.endr
        // 16 + 16 = 32 bytes
        mov             v1.8b, v19.8b
        ext             v2.16b, v19.16b, v20.16b, #4
        mov             v17.16b, v22.16b
        add_paeth_prediction 4, state=extract
.rept   3
        add_paeth_prediction 4, state=shift
.endr
        // 32 + 12 bytes
        mov             v1.8b, v20.8b
        ext             v2.16b, v20.16b, v20.16b, #4
        mov             v17.16b, v23.16b
        add_paeth_prediction 4, state=extract
.rept   2
        add_paeth_prediction 4, state=shift
.endr
        b.ne            41b
        b               2f

60:     // check bpp = 6
        cmp             w4, #6
        b.gt            80f
61:
        // process 12 bytes
        add_paeth_prediction 6, state=load
        add_paeth_prediction 6, state=shift
        subs            w5, w5, w7

        // 12 + 12 = 24 bytes
        ext             v1.16b, v18.16b, v19.16b, #12
        ext             v2.16b, v19.16b, v20.16b, #2
        ext             v17.16b, v21.16b, v22.16b, #12
        add_paeth_prediction 6, state=extract
        add_paeth_prediction 6, state=shift
        // 24 + 12 = 36 bytes
        ext             v1.16b, v19.16b, v20.16b, #8
        ext             v2.16b, v19.16b, v20.16b, #14
        ext             v17.16b, v22.16b, v23.16b, #8
        add_paeth_prediction 6, state=extract
        add_paeth_prediction 6, state=shift
        // 36 + 6 = 42 bytes
        ext             v1.16b, v20.16b, v20.16b, #4
        ext             v2.16b, v20.16b, v20.16b, #10
        ext             v17.16b, v23.16b, v23.16b, #4
        add_paeth_prediction 6, state=extract

        b.ne            61b
        b               2f

80:     // 40 bytes per loop
        // 16 bytes
        add_paeth_prediction 8, state=load
        add_paeth_prediction 8, state=shift
        subs            w5, w5, w7

        // 16 + 16 = 32 bytes
        mov             v1.8b, v19.8b
        ext             v2.16b, v19.16b, v20.16b, #8
        mov             v17.16b, v22.16b
        add_paeth_prediction 8, state=extract
        add_paeth_prediction 8, state=shift

        // 32 + 8 = 40 bytes
        mov             v1.8b, v20.8b
        ext             v2.16b, v20.16b, v20.16b, #8
        mov             v17.8b, v23.8b
        add_paeth_prediction 8, state=extract

        b.ne            80b
2:
        cbz             w3, 8f
3:
        ldrb            w7, [x0, w6, sxtw]          // a = dst[i - bpp]
        ldrb            w8, [x2, w4, uxtw]          // b = top[i]
        ldrb            w9, [x2], #1                // c = top[i - bpp]

        sub             w10, w8, w9                 // p = b - c
        sub             w11, w7, w9                 // a - c

        cmp             w10, #0
        cneg            w12, w10, lt                // pa = abs(b - c)
        cmp             w11, #0
        add             w14, w10, w11
        cneg            w13, w11, lt                // pb = abs(a - c)
        cmp             w14, #0
        cneg            w14, w14, lt                // pc = abs(a + b - 2*c)

        ldrb            w16, [x1], #1

        cmp             w13, w14                    // pb vs pc
        csel            w15, w8, w9, le             // w15 = (pb <= pc) ? b : c
        cmp             w12, w13                    // pa vs pb
        ccmp            w12, w14, #2, le            // if pa <= pb, check pa vs pc
        csel            w15, w7, w15, le            // p = (pa <= pb && pa <= pc) ? a : w15

        subs            w3, w3, #1
        add             w15, w15, w16
        strb            w15, [x0], #1
        b.ne            3b
8:
        ret
endfunc
