;******************************************************************************
;* MMX-optimized H.263 loop filter
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

SECTION_RODATA
cextern pb_FC
cextern h263_loop_filter_strength

SECTION_TEXT

%macro H263_LOOP_FILTER 5
    pxor         m7, m7
    mova         m0, [%1]
    mova         m1, [%1]
    mova         m2, [%4]
    mova         m3, [%4]
    punpcklbw    m0, m7
    punpckhbw    m1, m7
    punpcklbw    m2, m7
    punpckhbw    m3, m7
    psubw        m0, m2
    psubw        m1, m3
    mova         m2, [%2]
    mova         m3, [%2]
    mova         m4, [%3]
    mova         m5, [%3]
    punpcklbw    m2, m7
    punpckhbw    m3, m7
    punpcklbw    m4, m7
    punpckhbw    m5, m7
    psubw        m4, m2
    psubw        m5, m3
    psllw        m4, 2
    psllw        m5, 2
    paddw        m4, m0
    paddw        m5, m1
    pxor         m6, m6
    pcmpgtw      m6, m4
    pcmpgtw      m7, m5
    pxor         m4, m6
    pxor         m5, m7
    psubw        m4, m6
    psubw        m5, m7
    psrlw        m4, 3
    psrlw        m5, 3
    packuswb     m4, m5
    packsswb     m6, m7
    pxor         m7, m7
    movd         m2, %5
    punpcklbw    m2, m2
    punpcklbw    m2, m2
    punpcklbw    m2, m2
    psubusb      m2, m4
    mova         m3, m2
    psubusb      m3, m4
    psubb        m2, m3
    mova         m3, [%2]
    mova         m4, [%3]
    pxor         m3, m6
    pxor         m4, m6
    paddusb      m3, m2
    psubusb      m4, m2
    pxor         m3, m6
    pxor         m4, m6
    paddusb      m2, m2
    packsswb     m0, m1
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
    mova         m5, [%1]
    mova         m6, [%4]
    psubb        m5, m1
    paddb        m6, m1
%endmacro

INIT_MMX mmx
; void h263_v_loop_filter(uint8_t *src, int stride, int qscale)
cglobal h263_v_loop_filter, 3,5
    movsxdifnidn r1, r1d
    movsxdifnidn r2, r2d

    lea          r4, [h263_loop_filter_strength]
    movzx       r3d, BYTE [r4+r2]
    movsx        r2, r3b
    shl          r2, 1

    mov          r3, r0
    sub          r3, r1
    mov          r4, r3
    sub          r4, r1
    H263_LOOP_FILTER r4, r3, r0, r0+r1, r2d

    mova       [r3], m3
    mova       [r0], m4
    mova       [r4], m5
    mova    [r0+r1], m6
    RET

%macro TRANSPOSE4X4 2
    movd      m0, [%1]
    movd      m1, [%1+r1]
    movd      m2, [%1+r1*2]
    movd      m3, [%1+r3]
    punpcklbw m0, m1
    punpcklbw m2, m3
    mova      m1, m0
    punpcklwd m0, m2
    punpckhwd m1, m2
    movd [%2+ 0], m0
    punpckhdq m0, m0
    movd [%2+ 8], m0
    movd [%2+16], m1
    punpckhdq m1, m1
    movd [%2+24], m1
%endmacro


; void h263_h_loop_filter(uint8_t *src, int stride, int qscale)
INIT_MMX mmx
cglobal h263_h_loop_filter, 3,5,0,32
    movsxdifnidn r1, r1d
    movsxdifnidn r2, r2d

    lea          r4, [h263_loop_filter_strength]
    movzx       r3d, BYTE [r4+r2]
    movsx        r2, r3b
    shl          r2, 1

    sub          r0, 2
    lea          r3, [r1*3]

    TRANSPOSE4X4 r0, rsp
    lea          r4, [r0+r1*4]
    TRANSPOSE4X4 r4, rsp+4

    H263_LOOP_FILTER rsp, rsp+8, rsp+16, rsp+24, r2d

    mova         m1, m5
    mova         m0, m4
    punpcklbw    m5, m3
    punpcklbw    m4, m6
    punpckhbw    m1, m3
    punpckhbw    m0, m6
    mova         m3, m5
    mova         m6, m1
    punpcklwd    m5, m4
    punpcklwd    m1, m0
    punpckhwd    m3, m4
    punpckhwd    m6, m0
    movd       [r0], m5
    punpckhdq    m5, m5
    movd  [r0+r1*1], m5
    movd  [r0+r1*2], m3
    punpckhdq    m3, m3
    movd    [r0+r3], m3
    movd       [r4], m1
    punpckhdq    m1, m1
    movd  [r4+r1*1], m1
    movd  [r4+r1*2], m6
    punpckhdq    m6, m6
    movd    [r4+r3], m6
    RET
