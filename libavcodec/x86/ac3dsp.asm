;*****************************************************************************
;* x86-optimized AC-3 DSP utils
;* Copyright (c) 2011 Justin Ruggles
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

; 16777216.0f - used in ff_float_to_fixed24()
pf_1_24: times 4 dd 0x4B800000

; used in ff_ac3_compute_mantissa_size()
cextern ac3_bap_bits
pw_bap_mul1: dw 21846, 21846, 0, 32768, 21846, 21846, 0, 32768
pw_bap_mul2: dw 5, 7, 0, 7, 5, 7, 0, 7

; used in ff_ac3_extract_exponents()
pd_1:   times 4 dd 1
pd_151: times 4 dd 151

SECTION .text

;-----------------------------------------------------------------------------
; void ff_ac3_exponent_min(uint8_t *exp, int num_reuse_blocks, int nb_coefs)
;-----------------------------------------------------------------------------

%macro AC3_EXPONENT_MIN 0
cglobal ac3_exponent_min, 3, 4, 2, exp, reuse_blks, expn, offset
    shl  reuse_blksq, 8
    jz .end
    LOOP_ALIGN
.nextexp:
    mov      offsetq, reuse_blksq
    mova          m0, [expq+offsetq]
    sub      offsetq, 256
    LOOP_ALIGN
.nextblk:
    PMINUB        m0, [expq+offsetq], m1
    sub      offsetq, 256
    jae .nextblk
    mova      [expq], m0
    add         expq, mmsize
    sub        expnq, mmsize
    jg .nextexp
.end:
    REP_RET
%endmacro

%define LOOP_ALIGN
INIT_MMX mmx
AC3_EXPONENT_MIN
%if HAVE_MMXEXT_EXTERNAL
%define LOOP_ALIGN ALIGN 16
INIT_MMX mmxext
AC3_EXPONENT_MIN
%endif
%if HAVE_SSE2_EXTERNAL
INIT_XMM sse2
AC3_EXPONENT_MIN
%endif
%undef LOOP_ALIGN

;-----------------------------------------------------------------------------
; int ff_ac3_max_msb_abs_int16(const int16_t *src, int len)
;
; This function uses 2 different methods to calculate a valid result.
; 1) logical 'or' of abs of each element
;        This is used for ssse3 because of the pabsw instruction.
;        It is also used for mmx because of the lack of min/max instructions.
; 2) calculate min/max for the array, then or(abs(min),abs(max))
;        This is used for mmxext and sse2 because they have pminsw/pmaxsw.
;-----------------------------------------------------------------------------

; logical 'or' of 4 or 8 words in an mmx or xmm register into the low word
%macro OR_WORDS_HORIZ 2 ; src, tmp
%if cpuflag(sse2)
    movhlps     %2, %1
    por         %1, %2
    pshuflw     %2, %1, q0032
    por         %1, %2
    pshuflw     %2, %1, q0001
    por         %1, %2
%elif cpuflag(mmxext)
    pshufw      %2, %1, q0032
    por         %1, %2
    pshufw      %2, %1, q0001
    por         %1, %2
%else ; mmx
    movq        %2, %1
    psrlq       %2, 32
    por         %1, %2
    movq        %2, %1
    psrlq       %2, 16
    por         %1, %2
%endif
%endmacro

%macro AC3_MAX_MSB_ABS_INT16 1
cglobal ac3_max_msb_abs_int16, 2,2,5, src, len
    pxor        m2, m2
    pxor        m3, m3
.loop:
%ifidn %1, min_max
    mova        m0, [srcq]
    mova        m1, [srcq+mmsize]
    pminsw      m2, m0
    pminsw      m2, m1
    pmaxsw      m3, m0
    pmaxsw      m3, m1
%else ; or_abs
%if notcpuflag(ssse3)
    mova        m0, [srcq]
    mova        m1, [srcq+mmsize]
    ABS2        m0, m1, m3, m4
%else ; ssse3
    ; using memory args is faster for ssse3
    pabsw       m0, [srcq]
    pabsw       m1, [srcq+mmsize]
%endif
    por         m2, m0
    por         m2, m1
%endif
    add       srcq, mmsize*2
    sub       lend, mmsize
    ja .loop
%ifidn %1, min_max
    ABS2        m2, m3, m0, m1
    por         m2, m3
%endif
    OR_WORDS_HORIZ m2, m0
    movd       eax, m2
    and        eax, 0xFFFF
    RET
%endmacro

INIT_MMX mmx
AC3_MAX_MSB_ABS_INT16 or_abs
INIT_MMX mmxext
AC3_MAX_MSB_ABS_INT16 min_max
INIT_XMM sse2
AC3_MAX_MSB_ABS_INT16 min_max
INIT_XMM ssse3
AC3_MAX_MSB_ABS_INT16 or_abs

