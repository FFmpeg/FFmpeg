;******************************************************************************
;* MMX/SSE2-optimized functions for the RV30 and RV40 decoders
;* Copyright (C) 2012 Christophe Gisquet <christophe.gisquet@gmail.com>
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

SECTION_RODATA
pw_row_coeffs:  times 4 dw 13
                times 4 dw 17
                times 4 dw  7
pd_512: times 2 dd 0x200
pw_col_coeffs:  dw 13,  13,  13, -13
                dw 17,   7,   7, -17
                dw 13, -13,  13,  13
                dw -7,  17, -17,  -7

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

%macro rv34_idct 1
cglobal rv34_idct_%1, 1, 2, 0
    movsx   r1, word [r0]
    IDCT_DC r1
    movd    m0, r1d
    pshufw  m0, m0, 0
    movq    [r0+ 0], m0
    movq    [r0+ 8], m0
    movq    [r0+16], m0
    movq    [r0+24], m0
    REP_RET
%endmacro

INIT_MMX mmx2
%define IDCT_DC IDCT_DC_ROUND
rv34_idct dc
%define IDCT_DC IDCT_DC_NOROUND
rv34_idct dc_noround

; ff_rv34_idct_dc_add_mmx(uint8_t *dst, int stride, int dc);
INIT_MMX mmx
cglobal rv34_idct_dc_add, 3, 3
    ; calculate DC
    IDCT_DC_ROUND r2
    pxor       m1, m1
    movd       m0, r2d
    psubw      m1, m0
    packuswb   m0, m0
    packuswb   m1, m1
    punpcklbw  m0, m0
    punpcklbw  m1, m1
    punpcklwd  m0, m0
    punpcklwd  m1, m1

    ; add DC
    lea        r2, [r0+r1*2]
    movh       m2, [r0]
    movh       m3, [r0+r1]
    movh       m4, [r2]
    movh       m5, [r2+r1]
    paddusb    m2, m0
    paddusb    m3, m0
    paddusb    m4, m0
    paddusb    m5, m0
    psubusb    m2, m1
    psubusb    m3, m1
    psubusb    m4, m1
    psubusb    m5, m1
    movh       [r0], m2
    movh       [r0+r1], m3
    movh       [r2], m4
    movh       [r2+r1], m5
    RET

; Load coeffs and perform row transform
; Output: coeffs in mm[0467], rounder in mm5
%macro ROW_TRANSFORM  1
    pxor        mm7, mm7
    mova        mm0, [%1+ 0*8]
    mova        mm1, [%1+ 1*8]
    mova        mm2, [%1+ 2*8]
    mova        mm3, [%1+ 3*8]
    mova  [%1+ 0*8], mm7
    mova  [%1+ 1*8], mm7
    mova  [%1+ 2*8], mm7
    mova  [%1+ 3*8], mm7
    mova        mm4, mm0
    mova        mm6, [pw_row_coeffs+ 0]
    paddsw      mm0, mm2                ; b0 + b2
    psubsw      mm4, mm2                ; b0 - b2
    pmullw      mm0, mm6                ; *13 = z0
    pmullw      mm4, mm6                ; *13 = z1
    mova        mm5, mm1
    pmullw      mm1, [pw_row_coeffs+ 8] ; b1*17
    pmullw      mm5, [pw_row_coeffs+16] ; b1* 7
    mova        mm7, mm3
    pmullw      mm3, [pw_row_coeffs+ 8] ; b3*17
    pmullw      mm7, [pw_row_coeffs+16] ; b3* 7
    paddsw      mm1, mm7                ; z3 = b1*17 + b3* 7
    psubsw      mm5, mm3                ; z2 = b1* 7 - b3*17
    mova        mm7, mm0
    mova        mm6, mm4
    paddsw      mm0, mm1                ; z0 + z3
    psubsw      mm7, mm1                ; z0 - z3
    paddsw      mm4, mm5                ; z1 + z2
    psubsw      mm6, mm5                ; z1 - z2
    mova        mm5, [pd_512]           ; 0x200
%endmacro

; ff_rv34_idct_add_mmx2(uint8_t *dst, ptrdiff_t stride, DCTELEM *block);
%macro COL_TRANSFORM  4
    pshufw      mm3, %2, 0xDD        ; col. 1,3,1,3
    pshufw       %2, %2, 0x88        ; col. 0,2,0,2
    pmaddwd      %2, %3              ; 13*c0+13*c2 | 13*c0-13*c2 = z0 | z1
    pmaddwd     mm3, %4              ; 17*c1+ 7*c3 |  7*c1-17*c3 = z3 | z2
    paddd        %2, mm5
    pshufw      mm1,  %2, 01001110b  ;    z1 | z0
    pshufw      mm2, mm3, 01001110b  ;    z2 | z3
    paddd        %2, mm3             ; z0+z3 | z1+z2
    psubd       mm1, mm2             ; z1-z2 | z0-z3
    movd        mm3, %1
    psrad        %2, 10
    pxor        mm2, mm2
    psrad       mm1, 10
    punpcklbw   mm3, mm2
    packssdw     %2, mm1
    paddw        %2, mm3
    packuswb     %2, %2
    movd         %1, %2
%endmacro
INIT_MMX mmx2
cglobal rv34_idct_add, 3,3,0, d, s, b
    ROW_TRANSFORM       bq
    COL_TRANSFORM     [dq], mm0, [pw_col_coeffs+ 0], [pw_col_coeffs+ 8]
    mova               mm0, [pw_col_coeffs+ 0]
    COL_TRANSFORM  [dq+sq], mm4, mm0, [pw_col_coeffs+ 8]
    mova               mm4, [pw_col_coeffs+ 8]
    lea                 dq, [dq + 2*sq]
    COL_TRANSFORM     [dq], mm6, mm0, mm4
    COL_TRANSFORM  [dq+sq], mm7, mm0, mm4
    ret

; ff_rv34_idct_dc_add_sse4(uint8_t *dst, int stride, int dc);
INIT_XMM sse4
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
    pextrd [r0+r1], m2, 1
    pextrd    [r2], m2, 2
    pextrd [r2+r1], m2, 3
    RET
