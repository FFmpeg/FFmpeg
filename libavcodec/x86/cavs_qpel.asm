;*****************************************************************************
;* SSE2-optimized CAVS QPEL code
;*****************************************************************************
;* Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
;* based on H.264 optimizations by Michael Niedermayer and Loren Merritt
;* Copyright (c) 2025 Andreas Rheinhardt <andreas.rheinhardt@outlook.com>
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with FFmpeg; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"

cextern pw_4
cextern pw_5

SECTION .text

%macro op_avgh 3
    movh   %3, %2
    pavgb  %1, %3
    movh   %2, %1
%endmacro

%macro op_puth 2-3
    movh   %2, %1
%endmacro

%macro CAVS_QPEL_H 1
; ff_put_cavs_qpel8_mc20(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
cglobal %1_cavs_qpel8_mc20, 3,4,6
    mov         r3d, 8
    jmp         %1_cavs_qpel8_h_after_prologue

; ff_put_cavs_qpel8_h(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h)
cglobal %1_cavs_qpel8_h, 4,4,6
%1_cavs_qpel8_h_after_prologue:
    mova         m3, [pw_4]
    mova         m4, [pw_5]
    pxor         m5, m5
.loop:
    movh         m0, [r1]
    movh         m1, [r1+1]
    punpcklbw    m0, m5
    punpcklbw    m1, m5
    paddw        m0, m1
    movh         m1, [r1-1]
    movh         m2, [r1+2]
    pmullw       m0, m4
    punpcklbw    m1, m5
    punpcklbw    m2, m5
    paddw        m0, m3
    add          r1, r2
    paddw        m1, m2
    psubw        m0, m1
    psraw        m0, 3
    packuswb     m0, m5
    op_%1h       m0, [r0], m1
    add          r0, r2
    dec         r3d
    jne       .loop
    RET
%endmacro

INIT_XMM sse2
CAVS_QPEL_H avg
CAVS_QPEL_H put
