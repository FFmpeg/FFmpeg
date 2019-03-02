/*
 * VP8 NEON optimisations
 *
 * Copyright (c) 2010 Rob Clark <rob@ti.com>
 * Copyright (c) 2011 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2018 Magnus Röös <mla2.roos@gmail.com>
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

function ff_vp8_idct_add_neon, export=1
        ld1             {v0.8b - v3.8b},  [x1]
        mov             w4,  #20091
        movk            w4,  #35468/2, lsl #16
        dup             v4.2s, w4

        smull           v26.4s, v1.4h,  v4.h[0]
        smull           v27.4s, v3.4h,  v4.h[0]
        sqdmulh         v20.4h, v1.4h,  v4.h[1]
        sqdmulh         v23.4h, v3.4h,  v4.h[1]
        sqshrn          v21.4h, v26.4s, #16
        sqshrn          v22.4h, v27.4s, #16
        add             v21.4h, v21.4h, v1.4h
        add             v22.4h, v22.4h, v3.4h

        add             v16.4h,  v0.4h,   v2.4h
        sub             v17.4h,  v0.4h,   v2.4h

        add             v18.4h,  v21.4h,  v23.4h
        sub             v19.4h,  v20.4h,  v22.4h

        add             v0.4h,   v16.4h,  v18.4h
        add             v1.4h,   v17.4h,  v19.4h
        sub             v3.4h,   v16.4h,  v18.4h
        sub             v2.4h,   v17.4h,  v19.4h

        transpose_4x4H  v0, v1, v2, v3, v24, v5, v6, v7

        movi            v29.8h, #0
        smull           v26.4s,     v1.4h,  v4.h[0]
        st1             {v29.8h},   [x1],   #16
        smull           v27.4s,     v3.4h,  v4.h[0]
        st1             {v29.16b},  [x1]
        sqdmulh         v21.4h,     v1.4h,  v4.h[1]
        sqdmulh         v23.4h,     v3.4h,  v4.h[1]
        sqshrn          v20.4h,     v26.4s, #16
        sqshrn          v22.4h,     v27.4s, #16
        add             v20.4h,     v20.4h, v1.4h
        add             v22.4h,     v22.4h, v3.4h
        add             v16.4h,     v0.4h,  v2.4h
        sub             v17.4h,     v0.4h,  v2.4h

        add             v18.4h,     v20.4h, v23.4h
        ld1             {v24.d}[0], [x0],   x2
        zip1            v16.2d,     v16.2d, v17.2d
        sub             v19.4h,     v21.4h, v22.4h
        ld1             {v25.d}[0], [x0],   x2
        zip1            v18.2d,     v18.2d, v19.2d
        add             v0.8h,      v16.8h, v18.8h
        ld1             {v25.d}[1], [x0],   x2
        sub             v1.8h,      v16.8h, v18.8h
        ld1             {v24.d}[1], [x0],   x2
        srshr           v0.8h,      v0.8h,  #3
        trn1            v24.4s,     v24.4s, v25.4s
        srshr           v1.8h,      v1.8h,  #3
        sub             x0,  x0,  x2,  lsl #2

        ext             v1.16b, v1.16b, v1.16b, #8
        trn1            v3.2d,  v0.2d,  v1.2d
        trn2            v0.2d,  v0.2d,  v1.2d
        trn1            v1.8h,  v3.8h,  v0.8h
        trn2            v3.8h,  v3.8h,  v0.8h
        uzp1            v0.4s,  v1.4s,  v3.4s
        uzp2            v1.4s,  v3.4s,  v1.4s

        uaddw           v0.8h,  v0.8h, v24.8b
        uaddw2          v1.8h,  v1.8h, v24.16b
        sqxtun          v0.8b,  v0.8h
        sqxtun2         v0.16b, v1.8h
        st1             {v0.s}[0],  [x0], x2
        st1             {v0.s}[1],  [x0], x2
        st1             {v0.s}[3],  [x0], x2
        st1             {v0.s}[2],  [x0], x2

        ret
endfunc

function ff_vp8_idct_dc_add4y_neon, export=1
        movi            v0.16b,  #0
        mov             x3,  #32
        ld1r            {v16.4h},    [x1]
        st1             {v0.h}[0],   [x1], x3
        ld1r            {v17.4h},    [x1]
        st1             {v0.h}[0],   [x1], x3
        zip1            v16.2d,      v16.2d, v17.2d
        ld1r            {v18.4h},    [x1]
        st1             {v0.h}[0],   [x1], x3
        ld1r            {v19.4h},    [x1]
        st1             {v0.h}[0],   [x1], x3
        zip1            v18.2d,      v18.2d, v19.2d
        srshr           v16.8h,      v16.8h,  #3            // dc >>= 3
        ld1             {v0.16b},     [x0], x2
        srshr           v18.8h,       v18.8h,  #3
        ld1             {v1.16b},     [x0], x2
        uaddw           v20.8h,       v16.8h,  v0.8b
        ld1             {v2.16b},     [x0], x2
        uaddw2          v0.8h,        v18.8h,   v0.16b
        ld1             {v3.16b},     [x0], x2
        uaddw           v21.8h, v16.8h,  v1.8b
        uaddw2          v1.8h,  v18.8h,  v1.16b
        uaddw           v22.8h, v16.8h,  v2.8b
        uaddw2          v2.8h,  v18.8h,  v2.16b
        uaddw           v23.8h, v16.8h,  v3.8b
        uaddw2          v3.8h,  v18.8h,  v3.16b
        sub             x0,  x0,  x2,  lsl #2
        sqxtun          v20.8b,  v20.8h
        sqxtun2         v20.16b, v0.8h
        sqxtun          v21.8b,  v21.8h
        sqxtun2         v21.16b, v1.8h
        sqxtun          v22.8b,  v22.8h
        st1             {v20.16b},    [x0], x2
        sqxtun2         v22.16b, v2.8h
        st1             {v21.16b},    [x0], x2
        sqxtun          v23.8b,  v23.8h
        st1             {v22.16b},    [x0], x2
        sqxtun2         v23.16b, v3.8h
        st1             {v23.16b},    [x0], x2

        ret
endfunc

function ff_vp8_idct_dc_add_neon, export=1
        mov             w3,       #0
        ld1r            {v2.8h},  [x1]
        strh            w3,       [x1]
        srshr           v2.8h,  v2.8h,  #3
        ld1             {v0.s}[0],  [x0], x2
        ld1             {v0.s}[1],  [x0], x2
        uaddw           v3.8h,  v2.8h,  v0.8b
        ld1             {v1.s}[0],  [x0], x2
        ld1             {v1.s}[1],  [x0], x2
        uaddw           v4.8h,  v2.8h,  v1.8b
        sqxtun          v0.8b,  v3.8h
        sqxtun          v1.8b,  v4.8h
        sub             x0,  x0,  x2, lsl #2
        st1             {v0.s}[0],  [x0], x2
        st1             {v0.s}[1],  [x0], x2
        st1             {v1.s}[0],  [x0], x2
        st1             {v1.s}[1],  [x0], x2
        ret
endfunc

// Register layout:
//   P3..Q3 -> v0..v7
//   flim_E -> v22
//   flim_I -> v23
//   hev_thresh -> x5
//
.macro  vp8_loop_filter, inner=0, simple=0, hev_thresh
    .if \simple
        uabd            v17.16b, v3.16b,  v4.16b      // abs(P0-Q0)
        uabd            v23.16b, v2.16b,  v5.16b      // abs(P1-Q1)
        uqadd           v17.16b, v17.16b, v17.16b     // abs(P0-Q0) * 2
        ushr            v18.16b, v23.16b, #1          // abs(P1-Q1) / 2
        uqadd           v19.16b, v17.16b,  v18.16b    // (abs(P0-Q0)*2) + (abs(P1-Q1)/2)
        movi            v21.16b, #0x80
        cmhs            v16.16b, v22.16b, v19.16b    // (abs(P0-Q0)*2) + (abs(P1-Q1)/2) <= flim
    .else
        // calculate hev and normal_limit:
        uabd            v20.16b, v2.16b,  v3.16b      // abs(P1-P0)
        uabd            v21.16b, v5.16b,  v4.16b      // abs(Q1-Q0)
        uabd            v18.16b, v0.16b,  v1.16b      // abs(P3-P2)
        uabd            v19.16b, v1.16b,  v2.16b      // abs(P2-P1)
        cmhs            v16.16b, v23.16b, v20.16b     // abs(P1-P0) <= flim_I
        cmhs            v17.16b, v23.16b, v21.16b     // abs(Q1-Q0) <= flim_I
        cmhs            v18.16b, v23.16b, v18.16b     // abs(P3-P2) <= flim_I
        cmhs            v19.16b, v23.16b, v19.16b     // abs(P2-P1) <= flim_I
        and             v16.16b, v17.16b, v16.16b
        uabd            v17.16b, v7.16b,  v6.16b      // abs(Q3-Q2)
        and             v16.16b, v16.16b, v19.16b
        uabd            v19.16b, v6.16b,  v5.16b      // abs(Q2-Q1)
        and             v16.16b, v16.16b, v18.16b
        cmhs            v18.16b, v23.16b, v17.16b     // abs(Q3-Q2) <= flim_I
        cmhs            v19.16b, v23.16b, v19.16b     // abs(Q2-Q1) <= flim_I
        uabd            v17.16b, v3.16b,  v4.16b      // abs(P0-Q0)
        uabd            v23.16b, v2.16b,  v5.16b      // abs(P1-Q1)
        and             v16.16b, v16.16b, v18.16b
        uqadd           v17.16b, v17.16b, v17.16b     // abs(P0-Q0) * 2
        and             v16.16b, v16.16b, v19.16b
        ushr            v18.16b, v23.16b, #1          // abs(P1-Q1) / 2
        dup             v23.16b, \hev_thresh          // hev_thresh
        uqadd           v19.16b, v17.16b, v18.16b     // (abs(P0-Q0)*2) + (abs(P1-Q1)/2)
        cmhi            v20.16b, v20.16b, v23.16b     // abs(P1-P0) > hev_thresh
        cmhs            v19.16b, v22.16b, v19.16b     // (abs(P0-Q0)*2) + (abs(P1-Q1)/2) <= flim_E
        cmhi            v22.16b, v21.16b, v23.16b     // abs(Q1-Q0) > hev_thresh
        and             v16.16b, v16.16b, v19.16b
        movi            v21.16b, #0x80
        orr             v17.16b, v20.16b, v22.16b
    .endif

        // at this point:
        //   v16: normal_limit
        //   v17: hev

        // convert to signed value:
        eor            v3.16b, v3.16b, v21.16b           // PS0 = P0 ^ 0x80
        eor            v4.16b, v4.16b, v21.16b           // QS0 = Q0 ^ 0x80

        movi           v20.8h, #3
        ssubl          v18.8h, v4.8b,  v3.8b             // QS0 - PS0
        ssubl2         v19.8h, v4.16b, v3.16b            //   (widened to 16bit)
        eor            v2.16b, v2.16b, v21.16b           // PS1 = P1 ^ 0x80
        eor            v5.16b, v5.16b, v21.16b           // QS1 = Q1 ^ 0x80
        mul            v18.8h, v18.8h, v20.8h            // w = 3 * (QS0 - PS0)
        mul            v19.8h, v19.8h, v20.8h

        sqsub          v20.16b, v2.16b, v5.16b           // clamp(PS1-QS1)
        movi           v22.16b, #4
        movi           v23.16b, #3
    .if \inner
        and            v20.16b, v20.16b, v17.16b         // if(hev) w += clamp(PS1-QS1)
    .endif
        saddw          v18.8h,  v18.8h, v20.8b           // w += clamp(PS1-QS1)
        saddw2         v19.8h,  v19.8h, v20.16b
        sqxtn          v18.8b,  v18.8h                   // narrow result back into v18
        sqxtn2         v18.16b, v19.8h
    .if !\inner && !\simple
        eor            v1.16b,  v1.16b,  v21.16b         // PS2 = P2 ^ 0x80
        eor            v6.16b,  v6.16b,  v21.16b         // QS2 = Q2 ^ 0x80
    .endif
        and            v18.16b, v18.16b, v16.16b         // w &= normal_limit

        // registers used at this point..
        //   v0 -> P3  (don't corrupt)
        //   v1-v6 -> PS2-QS2
        //   v7 -> Q3  (don't corrupt)
        //   v17 -> hev
        //   v18 -> w
        //   v21 -> #0x80
        //   v22 -> #4
        //   v23 -> #3
        //   v16, v19, v29 -> unused
        //
        // filter_common:   is4tap==1
        //   c1 = clamp(w + 4) >> 3;
        //   c2 = clamp(w + 3) >> 3;
        //   Q0 = s2u(QS0 - c1);
        //   P0 = s2u(PS0 + c2);

    .if \simple
        sqadd          v19.16b, v18.16b, v22.16b           // c1 = clamp((w&hev)+4)
        sqadd          v20.16b, v18.16b, v23.16b           // c2 = clamp((w&hev)+3)
        sshr           v19.16b, v19.16b, #3                // c1 >>= 3
        sshr           v20.16b, v20.16b, #3                // c2 >>= 3
        sqsub          v4.16b,  v4.16b,  v19.16b           // QS0 = clamp(QS0-c1)
        sqadd          v3.16b,  v3.16b,  v20.16b           // PS0 = clamp(PS0+c2)
        eor            v4.16b,  v4.16b,  v21.16b           // Q0 = QS0 ^ 0x80
        eor            v3.16b,  v3.16b,  v21.16b           // P0 = PS0 ^ 0x80
        eor            v5.16b,  v5.16b,  v21.16b           // Q1 = QS1 ^ 0x80
        eor            v2.16b,  v2.16b,  v21.16b           // P1 = PS1 ^ 0x80
    .elseif \inner
        // the !is4tap case of filter_common, only used for inner blocks
        //   c3 = ((c1&~hev) + 1) >> 1;
        //   Q1 = s2u(QS1 - c3);
        //   P1 = s2u(PS1 + c3);
        sqadd          v19.16b, v18.16b, v22.16b           // c1 = clamp((w&hev)+4)
        sqadd          v20.16b, v18.16b, v23.16b           // c2 = clamp((w&hev)+3)
        sshr           v19.16b, v19.16b, #3                // c1 >>= 3
        sshr           v20.16b, v20.16b, #3                // c2 >>= 3
        sqsub          v4.16b,  v4.16b,  v19.16b           // QS0 = clamp(QS0-c1)
        sqadd          v3.16b,  v3.16b,  v20.16b           // PS0 = clamp(PS0+c2)
        bic            v19.16b, v19.16b, v17.16b           // c1 & ~hev
        eor            v4.16b,  v4.16b,  v21.16b           // Q0 = QS0 ^ 0x80
        srshr          v19.16b, v19.16b, #1                // c3 >>= 1
        eor            v3.16b,  v3.16b,  v21.16b           // P0 = PS0 ^ 0x80
        sqsub          v5.16b,  v5.16b,  v19.16b           // QS1 = clamp(QS1-c3)
        sqadd          v2.16b,  v2.16b,  v19.16b           // PS1 = clamp(PS1+c3)
        eor            v5.16b,  v5.16b,  v21.16b           // Q1 = QS1 ^ 0x80
        eor            v2.16b,  v2.16b,  v21.16b           // P1 = PS1 ^ 0x80
    .else
        and            v20.16b, v18.16b, v17.16b           // w & hev
        sqadd          v19.16b, v20.16b, v22.16b           // c1 = clamp((w&hev)+4)
        sqadd          v20.16b, v20.16b, v23.16b           // c2 = clamp((w&hev)+3)
        sshr           v19.16b, v19.16b, #3                // c1 >>= 3
        sshr           v20.16b, v20.16b, #3                // c2 >>= 3
        bic            v18.16b, v18.16b, v17.16b           // w &= ~hev
        sqsub          v4.16b,  v4.16b,  v19.16b           // QS0 = clamp(QS0-c1)
        sqadd          v3.16b,  v3.16b,  v20.16b           // PS0 = clamp(PS0+c2)

        // filter_mbedge:
        //   a = clamp((27*w + 63) >> 7);
        //   Q0 = s2u(QS0 - a);
        //   P0 = s2u(PS0 + a);
        //   a = clamp((18*w + 63) >> 7);
        //   Q1 = s2u(QS1 - a);
        //   P1 = s2u(PS1 + a);
        //   a = clamp((9*w + 63) >> 7);
        //   Q2 = s2u(QS2 - a);
        //   P2 = s2u(PS2 + a);
        movi           v17.8h,  #63
        sshll          v22.8h,  v18.8b, #3
        sshll2         v23.8h,  v18.16b, #3
        saddw          v22.8h,  v22.8h, v18.8b
        saddw2         v23.8h,  v23.8h, v18.16b
        add            v16.8h,  v17.8h, v22.8h
        add            v17.8h,  v17.8h, v23.8h           //  9*w + 63
        add            v19.8h,  v16.8h, v22.8h
        add            v20.8h,  v17.8h, v23.8h           // 18*w + 63
        add            v22.8h,  v19.8h, v22.8h
        add            v23.8h,  v20.8h, v23.8h           // 27*w + 63
        sqshrn         v16.8b,  v16.8h,  #7
        sqshrn2        v16.16b, v17.8h, #7              // clamp(( 9*w + 63)>>7)
        sqshrn         v19.8b,  v19.8h, #7
        sqshrn2        v19.16b, v20.8h, #7              // clamp((18*w + 63)>>7)
        sqshrn         v22.8b,  v22.8h, #7
        sqshrn2        v22.16b, v23.8h, #7              // clamp((27*w + 63)>>7)
        sqadd          v1.16b,  v1.16b,  v16.16b        // PS2 = clamp(PS2+a)
        sqsub          v6.16b,  v6.16b,  v16.16b        // QS2 = clamp(QS2-a)
        sqadd          v2.16b,  v2.16b,  v19.16b        // PS1 = clamp(PS1+a)
        sqsub          v5.16b,  v5.16b,  v19.16b        // QS1 = clamp(QS1-a)
        sqadd          v3.16b,  v3.16b,  v22.16b        // PS0 = clamp(PS0+a)
        sqsub          v4.16b,  v4.16b,  v22.16b        // QS0 = clamp(QS0-a)
        eor            v3.16b,  v3.16b,  v21.16b        // P0 = PS0 ^ 0x80
        eor            v4.16b,  v4.16b,  v21.16b        // Q0 = QS0 ^ 0x80
        eor            v2.16b,  v2.16b,  v21.16b        // P1 = PS1 ^ 0x80
        eor            v5.16b,  v5.16b,  v21.16b        // Q1 = QS1 ^ 0x80
        eor            v1.16b,  v1.16b,  v21.16b        // P2 = PS2 ^ 0x80
        eor            v6.16b,  v6.16b,  v21.16b        // Q2 = QS2 ^ 0x80
    .endif
.endm

.macro  vp8_v_loop_filter16 name, inner=0, simple=0
function ff_vp8_v_loop_filter16\name\()_neon, export=1
        sub             x0,  x0,  x1,  lsl #1+!\simple

        // Load pixels:
    .if !\simple
        ld1             {v0.16b},     [x0], x1 // P3
        ld1             {v1.16b},     [x0], x1 // P2
    .endif
        ld1             {v2.16b},     [x0], x1 // P1
        ld1             {v3.16b},     [x0], x1 // P0
        ld1             {v4.16b},     [x0], x1 // Q0
        ld1             {v5.16b},     [x0], x1 // Q1
    .if !\simple
        ld1             {v6.16b},     [x0], x1 // Q2
        ld1             {v7.16b},     [x0]     // Q3
        dup             v23.16b, w3                 // flim_I
    .endif
        dup             v22.16b, w2                 // flim_E

        vp8_loop_filter inner=\inner, simple=\simple, hev_thresh=w4

        // back up to P2:  dst -= stride * 6
        sub             x0,  x0,  x1,  lsl #2
    .if !\simple
        sub             x0,  x0,  x1,  lsl #1

        // Store pixels:
        st1             {v1.16b},     [x0], x1 // P2
    .endif
        st1             {v2.16b},     [x0], x1 // P1
        st1             {v3.16b},     [x0], x1 // P0
        st1             {v4.16b},     [x0], x1 // Q0
        st1             {v5.16b},     [x0], x1 // Q1
    .if !\simple
        st1             {v6.16b},     [x0]     // Q2
    .endif

        ret
endfunc
.endm

vp8_v_loop_filter16
vp8_v_loop_filter16 _inner,  inner=1
vp8_v_loop_filter16 _simple, simple=1

.macro  vp8_v_loop_filter8uv name, inner=0
function ff_vp8_v_loop_filter8uv\name\()_neon, export=1
        sub             x0,  x0,  x2,  lsl #2
        sub             x1,  x1,  x2,  lsl #2
        // Load pixels:
        ld1          {v0.d}[0],     [x0], x2  // P3
        ld1          {v0.d}[1],     [x1], x2  // P3
        ld1          {v1.d}[0],     [x0], x2  // P2
        ld1          {v1.d}[1],     [x1], x2  // P2
        ld1          {v2.d}[0],     [x0], x2  // P1
        ld1          {v2.d}[1],     [x1], x2  // P1
        ld1          {v3.d}[0],     [x0], x2  // P0
        ld1          {v3.d}[1],     [x1], x2  // P0
        ld1          {v4.d}[0],     [x0], x2  // Q0
        ld1          {v4.d}[1],     [x1], x2  // Q0
        ld1          {v5.d}[0],     [x0], x2  // Q1
        ld1          {v5.d}[1],     [x1], x2  // Q1
        ld1          {v6.d}[0],     [x0], x2  // Q2
        ld1          {v6.d}[1],     [x1], x2  // Q2
        ld1          {v7.d}[0],     [x0]      // Q3
        ld1          {v7.d}[1],     [x1]      // Q3

        dup          v22.16b, w3                 // flim_E
        dup          v23.16b, w4                 // flim_I

        vp8_loop_filter inner=\inner, hev_thresh=w5

        // back up to P2:  u,v -= stride * 6
        sub          x0,  x0,  x2,  lsl #2
        sub          x1,  x1,  x2,  lsl #2
        sub          x0,  x0,  x2,  lsl #1
        sub          x1,  x1,  x2,  lsl #1

        // Store pixels:

        st1          {v1.d}[0],     [x0], x2  // P2
        st1          {v1.d}[1],     [x1], x2  // P2
        st1          {v2.d}[0],     [x0], x2  // P1
        st1          {v2.d}[1],     [x1], x2  // P1
        st1          {v3.d}[0],     [x0], x2  // P0
        st1          {v3.d}[1],     [x1], x2  // P0
        st1          {v4.d}[0],     [x0], x2  // Q0
        st1          {v4.d}[1],     [x1], x2  // Q0
        st1          {v5.d}[0],     [x0], x2  // Q1
        st1          {v5.d}[1],     [x1], x2  // Q1
        st1          {v6.d}[0],     [x0]      // Q2
        st1          {v6.d}[1],     [x1]      // Q2

        ret
endfunc
.endm

vp8_v_loop_filter8uv
vp8_v_loop_filter8uv _inner, inner=1

.macro  vp8_h_loop_filter16 name, inner=0, simple=0
function ff_vp8_h_loop_filter16\name\()_neon, export=1

        sub             x0,  x0,  #4
        // Load pixels:
        ld1             {v0.d}[0], [x0], x1
        ld1             {v1.d}[0], [x0], x1
        ld1             {v2.d}[0], [x0], x1
        ld1             {v3.d}[0], [x0], x1
        ld1             {v4.d}[0], [x0], x1
        ld1             {v5.d}[0], [x0], x1
        ld1             {v6.d}[0], [x0], x1
        ld1             {v7.d}[0], [x0], x1
        ld1             {v0.d}[1], [x0], x1
        ld1             {v1.d}[1], [x0], x1
        ld1             {v2.d}[1], [x0], x1
        ld1             {v3.d}[1], [x0], x1
        ld1             {v4.d}[1], [x0], x1
        ld1             {v5.d}[1], [x0], x1
        ld1             {v6.d}[1], [x0], x1
        ld1             {v7.d}[1], [x0], x1

        transpose_8x16B   v0,  v1,  v2,  v3,  v4,  v5,  v6,  v7, v30, v31

        dup             v22.16b, w2                 // flim_E
    .if !\simple
        dup             v23.16b, w3                 // flim_I
    .endif

        vp8_loop_filter inner=\inner, simple=\simple, hev_thresh=w4

        sub             x0,  x0,  x1, lsl #4    // backup 16 rows

        transpose_8x16B   v0,  v1,  v2,  v3,  v4,  v5,  v6,  v7, v30, v31

        // Store pixels:
        st1             {v0.d}[0], [x0], x1
        st1             {v1.d}[0], [x0], x1
        st1             {v2.d}[0], [x0], x1
        st1             {v3.d}[0], [x0], x1
        st1             {v4.d}[0], [x0], x1
        st1             {v5.d}[0], [x0], x1
        st1             {v6.d}[0], [x0], x1
        st1             {v7.d}[0], [x0], x1
        st1             {v0.d}[1], [x0], x1
        st1             {v1.d}[1], [x0], x1
        st1             {v2.d}[1], [x0], x1
        st1             {v3.d}[1], [x0], x1
        st1             {v4.d}[1], [x0], x1
        st1             {v5.d}[1], [x0], x1
        st1             {v6.d}[1], [x0], x1
        st1             {v7.d}[1], [x0]

        ret
endfunc
.endm

vp8_h_loop_filter16
vp8_h_loop_filter16 _inner,  inner=1
vp8_h_loop_filter16 _simple, simple=1

.macro  vp8_h_loop_filter8uv name, inner=0
function ff_vp8_h_loop_filter8uv\name\()_neon, export=1
        sub             x0,  x0,  #4
        sub             x1,  x1,  #4

        // Load pixels:
        ld1          {v0.d}[0],     [x0], x2 // load u
        ld1          {v0.d}[1],     [x1], x2 // load v
        ld1          {v1.d}[0],     [x0], x2
        ld1          {v1.d}[1],     [x1], x2
        ld1          {v2.d}[0],     [x0], x2
        ld1          {v2.d}[1],     [x1], x2
        ld1          {v3.d}[0],     [x0], x2
        ld1          {v3.d}[1],     [x1], x2
        ld1          {v4.d}[0],     [x0], x2
        ld1          {v4.d}[1],     [x1], x2
        ld1          {v5.d}[0],     [x0], x2
        ld1          {v5.d}[1],     [x1], x2
        ld1          {v6.d}[0],     [x0], x2
        ld1          {v6.d}[1],     [x1], x2
        ld1          {v7.d}[0],     [x0], x2
        ld1          {v7.d}[1],     [x1], x2

        transpose_8x16B   v0,  v1,  v2,  v3,  v4,  v5,  v6,  v7, v30, v31

        dup             v22.16b, w3                 // flim_E
        dup             v23.16b, w4                 // flim_I

        vp8_loop_filter inner=\inner, hev_thresh=w5

        sub             x0,  x0,  x2, lsl #3    // backup u 8 rows
        sub             x1,  x1,  x2, lsl #3    // backup v 8 rows

        transpose_8x16B   v0,  v1,  v2,  v3,  v4,  v5,  v6,  v7, v30, v31

        // Store pixels:
        st1          {v0.d}[0],     [x0], x2 // load u
        st1          {v0.d}[1],     [x1], x2 // load v
        st1          {v1.d}[0],     [x0], x2
        st1          {v1.d}[1],     [x1], x2
        st1          {v2.d}[0],     [x0], x2
        st1          {v2.d}[1],     [x1], x2
        st1          {v3.d}[0],     [x0], x2
        st1          {v3.d}[1],     [x1], x2
        st1          {v4.d}[0],     [x0], x2
        st1          {v4.d}[1],     [x1], x2
        st1          {v5.d}[0],     [x0], x2
        st1          {v5.d}[1],     [x1], x2
        st1          {v6.d}[0],     [x0], x2
        st1          {v6.d}[1],     [x1], x2
        st1          {v7.d}[0],     [x0]
        st1          {v7.d}[1],     [x1]

        ret

endfunc
.endm

vp8_h_loop_filter8uv
vp8_h_loop_filter8uv _inner, inner=1


function ff_put_vp8_pixels16_neon, export=1
1:
        subs            w4, w4, #4
        ld1             {v0.16b},     [x2], x3
        ld1             {v1.16b},     [x2], x3
        ld1             {v2.16b},     [x2], x3
        ld1             {v3.16b},     [x2], x3
        st1             {v0.16b},     [x0], x1
        st1             {v1.16b},     [x0], x1
        st1             {v2.16b},     [x0], x1
        st1             {v3.16b},     [x0], x1
        bgt             1b
        ret
endfunc

function ff_put_vp8_pixels8_neon, export=1
1:
        subs            w4, w4, #4
        ld1             {v0.8b},   [x2], x3
        ld1             {v0.d}[1], [x2], x3
        ld1             {v1.8b},   [x2], x3
        ld1             {v1.d}[1], [x2], x3
        st1             {v0.8b},   [x0], x1
        st1             {v0.d}[1], [x0], x1
        st1             {v1.8b},   [x0], x1
        st1             {v1.d}[1], [x0], x1
        bgt             1b
        ret
endfunc

/* 4/6-tap 8th-pel MC */

