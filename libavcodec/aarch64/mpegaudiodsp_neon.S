/*
 * Copyright (c) 2014 Janne Grunau <janne-libav@jannau.net>
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

#define FRAC_BITS   23   // fractional bits for sb_samples and dct
#define WFRAC_BITS  16   // fractional bits for window
#define OUT_SHIFT (WFRAC_BITS + FRAC_BITS - 15)

const   tbl_rev128_s, align=4
        .byte           12, 13, 14, 15
        .byte            8,  9, 10, 11
        .byte            4,  5,  6,  7
        .byte            0,  1,  2,  3
endconst

.macro   apply_window   type, st
function ff_mpadsp_apply_window_\type\()_neon, export=1
        mov             x7,  x0
        add             x8,  x0,  #512<<2
        ld1             {v0.4s,v1.4s,v2.4s,v3.4s},  [x7],  #64
        ld1             {v4.4s,v5.4s,v6.4s,v7.4s},  [x7],  #64
        st1             {v0.4s,v1.4s,v2.4s,v3.4s},  [x8],  #64
        st1             {v4.4s,v5.4s,v6.4s,v7.4s},  [x8],  #64
        movrel          x15, tbl_rev128_s
        ld1             {v27.4s}, [x15]
.ifc \type, fixed
        lsl             x4,  x4,  #1
.else
        lsl             x4,  x4,  #2
.endif
        add             x10, x0,  #45<<2
        add             x0,  x0,  #16<<2
        add             x1,  x1,  #16<<2
        add             x5,  x3,  x4,  lsl #5
        sub             x5,  x5,  x4            // samples2
        neg             x13, x4                 // -incr
        mov             x9,  #64<<2
.ifc \type, fixed
        ld1r            {v16.2s}, [x2]          // dither_state
        sxtl            v16.2d, v16.2s
        movi            v29.16b, #0
        movi            v30.2d, #(1<<OUT_SHIFT)-1
        trn1            v31.2d, v29.2d, v30.2d
        trn2            v30.2d, v30.2d, v29.2d
        trn1            v16.2d, v16.2d, v29.2d
.else
        movi            v16.4s, #0
        movi            v28.4s, #0
.endif
        mov             x14, #4
1:
        mov             x8,  x0
        sub             x7,  x1,  #3<<2
        sub             x6,  x1,  x14, lsl #4
        add             x7,  x7,  x14, lsl #4
        add             x11, x6, #(32)<<2      // w  + 32
        add             x12, x7, #(32)<<2      // w2 + 32
        mov             x15, #8
        movi            v17.16b, #0
        movi            v18.16b, #0
        movi            v19.16b, #0
2:
        subs            x15, x15, #1
        ld1             {v0.4s},  [x8],  x9
        ld1             {v1.4s},  [x10], x9
        ld1             {v2.4s},  [x6],  x9
        ld1             {v3.4s},  [x7],  x9
        tbl             v6.16b, {v0.16b}, v27.16b
        tbl             v7.16b, {v1.16b}, v27.16b
        ld1             {v4.4s},  [x11], x9
        ld1             {v5.4s},  [x12], x9
        MLA             v16, v2, v0
        MLA2            v17, v2, v0
        MLS             v18, v3, v6
        MLS2            v19, v3, v6
        MLS             v16, v4, v7
        MLS2            v17, v4, v7
        MLS             v18, v5, v1
        MLS2            v19, v5, v1
        b.gt            2b

        cmp             x14, #4
        sub             x10, x10, #64<<5        // 64 * 8 * sizeof(int32_t)

.ifc \type, fixed
        and             v28.16b, v16.16b, v30.16b
        ext             v28.16b, v29.16b, v28.16b, #8

        b.eq            4f
        round_sample    v19, 1, 1
4:
        round_sample    v16, 1, 0
        shrn            v16.2s, v16.2d,  #OUT_SHIFT
        round_sample    v19, 0, 0
        shrn            v19.2s, v19.2d,  #OUT_SHIFT
        round_sample    v17, 0, 1
        round_sample    v18, 1, 1
        round_sample    v17, 1, 0
        shrn2           v16.4s, v17.2d,  #OUT_SHIFT
        round_sample    v18, 0, 0
        shrn2           v19.4s, v18.2d,  #OUT_SHIFT
        sqxtn           v16.4h, v16.4s
        sqxtn           v18.4h, v19.4s
.else
        ext             v18.16b, v18.16b, v18.16b, #8
.endif

        st1             {v16.\st\()}[0], [x3], x4
        b.eq            4f
        st1             {v18.\st\()}[1], [x5], x13
4:
        st1             {v16.\st\()}[1], [x3], x4
        st1             {v18.\st\()}[0], [x5], x13
        st1             {v16.\st\()}[2], [x3], x4
        st1             {v18.\st\()}[3], [x5], x13
        st1             {v16.\st\()}[3], [x3], x4
        st1             {v18.\st\()}[2], [x5], x13

        mov             v16.16b, v28.16b

        subs            x14, x14, #1
        add             x0,  x0,  #4<<2
        sub             x10, x10, #4<<2
        b.gt            1b

// computing samples[16]
        add             x6,  x1,  #32<<2
        ld1             {v0.2s},  [x6],  x9
        ld1             {v1.2s},  [x0],  x9
.rept   3
        ld1             {v2.2s},  [x6],  x9
        ld1             {v3.2s},  [x0],  x9
        MLS             v16, v0,  v1
        ld1             {v0.2s},  [x6],  x9
        ld1             {v1.2s},  [x0],  x9
        MLS             v16, v2,  v3
.endr
        ld1             {v2.2s},  [x6],  x9
        ld1             {v3.2s},  [x0],  x9
        MLS             v16, v0,  v1
        MLS             v16, v2,  v3

.ifc \type, fixed
        and             v28.16b, v16.16b, v30.16b
        shrn            v20.2s,  v16.2d,  #OUT_SHIFT
        xtn             v28.2s,  v28.2d
        sqxtn           v20.4h,  v20.4s
        st1             {v28.s}[0], [x2]        // save dither_state
        st1             {v20.h}[0], [x3]
.else
        st1             {v16.s}[0], [x3]
.endif

        ret
endfunc
.purgem round_sample
.purgem MLA
.purgem MLA2
.purgem MLS
.purgem MLS2
.endm


.macro  round_sample    r, idx, next
        add             \r\().2d, \r\().2d, v28.2d
.if \idx == 0
        and             v28.16b,  \r\().16b,  v30.16b
.else // \idx == 1
        and             v28.16b,  \r\().16b,  v31.16b
.endif
.if \idx != \next
  .if \next == 0
        ext             v28.16b, v28.16b, v29.16b, #8
  .else
        ext             v28.16b, v29.16b, v28.16b, #8
  .endif
.endif
.endm
.macro  MLA             d, s1, s2
        smlal           \d\().2d, \s1\().2s, \s2\().2s
.endm
.macro  MLA2            d, s1, s2
        smlal2          \d\().2d, \s1\().4s, \s2\().4s
.endm
.macro  MLS             d, s1, s2
        smlsl           \d\().2d, \s1\().2s, \s2\().2s
.endm
.macro  MLS2            d, s1, s2
        smlsl2          \d\().2d, \s1\().4s, \s2\().4s
.endm
apply_window fixed, h


// nothing to do for round_sample and ML{A,S}2
.macro  round_sample    r, idx, next
.endm
.macro  MLA2            d, s1, s2
.endm
.macro  MLS2            d, s1, s2
.endm
.macro  MLA             d, s1, s2
        fmla            \d\().4s, \s1\().4s, \s2\().4s
.endm
.macro  MLS             d, s1, s2
        fmls            \d\().4s, \s1\().4s, \s2\().4s
.endm
apply_window float, s
