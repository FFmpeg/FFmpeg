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
        ld1             {v0.2D}, [x3]
.endm

.macro idct_end
        br              x10
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
        smull\i         v7.4S,  \y3\l, z2
        smull\i         v16.4S, \y3\l, z6
        smull\i         v17.4S, \y2\l, z1
        add             v19.4S, v23.4S, v7.4S
        smull\i         v18.4S, \y2\l, z3
        add             v20.4S, v23.4S, v16.4S
        smull\i         v5.4S,  \y2\l, z5
        sub             v21.4S, v23.4S, v16.4S
        smull\i         v6.4S,  \y2\l, z7
        sub             v22.4S, v23.4S, v7.4S

        smlal\i         v17.4S, \y4\l, z3
        smlsl\i         v18.4S, \y4\l, z7
        smlsl\i         v5.4S,  \y4\l, z1
        smlsl\i         v6.4S,  \y4\l, z5
.endm

.macro idct_row4_neon y1, y2, y3, y4, pass
        ld1             {\y1\().2D-\y2\().2D}, [x2], #32
        movi            v23.4S, #1<<2, lsl #8
        orr             v5.16B, \y1\().16B, \y2\().16B
        ld1             {\y3\().2D, \y4\().2D}, [x2], #32
        orr             v6.16B, \y3\().16B, \y4\().16B
        orr             v5.16B, v5.16B, v6.16B
        mov             x3, v5.D[1]
        smlal           v23.4S, \y1\().4H, z4

        idct_col4_top   \y1, \y2, \y3, \y4, 1, .4H

        cmp             x3, #0
        beq             \pass\()f

        smull2          v7.4S, \y1\().8H, z4
        smlal2          v17.4S, \y2\().8H, z5
        smlsl2          v18.4S, \y2\().8H, z1
        smull2          v16.4S, \y3\().8H, z2
        smlal2          v5.4S, \y2\().8H, z7
        add             v19.4S, v19.4S, v7.4S
        sub             v20.4S, v20.4S, v7.4S
        sub             v21.4S, v21.4S, v7.4S
        add             v22.4S, v22.4S, v7.4S
        smlal2          v6.4S, \y2\().8H, z3
        smull2          v7.4S, \y3\().8H, z6
        smlal2          v17.4S, \y4\().8H, z7
        smlsl2          v18.4S, \y4\().8H, z5
        smlal2          v5.4S, \y4\().8H, z3
        smlsl2          v6.4S, \y4\().8H, z1
        add             v19.4S, v19.4S, v7.4S
        sub             v20.4S, v20.4S, v16.4S
        add             v21.4S, v21.4S, v16.4S
        sub             v22.4S, v22.4S, v7.4S

\pass:  add             \y3\().4S, v19.4S, v17.4S
        add             \y4\().4S, v20.4S, v18.4S
        shrn            \y1\().4H, \y3\().4S, #ROW_SHIFT
        shrn            \y2\().4H, \y4\().4S, #ROW_SHIFT
        add             v7.4S, v21.4S, v5.4S
        add             v16.4S, v22.4S, v6.4S
        shrn            \y3\().4H, v7.4S, #ROW_SHIFT
        shrn            \y4\().4H, v16.4S, #ROW_SHIFT
        sub             v22.4S, v22.4S, v6.4S
        sub             v19.4S, v19.4S, v17.4S
        sub             v21.4S, v21.4S, v5.4S
        shrn2           \y1\().8H, v22.4S, #ROW_SHIFT
        sub             v20.4S, v20.4S, v18.4S
        shrn2           \y2\().8H, v21.4S, #ROW_SHIFT
        shrn2           \y3\().8H, v20.4S, #ROW_SHIFT
        shrn2           \y4\().8H, v19.4S, #ROW_SHIFT

        trn1            v16.8H, \y1\().8H, \y2\().8H
        trn2            v17.8H, \y1\().8H, \y2\().8H
        trn1            v18.8H, \y3\().8H, \y4\().8H
        trn2            v19.8H, \y3\().8H, \y4\().8H
        trn1            \y1\().4S, v16.4S, v18.4S
        trn1            \y2\().4S, v17.4S, v19.4S
        trn2            \y3\().4S, v16.4S, v18.4S
        trn2            \y4\().4S, v17.4S, v19.4S
