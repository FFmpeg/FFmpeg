/*
 * Copyright (c) 2026 DROOdotFOO <drew@axol.io>
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

// Apple packs ints to 4 bytes on the stack; AAPCS-64 pads to 8.
#ifdef __APPLE__
.set    SP_INT, 4
#else
.set    SP_INT, 8
#endif

.macro THRESHOLD bits, vecsz, shape, ldop, stop, step
function ff_threshold\bits\()_neon, export=1
        stp             x19, x20, [sp, #-16]!
        ldp             x8,  x9,  [sp, #16]         // slinesize, olinesize
        ldr             w10, [sp, #32]              // w
        ldr             w11, [sp, #32+SP_INT]       // h
        cmp             w11, #0
        b.le            9f
        cmp             w10, #0
        b.le            9f
1:
        mov             x12, x0
        mov             x13, x1
        mov             x14, x2
        mov             x15, x3
        mov             x16, x4
        mov             w17, w10
2:
        cmp             w17, #(2*\vecsz)
        b.lt            3f
        ld1             {v0.16b, v1.16b}, [x12], #32
        ld1             {v2.16b, v3.16b}, [x13], #32
        ld1             {v4.16b, v5.16b}, [x14], #32
        ld1             {v6.16b, v7.16b}, [x15], #32
        cmhs            v16.\shape, v2.\shape, v0.\shape  // thr >= in
        cmhs            v17.\shape, v3.\shape, v1.\shape
        bsl             v16.16b, v4.16b, v6.16b           // mask ? min : max
        bsl             v17.16b, v5.16b, v7.16b
        st1             {v16.16b, v17.16b}, [x16], #32
        sub             w17, w17, #(2*\vecsz)
        b               2b
3:
        cmp             w17, #\vecsz
        b.lt            4f
        ld1             {v0.16b}, [x12], #16
        ld1             {v1.16b}, [x13], #16
        ld1             {v2.16b}, [x14], #16
        ld1             {v3.16b}, [x15], #16
        cmhs            v4.\shape, v1.\shape, v0.\shape   // thr >= in
        bsl             v4.16b, v2.16b, v3.16b            // mask ? min : max
        st1             {v4.16b}, [x16], #16
        sub             w17, w17, #\vecsz
4:
        cbz             w17, 6f
5:
        \ldop           w19, [x12], #\step
        \ldop           w20, [x13], #\step
        cmp             w19, w20
        \ldop           w19, [x14], #\step
        \ldop           w20, [x15], #\step
        csel            w19, w19, w20, ls
        \stop           w19, [x16], #\step
        subs            w17, w17, #1
        b.gt            5b
6:
        add             x0, x0, x5
        add             x1, x1, x6
        add             x2, x2, x7
        add             x3, x3, x8
        add             x4, x4, x9
        subs            w11, w11, #1
        b.gt            1b
9:
        ldp             x19, x20, [sp], #16
        ret
endfunc
.endm

THRESHOLD 8,  16, 16b, ldrb, strb, 1
THRESHOLD 16, 8,  8h,  ldrh, strh, 2
