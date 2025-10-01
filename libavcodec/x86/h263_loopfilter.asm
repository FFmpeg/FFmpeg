;******************************************************************************
;* SSE2-optimized H.263 loop filter
;* Copyright (c) 2003-2013 Michael Niedermayer
;* Copyright (c) 2013 Daniel Kang
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

cextern pb_FC
cextern h263_loop_filter_strength

SECTION .text

%macro H263_LOOP_FILTER 5
    pxor         m7, m7
    movq         m0, [%1]
    movq         m6, [%4]
    mova         m5, m0
    punpcklbw    m0, m7
    punpcklbw    m6, m7
    psubw        m0, m6
    movq         m2, [%2]
    movq         m1, [%3]
    mova         m3, m2
    mova         m4, m1
    punpcklbw    m2, m7
    punpcklbw    m1, m7
    psubw        m1, m2
    psllw        m1, 2
    paddw        m1, m0
    pxor         m6, m6
    pcmpgtw      m6, m1
    pxor         m1, m6
    psubw        m1, m6
    psrlw        m1, 3
    packuswb     m1, m7
    packsswb     m6, m7
    movd         m2, %5
    punpcklbw    m2, m2
    punpcklbw    m2, m2
    punpcklbw    m2, m2
    psubusb      m2, m1
    mova         m7, m2
    psubusb      m7, m1
    psubb        m2, m7
    pxor         m3, m6
    pxor         m4, m6
    paddusb      m3, m2
    psubusb      m4, m2
    pxor         m7, m7
    pxor         m3, m6
    pxor         m4, m6
    paddusb      m2, m2
    packsswb     m0, m7
    pcmpgtb      m7, m0
    pxor         m0, m7
    psubb        m0, m7
    mova         m1, m0
    psubusb      m0, m2
    psubb        m1, m0
    pand         m1, [pb_FC]
    psrlw        m1, 2
    pxor         m1, m7
    psubb        m1, m7
    movq         m6, [%4]
    psubb        m5, m1
    paddb        m6, m1
%endmacro

INIT_XMM sse2
; void ff_h263_v_loop_filter_sse2(uint8_t *src, int stride, int qscale)
cglobal h263_v_loop_filter, 3,5,8
    movsxdifnidn r1, r1d
    movsxdifnidn r2, r2d

    lea          r3, [h263_loop_filter_strength]
    movzx       r2d, BYTE [r3+r2]
    shl         r2d, 1

    mov          r3, r0
    sub          r3, r1
    mov          r4, r3
    sub          r4, r1
    H263_LOOP_FILTER r4, r3, r0, r0+r1, r2d

    movq       [r3], m3
    movq       [r0], m4
    movq       [r4], m5
    movq    [r0+r1], m6
    RET

%macro TRANSPOSE4X4 2
    movd         %1, [%2]
    movd         m2, [%2+r1]
    movd         m3, [%2+r1*2]
    movd         m4, [%2+r3]
    punpcklbw    %1, m2
    punpcklbw    m3, m4
    punpcklwd    %1, m3
%endmacro


; void ff_h263_h_loop_filter_sse2(uint8_t *src, int stride, int qscale)
INIT_XMM sse2
cglobal h263_h_loop_filter, 3,5,8,32
    movsxdifnidn r1, r1d
    movsxdifnidn r2, r2d

    lea          r4, [h263_loop_filter_strength]
    movzx       r2d, BYTE [r4+r2]
    shl         r2d, 1

    sub          r0, 2
    lea          r3, [r1*3]
    lea          r4, [r0+r1*4]

    TRANSPOSE4X4 m0, r0
    TRANSPOSE4X4 m1, r4
    mova         m2, m0
    punpckldq    m0, m1
    mova      [rsp], m0
    punpckhdq    m2, m1
    mova   [rsp+16], m2

    H263_LOOP_FILTER rsp, rsp+8, rsp+16, rsp+24, r2d

    punpcklbw    m5, m3
    punpcklbw    m4, m6
    mova         m0, m5
    punpcklwd    m5, m4
    punpckhwd    m0, m4
    movd       [r0], m5
    movd       [r4], m0
    pshufd       m1, m5, 0x1
    pshufd       m2, m0, 0x1
    movd  [r0+r1*1], m1
    movd  [r4+r1*1], m2
    punpckhdq    m5, m5
    punpckhdq    m0, m0
    movd  [r0+r1*2], m5
    movd  [r4+r1*2], m0
    punpckhdq    m5, m5
    punpckhdq    m0, m0
    movd    [r0+r3], m5
    movd    [r4+r3], m0
    RET
