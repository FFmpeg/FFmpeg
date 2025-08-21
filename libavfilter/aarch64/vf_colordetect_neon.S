/*
 * Copyright (c) 2025 Zhao Zhili <quinkblack@foxmail.com>
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

#define FF_ALPHA_TRANSPARENT        (1 << 0)
#define FF_ALPHA_STRAIGHT           ((1 << 1) | FF_ALPHA_TRANSPARENT)

const mask
        .byte           255, 255, 255, 255, 255, 255, 255, 255
        .byte           255, 255, 255, 255, 255, 255, 255, 255
mask_start:
        .byte           0, 0, 0, 0, 0, 0, 0, 0
        .byte           0, 0, 0, 0, 0, 0, 0, 0
        .byte           255, 255, 255, 255, 255, 255, 255, 255
        .byte           255, 255, 255, 255, 255, 255, 255, 255
endconst

.macro load_mask_zero, shift=0
        movrel          x9, mask_start
        sub             x9, x9, x7, lsl #(\shift)
        ldr             q3, [x9]
.endm

.macro load_mask, shift=0
        movrel          x9, mask_start
        sub             x9, x9, x7, lsl #(\shift)
        ld1             {v3.16b, v4.16b}, [x9]
.endm

/* x0: const uint8_t *data
 * x1: ptrdiff_t stride
 * x2: ptrdiff_t width
 * x3: ptrdiff_t height
 * w4: int mpeg_min
 * w5: int mpeg_max
 */
function ff_detect_range_neon, export=1
        ands            x7, x2, #15                 // width % 16
        bic             x8, x2, #15                 // width / 16 * 16
        bic             x6, x2, #31                 // width / 32 * 32
        and             x10, x2, #16                // check x8 != x6
        dup             v0.16b, w4                  // mpeg_min
        dup             v1.16b, w5                  // mpeg_max
        movi            v2.16b, #0                  // cond
        sub             x1, x1, x8
        b.eq            1f
        load_mask_zero
1:
        cbz             x6, 20f                     // width < 32
        mov             x12, x6
2:
        ld1             {v5.16b, v6.16b}, [x0], #32
        cmhi            v16.16b, v0.16b, v5.16b
        cmhi            v17.16b, v5.16b, v1.16b
        cmhi            v18.16b, v0.16b, v6.16b
        cmhi            v19.16b, v6.16b, v1.16b
        orr             v20.16b, v16.16b, v17.16b
        orr             v21.16b, v18.16b, v19.16b
        subs            x12, x12, #32
        orr             v20.16b, v20.16b, v21.16b
        orr             v2.16b, v2.16b, v20.16b
        b.gt            2b
20:
        cbz             x10, 3f                     // width < 16
        ldr             q20, [x0], #16
        cmhi            v16.16b, v0.16b, v20.16b
        cmhi            v17.16b, v20.16b, v1.16b
        orr             v16.16b, v16.16b, v17.16b
        orr             v2.16b, v2.16b, v16.16b
3:
        cbz             x7, 4f
        ldr             q21, [x0]
        cmhi            v18.16b, v0.16b, v21.16b
        cmhi            v19.16b, v21.16b, v1.16b
        orr             v16.16b, v18.16b, v19.16b
        and             v16.16b, v16.16b, v3.16b
        orr             v2.16b, v2.16b, v16.16b
4:
        umaxv           b4, v2.16b
        subs            x3, x3, #1
        umov            w9, v4.b[0]
        add             x0, x0, x1
        cbnz            w9, 8f
        b.gt            1b
        mov             x0, #0
        ret
8:
        mov             x0, #1
        ret
endfunc

/* x0: const uint8_t *data
 * x1: ptrdiff_t stride
 * x2: ptrdiff_t width
 * x3: ptrdiff_t height
 * w4: int mpeg_min
 * w5: int mpeg_max
 */
function ff_detect_range16_neon, export=1
        ands            x7, x2, #7                  // width % 7
        bic             x8, x2, #7                  // width / 8 * 8
        bic             x6, x2, #15                 // width / 16 * 16
        and             x10, x2, #8                 // check x8 != x6
        dup             v0.8h, w4                   // mpeg_min
        dup             v1.8h, w5                   // mpeg_max
        movi            v2.16b, #0                  // cond
        sub             x1, x1, x8, lsl #1
        b.eq            1f
        load_mask_zero  shift=1
1:
        cbz             x6, 20f                     // width < 16
        mov             x12, x6
