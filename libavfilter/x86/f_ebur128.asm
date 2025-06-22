;*****************************************************************************
;* x86-optimized functions for ebur128 filter
;*
;* Copyright (C) 2025 Niklas Haas
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
;*****************************************************************************

%include "libavutil/x86/x86util.asm"

struc Biquad
    .b0 resq 1
    .b1 resq 1
    .b2 resq 1
    .a1 resq 1
    .a2 resq 1
endstruc

struc DSP
    .pre resq 5
    .rlb resq 5
    .y resq 1
    .z resq 1
endstruc

SECTION_RODATA

abs_mask: dq 0x7FFFFFFFFFFFFFFF

SECTION .text

%macro MOVNQ 3 ; num, dst, src
%if %1 == 1
    movsd %2, %3
%else
    movupd %2, %3
%endif
%endmacro

%macro FILTER 11 ; y0, y1, y2, x, b0, b1, b2, a1, a2, samples, num_channels
    ; Y[0] := b0 * X + Y1
    ; Y[1] := b1 * X + Y2 - a1 * Y[0]
    ; Y[2] := b2 * X - a2 * Y[0]
    movsd %1, [%10 +  8]
    movsd %3, [%10 + 16]
%if %11 > 1
    movhpd %1, [%10 + 32]
    movhpd %3, [%10 + 40]
%endif

    mulpd %2, %5, %4
    addpd %1, %2

    mulpd %2, %8, %1
    subpd %3, %2
    mulpd %2, %6, %4
    addpd %2, %3

    mulpd %3, %7, %4
    mulpd %4, %9, %1
    subpd %3, %4

    movsd [%10 +  0], %1
    movsd [%10 +  8], %2
    movsd [%10 + 16], %3
%if %11 > 1
    movhpd [%10 + 24], %1
    movhpd [%10 + 32], %2
    movhpd [%10 + 40], %3
%endif
    add %10, 24 * %11
%endmacro

%macro filter_channels 1 ; num_channels
    MOVNQ %1, m3, [samplesq]
    add samplesq, 8 * %1

    FILTER m0, m1, m2, m3, m4,  m5,  m6,  m7,  m8, r7q, %1
    FILTER m3, m1, m2, m0, m9, m10, m11, m12, m13, r8q, %1

    ; update sum and cache
    mulpd m3, m3
    subpd m0, m3, [cache400q]
    subpd m1, m3, [cache3000q]
    MOVNQ %1, [cache400q],  m3
    MOVNQ %1, [cache3000q], m3
    add cache400q,  8 * %1
    add cache3000q, 8 * %1
    addpd m0, [sum400q]
    addpd m1, [sum3000q]
    MOVNQ %1, [sum400q],  m0
    MOVNQ %1, [sum3000q], m1
    add sum400q,  8 * %1
    add sum3000q, 8 * %1
%endmacro

%if ARCH_X86_64

INIT_XMM avx
cglobal ebur128_filter_channels, 7, 9, 14, dsp, samples, cache400, cache3000, sum400, sum3000, channels
    movddup m4,  [dspq + DSP.pre + Biquad.b0]
    movddup m5,  [dspq + DSP.pre + Biquad.b1]
    movddup m6,  [dspq + DSP.pre + Biquad.b2]
    movddup m7,  [dspq + DSP.pre + Biquad.a1]
    movddup m8,  [dspq + DSP.pre + Biquad.a2]

    movddup m9,  [dspq + DSP.rlb + Biquad.b0]
    movddup m10, [dspq + DSP.rlb + Biquad.b1]
    movddup m11, [dspq + DSP.rlb + Biquad.b2]
    movddup m12, [dspq + DSP.rlb + Biquad.a1]
    movddup m13, [dspq + DSP.rlb + Biquad.a2]

    mov r7q, [dspq + DSP.y]
    mov r8q, [dspq + DSP.z]

    ; handle odd channel count
    test channelsd, 1
    jnz .tail

.loop:
    filter_channels 2
    sub channelsd, 2
    jg .loop
    RET

.tail:
    filter_channels 1
    dec channelsd
    test channelsd, channelsd
    jnz .loop
    RET

cglobal ebur128_find_peak_2ch, 4, 5, 3, ch_peaks, channels, samples, nb_samples
    movddup m2, [abs_mask]
    movupd m0, [ch_peaksq]
.loop:
    movupd m1, [samplesq]
    add samplesq, mmsize
    pand m1, m2
    maxpd m0, m1
    dec nb_samplesd
    jg .loop
    movupd [ch_peaksq], m0
    shufpd m1, m0, m0, 1
    maxpd m0, m1
    movq rax, m0
    RET

%endif ; ARCH_X86_64