.macro  vp8_epel8_h6    d,   s0,   s1
        ext             v22.8b, \s0\().8b,  \s1\().8b,  #1
        uxtl            v18.8h, \s0\().8b
        ext             v23.8b, \s0\().8b,  \s1\().8b,  #2
        uxtl            v19.8h, v22.8b
        ext             v24.8b, \s0\().8b,  \s1\().8b,  #3
        uxtl            v21.8h, v23.8b
        ext             v25.8b, \s0\().8b,  \s1\().8b,  #4
        uxtl            v22.8h, v24.8b
        ext             v26.8b, \s0\().8b,  \s1\().8b,  #5
        uxtl            v25.8h, v25.8b
        mul             v21.8h, v21.8h, v0.h[2]
        uxtl            v26.8h, v26.8b
        mul             v22.8h, v22.8h, v0.h[3]
        mls             v21.8h, v19.8h, v0.h[1]
        mls             v22.8h, v25.8h, v0.h[4]
        mla             v21.8h, v18.8h, v0.h[0]
        mla             v22.8h, v26.8h, v0.h[5]
        sqadd           v22.8h, v21.8h, v22.8h
        sqrshrun        \d\().8b, v22.8h, #7
.endm

.macro  vp8_epel16_h6   d0,  v0,  v1
        ext             v22.16b, \v0\().16b, \v1\().16b, #3
        ext             v23.16b, \v0\().16b, \v1\().16b, #4
        uxtl            v19.8h,  v22.8b
        uxtl2           v22.8h,  v22.16b
        ext             v3.16b,  \v0\().16b, \v1\().16b, #2
        uxtl            v20.8h,  v23.8b
        uxtl2           v23.8h,  v23.16b
        ext             v16.16b, \v0\().16b, \v1\().16b, #1
        uxtl            v18.8h,  v3.8b
        uxtl2           v3.8h,   v3.16b
        ext             v2.16b,  \v0\().16b, \v1\().16b, #5
        uxtl            v21.8h,  v2.8b
        uxtl2           v2.8h,   v2.16b
        uxtl            v17.8h,  v16.8b
        uxtl2           v16.8h,  v16.16b
        mul             v19.8h,  v19.8h, v0.h[3]
        mul             v18.8h,  v18.8h, v0.h[2]
        mul             v3.8h,   v3.8h,  v0.h[2]
        mul             v22.8h,  v22.8h, v0.h[3]
        mls             v19.8h,  v20.8h, v0.h[4]
        uxtl            v20.8h,  \v0\().8b
        uxtl2           v1.8h,   \v0\().16b
        mls             v18.8h,  v17.8h, v0.h[1]
        mls             v3.8h,   v16.8h, v0.h[1]
        mls             v22.8h,  v23.8h, v0.h[4]
        mla             v18.8h,  v20.8h, v0.h[0]
        mla             v19.8h,  v21.8h, v0.h[5]
        mla             v3.8h,   v1.8h,  v0.h[0]
        mla             v22.8h,  v2.8h,  v0.h[5]
        sqadd           v19.8h,  v18.8h, v19.8h
        sqadd           v22.8h,  v3.8h,  v22.8h
        sqrshrun        \d0\().8b,  v19.8h, #7
        sqrshrun2       \d0\().16b, v22.8h, #7