;-----------------------------------------------------------------------------
; macro used for ff_ac3_lshift_int16() and ff_ac3_rshift_int32()
;-----------------------------------------------------------------------------

%macro AC3_SHIFT 3 ; l/r, 16/32, shift instruction, instruction set
cglobal ac3_%1shift_int%2, 3, 3, 5, src, len, shift
    movd      m0, shiftd
.loop:
    mova      m1, [srcq         ]
    mova      m2, [srcq+mmsize  ]
    mova      m3, [srcq+mmsize*2]
    mova      m4, [srcq+mmsize*3]
    %3        m1, m0
    %3        m2, m0
    %3        m3, m0
    %3        m4, m0
    mova  [srcq         ], m1
    mova  [srcq+mmsize  ], m2
    mova  [srcq+mmsize*2], m3
    mova  [srcq+mmsize*3], m4
    add     srcq, mmsize*4
    sub     lend, mmsize*32/%2
    ja .loop
.end:
    REP_RET
%endmacro

;-----------------------------------------------------------------------------
; void ff_ac3_lshift_int16(int16_t *src, unsigned int len, unsigned int shift)
;-----------------------------------------------------------------------------

INIT_MMX mmx
AC3_SHIFT l, 16, psllw
INIT_XMM sse2
AC3_SHIFT l, 16, psllw

;-----------------------------------------------------------------------------
; void ff_ac3_rshift_int32(int32_t *src, unsigned int len, unsigned int shift)
;-----------------------------------------------------------------------------

INIT_MMX mmx
AC3_SHIFT r, 32, psrad
INIT_XMM sse2
AC3_SHIFT r, 32, psrad

;-----------------------------------------------------------------------------
; void ff_float_to_fixed24(int32_t *dst, const float *src, unsigned int len)
;-----------------------------------------------------------------------------

; The 3DNow! version is not bit-identical because pf2id uses truncation rather
; than round-to-nearest.
INIT_MMX 3dnow
cglobal float_to_fixed24, 3, 3, 0, dst, src, len
    movq   m0, [pf_1_24]
.loop:
    movq   m1, [srcq   ]
    movq   m2, [srcq+8 ]
    movq   m3, [srcq+16]
    movq   m4, [srcq+24]
    pfmul  m1, m0
    pfmul  m2, m0
    pfmul  m3, m0
    pfmul  m4, m0
    pf2id  m1, m1
    pf2id  m2, m2
    pf2id  m3, m3
    pf2id  m4, m4
    movq  [dstq   ], m1
    movq  [dstq+8 ], m2
    movq  [dstq+16], m3
    movq  [dstq+24], m4
    add  srcq, 32
    add  dstq, 32
    sub  lend, 8
    ja .loop
    femms
    RET

INIT_XMM sse
cglobal float_to_fixed24, 3, 3, 3, dst, src, len
    movaps     m0, [pf_1_24]
.loop:
    movaps     m1, [srcq   ]
    movaps     m2, [srcq+16]
    mulps      m1, m0
    mulps      m2, m0
    cvtps2pi  mm0, m1
    movhlps    m1, m1
    cvtps2pi  mm1, m1
    cvtps2pi  mm2, m2
    movhlps    m2, m2
    cvtps2pi  mm3, m2
    movq  [dstq   ], mm0
    movq  [dstq+ 8], mm1
    movq  [dstq+16], mm2
    movq  [dstq+24], mm3
    add      srcq, 32
    add      dstq, 32
    sub      lend, 8
    ja .loop
    emms
    RET

INIT_XMM sse2
cglobal float_to_fixed24, 3, 3, 9, dst, src, len
    movaps     m0, [pf_1_24]
.loop:
    movaps     m1, [srcq    ]
    movaps     m2, [srcq+16 ]
    movaps     m3, [srcq+32 ]
    movaps     m4, [srcq+48 ]
%ifdef m8
    movaps     m5, [srcq+64 ]
    movaps     m6, [srcq+80 ]
    movaps     m7, [srcq+96 ]
    movaps     m8, [srcq+112]
%endif
    mulps      m1, m0
    mulps      m2, m0
    mulps      m3, m0
    mulps      m4, m0
%ifdef m8
    mulps      m5, m0
    mulps      m6, m0
    mulps      m7, m0
    mulps      m8, m0
%endif
    cvtps2dq   m1, m1
    cvtps2dq   m2, m2
    cvtps2dq   m3, m3
    cvtps2dq   m4, m4
