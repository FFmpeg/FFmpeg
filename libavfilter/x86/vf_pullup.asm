;*****************************************************************************
;* x86-optimized functions for pullup filter
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License as published by
;* the Free Software Foundation; either version 2 of the License, or
;* (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;* GNU General Public License for more details.
;*
;* You should have received a copy of the GNU General Public License along
;* with FFmpeg; if not, write to the Free Software Foundation, Inc.,
;* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION .text

INIT_XMM sse2
cglobal pullup_filter_diff, 3, 4, 3, first, second, size
    mov        r3, 4
    pxor       m2, m2

.loop:
    movq       m0, [firstq]
    add        firstq, sizeq
    movq       m1, [secondq]
    add        secondq, sizeq
    psadbw     m0, m1
    paddw      m2, m0

    dec        r3
    jnz .loop

    movd      eax, m2
    RET

INIT_XMM ssse3
cglobal pullup_filter_comb, 3, 5, 7, first, second, size
    movq       m0, [firstq]
    sub   secondq, sizeq
    movq       m1, [secondq]
    pxor       m6, m6
    punpcklbw  m0, m6
    punpcklbw  m1, m6
    add    firstq, sizeq
    add   secondq, sizeq
    pxor       m5, m5
    mov        r3, 4

.loop:
    movq       m2, [firstq]
    movq       m3, [secondq]
    add    firstq, sizeq
    add   secondq, sizeq
    punpcklbw  m2, m6
    punpcklbw  m3, m6
    mova       m4, m0

    paddw      m0, m0
    paddw      m1, m3
    psubw      m0, m1
    pabsw      m0, m0
    paddw      m5, m0

    mova       m1, m3
    paddw      m4, m2
    paddw      m3, m3
    psubw      m3, m4
    pabsw      m3, m3
    paddw      m5, m3
    mova       m2, m0

    dec        r3
    jnz .loop

    movq       m0, m5
    punpcklwd  m5, m6
    punpckhwd  m0, m6
    paddd      m0, m5
    pshufd     m5, m0, 0xE
    paddd      m0, m5
    pshufd     m5, m0, 0x1
    paddd      m0, m5
    movd      eax, m0
    RET

INIT_XMM sse2
cglobal pullup_filter_var, 3, 3, 3, first, second, size
    movq       m0, [firstq]
    add        firstq, sizeq
    movq       m1, [firstq]
    pxor       m2, m2
    psadbw     m0, m1
    paddw      m2, m0
    movq       m0, [firstq+sizeq]
    psadbw     m1, m0
    paddw      m2, m1
    movq       m1, [firstq+2*sizeq]
    psadbw     m0, m1
    paddw      m2, m0
    movd      eax, m2
    shl       eax, 2
    RET
