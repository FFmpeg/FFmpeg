/*
 * Bitpacked 10-bit YUV 4:2:2 unpacking
 * Copyright (c) 2026 Devin Heitmueller <dheitmueller@ltnglobal.com>
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

const   bitpacked_tbl, align=4
        // Byte-pair indices for Y samples 0-7 and 8-15.
        .byte           2, 1, 4, 3, 7, 6, 9, 8, 12, 11, 14, 13, 17, 16, 19, 18
        .byte           22, 21, 24, 23, 27, 26, 29, 28
        .byte           32, 31, 34, 33, 37, 36, 39, 38
        // Byte-pair indices for the U and V samples.
        .byte           1, 0, 6, 5, 11, 10, 16, 15, 21, 20, 26, 25, 31, 30, 36, 35
        .byte           3, 2, 8, 7, 13, 12, 18, 17, 23, 22, 28, 27, 33, 32, 38, 37
        // Per-lane multipliers align the alternating Y fields.
        .hword          4, 64, 4, 64, 4, 64, 4, 64
endconst

// Consume 40 bytes and produce 16 Y, 8 U and 8 V samples. TBL reverses
// each big-endian byte pair; 16-bit multiply/shift overflow discards unwanted
// high bits before the common right shift. The main loop unrolls to 80 bytes.
.macro  unpack_16
        ld1             {v0.16b, v1.16b}, [x0]
        ldr             d2, [x0, #32]
        add             x0, x0, #40

        tbl             v3.16b, {v0.16b-v2.16b}, v16.16b
        tbl             v4.16b, {v0.16b-v2.16b}, v17.16b
        tbl             v5.16b, {v0.16b-v2.16b}, v18.16b
        tbl             v6.16b, {v0.16b-v2.16b}, v19.16b

        mul             v3.8h, v3.8h, v20.8h
        mul             v4.8h, v4.8h, v20.8h
        shl             v6.8h, v6.8h, #4
        ushr            v3.8h, v3.8h, #6
        ushr            v4.8h, v4.8h, #6
        ushr            v5.8h, v5.8h, #6
        ushr            v6.8h, v6.8h, #6

        st1             {v3.8h, v4.8h}, [x1], #32
        str             q5, [x2], #16
        str             q6, [x3], #16
.endm

// ff_bitpacked_unpack_yuv422p10_neon(const uint8_t *src,
//                                    uint16_t *y, uint16_t *u, uint16_t *v,
//                                    int width)
function ff_bitpacked_unpack_yuv422p10_neon, export=1
        movrel          x5, bitpacked_tbl
        ld1             {v16.16b-v19.16b}, [x5], #64
        ldr             q20, [x5]

        subs            w4, w4, #32
        b.lt            2f

1:      unpack_16
        unpack_16
        subs            w4, w4, #32
        b.ge            1b

2:      add             w4, w4, #32
        tbz             w4, #4, 3f
        unpack_16
        sub             w4, w4, #16

3:      cbz             w4, 4f
        ldr             q0, [x0], #16
        ldr             s1, [x0]

        tbl             v3.16b, {v0.16b-v1.16b}, v16.16b
        tbl             v5.16b, {v0.16b-v1.16b}, v18.16b
        tbl             v6.16b, {v0.16b-v1.16b}, v19.16b

        mul             v3.8h, v3.8h, v20.8h
        shl             v6.8h, v6.8h, #4
        ushr            v3.8h, v3.8h, #6
        ushr            v5.8h, v5.8h, #6
        ushr            v6.8h, v6.8h, #6

        str             q3, [x1]
        str             d5, [x2]
        str             d6, [x3]

4:      ret
endfunc
