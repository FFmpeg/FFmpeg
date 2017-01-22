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

INIT_MMX mmx
cglobal pullup_filter_diff, 3, 5, 8, first, second, size
    mov        r3, 4
    pxor       m4, m4
    pxor       m7, m7

.loop:
    movq       m0, [firstq]
    movq       m2, [firstq]
    add        firstq, sizeq
    movq       m1, [secondq]
    add        secondq, sizeq
    psubusb    m2, m1
    psubusb    m1, m0
    movq       m0, m2
    movq       m3, m1
    punpcklbw  m0, m7
    punpcklbw  m1, m7
    punpckhbw  m2, m7
    punpckhbw  m3, m7
    paddw      m4, m0
    paddw      m4, m1
    paddw      m4, m2
    paddw      m4, m3

    dec        r3
    jnz .loop

    movq       m3, m4
    punpcklwd  m4, m7
    punpckhwd  m3, m7
    paddd      m3, m4
    movd      eax, m3
    psrlq      m3, 32
    movd      r4d, m3
    add       eax, r4d
    RET

INIT_MMX mmx
cglobal pullup_filter_comb, 3, 5, 8, first, second, size
    mov        r3, 4
    pxor       m6, m6
    pxor       m7, m7
    sub        secondq, sizeq

.loop:
    movq       m0, [firstq]
    movq       m1, [secondq]
    punpcklbw  m0, m7
    movq       m2, [secondq+sizeq]
    punpcklbw  m1, m7
    punpcklbw  m2, m7
    paddw      m0, m0
    paddw      m1, m2
    movq       m2, m0
    psubusw    m0, m1
    psubusw    m1, m2
    paddw      m6, m0
    paddw      m6, m1

    movq       m0, [firstq]
    movq       m1, [secondq]
    punpckhbw  m0, m7
    movq       m2, [secondq+sizeq]
    punpckhbw  m1, m7
    punpckhbw  m2, m7
    paddw      m0, m0
    paddw      m1, m2
    movq       m2, m0
    psubusw    m0, m1
    psubusw    m1, m2
    paddw      m6, m0
    paddw      m6, m1

    movq       m0, [secondq+sizeq]
    movq       m1, [firstq]
    punpcklbw  m0, m7
    movq       m2, [firstq+sizeq]
    punpcklbw  m1, m7
    punpcklbw  m2, m7
    paddw      m0, m0
    paddw      m1, m2
    movq       m2, m0
    psubusw    m0, m1
    psubusw    m1, m2
    paddw      m6, m0
    paddw      m6, m1

    movq       m0, [secondq+sizeq]
    movq       m1, [firstq]
    punpckhbw  m0, m7
    movq       m2, [firstq+sizeq]
    punpckhbw  m1, m7
    punpckhbw  m2, m7
    paddw      m0, m0
    paddw      m1, m2
    movq       m2, m0
    psubusw    m0, m1
    psubusw    m1, m2
    paddw      m6, m0
    paddw      m6, m1

    add        firstq, sizeq
    add        secondq, sizeq
    dec        r3
    jnz .loop

    movq       m5, m6
    punpcklwd  m6, m7
    punpckhwd  m5, m7
    paddd      m5, m6
    movd      eax, m5
    psrlq      m5, 32
    movd      r4d, m5
    add       eax, r4d
    RET

INIT_MMX mmx
cglobal pullup_filter_var, 3, 5, 8, first, second, size
    mov        r3, 3
    pxor       m4, m4
    pxor       m7, m7

.loop:
    movq       m0, [firstq]
    movq       m2, [firstq]
    movq       m1, [firstq+sizeq]
    add        firstq, sizeq
    psubusb    m2, m1
    psubusb    m1, m0
    movq       m0, m2
    movq       m3, m1
    punpcklbw  m0, m7
    punpcklbw  m1, m7
    punpckhbw  m2, m7
    punpckhbw  m3, m7
    paddw      m4, m0
    paddw      m4, m1
    paddw      m4, m2
    paddw      m4, m3

    dec        r3
    jnz .loop

    movq       m3, m4
    punpcklwd  m4, m7
    punpckhwd  m3, m7
    paddd      m3, m4
    movd      eax, m3
    psrlq      m3, 32
    movd      r4d, m3
    add       eax, r4d
    shl       eax, 2
    RET
