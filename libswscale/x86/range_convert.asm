;******************************************************************************
;* Copyright (c) 2024 Ramiro Polla
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
pack19:             times 4 dd (1 << 19) - 1

SECTION .text

;-----------------------------------------------------------------------------
; lumConvertRange
;
; void ff_lumRangeToJpeg{8,16}_<opt>(int16_t *dst, int width,
;                                    uint32_t coeff, int64_t offset);
; void ff_lumRangeFromJpeg{8,16}_<opt>(int16_t *dst, int width,
;                                      uint32_t coeff, int64_t offset);
;
;-----------------------------------------------------------------------------

%macro LUMCONVERTRANGE 2
cglobal lumRange%1Jpeg%2, 4, 4, 5, dst, width, coeff, offset
    shl          widthd, %2 >> 3
    movd            xm2, coeffd
    VBROADCASTSS     m2, xm2
%if ARCH_X86_64
    movq            xm3, offsetq
%else
    movq            xm3, offsetm
%endif
%if %2 == 16
    VBROADCASTSD     m3, xm3
%ifidni %1,To
    VBROADCASTI128   m4, [pack19]
%endif
%elif %2 == 8
    VBROADCASTSS     m3, xm3
    pxor             m4, m4
%endif ; %2 == 8/16
    add            dstq, widthq
    neg          widthq
.loop:
    movu             m0, [dstq+widthq]
%if %2 == 16
    pshufd           m1, m0, 0xb1
    pmuldq           m0, m2
    pmuldq           m1, m2
    paddq            m0, m3
    paddq            m1, m3
    psrlq            m0, 18
    psrlq            m1, 18
    pshufd           m0, m0, 0xd8
    pshufd           m1, m1, 0xd8
    punpckldq        m0, m1
%ifidni %1,To
    PMINSD           m0, m4, m1
%endif
%elif %2 == 8
    punpckhwd        m1, m0, m4
    punpcklwd        m0, m4
    pmaddwd          m0, m2
    pmaddwd          m1, m2
    paddd            m0, m3
    paddd            m1, m3
    psrad            m0, 14
    psrad            m1, 14
    packssdw         m0, m1
%endif ; %2 == 8/16
    movu  [dstq+widthq], m0
    add          widthq, mmsize
    jl .loop
    RET
%endmacro

;-----------------------------------------------------------------------------
; chrConvertRange
;
; void ff_chrRangeToJpeg{8,16}_<opt>(int16_t *dstU, int16_t *dstV, int width,
;                                    uint32_t coeff, int64_t offset);
; void ff_chrRangeFromJpeg{8,16}_<opt>(int16_t *dstU, int16_t *dstV, int width,
;                                      uint32_t coeff, int64_t offset);
;
;-----------------------------------------------------------------------------

%macro CHRCONVERTRANGE 2
cglobal chrRange%1Jpeg%2, 5, 5, 7, dstU, dstV, width, coeff, offset
    shl          widthd, %2 >> 3
    movd            xm4, coeffd
    VBROADCASTSS     m4, xm4
%if ARCH_X86_64
    movq            xm5, offsetq
%else
    movq            xm5, offsetm
%endif
%if %2 == 16
    VBROADCASTSD     m5, xm5
%ifidni %1,To
    VBROADCASTI128   m6, [pack19]
%endif
%elif %2 == 8
    VBROADCASTSS     m5, xm5
    pxor             m6, m6
%endif ; %2 == 8/16
    add           dstUq, widthq
    add           dstVq, widthq
    neg          widthq
.loop:
    movu             m0, [dstUq+widthq]
    movu             m2, [dstVq+widthq]
%if %2 == 16
    pshufd           m1, m0, 0xb1
    pshufd           m3, m2, 0xb1
    pmuldq           m0, m4
    pmuldq           m1, m4
    pmuldq           m2, m4
    pmuldq           m3, m4
    paddq            m0, m5
    paddq            m1, m5
    paddq            m2, m5
    paddq            m3, m5
    psrlq            m0, 18
    psrlq            m1, 18
    psrlq            m2, 18
    psrlq            m3, 18
    pshufd           m0, m0, 0xd8
    pshufd           m1, m1, 0xd8
    pshufd           m2, m2, 0xd8
    pshufd           m3, m3, 0xd8
    punpckldq        m0, m1
    punpckldq        m2, m3
%ifidni %1,To
    PMINSD           m0, m6, m1
    PMINSD           m2, m6, m3
%endif
%elif %2 == 8
    punpckhwd        m1, m0, m6
    punpckhwd        m3, m2, m6
    punpcklwd        m0, m6
    punpcklwd        m2, m6
    pmaddwd          m0, m4
    pmaddwd          m1, m4
    pmaddwd          m2, m4
    pmaddwd          m3, m4
    paddd            m0, m5
    paddd            m1, m5
    paddd            m2, m5
    paddd            m3, m5
    psrad            m0, 14
    psrad            m1, 14
    psrad            m2, 14
    psrad            m3, 14
    packssdw         m0, m1
    packssdw         m2, m3
%endif ; %2 == 8/16
    movu [dstUq+widthq], m0
    movu [dstVq+widthq], m2
    add          widthq, mmsize
    jl .loop
    RET
%endmacro

INIT_XMM sse2
LUMCONVERTRANGE To,    8
CHRCONVERTRANGE To,    8
LUMCONVERTRANGE From,  8
CHRCONVERTRANGE From,  8

INIT_XMM sse4
LUMCONVERTRANGE To,   16
CHRCONVERTRANGE To,   16
LUMCONVERTRANGE From, 16
CHRCONVERTRANGE From, 16

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
LUMCONVERTRANGE To,    8
LUMCONVERTRANGE To,   16
CHRCONVERTRANGE To,    8
CHRCONVERTRANGE To,   16
LUMCONVERTRANGE From,  8
LUMCONVERTRANGE From, 16
CHRCONVERTRANGE From,  8
CHRCONVERTRANGE From, 16
%endif