.endm

.macro  vp8_epel8_v6    d0,  s0,  s1,  s2, s3, s4, s5
        uxtl            \s2\().8h, \s2\().8b
        uxtl            \s3\().8h, \s3\().8b
        uxtl            \s1\().8h, \s1\().8b
        uxtl            \s4\().8h, \s4\().8b
        uxtl            \s0\().8h, \s0\().8b
        uxtl            \s5\().8h, \s5\().8b
        mul             \s2\().8h, \s2\().8h, v0.h[2]
        mul             \s3\().8h, \s3\().8h, v0.h[3]
        mls             \s2\().8h, \s1\().8h, v0.h[1]
        mls             \s3\().8h, \s4\().8h, v0.h[4]
        mla             \s2\().8h, \s0\().8h, v0.h[0]
        mla             \s3\().8h, \s5\().8h, v0.h[5]
        sqadd           \s3\().8h, \s2\().8h, \s3\().8h
        sqrshrun        \d0\().8b, \s3\().8h, #7
.endm

.macro  vp8_epel8_v6_y2 d0, d1, s0, s1, s2, s3, s4, s5, s6
        uxtl            \s0\().8h, \s0\().8b
        uxtl            \s3\().8h, \s3\().8b
        uxtl            \s6\().8h, \s6\().8b
        uxtl            \s1\().8h, \s1\().8b
        uxtl            \s4\().8h, \s4\().8b
        uxtl            \s2\().8h, \s2\().8b
        uxtl            \s5\().8h, \s5\().8b
        mul             \s0\().8h, \s0\().8h, v0.h[0]
        mul             v31.8h   , \s3\().8h, v0.h[3]
        mul             \s3\().8h, \s3\().8h, v0.h[2]
        mul             \s6\().8h, \s6\().8h, v0.h[5]

        mls             \s0\().8h, \s1\().8h, v0.h[1]
        mls             v31.8h   , \s4\().8h, v0.h[4]
        mls             \s3\().8h, \s2\().8h, v0.h[1]
        mls             \s6\().8h, \s5\().8h, v0.h[4]

        mla             \s0\().8h, \s2\().8h, v0.h[2]
        mla             v31.8h   , \s5\().8h, v0.h[5]
        mla             \s3\().8h, \s1\().8h, v0.h[0]
        mla             \s6\().8h, \s4\().8h, v0.h[3]
        sqadd           v31.8h   , \s0\().8h, v31.8h
        sqadd           \s6\().8h, \s3\().8h, \s6\().8h
        sqrshrun        \d0\().8b, v31.8h,    #7
        sqrshrun        \d1\().8b, \s6\().8h, #7
