; XVID MPEG-4 VIDEO CODEC
; - SSE2 inverse discrete cosine transform -
;
; Copyright(C) 2003 Pascal Massimino <skal@planet-d.net>
;
; Conversion to gcc syntax with modifications
; by Alexander Strange <astrange@ithinksw.com>
;
; Originally from dct/x86_asm/fdct_sse2_skal.asm in Xvid.
;
; This file is part of FFmpeg.
;
; Vertical pass is an implementation of the scheme:
;  Loeffler C., Ligtenberg A., and Moschytz C.S.:
;  Practical Fast 1D DCT Algorithm with Eleven Multiplications,
;  Proc. ICASSP 1989, 988-991.
;
; Horizontal pass is a double 4x4 vector/matrix multiplication,
; (see also Intel's Application Note 922:
;  http://developer.intel.com/vtune/cbts/strmsimd/922down.htm
;  Copyright (C) 1999 Intel Corporation)
;
; More details at http://skal.planet-d.net/coding/dct.html
;
; FFmpeg is free software; you can redistribute it and/or
; modify it under the terms of the GNU Lesser General Public
; License as published by the Free Software Foundation; either
; version 2.1 of the License, or (at your option) any later version.
;
; FFmpeg is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; Lesser General Public License for more details.
;
; You should have received a copy of the GNU Lesser General Public License
; along with FFmpeg; if not, write to the Free Software Foundation,
; Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

%include "libavutil/x86/x86util.asm"

SECTION_RODATA
tan1:   times 8 dw 13036
tan2:   times 8 dw 27146
tan3:   times 8 dw 43790
sqrt2:  times 8 dw 23170

iTab1:  dw 0x4000, 0x539f, 0xc000, 0xac61, 0x4000, 0xdd5d, 0x4000, 0xdd5d
        dw 0x4000, 0x22a3, 0x4000, 0x22a3, 0xc000, 0x539f, 0x4000, 0xac61
        dw 0x3249, 0x11a8, 0x4b42, 0xee58, 0x11a8, 0x4b42, 0x11a8, 0xcdb7
        dw 0x58c5, 0x4b42, 0xa73b, 0xcdb7, 0x3249, 0xa73b, 0x4b42, 0xa73b
iTab2:  dw 0x58c5, 0x73fc, 0xa73b, 0x8c04, 0x58c5, 0xcff5, 0x58c5, 0xcff5
        dw 0x58c5, 0x300b, 0x58c5, 0x300b, 0xa73b, 0x73fc, 0x58c5, 0x8c04
        dw 0x45bf, 0x187e, 0x6862, 0xe782, 0x187e, 0x6862, 0x187e, 0xba41
        dw 0x7b21, 0x6862, 0x84df, 0xba41, 0x45bf, 0x84df, 0x6862, 0x84df
iTab3:  dw 0x539f, 0x6d41, 0xac61, 0x92bf, 0x539f, 0xd2bf, 0x539f, 0xd2bf
        dw 0x539f, 0x2d41, 0x539f, 0x2d41, 0xac61, 0x6d41, 0x539f, 0x92bf
        dw 0x41b3, 0x1712, 0x6254, 0xe8ee, 0x1712, 0x6254, 0x1712, 0xbe4d
        dw 0x73fc, 0x6254, 0x8c04, 0xbe4d, 0x41b3, 0x8c04, 0x6254, 0x8c04
iTab4:  dw 0x4b42, 0x6254, 0xb4be, 0x9dac, 0x4b42, 0xd746, 0x4b42, 0xd746
        dw 0x4b42, 0x28ba, 0x4b42, 0x28ba, 0xb4be, 0x6254, 0x4b42, 0x9dac
        dw 0x3b21, 0x14c3, 0x587e, 0xeb3d, 0x14c3, 0x587e, 0x14c3, 0xc4df
        dw 0x6862, 0x587e, 0x979e, 0xc4df, 0x3b21, 0x979e, 0x587e, 0x979e

