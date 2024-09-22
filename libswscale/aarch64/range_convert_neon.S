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

.macro lumConvertRange fromto, bit_depth
function ff_lumRange\fromto\()Jpeg\bit_depth\()_neon, export=1
// x0  int16_t *dst
// w1  int width
// w2  uint32_t coeff
// x3  int64_t offset
.if \bit_depth == 16
.ifc \fromto, To
        movi            v25.4s, #1
        movi            v24.4s, #1<<3, lsl #16
        sub             v24.4s, v24.4s, v25.4s
.endif
        dup             v25.4s, w2
        dup             v26.2d, x3
1:
        ld1             {v0.4s, v1.4s}, [x0]
        mov             v16.16b, v26.16b
        mov             v17.16b, v26.16b
        mov             v18.16b, v26.16b
        mov             v19.16b, v26.16b
        smlal           v16.2d, v0.2s, v25.2s
        smlal2          v17.2d, v0.4s, v25.4s
        smlal           v18.2d, v1.2s, v25.2s
        smlal2          v19.2d, v1.4s, v25.4s
        shrn            v0.2s, v16.2d, 18
        shrn2           v0.4s, v17.2d, 18
        shrn            v1.2s, v18.2d, 18
        shrn2           v1.4s, v19.2d, 18
        subs            w1, w1, #8
.ifc \fromto, To
        smin            v0.4s, v0.4s, v24.4s
        smin            v1.4s, v1.4s, v24.4s
.endif
        st1             {v0.4s, v1.4s}, [x0], #32
        b.gt            1b
.else
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
.endif
        ret
endfunc
.endm

.macro chrConvertRange fromto, bit_depth
function ff_chrRange\fromto\()Jpeg\bit_depth\()_neon, export=1
// x0  int16_t *dstU
// x1  int16_t *dstV
// w2  int width
// w3  uint32_t coeff
// x4  int64_t offset
.if \bit_depth == 16
.ifc \fromto, To
        movi            v25.4s, #1
        movi            v24.4s, #1<<3, lsl #16
        sub             v24.4s, v24.4s, v25.4s
.endif
        dup             v25.4s, w3
        dup             v26.2d, x4
1:
        ld1             {v0.4s, v1.4s}, [x0]
        ld1             {v2.4s, v3.4s}, [x1]
        mov             v16.16b, v26.16b
        mov             v17.16b, v26.16b
        mov             v18.16b, v26.16b
        mov             v19.16b, v26.16b
        mov             v20.16b, v26.16b
        mov             v21.16b, v26.16b
        mov             v22.16b, v26.16b
        mov             v23.16b, v26.16b
        smlal           v16.2d, v0.2s, v25.2s
        smlal2          v17.2d, v0.4s, v25.4s
        smlal           v18.2d, v1.2s, v25.2s
        smlal2          v19.2d, v1.4s, v25.4s
        smlal           v20.2d, v2.2s, v25.2s
        smlal2          v21.2d, v2.4s, v25.4s
        smlal           v22.2d, v3.2s, v25.2s
        smlal2          v23.2d, v3.4s, v25.4s
        shrn            v0.2s, v16.2d, 18
        shrn2           v0.4s, v17.2d, 18
        shrn            v1.2s, v18.2d, 18
        shrn2           v1.4s, v19.2d, 18
        shrn            v2.2s, v20.2d, 18
        shrn2           v2.4s, v21.2d, 18
        shrn            v3.2s, v22.2d, 18
        shrn2           v3.4s, v23.2d, 18
        subs            w2, w2, #8
.ifc \fromto, To
        smin            v0.4s, v0.4s, v24.4s
        smin            v1.4s, v1.4s, v24.4s
        smin            v2.4s, v2.4s, v24.4s
        smin            v3.4s, v3.4s, v24.4s
.endif
        st1             {v0.4s, v1.4s}, [x0], #32
        st1             {v2.4s, v3.4s}, [x1], #32
        b.gt            1b
.else
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
.endif
        ret
endfunc
.endm

lumConvertRange To,    8
lumConvertRange To,   16
chrConvertRange To,    8
chrConvertRange To,   16
lumConvertRange From,  8
lumConvertRange From, 16
chrConvertRange From,  8
chrConvertRange From, 16