.endm

.macro  vp8_epel8_h4    d,   v0,   v1
        ext             v22.8b, \v0\().8b,  \v1\().8b,  #1
        uxtl            v19.8h, \v0\().8b
        ext             v23.8b, \v0\().8b,  \v1\().8b,  #2
        uxtl            v20.8h, v22.8b
        ext             v25.8b, \v0\().8b,  \v1\().8b,  #3
        uxtl            v22.8h, v23.8b
        uxtl            v25.8h, v25.8b
        mul             v20.8h, v20.8h, v0.h[2]
        mul             v22.8h, v22.8h, v0.h[3]
        mls             v20.8h, v19.8h, v0.h[1]
        mls             v22.8h, v25.8h, v0.h[4]
        sqadd           v22.8h, v20.8h, v22.8h
        sqrshrun        \d\().8b, v22.8h, #7
.endm

.macro  vp8_epel8_v4_y2 d0, s0, s1, s2, s3, s4
        uxtl            \s0\().8h,  \s0\().8b
        uxtl            \s1\().8h,  \s1\().8b
        uxtl            \s2\().8h,  \s2\().8b
        uxtl            \s3\().8h,  \s3\().8b
        uxtl            \s4\().8h,  \s4\().8b
        mul             v21.8h,     \s1\().8h, v0.h[2]
        mul             v23.8h,     \s2\().8h, v0.h[3]
        mul             \s2\().8h,  \s2\().8h, v0.h[2]
        mul             v22.8h,     \s3\().8h, v0.h[3]
        mls             v21.8h,     \s0\().8h, v0.h[1]
        mls             v23.8h,     \s3\().8h, v0.h[4]
        mls             \s2\().8h,  \s1\().8h, v0.h[1]
        mls             v22.8h,     \s4\().8h, v0.h[4]
        sqadd           v21.8h,     v21.8h,    v23.8h
        sqadd           \s2\().8h,  \s2\().8h, v22.8h
        sqrshrun        \d0\().8b,  v21.8h,    #7
        sqrshrun2       \d0\().16b, \s2\().8h, #7