%ifdef m8
    cvtps2dq   m5, m5
    cvtps2dq   m6, m6
    cvtps2dq   m7, m7
    cvtps2dq   m8, m8
%endif
    movdqa  [dstq    ], m1
    movdqa  [dstq+16 ], m2
    movdqa  [dstq+32 ], m3
    movdqa  [dstq+48 ], m4
%ifdef m8
    movdqa  [dstq+64 ], m5
    movdqa  [dstq+80 ], m6
    movdqa  [dstq+96 ], m7
    movdqa  [dstq+112], m8
    add      srcq, 128
    add      dstq, 128
    sub      lenq, 32
%else
    add      srcq, 64
    add      dstq, 64
    sub      lenq, 16
%endif
    ja .loop
    REP_RET

;------------------------------------------------------------------------------
; int ff_ac3_compute_mantissa_size(uint16_t mant_cnt[6][16])
;------------------------------------------------------------------------------

%macro PHADDD4 2 ; xmm src, xmm tmp
    movhlps  %2, %1
    paddd    %1, %2
    pshufd   %2, %1, 0x1
    paddd    %1, %2
%endmacro

INIT_XMM sse2
cglobal ac3_compute_mantissa_size, 1, 2, 4, mant_cnt, sum
    movdqa      m0, [mant_cntq      ]
    movdqa      m1, [mant_cntq+ 1*16]
    paddw       m0, [mant_cntq+ 2*16]
    paddw       m1, [mant_cntq+ 3*16]
    paddw       m0, [mant_cntq+ 4*16]
    paddw       m1, [mant_cntq+ 5*16]
    paddw       m0, [mant_cntq+ 6*16]
    paddw       m1, [mant_cntq+ 7*16]
    paddw       m0, [mant_cntq+ 8*16]
    paddw       m1, [mant_cntq+ 9*16]
    paddw       m0, [mant_cntq+10*16]
    paddw       m1, [mant_cntq+11*16]
    pmaddwd     m0, [ac3_bap_bits   ]
    pmaddwd     m1, [ac3_bap_bits+16]
    paddd       m0, m1
    PHADDD4     m0, m1
    movd      sumd, m0
    movdqa      m3, [pw_bap_mul1]
    movhpd      m0, [mant_cntq     +2]
    movlpd      m0, [mant_cntq+1*32+2]
    movhpd      m1, [mant_cntq+2*32+2]
    movlpd      m1, [mant_cntq+3*32+2]
    movhpd      m2, [mant_cntq+4*32+2]
    movlpd      m2, [mant_cntq+5*32+2]
    pmulhuw     m0, m3
    pmulhuw     m1, m3
    pmulhuw     m2, m3
    paddusw     m0, m1
    paddusw     m0, m2
    pmaddwd     m0, [pw_bap_mul2]
    PHADDD4     m0, m1
    movd       eax, m0
    add        eax, sumd
    RET

;------------------------------------------------------------------------------
; void ff_ac3_extract_exponents(uint8_t *exp, int32_t *coef, int nb_coefs)
;------------------------------------------------------------------------------

%macro PABSD 1-2 ; src/dst, unused
%if cpuflag(ssse3)
    pabsd    %1, %1
%else ; src/dst, tmp
    pxor     %2, %2
    pcmpgtd  %2, %1
    pxor     %1, %2
    psubd    %1, %2
%endif
%endmacro

%macro AC3_EXTRACT_EXPONENTS 0
cglobal ac3_extract_exponents, 3, 3, 4, exp, coef, len
    add     expq, lenq
    lea    coefq, [coefq+4*lenq]
    neg     lenq
    mova      m2, [pd_1]
    mova      m3, [pd_151]
.loop:
    ; move 4 32-bit coefs to xmm0
    mova      m0, [coefq+4*lenq]
    ; absolute value
    PABSD     m0, m1
    ; convert to float and extract exponents
    pslld     m0, 1
    por       m0, m2
    cvtdq2ps  m1, m0
    psrld     m1, 23
    mova      m0, m3
    psubd     m0, m1
    ; move the lowest byte in each of 4 dwords to the low dword
    ; NOTE: We cannot just extract the low bytes with pshufb because the dword
    ;       result for 16777215 is -1 due to float inaccuracy. Using packuswb
    ;       clips this to 0, which is the correct exponent.
    packssdw  m0, m0
    packuswb  m0, m0
    movd  [expq+lenq], m0

    add     lenq, 4
    jl .loop
    REP_RET
%endmacro

%if HAVE_SSE2_EXTERNAL
INIT_XMM sse2
AC3_EXTRACT_EXPONENTS
%endif
%if HAVE_SSSE3_EXTERNAL
INIT_XMM ssse3
AC3_EXTRACT_EXPONENTS
%endif
