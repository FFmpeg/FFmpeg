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

chr_to_mult:        times 4 dw 4663, 0
chr_to_offset:      times 4 dd -9289992
%define chr_to_shift 12

chr_from_mult:      times 4 dw 1799, 0
chr_from_offset:    times 4 dd 4081085
%define chr_from_shift 11

lum_to_mult:        times 4 dw 19077, 0
lum_to_offset:      times 4 dd -39057361
%define lum_to_shift 14

lum_from_mult:      times 4 dw 14071, 0
lum_from_offset:    times 4 dd 33561947
%define lum_from_shift 14

SECTION .text

; NOTE: there is no need to clamp the input when converting to jpeg range
;       (like we do in the C code) because packssdw will saturate the output.

;-----------------------------------------------------------------------------
; lumConvertRange
;
; void ff_lumRangeToJpeg_<opt>(int16_t *dst, int width);
; void ff_lumRangeFromJpeg_<opt>(int16_t *dst, int width);
;
;-----------------------------------------------------------------------------

%macro LUMCONVERTRANGE 4
cglobal %1, 2, 2, 5, dst, width
    shl          widthd, 1
    VBROADCASTI128   m2, [%2]
    VBROADCASTI128   m3, [%3]
    pxor             m4, m4
    add            dstq, widthq
    neg          widthq
.loop:
    movu             m0, [dstq+widthq]
    punpckhwd        m1, m0, m4
    punpcklwd        m0, m4
    pmaddwd          m0, m2
    pmaddwd          m1, m2
    paddd            m0, m3
    paddd            m1, m3
    psrad            m0, %4
    psrad            m1, %4
    packssdw         m0, m1
    movu  [dstq+widthq], m0
    add          widthq, mmsize
    jl .loop
    RET
%endmacro

;-----------------------------------------------------------------------------
; chrConvertRange
;
; void ff_chrRangeToJpeg_<opt>(int16_t *dstU, int16_t *dstV, int width);
; void ff_chrRangeFromJpeg_<opt>(int16_t *dstU, int16_t *dstV, int width);
;
;-----------------------------------------------------------------------------

%macro CHRCONVERTRANGE 4
cglobal %1, 3, 3, 7, dstU, dstV, width
    shl          widthd, 1
    VBROADCASTI128   m4, [%2]
    VBROADCASTI128   m5, [%3]
    pxor             m6, m6
    add           dstUq, widthq
    add           dstVq, widthq
    neg          widthq
.loop:
    movu             m0, [dstUq+widthq]
    movu             m2, [dstVq+widthq]
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
    psrad            m0, %4
    psrad            m1, %4
    psrad            m2, %4
    psrad            m3, %4
    packssdw         m0, m1
    packssdw         m2, m3
    movu [dstUq+widthq], m0
    movu [dstVq+widthq], m2
    add          widthq, mmsize
    jl .loop
    RET
%endmacro

INIT_XMM sse2
LUMCONVERTRANGE lumRangeToJpeg,   lum_to_mult,   lum_to_offset,   lum_to_shift
CHRCONVERTRANGE chrRangeToJpeg,   chr_to_mult,   chr_to_offset,   chr_to_shift
LUMCONVERTRANGE lumRangeFromJpeg, lum_from_mult, lum_from_offset, lum_from_shift
CHRCONVERTRANGE chrRangeFromJpeg, chr_from_mult, chr_from_offset, chr_from_shift

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
LUMCONVERTRANGE lumRangeToJpeg,   lum_to_mult,   lum_to_offset,   lum_to_shift
CHRCONVERTRANGE chrRangeToJpeg,   chr_to_mult,   chr_to_offset,   chr_to_shift
LUMCONVERTRANGE lumRangeFromJpeg, lum_from_mult, lum_from_offset, lum_from_shift
CHRCONVERTRANGE chrRangeFromJpeg, chr_from_mult, chr_from_offset, chr_from_shift
%endif
