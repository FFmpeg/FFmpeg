;*****************************************************************************
;* x86-optimized functions for interlace filter
;*
;* Copyright (C) 2014 Kieran Kunhya <kierank@obe.tv>
;* Copyright (c) 2014 Michael Niedermayer <michaelni@gmx.at>
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

SECTION_RODATA

SECTION .text

%macro LOWPASS_LINE 0
cglobal lowpass_line, 5, 5, 7
    add r0, r1
    add r2, r1
    add r3, r1
    add r4, r1
    neg r1

    pcmpeqb m6, m6

.loop
    mova m0, [r3+r1]
    mova m1, [r3+r1+mmsize]
    pavgb m0, [r4+r1]
    pavgb m1, [r4+r1+mmsize]
    pxor m0, m6
    pxor m1, m6
    pxor m2, m6, [r2+r1]
    pxor m3, m6, [r2+r1+mmsize]
    pavgb m0, m2
    pavgb m1, m3
    pxor m0, m6
    pxor m1, m6
    mova [r0+r1], m0
    mova [r0+r1+mmsize], m1

    add r1, 2*mmsize
    jl .loop
REP_RET
%endmacro

INIT_XMM sse2
LOWPASS_LINE

INIT_XMM avx
LOWPASS_LINE
