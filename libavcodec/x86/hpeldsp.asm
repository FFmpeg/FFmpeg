;******************************************************************************
;*
;* Copyright (c) 2000-2001 Fabrice Bellard <fabrice@bellard.org>
;* Copyright (c)      Nick Kurshev <nickols_k@mail.ru>
;* Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
;* Copyright (c) 2002 Zdenek Kabelac <kabi@informatics.muni.cz>
;* Copyright (c) 2013 Daniel Kang
;*
;* SIMD-optimized halfpel functions
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
cextern pb_1
cextern pw_2
pb_interleave16: db 0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15
pb_interleave8:  db 0, 4, 1, 5, 2, 6, 3, 7

cextern pw_8192

SECTION .text

; void ff_put_pixels8_x2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro PUT_PIXELS8_X2 0
%if cpuflag(sse2)
cglobal put_pixels16_x2, 4,5,4
%else
cglobal put_pixels8_x2, 4,5
%endif
    lea          r4, [r2*2]
.loop:
    movu         m0, [r1+1]
    movu         m1, [r1+r2+1]
%if cpuflag(sse2)
    movu         m2, [r1]
    movu         m3, [r1+r2]
    pavgb        m0, m2
    pavgb        m1, m3
%else
    PAVGB        m0, [r1]
    PAVGB        m1, [r1+r2]
%endif
    mova       [r0], m0
    mova    [r0+r2], m1
    add          r1, r4
    add          r0, r4
    movu         m0, [r1+1]
    movu         m1, [r1+r2+1]
%if cpuflag(sse2)
    movu         m2, [r1]
    movu         m3, [r1+r2]
    pavgb        m0, m2
    pavgb        m1, m3
%else
    PAVGB        m0, [r1]
    PAVGB        m1, [r1+r2]
%endif
    add          r1, r4
    mova       [r0], m0
    mova    [r0+r2], m1
    add          r0, r4
    sub         r3d, 4
    jne .loop
    REP_RET
%endmacro

INIT_MMX mmxext
PUT_PIXELS8_X2
INIT_MMX 3dnow
PUT_PIXELS8_X2


; void ff_put_pixels16_x2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro PUT_PIXELS_16 0
cglobal put_pixels16_x2, 4,5
    lea          r4, [r2*2]
.loop:
    mova         m0, [r1]
    mova         m1, [r1+r2]
    mova         m2, [r1+8]
    mova         m3, [r1+r2+8]
    PAVGB        m0, [r1+1]
    PAVGB        m1, [r1+r2+1]
    PAVGB        m2, [r1+9]
    PAVGB        m3, [r1+r2+9]
    mova       [r0], m0
    mova    [r0+r2], m1
    mova     [r0+8], m2
    mova  [r0+r2+8], m3
    add          r1, r4
    add          r0, r4
    mova         m0, [r1]
    mova         m1, [r1+r2]
    mova         m2, [r1+8]
    mova         m3, [r1+r2+8]
    PAVGB        m0, [r1+1]
    PAVGB        m1, [r1+r2+1]
    PAVGB        m2, [r1+9]
    PAVGB        m3, [r1+r2+9]
    add          r1, r4
    mova       [r0], m0
    mova    [r0+r2], m1
    mova     [r0+8], m2
    mova  [r0+r2+8], m3
    add          r0, r4
    sub         r3d, 4
    jne .loop
    REP_RET
%endmacro

INIT_MMX mmxext
PUT_PIXELS_16
INIT_MMX 3dnow
PUT_PIXELS_16
; The 8_X2 macro can easily be used here
INIT_XMM sse2
PUT_PIXELS8_X2


; void ff_put_no_rnd_pixels8_x2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro PUT_NO_RND_PIXELS8_X2 0
cglobal put_no_rnd_pixels8_x2, 4,5
    mova         m6, [pb_1]
    lea          r4, [r2*2]
.loop:
    mova         m0, [r1]
    mova         m2, [r1+r2]
    mova         m1, [r1+1]
    mova         m3, [r1+r2+1]
    add          r1, r4
    psubusb      m0, m6
    psubusb      m2, m6
    PAVGB        m0, m1
    PAVGB        m2, m3
    mova       [r0], m0
    mova    [r0+r2], m2
    mova         m0, [r1]
    mova         m1, [r1+1]
    mova         m2, [r1+r2]
    mova         m3, [r1+r2+1]
    add          r0, r4
    add          r1, r4
    psubusb      m0, m6
    psubusb      m2, m6
    PAVGB        m0, m1
    PAVGB        m2, m3
    mova       [r0], m0
    mova    [r0+r2], m2
    add          r0, r4
    sub         r3d, 4
    jne .loop
    REP_RET
