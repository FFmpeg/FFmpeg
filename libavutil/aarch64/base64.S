/*
 * Copyright (c) 2026 Shreesh Adiga <16567adigashreesh@gmail.com>
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

#include "asm.S"

function ff_base64_encode_neon, export=1
        // x0 - char *out
        // x1 - const char *in
        // w2 - int in_size
        // x3 - const char *tbl
        uxtw            x2, w2
        cmp             x2, #48
        add             x4, x2, x1 // end = src + in_size
        mov             x5, x0     // dst = out
        b.lt            2f         // skip NEON loop
        ld1             { v0.16b - v3.16b }, [x3]
        movi            v4.16b, #63
        sub             x6, x4, #48

1: // NEON 48bytes loop
        ld3             { v5.16b - v7.16b }, [x1], #48
        ushr            v18.16b, v5.16b, #2
        ushr            v16.16b, v6.16b, #4
        tbl             v18.16b, { v0.16b, v1.16b, v2.16b, v3.16b }, v18.16b
        ushr            v17.16b, v7.16b, #6
        sli             v16.16b, v5.16b, #4
        and             v5.16b, v7.16b, v4.16b
        sli             v17.16b, v6.16b, #2
        tbl             v21.16b, { v0.16b, v1.16b, v2.16b, v3.16b }, v5.16b
        and             v16.16b, v16.16b, v4.16b
        cmp             x1, x6
        tbl             v19.16b, { v0.16b, v1.16b, v2.16b, v3.16b }, v16.16b
        and             v17.16b, v17.16b, v4.16b
        tbl             v20.16b, { v0.16b, v1.16b, v2.16b, v3.16b }, v17.16b
        st4             { v18.16b - v21.16b }, [x5], #64
        b.ls            1b // NEON 48bytes loop

        sub             x6, x4, #24
        cmp             x1, x6
        b.gt            2f

        // process 24 bytes using half width vector
        ld3             { v5.8b - v7.8b }, [x1], #24
        ushr            v18.8b, v5.8b, #2
        ushr            v16.8b, v6.8b, #4
        tbl             v18.8b, { v0.16b, v1.16b, v2.16b, v3.16b }, v18.8b
        ushr            v17.8b, v7.8b, #6
        sli             v16.8b, v5.8b, #4
        and             v5.8b, v7.8b, v4.8b
        sli             v17.8b, v6.8b, #2
        tbl             v21.8b, { v0.16b, v1.16b, v2.16b, v3.16b }, v5.8b
        and             v16.8b, v16.8b, v4.8b
        tbl             v19.8b, { v0.16b, v1.16b, v2.16b, v3.16b }, v16.8b
        and             v17.8b, v17.8b, v4.8b
        tbl             v20.8b, { v0.16b, v1.16b, v2.16b, v3.16b }, v17.8b
        st4             { v18.8b - v21.8b }, [x5], #32
2: // skip NEON loop
        sub             x6, x4, #4
        cmp             x1, x6
        b.gt            4f // <4 bytes tail

3: // 4bytes available loop
        ldr             w7, [x1], #3
        rev             w7, w7
        lsr             x8, x7, #26
        ubfx            x9, x7, #20, #6
        ldrb            w12, [x3, x8]
        ubfx            x10, x7, #14, #6
        ldrb            w13, [x3, x9]
        ubfx            x11, x7, #8, #6
        ldrb            w14, [x3, x10]
        orr             w12, w12, w13, lsl #8
        cmp             x1, x6
        ldrb            w15, [x3, x11]
        orr             w12, w12, w14, lsl #16
        orr             w12, w12, w15, lsl #24
        str             w12, [x5], #4
        b.ls            3b // 4bytes available loop

4: // <4 bytes tail
        sub             x6, x4, x1
        cmp             x6, #3
        b.eq            7f
        cmp             x6, #2
        b.eq            6f
        cmp             x6, #1
        b.eq            5f
        strb            wzr, [x5]
        ret

5:
        // 1 byte input remaining, 2 bytes padding needed
        ldrb            w7, [x1]
        lsr             x8, x7, #2
        ubfiz           x9, x7, #4, #2
        ldrb            w8, [x3, x8]
        movz            w10, #0x3d3d, lsl #16
        ldrb            w9, [x3, x9]
        orr             w10, w10, w8
        orr             w10, w10, w9, lsl #8
        str             w10, [x5]
        strb            wzr, [x5, #4]
        ret

6:
        // 2 bytes input remaining, 1 byte padding needed
        ldrb            w7, [x1]
        ldrb            w8, [x1, #1]
        lsr             w9, w7, #2
        lsr             w10, w8, #4
        ubfiz           w11, w8, #2, #4
        ldrb            w12, [x3, x9]
        bfi             w10, w7, #4, #2
        ldrb            w14, [x3, x11]
        movz            w15, #0x3d00, lsl #16
        ldrb            w13, [x3, x10]
        orr             w15, w15, w12
        orr             w15, w15, w14, lsl #16
        orr             w15, w15, w13, lsl #8
        str             w15, [x5]
        strb            wzr, [x5, #4]
        ret

7:
        // 3 bytes input remaining, no padding needed
        ldrb            w7, [x1]
        ldrb            w8, [x1, #1]
        lsr             w10, w7, #2
        ldrb            w9, [x1, #2]
        lsr             w11, w8, #4
        ldrb            w10, [x3, x10]
        lsr             w12, w9, #6
        bfi             w11, w7, #4, #2
        bfi             w12, w8, #2, #4
        ldrb            w11, [x3, x11]
        and             w13, w9, #0x3f
        ldrb            w12, [x3, x12]
        orr             w10, w10, w11, lsl #8
        ldrb            w13, [x3, x13]
        orr             w10, w10, w12, lsl #16
        orr             w10, w10, w13, lsl #24
        str             w10, [x5]
        strb            wzr, [x5, #4]
        ret
endfunc
