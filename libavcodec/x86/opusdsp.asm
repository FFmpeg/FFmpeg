;******************************************************************************
;* Opus SIMD functions
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

SECTION .text

INIT_XMM fma3
%if UNIX64
cglobal opus_deemphasis, 4, 4, 8, out, in, weights, len
%else
cglobal opus_deemphasis, 5, 5, 8, out, in, coeff, weights, len
%endif
%if ARCH_X86_32
    VBROADCASTSS m0, coeffm
%elif WIN64
    shufps m0, m2, m2, 0
%else
    shufps m0, m0, 0
%endif

    movaps m4, [weightsq]
    VBROADCASTSS m5, m4
    shufps m6, m4, m4, q1111
    shufps m7, m4, m4, q2222

.loop:
    movaps  m1, [inq]                ; x0, x1, x2, x3

    pslldq  m2, m1, 4                ;  0, x0, x1, x2
    pslldq  m3, m1, 8                ;  0,  0, x0, x1

    fmaddps m2, m2, m5, m1           ; x + c1*x[0-2]
    pslldq  m1, 12                   ;  0,  0,  0, x0

    fmaddps m2, m3, m6, m2           ; x + c1*x[0-2] + c2*x[0-1]
    fmaddps m1, m1, m7, m2           ; x + c1*x[0-2] + c2*x[0-1] + c3*x[0]
    fmaddps m0, m0, m4, m1           ; x + c1*x[0-2] + c2*x[0-1] + c3*x[0] + c*s

    movaps [outq], m0
    shufps m0, m0, q3333             ; new state

    add inq,  mmsize
    add outq, mmsize
    sub lend, mmsize >> 2
    jg .loop

%if ARCH_X86_64 == 0
    movss r0m, m0
    fld dword r0m
%endif
    RET


INIT_XMM fma3
cglobal opus_postfilter, 4, 4, 8, data, period, gains, len
    VBROADCASTSS m0, [gainsq + 0]
    VBROADCASTSS m1, [gainsq + 4]
    VBROADCASTSS m2, [gainsq + 8]

    shl periodd, 2
    add periodq, 8
    neg periodq

    movups  m3, [dataq + periodq]
    mulps   m3, m2

.loop:
    movups  m4, [dataq + periodq +  4]
    movups  m5, [dataq + periodq +  8]
    movups  m6, [dataq + periodq + 12]
    movups  m7, [dataq + periodq + 16]

    fmaddps m3, m7, m2, m3
    addps   m6, m4

    fmaddps m5, m5, m0, [dataq]
    fmaddps m6, m6, m1, m3

    addps   m5, m6
    mulps   m3, m7, m2

    movaps  [dataq], m5

    add dataq, mmsize
    sub lend,  mmsize >> 2
    jg .loop

    RET
