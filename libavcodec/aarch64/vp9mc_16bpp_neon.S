/*
 * Copyright (c) 2017 Google Inc.
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

function ff_vp9_avg64_16_neon, export=1
        mov             x5,  x0
        sub             x1,  x1,  #64
        sub             x3,  x3,  #64
1:
        ld1             {v4.8h,  v5.8h,  v6.8h,  v7.8h},  [x2], #64
        ld1             {v0.8h,  v1.8h,  v2.8h,  v3.8h},  [x0], #64
        ld1             {v20.8h, v21.8h, v22.8h, v23.8h}, [x2], x3
        urhadd          v0.8h,  v0.8h,  v4.8h
        urhadd          v1.8h,  v1.8h,  v5.8h
        ld1             {v16.8h, v17.8h, v18.8h, v19.8h}, [x0], x1
        urhadd          v2.8h,  v2.8h,  v6.8h
        urhadd          v3.8h,  v3.8h,  v7.8h
        subs            w4,  w4,  #1
        urhadd          v16.8h, v16.8h, v20.8h
        urhadd          v17.8h, v17.8h, v21.8h
        st1             {v0.8h,  v1.8h,  v2.8h,  v3.8h},  [x5], #64
        urhadd          v18.8h, v18.8h, v22.8h
        urhadd          v19.8h, v19.8h, v23.8h
        st1             {v16.8h, v17.8h, v18.8h, v19.8h}, [x5], x1
        b.ne            1b
        ret
endfunc

function ff_vp9_avg32_16_neon, export=1
        mov             x5,  x0
1:
        ld1             {v4.8h,  v5.8h,  v6.8h,  v7.8h},  [x2], x3
        ld1             {v0.8h,  v1.8h,  v2.8h,  v3.8h},  [x0], x1
        ld1             {v20.8h, v21.8h, v22.8h, v23.8h}, [x2], x3
        urhadd          v0.8h,  v0.8h,  v4.8h
        urhadd          v1.8h,  v1.8h,  v5.8h
        ld1             {v16.8h, v17.8h, v18.8h, v19.8h}, [x0], x1
        urhadd          v2.8h,  v2.8h,  v6.8h
        urhadd          v3.8h,  v3.8h,  v7.8h
        subs            w4,  w4,  #2
        urhadd          v16.8h, v16.8h, v20.8h
        urhadd          v17.8h, v17.8h, v21.8h
        st1             {v0.8h,  v1.8h,  v2.8h,  v3.8h},  [x5], x1
        urhadd          v18.8h, v18.8h, v22.8h
        urhadd          v19.8h, v19.8h, v23.8h
        st1             {v16.8h, v17.8h, v18.8h, v19.8h}, [x5], x1
        b.ne            1b
        ret
endfunc

function ff_vp9_avg16_16_neon, export=1
1:
        ld1             {v2.8h, v3.8h},  [x2], x3
        ld1             {v0.8h, v1.8h},  [x0]
        urhadd          v0.8h,  v0.8h,  v2.8h
        urhadd          v1.8h,  v1.8h,  v3.8h
        subs            w4,  w4,  #1
        st1             {v0.8h, v1.8h},  [x0], x1
        b.ne            1b
        ret
endfunc

function ff_vp9_avg8_16_neon, export=1
        mov             x5,  x0
1:
        ld1             {v2.8h},  [x2], x3
        ld1             {v0.8h},  [x0], x1
        ld1             {v3.8h},  [x2], x3
        urhadd          v0.8h,  v0.8h,  v2.8h
        ld1             {v1.8h},  [x0], x1
        urhadd          v1.8h,  v1.8h,  v3.8h
        subs            w4,  w4,  #2
        st1             {v0.8h},  [x5], x1
        st1             {v1.8h},  [x5], x1
        b.ne            1b
        ret
endfunc

function ff_vp9_avg4_16_neon, export=1
        mov             x5,  x0
1:
        ld1             {v2.4h},  [x2], x3
        ld1             {v0.4h},  [x0], x1
        ld1             {v3.4h},  [x2], x3
        urhadd          v0.4h,  v0.4h,  v2.4h
        ld1             {v1.4h},  [x0], x1
        urhadd          v1.4h,  v1.4h,  v3.4h
        subs            w4,  w4,  #2
        st1             {v0.4h},  [x5], x1
        st1             {v1.8b},  [x5], x1
        b.ne            1b
        ret
endfunc


// Extract a vector from src1-src2 and src4-src5 (src1-src3 and src4-src6
// for size >= 16), and multiply-accumulate into dst1 and dst5 (or
// dst1-dst2 and dst5-dst6 for size >= 8 and dst1-dst4 and dst5-dst8
// for size >= 16)
.macro extmlal dst1, dst2, dst3, dst4, dst5, dst6, dst7, dst8, src1, src2, src3, src4, src5, src6, offset, size
        ext             v20.16b, \src1\().16b, \src2\().16b, #(2*\offset)
        ext             v22.16b, \src4\().16b, \src5\().16b, #(2*\offset)
        smlal           \dst1\().4s, v20.4h, v0.h[\offset]
        smlal           \dst5\().4s, v22.4h, v0.h[\offset]
.if \size >= 16
        ext             v21.16b, \src2\().16b, \src3\().16b, #(2*\offset)
        ext             v23.16b, \src5\().16b, \src6\().16b, #(2*\offset)
.endif
.if \size >= 8
        smlal2          \dst2\().4s, v20.8h, v0.h[\offset]
        smlal2          \dst6\().4s, v22.8h, v0.h[\offset]
.endif
.if \size >= 16
        smlal           \dst3\().4s, v21.4h, v0.h[\offset]
        smlal           \dst7\().4s, v23.4h, v0.h[\offset]
        smlal2          \dst4\().4s, v21.8h, v0.h[\offset]
        smlal2          \dst8\().4s, v23.8h, v0.h[\offset]
.endif
.endm


// Instantiate a horizontal filter function for the given size.
// This can work on 4, 8 or 16 pixels in parallel; for larger
// widths it will do 16 pixels at a time and loop horizontally.
// The actual width (in bytes) is passed in x5, the height in w4 and
// the filter coefficients in x9.
.macro do_8tap_h type, size
function \type\()_8tap_\size\()h
        sub             x2,  x2,  #6
        add             x6,  x0,  x1
        add             x7,  x2,  x3
        add             x1,  x1,  x1
        add             x3,  x3,  x3
        // Only size >= 16 loops horizontally and needs
        // reduced dst stride
.if \size >= 16
        sub             x1,  x1,  x5
.endif
        // size >= 16 loads two qwords and increments r2,
        // for size 4/8 it's enough with one qword and no
        // postincrement
.if \size >= 16
        sub             x3,  x3,  x5
        sub             x3,  x3,  #16
.endif
        // Load the filter vector
        ld1             {v0.8h},  [x9]
1:
.if \size >= 16
        mov             x9,  x5
.endif
        // Load src
.if \size >= 16
        ld1             {v5.8h,  v6.8h,  v7.8h},  [x2], #48
        ld1             {v16.8h, v17.8h, v18.8h}, [x7], #48
.else
        ld1             {v5.8h,  v6.8h},  [x2]
        ld1             {v16.8h, v17.8h}, [x7]
.endif
2:

        smull           v1.4s,  v5.4h,  v0.h[0]
        smull           v24.4s, v16.4h, v0.h[0]
.if \size >= 8
        smull2          v2.4s,  v5.8h,  v0.h[0]
        smull2          v25.4s, v16.8h, v0.h[0]
.endif
.if \size >= 16
        smull           v3.4s,  v6.4h,  v0.h[0]
        smull           v26.4s, v17.4h, v0.h[0]
        smull2          v4.4s,  v6.8h,  v0.h[0]
        smull2          v27.4s, v17.8h, v0.h[0]
.endif
        extmlal         v1,  v2,  v3,  v4,  v24, v25, v26, v27, v5,  v6,  v7,  v16, v17, v18, 1, \size
        extmlal         v1,  v2,  v3,  v4,  v24, v25, v26, v27, v5,  v6,  v7,  v16, v17, v18, 2, \size
        extmlal         v1,  v2,  v3,  v4,  v24, v25, v26, v27, v5,  v6,  v7,  v16, v17, v18, 3, \size
        extmlal         v1,  v2,  v3,  v4,  v24, v25, v26, v27, v5,  v6,  v7,  v16, v17, v18, 4, \size
        extmlal         v1,  v2,  v3,  v4,  v24, v25, v26, v27, v5,  v6,  v7,  v16, v17, v18, 5, \size
        extmlal         v1,  v2,  v3,  v4,  v24, v25, v26, v27, v5,  v6,  v7,  v16, v17, v18, 6, \size
        extmlal         v1,  v2,  v3,  v4,  v24, v25, v26, v27, v5,  v6,  v7,  v16, v17, v18, 7, \size

        // Round, shift and saturate
        // The sqrshrun takes care of clamping negative values to zero, but
        // we manually need to do umin with the max pixel value.
        sqrshrun        v1.4h,  v1.4s,  #7
        sqrshrun        v24.4h, v24.4s, #7
.if \size >= 8
        sqrshrun2       v1.8h,  v2.4s,  #7
        sqrshrun2       v24.8h, v25.4s, #7
        umin            v1.8h,  v1.8h,  v31.8h
        umin            v24.8h, v24.8h, v31.8h
.if \size >= 16
        sqrshrun        v2.4h,  v3.4s,  #7
        sqrshrun        v25.4h, v26.4s, #7
        sqrshrun2       v2.8h,  v4.4s,  #7
        sqrshrun2       v25.8h, v27.4s, #7
        umin            v2.8h,  v2.8h,  v31.8h
        umin            v25.8h, v25.8h, v31.8h
.endif
.else
        umin            v1.4h,  v1.4h,  v31.4h
        umin            v24.4h, v24.4h, v31.4h
.endif
        // Average
.ifc \type,avg
.if \size >= 16
        ld1             {v3.8h,  v4.8h},  [x0]
        ld1             {v29.8h, v30.8h}, [x6]
        urhadd          v1.8h,  v1.8h,  v3.8h
        urhadd          v2.8h,  v2.8h,  v4.8h
        urhadd          v24.8h, v24.8h, v29.8h
        urhadd          v25.8h, v25.8h, v30.8h
.elseif \size >= 8
        ld1             {v3.8h},  [x0]
        ld1             {v4.8h},  [x6]
        urhadd          v1.8h,  v1.8h,  v3.8h
        urhadd          v24.8h, v24.8h, v4.8h
.else
        ld1             {v3.4h},  [x0]
        ld1             {v4.4h},  [x6]
        urhadd          v1.4h,  v1.4h,  v3.4h
        urhadd          v24.4h, v24.4h, v4.4h
.endif
.endif
        // Store and loop horizontally (for size >= 16)
.if \size >= 16
        subs            x9,  x9,  #32
        st1             {v1.8h,  v2.8h},  [x0], #32
        st1             {v24.8h, v25.8h}, [x6], #32
        b.eq            3f
        mov             v5.16b,  v7.16b
        mov             v16.16b, v18.16b
        ld1             {v6.8h,  v7.8h},  [x2], #32
        ld1             {v17.8h, v18.8h}, [x7], #32
        b               2b
.elseif \size == 8
        st1             {v1.8h},  [x0]
        st1             {v24.8h}, [x6]
.else // \size == 4
        st1             {v1.4h},  [x0]
        st1             {v24.4h}, [x6]
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
do_8tap_h put, \size
do_8tap_h avg, \size
.endm

do_8tap_h_size 4
do_8tap_h_size 8
do_8tap_h_size 16

.macro do_8tap_h_func type, filter, offset, size, bpp
function ff_vp9_\type\()_\filter\()\size\()_h_\bpp\()_neon, export=1
        mvni            v31.8h, #((0xff << (\bpp - 8)) & 0xff), lsl #8
        movrel          x6,  X(ff_vp9_subpel_filters), 256*\offset
        cmp             w5,  #8
        add             x9,  x6,  w5, uxtw #4
        mov             x5,  #2*\size
.if \size >= 16
        b               \type\()_8tap_16h
.else
        b               \type\()_8tap_\size\()h
.endif
endfunc
.endm

.macro do_8tap_h_filters size, bpp
do_8tap_h_func put, regular, 1, \size, \bpp
do_8tap_h_func avg, regular, 1, \size, \bpp
do_8tap_h_func put, sharp,   2, \size, \bpp
do_8tap_h_func avg, sharp,   2, \size, \bpp
do_8tap_h_func put, smooth,  0, \size, \bpp
do_8tap_h_func avg, smooth,  0, \size, \bpp
.endm

.macro do_8tap_h_filters_bpp bpp
do_8tap_h_filters 64, \bpp
do_8tap_h_filters 32, \bpp
do_8tap_h_filters 16, \bpp
do_8tap_h_filters 8,  \bpp
do_8tap_h_filters 4,  \bpp
.endm

do_8tap_h_filters_bpp 10
do_8tap_h_filters_bpp 12


// Vertical filters

// Round, shift and saturate and store reg1-reg4
.macro do_store4 reg1, reg2, reg3, reg4, tmp1, tmp2, tmp3, tmp4, minreg, type
        sqrshrun        \reg1\().4h,  \reg1\().4s, #7
        sqrshrun        \reg2\().4h,  \reg2\().4s, #7
        sqrshrun        \reg3\().4h,  \reg3\().4s, #7
        sqrshrun        \reg4\().4h,  \reg4\().4s, #7
.ifc \type,avg
        ld1             {\tmp1\().4h},  [x7], x1
        ld1             {\tmp2\().4h},  [x7], x1
        ld1             {\tmp3\().4h},  [x7], x1
        ld1             {\tmp4\().4h},  [x7], x1
.endif
        umin            \reg1\().4h,  \reg1\().4h,  \minreg\().4h
        umin            \reg2\().4h,  \reg2\().4h,  \minreg\().4h
        umin            \reg3\().4h,  \reg3\().4h,  \minreg\().4h
        umin            \reg4\().4h,  \reg4\().4h,  \minreg\().4h
.ifc \type,avg
        urhadd          \reg1\().4h,  \reg1\().4h,  \tmp1\().4h
        urhadd          \reg2\().4h,  \reg2\().4h,  \tmp2\().4h
        urhadd          \reg3\().4h,  \reg3\().4h,  \tmp3\().4h
        urhadd          \reg4\().4h,  \reg4\().4h,  \tmp4\().4h
.endif
        st1             {\reg1\().4h},  [x0], x1
        st1             {\reg2\().4h},  [x0], x1
        st1             {\reg3\().4h},  [x0], x1
        st1             {\reg4\().4h},  [x0], x1
.endm

// Round, shift and saturate and store reg1-8, where
// reg1-2, reg3-4 etc pairwise correspond to 4 rows.
.macro do_store8 reg1, reg2, reg3, reg4, reg5, reg6, reg7, reg8, minreg, type
        sqrshrun        \reg1\().4h,  \reg1\().4s, #7
        sqrshrun2       \reg1\().8h,  \reg2\().4s, #7
        sqrshrun        \reg2\().4h,  \reg3\().4s, #7
        sqrshrun2       \reg2\().8h,  \reg4\().4s, #7
        sqrshrun        \reg3\().4h,  \reg5\().4s, #7
        sqrshrun2       \reg3\().8h,  \reg6\().4s, #7
        sqrshrun        \reg4\().4h,  \reg7\().4s, #7
        sqrshrun2       \reg4\().8h,  \reg8\().4s, #7
.ifc \type,avg
        ld1             {\reg5\().8h},  [x7], x1
        ld1             {\reg6\().8h},  [x7], x1
        ld1             {\reg7\().8h},  [x7], x1
        ld1             {\reg8\().8h},  [x7], x1
.endif
        umin            \reg1\().8h,  \reg1\().8h,  \minreg\().8h
        umin            \reg2\().8h,  \reg2\().8h,  \minreg\().8h
        umin            \reg3\().8h,  \reg3\().8h,  \minreg\().8h
        umin            \reg4\().8h,  \reg4\().8h,  \minreg\().8h
.ifc \type,avg
        urhadd          \reg1\().8h,  \reg1\().8h,  \reg5\().8h
        urhadd          \reg2\().8h,  \reg2\().8h,  \reg6\().8h
        urhadd          \reg3\().8h,  \reg3\().8h,  \reg7\().8h
        urhadd          \reg4\().8h,  \reg4\().8h,  \reg8\().8h
.endif
        st1             {\reg1\().8h},  [x0], x1
        st1             {\reg2\().8h},  [x0], x1
        st1             {\reg3\().8h},  [x0], x1
        st1             {\reg4\().8h},  [x0], x1
.endm

// Evaluate the filter twice in parallel, from the inputs src1-src9 into dst1-dst2
// (src1-src8 into dst1, src2-src9 into dst2).
.macro convolve4 dst1, dst2, src1, src2, src3, src4, src5, src6, src7, src8, src9, tmp1, tmp2
        smull           \dst1\().4s, \src1\().4h, v0.h[0]
        smull           \dst2\().4s, \src2\().4h, v0.h[0]
        smull           \tmp1\().4s, \src2\().4h, v0.h[1]
        smull           \tmp2\().4s, \src3\().4h, v0.h[1]
        smlal           \dst1\().4s, \src3\().4h, v0.h[2]
        smlal           \dst2\().4s, \src4\().4h, v0.h[2]
        smlal           \tmp1\().4s, \src4\().4h, v0.h[3]
        smlal           \tmp2\().4s, \src5\().4h, v0.h[3]
        smlal           \dst1\().4s, \src5\().4h, v0.h[4]
        smlal           \dst2\().4s, \src6\().4h, v0.h[4]
        smlal           \tmp1\().4s, \src6\().4h, v0.h[5]
        smlal           \tmp2\().4s, \src7\().4h, v0.h[5]
        smlal           \dst1\().4s, \src7\().4h, v0.h[6]
        smlal           \dst2\().4s, \src8\().4h, v0.h[6]
        smlal           \tmp1\().4s, \src8\().4h, v0.h[7]
        smlal           \tmp2\().4s, \src9\().4h, v0.h[7]
        add             \dst1\().4s, \dst1\().4s, \tmp1\().4s
        add             \dst2\().4s, \dst2\().4s, \tmp2\().4s
.endm

// Evaluate the filter twice in parallel, from the inputs src1-src9 into dst1-dst4
// (src1-src8 into dst1-dst2, src2-src9 into dst3-dst4).
.macro convolve8 dst1, dst2, dst3, dst4, src1, src2, src3, src4, src5, src6, src7, src8, src9
        smull           \dst1\().4s, \src1\().4h, v0.h[0]
        smull2          \dst2\().4s, \src1\().8h, v0.h[0]
        smull           \dst3\().4s, \src2\().4h, v0.h[0]
        smull2          \dst4\().4s, \src2\().8h, v0.h[0]
        smlal           \dst1\().4s, \src2\().4h, v0.h[1]
        smlal2          \dst2\().4s, \src2\().8h, v0.h[1]
        smlal           \dst3\().4s, \src3\().4h, v0.h[1]
        smlal2          \dst4\().4s, \src3\().8h, v0.h[1]
        smlal           \dst1\().4s, \src3\().4h, v0.h[2]
        smlal2          \dst2\().4s, \src3\().8h, v0.h[2]
        smlal           \dst3\().4s, \src4\().4h, v0.h[2]
        smlal2          \dst4\().4s, \src4\().8h, v0.h[2]
        smlal           \dst1\().4s, \src4\().4h, v0.h[3]
        smlal2          \dst2\().4s, \src4\().8h, v0.h[3]
        smlal           \dst3\().4s, \src5\().4h, v0.h[3]
        smlal2          \dst4\().4s, \src5\().8h, v0.h[3]
        smlal           \dst1\().4s, \src5\().4h, v0.h[4]
        smlal2          \dst2\().4s, \src5\().8h, v0.h[4]
        smlal           \dst3\().4s, \src6\().4h, v0.h[4]
        smlal2          \dst4\().4s, \src6\().8h, v0.h[4]
        smlal           \dst1\().4s, \src6\().4h, v0.h[5]
        smlal2          \dst2\().4s, \src6\().8h, v0.h[5]
        smlal           \dst3\().4s, \src7\().4h, v0.h[5]
        smlal2          \dst4\().4s, \src7\().8h, v0.h[5]
        smlal           \dst1\().4s, \src7\().4h, v0.h[6]
        smlal2          \dst2\().4s, \src7\().8h, v0.h[6]
        smlal           \dst3\().4s, \src8\().4h, v0.h[6]
        smlal2          \dst4\().4s, \src8\().8h, v0.h[6]
        smlal           \dst1\().4s, \src8\().4h, v0.h[7]
        smlal2          \dst2\().4s, \src8\().8h, v0.h[7]
        smlal           \dst3\().4s, \src9\().4h, v0.h[7]
        smlal2          \dst4\().4s, \src9\().8h, v0.h[7]
.endm

// Instantiate a vertical filter function for filtering 8 pixels at a time.
// The height is passed in x4, the width in x5 and the filter coefficients
// in x6.
.macro do_8tap_8v type
function \type\()_8tap_8v
        sub             x2,  x2,  x3, lsl #1
        sub             x2,  x2,  x3
        ld1             {v0.8h},  [x6]
1:
.ifc \type,avg
        mov             x7,  x0
.endif
        mov             x6,  x4

        ld1             {v17.8h}, [x2], x3
        ld1             {v18.8h}, [x2], x3
        ld1             {v19.8h}, [x2], x3
        ld1             {v20.8h}, [x2], x3
        ld1             {v21.8h}, [x2], x3
        ld1             {v22.8h}, [x2], x3
        ld1             {v23.8h}, [x2], x3
2:
        ld1             {v24.8h}, [x2], x3
        ld1             {v25.8h}, [x2], x3
        ld1             {v26.8h}, [x2], x3
        ld1             {v27.8h}, [x2], x3

        convolve8       v2,  v3,  v4,  v5,  v17, v18, v19, v20, v21, v22, v23, v24, v25
        convolve8       v6,  v7,  v30, v31, v19, v20, v21, v22, v23, v24, v25, v26, v27
        do_store8       v2,  v3,  v4,  v5,  v6,  v7,  v30, v31, v1,  \type

        subs            x6,  x6,  #4
        b.eq            8f

        ld1             {v16.8h}, [x2], x3
        ld1             {v17.8h}, [x2], x3
        ld1             {v18.8h}, [x2], x3
        ld1             {v19.8h}, [x2], x3
        convolve8       v2,  v3,  v4,  v5,  v21, v22, v23, v24, v25, v26, v27, v16, v17
        convolve8       v6,  v7,  v20, v21, v23, v24, v25, v26, v27, v16, v17, v18, v19
        do_store8       v2,  v3,  v4,  v5,  v6,  v7,  v20, v21, v1,  \type

        subs            x6,  x6,  #4
        b.eq            8f

        ld1             {v20.8h}, [x2], x3
        ld1             {v21.8h}, [x2], x3
        ld1             {v22.8h}, [x2], x3
        ld1             {v23.8h}, [x2], x3
        convolve8       v2,  v3,  v4,  v5,  v25, v26, v27, v16, v17, v18, v19, v20, v21
        convolve8       v6,  v7,  v24, v25, v27, v16, v17, v18, v19, v20, v21, v22, v23
        do_store8       v2,  v3,  v4,  v5,  v6,  v7,  v24, v25, v1,  \type

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
        add             x2,  x2,  #16
        add             x0,  x0,  #16
        b               1b
9:
        ret
endfunc
.endm

do_8tap_8v put
do_8tap_8v avg


// Instantiate a vertical filter function for filtering a 4 pixels wide
// slice. This only is designed to work for 4 or 8 output lines.
.macro do_8tap_4v type
function \type\()_8tap_4v
        sub             x2,  x2,  x3, lsl #1
        sub             x2,  x2,  x3
        ld1             {v0.8h},  [x6]
.ifc \type,avg
        mov             x7,  x0
.endif

        ld1             {v16.4h}, [x2], x3
        ld1             {v17.4h}, [x2], x3
        ld1             {v18.4h}, [x2], x3
        ld1             {v19.4h}, [x2], x3
        ld1             {v20.4h}, [x2], x3
        ld1             {v21.4h}, [x2], x3
        ld1             {v22.4h}, [x2], x3
        ld1             {v23.4h}, [x2], x3
        ld1             {v24.4h}, [x2], x3
        ld1             {v25.4h}, [x2], x3
        ld1             {v26.4h}, [x2], x3

        convolve4       v2,  v3,  v16, v17, v18, v19, v20, v21, v22, v23, v24, v30, v31
        convolve4       v4,  v5,  v18, v19, v20, v21, v22, v23, v24, v25, v26, v30, v31
        do_store4       v2,  v3,  v4,  v5,  v28, v29, v30, v31, v1,  \type

        subs            x4,  x4,  #4
        b.eq            9f

        ld1             {v27.4h}, [x2], x3
        ld1             {v28.4h}, [x2], x3
        ld1             {v29.4h}, [x2], x3
        ld1             {v30.4h}, [x2], x3

        convolve4       v2,  v3,  v20, v21, v22, v23, v24, v25, v26, v27, v28, v16, v17
        convolve4       v4,  v5,  v22, v23, v24, v25, v26, v27, v28, v29, v30, v16, v17
        do_store4       v2,  v3,  v4,  v5,  v16, v17, v18, v19, v1,  \type

9:
        ret
endfunc
.endm

do_8tap_4v put
do_8tap_4v avg


.macro do_8tap_v_func type, filter, offset, size, bpp
function ff_vp9_\type\()_\filter\()\size\()_v_\bpp\()_neon, export=1
        uxtw            x4,  w4
        mvni            v1.8h, #((0xff << (\bpp - 8)) & 0xff), lsl #8
        movrel          x5,  X(ff_vp9_subpel_filters), 256*\offset
        add             x6,  x5,  w6, uxtw #4
        mov             x5,  #\size
.if \size >= 8
        b               \type\()_8tap_8v
.else
        b               \type\()_8tap_4v
.endif
endfunc
.endm

.macro do_8tap_v_filters size, bpp
do_8tap_v_func put, regular, 1, \size, \bpp
do_8tap_v_func avg, regular, 1, \size, \bpp
do_8tap_v_func put, sharp,   2, \size, \bpp
do_8tap_v_func avg, sharp,   2, \size, \bpp
do_8tap_v_func put, smooth,  0, \size, \bpp
do_8tap_v_func avg, smooth,  0, \size, \bpp
.endm

.macro do_8tap_v_filters_bpp bpp
do_8tap_v_filters 64, \bpp
do_8tap_v_filters 32, \bpp
do_8tap_v_filters 16, \bpp
do_8tap_v_filters 8,  \bpp
do_8tap_v_filters 4,  \bpp
.endm

do_8tap_v_filters_bpp 10
do_8tap_v_filters_bpp 12