2:
        ld1             {v5.8h, v6.8h}, [x0], #32
        cmhi            v16.8h, v0.8h, v5.8h
        cmhi            v17.8h, v5.8h, v1.8h
        cmhi            v18.8h, v0.8h, v6.8h
        cmhi            v19.8h, v6.8h, v1.8h
        orr             v20.16b, v16.16b, v17.16b
        orr             v21.16b, v18.16b, v19.16b
        subs            x12, x12, #16
        orr             v20.16b, v20.16b, v21.16b
        orr             v2.16b, v2.16b, v20.16b
        b.gt            2b
20:
        cbz             x10, 3f                     // width < 8
        ldr             q20, [x0], #16
        cmhi            v16.8h, v0.8h, v20.8h
        cmhi            v17.8h, v20.8h, v1.8h
        orr             v16.16b, v16.16b, v17.16b
        orr             v2.16b, v2.16b, v16.16b
3:
        cbz             x7, 4f
        ldr             q21, [x0]
        cmhi            v18.8h, v0.8h, v21.8h
        cmhi            v19.8h, v21.8h, v1.8h
        orr             v16.16b, v18.16b, v19.16b
        and             v16.16b, v16.16b, v3.16b
        orr             v2.16b, v2.16b, v16.16b
4:
        umaxv           h4, v2.8h
        subs            x3, x3, #1
        umov            w9, v4.h[0]
        add             x0, x0, x1
        cbnz            w9, 8f
        b.gt            1b
        mov             x0, #0
        ret
8:
        mov             x0, #1
        ret
endfunc

/*
 * x0: const uint8_t *color,
 * x1: ptrdiff_t color_stride,
 * x2: const uint8_t *alpha,
 * x3: ptrdiff_t alpha_stride,
 * x4: ptrdiff_t width,
 * x5: ptrdiff_t height,
 * w6: int alpha_max,
 */
function ff_detect_alpha_full_neon, export=1
        ands            x7, x4, #15             // width % 16
        bic             x8, x4, #15             // width / 16 * 16
        movi            v0.16b, #0
        movi            v1.16b, #255
        dup             v2.16b, w6              // alpha_max
        sub             x1, x1, x8              // color_stride - aligned_width
        sub             x3, x3, x8              // alpha_stride - aligned_width
        b.eq            1f

        // Create mask for non-aligned width
        load_mask
1:
        cbz             x8, 20f                 // width < 16
        mov             x12, x8                 // w12: aligned_width
2:
        ldr             q5, [x0], #16
        ldr             q6, [x2], #16
        subs            x12, x12, #16
        cmhi            v7.16b, v5.16b, v6.16b
        cmeq            v16.16b, v6.16b, v2.16b
        orr             v0.16b, v0.16b, v7.16b
        and             v1.16b, v1.16b, v16.16b
        b.gt            2b
20:
        cbz             w7, 3f
        // handle loop tail
        ldr             q5, [x0]
        ldr             q6, [x2]
        cmhi            v7.16b, v5.16b, v6.16b
        cmeq            v16.16b, v6.16b, v2.16b
        and             v7.16b, v7.16b, v3.16b
        orr             v16.16b, v16.16b, v4.16b
        orr             v0.16b, v0.16b, v7.16b
        and             v1.16b, v1.16b, v16.16b
3:
        umaxv           b17, v0.16b
        subs            x5, x5, #1
        umov            w9, v17.b[0]
        add             x0, x0, x1
        add             x2, x2, x3
        cbnz            w9, 4f
        b.gt            1b

        uminv           b1, v1.16b
        umov            w9, v1.b[0]
        mov             x0, #0
        cbnz            w9, 5f
        mov             x0, #FF_ALPHA_TRANSPARENT
        ret
4:
        mov             x0, #FF_ALPHA_STRAIGHT
5:
        ret
endfunc

/*
 * x0: const uint8_t *color,
 * x1: ptrdiff_t color_stride,
 * x2: const uint8_t *alpha,
 * x3: ptrdiff_t alpha_stride,
 * x4: ptrdiff_t width,
 * x5: ptrdiff_t height,
 * w6: int alpha_max,
 */
function ff_detect_alpha16_full_neon, export=1
        ands            x7, x4, #7              // width % 8
        bic             x8, x4, #7              // width / 8 * 8
        movi            v0.8h, #0
        movi            v1.16b, #255
        dup             v2.8h, w6               // alpha_max
        sub             x1, x1, x8, lsl #1      // color_stride - (aligned_width * 2)
        sub             x3, x3, x8, lsl #1      // alpha_stride - (aligned_width * 2)
        b.eq            1f

        // Create mask for non-aligned width
        load_mask       shift=1
