;******************************************************************************
;* Copyright (c) Lynne
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

SECTION_RODATA 32

one_tab: times 4 dq 1.0
seq_tab_avx2: dq 3.0, 2.0, 1.0, 0.0
sub_tab: dq -1.0, -2.0, -3.0, -4.0
add_tab_avx2: times 4 dq  4.0
dec_tab_avx2: times 4 dq -4.0
add_tab_sse2: times 2 dq  2.0
dec_tab_sse2: times 2 dq -2.0
dec_tab_scalar: times 2 dq -1.0
seq_tab_sse2: dq 1.0, 0.0

SECTION .text

%macro APPLY_WELCH_FN 0
cglobal lpc_apply_welch_window, 3, 5, 8, data, len, out, off1, off2
    cmp lenq, 0
    je .end_e
    cmp lenq, 2
    je .two
    cmp lenq, 1
    je .one

    movapd m6, [one_tab]

    movd xm1, lend
    cvtdq2pd xm1, xm1      ; len
%if cpuflag(avx2)
    vbroadcastsd m1, xm1
%else
    shufpd m1, m1, 00b
%endif

    addpd m0, m6, m6       ; 2.0
    subpd m1, m6           ; len - 1
    divpd m0, m1           ; 2.0 / (len - 1)

    mov off1q, lenq
    and off1q, 1
    je .even

    movapd m5, m0
    addpd m0, [sub_tab]

    lea off2q, [lenq*4 - mmsize/2]
    sub lenq, mmsize/4     ; avoid overwriting
    xor off1q, off1q

    cmp lenq, mmsize/4
    jl .scalar_o

%if cpuflag(avx2)
    movapd m7, [dec_tab_avx2]
%else
    movapd m7, [dec_tab_sse2]
%endif

.loop_o:
    movapd m1, m6
%if cpuflag(avx2)
    fnmaddpd m1, m0, m0, m1
    vpermpd m2, m1, q0123
%else
    mulpd m2, m0, m0
    subpd m1, m2
    shufpd m2, m1, m1, 01b
%endif

    cvtdq2pd m3, [dataq + off1q]
    cvtdq2pd m4, [dataq + off2q]

    mulpd m1, m3
    mulpd m2, m4

    movupd [outq + off1q*2], m1
    movupd [outq + off2q*2], m2

    addpd m0, m7
    add off1q, mmsize/2
    sub off2q, mmsize/2
    sub lenq, mmsize/4
    jg .loop_o

    add lend, (mmsize/4 - 1)
    cmp lend, 0
    je .end_o
    sub lenq, (mmsize/4 - 1)

.scalar_o:
    movapd xm7, [dec_tab_scalar]

    ; Set offsets
    add off2q, (mmsize/4) + 4*cpuflag(avx2)
    add lenq, mmsize/4 - 2

.loop_o_scalar:
    movapd xm1, xm6
%if cpuflag(avx2)
    fnmaddpd xm1, xm0, xm0, xm1
%else
    mulpd xm2, xm0, xm0
    subpd xm1, xm2
%endif

    cvtdq2pd xm3, [dataq + off1q]
    cvtdq2pd xm4, [dataq + off2q]

    mulpd xm3, xm1
    mulpd xm4, xm1

    movlpd [outq + off1q*2], xm3
    movlpd [outq + off2q*2], xm4

    addpd xm0, xm7

    add off1q, 4
    sub off2q, 4

    sub lenq, 2
    jg .loop_o_scalar

.end_o:
    xorpd xm3, xm3
    movlpd [outq + off1q*2], xm3
    RET

.even:
%if cpuflag(avx2)
    addpd m0, [seq_tab_avx2]
%else
    addpd m0, [seq_tab_sse2]
%endif

    mov off1d, lend
    shr off1d, 1
    movd xm1, off1d
    cvtdq2pd xm1, xm1      ; len/2
%if cpuflag(avx2)
    vbroadcastsd m1, xm1
%else
    shufpd m1, m1, 00b
%endif
    subpd m0, m1

%if cpuflag(avx2)
    movapd m7, [add_tab_avx2]
%else
    movapd m7, [add_tab_sse2]
%endif

    lea off2q, [lenq*2]
    lea off1q, [lenq*2 - mmsize/2]
    sub lenq, mmsize/4

    cmp lenq, mmsize/4
    jl .scalar_e

.loop_e:
    movapd m1, m6
%if cpuflag(avx2)
    fnmaddpd m1, m0, m0, m1
%else
    mulpd m2, m0, m0
    subpd m1, m2
%endif
%if cpuflag(avx2)
    vpermpd m2, m1, q0123
%else
    shufpd m2, m1, m1, 01b
%endif

    cvtdq2pd m3, [dataq + off1q]
    cvtdq2pd m4, [dataq + off2q]

    mulpd m1, m3
    mulpd m2, m4

    movupd [outq + off1q*2], m1
    movupd [outq + off2q*2], m2

    addpd m0, m7
    add off2q, mmsize/2
    sub off1q, mmsize/2
    sub lenq, mmsize/4
    jge .loop_e

.scalar_e:
    subpd xm0, xm7
    movapd xm7, [dec_tab_scalar]
    subpd xm0, xm7

    add off1q, (mmsize/2)
    sub off2q, (mmsize/2) - 8*cpuflag(avx2)
    add lenq, 6 + 4*cpuflag(avx2)

    addpd xm0, [sub_tab]

.loop_e_scalar:
    movapd xm1, xm6
%if cpuflag(avx2)
    fnmaddpd xm1, xm0, xm0, xm1
%else
    mulpd xm2, xm0, xm0
    subpd xm1, xm2
%endif

    cvtdq2pd xm3, [dataq + off1q]
    cvtdq2pd xm4, [dataq + off2q]

    mulpd xm3, xm1
    shufpd xm1, xm1, 00b
    mulpd xm4, xm1

    movlpd [outq + off1q*2], xm3
    movhpd [outq + off2q*2 + 8], xm4

    subpd xm0, xm7

    add off2q, 4
    sub off1q, 4
    sub lenq, 2
    jg .loop_e_scalar
    RET

.two:
    xorpd xm0, xm0
    movhpd [outq + 8], xm0
.one:
    xorpd xm0, xm0
    movhpd [outq], xm0
.end_e:
    RET
%endmacro

INIT_XMM sse2
APPLY_WELCH_FN

INIT_YMM avx2
APPLY_WELCH_FN
