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

#if HAVE_ARM_AES
ENABLE_ARM_AES

.macro aesenc x, y
        aese            \x, \y
        aesmc           \x, \x
.endm

.macro aesenclast x, y
        aese            \x, \y
.endm

.macro aesdec x, y
        aesd            \x, \y
        aesimc          \x, \x
.endm

.macro aesdeclast x, y
        aesd            \x, \y
.endm

.macro AES_CRYPT op, rounds
//-----------------------------------------------------------------------------
// void ff_aes_[enc|dec]rypt_N_neon(AVAES *a, uint8_t *dst, const uint8_t *src,
//                                  int count, uint8_t *iv, int rounds)
//-----------------------------------------------------------------------------
function ff_aes_\op\()rypt_\rounds\()_neon, export=1
        cbz             w3, 3f
        ubfiz           x3, x3, #4, #32 // x3 = w3 << 4
        add             x2, x2, x3
        add             x1, x1, x3
        neg             x3, x3
        movi            v1.2d, #0
  .if \rounds == 14
        ldr             q31, [x0, #(8*2*\rounds)]
        ldp             q29, q30, [x0, #0xc0]
        ldp             q27, q28, [x0, #0xa0]
  .elseif \rounds == 12
        ldr             q29, [x0, #(8*2*\rounds)]
        ldp             q27, q28, [x0, #0xa0]
  .else
        ldr             q27, [x0, #(8*2*\rounds)]
  .endif
        ldp             q25, q26, [x0, #0x80]
        ldp             q23, q24, [x0, #0x60]
        ldp             q21, q22, [x0, #0x40]
        ldp             q19, q20, [x0, #0x20]
        ldp             q17, q18, [x0]
        cbz             x4, 1f
        ldr             q1, [x4]
1:
        ldr             q0, [x2, x3]
  .ifc \op, enc
        eor             v0.16b, v0.16b, v1.16b
  .endif
  .if \rounds > 10
    .if \rounds > 12
        aes\op          v0.16b, v31.16b
        aes\op          v0.16b, v30.16b
    .endif
        aes\op          v0.16b, v29.16b
        aes\op          v0.16b, v28.16b
  .endif
        aes\op          v0.16b, v27.16b
        aes\op          v0.16b, v26.16b
        aes\op          v0.16b, v25.16b
        aes\op          v0.16b, v24.16b
        aes\op          v0.16b, v23.16b
        aes\op          v0.16b, v22.16b
        aes\op          v0.16b, v21.16b
        aes\op          v0.16b, v20.16b
        aes\op          v0.16b, v19.16b
        aes\op\()last   v0.16b, v18.16b
        eor             v0.16b, v0.16b, v17.16b
        cbz             x4, 2f
  .ifc \op, enc
        mov             v1.16b, v0.16b
  .else
        eor             v0.16b, v0.16b, v1.16b
        ldr             q1, [x2, x3]
  .endif
2:
        str             q0, [x1, x3]
        adds            x3, x3, #16
        b.lt            1b
        cbz             x4, 3f
  .ifc \op, dec
        str             q1, [x4]
  .else
        str             q0, [x4]
  .endif
3:
        ret
endfunc
.endm

AES_CRYPT enc, 10
AES_CRYPT enc, 12
AES_CRYPT enc, 14
AES_CRYPT dec, 10
AES_CRYPT dec, 12
AES_CRYPT dec, 14

DISABLE_ARM_AES
#endif
