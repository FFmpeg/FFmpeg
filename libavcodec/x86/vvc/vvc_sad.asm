; /*
; * Provide SIMD DMVR SAD functions for VVC decoding
; *
; * Copyright (c) 2024 Stone Chen
; *
; * This file is part of FFmpeg.
; *
; * FFmpeg is free software; you can redistribute it and/or
; * modify it under the terms of the GNU Lesser General Public
; * License as published by the Free Software Foundation; either
; * version 2.1 of the License, or (at your option) any later version.
; *
; * FFmpeg is distributed in the hope that it will be useful,
; * but WITHOUT ANY WARRANTY; without even the implied warranty of
; * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; * Lesser General Public License for more details.
; *
; * You should have received a copy of the GNU Lesser General Public
; * License along with FFmpeg; if not, write to the Free Software
; * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
; */

%include "libavutil/x86/x86util.asm"
%define MAX_PB_SIZE 128
%define ROWS 2

SECTION_RODATA

pw_1: times 2 dw 1

; DMVR SAD is only calculated on even rows to reduce complexity
; Additionally the only valid sizes are 8x16, 16x8, and 16x16
SECTION .text

%macro MIN_MAX_SAD 3
    pminuw           %3, %2, %1
    pmaxuw           %1, %2, %1
    psubusw          %1, %1, %3
%endmacro

%macro HORIZ_ADD 3  ; xm0, xm1, m1
    vextracti128     %1, %3, q0001  ;        3        2      1          0
    paddd            %1, %2         ; xm0 (7 + 3) (6 + 2) (5 + 1)   (4 + 0)
    pshufd           %2, %1, q0032  ; xm1    -      -     (7 + 3)   (6 + 2)
    paddd            %1, %1, %2     ; xm0    _      _     (5 1 7 3) (4 0 6 2)
    pshufd           %2, %1, q0001  ; xm1    _      _     (5 1 7 3) (5 1 7 3)
    paddd            %1, %1, %2     ;                               (01234567)
%endmacro

%if ARCH_X86_64
%if HAVE_AVX2_EXTERNAL

INIT_YMM avx2

cglobal vvc_sad, 6, 9, 5, src1, src2, dx, dy, block_w, block_h, off1, off2, row_idx
    movsxdifnidn    dxq, dxd
    movsxdifnidn    dyq, dyd

    sub             dxq, 2
    sub             dyq, 2

    mov             off1q, 2
    mov             off2q, 2

    add             off1q, dyq
    sub             off2q, dyq

    shl             off1q, 7
    shl             off2q, 7

    add             off1q, dxq
    sub             off2q, dxq

    lea             src1q, [src1q + off1q * 2 + 2 * 2]
    lea             src2q, [src2q + off2q * 2 + 2 * 2]

    pxor               m3, m3
    vpbroadcastd       m4, [pw_1]

    cmp          block_wd, 16
    je         vvc_sad_16

    vvc_sad_8:
        .loop_height:
        movu              xm0, [src1q]
        vinserti128        m0, m0, [src1q + MAX_PB_SIZE * ROWS * 2], 1
        movu              xm1, [src2q]
        vinserti128        m1, m1, [src2q + MAX_PB_SIZE * ROWS * 2], 1

        MIN_MAX_SAD        m1, m0, m2
        pmaddwd            m1, m4
        paddd              m3, m1

        add         src1q, 2 * MAX_PB_SIZE * ROWS * 2
        add         src2q, 2 * MAX_PB_SIZE * ROWS * 2

        sub      block_hd, 4
        jg   .loop_height

        HORIZ_ADD     xm0, xm3, m3
        movd          eax, xm0
    RET

    vvc_sad_16:
        sar      block_wd, 4
        .loop_height:
        mov         off1q, src1q
        mov         off2q, src2q
        mov      row_idxd, block_wd

        .loop_width:
            movu               m0, [src1q]
            movu               m1, [src2q]
            MIN_MAX_SAD        m1, m0, m2
            pmaddwd            m1, m4
            paddd              m3, m1

            add             src1q, 32
            add             src2q, 32
            dec          row_idxd
            jg        .loop_width

        lea         src1q, [off1q + ROWS * MAX_PB_SIZE * 2]
        lea         src2q, [off2q + ROWS * MAX_PB_SIZE * 2]

        sub      block_hd, 2
        jg   .loop_height

        HORIZ_ADD     xm0, xm3, m3
        movd          eax, xm0
    RET

%endif
%endif
