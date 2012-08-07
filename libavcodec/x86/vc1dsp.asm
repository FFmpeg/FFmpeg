;******************************************************************************
;* VC1 deblocking optimizations
;* Copyright (c) 2009 David Conrad
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

%include "x86inc.asm"
%include "x86util.asm"

cextern pw_4
cextern pw_5

section .text

; dst_low, dst_high (src), zero
; zero-extends one vector from 8 to 16 bits
%macro UNPACK_8TO16 4
    mova      m%2, m%3
    punpckh%1 m%3, m%4
    punpckl%1 m%2, m%4
%endmacro

%macro STORE_4_WORDS_MMX 6
    movd  %6d, %5
%if mmsize==16
    psrldq %5, 4
%else
    psrlq  %5, 32
%endif
    mov    %1, %6w
    shr    %6, 16
    mov    %2, %6w
    movd  %6d, %5
    mov    %3, %6w
    shr    %6, 16
    mov    %4, %6w
%endmacro

%macro STORE_4_WORDS_SSE4 6
    pextrw %1, %5, %6+0
    pextrw %2, %5, %6+1
    pextrw %3, %5, %6+2
    pextrw %4, %5, %6+3
%endmacro

; in:  p1 p0 q0 q1, clobbers p0
; out: p1 = (2*(p1 - q1) - 5*(p0 - q0) + 4) >> 3
%macro VC1_LOOP_FILTER_A0 4
    psubw  %1, %4
    psubw  %2, %3
    paddw  %1, %1
    pmullw %2, [pw_5]
    psubw  %1, %2
    paddw  %1, [pw_4]
    psraw  %1, 3
%endmacro

; in: p0 q0 a0 a1 a2
;     m0 m1 m7 m6 m5
; %1: size
; out: m0=p0' m1=q0'
%macro VC1_FILTER 1
    PABSW   m4, m7
    PABSW   m3, m6
    PABSW   m2, m5
    mova    m6, m4
    pminsw  m3, m2
    pcmpgtw m6, m3  ; if (a2 < a0 || a1 < a0)
    psubw   m3, m4
    pmullw  m3, [pw_5]   ; 5*(a3 - a0)
    PABSW   m2, m3
    psraw   m2, 3   ; abs(d/8)
    pxor    m7, m3  ; d_sign ^= a0_sign

    pxor    m5, m5
    movd    m3, r2d
%if %1 > 4
    punpcklbw m3, m3
%endif
    punpcklbw m3, m5
    pcmpgtw m3, m4  ; if (a0 < pq)
    pand    m6, m3

    mova    m3, m0
    psubw   m3, m1
    PABSW   m4, m3
    psraw   m4, 1
    pxor    m3, m7  ; d_sign ^ clip_sign
    psraw   m3, 15
    pminsw  m2, m4  ; min(d, clip)
    pcmpgtw m4, m5
    pand    m6, m4  ; filt3 (C return value)

; each set of 4 pixels is not filtered if the 3rd is not
%if mmsize==16
    pshuflw m4, m6, 0xaa
%if %1 > 4
    pshufhw m4, m4, 0xaa
%endif
%else
    pshufw  m4, m6, 0xaa
%endif
    pandn   m3, m4
    pand    m2, m6
    pand    m3, m2  ; d final

    psraw   m7, 15
    pxor    m3, m7
    psubw   m3, m7
    psubw   m0, m3
    paddw   m1, m3
    packuswb m0, m0
    packuswb m1, m1
%endmacro

; 1st param: size of filter
; 2nd param: mov suffix equivalent to the filter size
%macro VC1_V_LOOP_FILTER 2
    pxor      m5, m5
    mov%2     m6, [r4]
    mov%2     m4, [r4+r1]
    mov%2     m7, [r4+2*r1]
    mov%2     m0, [r4+r3]
    punpcklbw m6, m5
    punpcklbw m4, m5
    punpcklbw m7, m5
    punpcklbw m0, m5

    VC1_LOOP_FILTER_A0 m6, m4, m7, m0
    mov%2     m1, [r0]
    mov%2     m2, [r0+r1]
    punpcklbw m1, m5
    punpcklbw m2, m5
    mova      m4, m0
    VC1_LOOP_FILTER_A0 m7, m4, m1, m2
    mov%2     m3, [r0+2*r1]
    mov%2     m4, [r0+r3]
    punpcklbw m3, m5
    punpcklbw m4, m5
    mova      m5, m1
    VC1_LOOP_FILTER_A0 m5, m2, m3, m4

    VC1_FILTER %1
    mov%2 [r4+r3], m0
    mov%2 [r0],    m1
%endmacro

; 1st param: size of filter
;     NOTE: UNPACK_8TO16 this number of 8 bit numbers are in half a register
; 2nd (optional) param: temp register to use for storing words
%macro VC1_H_LOOP_FILTER 1-2
%if %1 == 4
    movq      m0, [r0     -4]
    movq      m1, [r0+  r1-4]
    movq      m2, [r0+2*r1-4]
    movq      m3, [r0+  r3-4]
    TRANSPOSE4x4B 0, 1, 2, 3, 4
%else
    movq      m0, [r0     -4]
    movq      m4, [r0+  r1-4]
    movq      m1, [r0+2*r1-4]
    movq      m5, [r0+  r3-4]
    movq      m2, [r4     -4]
    movq      m6, [r4+  r1-4]
    movq      m3, [r4+2*r1-4]
    movq      m7, [r4+  r3-4]
    punpcklbw m0, m4
    punpcklbw m1, m5
    punpcklbw m2, m6
    punpcklbw m3, m7
    TRANSPOSE4x4W 0, 1, 2, 3, 4