.endm

.macro declare_idct_col4_neon i, l
function idct_col4_neon\i
        dup             v23.4H, z4c
.if \i == 1
        add             v23.4H, v23.4H, v24.4H
.else
        mov             v5.D[0], v24.D[1]
        add             v23.4H, v23.4H, v5.4H
.endif
        smull           v23.4S, v23.4H, z4

        idct_col4_top   v24, v25, v26, v27, \i, \l

        mov             x4, v28.D[\i - 1]
        mov             x5, v29.D[\i - 1]
        cmp             x4, #0
        beq             1f

        smull\i         v7.4S,  v28\l,  z4
        add             v19.4S, v19.4S, v7.4S
        sub             v20.4S, v20.4S, v7.4S
        sub             v21.4S, v21.4S, v7.4S
        add             v22.4S, v22.4S, v7.4S

1:      mov             x4, v30.D[\i - 1]
        cmp             x5, #0
        beq             2f

        smlal\i         v17.4S, v29\l, z5
        smlsl\i         v18.4S, v29\l, z1
        smlal\i         v5.4S,  v29\l, z7
        smlal\i         v6.4S,  v29\l, z3

2:      mov             x5, v31.D[\i - 1]
        cmp             x4, #0
        beq             3f

        smull\i         v7.4S,  v30\l, z6
        smull\i         v16.4S, v30\l, z2
        add             v19.4S, v19.4S, v7.4S
        sub             v22.4S, v22.4S, v7.4S
        sub             v20.4S, v20.4S, v16.4S
        add             v21.4S, v21.4S, v16.4S

3:      cmp             x5, #0
        beq             4f

        smlal\i         v17.4S, v31\l, z7
        smlsl\i         v18.4S, v31\l, z5
        smlal\i         v5.4S,  v31\l, z3
        smlsl\i         v6.4S,  v31\l, z1

4:      addhn           v7.4H, v19.4S, v17.4S
        addhn2          v7.8H, v20.4S, v18.4S
        subhn           v18.4H, v20.4S, v18.4S
        subhn2          v18.8H, v19.4S, v17.4S

        addhn           v16.4H, v21.4S, v5.4S
        addhn2          v16.8H, v22.4S, v6.4S
        subhn           v17.4H, v22.4S, v6.4S
        subhn2          v17.8H, v21.4S, v5.4S

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

        sqshrun         v1.8B,  v7.8H, #COL_SHIFT-16
        sqshrun2        v1.16B, v16.8H, #COL_SHIFT-16
        sqshrun         v3.8B,  v17.8H, #COL_SHIFT-16
        sqshrun2        v3.16B, v18.8H, #COL_SHIFT-16

        bl              idct_col4_neon2

        sqshrun         v2.8B,  v7.8H, #COL_SHIFT-16
        sqshrun2        v2.16B, v16.8H, #COL_SHIFT-16
        sqshrun         v4.8B,  v17.8H, #COL_SHIFT-16
        sqshrun2        v4.16B, v18.8H, #COL_SHIFT-16

        zip1            v16.4S, v1.4S, v2.4S
        zip2            v17.4S, v1.4S, v2.4S

        st1             {v16.D}[0], [x0], x1
        st1             {v16.D}[1], [x0], x1

        zip1            v18.4S, v3.4S, v4.4S
        zip2            v19.4S, v3.4S, v4.4S

        st1             {v17.D}[0], [x0], x1
        st1             {v17.D}[1], [x0], x1
        st1             {v18.D}[0], [x0], x1
        st1             {v18.D}[1], [x0], x1
        st1             {v19.D}[0], [x0], x1
        st1             {v19.D}[1], [x0], x1

        idct_end
endfunc

