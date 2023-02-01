;*****************************************************************************
;* x86-optimized functions for interlace filter
;*
;* Copyright (C) 2014 Kieran Kunhya <kierank@obe.tv>
;* Copyright (c) 2014 Michael Niedermayer <michaelni@gmx.at>
;* Copyright (c) 2017 Thomas Mundt <tmundt75@gmail.com>
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

pw_4: times 8 dw 4

SECTION .text

%macro LOWPASS 1
    add dstq, hq
    add srcq, hq
    add mrefq, srcq
    add prefq, srcq
    neg hq

    pcmpeq%1 m6, m6

    test hq, mmsize
    je .loop

    ;process 1 * mmsize
    movu m0, [mrefq+hq]
    pavg%1 m0, [prefq+hq]
    pxor m0, m6
    pxor m2, m6, [srcq+hq]
    pavg%1 m0, m2
    pxor m0, m6
    movu [dstq+hq], m0
    add hq, mmsize
    jge .end

.loop:
    movu m0, [mrefq+hq]
    movu m1, [mrefq+hq+mmsize]
    pavg%1 m0, [prefq+hq]
    pavg%1 m1, [prefq+hq+mmsize]
    pxor m0, m6
    pxor m1, m6
    pxor m2, m6, [srcq+hq]
    pxor m3, m6, [srcq+hq+mmsize]
    pavg%1 m0, m2
    pavg%1 m1, m3
    pxor m0, m6
    pxor m1, m6
    movu [dstq+hq], m0
    movu [dstq+hq+mmsize], m1

    add hq, 2*mmsize
    jl .loop

.end:
    RET
%endmacro

%macro LOWPASS_LINE 0
cglobal lowpass_line, 5, 5, 7, dst, h, src, mref, pref
    LOWPASS b

cglobal lowpass_line_16, 5, 5, 7, dst, h, src, mref, pref
    shl hq, 1
    LOWPASS w
%endmacro

%macro LOWPASS_LINE_COMPLEX 0
cglobal lowpass_line_complex, 5, 5, 8, dst, h, src, mref, pref
    pxor m7, m7
.loop:
    movu m0, [srcq+mrefq]
    movu m2, [srcq+prefq]
    mova m1, m0
    mova m3, m2
    punpcklbw m0, m7
    punpcklbw m2, m7
    punpckhbw m1, m7
    punpckhbw m3, m7
    paddw m0, m2
    paddw m1, m3
    mova m6, m0
    mova m5, m1
    movu m2, [srcq]
    mova m3, m2
    punpcklbw m2, m7
    punpckhbw m3, m7
    paddw m0, m2
    paddw m1, m3
    psllw m2, 1
    psllw m3, 1
    paddw m0, m2
    paddw m1, m3
    psllw m0, 1
    psllw m1, 1
    pcmpgtw m6, m2
    pcmpgtw m5, m3
    packsswb m6, m5
    movu m2, [srcq+mrefq*2]
    movu m4, [srcq+prefq*2]
    mova m3, m2
    mova m5, m4
    punpcklbw m2, m7
    punpcklbw m4, m7
    punpckhbw m3, m7
    punpckhbw m5, m7
    paddw m2, m4
    paddw m3, m5
    paddw m0, [pw_4]
    paddw m1, [pw_4]
    psubusw m0, m2
    psubusw m1, m3
    psrlw m0, 3
    psrlw m1, 3
    packuswb m0, m1
    mova m1, m0
    movu m2, [srcq]
    pmaxub m0, m2
    pminub m1, m2
    pand m0, m6
    pandn m6, m1
    por m0, m6
    movu [dstq], m0

    add dstq, mmsize
    add srcq, mmsize
    sub hd, mmsize
    jg .loop
RET

cglobal lowpass_line_complex_12, 5, 5, 8, 16, dst, h, src, mref, pref, clip_max
    movd m7, DWORD clip_maxm
    SPLATW m7, m7, 0
    movu [rsp], m7
.loop:
    movu m0, [srcq+mrefq]
    movu m1, [srcq+mrefq+mmsize]
    movu m2, [srcq+prefq]
    movu m3, [srcq+prefq+mmsize]
    paddw m0, m2
    paddw m1, m3
    mova m6, m0
    mova m7, m1
    movu m2, [srcq]
    movu m3, [srcq+mmsize]
    paddw m0, m2
    paddw m1, m3
    psllw m2, 1
    psllw m3, 1
    paddw m0, m2
    paddw m1, m3
    psllw m0, 1
    psllw m1, 1
    pcmpgtw m6, m2
    pcmpgtw m7, m3
    movu m2, [srcq+2*mrefq]
    movu m3, [srcq+2*mrefq+mmsize]
    movu m4, [srcq+2*prefq]
    movu m5, [srcq+2*prefq+mmsize]
    paddw m2, m4
    paddw m3, m5
    paddw m0, [pw_4]
    paddw m1, [pw_4]
    psubusw m0, m2
    psubusw m1, m3
    psrlw m0, 3
    psrlw m1, 3
    pminsw m0, [rsp]
    pminsw m1, [rsp]
    mova m2, m0
    mova m3, m1
    movu m4, [srcq]
    pmaxsw m0, m4
    pminsw m2, m4
    movu m4, [srcq + mmsize]
    pmaxsw m1, m4
    pminsw m3, m4
    pand m0, m6
    pand m1, m7
    pandn m6, m2
    pandn m7, m3
    por m0, m6
    por m1, m7
    movu [dstq], m0
    movu [dstq+mmsize], m1

    add dstq, 2*mmsize
    add srcq, 2*mmsize
    sub hd, mmsize
    jg .loop
RET
%endmacro

INIT_XMM sse2
LOWPASS_LINE

INIT_XMM avx
LOWPASS_LINE

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
LOWPASS_LINE
%endif

INIT_XMM sse2
LOWPASS_LINE_COMPLEX
