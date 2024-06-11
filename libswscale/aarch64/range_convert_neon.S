/*
 * Copyright (c) 2024 Ramiro Polla
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

.macro lumConvertRange name, max, mult, offset, shift
function ff_\name, export=1
.if \max != 0
        mov             w3, #\max
        dup             v24.8h, w3
.endif
        mov             w3, #\mult
        dup             v25.4s, w3
        movz            w3, #(\offset & 0xffff)
        movk            w3, #((\offset >> 16) & 0xffff), lsl #16
        dup             v26.4s, w3
1:
        ld1             {v0.8h}, [x0]
.if \max != 0
        smin            v0.8h, v0.8h, v24.8h
.endif
        mov             v16.16b, v26.16b
        mov             v18.16b, v26.16b
        sxtl            v20.4s, v0.4h
        sxtl2           v22.4s, v0.8h
        mla             v16.4s, v20.4s, v25.4s
        mla             v18.4s, v22.4s, v25.4s
        shrn            v0.4h, v16.4s, #\shift
        shrn2           v0.8h, v18.4s, #\shift
        subs            w1, w1, #8
        st1             {v0.8h}, [x0], #16
        b.gt            1b
        ret
endfunc
.endm

.macro chrConvertRange name, max, mult, offset, shift
function ff_\name, export=1
.if \max != 0
        mov             w3, #\max
        dup             v24.8h, w3
.endif
        mov             w3, #\mult
        dup             v25.4s, w3
        movz            w3, #(\offset & 0xffff)
        movk            w3, #((\offset >> 16) & 0xffff), lsl #16
        dup             v26.4s, w3
1:
        ld1             {v0.8h}, [x0]
        ld1             {v1.8h}, [x1]
.if \max != 0
        smin            v0.8h, v0.8h, v24.8h
        smin            v1.8h, v1.8h, v24.8h
.endif
        mov             v16.16b, v26.16b
        mov             v17.16b, v26.16b
        mov             v18.16b, v26.16b
        mov             v19.16b, v26.16b
        sxtl            v20.4s, v0.4h
        sxtl            v21.4s, v1.4h
        sxtl2           v22.4s, v0.8h
        sxtl2           v23.4s, v1.8h
        mla             v16.4s, v20.4s, v25.4s
        mla             v17.4s, v21.4s, v25.4s
        mla             v18.4s, v22.4s, v25.4s
        mla             v19.4s, v23.4s, v25.4s
        shrn            v0.4h, v16.4s, #\shift
        shrn            v1.4h, v17.4s, #\shift
        shrn2           v0.8h, v18.4s, #\shift
        shrn2           v1.8h, v19.4s, #\shift
        subs            w2, w2, #8
        st1             {v0.8h}, [x0], #16
        st1             {v1.8h}, [x1], #16
        b.gt            1b
        ret
endfunc
.endm

lumConvertRange lumRangeToJpeg_neon,   30189, 19077, -39057361, 14
chrConvertRange chrRangeToJpeg_neon,   30775,  4663,  -9289992, 12
lumConvertRange lumRangeFromJpeg_neon,     0, 14071,  33561947, 14
chrConvertRange chrRangeFromJpeg_neon,     0,  1799,   4081085, 11
