;*****************************************************************************
;* SSE2-optimized H.264 iDCT
;*****************************************************************************
;* Copyright (C) 2003-2008 x264 project
;*
;* Authors: Laurent Aimar <fenrir@via.ecp.fr>
;*          Loren Merritt <lorenm@u.washington.edu>
;*          Holger Lubitz <hal@duncan.ol.sub.de>
;*          Min Chen <chenm001.163.com>
;*
;* This program is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License as published by
;* the Free Software Foundation; either version 2 of the License, or
;* (at your option) any later version.
;*
;* This program is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;* GNU General Public License for more details.
;*
;* You should have received a copy of the GNU General Public License
;* along with this program; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
;*****************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

SECTION_RODATA
pw_32: times 8 dw 32

SECTION .text

%macro IDCT4_1D 6
    SUMSUB_BA   m%3, m%1
    SUMSUBD2_AB m%2, m%4, m%6, m%5
    SUMSUB_BADC m%2, m%3, m%5, m%1
    SWAP %1, %2, %5, %4, %3
%endmacro

INIT_XMM
cglobal x264_add8x4_idct_sse2, 3,3
    movq   m0, [r1+ 0]
    movq   m1, [r1+ 8]
    movq   m2, [r1+16]
    movq   m3, [r1+24]
    movhps m0, [r1+32]
    movhps m1, [r1+40]
    movhps m2, [r1+48]
    movhps m3, [r1+56]
    IDCT4_1D 0,1,2,3,4,5
    TRANSPOSE2x4x4W 0,1,2,3,4
    paddw m0, [pw_32 GLOBAL]
    IDCT4_1D 0,1,2,3,4,5
    pxor  m7, m7
    STORE_DIFF  m0, m4, m7, [r0]
    STORE_DIFF  m1, m4, m7, [r0+r2]
    lea   r0, [r0+r2*2]
    STORE_DIFF  m2, m4, m7, [r0]
    STORE_DIFF  m3, m4, m7, [r0+r2]
    RET