walkenIdctRounders: times 4 dd 65536
                    times 4 dd  3597
                    times 4 dd  2260
                    times 4 dd  1203
                    times 4 dd   120
                    times 4 dd   512

pb_127: times 8 db 127

SECTION .text

; Temporary storage before the column pass
%define ROW1 xmm6
%define ROW3 xmm4
%define ROW5 xmm5
%define ROW7 xmm7

%macro CLEAR_ODD 1
    pxor      %1, %1
%endmacro
%macro PUT_ODD 1
    pshufhw   %1, xmm2, 0x1B
%endmacro

%macro MOV32 2
%if ARCH_X86_32
    movdqa    %2, %1
%endif
%endmacro

%macro CLEAR_EVEN 1
%if ARCH_X86_64
    CLEAR_ODD %1
%endif
%endmacro

%macro PUT_EVEN 1
%if ARCH_X86_64
    PUT_ODD   %1
%else
    pshufhw xmm2, xmm2, 0x1B
    movdqa    %1, xmm2
%endif
%endmacro

%if ARCH_X86_64
%define ROW0  xmm8
%define REG0  ROW0
%define ROW2  xmm9
%define REG2  ROW2
%define ROW4  xmm10
%define REG4  ROW4
%define ROW6  xmm11
%define REG6  ROW6
%define XMMS  xmm12
%define SREG2 REG2
%define TAN3  xmm13
%define TAN1  xmm14
%else
%define ROW0  [r0 + 0*16]
%define REG0  xmm4
%define ROW2  [r0 + 2*16]
%define REG2  xmm4
%define ROW4  [r0 + 4*16]
%define REG4  xmm6
%define ROW6  [r0 + 6*16]
%define REG6  xmm6
%define XMMS  xmm2
%define SREG2 xmm7
%define TAN3  xmm0
%define TAN1  xmm2
%endif

%macro JZ  2
    test      %1, %1
    jz       .%2
%endmacro

%macro JNZ  2
    test      %1, %1
    jnz      .%2
%endmacro

%macro TEST_ONE_ROW 4 ; src, reg, clear, arg
    %3        %4
    movq     mm1, [%1]
    por      mm1, [%1 + 8]
    paddusb  mm1, mm0
    pmovmskb  %2, mm1
%endmacro

;row1, row2, reg1, reg2, clear1, arg1, clear2, arg2
%macro  TEST_TWO_ROWS  8
    %5         %6
    %7         %8
    movq      mm1, [%1 + 0]
    por       mm1, [%1 + 8]
    movq      mm2, [%2 + 0]
    por       mm2, [%2 + 8]
    paddusb   mm1, mm0
    paddusb   mm2, mm0
    pmovmskb   %3, mm1
    pmovmskb   %4, mm2
%endmacro

; IDCT pass on rows.
%macro iMTX_MULT   4-5 ; src, table, put, arg, rounder
    movdqa       xmm3, [%1]
    movdqa       xmm0, xmm3
    pshufd       xmm1, xmm3, 0x11 ; 4602
    punpcklqdq   xmm0, xmm0       ; 0246
    pmaddwd      xmm0, [%2]
    pmaddwd      xmm1, [%2+16]
    pshufd       xmm2, xmm3, 0xBB ; 5713
    punpckhqdq   xmm3, xmm3       ; 1357
    pmaddwd      xmm2, [%2+32]
    pmaddwd      xmm3, [%2+48]
    paddd        xmm0, xmm1
    paddd        xmm2, xmm3
%if %0 == 5
    paddd        xmm0, [walkenIdctRounders+%5]
%endif
    movdqa       xmm3, xmm2
    paddd        xmm2, xmm0
    psubd        xmm0, xmm3
    psrad        xmm2, 11
    psrad        xmm0, 11
    packssdw     xmm2, xmm0
    %3           %4
