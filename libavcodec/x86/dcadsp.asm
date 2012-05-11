;******************************************************************************
;* SSE-optimized functions for the DCA decoder
;* Copyright (C) 2012-2014 Christophe Gisquet <christophe.gisquet@gmail.com>
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

%include "libavutil/x86/x86util.asm"

SECTION_RODATA
pf_inv16:  times 4 dd 0x3D800000 ; 1/16

SECTION_TEXT

; void int8x8_fmul_int32_sse2(float *dst, const int8_t *src, int scale)
%macro INT8X8_FMUL_INT32 0
cglobal int8x8_fmul_int32, 3,3,5, dst, src, scale
    cvtsi2ss    m0, scalem
    mulss       m0, [pf_inv16]
    shufps      m0, m0, 0
%if cpuflag(sse2)
%if cpuflag(sse4)
    pmovsxbd    m1, [srcq+0]
    pmovsxbd    m2, [srcq+4]
%else
    movq        m1, [srcq]
    punpcklbw   m1, m1
    mova        m2, m1
    punpcklwd   m1, m1
    punpckhwd   m2, m2
    psrad       m1, 24
    psrad       m2, 24
%endif
    cvtdq2ps    m1, m1
    cvtdq2ps    m2, m2
%else
    movd       mm0, [srcq+0]
    movd       mm1, [srcq+4]
    punpcklbw  mm0, mm0
    punpcklbw  mm1, mm1
    movq       mm2, mm0
    movq       mm3, mm1
    punpcklwd  mm0, mm0
    punpcklwd  mm1, mm1
    punpckhwd  mm2, mm2
    punpckhwd  mm3, mm3
    psrad      mm0, 24
    psrad      mm1, 24
    psrad      mm2, 24
    psrad      mm3, 24
    cvtpi2ps    m1, mm0
    cvtpi2ps    m2, mm1
    cvtpi2ps    m3, mm2
    cvtpi2ps    m4, mm3
    shufps      m0, m0, 0
    emms
    shufps      m1, m3, q1010
    shufps      m2, m4, q1010
%endif
    mulps       m1, m0
    mulps       m2, m0
    mova [dstq+ 0], m1
    mova [dstq+16], m2
    REP_RET
%endmacro

%if ARCH_X86_32
INIT_XMM sse
INT8X8_FMUL_INT32
%endif

INIT_XMM sse2
INT8X8_FMUL_INT32

INIT_XMM sse4
INT8X8_FMUL_INT32
