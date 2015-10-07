;******************************************************************************
;* ALAC DSP SIMD optimizations
;*
;* Copyright (C) 2015 James Almer
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

INIT_XMM sse4
%if ARCH_X86_64
cglobal alac_decorrelate_stereo, 2, 5, 8, buf0, len, shift, weight, buf1
%else
cglobal alac_decorrelate_stereo, 2, 3, 8, buf0, len, shift, weight
%define  buf1q  r2q
%endif
    movd    m6, shiftm
    movd    m7, weightm
    SPLATD  m7
    shl   lend, 2
    mov  buf1q, [buf0q + gprsize]
    mov  buf0q, [buf0q]
    add  buf1q, lenq
    add  buf0q, lenq
    neg  lenq

align 16
.loop:
    mova    m0, [buf0q + lenq]
    mova    m1, [buf0q + lenq + mmsize]
    mova    m2, [buf1q + lenq]
    mova    m3, [buf1q + lenq + mmsize]
    pmulld  m4, m2, m7
    pmulld  m5, m3, m7
    psrad   m4, m6
    psrad   m5, m6
    psubd   m0, m4
    psubd   m1, m5
    paddd   m2, m0
    paddd   m3, m1
    mova [buf1q + lenq], m0
    mova [buf1q + lenq + mmsize], m1
    mova [buf0q + lenq], m2
    mova [buf0q + lenq + mmsize], m3

    add   lenq, mmsize*2
    jl .loop
    RET

INIT_XMM sse2
cglobal alac_append_extra_bits_stereo, 2, 5, 5, buf0, exbuf0, buf1, exbuf1, len
    movifnidn lend, lenm
    movd      m4, r2m ; exbits
    shl     lend, 2
    mov    buf1q, [buf0q + gprsize]
    mov    buf0q, [buf0q]
    mov  exbuf1q, [exbuf0q + gprsize]
    mov  exbuf0q, [exbuf0q]
    add    buf1q, lenq
    add    buf0q, lenq
    add  exbuf1q, lenq
    add  exbuf0q, lenq
    neg lenq

align 16
.loop:
    mova      m0, [buf0q + lenq]
    mova      m1, [buf0q + lenq + mmsize]
    pslld     m0, m4
    pslld     m1, m4
    mova      m2, [buf1q + lenq]
    mova      m3, [buf1q + lenq + mmsize]
    pslld     m2, m4
    pslld     m3, m4
    por       m0, [exbuf0q + lenq]
    por       m1, [exbuf0q + lenq + mmsize]
    por       m2, [exbuf1q + lenq]
    por       m3, [exbuf1q + lenq + mmsize]
    mova [buf0q + lenq         ], m0
    mova [buf0q + lenq + mmsize], m1
    mova [buf1q + lenq         ], m2
    mova [buf1q + lenq + mmsize], m3

    add     lenq, mmsize*2
    jl .loop
    REP_RET

%if ARCH_X86_64
cglobal alac_append_extra_bits_mono, 2, 5, 3, buf, exbuf, exbits, ch, len
%else
cglobal alac_append_extra_bits_mono, 2, 3, 3, buf, exbuf, len
%define exbitsm r2m
%endif
    movifnidn lend, r4m
    movd     m2, exbitsm
    shl    lend, 2
    mov    bufq, [bufq]
    mov  exbufq, [exbufq]
    add    bufq, lenq
    add  exbufq, lenq
    neg lenq

align 16
.loop:
    mova      m0, [bufq + lenq]
    mova      m1, [bufq + lenq + mmsize]
    pslld     m0, m2
    pslld     m1, m2
    por       m0, [exbufq + lenq]
    por       m1, [exbufq + lenq + mmsize]
    mova [bufq + lenq], m0
    mova [bufq + lenq + mmsize], m1

    add     lenq, mmsize*2
    jl .loop
    REP_RET
