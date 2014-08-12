;******************************************************************************
;* FLAC DSP functions
;*
;* Copyright (c) 2014 James Darnley <james.darnley@gmail.com>
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License as published by
;* the Free Software Foundation; either version 2 of the License, or
;* (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;* GNU General Public License for more details.
;*
;* You should have received a copy of the GNU General Public License along
;* with FFmpeg; if not, write to the Free Software Foundation, Inc.,
;* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_TEXT

INIT_XMM sse4
%if ARCH_X86_64
    cglobal flac_enc_lpc_16, 5, 7, 8, 0, res, smp, len, order, coefs
    DECLARE_REG_TMP 5, 6
    %define length r2d

    movsxd orderq, orderd
%else
    cglobal flac_enc_lpc_16, 5, 6, 8, 0, res, smp, len, order, coefs
    DECLARE_REG_TMP 2, 5
    %define length r2mp
%endif

; Here we assume that the maximum order value is 32.  This means that we only
; need to copy a maximum of 32 samples.  Therefore we let the preprocessor
; unroll this loop and copy all 32.
%assign iter 0
%rep 32/(mmsize/4)
    movu  m0,         [smpq+iter]
    movu [resq+iter],  m0
    %assign iter iter+mmsize
%endrep

lea  resq,   [resq+orderq*4]
lea  smpq,   [smpq+orderq*4]
lea  coefsq, [coefsq+orderq*4]
sub  length,  orderd
movd m3,      r5m
neg  orderq

%define posj t0q
%define negj t1q

.looplen:
    pxor m0,   m0
    pxor m4,   m4
    pxor m6,   m6
    mov  posj, orderq
    xor  negj, negj

    .looporder:
        movd   m2, [coefsq+posj*4] ; c = coefs[j]
        SPLATD m2
        movu   m1, [smpq+negj*4-4] ; s = smp[i-j-1]
        movu   m5, [smpq+negj*4-4+mmsize]
        movu   m7, [smpq+negj*4-4+mmsize*2]
        pmulld m1,  m2
        pmulld m5,  m2
        pmulld m7,  m2
        paddd  m0,  m1             ; p += c * s
        paddd  m4,  m5
        paddd  m6,  m7

        dec    negj
        inc    posj
    jnz .looporder

    psrad  m0,     m3              ; p >>= shift
    psrad  m4,     m3
    psrad  m6,     m3
    movu   m1,    [smpq]
    movu   m5,    [smpq+mmsize]
    movu   m7,    [smpq+mmsize*2]
    psubd  m1,     m0              ; smp[i] - p
    psubd  m5,     m4
    psubd  m7,     m6
    movu  [resq],  m1              ; res[i] = smp[i] - (p >> shift)
    movu  [resq+mmsize], m5
    movu  [resq+mmsize*2], m7

    add resq,    3*mmsize
    add smpq,    3*mmsize
    sub length, (3*mmsize)/4
jg .looplen
RET
