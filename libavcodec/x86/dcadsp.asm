;******************************************************************************
;* SSE-optimized functions for the DCA decoder
;* Copyright (C) 2012-2014 Christophe Gisquet <christophe.gisquet@gmail.com>
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

SECTION_RODATA
pf_inv16:  times 4 dd 0x3D800000 ; 1/16

SECTION .text

; void decode_hf(float dst[DCA_SUBBANDS][8], const int32_t vq_num[DCA_SUBBANDS],
;                const int8_t hf_vq[1024][32], intptr_t vq_offset,
;                int32_t scale[DCA_SUBBANDS][2], intptr_t start, intptr_t end)

%macro DECODE_HF 0
cglobal decode_hf, 6,6,5, dst, num, src, offset, scale, start, end
    lea       srcq, [srcq + offsetq]
    shl     startq, 2
    mov    offsetd, endm
%define DICT offsetq
    shl    offsetq, 2
    mov       endm, offsetq
.loop:
%if ARCH_X86_64
    mov    offsetd, [scaleq + 2 * startq]
    cvtsi2ss    m0, offsetd
%else
    cvtsi2ss    m0, [scaleq + 2 * startq]
%endif
    mov    offsetd, [numq + startq]
    mulss       m0, [pf_inv16]
    shl       DICT, 5
    shufps      m0, m0, 0
%if cpuflag(sse2)
%if cpuflag(sse4)
    pmovsxbd    m1, [srcq + DICT + 0]
    pmovsxbd    m2, [srcq + DICT + 4]
%else
    movq        m1, [srcq + DICT]
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
    movd       mm0, [srcq + DICT + 0]
    movd       mm1, [srcq + DICT + 4]
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
    shufps      m1, m3, q1010
    shufps      m2, m4, q1010
%endif
    mulps       m1, m0
    mulps       m2, m0
    mova [dstq + 8 * startq +  0], m1
    mova [dstq + 8 * startq + 16], m2
    add     startq, 4
    cmp     startq, endm
    jl       .loop
.end:
%if notcpuflag(sse2)
    emms
%endif
    REP_RET
%endmacro

%if ARCH_X86_32
INIT_XMM sse
DECODE_HF
%endif

INIT_XMM sse2
DECODE_HF

INIT_XMM sse4
DECODE_HF

; %1=v0/v1  %2=in1  %3=in2
%macro FIR_LOOP 2-3
.loop%1:
%define va          m1
%define vb          m2
%if %1
%define OFFSET      0
%else
%define OFFSET      NUM_COEF*count
%endif
; for v0, incrementing and for v1, decrementing
    mova        va, [cf0q + OFFSET]
    mova        vb, [cf0q + OFFSET + 4*NUM_COEF]
%if %0 == 3
    mova        m4, [cf0q + OFFSET + mmsize]
    mova        m0, [cf0q + OFFSET + 4*NUM_COEF + mmsize]
%endif
    mulps       va, %2
    mulps       vb, %2
%if %0 == 3
%if cpuflag(fma3)
    fmaddps     va, m4, %3, va
    fmaddps     vb, m0, %3, vb
%else
    mulps       m4, %3
    mulps       m0, %3
    addps       va, m4
    addps       vb, m0
%endif
%endif
    ; va = va1 va2 va3 va4
    ; vb = vb1 vb2 vb3 vb4
%if %1
    SWAP        va, vb
%endif
    mova        m4, va
    unpcklps    va, vb ; va3 vb3 va4 vb4
    unpckhps    m4, vb ; va1 vb1 va2 vb2
    addps       m4, va ; va1+3 vb1+3 va2+4 vb2+4
    movhlps     vb, m4 ; va1+3  vb1+3
    addps       vb, m4 ; va0..4 vb0..4
    movlps  [outq + count], vb
%if %1
    sub       cf0q, 8*NUM_COEF
%endif
    add      count, 8
    jl   .loop%1
%endmacro

; void dca_lfe_fir(float *out, float *in, float *coefs)
%macro DCA_LFE_FIR 1
cglobal dca_lfe_fir%1, 3,3,6-%1, out, in, cf0
%define IN1       m3
%define IN2       m5
%define count     inq
%define NUM_COEF  4*(2-%1)
%define NUM_OUT   32*(%1+1)

    movu     IN1, [inq + 4 - 1*mmsize]
    shufps   IN1, IN1, q0123
%if %1 == 0
    movu     IN2, [inq + 4 - 2*mmsize]
    shufps   IN2, IN2, q0123
%endif

    mov    count, -4*NUM_OUT
    add     cf0q, 4*NUM_COEF*NUM_OUT
    add     outq, 4*NUM_OUT
    ; compute v0 first
