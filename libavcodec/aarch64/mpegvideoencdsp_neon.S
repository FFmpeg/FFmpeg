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

function ff_pix_sum16_neon, export=1
// x0  const uint8_t *pix
// x1  ptrdiff_t line_size

        add             x2, x0, x1
        lsl             x1, x1, #1
        movi            v0.16b, #0
        mov             w3, #16

1:
        ld1             {v1.16b}, [x0], x1
        ld1             {v2.16b}, [x2], x1
        subs            w3, w3, #2
        uadalp          v0.8h, v1.16b
        uadalp          v0.8h, v2.16b
        b.ne            1b

        uaddlv          s0, v0.8h
        fmov            w0, s0

        ret
endfunc

function ff_pix_norm1_neon, export=1
// x0  const uint8_t *pix
// x1  ptrdiff_t line_size

        movi            v0.16b, #0
        mov             w2, #16

1:
        ld1             {v1.16b}, [x0], x1
        subs            w2, w2, #1
        umull           v2.8h, v1.8b,  v1.8b
        umull2          v3.8h, v1.16b, v1.16b
        uadalp          v0.4s, v2.8h
        uadalp          v0.4s, v3.8h
        b.ne            1b

        uaddlv          d0, v0.4s
        fmov            w0, s0

        ret
endfunc

#if HAVE_DOTPROD
ENABLE_DOTPROD

function ff_pix_norm1_neon_dotprod, export=1
// x0  const uint8_t *pix
// x1  ptrdiff_t line_size

        movi            v0.16b, #0
        mov             w2, #16

1:
        ld1             {v1.16b}, [x0], x1
        ld1             {v2.16b}, [x0], x1
        udot            v0.4s, v1.16b, v1.16b
        subs            w2, w2, #2
        udot            v0.4s, v2.16b, v2.16b
        b.ne            1b

        uaddlv          d0, v0.4s
        fmov            w0, s0

        ret
endfunc

DISABLE_DOTPROD
#endif
