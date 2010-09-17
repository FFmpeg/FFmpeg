;*****************************************************************************
;* MMX optimized DSP utils
;*****************************************************************************
;* Copyright (c) 2000, 2001 Fabrice Bellard
;* Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;*****************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

SECTION .text

INIT_XMM
; sse16_sse2(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h)
cglobal sse16_sse2, 5, 5, 8
    shr       r4, 1
    pxor      m0, m0         ; mm0 = 0
    pxor      m7, m7         ; mm7 holds the sum

.next2lines ; FIXME why are these unaligned movs? pix1[] is aligned
    movu      m1, [r1   ]    ; mm1 = pix1[0][0-15]
    movu      m2, [r2   ]    ; mm2 = pix2[0][0-15]
    movu      m3, [r1+r3]    ; mm3 = pix1[1][0-15]
    movu      m4, [r2+r3]    ; mm4 = pix2[1][0-15]

    ; todo: mm1-mm2, mm3-mm4
    ; algo: subtract mm1 from mm2 with saturation and vice versa
    ;       OR the result to get the absolute difference
    mova      m5, m1
    mova      m6, m3
    psubusb   m1, m2
    psubusb   m3, m4
    psubusb   m2, m5
    psubusb   m4, m6

    por       m2, m1
    por       m4, m3

    ; now convert to 16-bit vectors so we can square them
    mova      m1, m2
    mova      m3, m4

    punpckhbw m2, m0
    punpckhbw m4, m0
    punpcklbw m1, m0         ; mm1 not spread over (mm1,mm2)
    punpcklbw m3, m0         ; mm4 not spread over (mm3,mm4)

    pmaddwd   m2, m2
    pmaddwd   m4, m4
    pmaddwd   m1, m1
    pmaddwd   m3, m3

    lea       r1, [r1+r3*2]  ; pix1 += 2*line_size
    lea       r2, [r2+r3*2]  ; pix2 += 2*line_size

    paddd     m1, m2
    paddd     m3, m4
    paddd     m7, m1
    paddd     m7, m3

    dec       r4
    jnz .next2lines

    mova      m1, m7
    psrldq    m7, 8          ; shift hi qword to lo
    paddd     m7, m1
    mova      m1, m7
    psrldq    m7, 4          ; shift hi dword to lo
    paddd     m7, m1
    movd     eax, m7         ; return value
    RET
