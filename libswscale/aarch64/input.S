/*
 * Copyright (c) 2024 Zhao Zhili <quinkblack@foxmail.com>
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

.macro rgb24_to_yuv_load_rgb, src
        ld3             { v16.16b, v17.16b, v18.16b }, [\src]
        uxtl            v19.8h, v16.8b             // v19: r
        uxtl            v20.8h, v17.8b             // v20: g
        uxtl            v21.8h, v18.8b             // v21: b
        uxtl2           v22.8h, v16.16b            // v22: r
        uxtl2           v23.8h, v17.16b            // v23: g
        uxtl2           v24.8h, v18.16b            // v24: b
.endm

.macro rgb24_to_yuv_product, r, g, b, dst1, dst2, dst, coef0, coef1, coef2, right_shift
        mov             \dst1\().16b, v6.16b                    // dst1 = const_offset
        mov             \dst2\().16b, v6.16b                    // dst2 = const_offset
        smlal           \dst1\().4s, \coef0\().4h, \r\().4h     // dst1 += rx * r
        smlal           \dst1\().4s, \coef1\().4h, \g\().4h     // dst1 += gx * g
        smlal           \dst1\().4s, \coef2\().4h, \b\().4h     // dst1 += bx * b
        smlal2          \dst2\().4s, \coef0\().8h, \r\().8h     // dst2 += rx * r
        smlal2          \dst2\().4s, \coef1\().8h, \g\().8h     // dst2 += gx * g
        smlal2          \dst2\().4s, \coef2\().8h, \b\().8h     // dst2 += bx * b
        sqshrn          \dst\().4h, \dst1\().4s, \right_shift   // dst_lower_half = dst1 >> right_shift
        sqshrn2         \dst\().8h, \dst2\().4s, \right_shift   // dst_higher_half = dst2 >> right_shift
.endm

function ff_rgb24ToY_neon, export=1
        cmp             w4, #0                  // check width > 0
        ldp             w10, w11, [x5]          // w10: ry, w11: gy
        ldr             w12, [x5, #8]           // w12: by
        b.le            3f

        mov             w9, #256                // w9 = 1 << (RGB2YUV_SHIFT - 7)
        movk            w9, #8, lsl #16         // w9 += 32 << (RGB2YUV_SHIFT - 1)
        dup             v6.4s, w9               // w9: const_offset

        cmp             w4, #16
        dup             v0.8h, w10
        dup             v1.8h, w11
        dup             v2.8h, w12
        b.lt            2f
1:
        rgb24_to_yuv_load_rgb x1
        rgb24_to_yuv_product v19, v20, v21, v25, v26, v16, v0, v1, v2, #9
        rgb24_to_yuv_product v22, v23, v24, v27, v28, v17, v0, v1, v2, #9
        sub             w4, w4, #16             // width -= 16
        add             x1, x1, #48             // src += 48
        cmp             w4, #16                 // width >= 16 ?
        stp             q16, q17, [x0], #32     // store to dst
        b.ge            1b
        cbz             x4, 3f
2:
        ldrb            w13, [x1]               // w13: r
        ldrb            w14, [x1, #1]           // w14: g
        ldrb            w15, [x1, #2]           // w15: b

        smaddl          x13, w13, w10, x9       // x13 = ry * r + const_offset
        smaddl          x13, w14, w11, x13      // x13 += gy * g
        smaddl          x13, w15, w12, x13      // x13 += by * b
        asr             w13, w13, #9            // x13 >>= 9
        sub             w4, w4, #1              // width--
        add             x1, x1, #3              // src += 3
        strh            w13, [x0], #2           // store to dst
        cbnz            w4, 2b
3:
        ret
endfunc

.macro rgb24_load_uv_coeff half
        ldp             w10, w11, [x6, #12]     // w10: ru, w11: gu
        ldp             w12, w13, [x6, #20]     // w12: bu, w13: rv
        ldp             w14, w15, [x6, #28]     // w14: gv, w15: bv
    .if \half
        mov             w9, #512
        movk            w9, #128, lsl #16       // w9: const_offset
    .else
        mov             w9, #256
        movk            w9, #64, lsl #16        // w9: const_offset
    .endif
        dup             v0.8h, w10
        dup             v1.8h, w11
        dup             v2.8h, w12
        dup             v3.8h, w13
        dup             v4.8h, w14
        dup             v5.8h, w15
        dup             v6.4s, w9
.endm

function ff_rgb24ToUV_half_neon, export=1
        cmp             w5, #0          // check width > 0
        b.le            3f

        cmp             w5, #8
        rgb24_load_uv_coeff half=1
        b.lt            2f
1:
        ld3             { v16.16b, v17.16b, v18.16b }, [x3]
        uaddlp          v19.8h, v16.16b         // v19: r
        uaddlp          v20.8h, v17.16b         // v20: g
        uaddlp          v21.8h, v18.16b         // v21: b

        rgb24_to_yuv_product v19, v20, v21, v22, v23, v16, v0, v1, v2, #10
        rgb24_to_yuv_product v19, v20, v21, v24, v25, v17, v3, v4, v5, #10
        sub             w5, w5, #8              // width -= 8
        add             x3, x3, #48             // src += 48
        cmp             w5, #8                  // width >= 8 ?
        str             q16, [x0], #16          // store dst_u
        str             q17, [x1], #16          // store dst_v
        b.ge            1b
        cbz             w5, 3f
2:
        ldrb            w2, [x3]                // w2: r1
        ldrb            w4, [x3, #3]            // w4: r2
        add             w2, w2, w4              // w2 = r1 + r2

        ldrb            w4, [x3, #1]            // w4: g1
        ldrb            w7, [x3, #4]            // w7: g2
        add             w4, w4, w7              // w4 = g1 + g2

        ldrb            w7, [x3, #2]            // w7: b1
        ldrb            w8, [x3, #5]            // w8: b2
        add             w7, w7, w8              // w7 = b1 + b2

        smaddl          x8, w2, w10, x9         // dst_u = ru * r + const_offset
        smaddl          x8, w4, w11, x8         // dst_u += gu * g
        smaddl          x8, w7, w12, x8         // dst_u += bu * b
        asr             x8, x8, #10             // dst_u >>= 10
        strh            w8, [x0], #2            // store dst_u

        smaddl          x8, w2, w13, x9         // dst_v = rv * r + const_offset
        smaddl          x8, w4, w14, x8         // dst_v += gv * g
        smaddl          x8, w7, w15, x8         // dst_v += bv * b
        asr             x8, x8, #10             // dst_v >>= 10
        sub             w5, w5, #1
        add             x3, x3, #6              // src += 6
        strh            w8, [x1], #2            // store dst_v
        cbnz            w5, 2b
3:
        ret
endfunc

function ff_rgb24ToUV_neon, export=1
        cmp             w5, #0                  // check width > 0
        b.le            3f

        cmp             w5, #16
        rgb24_load_uv_coeff half=0
        b.lt            2f
1:
        rgb24_to_yuv_load_rgb x3
        rgb24_to_yuv_product v19, v20, v21, v25, v26, v16, v0, v1, v2, #9
        rgb24_to_yuv_product v22, v23, v24, v27, v28, v17, v0, v1, v2, #9
        rgb24_to_yuv_product v19, v20, v21, v25, v26, v18, v3, v4, v5, #9
        rgb24_to_yuv_product v22, v23, v24, v27, v28, v19, v3, v4, v5, #9
        sub             w5, w5, #16
        add             x3, x3, #48             // src += 48
        cmp             w5, #16
        stp             q16, q17, [x0], #32     // store to dst_u
        stp             q18, q19, [x1], #32     // store to dst_v
        b.ge            1b
        cbz             w5, 3f
2:
        ldrb            w16, [x3]               // w16: r
        ldrb            w17, [x3, #1]           // w17: g
        ldrb            w4, [x3, #2]            // w4: b

        smaddl          x8, w16, w10, x9        // x8 = ru * r + const_offset
        smaddl          x8, w17, w11, x8        // x8 += gu * g
        smaddl          x8, w4, w12, x8         // x8 += bu * b
        asr             w8, w8, #9              // x8 >>= 9
        strh            w8, [x0], #2            // store to dst_u

        smaddl          x8, w16, w13, x9        // x8 = rv * r + const_offset
        smaddl          x8, w17, w14, x8        // x8 += gv * g
        smaddl          x8, w4, w15, x8         // x8 += bv * b
        asr             w8, w8, #9              // x8 >>= 9
        sub             w5, w5, #1              // width--
        add             x3, x3, #3              // src += 3
        strh            w8, [x1], #2            // store to dst_v
        cbnz            w5, 2b
3:
        ret
endfunc
