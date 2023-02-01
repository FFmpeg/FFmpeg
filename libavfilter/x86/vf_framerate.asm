;*****************************************************************************
;* x86-optimized functions for framerate filter
;*
;* Copyright (C) 2018 Marton Balint
;*
;* Based on vf_blend.asm, Copyright (C) 2015 Paul B Mahol
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


%macro XSPLAT 3
%if cpuflag(avx2)
    vpbroadcast%3  %1, %2
%else
    movd           %1, %2
%ifidn %3, d
    SPLATD         %1
%else
    SPLATW         %1, %1
%endif
%endif
%endmacro


%macro BLEND_INIT 0-1
%if ARCH_X86_64
cglobal blend_frames%1, 6, 9, 5, src1, src1_linesize, src2, src2_linesize, dst, dst_linesize, width, end, x
    mov    widthd, dword widthm
%else
cglobal blend_frames%1, 5, 7, 5, src1, src1_linesize, src2, src2_linesize, dst, end, x
%define dst_linesizeq r5mp
%define widthq r6mp
%endif
    mov      endd, dword r7m
    add     src1q, widthq
    add     src2q, widthq
    add      dstq, widthq
    neg    widthq
%endmacro


%macro BLEND_LOOP 4
.nextrow:
    mov        xq, widthq

    .loop:
        movu            m0, [src1q + xq]
        movu            m1, [src2q + xq]
        SBUTTERFLY    %1%2, 0, 1, 4         ; aAbBcCdD
                                            ; eEfFgGhH
        pmadd%3         m0, m2
        pmadd%3         m1, m2

        padd%2          m0, m3
        padd%2          m1, m3
        psrl%2          m0, %4              ; 0A0B0C0D
        psrl%2          m1, %4              ; 0E0F0G0H

        packus%2%1      m0, m1              ; ABCDEFGH
        movu   [dstq + xq], m0
        add             xq, mmsize
    jl .loop
    add     src1q, src1_linesizeq
    add     src2q, src2_linesizeq
    add      dstq, dst_linesizeq
    sub      endd, 1
    jg .nextrow
RET
%endmacro


%macro BLEND_FRAMES 0
    BLEND_INIT

    XSPLAT     m2, r8m, w                   ; factor1
    XSPLAT     m3, r9m, w                   ; factor2

    psllw      m3, 8
    por        m2, m3                       ; interleaved factors

    XSPLAT     m3, r10m, w                  ; half

    BLEND_LOOP  b, w, ubsw, 7
%endmacro


%macro BLEND_FRAMES16 0
    BLEND_INIT 16

    XSPLAT     m2, r8m, d                   ; factor1
    XSPLAT     m3, r9m, d                   ; factor2

    pslld      m3, 16
    por        m2, m3                       ; interleaved factors

    XSPLAT     m3, r10m, d                  ; half

    BLEND_LOOP  w, d, wd, 15
%endmacro


INIT_XMM ssse3
BLEND_FRAMES

INIT_XMM sse4
BLEND_FRAMES16


%if HAVE_AVX2_EXTERNAL

INIT_YMM avx2
BLEND_FRAMES
BLEND_FRAMES16

%endif
