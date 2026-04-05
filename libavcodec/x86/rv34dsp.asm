;******************************************************************************
;* ASM-optimized functions for the RV30 and RV40 decoders
;* Copyright (C) 2012 Christophe Gisquet <christophe.gisquet@gmail.com>
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
; 0 1 2 3 (words) -> 1 3 2 0 1 3 2 0 (words)
shuffle: times 2 db 2, 3, 6, 7, 4, 5, 0, 1

pw_13:   times 8 dw 13
pw_17:   times 8 dw 17
pw_7:    times 8 dw  7
pw_col_coeffs: dw -17, -7, -13, 13, 7, -17, 13, 13
pd_512:  times 4 dd 0x200

SECTION .text

%macro IDCT_DC_NOROUND 1
    imul   %1, 13*13*3
    sar    %1, 11
%endmacro

%macro IDCT_DC_ROUND 1
    imul   %1, 13*13
    add    %1, 0x200
    sar    %1, 10
%endmacro

INIT_XMM sse2
cglobal rv34_idct_dc_noround, 1, 2, 1
    movsx   r1, word [r0]
    IDCT_DC_NOROUND r1
    movd    m0, r1d
    SPLATW  m0, m0
    mova    [r0+ 0], m0
    mova    [r0+16], m0
    RET

%macro COL_TRANSFORM  3
    ; -17*c1-7*c3 | 13*c0-13*c2 | 7*c1-17*c3 | 13*c1+13*c2 = -z3 | z1 | z2 | z0
    pmaddwd      %2, m7
    movd         m3, %1
    pshufd       %3, %2, q0123       ; z0 | z2 | z1 | -z3
    psignd       %2, m7              ; z3 | z1 |-z2 | z0
    paddd        %3, m5
    paddd        %2, %3              ; z0+z3 | z1+z2 | z1-z2 | z0-z3 (+round)
%ifidn %3,m1
    pxor         m1, m1
%endif
    psrad        %2, 10
    punpcklbw    m3, m1
    packssdw     %2, %2
    paddw        %2, m3
    packuswb     %2, %2
    movd         %1, %2
%endmacro

INIT_XMM ssse3
; ff_rv34_idct_add_ssse3(uint8_t *dst, ptrdiff_t stride, int16_t *block)
cglobal rv34_idct_add, 3, 3, 8, dst, stride, block
    ; row transform
    movq            m0, [blockq + 0*8]
    movq            m1, [blockq + 1*8]
    movq            m2, [blockq + 2*8]
    movq            m3, [blockq + 3*8]
    pxor            m7, m7
    mova            m6, [shuffle]
    mova [blockq +  0], m7
    mova [blockq + 16], m7
    mova            m4, m0
    mova            m5, [pw_13]
    paddsw          m0, m2                 ; b0 + b2
    pshufb          m1, m6
    psubsw          m4, m2                 ; b0 - b2
    pmullw          m0, m5                 ; *13 = z0
    pshufb          m3, m6
    pmullw          m4, m5                 ; *13 = z1
    mova            m5, m1
    pmullw          m1, [pw_17]            ; b1*17
    pmullw          m5, [pw_7]             ; b1* 7
    pshufb          m0, m6
    mova            m2, m3
    pmullw          m3, [pw_17]            ; b3*17
    pmullw          m2, [pw_7]             ; b3* 7
    pshufb          m4, m6
    mova            m7, [pw_col_coeffs]
    paddsw          m1, m2                 ; z3 = b1*17 + b3* 7
    psubsw          m5, m3                 ; z2 = b1* 7 - b3*17
    mova            m2, m0
    mova            m6, m4
    paddsw          m0, m1                 ; z0 + z3
    paddsw          m4, m5                 ; z1 + z2
    psubsw          m2, m1                 ; z0 - z3
    psubsw          m6, m5                 ; z1 - z2
    mova            m5, [pd_512]           ; 0x200
    COL_TRANSFORM [dstq], m0, m1
    COL_TRANSFORM [dstq+strideq], m4, m0
    lea                dstq, [dstq + 2*strideq]
    COL_TRANSFORM [dstq], m6, m0
    COL_TRANSFORM [dstq+strideq], m2, m0
    RET

; ff_rv34_idct_dc_add_sse4(uint8_t *dst, int stride, int dc);
%macro RV34_IDCT_DC_ADD 0
cglobal rv34_idct_dc_add, 3, 3, 6
    ; load data
    IDCT_DC_ROUND r2
    pxor       m1, m1

    ; calculate DC
    movd       m0, r2d
    lea        r2, [r0+r1*2]
    movd       m2, [r0]
    movd       m3, [r0+r1]
    pshuflw    m0, m0, 0
    movd       m4, [r2]
    movd       m5, [r2+r1]
    punpcklqdq m0, m0
    punpckldq  m2, m3
    punpckldq  m4, m5
    punpcklbw  m2, m1
    punpcklbw  m4, m1
    paddw      m2, m0
    paddw      m4, m0
    packuswb   m2, m4
    movd      [r0], m2
%if cpuflag(sse4)
    pextrd [r0+r1], m2, 1
    pextrd    [r2], m2, 2
    pextrd [r2+r1], m2, 3
%else
    psrldq     m2, 4
    movd   [r0+r1], m2
    psrldq     m2, 4
    movd      [r2], m2
    psrldq     m2, 4
    movd   [r2+r1], m2
%endif
    RET
%endmacro

INIT_XMM sse2
RV34_IDCT_DC_ADD
INIT_XMM sse4
RV34_IDCT_DC_ADD
