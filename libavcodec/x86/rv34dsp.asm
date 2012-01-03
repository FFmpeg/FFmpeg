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
cglobal rv34_idct_%1_mmx2, 1, 2, 0
    movsx   r1, word [r0]
    IDCT_DC r1
    movd    m0, r1
    pshufw  m0, m0, 0
    movq    [r0+ 0], m0
    movq    [r0+ 8], m0
    movq    [r0+16], m0
    movq    [r0+24], m0
    REP_RET
%endmacro

INIT_MMX
%define IDCT_DC IDCT_DC_ROUND
rv34_idct dc
%define IDCT_DC IDCT_DC_NOROUND
rv34_idct dc_noround

; ff_rv34_idct_dc_add_mmx(uint8_t *dst, int stride, int dc);
cglobal rv34_idct_dc_add_mmx, 3, 3
    ; calculate DC
    IDCT_DC_ROUND r2
    pxor       m1, m1
    movd       m0, r2
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

; ff_rv34_idct_dc_add_sse4(uint8_t *dst, int stride, int dc);
INIT_XMM
cglobal rv34_idct_dc_add_sse4, 3, 3, 6
    ; load data
    IDCT_DC_ROUND r2
    pxor       m1, m1

    ; calculate DC
    movd       m0, r2
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