%if %1 == 0
    FIR_LOOP   0, IN1, IN2
%else
    FIR_LOOP   0, IN1
%endif
    shufps   IN1, IN1, q0123
    mov    count, -4*NUM_OUT
    ; cf1 already correctly positioned
    add     outq, 4*NUM_OUT          ; outq now at out2
    sub     cf0q, 8*NUM_COEF
%if %1 == 0
    shufps   IN2, IN2, q0123
    FIR_LOOP   1, IN2, IN1
%else
    FIR_LOOP   1, IN1
%endif
    RET
%endmacro

INIT_XMM sse
DCA_LFE_FIR 0
DCA_LFE_FIR 1
%if HAVE_FMA3_EXTERNAL
INIT_XMM fma3
DCA_LFE_FIR 0
%endif

%macro SETZERO 1
%if cpuflag(sse2) && notcpuflag(avx)
    pxor          %1, %1
%else
    xorps         %1, %1, %1
%endif
%endmacro

%macro SHUF 3
%if cpuflag(avx)
    mova          %3, [%2 - 16]
    vperm2f128    %1, %3, %3, 1
    vshufps       %1, %1, %1, q0123
%elif cpuflag(sse2)
    pshufd        %1, [%2], q0123
%else
    mova          %1, [%2]
    shufps        %1, %1, q0123
%endif
%endmacro

%macro INNER_LOOP   1
    ; reading backwards:  ptr1 = synth_buf + j + i; ptr2 = synth_buf + j - i
    ;~ a += window[i + j]      * (-synth_buf[15 - i + j])
    ;~ b += window[i + j + 16] * (synth_buf[i + j])
    SHUF          m5,  ptr2 + j + (15 - 3) * 4, m6
    mova          m6, [ptr1 + j]
%if ARCH_X86_64
    SHUF         m11,  ptr2 + j + (15 - 3) * 4 - mmsize, m12
    mova         m12, [ptr1 + j + mmsize]
%endif
%if cpuflag(fma3)
    fmaddps       m2, m6,  [win + %1 + j + 16 * 4], m2
    fnmaddps      m1, m5,  [win + %1 + j], m1
%if ARCH_X86_64
    fmaddps       m8, m12, [win + %1 + j + mmsize + 16 * 4], m8
    fnmaddps      m7, m11, [win + %1 + j + mmsize], m7
%endif
%else ; non-FMA
    mulps         m6, m6,  [win + %1 + j + 16 * 4]
    mulps         m5, m5,  [win + %1 + j]
%if ARCH_X86_64
    mulps        m12, m12, [win + %1 + j + mmsize + 16 * 4]
    mulps        m11, m11, [win + %1 + j + mmsize]
%endif
    addps         m2, m2, m6
    subps         m1, m1, m5
%if ARCH_X86_64
    addps         m8, m8, m12
    subps         m7, m7, m11
%endif
%endif ; cpuflag(fma3)
    ;~ c += window[i + j + 32] * (synth_buf[16 + i + j])
    ;~ d += window[i + j + 48] * (synth_buf[31 - i + j])
    SHUF          m6,  ptr2 + j + (31 - 3) * 4, m5
    mova          m5, [ptr1 + j + 16 * 4]
%if ARCH_X86_64
    SHUF         m12,  ptr2 + j + (31 - 3) * 4 - mmsize, m11
    mova         m11, [ptr1 + j + mmsize + 16 * 4]
%endif
%if cpuflag(fma3)
    fmaddps       m3, m5,  [win + %1 + j + 32 * 4], m3
    fmaddps       m4, m6,  [win + %1 + j + 48 * 4], m4
%if ARCH_X86_64
    fmaddps       m9, m11, [win + %1 + j + mmsize + 32 * 4], m9
    fmaddps      m10, m12, [win + %1 + j + mmsize + 48 * 4], m10
%endif
%else ; non-FMA
    mulps         m5, m5,  [win + %1 + j + 32 * 4]
    mulps         m6, m6,  [win + %1 + j + 48 * 4]
%if ARCH_X86_64
    mulps        m11, m11, [win + %1 + j + mmsize + 32 * 4]
    mulps        m12, m12, [win + %1 + j + mmsize + 48 * 4]
%endif
    addps         m3, m3, m5
    addps         m4, m4, m6
%if ARCH_X86_64
    addps         m9, m9, m11
    addps        m10, m10, m12
%endif
%endif ; cpuflag(fma3)
    sub            j, 64 * 4
%endmacro

