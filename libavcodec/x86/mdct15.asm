;******************************************************************************
;* SIMD optimized non-power-of-two MDCT functions
;*
;* Copyright (C) 2017 Rostislav Pehlivanov <atomnuker@gmail.com>
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

%if ARCH_X86_64

SECTION_RODATA

sign_adjust_5: dd 0x00000000, 0x80000000, 0x80000000, 0x00000000

SECTION .text

%macro FFT5 3 ; %1 - in_offset, %2 - dst1 (64bit used), %3 - dst2
    VBROADCASTSD m0, [inq + %1]         ; in[ 0].re, in[ 0].im, in[ 0].re, in[ 0].im
    movsd   xm1, [inq + 1*16 +  8 + %1] ; in[ 3].re, in[ 3].im,         0,         0
    movsd   xm4, [inq + 6*16 +  0 + %1] ; in[12].re, in[12].im,         0,         0
    movhps  xm1, [inq + 3*16 +  0 + %1] ; in[ 3].re, in[ 3].im, in[ 6].re, in[ 6].im
    movhps  xm4, [inq + 4*16 +  8 + %1] ; in[12].re, in[12].im, in[ 9].re, in[ 9].im

    subps       xm2,  xm1, xm4          ; t[2].im, t[2].re, t[3].im, t[3].re
    addps       xm1,  xm4               ; t[0].re, t[0].im, t[1].re, t[1].im

    movhlps     %2,   xm1               ; t[0].re, t[1].re, t[0].im, t[1].im
    addps       %2,   xm1
    addps       %2,   xm0               ; DC[0].re, DC[0].im, junk...
    movlhps     %2,   %2                ; DC[0].re, DC[0].im, DC[0].re, DC[0].im

    shufps      xm3,  xm1, xm2, q0110   ; t[0].re, t[0].im, t[2].re, t[2].im
    shufps      xm1,  xm2, q2332        ; t[1].re, t[1].im, t[3].re, t[3].im

    mulps       xm%3, xm1, xm5
    mulps       xm4,  xm3, xm6
    mulps       xm1,  xm6

    xorps       xm1,  xm7
    mulps       xm3,  xm5
    addsubps    xm3,  xm1               ; t[0].re, t[0].im, t[2].re, t[2].im
    subps       xm%3, xm4               ; t[4].re, t[4].im, t[5].re, t[5].im

    movhlps     xm2, xm%3, xm3          ; t[2].re, t[2].im, t[5].re, t[5].im
    movlhps     xm3, xm%3               ; t[0].re, t[0].im, t[4].re, t[4].im

    xorps       xm2,  xm7
    addps       xm%3, xm2, xm3
    subps       xm3,  xm2

    shufps      xm3,  xm3, q1032
    vinsertf128 m%3,  m%3, xm3, 1       ; All ACs (tmp[1] through to tmp[4])
    addps       m%3,  m%3,  m0          ; Finally offset with DCs
%endmacro

%macro BUTTERFLIES_DC 2 ; %1 - exptab_offset, %2 - out
    mulps xm0,  xm9, [exptabq + %1 + 16*0]
    mulps xm1, xm10, [exptabq + %1 + 16*1]

    haddps  xm0,  xm1
    movhlps xm1,  xm0                   ; t[0].re, t[1].re, t[0].im, t[1].im

    addps   xm0,  xm1
    addps   xm0,  xm8

    movsd [%2q], xm0
%endmacro

%macro BUTTERFLIES_AC 2 ; exptab, exptab_offset, src1, src2, src3, out (uses m0-m3)
    mulps  m0, m12, [exptabq + 64*0 + 0*mmsize + %1]
    mulps  m1, m12, [exptabq + 64*0 + 1*mmsize + %1]
    mulps  m2, m13, [exptabq + 64*1 + 0*mmsize + %1]
    mulps  m3, m13, [exptabq + 64*1 + 1*mmsize + %1]

    addps  m0, m0, m2
    addps  m1, m1, m3
    addps  m0, m0, m11

    shufps m1, m1, m1, q2301
    addps  m0, m0, m1

    vextractf128 xm1, m0, 1

    movlps [%2q + strideq*1], xm0
    movhps [%2q + strideq*2], xm0
    movlps [%2q +  stride3q], xm1
    movhps [%2q + strideq*4], xm1
%endmacro

;*****************************************************************************************
;void ff_fft15_avx(FFTComplex *out, FFTComplex *in, FFTComplex *exptab, ptrdiff_t stride);
;*****************************************************************************************
INIT_YMM avx
cglobal fft15, 4, 6, 14, out, in, exptab, stride, stride3, stride5
%define out0q inq
    shl strideq, 3

    movaps xm5, [exptabq + 480 + 16*0]
    movaps xm6, [exptabq + 480 + 16*1]
    movaps xm7, [sign_adjust_5]

    FFT5  0,  xm8, 11
    FFT5  8,  xm9, 12
    FFT5 16, xm10, 13

    lea stride3q, [strideq + strideq*2]
    lea stride5q, [strideq + strideq*4]

    mov out0q, outq

    BUTTERFLIES_DC (8*6 + 4*0)*2*4, out0
    lea outq, [out0q + stride5q*1]
    BUTTERFLIES_DC (8*6 + 4*1)*2*4, out
    lea outq, [out0q + stride5q*2]
    BUTTERFLIES_DC (8*6 + 4*2)*2*4, out

    BUTTERFLIES_AC (8*0)*2*4, out0
    lea outq, [out0q + stride5q*1]
    BUTTERFLIES_AC (8*2)*2*4, out
    lea outq, [out0q + stride5q*2]
    BUTTERFLIES_AC (8*4)*2*4, out

    RET

%endif
