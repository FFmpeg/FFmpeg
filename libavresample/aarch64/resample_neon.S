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
#include "asm-offsets.h"

.macro resample_one     fmt, es=2
.ifnc \fmt, dbl
    .macro  M_MUL2      x:vararg
    .endm
    .macro  M_MLA2      x:vararg
    .endm
.endif
function ff_resample_one_\fmt\()_neon, export=1
        sxtw            x2,  w2
        ldr             x9,  [x0, #FILTER_BANK]
        ldr             w6,  [x0, #FILTER_LENGTH]
        ldp             w7,  w8,  [x0, #PHASE_SHIFT]    // and phase_mask
        lsr             x10, x4,  x7                    // sample_index
        and             x4,  x4,  x8
        lsl             x11, x6,  #\es          // filter_length * elem_size
        add             x3,  x3,  x10, lsl #\es // src[sample_index]
        madd            x9,  x11, x4,  x9       // filter
        cmp             w6,  #16
        b.lt            5f
8:      // remaining filter_length at least 16
        subs            w6,  w6,  #16
        LOAD8           v4,  v5,  v6,  v7,  x3
        LOAD8           v16, v17, v18, v19, x9
        M_MUL           v0,  v4,  v16, v1
        M_MUL2          v1,  v6,  v18
7:
        LOAD8           v20, v21, v22, v23, x3
        M_MLA           v0,  v5,  v17, v1
        M_MLA2          v1,  v7,  v19
        LOAD8           v24, v25, v26, v27, x9
        M_MLA           v0,  v20, v24, v1
        M_MLA2          v1,  v22, v26
        b.eq            6f
        cmp             w6,  #16
        M_MLA           v0,  v21, v25, v1
        M_MLA2          v1,  v23, v27
        b.lt            4f
        subs            w6,  w6,  #16
        LOAD8           v4,  v5,  v6,  v7,  x3
        LOAD8           v16, v17, v18, v19, x9
        M_MLA           v0,  v4,  v16, v1
        M_MLA2          v1,  v6,  v18
        b               7b
6:
        M_MLA           v0,  v21, v25,  v1
        M_MLA2          v1,  v23, v27
        STORE_ONE       0,   x1,  x2,   v1
        ret
5:
        movi            v0.16b, #0
        movi            v1.16b, #0
4:      // remaining filter_length 1-15
        cmp             w6,  #4
        b.lt            2f
        subs            w6,  w6,  #4
        LOAD4           v4,  v5,  x3
        LOAD4           v6,  v7,  x9
        M_MLA           v0,  v4,  v6,  v1
        M_MLA2          v1,  v5,  v7
        b.eq            0f
        b               4b
2:      // remaining filter_length 1-3
        cmp             w6,  #2
        b.lt            1f
        LOAD2           2,   x3
        LOAD2           3,   x9
        subs            w6,  w6,  #2
        M_MLA           v0,  v2,  v3
        b.eq            0f
1:      // remaining filter_length 1
        LOAD1           6,   x3
        LOAD1           7,   x9
        M_MLA           v0,  v6,  v7
0:
        STORE_ONE       0,   x1,  x2,  v1
        ret
endfunc

.purgem LOAD1
.purgem LOAD2
.purgem LOAD4
.purgem LOAD8
.purgem M_MLA
.purgem M_MLA2
.purgem M_MUL
.purgem M_MUL2
.purgem STORE_ONE
.endm


.macro  LOAD1           d1, addr
        ldr             d\d1, [\addr], #8
.endm
.macro  LOAD2           d1, addr
        ld1             {v\d1\().2d}, [\addr], #16
.endm
.macro  LOAD4           d1, d2, addr
        ld1             {\d1\().2d,\d2\().2d}, [\addr], #32
.endm
.macro  LOAD8           d1, d2, d3, d4, addr
        ld1             {\d1\().2d,\d2\().2d,\d3\().2d,\d4\().2d}, [\addr], #64
.endm
.macro  M_MLA           d, r0, r1, d2:vararg
        fmla            \d\().2d, \r0\().2d, \r1\().2d
.endm
.macro  M_MLA2          second:vararg
        M_MLA           \second
.endm
.macro  M_MUL           d, r0, r1, d2:vararg
        fmul            \d\().2d, \r0\().2d, \r1\().2d
.endm
.macro  M_MUL2          second:vararg
        M_MUL           \second
.endm
.macro  STORE_ONE       rn, addr, idx, d2
        fadd            v\rn\().2d,  v\rn\().2d,  \d2\().2d
        faddp           d\rn\(),  v\rn\().2d
        str             d\rn\(),  [\addr, \idx, lsl #3]
.endm

resample_one dbl, 3


.macro  LOAD1           d1, addr
        ldr             s\d1, [\addr], #4
.endm
.macro  LOAD2           d1, addr
        ld1             {v\d1\().2s}, [\addr], #8
.endm
.macro  LOAD4           d1, d2, addr
        ld1             {\d1\().4s}, [\addr], #16
.endm
.macro  LOAD8           d1, d2, d3, d4, addr
        ld1             {\d1\().4s,\d2\().4s}, [\addr], #32
.endm
.macro  M_MLA           d, r0, r1, d2:vararg
        fmla            \d\().4s, \r0\().4s, \r1\().4s
.endm
.macro  M_MUL           d, r0, r1, d2:vararg
        fmul            \d\().4s, \r0\().4s, \r1\().4s
.endm
.macro  STORE_ONE       rn, addr, idx, d2
        faddp           v\rn\().4s,  v\rn\().4s,  v\rn\().4s
        faddp           s\rn\(),  v\rn\().2s
        str             s\rn\(),  [\addr, \idx, lsl #2]
.endm

resample_one flt


.macro  LOAD1           d1, addr
        ldr             h\d1, [\addr], #2
.endm
.macro  LOAD2           d1, addr
        ldr             s\d1, [\addr], #4
.endm
.macro  LOAD4           d1, d2, addr
        ld1             {\d1\().4h}, [\addr], #8
.endm
.macro  LOAD8           d1, d2, d3, d4, addr
        ld1             {\d1\().4h,\d2\().4h}, [\addr], #16
.endm
.macro  M_MLA           d, r0, r1, d2:vararg
        smlal           \d\().4s, \r0\().4h, \r1\().4h
.endm
.macro  M_MUL           d, r0, r1, d2:vararg
        smull           \d\().4s, \r0\().4h, \r1\().4h
.endm
.macro  STORE_ONE       rn, addr, idx, d2
        addp            v\rn\().4s,  v\rn\().4s,  v\rn\().4s
        addp            v\rn\().4s,  v\rn\().4s,  v\rn\().4s
        sqrshrn         v\rn\().4h,  v\rn\().4s,  #15
        str             h\rn\(),  [\addr, \idx, lsl #1]
.endm

resample_one s16, 1


.macro  LOAD1           d1, addr
        ldr             s\d1, [\addr], #4
.endm
.macro  LOAD2           d1, addr
        ld1             {v\d1\().2s}, [\addr], #8
.endm
.macro  LOAD4           d1, d2, addr
        ld1             {\d1\().4s}, [\addr], #16
.endm
.macro  LOAD8           d1, d2, d3, d4, addr
        ld1             {\d1\().4s,\d2\().4s}, [\addr], #32
.endm
.macro  M_MLA           d1, r0, r1, d2:vararg
        smlal           \d1\().2d, \r0\().2s, \r1\().2s
.ifnb \d2
        smlal2          \d2\().2d, \r0\().4s, \r1\().4s
.endif
.endm
.macro  M_MUL           d1, r0, r1, d2:vararg
        smull           \d1\().2d, \r0\().2s, \r1\().2s
.ifnb \d2
        smull2          \d2\().2d, \r0\().4s, \r1\().4s
.endif
.endm
.macro  STORE_ONE       rn, addr, idx, d2
        add             v\rn\().2d,  v\rn\().2d,  \d2\().2d
        addp            d\rn\(),     v\rn\().2d
        sqrshrn         v\rn\().2s,  v\rn\().2d,  #30
        str             s\rn\(),  [\addr, \idx, lsl #2]
.endm

resample_one s32
