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

SECTION .text

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