.endm


// note: worst case sum of all 6-tap filter values * 255 is 0x7f80 so 16 bit
// arithmatic can be used to apply filters
const   subpel_filters, align=4
        .short     0,   6, 123,  12,   1,   0,   0,   0
        .short     2,  11, 108,  36,   8,   1,   0,   0
        .short     0,   9,  93,  50,   6,   0,   0,   0
        .short     3,  16,  77,  77,  16,   3,   0,   0
        .short     0,   6,  50,  93,   9,   0,   0,   0
        .short     1,   8,  36, 108,  11,   2,   0,   0
        .short     0,   1,  12, 123,   6,   0,   0,   0
endconst

function ff_put_vp8_epel16_v6_neon, export=1
        sub             x2,  x2,  x3,  lsl #1

        sxtw            x4,  w4
        sxtw            x6,  w6
        movrel          x17,  subpel_filters, -16
        add             x6,  x17,  x6, lsl #4  // y
        ld1             {v0.8h},     [x6]
1:
        ld1             {v1.1d - v2.1d},    [x2], x3
        ld1             {v3.1d - v4.1d},    [x2], x3
        ld1             {v16.1d - v17.1d},  [x2], x3
        ld1             {v18.1d - v19.1d},  [x2], x3
        ld1             {v20.1d - v21.1d},  [x2], x3
        ld1             {v22.1d - v23.1d},  [x2], x3
        ld1             {v24.1d - v25.1d},  [x2]
        sub             x2,  x2,  x3, lsl #2

        vp8_epel8_v6_y2 v1, v3, v1, v3, v16, v18, v20, v22, v24
        vp8_epel8_v6_y2 v2, v4, v2, v4, v17, v19, v21, v23, v25

        st1             {v1.1d - v2.1d}, [x0], x1
        st1             {v3.1d - v4.1d}, [x0], x1
        subs            x4, x4, #2
        bne             1b

        ret
