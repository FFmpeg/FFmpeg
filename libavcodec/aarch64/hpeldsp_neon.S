/*
 * ARM NEON optimised DSP functions
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2013 Janne Grunau <janne-libav@jannau.net>
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

.macro  pixels16        rnd=1, avg=0
  .if \avg
        mov             x12, x0
  .endif
1:      ld1             {v0.16b},  [x1], x2
        ld1             {v1.16b},  [x1], x2
        ld1             {v2.16b},  [x1], x2
        ld1             {v3.16b},  [x1], x2
  .if \avg
        ld1             {v4.16b},  [x12], x2
        urhadd          v0.16b,  v0.16b,  v4.16b
        ld1             {v5.16b},  [x12], x2
        urhadd          v1.16b,  v1.16b,  v5.16b
        ld1             {v6.16b},  [x12], x2
        urhadd          v2.16b,  v2.16b,  v6.16b
        ld1             {v7.16b},  [x12], x2
        urhadd          v3.16b,  v3.16b,  v7.16b
  .endif
        subs            w3,  w3,  #4
        st1             {v0.16b},  [x0], x2
        st1             {v1.16b},  [x0], x2
        st1             {v2.16b},  [x0], x2
        st1             {v3.16b},  [x0], x2
        b.ne            1b
        ret
.endm

.macro  pixels16_x2     rnd=1, avg=0
1:      ld1             {v0.16b, v1.16b}, [x1], x2
        ld1             {v2.16b, v3.16b}, [x1], x2
        subs            w3,  w3,  #2
        ext             v1.16b,  v0.16b,  v1.16b,  #1
        avg             v0.16b,  v0.16b,  v1.16b
        ext             v3.16b,  v2.16b,  v3.16b,  #1
        avg             v2.16b,  v2.16b,  v3.16b
  .if \avg
        ld1             {v1.16b}, [x0], x2
        ld1             {v3.16b}, [x0]
        urhadd          v0.16b,  v0.16b,  v1.16b
        urhadd          v2.16b,  v2.16b,  v3.16b
        sub             x0,  x0,  x2
  .endif
        st1             {v0.16b}, [x0], x2
        st1             {v2.16b}, [x0], x2
        b.ne            1b
        ret
.endm

.macro  pixels16_y2     rnd=1, avg=0
        sub             w3,  w3,  #2
        ld1             {v0.16b}, [x1], x2
        ld1             {v1.16b}, [x1], x2
1:      subs            w3,  w3,  #2
        avg             v2.16b,  v0.16b,  v1.16b
        ld1             {v0.16b}, [x1], x2
        avg             v3.16b,  v0.16b,  v1.16b
        ld1             {v1.16b}, [x1], x2
  .if \avg
        ld1             {v4.16b}, [x0], x2
        ld1             {v5.16b}, [x0]
        urhadd          v2.16b,  v2.16b,  v4.16b
        urhadd          v3.16b,  v3.16b,  v5.16b
        sub             x0,  x0,  x2
  .endif
        st1             {v2.16b}, [x0], x2
        st1             {v3.16b}, [x0], x2
        b.ne            1b

        avg             v2.16b,  v0.16b,  v1.16b
        ld1             {v0.16b}, [x1], x2
        avg             v3.16b,  v0.16b,  v1.16b
  .if \avg
        ld1             {v4.16b}, [x0], x2
        ld1             {v5.16b}, [x0]
        urhadd          v2.16b,  v2.16b,  v4.16b
        urhadd          v3.16b,  v3.16b,  v5.16b
        sub             x0,  x0,  x2
  .endif
        st1             {v2.16b},     [x0], x2
        st1             {v3.16b},     [x0], x2

        ret
.endm

.macro  pixels16_xy2    rnd=1, avg=0
        sub             w3,  w3,  #2
        ld1             {v0.16b, v1.16b}, [x1], x2
        ld1             {v4.16b, v5.16b}, [x1], x2
NRND    movi            v26.8H, #1
        ext             v1.16b,  v0.16b,  v1.16b,  #1
        ext             v5.16b,  v4.16b,  v5.16b,  #1
        uaddl           v16.8h,  v0.8b,   v1.8b
        uaddl2          v20.8h,  v0.16b,  v1.16b
        uaddl           v18.8h,  v4.8b,   v5.8b
        uaddl2          v22.8h,  v4.16b,  v5.16b
1:      subs            w3,  w3,  #2
        ld1             {v0.16b, v1.16b}, [x1], x2
        add             v24.8h,  v16.8h,  v18.8h
NRND    add             v24.8H,  v24.8H,  v26.8H
        ext             v30.16b, v0.16b,  v1.16b,  #1
        add             v1.8h,   v20.8h,  v22.8h
        mshrn           v28.8b,  v24.8h,  #2
NRND    add             v1.8H,   v1.8H,   v26.8H
        mshrn2          v28.16b, v1.8h,   #2
  .if \avg
        ld1             {v16.16b},        [x0]
        urhadd          v28.16b, v28.16b, v16.16b
  .endif
        uaddl           v16.8h,  v0.8b,   v30.8b
        ld1             {v2.16b, v3.16b}, [x1], x2
        uaddl2          v20.8h,  v0.16b,  v30.16b
        st1             {v28.16b},        [x0], x2
        add             v24.8h,  v16.8h,  v18.8h
NRND    add             v24.8H,  v24.8H,  v26.8H
        ext             v3.16b,  v2.16b,  v3.16b,  #1
        add             v0.8h,   v20.8h,  v22.8h
        mshrn           v30.8b,  v24.8h,  #2
NRND    add             v0.8H,   v0.8H,   v26.8H
        mshrn2          v30.16b, v0.8h,   #2
  .if \avg
        ld1             {v18.16b},        [x0]
        urhadd          v30.16b, v30.16b, v18.16b
  .endif
        uaddl           v18.8h,   v2.8b,  v3.8b
        uaddl2          v22.8h,   v2.16b, v3.16b
        st1             {v30.16b},        [x0], x2
        b.gt            1b

        ld1             {v0.16b, v1.16b}, [x1], x2
        add             v24.8h,  v16.8h,  v18.8h
NRND    add             v24.8H,  v24.8H,  v26.8H
        ext             v30.16b, v0.16b,  v1.16b,  #1
        add             v1.8h,   v20.8h,  v22.8h
        mshrn           v28.8b,  v24.8h,  #2
NRND    add             v1.8H,   v1.8H,   v26.8H
        mshrn2          v28.16b, v1.8h,   #2
  .if \avg
        ld1             {v16.16b},        [x0]
        urhadd          v28.16b, v28.16b, v16.16b
  .endif
        uaddl           v16.8h,  v0.8b,   v30.8b
        uaddl2          v20.8h,  v0.16b,  v30.16b
        st1             {v28.16b},        [x0], x2
        add             v24.8h,  v16.8h,  v18.8h
NRND    add             v24.8H,  v24.8H,  v26.8H
        add             v0.8h,   v20.8h,  v22.8h
        mshrn           v30.8b,  v24.8h,  #2
NRND    add             v0.8H,   v0.8H,   v26.8H
        mshrn2          v30.16b, v0.8h,   #2
  .if \avg
        ld1             {v18.16b},        [x0]
        urhadd          v30.16b, v30.16b, v18.16b
  .endif
        st1             {v30.16b},        [x0], x2

        ret
.endm

.macro  pixels8         rnd=1, avg=0
1:      ld1             {v0.8b}, [x1], x2
        ld1             {v1.8b}, [x1], x2
        ld1             {v2.8b}, [x1], x2
        ld1             {v3.8b}, [x1], x2
  .if \avg
        ld1             {v4.8b}, [x0], x2
        urhadd          v0.8b,  v0.8b,  v4.8b
        ld1             {v5.8b}, [x0], x2
        urhadd          v1.8b,  v1.8b,  v5.8b
        ld1             {v6.8b}, [x0], x2
        urhadd          v2.8b,  v2.8b,  v6.8b
        ld1             {v7.8b}, [x0], x2
        urhadd          v3.8b,  v3.8b,  v7.8b
        sub             x0,  x0,  x2,  lsl #2
  .endif
        subs            w3,  w3,  #4
        st1             {v0.8b}, [x0], x2
        st1             {v1.8b}, [x0], x2
        st1             {v2.8b}, [x0], x2
        st1             {v3.8b}, [x0], x2
        b.ne            1b
        ret
.endm

.macro  pixels8_x2      rnd=1, avg=0
1:      ld1             {v0.8b, v1.8b}, [x1], x2
        ext             v1.8b,  v0.8b,  v1.8b,  #1
        ld1             {v2.8b, v3.8b}, [x1], x2
        ext             v3.8b,  v2.8b,  v3.8b,  #1
        subs            w3,  w3,  #2
        avg             v0.8b,   v0.8b,   v1.8b
        avg             v2.8b,   v2.8b,   v3.8b
  .if \avg
        ld1             {v4.8b},     [x0], x2
        ld1             {v5.8b},     [x0]
        urhadd          v0.8b,   v0.8b,   v4.8b
        urhadd          v2.8b,   v2.8b,   v5.8b
        sub             x0,  x0,  x2
  .endif
        st1             {v0.8b}, [x0], x2
        st1             {v2.8b}, [x0], x2
        b.ne            1b
        ret
.endm

.macro  pixels8_y2      rnd=1, avg=0
        sub             w3,  w3,  #2
        ld1             {v0.8b},  [x1], x2
        ld1             {v1.8b},  [x1], x2
1:      subs            w3,  w3,  #2
        avg             v4.8b,  v0.8b,  v1.8b
        ld1             {v0.8b},  [x1], x2
        avg             v5.8b,  v0.8b,  v1.8b
        ld1             {v1.8b},  [x1], x2
  .if \avg
        ld1             {v2.8b},     [x0], x2
        ld1             {v3.8b},     [x0]
        urhadd          v4.8b,  v4.8b,  v2.8b
        urhadd          v5.8b,  v5.8b,  v3.8b
        sub             x0,  x0,  x2
  .endif
        st1             {v4.8b},     [x0], x2
        st1             {v5.8b},     [x0], x2
        b.ne            1b

        avg             v4.8b,  v0.8b,  v1.8b
        ld1             {v0.8b},  [x1], x2
        avg             v5.8b,  v0.8b,  v1.8b
  .if \avg
        ld1             {v2.8b},     [x0], x2
        ld1             {v3.8b},     [x0]
        urhadd          v4.8b,  v4.8b,  v2.8b
        urhadd          v5.8b,  v5.8b,  v3.8b
        sub             x0,  x0,  x2
  .endif
        st1             {v4.8b},     [x0], x2
        st1             {v5.8b},     [x0], x2

        ret
.endm

.macro  pixels8_xy2     rnd=1, avg=0
        sub             w3,  w3,  #2
        ld1             {v0.16b},     [x1], x2
        ld1             {v1.16b},     [x1], x2
NRND    movi            v19.8H, #1
        ext             v4.16b,  v0.16b,  v4.16b,  #1
        ext             v6.16b,  v1.16b,  v6.16b,  #1
        uaddl           v16.8h,  v0.8b,  v4.8b
        uaddl           v17.8h,  v1.8b,  v6.8b
1:      subs            w3,  w3,  #2
        ld1             {v0.16b},     [x1], x2
        add             v18.8h, v16.8h,  v17.8h
        ext             v4.16b,  v0.16b,  v4.16b,  #1
NRND    add             v18.8H, v18.8H, v19.8H
        uaddl           v16.8h,  v0.8b,  v4.8b
        mshrn           v5.8b,  v18.8h, #2
        ld1             {v1.16b},     [x1], x2
        add             v18.8h, v16.8h,  v17.8h
  .if \avg
        ld1             {v7.8b},     [x0]
        urhadd          v5.8b,  v5.8b,  v7.8b
  .endif
NRND    add             v18.8H, v18.8H, v19.8H
        st1             {v5.8b},     [x0], x2
        mshrn           v7.8b,  v18.8h, #2
  .if \avg
        ld1             {v5.8b},     [x0]
        urhadd          v7.8b,  v7.8b,  v5.8b
  .endif
        ext             v6.16b,  v1.16b,  v6.16b,  #1
        uaddl           v17.8h,  v1.8b,   v6.8b
        st1             {v7.8b},     [x0], x2
        b.gt            1b

        ld1             {v0.16b},     [x1], x2
        add             v18.8h, v16.8h, v17.8h
        ext             v4.16b, v0.16b, v4.16b,  #1
NRND    add             v18.8H, v18.8H, v19.8H
        uaddl           v16.8h,  v0.8b, v4.8b
        mshrn           v5.8b,  v18.8h, #2
        add             v18.8h, v16.8h, v17.8h
  .if \avg
        ld1             {v7.8b},     [x0]
        urhadd          v5.8b,  v5.8b,  v7.8b
  .endif
NRND    add             v18.8H, v18.8H, v19.8H
        st1             {v5.8b},     [x0], x2
        mshrn           v7.8b,  v18.8h, #2
  .if \avg
        ld1             {v5.8b},     [x0]
        urhadd          v7.8b,  v7.8b,  v5.8b
  .endif
        st1             {v7.8b},     [x0], x2

        ret
.endm

.macro  pixfunc         pfx, name, suf, rnd=1, avg=0
  .if \rnd
    .macro avg  rd, rn, rm
        urhadd          \rd, \rn, \rm
    .endm
    .macro mshrn rd, rn, rm
        rshrn           \rd, \rn, \rm
    .endm
    .macro mshrn2 rd, rn, rm
        rshrn2          \rd, \rn, \rm
    .endm
    .macro NRND insn:vararg
    .endm
  .else
    .macro avg  rd, rn, rm
        uhadd           \rd, \rn, \rm
    .endm
    .macro mshrn rd, rn, rm
        shrn            \rd, \rn, \rm
    .endm
    .macro mshrn2 rd, rn, rm
        shrn2           \rd, \rn, \rm
    .endm
    .macro NRND insn:vararg
        \insn
    .endm
  .endif
function ff_\pfx\name\suf\()_neon, export=1
        \name           \rnd, \avg
endfunc
        .purgem         avg
        .purgem         mshrn
        .purgem         mshrn2
        .purgem         NRND
.endm

.macro  pixfunc2        pfx, name, avg=0
        pixfunc         \pfx, \name,          rnd=1, avg=\avg
        pixfunc         \pfx, \name, _no_rnd, rnd=0, avg=\avg
.endm

function ff_put_h264_qpel16_mc00_neon, export=1
        mov             w3,  #16
endfunc

        pixfunc         put_, pixels16,     avg=0
        pixfunc2        put_, pixels16_x2,  avg=0
        pixfunc2        put_, pixels16_y2,  avg=0
        pixfunc2        put_, pixels16_xy2, avg=0

function ff_avg_h264_qpel16_mc00_neon, export=1
        mov             w3,  #16
endfunc

        pixfunc         avg_, pixels16,     avg=1
        pixfunc2        avg_, pixels16_x2,  avg=1
        pixfunc2        avg_, pixels16_y2,  avg=1
        pixfunc2        avg_, pixels16_xy2, avg=1

function ff_put_h264_qpel8_mc00_neon, export=1
        mov             w3,  #8
endfunc

        pixfunc         put_, pixels8,     avg=0
        pixfunc2        put_, pixels8_x2,  avg=0
        pixfunc2        put_, pixels8_y2,  avg=0
        pixfunc2        put_, pixels8_xy2, avg=0

function ff_avg_h264_qpel8_mc00_neon, export=1
        mov             w3,  #8
endfunc

        pixfunc         avg_, pixels8,     avg=1
        pixfunc         avg_, pixels8_x2,  avg=1
        pixfunc         avg_, pixels8_y2,  avg=1
        pixfunc         avg_, pixels8_xy2, avg=1
