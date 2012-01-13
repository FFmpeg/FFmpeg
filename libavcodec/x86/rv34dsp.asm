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

%macro rv34_idct_dequant4x4_dc 1
cglobal rv34_idct_dequant4x4_%1_mmx2, 1, 2, 0
    movsx   r1, word [r0]
    IDCT_DC r1
    movd    mm0, r1
    pshufw  mm0, mm0, 0
    movq    [r0+ 0], mm0
    movq    [r0+16], mm0
    movq    [r0+32], mm0
    movq    [r0+48], mm0
    REP_RET
%endmacro

INIT_MMX
%define IDCT_DC IDCT_DC_ROUND
rv34_idct_dequant4x4_dc dc
%define IDCT_DC IDCT_DC_NOROUND
rv34_idct_dequant4x4_dc dc_noround