; void ff_synth_filter_inner_<opt>(float *synth_buf, float synth_buf2[32],
;                                  const float window[512], float out[32],
;                                  intptr_t offset, float scale)
%macro SYNTH_FILTER 0
cglobal synth_filter_inner, 0, 6 + 4 * ARCH_X86_64, 7 + 6 * ARCH_X86_64, \
                              synth_buf, synth_buf2, window, out, off, scale
%define scale m0
%if ARCH_X86_32 || WIN64
%if cpuflag(sse2) && notcpuflag(avx)
    movd       scale, scalem
    SPLATD        m0
%else
    VBROADCASTSS  m0, scalem
%endif
; Make sure offset is in a register and not on the stack
%define OFFQ  r4q
%else
    SPLATD      xmm0
%if cpuflag(avx)
    vinsertf128   m0, m0, xmm0, 1
%endif
%define OFFQ  offq
%endif
    ; prepare inner counter limit 1
    mov          r5q, 480
    sub          r5q, offmp
    and          r5q, -64
    shl          r5q, 2
%if ARCH_X86_32 || notcpuflag(avx)
    mov         OFFQ, r5q
%define i        r5q
    mov            i, 16 * 4 - (ARCH_X86_64 + 1) * mmsize  ; main loop counter
%else
%define i 0
%define OFFQ  r5q
%endif

%define buf2     synth_buf2q
%if ARCH_X86_32
    mov         buf2, synth_buf2mp
%endif
.mainloop:
    ; m1 = a  m2 = b  m3 = c  m4 = d
    SETZERO       m3
    SETZERO       m4
    mova          m1, [buf2 + i]
    mova          m2, [buf2 + i + 16 * 4]
%if ARCH_X86_32
%define ptr1     r0q
%define ptr2     r1q
%define win      r2q
%define j        r3q
    mov          win, windowm
    mov         ptr1, synth_bufm
%if ARCH_X86_32 || notcpuflag(avx)
    add          win, i
    add         ptr1, i
%endif
%else ; ARCH_X86_64
%define ptr1     r6q
%define ptr2     r7q ; must be loaded
%define win      r8q
%define j        r9q
    SETZERO       m9
    SETZERO      m10
    mova          m7, [buf2 + i + mmsize]
    mova          m8, [buf2 + i + mmsize + 16 * 4]
    lea          win, [windowq + i]
    lea         ptr1, [synth_bufq + i]
%endif
    mov         ptr2, synth_bufmp
    ; prepare the inner loop counter
    mov            j, OFFQ
%if ARCH_X86_32 || notcpuflag(avx)
    sub         ptr2, i
%endif
.loop1:
    INNER_LOOP  0
    jge       .loop1

    mov            j, 448 * 4
    sub            j, OFFQ
    jz          .end
    sub         ptr1, j
    sub         ptr2, j
    add          win, OFFQ ; now at j-64, so define OFFSET
    sub            j, 64 * 4
.loop2:
    INNER_LOOP  64 * 4
    jge       .loop2

.end:
%if ARCH_X86_32
    mov         buf2, synth_buf2m ; needed for next iteration anyway
    mov         outq, outmp       ; j, which will be set again during it
%endif
    ;~ out[i]      = a * scale;
    ;~ out[i + 16] = b * scale;
    mulps         m1, m1, scale
    mulps         m2, m2, scale
%if ARCH_X86_64
    mulps         m7, m7, scale
    mulps         m8, m8, scale
%endif
    ;~ synth_buf2[i]      = c;
    ;~ synth_buf2[i + 16] = d;
    mova   [buf2 + i +  0 * 4], m3
    mova   [buf2 + i + 16 * 4], m4
%if ARCH_X86_64
    mova   [buf2 + i +  0 * 4 + mmsize], m9
    mova   [buf2 + i + 16 * 4 + mmsize], m10
%endif
    ;~ out[i]      = a;
    ;~ out[i + 16] = a;
    mova   [outq + i +  0 * 4], m1
    mova   [outq + i + 16 * 4], m2
%if ARCH_X86_64
    mova   [outq + i +  0 * 4 + mmsize], m7
    mova   [outq + i + 16 * 4 + mmsize], m8
%endif
%if ARCH_X86_32 || notcpuflag(avx)
    sub            i, (ARCH_X86_64 + 1) * mmsize
    jge    .mainloop
%endif
    RET
%endmacro

%if ARCH_X86_32
INIT_XMM sse
SYNTH_FILTER
%endif
INIT_XMM sse2
SYNTH_FILTER
INIT_YMM avx
SYNTH_FILTER
INIT_YMM fma3
SYNTH_FILTER
