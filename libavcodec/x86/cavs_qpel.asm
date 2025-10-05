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

SECTION_RODATA

cextern pw_4
cextern pw_5
cextern pw_7
cextern pw_64
pw_42: times 8 dw 42
pw_96: times 8 dw 96

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

%macro FILT_V 1
    movh        m3, [r1]
    punpcklbw   m3, m7
    mova        m4, m1
    paddw       m4, m2
    paddw       m0, m3
    add         r1, r2
    pmullw      m4, m5
    psubw       m4, m0
    paddw       m4, m6
    psraw       m4, 3
    packuswb    m4, m7
    op_%1h      m4, [r0], m0
    add         r0, r2
    SWAP         0, 1, 2, 3
%endmacro

%macro CAVS_QPEL_MC02 1
; ff_put_cavs_qpel8_mc02(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
cglobal %1_cavs_qpel8_mc02, 3,4,8
    mov         r3d, 8
    jmp         %1_cavs_qpel8_v2_after_prologue

; ff_put_cavs_qpel8_v2(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h)
cglobal %1_cavs_qpel8_v2, 4,4,8
%1_cavs_qpel8_v2_after_prologue:
    movh         m1, [r1]
    sub          r1, r2
    movh         m0, [r1]
    lea          r1, [r1+2*r2]
    pxor         m7, m7
    movh         m2, [r1]
    add          r1, r2
    punpcklbw    m1, m7
    punpcklbw    m0, m7
    punpcklbw    m2, m7
    mova         m5, [pw_5]
    mova         m6, [pw_4]
.loop:
    FILT_V       %1
    FILT_V       %1
    FILT_V       %1
    FILT_V       %1
    sub         r3d, 4
    jne       .loop
    RET
%endmacro

INIT_XMM sse2
CAVS_QPEL_MC02 avg
CAVS_QPEL_MC02 put

%macro FILT_V3 1
    pmullw      m0, PW_7
    movh        m4, [r1]
    mova        m5, m1
    mova        m6, m2
    pmullw      m5, PW_42
    punpcklbw   m4, m7
    pmullw      m6, PW_96
    paddw       m0, m3
    add         r1, r2
    paddw       m0, m3
    paddw       m5, m6
    paddw       m0, m4
    ; m5-m0 can be in the -10*255..(42 + 96)*255 range and
    ; therefore is not guaranteed to fit into either a signed or
    ; an unsigned word. Because we need to clamp the result to 0..255
    ; anyway, we use saturated subtraction and a logical right shift
    ; for rescaling.
    psubusw     m5, m0
    paddw       m5, PW_64
    psrlw       m5, 7
    packuswb    m5, m7
    op_%1h      m5, [r0], m0
    add         r0, r2
    SWAP         0, 1, 2, 3, 4
%endmacro

%macro CAVS_QPEL_MC03 1
; ff_put_cavs_qpel8_mc03(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
cglobal %1_cavs_qpel8_mc03, 3,4,8+4*ARCH_X86_64
    mov         r3d, 8
    jmp         %1_cavs_qpel8_v3_after_prologue

; ff_put_cavs_qpel8_v3(uint8_t *dst, const uint8_t *src, ptrdiff_t stride, int h)
cglobal %1_cavs_qpel8_v3, 4,4,8+4*ARCH_X86_64
%1_cavs_qpel8_v3_after_prologue:
    movh         m1, [r1]
    movh         m2, [r1+r2]
    movh         m3, [r1+2*r2]
    sub          r1, r2
    pxor         m7, m7
    movh         m0, [r1]
    lea          r1, [r1+4*r2]
    punpcklbw    m1, m7
    punpcklbw    m2, m7
%if ARCH_X86_64
%define PW_7  m8
%define PW_42 m9
%define PW_96 m10
%define PW_64 m11
    mova         m8, [pw_7]
    mova         m9, [pw_42]
    mova        m10, [pw_96]
    mova        m11, [pw_64]
%else
%define PW_7  [pw_7]
%define PW_42 [pw_42]
%define PW_96 [pw_96]
%define PW_64 [pw_64]
%endif
    punpcklbw    m3, m7
    punpcklbw    m0, m7

.loop:
    FILT_V3      %1
    FILT_V3      %1
    FILT_V3      %1
    FILT_V3      %1
    SWAP          0, 1, 2, 3, 4
    mova         m3, m2
    mova         m2, m1
    mova         m1, m0
    mova         m0, m4
    sub         r3d, 4
    jne       .loop
    RET
%endmacro

INIT_XMM sse2
CAVS_QPEL_MC03 avg
CAVS_QPEL_MC03 put
