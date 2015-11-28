/*
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2015 Janne Grunau <janne-libav@jannau.net>
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

function ff_decode_hf_neon, export=1
        add             x2,  x2,  x3
        add             x0,  x0,  x5, lsl #5
        add             x1,  x1,  x5, lsl #2
        add             x4,  x4,  x5, lsl #3
        sub             x6,  x6,  x5
        ldr             w7,  [x1], #4
        add             x7,  x2,  x7, lsl #5
        subs            x6,  x6,  #1
        b.eq            1f
        b.gt            2f
        ret
2:
        ldr             w8,  [x1], #4
        subs            x6,  x6,  #2
        add             x8,  x2,  x8, lsl #5
        ld1             {v2.4s},  [x4], #16
        ld1             {v0.8b},  [x7]
        ld1             {v4.8b},  [x8]
        sxtl            v3.8h,  v0.8b
        sxtl            v7.8h,  v4.8b
        scvtf           v2.4s,  v2.4s,  #4
        sxtl            v0.4s,  v3.4h
        sxtl2           v1.4s,  v3.8h
        sxtl            v4.4s,  v7.4h
        sxtl2           v5.4s,  v7.8h
        scvtf           v0.4s,  v0.4s
        scvtf           v1.4s,  v1.4s
        scvtf           v4.4s,  v4.4s
        scvtf           v5.4s,  v5.4s
        fmul            v0.4s,  v0.4s,  v2.s[0]
        fmul            v1.4s,  v1.4s,  v2.s[0]
        fmul            v4.4s,  v4.4s,  v2.s[2]
        fmul            v5.4s,  v5.4s,  v2.s[2]
        b.lt            10f

        ldr             w7,  [x1], #4
        add             x7,  x2,  x7, lsl #5
        st1             {v0.4s,v1.4s},  [x0], #32
        st1             {v4.4s,v5.4s},  [x0], #32
        b.gt            2b
1:
        ldr             w9,  [x4]
        ld1             {v0.8b},  [x7]
        scvtf           s2,  w9,  #4
        sxtl            v3.8h,  v0.8b
        sxtl            v0.4s,  v3.4h
        sxtl2           v1.4s,  v3.8h
        scvtf           v0.4s,  v0.4s
        scvtf           v1.4s,  v1.4s
        fmul            v0.4s,  v0.4s,  v2.s[0]
        fmul            v1.4s,  v1.4s,  v2.s[0]
        st1             {v0.4s,v1.4s},  [x0]
        ret
10:
        st1             {v0.4s,v1.4s},  [x0], #32
        st1             {v4.4s,v5.4s},  [x0]
        ret
endfunc

function ff_dca_lfe_fir0_neon, export=1
        mov             x3,  #32                // decifactor
        sub             x1,  x1,  #7*4
        add             x4,  x0,  #2*32*4 - 16  // out2
        mov             x7,  #-16

        ld1             {v0.4s,v1.4s}, [x1]
        // reverse [-num_coeffs + 1, 0]
        ext             v3.16b, v0.16b, v0.16b, #8
        ext             v2.16b, v1.16b, v1.16b, #8
        rev64           v3.4s,  v3.4s
        rev64           v2.4s,  v2.4s
1:
        ld1             {v4.4s,v5.4s}, [x2], #32
        ld1             {v6.4s,v7.4s}, [x2], #32
        subs            x3,  x3,  #4
        fmul            v16.4s, v2.4s,  v4.4s
        fmul            v23.4s, v0.4s,  v4.4s
        fmul            v17.4s, v2.4s,  v6.4s
        fmul            v22.4s, v0.4s,  v6.4s

        fmla            v16.4s, v3.4s,  v5.4s
        fmla            v23.4s, v1.4s,  v5.4s
        ld1             {v4.4s,v5.4s}, [x2], #32
        fmla            v17.4s, v3.4s,  v7.4s
        fmla            v22.4s, v1.4s,  v7.4s
        ld1             {v6.4s,v7.4s}, [x2], #32
        fmul            v18.4s, v2.4s,  v4.4s
        fmul            v21.4s, v0.4s,  v4.4s
        fmul            v19.4s, v2.4s,  v6.4s
        fmul            v20.4s, v0.4s,  v6.4s

        fmla            v18.4s, v3.4s,  v5.4s
        fmla            v21.4s, v1.4s,  v5.4s
        fmla            v19.4s, v3.4s,  v7.4s
        fmla            v20.4s, v1.4s,  v7.4s

        faddp           v16.4s, v16.4s, v17.4s
        faddp           v18.4s, v18.4s, v19.4s
        faddp           v20.4s, v20.4s, v21.4s
        faddp           v22.4s, v22.4s, v23.4s
        faddp           v16.4s, v16.4s, v18.4s
        faddp           v20.4s, v20.4s, v22.4s

        st1             {v16.4s}, [x0], #16
        st1             {v20.4s}, [x4], x7
        b.gt            1b

        ret
endfunc

function ff_dca_lfe_fir1_neon, export=1
        mov             x3,  #64                // decifactor
        sub             x1,  x1,  #3*4
        add             x4,  x0,  #2*64*4 - 16  // out2
        mov             x7,  #-16

        ld1             {v0.4s}, [x1]
        // reverse [-num_coeffs + 1, 0]
        ext             v1.16b, v0.16b, v0.16b, #8
        rev64           v1.4s,  v1.4s

1:
        ld1             {v4.4s,v5.4s}, [x2], #32
        ld1             {v6.4s,v7.4s}, [x2], #32
        subs            x3,  x3,  #4
        fmul            v16.4s, v1.4s,  v4.4s
        fmul            v23.4s, v0.4s,  v4.4s
        fmul            v17.4s, v1.4s,  v5.4s
        fmul            v22.4s, v0.4s,  v5.4s
        fmul            v18.4s, v1.4s,  v6.4s
        fmul            v21.4s, v0.4s,  v6.4s
        fmul            v19.4s, v1.4s,  v7.4s
        fmul            v20.4s, v0.4s,  v7.4s
        faddp           v16.4s, v16.4s, v17.4s
        faddp           v18.4s, v18.4s, v19.4s
        faddp           v20.4s, v20.4s, v21.4s
        faddp           v22.4s, v22.4s, v23.4s
        faddp           v16.4s, v16.4s, v18.4s
        faddp           v20.4s, v20.4s, v22.4s
        st1             {v16.4s}, [x0], #16
        st1             {v20.4s}, [x4], x7
        b.gt            1b

        ret
endfunc
