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

.macro lumConvertRange fromto
function ff_lumRange\fromto\()Jpeg_neon, export=1
// x0  int16_t *dst
// w1  int width
// w2  uint32_t coeff
// x3  int64_t offset
        dup             v25.4s, w2
        dup             v26.4s, w3
1:
        ld1             {v0.8h}, [x0]
        mov             v16.16b, v26.16b
        mov             v18.16b, v26.16b
        sxtl            v20.4s, v0.4h
        sxtl2           v22.4s, v0.8h
        mla             v16.4s, v20.4s, v25.4s
        mla             v18.4s, v22.4s, v25.4s
.ifc \fromto, To
        sqshrn          v0.4h, v16.4s, 14
        sqshrn2         v0.8h, v18.4s, 14
.else
        shrn            v0.4h, v16.4s, 14
        shrn2           v0.8h, v18.4s, 14
.endif
        subs            w1, w1, #8
        st1             {v0.8h}, [x0], #16
        b.gt            1b
        ret
endfunc
.endm

.macro chrConvertRange fromto
function ff_chrRange\fromto\()Jpeg_neon, export=1
// x0  int16_t *dstU
// x1  int16_t *dstV
// w2  int width
// w3  uint32_t coeff
// x4  int64_t offset
        dup             v25.4s, w3
        dup             v26.4s, w4
1:
        ld1             {v0.8h}, [x0]
        ld1             {v1.8h}, [x1]
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
.ifc \fromto, To
        sqshrn          v0.4h, v16.4s, 14
        sqshrn          v1.4h, v17.4s, 14
        sqshrn2         v0.8h, v18.4s, 14
        sqshrn2         v1.8h, v19.4s, 14
.else
        shrn            v0.4h, v16.4s, 14
        shrn            v1.4h, v17.4s, 14
        shrn2           v0.8h, v18.4s, 14
        shrn2           v1.8h, v19.4s, 14
.endif
        subs            w2, w2, #8
        st1             {v0.8h}, [x0], #16
        st1             {v1.8h}, [x1], #16
        b.gt            1b
        ret
endfunc
.endm

lumConvertRange To
chrConvertRange To
lumConvertRange From
chrConvertRange From
