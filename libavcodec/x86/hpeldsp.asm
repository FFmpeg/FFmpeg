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

cextern pb_1

cextern pw_8192

SECTION .text

; void ff_put_pixels8_x2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro PIXELS_X2 4
cglobal %1_pixels%2_x2, 4,5,4
    lea          r4, [r2*2]
.loop:
    mov%3        m0, [r1+1]
    mov%3        m1, [r1+r2+1]
%if %2 == mmsize && avx_enabled
    pavgb        m0, [r1]
    pavgb        m1, [r1+r2]
%else
    mov%3        m2, [r1]
    mov%3        m3, [r1+r2]
    pavgb        m0, m2
    pavgb        m1, m3
%endif
    add          r1, r4
%ifidn %1,avg
%if %2 == mmsize
    pavgb        m0, [r0]
    pavgb        m1, [r0+r2]
%else
    mov%4        m2, [r0]
    mov%4        m3, [r0+r2]
    pavgb        m0, m2
    pavgb        m1, m3
%endif
%endif
    mov%4      [r0], m0
    mov%4   [r0+r2], m1
    add          r0, r4
    mov%3        m0, [r1+1]
    mov%3        m1, [r1+r2+1]
%if %2 == mmsize && avx_enabled
    pavgb        m0, [r1]
    pavgb        m1, [r1+r2]
%else
    mov%3        m2, [r1]
    mov%3        m3, [r1+r2]
    pavgb        m0, m2
    pavgb        m1, m3
%endif
    add          r1, r4
%ifidn %1,avg
%if %2 == mmsize
    pavgb        m0, [r0]
    pavgb        m1, [r0+r2]
%else
    mov%4        m2, [r0]
    mov%4        m3, [r0+r2]
    pavgb        m0, m2
    pavgb        m1, m3
%endif
%endif
    mov%4      [r0], m0
    mov%4   [r0+r2], m1
    add          r0, r4
    sub         r3d, 4
    jne .loop
    RET
%endmacro

INIT_XMM sse2
PIXELS_X2 put,  8, q, q
PIXELS_X2 avg,  8, q, q

PIXELS_X2 put, 16, u, a
PIXELS_X2 avg, 16, u, a