%endmacro

INIT_MMX mmxext
PUT_NO_RND_PIXELS8_X2
INIT_MMX 3dnow
PUT_NO_RND_PIXELS8_X2


; void ff_put_pixels8_y2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro PUT_PIXELS8_Y2 0
%if cpuflag(sse2)
cglobal put_pixels16_y2, 4,5,3
%else
cglobal put_pixels8_y2, 4,5
%endif
    lea          r4, [r2*2]
    movu         m0, [r1]
    sub          r0, r2
.loop:
    movu         m1, [r1+r2]
    movu         m2, [r1+r4]
    add          r1, r4
    PAVGB        m0, m1
    PAVGB        m1, m2
    mova    [r0+r2], m0
    mova    [r0+r4], m1
    movu         m1, [r1+r2]
    movu         m0, [r1+r4]
    add          r0, r4
    add          r1, r4
    PAVGB        m2, m1
    PAVGB        m1, m0
    mova    [r0+r2], m2
    mova    [r0+r4], m1
    add          r0, r4
    sub         r3d, 4
    jne .loop
    REP_RET
%endmacro

INIT_MMX mmxext
PUT_PIXELS8_Y2
INIT_MMX 3dnow
PUT_PIXELS8_Y2
; actually, put_pixels16_y2_sse2
INIT_XMM sse2
PUT_PIXELS8_Y2


; void ff_put_no_rnd_pixels8_y2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro PUT_NO_RND_PIXELS8_Y2 0
cglobal put_no_rnd_pixels8_y2, 4,5
    mova         m6, [pb_1]
    lea          r4, [r2+r2]
    mova         m0, [r1]
    sub          r0, r2
.loop:
    mova         m1, [r1+r2]
    mova         m2, [r1+r4]
    add          r1, r4
    psubusb      m1, m6
    PAVGB        m0, m1
    PAVGB        m1, m2
    mova    [r0+r2], m0
    mova    [r0+r4], m1
    mova         m1, [r1+r2]
    mova         m0, [r1+r4]
    add          r0, r4
    add          r1, r4
    psubusb      m1, m6
    PAVGB        m2, m1
    PAVGB        m1, m0
    mova    [r0+r2], m2
    mova    [r0+r4], m1
    add          r0, r4
    sub         r3d, 4
    jne .loop
    REP_RET
%endmacro

INIT_MMX mmxext
PUT_NO_RND_PIXELS8_Y2
INIT_MMX 3dnow
PUT_NO_RND_PIXELS8_Y2


