/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2013 Janne Grunau <janne-libav@jannau.net>
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
#include "neon.S"

function ff_h264_idct_add_neon, export=1
        ld1             {v0.4H, v1.4H, v2.4H, v3.4H},  [x1]
        sxtw            x2,     w2
        movi            v30.8H, #0

        add             v4.4H,  v0.4H,  v2.4H
        sshr            v16.4H, v1.4H,  #1
        st1             {v30.8H},    [x1], #16
        sshr            v17.4H, v3.4H,  #1
        st1             {v30.8H},    [x1], #16
        sub             v5.4H,  v0.4H,  v2.4H
        add             v6.4H,  v1.4H,  v17.4H
        sub             v7.4H,  v16.4H, v3.4H
        add             v0.4H,  v4.4H,  v6.4H
        add             v1.4H,  v5.4H,  v7.4H
        sub             v3.4H,  v4.4H,  v6.4H
        sub             v2.4H,  v5.4H,  v7.4H

        transpose_4x4H  v0, v1, v2, v3, v4, v5, v6, v7

        add             v4.4H,  v0.4H,  v3.4H
        ld1             {v18.S}[0], [x0], x2
        sshr            v16.4H,  v2.4H,  #1
        sshr            v17.4H,  v1.4H,  #1
        ld1             {v19.S}[1], [x0], x2
        sub             v5.4H,  v0.4H,  v3.4H
        ld1             {v18.S}[1], [x0], x2
        add             v6.4H,  v16.4H, v1.4H
        ins             v4.D[1],  v5.D[0]
        sub             v7.4H,  v2.4H,  v17.4H
        ld1             {v19.S}[0], [x0], x2
        ins             v6.D[1],  v7.D[0]
        sub             x0,  x0,  x2, lsl #2
        add             v0.8H,  v4.8H,  v6.8H
        sub             v1.8H,  v4.8H,  v6.8H

        srshr           v0.8H,  v0.8H,  #6
        srshr           v1.8H,  v1.8H,  #6

        uaddw           v0.8H,  v0.8H,  v18.8B
        uaddw           v1.8H,  v1.8H,  v19.8B

        sqxtun          v0.8B, v0.8H
        sqxtun          v1.8B, v1.8H

        st1             {v0.S}[0],  [x0], x2
        st1             {v1.S}[1],  [x0], x2
        st1             {v0.S}[1],  [x0], x2
        st1             {v1.S}[0],  [x0], x2

        sub             x1,  x1,  #32
        ret
endfunc

function ff_h264_idct_dc_add_neon, export=1
        sxtw            x2,  w2
        mov             w3,       #0
        ld1r            {v2.8H},  [x1]
        strh            w3,       [x1]
        srshr           v2.8H,  v2.8H,  #6
        ld1             {v0.S}[0],  [x0], x2
        ld1             {v0.S}[1],  [x0], x2
        uaddw           v3.8H,  v2.8H,  v0.8B
        ld1             {v1.S}[0],  [x0], x2
        ld1             {v1.S}[1],  [x0], x2
        uaddw           v4.8H,  v2.8H,  v1.8B
        sqxtun          v0.8B,  v3.8H
        sqxtun          v1.8B,  v4.8H
        sub             x0,  x0,  x2, lsl #2
        st1             {v0.S}[0],  [x0], x2
        st1             {v0.S}[1],  [x0], x2
        st1             {v1.S}[0],  [x0], x2
        st1             {v1.S}[1],  [x0], x2
        ret
endfunc

function ff_h264_idct_add16_neon, export=1
        mov             x12, x30
        mov             x6,  x0         // dest
        mov             x5,  x1         // block_offset
        mov             x1,  x2         // block
        mov             w9,  w3         // stride
        movrel          x7,  scan8
        mov             x10, #16
        movrel          x13, X(ff_h264_idct_dc_add_neon)
        movrel          x14, X(ff_h264_idct_add_neon)
1:      mov             w2,  w9
        ldrb            w3,  [x7], #1
        ldrsw           x0,  [x5], #4
        ldrb            w3,  [x4,  w3,  uxtw]
        subs            w3,  w3,  #1
        b.lt            2f
        ldrsh           w3,  [x1]
        add             x0,  x0,  x6
        ccmp            w3,  #0,  #4,  eq
        csel            x15, x13, x14, ne
        blr             x15