function ff_simple_idct_add_neon, export=1
        idct_start      x2

        idct_row4_neon  v24, v25, v26, v27, 1
        idct_row4_neon  v28, v29, v30, v31, 2
        bl              idct_col4_neon1

        sshr            v1.8H, V7.8H, #COL_SHIFT-16
        sshr            v2.8H, v16.8H, #COL_SHIFT-16
        sshr            v3.8H, v17.8H, #COL_SHIFT-16
        sshr            v4.8H, v18.8H, #COL_SHIFT-16

        bl              idct_col4_neon2

        sshr            v7.8H, V7.8H, #COL_SHIFT-16
        sshr            v16.8H, v16.8H, #COL_SHIFT-16
        sshr            v17.8H, v17.8H, #COL_SHIFT-16
        sshr            v18.8H, v18.8H, #COL_SHIFT-16

        mov             x9,  x0
        ld1             {v19.D}[0], [x0], x1
        zip1            v23.2D, v1.2D, v7.2D
        zip2            v24.2D, v1.2D, v7.2D
        ld1             {v19.D}[1], [x0], x1
        zip1            v25.2D, v2.2D, v16.2D
        zip2            v26.2D, v2.2D, v16.2D
        ld1             {v20.D}[0], [x0], x1
        zip1            v27.2D, v3.2D, v17.2D
        zip2            v28.2D, v3.2D, v17.2D
        ld1             {v20.D}[1], [x0], x1
        zip1            v29.2D, v4.2D, v18.2D
        zip2            v30.2D, v4.2D, v18.2D
        ld1             {v21.D}[0], [x0], x1
        uaddw           v23.8H, v23.8H, v19.8B
        uaddw2          v24.8H, v24.8H, v19.16B
        ld1             {v21.D}[1], [x0], x1
        sqxtun          v23.8B, v23.8H
        sqxtun2         v23.16B, v24.8H
        ld1             {v22.D}[0], [x0], x1
        uaddw           v24.8H, v25.8H, v20.8B
        uaddw2          v25.8H, v26.8H, v20.16B
        ld1             {v22.D}[1], [x0], x1
        sqxtun          v24.8B, v24.8H
        sqxtun2         v24.16B, v25.8H
        st1             {v23.D}[0], [x9], x1
        uaddw           v25.8H, v27.8H, v21.8B
        uaddw2          v26.8H, v28.8H, v21.16B
        st1             {v23.D}[1], [x9], x1
        sqxtun          v25.8B, v25.8H
        sqxtun2         v25.16B, v26.8H
        st1             {v24.D}[0], [x9], x1
        uaddw           v26.8H, v29.8H, v22.8B
        uaddw2          v27.8H, v30.8H, v22.16B
        st1             {v24.D}[1], [x9], x1
        sqxtun          v26.8B, v26.8H
        sqxtun2         v26.16B, v27.8H
        st1             {v25.D}[0], [x9], x1
        st1             {v25.D}[1], [x9], x1
        st1             {v26.D}[0], [x9], x1
        st1             {v26.D}[1], [x9], x1

        idct_end
endfunc

function ff_simple_idct_neon, export=1
        idct_start      x0

        mov             x2,  x0
        idct_row4_neon  v24, v25, v26, v27, 1
        idct_row4_neon  v28, v29, v30, v31, 2
        add             x2, x2, #-128
        bl              idct_col4_neon1

        sshr            v1.8H, v7.8H, #COL_SHIFT-16
        sshr            v2.8H, v16.8H, #COL_SHIFT-16
        sshr            v3.8H, v17.8H, #COL_SHIFT-16
        sshr            v4.8H, v18.8H, #COL_SHIFT-16

        bl              idct_col4_neon2

        sshr            v7.8H, v7.8H, #COL_SHIFT-16
        sshr            v16.8H, v16.8H, #COL_SHIFT-16
        sshr            v17.8H, v17.8H, #COL_SHIFT-16
        sshr            v18.8H, v18.8H, #COL_SHIFT-16

        zip1            v23.2D, v1.2D, v7.2D
        zip2            v24.2D, v1.2D, v7.2D
        st1             {v23.2D,V24.2D}, [x2], #32
        zip1            v25.2D, v2.2D, v16.2D
        zip2            v26.2D, v2.2D, v16.2D
        st1             {v25.2D,V26.2D}, [x2], #32
        zip1            v27.2D, v3.2D, v17.2D
        zip2            v28.2D, v3.2D, v17.2D
        st1             {v27.2D,V28.2D}, [x2], #32
        zip1            v29.2D, v4.2D, v18.2D
        zip2            v30.2D, v4.2D, v18.2D
        st1             {v29.2D,V30.2D}, [x2], #32

        idct_end
endfunc
