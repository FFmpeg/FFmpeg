;******************************************************************************
;* x86 optimized Format Conversion Utils
;* Copyright (c) 2008 Loren Merritt
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

;------------------------------------------------------------------------------
; void ff_int32_to_float_fmul_scalar(float *dst, const int32_t *src, float mul,
;                                    int len);
;------------------------------------------------------------------------------
%macro INT32_TO_FLOAT_FMUL_SCALAR 1
%if UNIX64
cglobal int32_to_float_fmul_scalar, 3, 3, %1, dst, src, len
%else
cglobal int32_to_float_fmul_scalar, 4, 4, %1, dst, src, mul, len
%endif
%if WIN64
    SWAP 0, 2
%elif ARCH_X86_32
    movss   m0, mulm
%endif
    SPLATD  m0
    shl     lend, 2
    add     srcq, lenq
    add     dstq, lenq
    neg     lenq
.loop:
%if cpuflag(sse2)
    cvtdq2ps  m1, [srcq+lenq   ]
    cvtdq2ps  m2, [srcq+lenq+16]
%else
    cvtpi2ps  m1, [srcq+lenq   ]
    cvtpi2ps  m3, [srcq+lenq+ 8]
    cvtpi2ps  m2, [srcq+lenq+16]
    cvtpi2ps  m4, [srcq+lenq+24]
    movlhps   m1, m3
    movlhps   m2, m4
%endif
    mulps     m1, m0
    mulps     m2, m0
    mova  [dstq+lenq   ], m1
    mova  [dstq+lenq+16], m2
    add     lenq, 32
    jl .loop
%if notcpuflag(sse2)
    ;; cvtpi2ps switches to MMX even if the source is a memory location
    ;; possible an error in documentation since every tested CPU disagrees with
    ;; that. Use emms anyway since the vast majority of machines will use the
    ;; SSE2 variant
    emms
%endif
    RET
%endmacro

INIT_XMM sse
INT32_TO_FLOAT_FMUL_SCALAR 5
INIT_XMM sse2
INT32_TO_FLOAT_FMUL_SCALAR 3

;------------------------------------------------------------------------------
; void ff_int32_to_float_fmul_array8(FmtConvertContext *c, float *dst, const int32_t *src,
;                                    const float *mul, int len);
;------------------------------------------------------------------------------
%macro INT32_TO_FLOAT_FMUL_ARRAY8 0
cglobal int32_to_float_fmul_array8, 5, 5, 5, c, dst, src, mul, len
    shl     lend, 2
    add     srcq, lenq
    add     dstq, lenq
    neg     lenq
.loop:
    movss     m0, [mulq]
    SPLATD    m0
%if cpuflag(sse2)
    cvtdq2ps  m1, [srcq+lenq   ]
    cvtdq2ps  m2, [srcq+lenq+16]
%else
    cvtpi2ps  m1, [srcq+lenq   ]
    cvtpi2ps  m3, [srcq+lenq+ 8]
    cvtpi2ps  m2, [srcq+lenq+16]
    cvtpi2ps  m4, [srcq+lenq+24]
    movlhps   m1, m3
    movlhps   m2, m4
%endif
    mulps     m1, m0
    mulps     m2, m0
    mova  [dstq+lenq   ], m1
    mova  [dstq+lenq+16], m2
    add     mulq, 4
    add     lenq, 32
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse
INT32_TO_FLOAT_FMUL_ARRAY8
INIT_XMM sse2
INT32_TO_FLOAT_FMUL_ARRAY8