endfunc

function ff_put_vp8_epel16_h6_neon, export=1
        sub             x2,  x2,  #2
        sxtw            x5,  w5 // x

        // first pass (horizontal):
        movrel          x17,  subpel_filters, -16
        add             x5,  x17,  x5, lsl #4 // x
        ld1             {v0.8h},  [x5]
1:
        ld1             {v1.16b, v2.16b}, [x2], x3
        vp8_epel16_h6   v1, v1, v2
        st1             {v1.16b}, [x0], x1

        subs            w4, w4, #1
        bne             1b
        ret
endfunc


function ff_put_vp8_epel16_h6v6_neon, export=1
        sub             x2,  x2,  x3,  lsl #1
        sub             x2,  x2,  #2

        // first pass (horizontal):
        movrel          x17,  subpel_filters, -16
        sxtw            x5,  w5 // x
        add             x16,  x17,  x5, lsl #4 // x
        sub             sp,  sp,  #336+16
        ld1             {v0.8h},  [x16]
        add             x7,  sp,  #15
        sxtw            x4,  w4
        add             x16, x4, #5   // h
        bic             x7,  x7,  #15
1:
        ld1             {v1.16b, v2.16b}, [x2], x3
        vp8_epel16_h6   v1, v1, v2
        st1             {v1.16b}, [x7], #16
        subs            x16, x16, #1
        bne             1b


        // second pass (vertical):
        sxtw            x6,  w6
        add             x6,  x17,  x6, lsl #4  // y
        add             x7,  sp,  #15
        ld1             {v0.8h},     [x6]
        bic             x7,  x7,  #15