1:
        cbz             x8, 20f                 // width < 8
        mov             x12, x8                 // w12: aligned_width
2:
        ldr             q5, [x0], #16
        ldr             q6, [x2], #16
        subs            x12, x12, #8
        cmhi            v7.8h, v5.8h, v6.8h
        cmeq            v16.8h, v6.8h, v2.8h
        orr             v0.16b, v0.16b, v7.16b
        and             v1.16b, v1.16b, v16.16b
        b.gt            2b
20:
        cbz             w7, 3f
        // handle loop tail
        ldr             q5, [x0]
        ldr             q6, [x2]
        cmhi            v7.8h, v5.8h, v6.8h
        cmeq            v16.8h, v6.8h, v2.8h
        and             v7.16b, v7.16b, v3.16b
        orr             v16.16b, v16.16b, v4.16b
        orr             v0.16b, v0.16b, v7.16b
        and             v1.16b, v1.16b, v16.16b
3:
        umaxv           h17, v0.8h
        subs            x5, x5, #1
        umov            w9, v17.h[0]
        add             x0, x0, x1
        add             x2, x2, x3
        cbnz            w9, 4f
        b.gt            1b

        uminv           h1, v1.8h
        umov            w9, v1.h[0]
        mov             x0, #0
        cbnz            w9, 5f
        mov             x0, #FF_ALPHA_TRANSPARENT
        ret
4:
        mov             x0, #FF_ALPHA_STRAIGHT
5:
        ret
endfunc

/*
 * x0: const uint8_t *color,
 * x1: ptrdiff_t color_stride,
 * x2: const uint8_t *alpha,
 * x3: ptrdiff_t alpha_stride,
 * x4: ptrdiff_t width,
 * x5: ptrdiff_t height,
 * w6: int alpha_max,
 * w7: int mpeg_range
 * [sp]: int offset
 */
function ff_detect_alpha_limited_neon, export=1
        dup             v17.16b, w7             // mpeg_range
        ldr             w13, [sp]
        movi            v0.16b, #0
        movi            v1.16b, #255
        dup             v2.16b, w6              // alpha_max
        ands            x7, x4, #15             // width % 16
        bic             x8, x4, #15             // width / 16 * 16
        dup             v18.8h, w13             // offset
        sub             x1, x1, x8              // color_stride - aligned_width
        sub             x3, x3, x8              // alpha_stride - aligned_width
        b.eq            1f

        // Create mask for non-aligned width
        load_mask
1:
        cbz             x8, 20f                     // width < 16
        mov             x12, x8                     // w12: aligned_width
2:
        ldr             q5, [x0], #16               // color
        ldr             q6, [x2], #16               // alpha
        umull           v19.8h, v2.8b, v5.8b        // alpha_max * color
        umull2          v20.8h, v2.16b, v5.16b      // alpha_max * color
        umull           v21.8h, v17.8b, v6.8b       // range * alpha
        umull2          v22.8h, v17.16b, v6.16b     // range * alpha
        cmeq            v16.16b, v6.16b, v2.16b
        subs            x12, x12, #16
        uqsub           v19.8h, v19.8h, v18.8h      // alpha_max * color - offset
        uqsub           v20.8h, v20.8h, v18.8h      // alpha_max * color - offset

        cmhi            v19.8h, v19.8h, v21.8h
        cmhi            v20.8h, v20.8h, v22.8h
        orr             v7.16b, v19.16b, v20.16b
        orr             v0.16b, v0.16b, v7.16b
        and             v1.16b, v1.16b, v16.16b
        b.gt            2b
20:
        cbz             w7, 3f
        // handle loop tail
        ldr             q5, [x0]
        ldr             q6, [x2]
        umull           v19.8h, v2.8b, v5.8b        // alpha_max * color
        umull2          v20.8h, v2.16b, v5.16b      // alpha_max * color
        umull           v21.8h, v17.8b, v6.8b       // range * alpha
        umull2          v22.8h, v17.16b, v6.16b     // range * alpha
        uqsub           v19.8h, v19.8h, v18.8h      // alpha_max * color - offset
        uqsub           v20.8h, v20.8h, v18.8h      // alpha_max * color - offset

        cmhi            v19.8h, v19.8h, v21.8h
        cmhi            v20.8h, v20.8h, v22.8h
        uqxtn           v7.8b, v19.8h
        uqxtn2          v7.16b, v20.8h
        cmeq            v16.16b, v6.16b, v2.16b

        and             v7.16b, v7.16b, v3.16b
        orr             v16.16b, v16.16b, v4.16b
        orr             v0.16b, v0.16b, v7.16b
        and             v1.16b, v1.16b, v16.16b
