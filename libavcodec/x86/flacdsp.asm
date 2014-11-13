;******************************************************************************
;* FLAC DSP SIMD optimizations
;*
;* Copyright (C) 2014 Loren Merritt
;* Copyright (C) 2014 James Almer
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

;----------------------------------------------------------------------------------
;void ff_flac_decorrelate_[lrm]s_16_sse2(uint8_t **out, int32_t **in, int channels,
;                                                   int len, int shift);
;----------------------------------------------------------------------------------
%macro FLAC_DECORRELATE_16 3-4
cglobal flac_decorrelate_%1_16, 2, 4, 4, out, in0, in1, len
%if ARCH_X86_32 || WIN64
    movd       m3, r4m
%if ARCH_X86_32
    mov      lend, lenm
%endif
%else ; UNIX64
    movd       m3, r4d
%endif
    shl      lend, 2
    mov      in1q, [in0q + gprsize]
    mov      in0q, [in0q]
    mov      outq, [outq]
    add      in1q, lenq
    add      in0q, lenq
    add      outq, lenq
    neg      lenq

align 16
.loop:
    mova       m0, [in0q + lenq]
    mova       m1, [in1q + lenq]
%ifidn %1, ms
    psrad      m2, m1, 1
    psubd      m0, m2
%endif
%ifnidn %1, indep2
    p%4d       m2, m0, m1
%endif
    packssdw  m%2, m%2
    packssdw  m%3, m%3
    punpcklwd m%2, m%3
    psllw     m%2, m3
    mova [outq + lenq], m%2
    add      lenq, 16
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
FLAC_DECORRELATE_16 ls, 0, 2, sub
FLAC_DECORRELATE_16 rs, 2, 1, add
FLAC_DECORRELATE_16 ms, 2, 0, add

;----------------------------------------------------------------------------------
;void ff_flac_decorrelate_[lrm]s_32_sse2(uint8_t **out, int32_t **in, int channels,
;                                        int len, int shift);
;----------------------------------------------------------------------------------
%macro FLAC_DECORRELATE_32 5
cglobal flac_decorrelate_%1_32, 2, 4, 4, out, in0, in1, len
%if ARCH_X86_32 || WIN64
    movd       m3, r4m
%if ARCH_X86_32
    mov      lend, lenm
%endif
%else ; UNIX64
    movd       m3, r4d
%endif
    mov      in1q, [in0q + gprsize]
    mov      in0q, [in0q]
    mov      outq, [outq]
    sub      in1q, in0q

align 16
.loop:
    mova       m0, [in0q]
    mova       m1, [in0q + in1q]
%ifidn %1, ms
    psrad      m2, m1, 1
    psubd      m0, m2
%endif
    p%5d       m2, m0, m1
    pslld     m%2, m3
    pslld     m%3, m3

    SBUTTERFLY dq, %2, %3, %4

    mova  [outq         ], m%2
    mova  [outq + mmsize], m%3

    add      in0q, mmsize
    add      outq, mmsize*2
    sub      lend, mmsize/4
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse2
FLAC_DECORRELATE_32 ls, 0, 2, 1, sub
FLAC_DECORRELATE_32 rs, 2, 1, 0, add
FLAC_DECORRELATE_32 ms, 2, 0, 1, add

;-----------------------------------------------------------------------------------------
;void ff_flac_decorrelate_indep<ch>_<bps>_<opt>(uint8_t **out, int32_t **in, int channels,
;                                            int len, int shift);
;-----------------------------------------------------------------------------------------
%macro TRANSPOSE8x4D 9
    SBUTTERFLY dq,  %1, %2, %9
    SBUTTERFLY dq,  %3, %4, %9
    SBUTTERFLY dq,  %5, %6, %9
    SBUTTERFLY dq,  %7, %8, %9
    SBUTTERFLY qdq, %1, %3, %9
    SBUTTERFLY qdq, %2, %4, %9
    SBUTTERFLY qdq, %5, %7, %9
    SBUTTERFLY qdq, %6, %8, %9
    SWAP %2, %5
    SWAP %4, %7
%endmacro

;%1 = bps
;%2 = channels
;%3 = last xmm reg used
;%4 = word/dword (shift instruction)
%macro FLAC_DECORRELATE_INDEP 4
%define REPCOUNT %2/(32/%1) ; 16bits = channels / 2; 32bits = channels
cglobal flac_decorrelate_indep%2_%1, 2, %2+2, %3+1, out, in0, in1, len, in2, in3, in4, in5, in6, in7
%if ARCH_X86_32
    movd      m%3, r4m
