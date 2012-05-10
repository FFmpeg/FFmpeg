;******************************************************************************
;* x86 optimized Format Conversion Utils
;* Copyright (c) 2008 Loren Merritt
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

SECTION_TEXT

;-----------------------------------------------------------------------------
; void ff_conv_fltp_to_flt_6ch(float *dst, float *const *src, int len,
;                              int channels);
;-----------------------------------------------------------------------------

%macro CONV_FLTP_TO_FLT_6CH 0
cglobal conv_fltp_to_flt_6ch, 2,8,7, dst, src, src1, src2, src3, src4, src5, len
%if ARCH_X86_64
    mov     lend, r2d
%else
    %define lend dword r2m
%endif
    mov    src1q, [srcq+1*gprsize]
    mov    src2q, [srcq+2*gprsize]
    mov    src3q, [srcq+3*gprsize]
    mov    src4q, [srcq+4*gprsize]
    mov    src5q, [srcq+5*gprsize]
    mov     srcq, [srcq]
    sub    src1q, srcq
    sub    src2q, srcq
    sub    src3q, srcq
    sub    src4q, srcq
    sub    src5q, srcq
.loop:
    mova      m0, [srcq      ]
    mova      m1, [srcq+src1q]
    mova      m2, [srcq+src2q]
    mova      m3, [srcq+src3q]
    mova      m4, [srcq+src4q]
    mova      m5, [srcq+src5q]
%if cpuflag(sse4)
    SBUTTERFLYPS 0, 1, 6
    SBUTTERFLYPS 2, 3, 6
    SBUTTERFLYPS 4, 5, 6

    blendps   m6, m4, m0, 1100b
    movlhps   m0, m2
    movhlps   m4, m2
    blendps   m2, m5, m1, 1100b
    movlhps   m1, m3
    movhlps   m5, m3

    movaps [dstq   ], m0
    movaps [dstq+16], m6
    movaps [dstq+32], m4
    movaps [dstq+48], m1
    movaps [dstq+64], m2
    movaps [dstq+80], m5
%else ; mmx
    SBUTTERFLY dq, 0, 1, 6
    SBUTTERFLY dq, 2, 3, 6
    SBUTTERFLY dq, 4, 5, 6

    movq   [dstq   ], m0
    movq   [dstq+ 8], m2
    movq   [dstq+16], m4
    movq   [dstq+24], m1
    movq   [dstq+32], m3
    movq   [dstq+40], m5
%endif
    add      srcq, mmsize
    add      dstq, mmsize*6
    sub      lend, mmsize/4
    jg .loop
%if mmsize == 8
    emms
    RET
%else
    REP_RET
%endif
%endmacro

INIT_MMX mmx
CONV_FLTP_TO_FLT_6CH
INIT_XMM sse4
CONV_FLTP_TO_FLT_6CH
%if HAVE_AVX
INIT_XMM avx
CONV_FLTP_TO_FLT_6CH
%endif
