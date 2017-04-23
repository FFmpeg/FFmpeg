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
cglobal lowpass_line, 5, 5, 7, dst, h, src, mref, pref
    add dstq, hq
    add srcq, hq
    add mrefq, srcq
    add prefq, srcq
    neg hq

    pcmpeqb m6, m6

.loop:
    mova m0, [mrefq+hq]
    mova m1, [mrefq+hq+mmsize]
    pavgb m0, [prefq+hq]
    pavgb m1, [prefq+hq+mmsize]
    pxor m0, m6
    pxor m1, m6
    pxor m2, m6, [srcq+hq]
    pxor m3, m6, [srcq+hq+mmsize]
    pavgb m0, m2
    pavgb m1, m3
    pxor m0, m6
    pxor m1, m6
    mova [dstq+hq], m0
    mova [dstq+hq+mmsize], m1

    add hq, 2*mmsize
    jl .loop
REP_RET
%endmacro

INIT_XMM sse2
LOWPASS_LINE

INIT_XMM avx
LOWPASS_LINE