2:      subs            x10, x10, #1
        add             x1,  x1,  #32
        b.ne            1b
        ret             x12
endfunc

function ff_h264_idct_add16intra_neon, export=1
        mov             x12, x30
        mov             x6,  x0         // dest
        mov             x5,  x1         // block_offset
        mov             x1,  x2         // block
        mov             w9,  w3         // stride
        movrel          x7,  scan8
        mov             x10, #16
        movrel          x13, X(ff_h264_idct_dc_add_neon)
        movrel          x14, X(ff_h264_idct_add_neon)
1:      mov             w2,  w9
        ldrb            w3,  [x7], #1
        ldrsw           x0,  [x5], #4
        ldrb            w3,  [x4,  w3,  uxtw]
        add             x0,  x0,  x6
        cmp             w3,  #0
        ldrsh           w3,  [x1]
        csel            x15, x13, x14, eq
        ccmp            w3,  #0,  #0,  eq
        b.eq            2f
        blr             x15
2:      subs            x10, x10, #1
        add             x1,  x1,  #32
        b.ne            1b
        ret             x12
endfunc

function ff_h264_idct_add8_neon, export=1
        sub             sp,  sp, #0x40
        stp             x19, x20, [sp]
        mov             x12, x30
        ldp             x6,  x15, [x0]          // dest[0], dest[1]
        add             x5,  x1,  #16*4         // block_offset
        add             x9,  x2,  #16*32        // block
        mov             w19, w3                 // stride
        movrel          x13, X(ff_h264_idct_dc_add_neon)
        movrel          x14, X(ff_h264_idct_add_neon)
        movrel          x7,  scan8+16
        mov             x10, #0
        mov             x11, #16
