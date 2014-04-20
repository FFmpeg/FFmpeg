/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/aarch64/asm.S"

function ff_vorbis_inverse_coupling_neon, export=1
        movi            v20.4s,  #1<<7, lsl #24
        subs            x2,  x2,  #4
        mov             x3,  x0
        mov             x4,  x1
        b.eq            3f

        ld1             {v7.4s},  [x1], #16
        ld1             {v6.4s},  [x0], #16
        cmle            v4.4s,  v7.4s,  #0
        and             v5.16b, v6.16b, v20.16b
        eor             v7.16b, v7.16b, v5.16b
        and             v2.16b, v7.16b, v4.16b
        bic             v3.16b, v7.16b, v4.16b
        fadd            v7.4s,  v6.4s,  v2.4s
        fsub            v6.4s,  v6.4s,  v3.4s
1:      ld1             {v1.4s},  [x1], #16
        ld1             {v0.4s},  [x0], #16
        cmle            v4.4s,  v1.4s,  #0
        and             v5.16b, v0.16b, v20.16b
        eor             v1.16b, v1.16b, v5.16b
        st1             {v7.4s},  [x3], #16
        st1             {v6.4s},  [x4], #16
        and             v2.16b, v1.16b, v4.16b
        bic             v3.16b, v1.16b, v4.16b
        fadd            v1.4s,  v0.4s,  v2.4s
        fsub            v0.4s,  v0.4s,  v3.4s
        subs            x2,  x2,  #8
        b.le            2f
        ld1             {v7.4s},  [x1], #16
        ld1             {v6.4s},  [x0], #16
        cmle            v4.4s,  v7.4s,  #0
        and             v5.16b, v6.16b, v20.16b
        eor             v7.16b, v7.16b, v5.16b
        st1             {v1.4s},  [x3], #16
        st1             {v0.4s},  [x4], #16
        and             v2.16b, v7.16b, v4.16b
        bic             v3.16b, v7.16b, v4.16b
        fadd            v7.4s,  v6.4s,  v2.4s
        fsub            v6.4s,  v6.4s,  v3.4s
        b               1b

2:      st1             {v1.4s},  [x3], #16
        st1             {v0.4s},  [x4], #16
        b.lt            ret

3:      ld1             {v1.4s},  [x1]
        ld1             {v0.4s},  [x0]
        cmle            v4.4s,  v1.4s,  #0
        and             v5.16b, v0.16b, v20.16b
        eor             v1.16b, v1.16b, v5.16b
        and             v2.16b, v1.16b, v4.16b
        bic             v3.16b, v1.16b, v4.16b
        fadd            v1.4s,  v0.4s,  v2.4s
        fsub            v0.4s,  v0.4s,  v3.4s
        st1             {v1.4s},  [x0], #16
        st1             {v0.4s},  [x1], #16
ret:
        ret
endfunc
