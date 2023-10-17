/*
 * ARM NEON IDCT
 *
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2017 Matthieu Bouron <matthieu.bouron@gmail.com>
 *
 * Based on Simple IDCT
 * Copyright (c) 2001 Michael Niedermayer <michaelni@gmx.at>
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

#define Z1  22725  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define Z2  21407  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define Z3  19266  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define Z4  16383  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define Z5  12873  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define Z6  8867   //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define Z7  4520   //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define Z4c ((1<<(COL_SHIFT-1))/Z4)
#define ROW_SHIFT 11
#define COL_SHIFT 20

#define z1 v0.H[0]
#define z2 v0.H[1]
#define z3 v0.H[2]
#define z4 v0.H[3]
#define z5 v0.H[4]
#define z6 v0.H[5]
#define z7 v0.H[6]
#define z4c v0.H[7]

const   idct_coeff_neon, align=4
        .short Z1, Z2, Z3, Z4, Z5, Z6, Z7, Z4c
endconst

.macro idct_start data
        prfm            pldl1keep, [\data]
        mov             x10, x30
        movrel          x3, idct_coeff_neon
        ld1             {v0.2d}, [x3]
.endm

.macro idct_end
        ret             x10
.endm

.macro smull1 a, b, c
        smull           \a, \b, \c
.endm

.macro smlal1 a, b, c
        smlal           \a, \b, \c
.endm

.macro smlsl1 a, b, c
        smlsl           \a, \b, \c
.endm

.macro idct_col4_top y1, y2, y3, y4, i, l
        smull\i         v7.4s,  \y3\l, z2
        smull\i         v16.4s, \y3\l, z6
        smull\i         v17.4s, \y2\l, z1
        add             v19.4s, v23.4s, v7.4s
        smull\i         v18.4s, \y2\l, z3
        add             v20.4s, v23.4s, v16.4s
        smull\i         v5.4s,  \y2\l, z5
        sub             v21.4s, v23.4s, v16.4s
        smull\i         v6.4s,  \y2\l, z7
        sub             v22.4s, v23.4s, v7.4s

        smlal\i         v17.4s, \y4\l, z3
        smlsl\i         v18.4s, \y4\l, z7
        smlsl\i         v5.4s,  \y4\l, z1
        smlsl\i         v6.4s,  \y4\l, z5
.endm

.macro idct_row4_neon y1, y2, y3, y4, pass
        ld1             {\y1\().2d,\y2\().2d}, [x2], #32
        movi            v23.4s, #1<<2, lsl #8
        orr             v5.16b, \y1\().16b, \y2\().16b
        ld1             {\y3\().2d,\y4\().2d}, [x2], #32
        orr             v6.16b, \y3\().16b, \y4\().16b
        orr             v5.16b, v5.16b, v6.16b
        mov             x3, v5.d[1]
        smlal           v23.4s, \y1\().4h, z4

        idct_col4_top   \y1, \y2, \y3, \y4, 1, .4h

        cmp             x3, #0
        b.eq            \pass\()f

        smull2          v7.4s, \y1\().8h, z4
        smlal2          v17.4s, \y2\().8h, z5
        smlsl2          v18.4s, \y2\().8h, z1
        smull2          v16.4s, \y3\().8h, z2
        smlal2          v5.4s, \y2\().8h, z7
        add             v19.4s, v19.4s, v7.4s
        sub             v20.4s, v20.4s, v7.4s
        sub             v21.4s, v21.4s, v7.4s
        add             v22.4s, v22.4s, v7.4s
        smlal2          v6.4s, \y2\().8h, z3
        smull2          v7.4s, \y3\().8h, z6
        smlal2          v17.4s, \y4\().8h, z7
        smlsl2          v18.4s, \y4\().8h, z5
        smlal2          v5.4s, \y4\().8h, z3
        smlsl2          v6.4s, \y4\().8h, z1
        add             v19.4s, v19.4s, v7.4s
        sub             v20.4s, v20.4s, v16.4s
        add             v21.4s, v21.4s, v16.4s
        sub             v22.4s, v22.4s, v7.4s

\pass:  add             \y3\().4S, v19.4S, v17.4S
        add             \y4\().4s, v20.4s, v18.4s
        shrn            \y1\().4h, \y3\().4s, #ROW_SHIFT
        shrn            \y2\().4h, \y4\().4s, #ROW_SHIFT
        add             v7.4s, v21.4s, v5.4s
        add             v16.4s, v22.4s, v6.4s
        shrn            \y3\().4h, v7.4s, #ROW_SHIFT
        shrn            \y4\().4h, v16.4s, #ROW_SHIFT
        sub             v22.4s, v22.4s, v6.4s
        sub             v19.4s, v19.4s, v17.4s
        sub             v21.4s, v21.4s, v5.4s
        shrn2           \y1\().8h, v22.4s, #ROW_SHIFT
        sub             v20.4s, v20.4s, v18.4s
        shrn2           \y2\().8h, v21.4s, #ROW_SHIFT
        shrn2           \y3\().8h, v20.4s, #ROW_SHIFT
        shrn2           \y4\().8h, v19.4s, #ROW_SHIFT

        trn1            v16.8h, \y1\().8h, \y2\().8h
        trn2            v17.8h, \y1\().8h, \y2\().8h
        trn1            v18.8h, \y3\().8h, \y4\().8h
        trn2            v19.8h, \y3\().8h, \y4\().8h
        trn1            \y1\().4s, v16.4s, v18.4s
        trn1            \y2\().4s, v17.4s, v19.4s
        trn2            \y3\().4s, v16.4s, v18.4s
        trn2            \y4\().4s, v17.4s, v19.4s
.endm

.macro declare_idct_col4_neon i, l
function idct_col4_neon\i
        dup             v23.4h, z4c
.if \i == 1
        add             v23.4h, v23.4h, v24.4h
.else
        mov             v5.d[0], v24.d[1]
        add             v23.4h, v23.4h, v5.4h
.endif
        smull           v23.4s, v23.4h, z4

        idct_col4_top   v24, v25, v26, v27, \i, \l

        mov             x4, v28.d[\i - 1]
        mov             x5, v29.d[\i - 1]
        cmp             x4, #0
        b.eq            1f

        smull\i         v7.4s,  v28\l,  z4
        add             v19.4s, v19.4s, v7.4s
        sub             v20.4s, v20.4s, v7.4s
        sub             v21.4s, v21.4s, v7.4s
        add             v22.4s, v22.4s, v7.4s

1:      mov             x4, v30.d[\i - 1]
        cmp             x5, #0
        b.eq            2f

        smlal\i         v17.4s, v29\l, z5
        smlsl\i         v18.4s, v29\l, z1
        smlal\i         v5.4s,  v29\l, z7
        smlal\i         v6.4s,  v29\l, z3

2:      mov             x5, v31.d[\i - 1]
        cmp             x4, #0
        b.eq            3f

        smull\i         v7.4s,  v30\l, z6
        smull\i         v16.4s, v30\l, z2
        add             v19.4s, v19.4s, v7.4s
        sub             v22.4s, v22.4s, v7.4s
        sub             v20.4s, v20.4s, v16.4s
        add             v21.4s, v21.4s, v16.4s

3:      cmp             x5, #0
        b.eq            4f

        smlal\i         v17.4s, v31\l, z7
        smlsl\i         v18.4s, v31\l, z5
        smlal\i         v5.4s,  v31\l, z3
        smlsl\i         v6.4s,  v31\l, z1

4:      addhn           v7.4h, v19.4s, v17.4s
        addhn2          v7.8h, v20.4s, v18.4s
        subhn           v18.4h, v20.4s, v18.4s
        subhn2          v18.8h, v19.4s, v17.4s

        addhn           v16.4h, v21.4s, v5.4s
        addhn2          v16.8h, v22.4s, v6.4s
        subhn           v17.4h, v22.4s, v6.4s
        subhn2          v17.8h, v21.4s, v5.4s

        ret
endfunc
.endm

declare_idct_col4_neon 1, .4H
declare_idct_col4_neon 2, .8H

function ff_simple_idct_put_neon, export=1
        idct_start      x2

        idct_row4_neon  v24, v25, v26, v27, 1
        idct_row4_neon  v28, v29, v30, v31, 2
        bl              idct_col4_neon1

        sqshrun         v1.8b,  v7.8h, #COL_SHIFT-16
        sqshrun2        v1.16b, v16.8h, #COL_SHIFT-16
        sqshrun         v3.8b,  v17.8h, #COL_SHIFT-16
        sqshrun2        v3.16b, v18.8h, #COL_SHIFT-16

        bl              idct_col4_neon2

        sqshrun         v2.8b,  v7.8h, #COL_SHIFT-16
        sqshrun2        v2.16b, v16.8h, #COL_SHIFT-16
        sqshrun         v4.8b,  v17.8h, #COL_SHIFT-16
        sqshrun2        v4.16b, v18.8h, #COL_SHIFT-16

        zip1            v16.4s, v1.4s, v2.4s
        zip2            v17.4s, v1.4s, v2.4s

        st1             {v16.d}[0], [x0], x1
        st1             {v16.d}[1], [x0], x1

        zip1            v18.4s, v3.4s, v4.4s
        zip2            v19.4s, v3.4s, v4.4s

        st1             {v17.d}[0], [x0], x1
        st1             {v17.d}[1], [x0], x1
        st1             {v18.d}[0], [x0], x1
        st1             {v18.d}[1], [x0], x1
        st1             {v19.d}[0], [x0], x1
        st1             {v19.d}[1], [x0], x1

        idct_end
endfunc

function ff_simple_idct_add_neon, export=1
        idct_start      x2

        idct_row4_neon  v24, v25, v26, v27, 1
        idct_row4_neon  v28, v29, v30, v31, 2
        bl              idct_col4_neon1

        sshr            v1.8h, v7.8h, #COL_SHIFT-16
        sshr            v2.8h, v16.8h, #COL_SHIFT-16
        sshr            v3.8h, v17.8h, #COL_SHIFT-16
        sshr            v4.8h, v18.8h, #COL_SHIFT-16

        bl              idct_col4_neon2

        sshr            v7.8h, v7.8h, #COL_SHIFT-16
        sshr            v16.8h, v16.8h, #COL_SHIFT-16
        sshr            v17.8h, v17.8h, #COL_SHIFT-16
        sshr            v18.8h, v18.8h, #COL_SHIFT-16

        mov             x9,  x0
        ld1             {v19.d}[0], [x0], x1
        zip1            v23.2d, v1.2d, v7.2d
        zip2            v24.2d, v1.2d, v7.2d
        ld1             {v19.d}[1], [x0], x1
        zip1            v25.2d, v2.2d, v16.2d
        zip2            v26.2d, v2.2d, v16.2d
        ld1             {v20.d}[0], [x0], x1
        zip1            v27.2d, v3.2d, v17.2d
        zip2            v28.2d, v3.2d, v17.2d
        ld1             {v20.d}[1], [x0], x1
        zip1            v29.2d, v4.2d, v18.2d
        zip2            v30.2d, v4.2d, v18.2d
        ld1             {v21.d}[0], [x0], x1
        uaddw           v23.8h, v23.8h, v19.8b
        uaddw2          v24.8h, v24.8h, v19.16b
        ld1             {v21.d}[1], [x0], x1
        sqxtun          v23.8b, v23.8h
        sqxtun2         v23.16b, v24.8h
        ld1             {v22.d}[0], [x0], x1
        uaddw           v24.8h, v25.8h, v20.8b
        uaddw2          v25.8h, v26.8h, v20.16b
        ld1             {v22.d}[1], [x0], x1
        sqxtun          v24.8b, v24.8h
        sqxtun2         v24.16b, v25.8h
        st1             {v23.d}[0], [x9], x1
        uaddw           v25.8h, v27.8h, v21.8b
        uaddw2          v26.8h, v28.8h, v21.16b
        st1             {v23.d}[1], [x9], x1
        sqxtun          v25.8b, v25.8h
        sqxtun2         v25.16b, v26.8h
        st1             {v24.d}[0], [x9], x1
        uaddw           v26.8h, v29.8h, v22.8b
        uaddw2          v27.8h, v30.8h, v22.16b
        st1             {v24.d}[1], [x9], x1
        sqxtun          v26.8b, v26.8h
        sqxtun2         v26.16b, v27.8h
        st1             {v25.d}[0], [x9], x1
        st1             {v25.d}[1], [x9], x1
        st1             {v26.d}[0], [x9], x1
        st1             {v26.d}[1], [x9], x1

        idct_end
endfunc

function ff_simple_idct_neon, export=1
        idct_start      x0

        mov             x2,  x0
        idct_row4_neon  v24, v25, v26, v27, 1
        idct_row4_neon  v28, v29, v30, v31, 2
        sub             x2, x2, #128
        bl              idct_col4_neon1

        sshr            v1.8h, v7.8h, #COL_SHIFT-16
        sshr            v2.8h, v16.8h, #COL_SHIFT-16
        sshr            v3.8h, v17.8h, #COL_SHIFT-16
        sshr            v4.8h, v18.8h, #COL_SHIFT-16

        bl              idct_col4_neon2

        sshr            v7.8h, v7.8h, #COL_SHIFT-16
        sshr            v16.8h, v16.8h, #COL_SHIFT-16
        sshr            v17.8h, v17.8h, #COL_SHIFT-16
        sshr            v18.8h, v18.8h, #COL_SHIFT-16

        zip1            v23.2d, v1.2d, v7.2d
        zip2            v24.2d, v1.2d, v7.2d
        st1             {v23.2d,v24.2d}, [x2], #32
        zip1            v25.2d, v2.2d, v16.2d
        zip2            v26.2d, v2.2d, v16.2d
        st1             {v25.2d,v26.2d}, [x2], #32
        zip1            v27.2d, v3.2d, v17.2d
        zip2            v28.2d, v3.2d, v17.2d
        st1             {v27.2d,v28.2d}, [x2], #32
        zip1            v29.2d, v4.2d, v18.2d
        zip2            v30.2d, v4.2d, v18.2d
        st1             {v29.2d,v30.2d}, [x2], #32

        idct_end
endfunc
