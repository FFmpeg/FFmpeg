;******************************************************************************
;* FLAC DSP SIMD optimizations
;*
;* Copyright (C) 2014 Loren Merritt
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

%macro LPC_32 1
INIT_XMM %1
cglobal flac_lpc_32, 5,6,5, decoded, coeffs, pred_order, qlevel, len, j
    sub    lend, pred_orderd
    jle .ret
    lea    decodedq, [decodedq+pred_orderq*4-8]
    lea    coeffsq, [coeffsq+pred_orderq*4]
    neg    pred_orderq
    movd   m4, qlevelm
ALIGN 16
.loop_sample:
    movd   m0, [decodedq+pred_orderq*4+8]
    add    decodedq, 8
    movd   m1, [coeffsq+pred_orderq*4]
    pxor   m2, m2
    pxor   m3, m3
    lea    jq, [pred_orderq+1]
    test   jq, jq
    jz .end_order
.loop_order:
    PMACSDQL m2, m0, m1, m2, m0
    movd   m0, [decodedq+jq*4]
    PMACSDQL m3, m1, m0, m3, m1
    movd   m1, [coeffsq+jq*4]
    inc    jq
    jl .loop_order
.end_order:
    PMACSDQL m2, m0, m1, m2, m0
    psrlq  m2, m4
    movd   m0, [decodedq]
    paddd  m0, m2
    movd   [decodedq], m0
    sub  lend, 2
    jl .ret
    PMACSDQL m3, m1, m0, m3, m1
    psrlq  m3, m4
    movd   m1, [decodedq+4]
    paddd  m1, m3
    movd   [decodedq+4], m1
    jg .loop_sample
.ret:
    REP_RET
%endmacro

%if HAVE_XOP_EXTERNAL
LPC_32 xop
%endif
LPC_32 sse4