2:
        ld1             {v1.8b - v4.8b},    [x7], #32
        ld1             {v16.8b - v19.8b},  [x7], #32
        ld1             {v20.8b - v23.8b},  [x7]
        sub             x7,  x7,  #48

        vp8_epel8_v6    v5, v1, v3, v16, v18, v20, v22
        vp8_epel8_v6    v2, v2, v4, v17, v19, v21, v23
        trn1            v2.2d, v5.2d, v2.2d

        st1             {v2.16b}, [x0], x1
        subs            x4, x4, #1
        bne             2b

        add             sp,  sp,  #336+16
        ret
endfunc

function ff_put_vp8_epel8_h6v6_neon, export=1
        sub             x2,  x2,  x3,  lsl #1
        sub             x2,  x2,  #2
        sxtw            x4,  w4

        // first pass (horizontal):
        movrel          x17,  subpel_filters, -16
        sxtw            x5,  w5
        add             x5,  x17,  x5, lsl #4 // x
        sub             sp,  sp,  #168+16
        ld1             {v0.8h},  [x5]
        add             x7,  sp,  #15
        add             x16, x4,  #5   // h
        bic             x7,  x7,  #15
1:
        ld1             {v1.8b, v2.8b}, [x2], x3

        vp8_epel8_h6    v1, v1, v2

        st1             {v1.8b}, [x7], #8
        subs            x16, x16, #1
        bne             1b

        // second pass (vertical):
        sxtw            x6,  w6
        add             x6,  x17,  x6, lsl #4  // y
        add             x7,  sp,   #15
        ld1             {v0.8h},   [x6]
        bic             x7,  x7,   #15
