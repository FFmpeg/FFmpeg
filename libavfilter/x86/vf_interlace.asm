;*****************************************************************************
;* x86-optimized functions for interlace filter
;*
;* Copyright (C) 2014 Kieran Kunhya <kierank@obe.tv>
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

    pxor m6, m6

.loop
    mova m0, [r2+r1]
    punpcklbw m1, m0, m6
    punpckhbw m0, m6
    paddw m0, m0
    paddw m1, m1

    mova m2, [r3+r1]
    punpcklbw m3, m2, m6
    punpckhbw m2, m6

    mova m4, [r4+r1]
    punpcklbw m5, m4, m6
    punpckhbw m4, m6

    paddw m1, m3
    pavgw m1, m5

    paddw m0, m2
    pavgw m0, m4

    psrlw m0, 1
    psrlw m1, 1

    packuswb m1, m0
    mova [r0+r1], m1

    add r1, mmsize
    jl .loop
REP_RET
%endmacro

INIT_XMM sse2
LOWPASS_LINE

INIT_XMM avx
LOWPASS_LINE
