/*
 * Copyright (c) 2025 Zhao Zhili <quinkblack@foxmail.com>
 * Copyright (c) 2026 Shreesh Adiga <16567adigashreesh@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "asm.S"

#if HAVE_ARM_CRC
ENABLE_ARM_CRC

.macro crc32_byte
        ldrb            w5, [x2], #1
        subs            x4, x4, #1
        crc32b          w0, w0, w5
.endm

function ff_crc32_aarch64, export=1
        and             x4, x2, #7          // non-aligned buffer addr
        mov             w0, w1
        cbz             x3, 8f

        cbz             x4, 2f

        mov             x5, #8
        sub             x4, x5, x4
        cmp             x4, x3
        csel            x4, x4, x3, lo
        sub             x3, x3, x4
1:
        // loop on non-aligned buffer
        crc32_byte
        b.ne            1b
2:
        // loop on aligned buffer
        bic             x5, x3, #15
        and             x4, x3, #15
        cbz             x5, 4f
3:
        ldp             x6, x7, [x2], #16
        subs            x5, x5, #16
        crc32x          w0, w0, x6
        crc32x          w0, w0, x7
        b.ne            3b
4:
        cbz             x4, 8f
5:
        crc32_byte
        b.ne            5b
8:
        ret
endfunc

DISABLE_ARM_CRC
#endif

#if HAVE_PMULL && HAVE_EOR3
ENABLE_PMULL
ENABLE_EOR3

const reverse_shuffle, align=4
        .byte  15, 14, 13, 12
        .byte  11, 10,  9,  8
        .byte   7,  6,  5,  4
        .byte   3,  2,  1,  0
endconst

const partial_bytes_shuf_tab, align=4
        .byte  255, 254, 253, 252
        .byte  251, 250, 249, 248
        .byte  247, 246, 245, 244
        .byte  243, 242, 241, 240
        .byte   0,  1,  2,  3
        .byte   4,  5,  6,  7
        .byte   8,  9, 10, 11
        .byte  12, 13, 14, 15
endconst

// performs Vfold = pmull(Vfold, Vconst) xor pmull2(Vfold, Vconst) xor Vdata
.macro FOLD_SINGLE Vfold, Vconst, Vdata, Vtemp
        pmull           \Vtemp\().1q, \Vconst\().1d, \Vfold\().1d
        pmull2          \Vfold\().1q, \Vconst\().2d, \Vfold\().2d
        eor3            \Vfold\().16b, \Vfold\().16b, \Vdata\().16b, \Vtemp\().16b
.endm

// assume Vfold is v16 and v0 is filled with 0
// uses v17 as temp
.macro FOLD_128_TO_64 le, Vconst
.if ! \le
        fmov            d17, d16
        pmull2          v16.1q, v16.2d, \Vconst\().2d
        ext             v17.16b, v0.16b, v17.16b, #12
        eor             v16.16b, v17.16b, v16.16b
        ext             \Vconst\().16b, \Vconst\().16b, \Vconst\().16b, #8
        pmull2          v17.1q, \Vconst\().2d, v16.2d
        eor             v16.16b, v16.16b, v17.16b
.else
        ext             v17.16b, v16.16b, v0.16b, #8
        pmull           v16.1q, v16.1d, \Vconst\().1d
        eor             v16.16b, v16.16b, v17.16b
        ext             v17.16b, v0.16b, v16.16b, #12
        ext             \Vconst\().16b, \Vconst\().16b, \Vconst\().16b, #8
        pmull           v17.1q, \Vconst\().1d, v17.1d
        eor             v16.16b, v16.16b, v17.16b
.endif
.endm

// assume Vfold is v16 and v0 is filled with 0
// uses v17 as temp
.macro FOLD_64_TO_32 le, Vconst
.if ! \le
        pmull           v17.1q, v16.1d, \Vconst\().1d
        pmull2          v17.1q, v17.2d, \Vconst\().2d
        eor             v16.16b, v16.16b, v17.16b
        fmov            w0, s16
        rev             w0, w0
.else
        mov             v16.s[0], wzr
        pmull           v17.1q, v16.1d, \Vconst\().1d
        eor             v17.16b, v17.16b, v16.16b
        ext             \Vconst\().16b, \Vconst\().16b, \Vconst\().16b, #8
        pmull           v17.1q, v17.1d, \Vconst\().1d
        eor             v16.16b, v16.16b, v17.16b
        mov             w0, v16.s[2]
.endif
.endm

#define CTX_OFFSET 4

// ff_crc[_le]_neon_pmull(const AVCRC *ctx, uint32_t crc, const uint8_t *buffer, size_t length)
// x0 - ctx; pre-computed fold constants, see crc_init_aarch64 for details on the structure.
// x1 - crc initial value
// x2 - buffer pointer
// x3 - length of buffer
//
// Additional registers used:
// x10 - buffer + length; points to end of buffer
// x11-x15 used as temp registers as needed
//
// v0 - zero register to perform vector shift during reduction using EXT
// v1 - crc input from x1
// v2 - holds the reverse shuffle values 15, 14 ... 0 to reverse vector using TBL
// v3 - holds the constant values used in PMULL/PMULL2 loaded via x0 ctx
// v4-v7 - used as temp for PMULL/PMULL2 output
// v16 - holds the primary fold register later reduced to 32 bits
// Registers v16-v19 are used in parallel 4x fold loop
// Registers v20-23 are used to load the 4 next data chunks
// Registers v17-v21 are used as temp elsewhere
.macro crc_fn_template le
.if \le
function ff_crc_le_neon_pmull, export=1
.else
function ff_crc_neon_pmull, export=1
        movrel          x9, reverse_shuffle
        ldr             q2, [x9]
.endif
        add             x10, x2, x3
        movi            v0.2d, #0
        mov             v1.16b, v0.16b
        mov             v1.s[0], w1
        cmp             x3, #64
        b.lo            8f // less than 64 bytes
        ld1             {v16.16b-v19.16b}, [x2], #64
        eor             v16.16b, v16.16b, v1.16b
.if ! \le
        tbl             v16.16b, { v16.16b }, v2.16b
        tbl             v17.16b, { v17.16b }, v2.16b
        tbl             v18.16b, { v18.16b }, v2.16b
        tbl             v19.16b, { v19.16b }, v2.16b
.endif
        sub             x11, x10, x2
        cmp             x11, #64
        b.lo            2f // reduce 4x to 1x
        ldr             q3, [x0, #(CTX_OFFSET + 0)]

1: // fold 4x loop
        ld1             {v20.16b-v23.16b}, [x2], #64
.if ! \le
        tbl             v20.16b, { v20.16b }, v2.16b
        tbl             v21.16b, { v21.16b }, v2.16b
        tbl             v22.16b, { v22.16b }, v2.16b
        tbl             v23.16b, { v23.16b }, v2.16b
.endif
        pmull           v4.1q, v16.1d, v3.1d
        pmull           v5.1q, v17.1d, v3.1d
        pmull           v6.1q, v18.1d, v3.1d
        pmull           v7.1q, v19.1d, v3.1d
        pmull2          v16.1q, v16.2d, v3.2d
        pmull2          v17.1q, v17.2d, v3.2d
        pmull2          v18.1q, v18.2d, v3.2d
        pmull2          v19.1q, v19.2d, v3.2d
        eor3            v16.16b, v16.16b, v4.16b, v20.16b
        eor3            v17.16b, v17.16b, v5.16b, v21.16b
        eor3            v18.16b, v18.16b, v6.16b, v22.16b
        eor3            v19.16b, v19.16b, v7.16b, v23.16b
        sub             x11, x10, x2
        cmp             x11, #64
        b.hs            1b // fold 4x loop

2: // reduce 4x to 1x
        ldr             q3, [x0, #(CTX_OFFSET + 16)]
        FOLD_SINGLE     v16, v3, v17, v4
        FOLD_SINGLE     v16, v3, v18, v4
        FOLD_SINGLE     v16, v3, v19, v4

3: // fold 1x pre
        sub             x11, x10, x2
        cmp             x11, #16
        b.lo            5f // partial_block

4: // fold 1x loop
        ldr             q17, [x2], #16
.if ! \le
        tbl             v17.16b, { v17.16b }, v2.16b
.endif
        FOLD_SINGLE     v16, v3, v17, v4
        sub             x11, x10, x2
        cmp             x11, #16
        b.hs            4b // fold 1x loop

5: // partial_block
        cmp             x10, x2
        b.eq            6f // reduce 128 to 64
        ldr             q17, [x10, #-16]
        movrel          x1, partial_bytes_shuf_tab
        ldr             q18, [x1, x11]
.if ! \le
        tbl             v16.16b, { v16.16b }, v2.16b
.endif
        mvn             v19.16b, v18.16b
        tbl             v20.16b, { v16.16b }, v19.16b
        cmge            v21.16b, v18.16b, #0
        bsl             v21.16b, v17.16b, v20.16b
        tbl             v16.16b, { v16.16b }, v18.16b
.if ! \le
        tbl             v16.16b, { v16.16b }, v2.16b
        tbl             v21.16b, { v21.16b }, v2.16b
.endif
        FOLD_SINGLE     v16, v3, v21, v4

6: // reduce 128 to 64
        ldr             q3, [x0, #(CTX_OFFSET + 32)]
        FOLD_128_TO_64  \le, v3

7: // reduce 64 to 32
        ldr             q3, [x0, #(CTX_OFFSET + 48)]
        FOLD_64_TO_32   \le, v3
        ret

8: // less than 64 bytes
        cmp             x3, #16
        b.lo            9f // less than 16 bytes
        ldr             q16, [x2], #16
        eor             v16.16b, v16.16b, v1.16b
.if ! \le
        tbl             v16.16b, { v16.16b }, v2.16b
.endif
        ldr             q3, [x0, #(CTX_OFFSET + 16)]
        b               3b // fold 1x pre

9: // less than 16 bytes
        movrel          x15, partial_bytes_shuf_tab
        add             x15, x15, x3
        str             q0, [sp, #-16]!
        add             x9, sp, x3
        // memcpy 0 to 15 bytes from src to stack and load it to v16 with zero extension
        tbz             x3, #3, 10f
        ldr             x11, [x2]
        ldr             x12, [x10, #-8]
        str             x11, [sp]
        str             x12, [x9, #-8]
        b               12f
10:
        tbz             x3, #2, 11f
        ldr             w11, [x2]
        ldr             w12, [x10, #-4]
        str             w11, [sp]
        str             w12, [x9, #-4]
        b               12f
11:
        cbz             x3, 12f
        lsr             x11, x3, #1
        ldrb            w12, [x2]
        ldrb            w14, [x10, #-1]
        ldrb            w13, [x2, x11]
        strb            w12, [sp]
        strb            w13, [sp, x11]
        strb            w14, [x9, #-1]
12:
        ldr             q16, [sp], #16
        eor             v16.16b, v16.16b, v1.16b
        cmp             x3, #5
        b.lo            13f // less than 5 bytes
        ldr             q17, [x15]
        tbl             v16.16b, { v16.16b }, v17.16b
.if ! \le
        tbl             v16.16b, { v16.16b }, v2.16b
.endif
        b               6b // reduce 128 to 64

13: // less than 5 bytes
.if ! \le
        ldr             q17, [x15, #12]
        tbl             v16.16b, { v16.16b }, v17.16b
        rev64           v16.16b, v16.16b
.else
        ldr             q17, [x15, #8]
        tbl             v16.16b, { v16.16b }, v17.16b
.endif
        b               7b // reduce 64 to 32
endfunc
.endm

crc_fn_template 0
crc_fn_template 1

DISABLE_PMULL
DISABLE_EOR3
#endif