2:
        ld1             {v1.8b - v4.8b}, [x7], #32
        ld1             {v5.8b - v7.8b}, [x7]

        sub             x7,  x7,  #16

        vp8_epel8_v6_y2 v1, v2, v1, v2, v3, v4, v5, v6, v7

        st1             {v1.8b}, [x0], x1
        st1             {v2.8b}, [x0], x1
        subs            x4, x4, #2
        bne             2b

        add             sp,  sp,  #168+16
        ret
endfunc

function ff_put_vp8_epel8_h4v6_neon, export=1
        sub             x2,  x2,  x3,  lsl #1
        sub             x2,  x2,  #1
        sxtw            x4,  w4

        // first pass (horizontal):
        movrel          x17,  subpel_filters, -16
        sxtw            x5,  w5
        add             x5,  x17,  x5, lsl #4 // x
        sub             sp,  sp,  #168+16
        ld1             {v0.8h},  [x5]
        add             x7,  sp,  #15
        add             x16, x4, #5   // h
        bic             x7,  x7,  #15
1:
        ld1             {v1.8b, v2.8b}, [x2], x3

        vp8_epel8_h4    v1, v1, v2

        st1             {v1.8b}, [x7], #8
        subs            x16, x16, #1
        bne             1b

        // second pass (vertical):
        sxtw            x6,  w6
        add             x6,  x17,  x6, lsl #4  // y
        add             x7,  sp,   #15
        ld1             {v0.8h},   [x6]
        bic             x7,  x7,   #15
2:
        ld1             {v1.8b - v4.8b}, [x7], #32
        ld1             {v5.8b - v7.8b}, [x7]

        sub             x7,  x7,  #16

        vp8_epel8_v6_y2 v1, v2, v1, v2, v3, v4, v5, v6, v7

        st1             {v1.8b}, [x0], x1
        st1             {v2.8b}, [x0], x1
        subs            x4, x4, #2
        bne             2b

        add             sp,  sp,  #168+16
        ret
endfunc

function ff_put_vp8_epel8_h4v4_neon, export=1
        sub             x2,  x2,  x3
        sub             x2,  x2,  #1
        sxtw            x4,  w4


        // first pass (horizontal):
        movrel          x17,  subpel_filters, -16
        sxtw            x5,  w5
        add             x5,  x17,  x5, lsl #4 // x
        sub             sp,  sp,  #168+16
        ld1             {v0.8h},  [x5]
        add             x7,  sp,  #15
        add             x16, x4, #3   // h
        bic             x7,  x7,  #15
1:
        ld1             {v1.8b, v2.8b}, [x2], x3

        vp8_epel8_h4    v1, v1, v2

        st1             {v1.8b}, [x7], #8
        subs            x16, x16, #1
        bne             1b

        // second pass (vertical):
        sxtw            x6,  w6
        add             x6,  x17,  x6, lsl #4  // y
        add             x7,  sp,   #15
        ld1             {v0.8h},   [x6]
        bic             x7,  x7,   #15
2:
        ld1             {v1.8b - v2.8b}, [x7], #16
        ld1             {v3.8b - v5.8b}, [x7]

        vp8_epel8_v4_y2 v1, v1, v2, v3, v4, v5

        st1             {v1.d}[0], [x0], x1
        st1             {v1.d}[1], [x0], x1
        subs            x4, x4, #2
        bne             2b

        add             sp,  sp,  #168+16
        ret
endfunc

function ff_put_vp8_epel8_h6v4_neon, export=1
        sub             x2,  x2,  x3
        sub             x2,  x2,  #2
        sxtw            x4,  w4


        // first pass (horizontal):
        movrel          x17,  subpel_filters, -16
        sxtw            x5,  w5
        add             x5,  x17,  x5, lsl #4 // x
        sub             sp,  sp,  #168+16
        ld1             {v0.8h},  [x5]
        add             x7,  sp,  #15
        add             x16, x4, #3   // h
        bic             x7,  x7,  #15
1:
        ld1             {v1.8b, v2.8b}, [x2], x3

        vp8_epel8_h6    v1, v1, v2

        st1             {v1.8b}, [x7], #8
        subs            x16, x16, #1
        bne             1b

        // second pass (vertical):
        sxtw            x6,  w6
        add             x6,  x17,  x6, lsl #4  // y
        add             x7,  sp,   #15
        ld1             {v0.8h},   [x6]
        bic             x7,  x7,   #15
2:
        ld1             {v1.8b - v2.8b}, [x7], #16
        ld1             {v3.8b - v5.8b}, [x7]

        vp8_epel8_v4_y2 v1, v1, v2, v3, v4, v5

        st1             {v1.d}[0], [x0], x1
        st1             {v1.d}[1], [x0], x1
        subs            x4, x4, #2
        bne             2b

        add             sp,  sp,  #168+16
        ret
endfunc