; void ff_avg_pixels8(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro AVG_PIXELS8 0
cglobal avg_pixels8, 4,5
    lea          r4, [r2*2]
.loop:
    mova         m0, [r0]
    mova         m1, [r0+r2]
    PAVGB        m0, [r1]
    PAVGB        m1, [r1+r2]
    mova       [r0], m0
    mova    [r0+r2], m1
    add          r1, r4
    add          r0, r4
    mova         m0, [r0]
    mova         m1, [r0+r2]
    PAVGB        m0, [r1]
    PAVGB        m1, [r1+r2]
    add          r1, r4
    mova       [r0], m0
    mova    [r0+r2], m1
    add          r0, r4
    sub         r3d, 4
    jne .loop
    REP_RET
%endmacro

INIT_MMX 3dnow
AVG_PIXELS8


; void ff_avg_pixels8_x2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro AVG_PIXELS8_X2 0
%if cpuflag(sse2)
cglobal avg_pixels16_x2, 4,5,4
%else
cglobal avg_pixels8_x2, 4,5
%endif
    lea          r4, [r2*2]
%if notcpuflag(mmxext)
    pcmpeqd      m5, m5
    paddb        m5, m5
%endif
.loop:
    movu         m0, [r1]
    movu         m2, [r1+r2]
%if cpuflag(sse2)
    movu         m1, [r1+1]
    movu         m3, [r1+r2+1]
    pavgb        m0, m1
    pavgb        m2, m3
%else
    PAVGB        m0, [r1+1], m3, m5
    PAVGB        m2, [r1+r2+1], m4, m5
%endif
    PAVGB        m0, [r0], m3, m5
    PAVGB        m2, [r0+r2], m4, m5
    add          r1, r4
    mova       [r0], m0
    mova    [r0+r2], m2
    movu         m0, [r1]
    movu         m2, [r1+r2]
%if cpuflag(sse2)
    movu         m1, [r1+1]
    movu         m3, [r1+r2+1]
    pavgb        m0, m1
    pavgb        m2, m3
%else
    PAVGB        m0, [r1+1], m3, m5
    PAVGB        m2, [r1+r2+1], m4, m5
%endif
    add          r0, r4
    add          r1, r4
    PAVGB        m0, [r0], m3, m5
    PAVGB        m2, [r0+r2], m4, m5
    mova       [r0], m0
    mova    [r0+r2], m2
    add          r0, r4
    sub         r3d, 4
    jne .loop
    REP_RET
%endmacro

INIT_MMX mmx
AVG_PIXELS8_X2
INIT_MMX mmxext
AVG_PIXELS8_X2
INIT_MMX 3dnow
AVG_PIXELS8_X2
; actually avg_pixels16_x2
INIT_XMM sse2
AVG_PIXELS8_X2


; void ff_avg_pixels8_y2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro AVG_PIXELS8_Y2 0
%if cpuflag(sse2)
cglobal avg_pixels16_y2, 4,5,3
%else
cglobal avg_pixels8_y2, 4,5
%endif
    lea          r4, [r2*2]
    movu         m0, [r1]
    sub          r0, r2
.loop:
    movu         m1, [r1+r2]
    movu         m2, [r1+r4]
    add          r1, r4
    PAVGB        m0, m1
    PAVGB        m1, m2
    PAVGB        m0, [r0+r2]
    PAVGB        m1, [r0+r4]
    mova    [r0+r2], m0
    mova    [r0+r4], m1
    movu         m1, [r1+r2]
    movu         m0, [r1+r4]
    PAVGB        m2, m1
    PAVGB        m1, m0
    add          r0, r4
    add          r1, r4
    PAVGB        m2, [r0+r2]
    PAVGB        m1, [r0+r4]
    mova    [r0+r2], m2
    mova    [r0+r4], m1
    add          r0, r4
    sub         r3d, 4
    jne .loop
    REP_RET
%endmacro

INIT_MMX mmxext
AVG_PIXELS8_Y2
INIT_MMX 3dnow
AVG_PIXELS8_Y2
; actually avg_pixels16_y2
INIT_XMM sse2
AVG_PIXELS8_Y2


; void ff_avg_pixels8_xy2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
; Note this is not correctly rounded, and is therefore used for
; not-bitexact output
%macro AVG_APPROX_PIXELS8_XY2 0
cglobal avg_approx_pixels8_xy2, 4,5
    mova         m6, [pb_1]
    lea          r4, [r2*2]
    mova         m0, [r1]
    PAVGB        m0, [r1+1]
.loop:
    mova         m2, [r1+r4]
    mova         m1, [r1+r2]
    psubusb      m2, m6
    PAVGB        m1, [r1+r2+1]
    PAVGB        m2, [r1+r4+1]
    add          r1, r4
    PAVGB        m0, m1
    PAVGB        m1, m2
    PAVGB        m0, [r0]
    PAVGB        m1, [r0+r2]
    mova       [r0], m0
    mova    [r0+r2], m1
    mova         m1, [r1+r2]
    mova         m0, [r1+r4]
    PAVGB        m1, [r1+r2+1]
    PAVGB        m0, [r1+r4+1]
    add          r0, r4
    add          r1, r4
    PAVGB        m2, m1
    PAVGB        m1, m0
    PAVGB        m2, [r0]
    PAVGB        m1, [r0+r2]
    mova       [r0], m2
    mova    [r0+r2], m1
    add          r0, r4
    sub         r3d, 4
    jne .loop
    REP_RET
%endmacro

INIT_MMX mmxext
AVG_APPROX_PIXELS8_XY2
INIT_MMX 3dnow
AVG_APPROX_PIXELS8_XY2


; void ff_avg_pixels16_xy2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro SET_PIXELS_XY2 1
%if cpuflag(sse2)
cglobal %1_pixels16_xy2, 4,5,8
%else
cglobal %1_pixels8_xy2, 4,5
%endif
    pxor        m7, m7
    mova        m6, [pw_2]
    movu        m0, [r1]
    movu        m4, [r1+1]
    mova        m1, m0
    mova        m5, m4
    punpcklbw   m0, m7
    punpcklbw   m4, m7
    punpckhbw   m1, m7
    punpckhbw   m5, m7
    paddusw     m4, m0
    paddusw     m5, m1
    xor         r4, r4
    add         r1, r2
.loop:
    movu        m0, [r1+r4]
    movu        m2, [r1+r4+1]
    mova        m1, m0
    mova        m3, m2
    punpcklbw   m0, m7
    punpcklbw   m2, m7
    punpckhbw   m1, m7
    punpckhbw   m3, m7
    paddusw     m0, m2
    paddusw     m1, m3
    paddusw     m4, m6
    paddusw     m5, m6
    paddusw     m4, m0
    paddusw     m5, m1
    psrlw       m4, 2
    psrlw       m5, 2
%ifidn %1, avg
    mova        m3, [r0+r4]
    packuswb    m4, m5
    PAVGB       m4, m3
%else
    packuswb    m4, m5
%endif
    mova   [r0+r4], m4
    add         r4, r2

    movu        m2, [r1+r4]
    movu        m4, [r1+r4+1]
    mova        m3, m2
    mova        m5, m4
    punpcklbw   m2, m7
    punpcklbw   m4, m7
    punpckhbw   m3, m7
    punpckhbw   m5, m7
    paddusw     m4, m2
    paddusw     m5, m3
    paddusw     m0, m6
    paddusw     m1, m6
    paddusw     m0, m4
    paddusw     m1, m5
    psrlw       m0, 2
    psrlw       m1, 2
%ifidn %1, avg
    mova        m3, [r0+r4]
    packuswb    m0, m1
    PAVGB       m0, m3
%else
    packuswb    m0, m1
%endif
    mova   [r0+r4], m0
    add         r4, r2
    sub        r3d, 2
    jnz .loop
    REP_RET
%endmacro

INIT_MMX mmxext
SET_PIXELS_XY2 avg
INIT_MMX 3dnow
SET_PIXELS_XY2 avg
INIT_XMM sse2
SET_PIXELS_XY2 put
SET_PIXELS_XY2 avg

%macro SSSE3_PIXELS_XY2 1-2
%if %0 == 2 ; sse2
cglobal %1_pixels16_xy2, 4,5,%2
    mova        m4, [pb_interleave16]
%else
cglobal %1_pixels8_xy2, 4,5
    mova        m4, [pb_interleave8]
%endif
    mova        m5, [pb_1]
    movu        m0, [r1]
    movu        m1, [r1+1]
    pmaddubsw   m0, m5
    pmaddubsw   m1, m5
    xor         r4, r4
    add         r1, r2
.loop:
    movu        m2, [r1+r4]
    movu        m3, [r1+r4+1]
    pmaddubsw   m2, m5
    pmaddubsw   m3, m5
    paddusw     m0, m2
    paddusw     m1, m3
    pmulhrsw    m0, [pw_8192]
    pmulhrsw    m1, [pw_8192]
%ifidn %1, avg
    mova        m6, [r0+r4]
    packuswb    m0, m1
    pshufb      m0, m4
    pavgb       m0, m6
%else
    packuswb    m0, m1
    pshufb      m0, m4
%endif
    mova   [r0+r4], m0
    add         r4, r2

    movu        m0, [r1+r4]
    movu        m1, [r1+r4+1]
    pmaddubsw   m0, m5
    pmaddubsw   m1, m5
    paddusw     m2, m0
    paddusw     m3, m1
    pmulhrsw    m2, [pw_8192]
    pmulhrsw    m3, [pw_8192]
%ifidn %1, avg
    mova        m6, [r0+r4]
    packuswb    m2, m3
    pshufb      m2, m4
    pavgb       m2, m6
%else
    packuswb    m2, m3
    pshufb      m2, m4
%endif
    mova   [r0+r4], m2
    add         r4, r2
    sub        r3d, 2
    jnz .loop
    REP_RET
%endmacro

INIT_MMX ssse3
SSSE3_PIXELS_XY2 put
SSSE3_PIXELS_XY2 avg
INIT_XMM ssse3
SSSE3_PIXELS_XY2 put, 6
SSSE3_PIXELS_XY2 avg, 7