%if %2 == 6
    DEFINE_ARGS out, in0, in1, in2, in3, in4, in5
    %define  lend  dword r3m
%else
    mov      lend, lenm
%endif
%elif WIN64
    movd      m%3, r4m
%else ; UNIX64
    movd      m%3, r4d
%endif

%assign %%i 1
%rep %2-1
    mov      in %+ %%i %+ q, [in0q+%%i*gprsize]
%assign %%i %%i+1
%endrep

    mov      in0q, [in0q]
    mov      outq, [outq]

%assign %%i 1
%rep %2-1
    sub      in %+ %%i %+ q, in0q
%assign %%i %%i+1
%endrep

align 16
.loop:
    mova       m0, [in0q]

%assign %%i 1
%rep REPCOUNT-1
    mova     m %+ %%i, [in0q + in %+ %%i %+ q]
%assign %%i %%i+1
%endrep

%if %1 == 32

%if %2 == 8
    TRANSPOSE8x4D 0, 1, 2, 3, 4, 5, 6, 7, 8
%elif %2 == 6
    SBUTTERFLY dq, 0, 1, 6
    SBUTTERFLY dq, 2, 3, 6
    SBUTTERFLY dq, 4, 5, 6

    punpcklqdq m6, m0, m2
    punpckhqdq m2, m4
    shufps     m4, m0, 0xe4
    punpcklqdq m0, m1, m3
    punpckhqdq m3, m5
    shufps     m5, m1, 0xe4
    SWAP 0,6,1,4,5,3
%elif %2 == 4
    TRANSPOSE4x4D 0, 1, 2, 3, 4
%else ; %2 == 2
    SBUTTERFLY dq, 0, 1, 2
%endif

%else ; %1 == 16

%if %2 == 8
    packssdw   m0, [in0q + in4q]
    packssdw   m1, [in0q + in5q]
    packssdw   m2, [in0q + in6q]
    packssdw   m3, [in0q + in7q]
    TRANSPOSE2x4x4W 0, 1, 2, 3, 4
%elif %2 == 6
    packssdw   m0, [in0q + in3q]
    packssdw   m1, [in0q + in4q]
    packssdw   m2, [in0q + in5q]
    pshufd     m3, m0,     q1032
    punpcklwd  m0, m1
    punpckhwd  m1, m2
    punpcklwd  m2, m3

    shufps     m3, m0, m2, q2020
    shufps     m0, m1,     q2031
    shufps     m2, m1,     q3131
    shufps     m1, m2, m3, q3120
    shufps     m3, m0,     q0220
    shufps     m0, m2,     q3113
    SWAP 2, 0, 3
%else ; %2 == 4
    packssdw   m0, [in0q + in2q]
    packssdw   m1, [in0q + in3q]
    SBUTTERFLY wd, 0, 1, 2
    SBUTTERFLY dq, 0, 1, 2
%endif

%endif

%assign %%i 0
%rep REPCOUNT
    psll%4   m %+ %%i, m%3
%assign %%i %%i+1
%endrep

%assign %%i 0
%rep REPCOUNT
    mova [outq + %%i*mmsize], m %+ %%i
%assign %%i %%i+1
%endrep

    add      in0q, mmsize
    add      outq, mmsize*REPCOUNT
    sub      lend, mmsize/4
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse2
FLAC_DECORRELATE_16 indep2, 0, 1 ; Reuse stereo 16bits macro
FLAC_DECORRELATE_INDEP 32, 2, 3, d
FLAC_DECORRELATE_INDEP 16, 4, 3, w
FLAC_DECORRELATE_INDEP 32, 4, 5, d
FLAC_DECORRELATE_INDEP 16, 6, 4, w
FLAC_DECORRELATE_INDEP 32, 6, 7, d
%if ARCH_X86_64
FLAC_DECORRELATE_INDEP 16, 8, 5, w
FLAC_DECORRELATE_INDEP 32, 8, 9, d
%endif

INIT_XMM avx
FLAC_DECORRELATE_INDEP 32, 4, 5, d
FLAC_DECORRELATE_INDEP 32, 6, 7, d
%if ARCH_X86_64
FLAC_DECORRELATE_INDEP 16, 8, 5, w
FLAC_DECORRELATE_INDEP 32, 8, 9, d
%endif
