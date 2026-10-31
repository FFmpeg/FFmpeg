/*
 * Copyright (c) 2026 Gyula Sebok <gyula.sbk@gmail.com>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/aarch64/asm.S"

// v31 = 0, clobbers v16-v23
.macro prefix_step sz, shift, nregs
        ext             v16.16b, v31.16b, v0.16b, #16 - \shift
.if \nregs >= 4
        ext             v17.16b, v31.16b, v1.16b, #16 - \shift
        ext             v18.16b, v31.16b, v2.16b, #16 - \shift
        ext             v19.16b, v31.16b, v3.16b, #16 - \shift
.endif
.if \nregs == 8
        ext             v20.16b, v31.16b, v4.16b, #16 - \shift
        ext             v21.16b, v31.16b, v5.16b, #16 - \shift
        ext             v22.16b, v31.16b, v6.16b, #16 - \shift
        ext             v23.16b, v31.16b, v7.16b, #16 - \shift
.endif
        add             v0\sz, v0\sz, v16\sz
.if \nregs >= 4
        add             v1\sz, v1\sz, v17\sz
        add             v2\sz, v2\sz, v18\sz
        add             v3\sz, v3\sz, v19\sz
.endif
.if \nregs == 8
        add             v4\sz, v4\sz, v20\sz
        add             v5\sz, v5\sz, v21\sz
        add             v6\sz, v6\sz, v22\sz
        add             v7\sz, v7\sz, v23\sz
.endif
.endm

.macro prefix_sum_8 nregs
        prefix_step     .16b, 1, \nregs
        prefix_step     .16b, 2, \nregs
        prefix_step     .16b, 4, \nregs
        prefix_step     .16b, 8, \nregs
.endm

.macro prefix_sum_16 nregs
        prefix_step     .8h,  2, \nregs
        prefix_step     .8h,  4, \nregs
        prefix_step     .8h,  8, \nregs
.endm

// v0-v3 or v0-v7 += v30 + the last elements of the preceding vectors,
// v30 = the new running sum. Only v0 and v1 get v30 directly, the others
// get it through the broadcasts of v1 and v3. mask=1 also ands the
// results with v29. Clobbers v16-v22.
.macro add_carry sz, last, nregs, mask=0
        add             v1\sz, v1\sz, v30\sz
        dup             v16\sz, v0\last
        dup             v18\sz, v2\last
.if \nregs == 8
        dup             v20\sz, v4\last
        dup             v22\sz, v6\last
.endif
        add             v0\sz, v0\sz, v30\sz
        add             v1\sz, v1\sz, v16\sz
        add             v3\sz, v3\sz, v18\sz
.if \nregs == 8
        add             v5\sz, v5\sz, v20\sz
        add             v7\sz, v7\sz, v22\sz
.endif
.if \mask
        and             v0.16b, v0.16b, v29.16b
.endif
        dup             v17\sz, v1\last
.if \nregs == 8
        dup             v21\sz, v5\last
.endif
.if \mask
        and             v1.16b, v1.16b, v29.16b
.endif
        add             v2\sz, v2\sz, v17\sz
        add             v3\sz, v3\sz, v17\sz
.if \nregs == 4
        dup             v30\sz, v3\last
.else
        add             v6\sz, v6\sz, v21\sz
        add             v7\sz, v7\sz, v21\sz
.if \mask
        and             v2.16b, v2.16b, v29.16b
.endif
        dup             v19\sz, v3\last
.if \mask
        and             v3.16b, v3.16b, v29.16b
.endif
        add             v4\sz, v4\sz, v19\sz
        add             v5\sz, v5\sz, v19\sz
        add             v6\sz, v6\sz, v19\sz
        add             v7\sz, v7\sz, v19\sz
        dup             v30\sz, v7\last
.endif
.if \mask
.if \nregs == 4
        and             v2.16b, v2.16b, v29.16b
        and             v3.16b, v3.16b, v29.16b
.else
        and             v4.16b, v4.16b, v29.16b
        and             v5.16b, v5.16b, v29.16b
        and             v6.16b, v6.16b, v29.16b
        and             v7.16b, v7.16b, v29.16b
.endif
.endif
.endm

// int ff_add_left_pred_neon(uint8_t *dst, const uint8_t *src,
//                           ptrdiff_t w, int acc)
function ff_add_left_pred_neon, export=1
        add             x4, x0, x2                  // dst + w
        movi            v31.16b, #0
        dup             v30.16b, w3                 // running sum
        subs            x2, x2, #128
        b.lt            2f
1:
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x1], #64
        ld1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x1], #64
        prefix_sum_8    8
        add_carry       .16b, .b[15], 8
        subs            x2, x2, #128
        st1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
        st1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x0], #64
        b.ge            1b
2:
        tbz             x2, #6, 3f                  // the low 7 bits are w % 128
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x1], #64
        prefix_sum_8    4
        add_carry       .16b, .b[15], 4
        st1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
3:
        ands            x2, x2, #63
        b.eq            5f
4:
        ld1             {v0.16b}, [x1], #16
        prefix_sum_8    1
        add             v0.16b, v0.16b, v30.16b
        subs            x2, x2, #16
        dup             v30.16b, v0.b[15]
        st1             {v0.16b}, [x0], #16
        b.gt            4b
        ldurb           w0, [x4, #-1]               // v30 can be past dst[w - 1]
        ret
5:
        umov            w0, v30.b[0]
        ret
endfunc

// int ff_add_left_pred_int16_neon(uint16_t *dst, const uint16_t *src,
//                                 unsigned mask, ptrdiff_t w, unsigned acc)
function ff_add_left_pred_int16_neon, export=1
        add             x5, x0, x3, lsl #1          // dst + w
        movi            v31.16b, #0
        dup             v29.8h, w2                  // mask, (1 << bits) - 1
        dup             v30.8h, w4                  // running sum
        subs            x3, x3, #64
        b.lt            2f
1:
        ld1             {v0.8h, v1.8h, v2.8h, v3.8h}, [x1], #64
        ld1             {v4.8h, v5.8h, v6.8h, v7.8h}, [x1], #64
        prefix_sum_16   8
        add_carry       .8h, .h[7], 8, mask=1
        subs            x3, x3, #64
        st1             {v0.8h, v1.8h, v2.8h, v3.8h}, [x0], #64
        st1             {v4.8h, v5.8h, v6.8h, v7.8h}, [x0], #64
        b.ge            1b
2:
        tbz             x3, #5, 3f                  // the low 6 bits are w % 64
        ld1             {v0.8h, v1.8h, v2.8h, v3.8h}, [x1], #64
        prefix_sum_16   4
        add_carry       .8h, .h[7], 4, mask=1
        st1             {v0.8h, v1.8h, v2.8h, v3.8h}, [x0], #64
3:
        ands            x3, x3, #31
        b.eq            5f
4:
        ld1             {v0.8h}, [x1], #16
        prefix_sum_16   1
        add             v0.8h, v0.8h, v30.8h
        subs            x3, x3, #8
        dup             v30.8h, v0.h[7]
        and             v0.16b, v0.16b, v29.16b
        st1             {v0.8h}, [x0], #16
        b.gt            4b
        ldurh           w0, [x5, #-2]               // v30 can be past dst[w - 1]
        ret
5:
        umov            w0, v30.h[0]
        and             w0, w0, w2                  // v30 is not masked
        ret
endfunc

// \r0-\r3 += top, clobbers v16-v19
.macro add_top4 r0, r1, r2, r3
        ld1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x3], #64
        add             \r0\().16b, \r0\().16b, v16.16b
        add             \r1\().16b, \r1\().16b, v17.16b
        add             \r2\().16b, \r2\().16b, v18.16b
        add             \r3\().16b, \r3\().16b, v19.16b
.endm

// void ff_add_gradient_pred_neon(uint8_t *src, const ptrdiff_t stride,
//                                const ptrdiff_t width)
// For residuals D and reconstructed top row T, the gradient recurrence is
// dst[x] = left - T[-1] + sum(D[0..x]) + T[x] (mod 256). Keep the
// adjusted residual sum in v30, adding T only after the prefix scan.
function ff_add_gradient_pred_neon, export=1
        sub             x3, x0, x1                  // top
        mov             x5, x0                      // read pointer
        ldurb           w6, [x0, #-1]
        ldurb           w7, [x3, #-1]
        sub             w6, w6, w7
        movi            v31.16b, #0
        dup             v30.16b, w6                 // left - top left
        subs            x2, x2, #128
        b.lt            2f
1:
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x5], #64
        ld1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x5], #64
        prefix_sum_8    8
        add_carry       .16b, .b[15], 8
        add_top4        v0, v1, v2, v3
        add_top4        v4, v5, v6, v7
        subs            x2, x2, #128
        st1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
        st1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x0], #64
        b.ge            1b
2:
        tbz             x2, #6, 3f
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x5], #64
        prefix_sum_8    4
        add_carry       .16b, .b[15], 4
        add_top4        v0, v1, v2, v3
        st1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
3:
        ands            x2, x2, #63
        b.eq            9f
4:
        ld1             {v0.16b}, [x5], #16
        prefix_sum_8    1
        add             v0.16b, v0.16b, v30.16b
        subs            x2, x2, #16
        dup             v30.16b, v0.b[15]
        ld1             {v16.16b}, [x3], #16
        add             v0.16b, v0.16b, v16.16b
        st1             {v0.16b}, [x0], #16
        b.gt            4b
9:
        ret
endfunc
