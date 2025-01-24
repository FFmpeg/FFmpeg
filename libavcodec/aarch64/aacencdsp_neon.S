/*
 * Copyright (c) 2025 Krzysztof Aleksander Pyrkosz
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

function ff_abs_pow34_neon, export=1
1:
        ld1             {v0.4s}, [x1], #16
        subs            w2, w2, #4
        fabs            v0.4s, v0.4s
        fsqrt           v2.4s, v0.4s
        fmul            v0.4s, v2.4s, v0.4s
        fsqrt           v0.4s, v0.4s
        st1             {v0.4s}, [x0], #16
        b.ne            1b
        ret
endfunc

function ff_aac_quant_bands_neon, export=1
        scvtf           s2, w5
        dup             v1.4s, v1.s[0]
        dup             v2.4s, v2.s[0]
        cbz             w4, 0f
        movi            v5.4s, 0x80, lsl #24
.irp signed,1,0
\signed:
        ld1             {v3.4s}, [x2], #16
        subs            w3, w3, #4
        fmul            v3.4s, v3.4s, v0.s[0]
.if \signed
        ld1             {v4.4s}, [x1], #16
.endif
        fadd            v3.4s, v3.4s, v1.4s
.if \signed
        and             v4.16b, v4.16b, v5.16b
.endif
        fmin            v3.4s, v3.4s, v2.4s
.if \signed
        eor             v3.16b, v4.16b, v3.16b
.endif
        fcvtzs          v3.4s, v3.4s
        st1             {v3.4s}, [x0], #16
        b.ne            \signed\()b
        ret
.endr
endfunc
