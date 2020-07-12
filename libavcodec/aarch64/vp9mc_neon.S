/*
 * Copyright (c) 2016 Google Inc.
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

// All public functions in this file have the following signature:
// typedef void (*vp9_mc_func)(uint8_t *dst, ptrdiff_t dst_stride,
//                            const uint8_t *ref, ptrdiff_t ref_stride,
//                            int h, int mx, int my);

function ff_vp9_avg64_neon, export=1
        mov             x5,  x0
1:
        ld1             {v4.16b,  v5.16b,  v6.16b,  v7.16b},  [x2], x3
        ld1             {v0.16b,  v1.16b,  v2.16b,  v3.16b},  [x0], x1
        ld1             {v20.16b, v21.16b, v22.16b, v23.16b}, [x2], x3
        urhadd          v0.16b,  v0.16b,  v4.16b
        urhadd          v1.16b,  v1.16b,  v5.16b
        ld1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x0], x1
        urhadd          v2.16b,  v2.16b,  v6.16b
        urhadd          v3.16b,  v3.16b,  v7.16b
        subs            w4,  w4,  #2
        urhadd          v16.16b, v16.16b, v20.16b
        urhadd          v17.16b, v17.16b, v21.16b
        st1             {v0.16b,  v1.16b,  v2.16b,  v3.16b},  [x5], x1
        urhadd          v18.16b, v18.16b, v22.16b
        urhadd          v19.16b, v19.16b, v23.16b
        st1             {v16.16b, v17.16b, v18.16b, v19.16b}, [x5], x1
        b.ne            1b
        ret
endfunc

function ff_vp9_avg32_neon, export=1
1:
        ld1             {v2.16b, v3.16b},  [x2], x3
        ld1             {v0.16b, v1.16b},  [x0]
        urhadd          v0.16b,  v0.16b,  v2.16b
        urhadd          v1.16b,  v1.16b,  v3.16b
        subs            w4,  w4,  #1
        st1             {v0.16b, v1.16b},  [x0], x1
        b.ne            1b
        ret
endfunc

function ff_vp9_copy16_neon, export=1
        add             x5,  x0,  x1
        lsl             x1,  x1,  #1
        add             x6,  x2,  x3
        lsl             x3,  x3,  #1
1:
        ld1             {v0.16b},  [x2], x3
        ld1             {v1.16b},  [x6], x3
        ld1             {v2.16b},  [x2], x3
        ld1             {v3.16b},  [x6], x3
        subs            w4,  w4,  #4
        st1             {v0.16b},  [x0], x1
        st1             {v1.16b},  [x5], x1
        st1             {v2.16b},  [x0], x1
        st1             {v3.16b},  [x5], x1
        b.ne            1b
        ret
endfunc

function ff_vp9_avg16_neon, export=1
        mov             x5,  x0
1:
        ld1             {v2.16b},  [x2], x3
        ld1             {v0.16b},  [x0], x1
        ld1             {v3.16b},  [x2], x3
        urhadd          v0.16b,  v0.16b,  v2.16b
        ld1             {v1.16b},  [x0], x1
        urhadd          v1.16b,  v1.16b,  v3.16b
        subs            w4,  w4,  #2
        st1             {v0.16b},  [x5], x1
        st1             {v1.16b},  [x5], x1
        b.ne            1b
        ret
endfunc

function ff_vp9_copy8_neon, export=1
1:
        ld1             {v0.8b},  [x2], x3
        ld1             {v1.8b},  [x2], x3
        subs            w4,  w4,  #2
        st1             {v0.8b},  [x0], x1
        st1             {v1.8b},  [x0], x1
        b.ne            1b
        ret
endfunc

function ff_vp9_avg8_neon, export=1
        mov             x5,  x0
1:
        ld1             {v2.8b},  [x2], x3
        ld1             {v0.8b},  [x0], x1
        ld1             {v3.8b},  [x2], x3
        urhadd          v0.8b,  v0.8b,  v2.8b
        ld1             {v1.8b},  [x0], x1
        urhadd          v1.8b,  v1.8b,  v3.8b
        subs            w4,  w4,  #2
        st1             {v0.8b},  [x5], x1
        st1             {v1.8b},  [x5], x1
        b.ne            1b
        ret
endfunc

function ff_vp9_copy4_neon, export=1
1:
        ld1             {v0.s}[0], [x2], x3
        ld1             {v1.s}[0], [x2], x3
        st1             {v0.s}[0], [x0], x1
        ld1             {v2.s}[0], [x2], x3
        st1             {v1.s}[0], [x0], x1
        ld1             {v3.s}[0], [x2], x3
        subs            w4,  w4,  #4
        st1             {v2.s}[0], [x0], x1
        st1             {v3.s}[0], [x0], x1
        b.ne            1b
        ret
endfunc

function ff_vp9_avg4_neon, export=1
        mov             x5,  x0
1:
        ld1             {v2.s}[0], [x2], x3
        ld1             {v0.s}[0], [x0], x1
        ld1             {v2.s}[1], [x2], x3
        ld1             {v0.s}[1], [x0], x1
        ld1             {v3.s}[0], [x2], x3
        ld1             {v1.s}[0], [x0], x1
        ld1             {v3.s}[1], [x2], x3
        ld1             {v1.s}[1], [x0], x1
        subs            w4,  w4,  #4
        urhadd          v0.8b,  v0.8b,  v2.8b
        urhadd          v1.8b,  v1.8b,  v3.8b
        st1             {v0.s}[0], [x5], x1
        st1             {v0.s}[1], [x5], x1
        st1             {v1.s}[0], [x5], x1
        st1             {v1.s}[1], [x5], x1
        b.ne            1b
        ret
endfunc


// Extract a vector from src1-src2 and src4-src5 (src1-src3 and src4-src6
// for size >= 16), and multiply-accumulate into dst1 and dst3 (or
// dst1-dst2 and dst3-dst4 for size >= 16)
.macro extmla dst1, dst2, dst3, dst4, src1, src2, src3, src4, src5, src6, offset, size
        ext             v20.16b, \src1\().16b, \src2\().16b, #(2*\offset)
        ext             v22.16b, \src4\().16b, \src5\().16b, #(2*\offset)
.if \size >= 16
        mla             \dst1\().8h, v20.8h, v0.h[\offset]
        ext             v21.16b, \src2\().16b, \src3\().16b, #(2*\offset)
        mla             \dst3\().8h, v22.8h, v0.h[\offset]
        ext             v23.16b, \src5\().16b, \src6\().16b, #(2*\offset)
        mla             \dst2\().8h, v21.8h, v0.h[\offset]
        mla             \dst4\().8h, v23.8h, v0.h[\offset]
.elseif \size == 8
        mla             \dst1\().8h, v20.8h, v0.h[\offset]
        mla             \dst3\().8h, v22.8h, v0.h[\offset]
.else
        mla             \dst1\().4h, v20.4h, v0.h[\offset]
        mla             \dst3\().4h, v22.4h, v0.h[\offset]
.endif
.endm
// The same as above, but don't accumulate straight into the
// destination, but use a temp register and accumulate with saturation.
.macro extmulqadd dst1, dst2, dst3, dst4, src1, src2, src3, src4, src5, src6, offset, size
        ext             v20.16b, \src1\().16b, \src2\().16b, #(2*\offset)
        ext             v22.16b, \src4\().16b, \src5\().16b, #(2*\offset)
.if \size >= 16
        mul             v20.8h, v20.8h, v0.h[\offset]
        ext             v21.16b, \src2\().16b, \src3\().16b, #(2*\offset)
        mul             v22.8h, v22.8h, v0.h[\offset]
        ext             v23.16b, \src5\().16b, \src6\().16b, #(2*\offset)
        mul             v21.8h, v21.8h, v0.h[\offset]
        mul             v23.8h, v23.8h, v0.h[\offset]
.elseif \size == 8
        mul             v20.8h, v20.8h, v0.h[\offset]
        mul             v22.8h, v22.8h, v0.h[\offset]
.else
        mul             v20.4h, v20.4h, v0.h[\offset]
        mul             v22.4h, v22.4h, v0.h[\offset]
.endif
.if \size == 4
        sqadd           \dst1\().4h, \dst1\().4h, v20.4h
        sqadd           \dst3\().4h, \dst3\().4h, v22.4h
.else
        sqadd           \dst1\().8h, \dst1\().8h, v20.8h
        sqadd           \dst3\().8h, \dst3\().8h, v22.8h
.if \size >= 16
        sqadd           \dst2\().8h, \dst2\().8h, v21.8h
        sqadd           \dst4\().8h, \dst4\().8h, v23.8h
.endif
.endif
.endm


// Instantiate a horizontal filter function for the given size.
// This can work on 4, 8 or 16 pixels in parallel; for larger
// widths it will do 16 pixels at a time and loop horizontally.
// The actual width is passed in x5, the height in w4 and the
// filter coefficients in x9. idx2 is the index of the largest
// filter coefficient (3 or 4) and idx1 is the other one of them.
.macro do_8tap_h type, size, idx1, idx2
function \type\()_8tap_\size\()h_\idx1\idx2
        sub             x2,  x2,  #3
        add             x6,  x0,  x1
        add             x7,  x2,  x3
        add             x1,  x1,  x1
        add             x3,  x3,  x3
        // Only size >= 16 loops horizontally and needs
        // reduced dst stride
.if \size >= 16
        sub             x1,  x1,  x5
.endif
        // size >= 16 loads two qwords and increments x2,
        // for size 4/8 it's enough with one qword and no
        // postincrement
.if \size >= 16
        sub             x3,  x3,  x5
        sub             x3,  x3,  #8
.endif
        // Load the filter vector
        ld1             {v0.8h},  [x9]
1:
.if \size >= 16
        mov             x9,  x5
.endif
        // Load src
.if \size >= 16
        ld1             {v4.8b,  v5.8b,  v6.8b},  [x2], #24
        ld1             {v16.8b, v17.8b, v18.8b}, [x7], #24
.else
        ld1             {v4.8b,  v5.8b},  [x2]
        ld1             {v16.8b, v17.8b}, [x7]
.endif
        uxtl            v4.8h,  v4.8b
        uxtl            v5.8h,  v5.8b
        uxtl            v16.8h, v16.8b
        uxtl            v17.8h, v17.8b
.if \size >= 16
        uxtl            v6.8h,  v6.8b
        uxtl            v18.8h, v18.8b
.endif
2:

        // Accumulate, adding idx2 last with a separate
        // saturating add. The positive filter coefficients
        // for all indices except idx2 must add up to less
        // than 127 for this not to overflow.
        mul             v1.8h,  v4.8h,  v0.h[0]
        mul             v24.8h, v16.8h, v0.h[0]
.if \size >= 16
        mul             v2.8h,  v5.8h,  v0.h[0]
        mul             v25.8h, v17.8h, v0.h[0]
.endif
        extmla          v1,  v2,  v24, v25, v4,  v5,  v6,  v16, v17, v18, 1,     \size
        extmla          v1,  v2,  v24, v25, v4,  v5,  v6,  v16, v17, v18, 2,     \size
        extmla          v1,  v2,  v24, v25, v4,  v5,  v6,  v16, v17, v18, \idx1, \size
        extmla          v1,  v2,  v24, v25, v4,  v5,  v6,  v16, v17, v18, 5,     \size
        extmla          v1,  v2,  v24, v25, v4,  v5,  v6,  v16, v17, v18, 6,     \size
        extmla          v1,  v2,  v24, v25, v4,  v5,  v6,  v16, v17, v18, 7,     \size
        extmulqadd      v1,  v2,  v24, v25, v4,  v5,  v6,  v16, v17, v18, \idx2, \size

        // Round, shift and saturate
        sqrshrun        v1.8b,   v1.8h,  #7
        sqrshrun        v24.8b,  v24.8h, #7
.if \size >= 16
        sqrshrun2       v1.16b,  v2.8h,  #7
        sqrshrun2       v24.16b, v25.8h, #7
.endif
        // Average
.ifc \type,avg
.if \size >= 16
        ld1             {v2.16b}, [x0]
        ld1             {v3.16b}, [x6]
        urhadd          v1.16b,  v1.16b,  v2.16b
        urhadd          v24.16b, v24.16b, v3.16b
.elseif \size == 8
        ld1             {v2.8b},  [x0]
        ld1             {v3.8b},  [x6]
        urhadd          v1.8b,  v1.8b,  v2.8b
        urhadd          v24.8b, v24.8b, v3.8b
.else
        ld1             {v2.s}[0], [x0]
        ld1             {v3.s}[0], [x6]
        urhadd          v1.8b,  v1.8b,  v2.8b
        urhadd          v24.8b, v24.8b, v3.8b
.endif
.endif
        // Store and loop horizontally (for size >= 16)
.if \size >= 16
        subs            x9,  x9,  #16
        st1             {v1.16b},  [x0], #16
        st1             {v24.16b}, [x6], #16
        b.eq            3f
        mov             v4.16b,  v6.16b
        mov             v16.16b, v18.16b
        ld1             {v6.16b},  [x2], #16
        ld1             {v18.16b}, [x7], #16
        uxtl            v5.8h,  v6.8b
        uxtl2           v6.8h,  v6.16b
        uxtl            v17.8h, v18.8b
        uxtl2           v18.8h, v18.16b
        b               2b
.elseif \size == 8
        st1             {v1.8b},    [x0]
        st1             {v24.8b},   [x6]
.else // \size == 4
        st1             {v1.s}[0],  [x0]
        st1             {v24.s}[0], [x6]
.endif
3:
        // Loop vertically
        add             x0,  x0,  x1
        add             x6,  x6,  x1
        add             x2,  x2,  x3
        add             x7,  x7,  x3
        subs            w4,  w4,  #2
        b.ne            1b
        ret
endfunc
.endm

.macro do_8tap_h_size size
do_8tap_h put, \size, 3, 4
do_8tap_h avg, \size, 3, 4
do_8tap_h put, \size, 4, 3
do_8tap_h avg, \size, 4, 3
.endm

do_8tap_h_size 4
do_8tap_h_size 8
do_8tap_h_size 16

.macro do_8tap_h_func type, filter, offset, size
function ff_vp9_\type\()_\filter\()\size\()_h_neon, export=1
        movrel          x6,  X(ff_vp9_subpel_filters), 256*\offset
        cmp             w5,  #8
        add             x9,  x6,  w5, uxtw #4
        mov             x5,  #\size
.if \size >= 16
        b.ge            \type\()_8tap_16h_34
        b               \type\()_8tap_16h_43
.else
        b.ge            \type\()_8tap_\size\()h_34
        b               \type\()_8tap_\size\()h_43
.endif
endfunc
.endm

.macro do_8tap_h_filters size
do_8tap_h_func put, regular, 1, \size
do_8tap_h_func avg, regular, 1, \size
do_8tap_h_func put, sharp,   2, \size
do_8tap_h_func avg, sharp,   2, \size
do_8tap_h_func put, smooth,  0, \size
do_8tap_h_func avg, smooth,  0, \size
.endm

do_8tap_h_filters 64
do_8tap_h_filters 32
do_8tap_h_filters 16
do_8tap_h_filters 8
do_8tap_h_filters 4


// Vertical filters

// Round, shift and saturate and store reg1-reg2 over 4 lines
.macro do_store4 reg1, reg2, tmp1, tmp2, type
        sqrshrun        \reg1\().8b,  \reg1\().8h, #7
        sqrshrun        \reg2\().8b,  \reg2\().8h, #7
.ifc \type,avg
        ld1             {\tmp1\().s}[0],  [x7], x1
        ld1             {\tmp2\().s}[0],  [x7], x1
        ld1             {\tmp1\().s}[1],  [x7], x1
        ld1             {\tmp2\().s}[1],  [x7], x1
        urhadd          \reg1\().8b,  \reg1\().8b,  \tmp1\().8b
        urhadd          \reg2\().8b,  \reg2\().8b,  \tmp2\().8b
.endif
        st1             {\reg1\().s}[0],  [x0], x1
        st1             {\reg2\().s}[0],  [x0], x1
        st1             {\reg1\().s}[1],  [x0], x1
        st1             {\reg2\().s}[1],  [x0], x1
.endm

// Round, shift and saturate and store reg1-4
.macro do_store reg1, reg2, reg3, reg4, tmp1, tmp2, tmp3, tmp4, type
        sqrshrun        \reg1\().8b,  \reg1\().8h, #7
        sqrshrun        \reg2\().8b,  \reg2\().8h, #7
        sqrshrun        \reg3\().8b,  \reg3\().8h, #7
        sqrshrun        \reg4\().8b,  \reg4\().8h, #7
.ifc \type,avg
        ld1             {\tmp1\().8b},  [x7], x1
        ld1             {\tmp2\().8b},  [x7], x1
        ld1             {\tmp3\().8b},  [x7], x1
        ld1             {\tmp4\().8b},  [x7], x1
        urhadd          \reg1\().8b,  \reg1\().8b,  \tmp1\().8b
        urhadd          \reg2\().8b,  \reg2\().8b,  \tmp2\().8b
        urhadd          \reg3\().8b,  \reg3\().8b,  \tmp3\().8b
        urhadd          \reg4\().8b,  \reg4\().8b,  \tmp4\().8b
.endif
        st1             {\reg1\().8b},  [x0], x1
        st1             {\reg2\().8b},  [x0], x1
        st1             {\reg3\().8b},  [x0], x1
        st1             {\reg4\().8b},  [x0], x1
.endm

// Evaluate the filter twice in parallel, from the inputs src1-src9 into dst1-dst2
// (src1-src8 into dst1, src2-src9 into dst2), adding idx2 separately
// at the end with saturation. Indices 0 and 7 always have negative or zero
// coefficients, so they can be accumulated into tmp1-tmp2 together with the
// largest coefficient.
.macro convolve dst1, dst2, src1, src2, src3, src4, src5, src6, src7, src8, src9, idx1, idx2, tmp1, tmp2
        mul             \dst1\().8h, \src2\().8h, v0.h[1]
        mul             \dst2\().8h, \src3\().8h, v0.h[1]
        mul             \tmp1\().8h, \src1\().8h, v0.h[0]
        mul             \tmp2\().8h, \src2\().8h, v0.h[0]
        mla             \dst1\().8h, \src3\().8h, v0.h[2]
        mla             \dst2\().8h, \src4\().8h, v0.h[2]
.if \idx1 == 3
        mla             \dst1\().8h, \src4\().8h, v0.h[3]
        mla             \dst2\().8h, \src5\().8h, v0.h[3]
.else
        mla             \dst1\().8h, \src5\().8h, v0.h[4]
        mla             \dst2\().8h, \src6\().8h, v0.h[4]
.endif
        mla             \dst1\().8h, \src6\().8h, v0.h[5]
        mla             \dst2\().8h, \src7\().8h, v0.h[5]
        mla             \tmp1\().8h, \src8\().8h, v0.h[7]
        mla             \tmp2\().8h, \src9\().8h, v0.h[7]
        mla             \dst1\().8h, \src7\().8h, v0.h[6]
        mla             \dst2\().8h, \src8\().8h, v0.h[6]
.if \idx2 == 3
        mla             \tmp1\().8h, \src4\().8h, v0.h[3]
        mla             \tmp2\().8h, \src5\().8h, v0.h[3]
.else
        mla             \tmp1\().8h, \src5\().8h, v0.h[4]
        mla             \tmp2\().8h, \src6\().8h, v0.h[4]
.endif
        sqadd           \dst1\().8h, \dst1\().8h, \tmp1\().8h
        sqadd           \dst2\().8h, \dst2\().8h, \tmp2\().8h
.endm

// Load pixels and extend them to 16 bit
.macro loadl dst1, dst2, dst3, dst4
        ld1             {v1.8b}, [x2], x3
        ld1             {v2.8b}, [x2], x3
        ld1             {v3.8b}, [x2], x3
.ifnb \dst4
        ld1             {v4.8b}, [x2], x3
.endif
        uxtl            \dst1\().8h, v1.8b
        uxtl            \dst2\().8h, v2.8b
        uxtl            \dst3\().8h, v3.8b
.ifnb \dst4
        uxtl            \dst4\().8h, v4.8b
.endif
.endm

// Instantiate a vertical filter function for filtering 8 pixels at a time.
// The height is passed in x4, the width in x5 and the filter coefficients
// in x6. idx2 is the index of the largest filter coefficient (3 or 4)
// and idx1 is the other one of them.
.macro do_8tap_8v type, idx1, idx2
function \type\()_8tap_8v_\idx1\idx2
        sub             x2,  x2,  x3, lsl #1
        sub             x2,  x2,  x3
        ld1             {v0.8h},  [x6]
1:
.ifc \type,avg
        mov             x7,  x0
.endif
        mov             x6,  x4

        loadl           v17, v18, v19

        loadl           v20, v21, v22, v23
2:
        loadl           v24, v25, v26, v27
        convolve        v1,  v2,  v17, v18, v19, v20, v21, v22, v23, v24, v25, \idx1, \idx2, v5,  v6
        convolve        v3,  v4,  v19, v20, v21, v22, v23, v24, v25, v26, v27, \idx1, \idx2, v5,  v6
        do_store        v1,  v2,  v3,  v4,  v5,  v6,  v7,  v28, \type

        subs            x6,  x6,  #4
        b.eq            8f

        loadl           v16, v17, v18, v19
        convolve        v1,  v2,  v21, v22, v23, v24, v25, v26, v27, v16, v17, \idx1, \idx2, v5,  v6
        convolve        v3,  v4,  v23, v24, v25, v26, v27, v16, v17, v18, v19, \idx1, \idx2, v5,  v6
        do_store        v1,  v2,  v3,  v4,  v5,  v6,  v7,  v28, \type

        subs            x6,  x6,  #4
        b.eq            8f

        loadl           v20, v21, v22, v23
        convolve        v1,  v2,  v25, v26, v27, v16, v17, v18, v19, v20, v21, \idx1, \idx2, v5,  v6
        convolve        v3,  v4,  v27, v16, v17, v18, v19, v20, v21, v22, v23, \idx1, \idx2, v5,  v6
        do_store        v1,  v2,  v3,  v4,  v5,  v6,  v7,  v28, \type

        subs            x6,  x6,  #4
        b.ne            2b

8:
        subs            x5,  x5,  #8
        b.eq            9f
        // x0 -= h * dst_stride
        msub            x0,  x1,  x4, x0
        // x2 -= h * src_stride
        msub            x2,  x3,  x4, x2
        // x2 -= 8 * src_stride
        sub             x2,  x2,  x3, lsl #3
        // x2 += 1 * src_stride
        add             x2,  x2,  x3
        add             x2,  x2,  #8
        add             x0,  x0,  #8
        b               1b
9:
        ret
endfunc
.endm

do_8tap_8v put, 3, 4
do_8tap_8v put, 4, 3
do_8tap_8v avg, 3, 4
do_8tap_8v avg, 4, 3


// Instantiate a vertical filter function for filtering a 4 pixels wide
// slice. The first half of the registers contain one row, while the second
// half of a register contains the second-next row (also stored in the first
// half of the register two steps ahead). The convolution does two outputs
// at a time; the output of v17-v24 into one, and v18-v25 into another one.
// The first half of first output is the first output row, the first half
// of the other output is the second output row. The second halves of the
// registers are rows 3 and 4.
// This only is designed to work for 4 or 8 output lines.
.macro do_8tap_4v type, idx1, idx2
function \type\()_8tap_4v_\idx1\idx2
        sub             x2,  x2,  x3, lsl #1
        sub             x2,  x2,  x3
        ld1             {v0.8h},  [x6]
.ifc \type,avg
        mov             x7,  x0
.endif

        ld1             {v1.s}[0],  [x2], x3
        ld1             {v2.s}[0],  [x2], x3
        ld1             {v3.s}[0],  [x2], x3
        ld1             {v4.s}[0],  [x2], x3
        ld1             {v5.s}[0],  [x2], x3
        ld1             {v6.s}[0],  [x2], x3
        trn1            v1.2s,  v1.2s,  v3.2s
        ld1             {v7.s}[0],  [x2], x3
        trn1            v2.2s,  v2.2s,  v4.2s
        ld1             {v26.s}[0], [x2], x3
        uxtl            v17.8h, v1.8b
        trn1            v3.2s,  v3.2s,  v5.2s
        ld1             {v27.s}[0], [x2], x3
        uxtl            v18.8h, v2.8b
        trn1            v4.2s,  v4.2s,  v6.2s
        ld1             {v28.s}[0], [x2], x3
        uxtl            v19.8h, v3.8b
        trn1            v5.2s,  v5.2s,  v7.2s
        ld1             {v29.s}[0], [x2], x3
        uxtl            v20.8h, v4.8b
        trn1            v6.2s,  v6.2s,  v26.2s
        uxtl            v21.8h, v5.8b
        trn1            v7.2s,  v7.2s,  v27.2s
        uxtl            v22.8h, v6.8b
        trn1            v26.2s, v26.2s, v28.2s
        uxtl            v23.8h, v7.8b
        trn1            v27.2s, v27.2s, v29.2s
        uxtl            v24.8h, v26.8b
        uxtl            v25.8h, v27.8b

        convolve        v1,  v2,  v17, v18, v19, v20, v21, v22, v23, v24, v25, \idx1, \idx2, v3,  v4
        do_store4       v1,  v2,  v5,  v6,  \type

        subs            x4,  x4,  #4
        b.eq            9f

        ld1             {v1.s}[0],  [x2], x3
        ld1             {v2.s}[0],  [x2], x3
        trn1            v28.2s, v28.2s, v1.2s
        trn1            v29.2s, v29.2s, v2.2s
        ld1             {v1.s}[1],  [x2], x3
        uxtl            v26.8h, v28.8b
        ld1             {v2.s}[1],  [x2], x3
        uxtl            v27.8h, v29.8b
        uxtl            v28.8h, v1.8b
        uxtl            v29.8h, v2.8b

        convolve        v1,  v2,  v21, v22, v23, v24, v25, v26, v27, v28, v29, \idx1, \idx2, v3,  v4
        do_store4       v1,  v2,  v5,  v6,  \type

9:
        ret
endfunc
.endm

do_8tap_4v put, 3, 4
do_8tap_4v put, 4, 3
do_8tap_4v avg, 3, 4
do_8tap_4v avg, 4, 3


.macro do_8tap_v_func type, filter, offset, size
function ff_vp9_\type\()_\filter\()\size\()_v_neon, export=1
        uxtw            x4,  w4
        movrel          x5,  X(ff_vp9_subpel_filters), 256*\offset
        cmp             w6,  #8
        add             x6,  x5,  w6, uxtw #4
        mov             x5,  #\size
.if \size >= 8
        b.ge            \type\()_8tap_8v_34
        b               \type\()_8tap_8v_43
.else
        b.ge            \type\()_8tap_4v_34
        b               \type\()_8tap_4v_43
.endif
endfunc
.endm

.macro do_8tap_v_filters size
do_8tap_v_func put, regular, 1, \size
do_8tap_v_func avg, regular, 1, \size
do_8tap_v_func put, sharp,   2, \size
do_8tap_v_func avg, sharp,   2, \size
do_8tap_v_func put, smooth,  0, \size
do_8tap_v_func avg, smooth,  0, \size
.endm

do_8tap_v_filters 64
do_8tap_v_filters 32
do_8tap_v_filters 16
do_8tap_v_filters 8
do_8tap_v_filters 4