%endif
    pxor      m5, m5

    UNPACK_8TO16 bw, 6, 0, 5
    UNPACK_8TO16 bw, 7, 1, 5
    VC1_LOOP_FILTER_A0 m6, m0, m7, m1
    UNPACK_8TO16 bw, 4, 2, 5
    mova    m0, m1                      ; m0 = p0
    VC1_LOOP_FILTER_A0 m7, m1, m4, m2
    UNPACK_8TO16 bw, 1, 3, 5
    mova    m5, m4
    VC1_LOOP_FILTER_A0 m5, m2, m1, m3
    SWAP 1, 4                           ; m1 = q0

    VC1_FILTER %1
    punpcklbw m0, m1
%if %0 > 1
    STORE_4_WORDS_MMX [r0-1], [r0+r1-1], [r0+2*r1-1], [r0+r3-1], m0, %2
%if %1 > 4
    psrldq m0, 4
    STORE_4_WORDS_MMX [r4-1], [r4+r1-1], [r4+2*r1-1], [r4+r3-1], m0, %2
%endif
%else
    STORE_4_WORDS_SSE4 [r0-1], [r0+r1-1], [r0+2*r1-1], [r0+r3-1], m0, 0
    STORE_4_WORDS_SSE4 [r4-1], [r4+r1-1], [r4+2*r1-1], [r4+r3-1], m0, 4
%endif
%endmacro


%macro START_V_FILTER 0
    mov  r4, r0
    lea  r3, [4*r1]
    sub  r4, r3
    lea  r3, [r1+2*r1]
    imul r2, 0x01010101
%endmacro

%macro START_H_FILTER 1
    lea  r3, [r1+2*r1]
%if %1 > 4
    lea  r4, [r0+4*r1]
%endif
    imul r2, 0x01010101
%endmacro

%macro VC1_LF_MMX 1
INIT_MMX
cglobal vc1_v_loop_filter_internal_%1
    VC1_V_LOOP_FILTER 4, d
    ret

cglobal vc1_h_loop_filter_internal_%1
    VC1_H_LOOP_FILTER 4, r4
    ret

; void ff_vc1_v_loop_filter4_mmx2(uint8_t *src, int stride, int pq)
cglobal vc1_v_loop_filter4_%1, 3,5,0
    START_V_FILTER
    call vc1_v_loop_filter_internal_%1
    RET

; void ff_vc1_h_loop_filter4_mmx2(uint8_t *src, int stride, int pq)
cglobal vc1_h_loop_filter4_%1, 3,5,0
    START_H_FILTER 4
    call vc1_h_loop_filter_internal_%1
    RET

; void ff_vc1_v_loop_filter8_mmx2(uint8_t *src, int stride, int pq)
cglobal vc1_v_loop_filter8_%1, 3,5,0
    START_V_FILTER
    call vc1_v_loop_filter_internal_%1
    add  r4, 4
    add  r0, 4
    call vc1_v_loop_filter_internal_%1
    RET

; void ff_vc1_h_loop_filter8_mmx2(uint8_t *src, int stride, int pq)
cglobal vc1_h_loop_filter8_%1, 3,5,0
    START_H_FILTER 4
    call vc1_h_loop_filter_internal_%1
    lea  r0, [r0+4*r1]
    call vc1_h_loop_filter_internal_%1
    RET
%endmacro

%define PABSW PABSW_MMX2
VC1_LF_MMX mmx2

INIT_XMM
; void ff_vc1_v_loop_filter8_sse2(uint8_t *src, int stride, int pq)
cglobal vc1_v_loop_filter8_sse2, 3,5,8
    START_V_FILTER
    VC1_V_LOOP_FILTER 8, q
    RET

; void ff_vc1_h_loop_filter8_sse2(uint8_t *src, int stride, int pq)
cglobal vc1_h_loop_filter8_sse2, 3,6,8
    START_H_FILTER 8
    VC1_H_LOOP_FILTER 8, r5
    RET

%define PABSW PABSW_SSSE3

INIT_MMX
; void ff_vc1_v_loop_filter4_ssse3(uint8_t *src, int stride, int pq)
cglobal vc1_v_loop_filter4_ssse3, 3,5,0
    START_V_FILTER
    VC1_V_LOOP_FILTER 4, d
    RET

; void ff_vc1_h_loop_filter4_ssse3(uint8_t *src, int stride, int pq)
cglobal vc1_h_loop_filter4_ssse3, 3,5,0
    START_H_FILTER 4
    VC1_H_LOOP_FILTER 4, r4
    RET

INIT_XMM
; void ff_vc1_v_loop_filter8_ssse3(uint8_t *src, int stride, int pq)
cglobal vc1_v_loop_filter8_ssse3, 3,5,8
    START_V_FILTER
    VC1_V_LOOP_FILTER 8, q
    RET

; void ff_vc1_h_loop_filter8_ssse3(uint8_t *src, int stride, int pq)
cglobal vc1_h_loop_filter8_ssse3, 3,6,8
    START_H_FILTER 8
    VC1_H_LOOP_FILTER 8, r5
    RET

; void ff_vc1_h_loop_filter8_sse4(uint8_t *src, int stride, int pq)
cglobal vc1_h_loop_filter8_sse4, 3,5,8
    START_H_FILTER 8
    VC1_H_LOOP_FILTER 8
    RET
