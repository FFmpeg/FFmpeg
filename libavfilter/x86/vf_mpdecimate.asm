;*****************************************************************************
;* x86-optimized functions for mpdecimate filter
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

SECTION .text

;------------------------------------------------------------------------------
; int ff_mpdecimate_sad_8x8_16(const uint8_t *src1, ptrdiff_t stride1,
;                              const uint8_t *src2, ptrdiff_t stride2);
;
; SAD of an 8x8 block of unaligned 16-bit samples, strides in bytes.
;------------------------------------------------------------------------------
INIT_XMM sse2
cglobal mpdecimate_sad_8x8_16, 4, 5, 5, src1, stride1, src2, stride2, cnt
    pxor            m0, m0
    pxor            m4, m4
    mov           cntd, 8
.loop:
    movu            m1, [src1q]
    movu            m2, [src2q]
    mova            m3, m1
    psubusw         m1, m2
    psubusw         m2, m3
    por             m1, m2          ; |src1 - src2|, full 16-bit range
    mova            m2, m1
    punpcklwd       m1, m4
    punpckhwd       m2, m4
    paddd           m0, m1
    paddd           m0, m2
    add          src1q, stride1q
    add          src2q, stride2q
    dec           cntd
    jg .loop
    HADDD           m0, m1
    movd           eax, m0
    RET
