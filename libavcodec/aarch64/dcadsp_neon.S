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