3:
        umaxv           b23, v0.16b
        subs            x5, x5, #1
        umov            w9, v23.b[0]
        add             x0, x0, x1
        add             x2, x2, x3
        cbnz            w9, 4f
        b.gt            1b

        uminv           b1, v1.16b
        umov            w9, v1.b[0]
        mov             x0, #0
        cbnz            w9, 5f
        mov             x0, #FF_ALPHA_TRANSPARENT
        ret
4:
        mov             x0, #FF_ALPHA_STRAIGHT
5:
        ret
endfunc

/*
 * x0: const uint8_t *color,
 * x1: ptrdiff_t color_stride,
 * x2: const uint8_t *alpha,
 * x3: ptrdiff_t alpha_stride,
 * x4: ptrdiff_t width,
 * x5: ptrdiff_t height,
 * w6: int alpha_max,
 * w7: int mpeg_range
 * [sp]: int offset
 */
function ff_detect_alpha16_limited_neon, export=1
        dup             v17.8h, w7                  // mpeg_range
        ldr             w13, [sp]
        movi            v0.8h, #0
        movi            v1.16b, #255
        dup             v2.8h, w6                   // alpha_max
        ands            x7, x4, #7                  // width % 8
        bic             x8, x4, #7                  // width / 8 * 8
        dup             v18.4s, w13                 // offset
        sub             x1, x1, x8, lsl #1          // color_stride - (aligned_width * 2)
        sub             x3, x3, x8, lsl #1          // alpha_stride - (aligned_width * 2)
        b.eq            1f

        // Create mask for non-aligned width
        load_mask       shift=1
1:
        cbz             x8, 20f                     // width < 8
        mov             x12, x8                     // w12: aligned_width
2:
        ldr             q5, [x0], #16
        ldr             q6, [x2], #16
        umull           v19.4s, v2.4h, v5.4h        // alpha_max * color
        umull2          v20.4s, v2.8h, v5.8h        // alpha_max * color
        umull           v21.4s, v17.4h, v6.4h       // range * alpha
        umull2          v22.4s, v17.8h, v6.8h       // range * alpha
        cmeq            v16.8h, v6.8h, v2.8h
        subs            x12, x12, #8
        uqsub           v19.4s, v19.4s, v18.4s      // alpha_max * color - offset
        uqsub           v20.4s, v20.4s, v18.4s      // alpha_max * color - offset

        cmhi            v19.4s, v19.4s, v21.4s
        cmhi            v20.4s, v20.4s, v22.4s
        orr             v7.16b, v19.16b, v20.16b
        orr             v0.16b, v0.16b, v7.16b
        and             v1.16b, v1.16b, v16.16b
        b.gt            2b
20:
        cbz             w7, 3f
        // handle loop tail
        ldr             q5, [x0]
        ldr             q6, [x2]
        umull           v19.4s, v2.4h, v5.4h        // alpha_max * color
        umull2          v20.4s, v2.8h, v5.8h        // alpha_max * color
        umull           v21.4s, v17.4h, v6.4h       // range * alpha
        umull2          v22.4s, v17.8h, v6.8h       // range * alpha
        uqsub           v19.4s, v19.4s, v18.4s      // alpha_max * color - offset
        uqsub           v20.4s, v20.4s, v18.4s      // alpha_max * color - offset

        cmhi            v19.4s, v19.4s, v21.4s
        cmhi            v20.4s, v20.4s, v22.4s
        uqxtn           v7.4h, v19.4s
        uqxtn2          v7.8h, v20.4s
        cmeq            v16.8h, v6.8h, v2.8h

        and             v7.16b, v7.16b, v3.16b
        orr             v16.16b, v16.16b, v4.16b
        orr             v0.16b, v0.16b, v7.16b
        and             v1.16b, v1.16b, v16.16b
3:
        umaxv           s23, v0.4s
        subs            x5, x5, #1
        umov            w9, v23.s[0]
        add             x0, x0, x1
        add             x2, x2, x3
        cbnz            w9, 4f
        b.gt            1b

        uminv           h1, v1.8h
        umov            w9, v1.h[0]
        mov             x0, #0
        cbnz            w9, 5f
        mov             x0, #FF_ALPHA_TRANSPARENT
        ret
4:
        mov             x0, #FF_ALPHA_STRAIGHT
5:
        ret
endfunc