; void ff_put_no_rnd_pixels8_x2_approx(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
INIT_XMM sse2
cglobal put_no_rnd_pixels8_x2_approx, 4,5,5
    mova         m4, [pb_1]
    lea          r4, [r2*2]
.loop:
    movq         m0, [r1]
    movq         m1, [r1+1]
    movhps       m0, [r1+r2]
    movhps       m1, [r1+r2+1]
    add          r1, r4
    psubusb      m0, m4
    pavgb        m0, m1
    movq       [r0], m0
    movhps  [r0+r2], m0
    movq         m0, [r1]
    movq         m1, [r1+1]
    movhps       m0, [r1+r2]
    movhps       m1, [r1+r2+1]
    add          r0, r4
    add          r1, r4
    psubusb      m0, m4
    pavgb        m0, m1
    movq       [r0], m0
    movhps  [r0+r2], m0
    add          r0, r4
    sub         r3d, 4
    jne .loop
    RET


%macro NO_RND_PIXELS_X2 4
cglobal %1_no_rnd_pixels%2_x2, 4,5,5
    lea          r4, [r2*3]
    pcmpeqb      m4, m4
.loop:
    mov%3        m0, [r1]
%if %2 == mmsize
    mov%3        m2, [r1+r2]
    mov%3        m1, [r1+1]
    mov%3        m3, [r1+r2+1]
%else
    movq         m1, [r1+1]
    movhps       m0, [r1+r2]
    movhps       m1, [r1+r2+1]
%endif
    pxor         m0, m4
%if %2 == mmsize
    pxor         m2, m4
%endif
    pxor         m1, m4
%if %2 == mmsize
    pxor         m3, m4
%endif
    pavgb        m0, m1
%if %2 == mmsize
    pavgb        m2, m3
%endif
    pxor         m0, m4
%if %2 == mmsize
    pxor         m2, m4
%endif
%ifidn %1, avg
    pavgb        m0, [r0]
    pavgb        m2, [r0+r2]
%endif
    mov%4      [r0], m0
%if %2 == mmsize
    mov%4   [r0+r2], m2
%else
    movhps  [r0+r2], m0
%endif
    mov%3        m0, [r1+2*r2]
%if %2 == mmsize
    mov%3        m2, [r1+r4]
    mov%3        m1, [r1+2*r2+1]
    mov%3        m3, [r1+r4+1]
%else
    movq         m1, [r1+2*r2+1]
    movhps       m0, [r1+r4]
    movhps       m1, [r1+r4+1]
%endif
    pxor         m0, m4
%if %2 == mmsize
    pxor         m2, m4
%endif
    pxor         m1, m4
%if %2 == mmsize
    pxor         m3, m4
%endif
    pavgb        m0, m1
%if %2 == mmsize
    pavgb        m2, m3
%endif
    pxor         m0, m4
%if %2 == mmsize
    pxor         m2, m4
%endif
%ifidn %1, avg
    pavgb        m0, [r0+r2*2]
    pavgb        m2, [r0+r4]
%endif
    mov%4 [r0+2*r2], m0
%if %2 == mmsize
    mov%4   [r0+r4], m2
%else
    movhps  [r0+r4], m0
%endif
    lea          r1, [r1+r2*4]
    lea          r0, [r0+r2*4]
    sub         r3d, 4
    jg .loop
    RET
%endmacro

INIT_XMM sse2
NO_RND_PIXELS_X2 put,  8, q, q

NO_RND_PIXELS_X2 avg, 16, u, a
NO_RND_PIXELS_X2 put, 16, u, a

; void ff_put_pixels8_y2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro PIXELS_Y2 4
cglobal %1_pixels%2_y2, 4,5,5
    mov%3        m0, [r1]
    lea          r4, [r2*2]
.loop:
    mov%3        m1, [r1+r2]
    mov%3        m2, [r1+r4]
    add          r1, r4
    pavgb        m0, m1
    pavgb        m1, m2
%ifidn %1,avg
%if %2 == mmsize
    pavgb        m0, [r0]
    pavgb        m1, [r0+r2]
%else
    mov%4        m3, [r0]
    mov%4        m4, [r0+r2]
    pavgb        m0, m3
    pavgb        m1, m4
%endif
%endif
    mov%4      [r0], m0
    mov%4   [r0+r2], m1
    mov%3        m1, [r1+r2]
    mov%3        m0, [r1+r4]
    add          r0, r4
    add          r1, r4
    pavgb        m2, m1
    pavgb        m1, m0
%ifidn %1,avg
%if %2 == mmsize
    pavgb        m2, [r0]
    pavgb        m1, [r0+r2]
%else
    mov%4        m3, [r0]
    mov%4        m4, [r0+r2]
    pavgb        m2, m3
    pavgb        m1, m4
%endif
%endif
    mov%4      [r0], m2
    mov%4   [r0+r2], m1
    add          r0, r4
    sub         r3d, 4
    jne .loop
    RET
%endmacro

INIT_XMM sse2
PIXELS_Y2 put,  8, q, q
PIXELS_Y2 avg,  8, q, q

PIXELS_Y2 put, 16, u, a
PIXELS_Y2 avg, 16, u, a


; void ff_put_no_rnd_pixels8_y2_approx(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
INIT_XMM sse2
cglobal put_no_rnd_pixels8_y2_approx, 4,5,4
    mova         m3, [pb_1]
    movq         m0, [r1]
    lea          r4, [r2+r2]
.loop:
    movq         m1, [r1+r2]
    movq         m2, [r1+r4]
    add          r1, r4
    psubusb      m1, m3
    pavgb        m0, m1
    pavgb        m1, m2
    movq       [r0], m0
    movq    [r0+r2], m1
    movq         m1, [r1+r2]
    movq         m0, [r1+r4]
    add          r0, r4
    add          r1, r4
    psubusb      m1, m3
    pavgb        m2, m1
    pavgb        m1, m0
    movq       [r0], m2
    movq    [r0+r2], m1
    add          r0, r4
    sub         r3d, 4
    jne .loop
    RET


%macro NO_RND_PIXELS_Y2 4
cglobal %1_no_rnd_pixels%2_y2, 4,5,4
    mov%3        m0, [r1]
    lea          r4, [r2*3]
    pcmpeqb      m3, m3
    add          r1, r2
    pxor         m0, m3
.loop:
    mov%3        m1, [r1]
    mov%3        m2, [r1+r2]
    pxor         m1, m3
    pxor         m2, m3
    pavgb        m0, m1
    pavgb        m1, m2
    pxor         m0, m3
    pxor         m1, m3
%ifidn %1, avg
    pavgb        m0, [r0]
    pavgb        m1, [r0+r2]
%endif
    mov%4      [r0], m0
    mov%4   [r0+r2], m1
    mov%3        m1, [r1+r2*2]
    mov%3        m0, [r1+r4]
    pxor         m1, m3
    pxor         m0, m3
    pavgb        m2, m1
    pavgb        m1, m0
    pxor         m2, m3
    pxor         m1, m3
%ifidn %1, avg
    pavgb        m2,[r0+r2*2]
    pavgb        m1,[r0+r4]
%endif
    mov%4 [r0+r2*2], m2
    mov%4   [r0+r4], m1
    lea          r1, [r1+r2*4]
    lea          r0, [r0+r2*4]
    sub         r3d, 4
    jg .loop
    RET
%endmacro

INIT_XMM sse2
NO_RND_PIXELS_Y2 put,  8, q, q

NO_RND_PIXELS_Y2 avg, 16, u, a
NO_RND_PIXELS_Y2 put, 16, u, a


; void ff_put_no_rnd_pixels8_xy2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro SET_PIXELS8_XY2 1-2
cglobal %1%2_pixels8_xy2, 4,5,5
    mova        m4, [pb_1]
%ifidn %2, _no_rnd
    pcmpeqw     m3, m3
%else
    mova        m3, [pw_8192]
%endif
    movh        m0, [r1]
    movh        m2, [r1+1]
    punpcklbw   m2, m0
    pmaddubsw   m2, m4
    xor         r4, r4
    add         r1, r2
.loop:
    movh        m0, [r1+r4]
    movh        m1, [r1+r4+1]
    punpcklbw   m0, m1
    pmaddubsw   m0, m4
%ifidn %2, _no_rnd
    psubw       m2, m3
    paddw       m2, m0
    psrlw       m2, 2
%else
    paddw       m2, m0
    pmulhrsw    m2, m3
%endif
%ifidn %1, avg
    movh        m1, [r0+r4]
    packuswb    m2, m2
    pavgb       m2, m1
%else
    packuswb    m2, m2
%endif
    movh   [r0+r4], m2
    add         r4, r2

    movh        m1, [r1+r4]
    movh        m2, [r1+r4+1]
    punpcklbw   m2, m1
    pmaddubsw   m2, m4
%ifidn %2, _no_rnd
    psubw       m0, m3
    paddw       m0, m2
    psrlw       m0, 2
%else
    paddw       m0, m2
    pmulhrsw    m0, m3
%endif
%ifidn %1, avg
    movh        m1, [r0+r4]
    packuswb    m0, m0
    pavgb       m0, m1
%else
    packuswb    m0, m0
%endif
    movh   [r0+r4], m0
    add         r4, r2
    sub        r3d, 2
    jnz .loop
    RET
%endmacro

INIT_XMM ssse3
SET_PIXELS8_XY2 put, _no_rnd
SET_PIXELS8_XY2 avg
SET_PIXELS8_XY2 put


; void ff_avg_pixels16_xy2(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
%macro SET_PIXELS_XY2 1-2
cglobal %1%2_pixels16_xy2, 4,5,8
    pxor        m7, m7
    movu        m0, [r1]
    movu        m4, [r1+1]
    pcmpeqw     m6, m6
%ifnidn %2, _no_rnd
    paddw       m6, m6
%endif
    mova        m1, m0
    mova        m5, m4
    punpcklbw   m0, m7
    punpcklbw   m4, m7
    punpckhbw   m1, m7
    punpckhbw   m5, m7
    paddw       m4, m0
    paddw       m5, m1
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
    paddw       m0, m2
    paddw       m1, m3
    psubw       m4, m6
    psubw       m5, m6
    paddw       m4, m0
    paddw       m5, m1
    psrlw       m4, 2
    psrlw       m5, 2
%ifidn %1, avg
    mova        m3, [r0+r4]
    packuswb    m4, m5
    pavgb       m4, m3
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
    paddw       m4, m2
    paddw       m5, m3
    psubw       m0, m6
    psubw       m1, m6
    paddw       m0, m4
    paddw       m1, m5
    psrlw       m0, 2
    psrlw       m1, 2
%ifidn %1, avg
    mova        m3, [r0+r4]
    packuswb    m0, m1
    pavgb       m0, m3
%else
    packuswb    m0, m1
%endif
    mova   [r0+r4], m0
    add         r4, r2
    sub        r3d, 2
    jnz .loop
    RET
%endmacro

INIT_XMM sse2
SET_PIXELS_XY2 put
SET_PIXELS_XY2 avg
SET_PIXELS_XY2 put, _no_rnd
SET_PIXELS_XY2 avg, _no_rnd

%macro SSSE3_PIXELS_XY2 1-2
cglobal %1_pixels16_xy2, 4,5,%2
    movu        m1, [r1+1]
    movu        m0, [r1]
    mova        m5, [pb_1]
    mova        m4, [pw_8192]
    pmaddubsw   m1, m5
    pmaddubsw   m0, m5
    xor         r4, r4
    add         r1, r2
.loop:
    movu        m3, [r1+r4+1]
    movu        m2, [r1+r4]
    pmaddubsw   m3, m5
    pmaddubsw   m2, m5
%ifidn %1, avg
    mova        m6, [r0+r4]
%endif
    paddw       m1, m3
    paddw       m0, m2
    pmulhrsw    m1, m4
    pmulhrsw    m0, m4
    pslldq      m1, 1
    por         m0, m1
%ifidn %1, avg
    pavgb       m0, m6
%endif
    mova   [r0+r4], m0
    add         r4, r2

    movu        m1, [r1+r4+1]
    movu        m0, [r1+r4]
    pmaddubsw   m1, m5
    pmaddubsw   m0, m5
%ifidn %1, avg
    mova        m6, [r0+r4]
%endif
    paddw       m3, m1
    paddw       m2, m0
    pmulhrsw    m3, m4
    pmulhrsw    m2, m4
    pslldq      m3, 1
    por         m2, m3
%ifidn %1, avg
    pavgb       m2, m6
%endif
    mova   [r0+r4], m2
    add         r4, r2
    sub        r3d, 2
    jnz .loop
    RET
%endmacro

INIT_XMM ssse3
SSSE3_PIXELS_XY2 put, 6
SSSE3_PIXELS_XY2 avg, 7
