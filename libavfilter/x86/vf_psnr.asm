;*****************************************************************************
;* x86-optimized functions for psnr filter
;*
;* Copyright (C) 2015 Ronald S. Bultje <rsbultje@gmail.com>
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

SECTION .text

%macro SSE_LINE_FN 2 ; 8 or 16, byte or word
INIT_XMM sse2
%if ARCH_X86_32
%if %1 == 8
cglobal sse_line_%1 %+ bit, 0, 6, 8, res, buf, w, px1, px2, ref
%else
cglobal sse_line_%1 %+ bit, 0, 7, 8, res, buf, reshigh, w, px1, px2, ref
%endif
    mov       bufq, r0mp
    mov       refq, r1mp
    mov         wd, r2m
%else
cglobal sse_line_%1 %+ bit, 3, 5, 8, buf, ref, w, px1, px2
%endif
    pxor        m6, m6
    pxor        m7, m7
    sub         wd, mmsize*2
    jl .end

.loop:
    movu        m0, [bufq+mmsize*0]
    movu        m1, [bufq+mmsize*1]
    movu        m2, [refq+mmsize*0]
    movu        m3, [refq+mmsize*1]
%if %1 == 8
    add       bufq, mmsize*2
    add       refq, mmsize*2
    psubusb     m4, m0, m2
    psubusb     m5, m1, m3
    psubusb     m2, m0
    psubusb     m3, m1
    por         m2, m4
    por         m3, m5
    punpcklbw   m0, m2, m6
    punpcklbw   m1, m3, m6
    punpckhbw   m2, m6
    punpckhbw   m3, m6
%else
    psubw       m0, m2
    psubw       m1, m3
    movu        m2, [bufq+mmsize*2]
    movu        m3, [bufq+mmsize*3]
    movu        m4, [refq+mmsize*2]
    movu        m5, [refq+mmsize*3]
    psubw       m2, m4
    psubw       m3, m5
    add       bufq, mmsize*4
    add       refq, mmsize*4
%endif
    pmaddwd     m0, m0
    pmaddwd     m1, m1
    pmaddwd     m2, m2
    pmaddwd     m3, m3
    paddd       m0, m1
    paddd       m2, m3
%if %1 == 8
    paddd       m7, m0
    paddd       m7, m2
%else
    paddd       m0, m2
    punpckldq   m2, m0, m6
    punpckhdq   m0, m6
    paddq       m7, m0
    paddq       m7, m2
%endif
    sub         wd, mmsize*2
    jge .loop

.end:
    add         wd, mmsize*2
    movhlps     m0, m7
%if %1 == 8
    paddd       m7, m0
    pshufd      m0, m7, 1
    paddd       m7, m0
    movd       eax, m7
%else
    paddq       m7, m0
%if ARCH_X86_32
    movd       eax, m7
    psrldq      m7, 4
    movd       edx, m7
%else
    movq       rax, m7
%endif
%endif

    ; deal with cases where w % 32 != 0
    test        wd, wd
    jz .end_scalar
.loop_scalar:
    movzx     px1d, %2 [bufq+wq*(%1/8)-(%1/8)]
    movzx     px2d, %2 [refq+wq*(%1/8)-(%1/8)]
    sub       px1d, px2d
    imul      px1d, px1d
%if %1 == 8
    add        eax, px1d
%elif ARCH_X86_64
    add        rax, px1q
%else
    add        eax, px1d
    adc        edx, 0
%endif
    dec         wd
    jg .loop_scalar

.end_scalar:
    ; for %1=8, no need to zero edx on x86-32, since edx=wd, which is zero
    RET
%endmacro

INIT_XMM sse2
SSE_LINE_FN  8, byte
SSE_LINE_FN 16, word