%endmacro

%macro iLLM_HEAD 0
    movdqa   TAN3, [tan3]
    movdqa   TAN1, [tan1]
%endmacro

; IDCT pass on columns.
%macro iLLM_PASS  1  ;dct
    movdqa   xmm1, TAN3
    movdqa   xmm3, TAN1
    pmulhw   TAN3, xmm4
    pmulhw   xmm1, xmm5
    paddsw   TAN3, xmm4
    paddsw   xmm1, xmm5
    psubsw   TAN3, xmm5
    paddsw   xmm1, xmm4
    pmulhw   xmm3, xmm7
    pmulhw   TAN1, xmm6
    paddsw   xmm3, xmm6
    psubsw   TAN1, xmm7
    movdqa   xmm7, xmm3
    movdqa   xmm6, TAN1
    psubsw   xmm3, xmm1
    psubsw   TAN1, TAN3
    paddsw   xmm1, xmm7
    paddsw   TAN3, xmm6
    movdqa   xmm6, xmm3
    psubsw   xmm3, TAN3
    paddsw   TAN3, xmm6
    movdqa   xmm4, [sqrt2]
    pmulhw   xmm3, xmm4
    pmulhw   TAN3, xmm4
    paddsw   TAN3, TAN3
    paddsw   xmm3, xmm3
    movdqa   xmm7, [tan2]
    MOV32    ROW2, REG2
    MOV32    ROW6, REG6
    movdqa   xmm5, xmm7
    pmulhw   xmm7, REG6
    pmulhw   xmm5, REG2
    paddsw   xmm7, REG2
    psubsw   xmm5, REG6
    MOV32    ROW0, REG0
    MOV32    ROW4, REG4
    MOV32    TAN1, [r0]
    movdqa   XMMS, REG0
    psubsw   REG0, REG4
    paddsw   REG4, XMMS
    movdqa   XMMS, REG4
    psubsw   REG4, xmm7
    paddsw   xmm7, XMMS
    movdqa   XMMS, REG0
    psubsw   REG0, xmm5
    paddsw   xmm5, XMMS
    movdqa   XMMS, xmm5
    psubsw   xmm5, TAN3
    paddsw   TAN3, XMMS
    movdqa   XMMS, REG0
    psubsw   REG0, xmm3
    paddsw   xmm3, XMMS
    MOV32    [r0], TAN1
    psraw    xmm5, 6
    psraw    REG0, 6
    psraw    TAN3, 6
    psraw    xmm3, 6
    movdqa   [%1+1*16], TAN3
    movdqa   [%1+2*16], xmm3
    movdqa   [%1+5*16], REG0
    movdqa   [%1+6*16], xmm5
    movdqa   xmm0, xmm7
    movdqa   xmm4, REG4
    psubsw   xmm7, xmm1
    psubsw   REG4, TAN1
    paddsw   xmm1, xmm0
    paddsw   TAN1, xmm4
    psraw    xmm1, 6
    psraw    xmm7, 6
    psraw    TAN1, 6
    psraw    REG4, 6
    movdqa   [%1+0*16], xmm1
    movdqa   [%1+3*16], TAN1
    movdqa   [%1+4*16], REG4
    movdqa   [%1+7*16], xmm7
%endmacro