1:      mov             w2,  w19
        ldrb            w3,  [x7, x10]          // scan8[i]
        ldrsw           x0,  [x5, x10, lsl #2]  // block_offset[i]
        ldrb            w3,  [x4, w3,  uxtw]    // nnzc[ scan8[i] ]
        add             x0,  x0,  x6            // block_offset[i] + dst[j-1]
        add             x1,  x9,  x10, lsl #5   // block + i * 16
        cmp             w3,  #0
        ldrsh           w3,  [x1]               // block[i*16]
        csel            x20, x13, x14, eq
        ccmp            w3,  #0,  #0,  eq
        b.eq            2f
        blr             x20
2:      add             x10, x10, #1
        cmp             x10, #4
        csel            x10, x11, x10, eq     // mov x10, #16
        csel            x6,  x15, x6,  eq
        cmp             x10, #20
        b.lt            1b
        ldp             x19, x20, [sp]
        add             sp,  sp,  #0x40
        ret             x12
endfunc

.macro  idct8x8_cols    pass
  .if \pass == 0
        va      .req    v18
        vb      .req    v30
        sshr            v18.8H, v26.8H, #1
        add             v16.8H, v24.8H, v28.8H
        ld1             {v30.8H, v31.8H}, [x1]
        st1             {v19.8H}, [x1],  #16
        st1             {v19.8H}, [x1],  #16
        sub             v17.8H,  v24.8H, v28.8H
        sshr            v19.8H,  v30.8H, #1
        sub             v18.8H,  v18.8H,  v30.8H
        add             v19.8H,  v19.8H,  v26.8H
  .else
        va      .req    v30
        vb      .req    v18
        sshr            v30.8H, v26.8H, #1
        sshr            v19.8H, v18.8H, #1
        add             v16.8H, v24.8H, v28.8H
        sub             v17.8H, v24.8H, v28.8H
        sub             v30.8H, v30.8H, v18.8H
        add             v19.8H, v19.8H, v26.8H
  .endif
        add             v26.8H, v17.8H, va.8H
        sub             v28.8H, v17.8H, va.8H
        add             v24.8H, v16.8H, v19.8H
        sub             vb.8H,  v16.8H, v19.8H
        sub             v16.8H, v29.8H, v27.8H
        add             v17.8H, v31.8H, v25.8H
        sub             va.8H,  v31.8H, v25.8H
        add             v19.8H, v29.8H, v27.8H
        sub             v16.8H, v16.8H, v31.8H
        sub             v17.8H, v17.8H, v27.8H
        add             va.8H,  va.8H,  v29.8H
        add             v19.8H, v19.8H, v25.8H
        sshr            v25.8H, v25.8H, #1
        sshr            v27.8H, v27.8H, #1
        sshr            v29.8H, v29.8H, #1
        sshr            v31.8H, v31.8H, #1
        sub             v16.8H, v16.8H, v31.8H
        sub             v17.8H, v17.8H, v27.8H
        add             va.8H,  va.8H,  v29.8H
        add             v19.8H, v19.8H, v25.8H
        sshr            v25.8H, v16.8H, #2
        sshr            v27.8H, v17.8H, #2
        sshr            v29.8H, va.8H,  #2
        sshr            v31.8H, v19.8H, #2
        sub             v19.8H, v19.8H, v25.8H
        sub             va.8H,  v27.8H, va.8H
        add             v17.8H, v17.8H, v29.8H
        add             v16.8H, v16.8H, v31.8H
  .if \pass == 0
        sub             v31.8H, v24.8H, v19.8H
        add             v24.8H, v24.8H, v19.8H
        add             v25.8H, v26.8H, v18.8H
        sub             v18.8H, v26.8H, v18.8H
        add             v26.8H, v28.8H, v17.8H
        add             v27.8H, v30.8H, v16.8H
        sub             v29.8H, v28.8H, v17.8H
        sub             v28.8H, v30.8H, v16.8H
  .else
        sub             v31.8H, v24.8H, v19.8H
        add             v24.8H, v24.8H, v19.8H
        add             v25.8H, v26.8H, v30.8H
        sub             v30.8H, v26.8H, v30.8H
        add             v26.8H, v28.8H, v17.8H
        sub             v29.8H, v28.8H, v17.8H
        add             v27.8H, v18.8H, v16.8H
        sub             v28.8H, v18.8H, v16.8H
  .endif
        .unreq          va
        .unreq          vb
.endm

function ff_h264_idct8_add_neon, export=1
        movi            v19.8H,   #0
        ld1             {v24.8H, v25.8H}, [x1]
        st1             {v19.8H},  [x1],   #16
        st1             {v19.8H},  [x1],   #16
        ld1             {v26.8H, v27.8H}, [x1]
        st1             {v19.8H},  [x1],   #16
        st1             {v19.8H},  [x1],   #16
        ld1             {v28.8H, v29.8H}, [x1]
        st1             {v19.8H},  [x1],   #16
        st1             {v19.8H},  [x1],   #16

        idct8x8_cols    0
        transpose_8x8H  v24, v25, v26, v27, v28, v29, v18, v31, v6, v7
        idct8x8_cols    1

        mov             x3,  x0
        srshr           v24.8H, v24.8H, #6
        ld1             {v0.8B},     [x0], x2
        srshr           v25.8H, v25.8H, #6
        ld1             {v1.8B},     [x0], x2
        srshr           v26.8H, v26.8H, #6
        ld1             {v2.8B},     [x0], x2
        srshr           v27.8H, v27.8H, #6
        ld1             {v3.8B},     [x0], x2
        srshr           v28.8H, v28.8H, #6
        ld1             {v4.8B},     [x0], x2
        srshr           v29.8H, v29.8H, #6
        ld1             {v5.8B},     [x0], x2
        srshr           v30.8H, v30.8H, #6
        ld1             {v6.8B},     [x0], x2
        srshr           v31.8H, v31.8H, #6
        ld1             {v7.8B},     [x0], x2
        uaddw           v24.8H, v24.8H, v0.8B
        uaddw           v25.8H, v25.8H, v1.8B
        uaddw           v26.8H, v26.8H, v2.8B
        sqxtun          v0.8B,  v24.8H
        uaddw           v27.8H, v27.8H, v3.8B
        sqxtun          v1.8B,  v25.8H
        uaddw           v28.8H, v28.8H, v4.8B
        sqxtun          v2.8B,  v26.8H
        st1             {v0.8B},     [x3], x2
        uaddw           v29.8H, v29.8H, v5.8B
        sqxtun          v3.8B,  v27.8H
        st1             {v1.8B},     [x3], x2
        uaddw           v30.8H, v30.8H, v6.8B
        sqxtun          v4.8B,  v28.8H
        st1             {v2.8B},     [x3], x2
        uaddw           v31.8H, v31.8H, v7.8B
        sqxtun          v5.8B,  v29.8H
        st1             {v3.8B},     [x3], x2
        sqxtun          v6.8B,  v30.8H
        sqxtun          v7.8B,  v31.8H
        st1             {v4.8B},     [x3], x2
        st1             {v5.8B},     [x3], x2
        st1             {v6.8B},     [x3], x2
        st1             {v7.8B},     [x3], x2

        sub             x1,  x1,  #128
        ret
endfunc

function ff_h264_idct8_dc_add_neon, export=1
        mov             w3,       #0
        sxtw            x2,       w2
        ld1r            {v31.8H}, [x1]
        strh            w3,       [x1]
        ld1             {v0.8B},  [x0], x2
        srshr           v31.8H, v31.8H, #6
        ld1             {v1.8B},     [x0], x2
        ld1             {v2.8B},     [x0], x2
        uaddw           v24.8H, v31.8H, v0.8B
        ld1             {v3.8B},     [x0], x2
        uaddw           v25.8H, v31.8H, v1.8B
        ld1             {v4.8B},     [x0], x2
        uaddw           v26.8H, v31.8H, v2.8B
        ld1             {v5.8B},     [x0], x2
        uaddw           v27.8H, v31.8H, v3.8B
        ld1             {v6.8B},     [x0], x2
        uaddw           v28.8H, v31.8H, v4.8B
        ld1             {v7.8B},     [x0], x2
        uaddw           v29.8H, v31.8H, v5.8B
        uaddw           v30.8H, v31.8H, v6.8B
        uaddw           v31.8H, v31.8H, v7.8B
        sqxtun          v0.8B,  v24.8H
        sqxtun          v1.8B,  v25.8H
        sqxtun          v2.8B,  v26.8H
        sqxtun          v3.8B,  v27.8H
        sub             x0,  x0,  x2, lsl #3
        st1             {v0.8B},     [x0], x2
        sqxtun          v4.8B,  v28.8H
        st1             {v1.8B},     [x0], x2
        sqxtun          v5.8B,  v29.8H
        st1             {v2.8B},     [x0], x2
        sqxtun          v6.8B,  v30.8H
        st1             {v3.8B},     [x0], x2
        sqxtun          v7.8B,  v31.8H
        st1             {v4.8B},     [x0], x2
        st1             {v5.8B},     [x0], x2
        st1             {v6.8B},     [x0], x2
        st1             {v7.8B},     [x0], x2
        ret
endfunc

function ff_h264_idct8_add4_neon, export=1
        mov             x12, x30
        mov             x6,  x0
        mov             x5,  x1
        mov             x1,  x2
        mov             w2,  w3
        movrel          x7,  scan8
        mov             w10, #16
        movrel          x13, X(ff_h264_idct8_dc_add_neon)
        movrel          x14, X(ff_h264_idct8_add_neon)
1:      ldrb            w9,  [x7], #4
        ldrsw           x0,  [x5], #16
        ldrb            w9,  [x4, w9, UXTW]
        subs            w9,  w9,  #1
        b.lt            2f
        ldrsh           w11,  [x1]
        add             x0,  x6,  x0
        ccmp            w11, #0,  #4,  eq
        csel            x15, x13, x14, ne
        blr             x15
2:      subs            w10, w10, #4
        add             x1,  x1,  #128
        b.ne            1b
        ret             x12
endfunc

const   scan8
        .byte           4+ 1*8, 5+ 1*8, 4+ 2*8, 5+ 2*8
        .byte           6+ 1*8, 7+ 1*8, 6+ 2*8, 7+ 2*8
        .byte           4+ 3*8, 5+ 3*8, 4+ 4*8, 5+ 4*8
        .byte           6+ 3*8, 7+ 3*8, 6+ 4*8, 7+ 4*8
        .byte           4+ 6*8, 5+ 6*8, 4+ 7*8, 5+ 7*8
        .byte           6+ 6*8, 7+ 6*8, 6+ 7*8, 7+ 7*8
        .byte           4+ 8*8, 5+ 8*8, 4+ 9*8, 5+ 9*8
        .byte           6+ 8*8, 7+ 8*8, 6+ 9*8, 7+ 9*8
        .byte           4+11*8, 5+11*8, 4+12*8, 5+12*8
        .byte           6+11*8, 7+11*8, 6+12*8, 7+12*8
        .byte           4+13*8, 5+13*8, 4+14*8, 5+14*8
        .byte           6+13*8, 7+13*8, 6+14*8, 7+14*8
endconst