; IDCT pass on columns, assuming rows 4-7 are zero
%macro iLLM_PASS_SPARSE   1 ;dct
    pmulhw   TAN3, xmm4
    paddsw   TAN3, xmm4
    movdqa   xmm3, xmm6
    pmulhw   TAN1, xmm6
    movdqa   xmm1, xmm4
    psubsw   xmm3, xmm1
    paddsw   xmm1, xmm6
    movdqa   xmm6, TAN1
    psubsw   TAN1, TAN3
    paddsw   TAN3, xmm6
    movdqa   xmm6, xmm3
    psubsw   xmm3, TAN3
    paddsw   TAN3, xmm6
    movdqa   xmm4, [sqrt2]
    pmulhw   xmm3, xmm4
    pmulhw   TAN3, xmm4
    paddsw   TAN3, TAN3
    paddsw   xmm3, xmm3
    movdqa   xmm5, [tan2]
    MOV32    ROW2, SREG2
    pmulhw   xmm5, SREG2
    MOV32    ROW0, REG0
    movdqa   xmm6, REG0
    psubsw   xmm6, SREG2
    paddsw  SREG2, REG0
    MOV32    TAN1, [r0]
    movdqa   XMMS, REG0
    psubsw   REG0, xmm5
    paddsw   xmm5, XMMS
    movdqa   XMMS, xmm5
    psubsw   xmm5, TAN3
    paddsw   TAN3, XMMS
    movdqa   XMMS, REG0
    psubsw   REG0, xmm3
    paddsw   xmm3, XMMS
    MOV32    [r0], TAN1
    psraw        xmm5, 6
    psraw        REG0, 6
    psraw        TAN3, 6
    psraw        xmm3, 6
    movdqa   [%1+1*16], TAN3
    movdqa   [%1+2*16], xmm3
    movdqa   [%1+5*16], REG0
    movdqa   [%1+6*16], xmm5
    movdqa   xmm0, SREG2
    movdqa   xmm4, xmm6
    psubsw  SREG2, xmm1
    psubsw   xmm6, TAN1
    paddsw   xmm1, xmm0
    paddsw   TAN1, xmm4
    psraw        xmm1, 6
    psraw       SREG2, 6
    psraw        TAN1, 6
    psraw        xmm6, 6
    movdqa   [%1+0*16], xmm1
    movdqa   [%1+3*16], TAN1
    movdqa   [%1+4*16], xmm6
    movdqa   [%1+7*16], SREG2
%endmacro

INIT_XMM sse2
cglobal xvid_idct, 1, 5, 8+7*ARCH_X86_64, block
    movq           mm0, [pb_127]
    iMTX_MULT      r0 + 0*16, iTab1, PUT_EVEN, ROW0, 0*16
    iMTX_MULT      r0 + 1*16, iTab2, PUT_ODD, ROW1,  1*16
    iMTX_MULT      r0 + 2*16, iTab3, PUT_EVEN, ROW2, 2*16

    TEST_TWO_ROWS  r0 + 3*16, r0 + 4*16, r1d, r2d, CLEAR_ODD, ROW3, CLEAR_EVEN, ROW4 ; a, c
    JZ   r1d, col1
    iMTX_MULT      r0 + 3*16, iTab4, PUT_ODD, ROW3,  3*16
.col1:
    TEST_TWO_ROWS  r0 + 5*16, r0 + 6*16, r1d, r3d, CLEAR_ODD, ROW5, CLEAR_EVEN, ROW6 ; a, d
    TEST_ONE_ROW   r0 + 7*16, r4d, CLEAR_ODD, ROW7 ; esi

    iLLM_HEAD
    JNZ  r2d, 2
    JNZ  r1d, 3
    JNZ  r3d, 4
    JNZ  r4d, 5
    iLLM_PASS_SPARSE r0
    jmp .6
.2:
    iMTX_MULT     r0 + 4*16, iTab1, PUT_EVEN, ROW4
.3:
    iMTX_MULT     r0 + 5*16, iTab4, PUT_ODD, ROW5,  4*16
    JZ   r3d, col2
.4:
    iMTX_MULT     r0 + 6*16, iTab3, PUT_EVEN, ROW6, 5*16
.col2:
    JZ   r4d, col3
.5:
    iMTX_MULT     r0 + 7*16, iTab2, PUT_ODD, ROW7,  5*16
.col3:
%if ARCH_X86_32
    iLLM_HEAD
%endif
    iLLM_PASS     r0
.6:
    RET
