;******************************************************************************
;* VC1 loopfilter optimizations
;* Copyright (c) 2009 David Conrad
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

cextern pw_4
cextern pw_5

SECTION .text

; dst_low, dst_high (src), zero
; zero-extends one vector from 8 to 16 bits
%macro UNPACK_8TO16 4
    mova      m%2, m%3
    punpckh%1 m%3, m%4
    punpckl%1 m%2, m%4
%endmacro

%macro STORE_4_WORDS 6
%if cpuflag(sse4)
    pextrw %1, %5, %6+0
    pextrw %2, %5, %6+1
    pextrw %3, %5, %6+2
    pextrw %4, %5, %6+3
%else
    movd  %6d, %5
    psrldq %5, 4
    mov    %1, %6w
    shr    %6, 16
    mov    %2, %6w
    movd  %6d, %5
    mov    %3, %6w
    shr    %6, 16
    mov    %4, %6w
%endif
%endmacro

; in:  p1 p0 q0 q1, clobbers p0
; out: p1 = (2*(p1 - q1) - 5*(p0 - q0) + 4) >> 3
%macro VC1_LOOP_FILTER_A0 4
    psubw  %2, %3
    psubw  %1, %4
    pmullw %2, [pw_5]
    paddw  %1, %1
    paddw  %1, [pw_4]
    psubw  %1, %2
    psraw  %1, 3
%endmacro

; in: p0 q0 a0 a1 a2
;     m0 m1 m7 m6 m5
; %1: size, %2: if set, m6 contains a1, a2
; out: m0=p0' m1=q0'
%macro VC1_FILTER 2
    PABSW   m3, m6
    movd    m6, r2d
%if %2
    movhlps m2, m3
%else
    PABSW   m2, m5
%endif
    PABSW   m4, m7
    pshuflw m6, m6, 0
    pminsw  m3, m2
    pcmpgtw m2, m4, m3   ; if (a2 < a0 || a1 < a0)
%if %1 > 4
    punpcklqdq m6, m6
%endif
    pcmpgtw m6, m4       ; if (a0 < pq)
    psubw   m4, m3
    psubw   m3, m0, m1   ; clip
    pmullw  m4, [pw_5]   ; 5*(a0 - a3)
    PABSW   m5, m3
    pand    m6, m2       ; if (min(a1,a2) < a0 && a0 < pq)
    psraw   m5, 1        ; final clip
    psraw   m4, 3        ; d = (5*(a0 - a3)) >> 3
    pxor    m2, m2
    pminsw  m4, m5       ; d = min(d, clip)
%if cpuflag(ssse3)
    ; m3 and m7 are in the -255..255 range, so that every bit in each word's
    ; upper half coincides with the sign bit. When subtracting as bytes
    ; the upper byte of every word is 0 if m3 and m7 have the same sign,
    ; 1 if m7 (a0_sign) is negative/set but m3 is not and -1 else.
    ; After the right shift by eight bits below, the value of the word
    ; coincides with the current value of the upper byte.
    psubb   m3, m7
    pcmpgtw m5, m2       ; if (clip)
%else
    psraw   m3, 15
    pcmpgtw m5, m2       ; if (clip)
    pxor    m7, m3       ; a0_sign ^ clip_sign
%endif
    pand    m6, m5       ; filt3 (C return value)

; each set of 4 pixels is not filtered if the 3rd is not
    pshuflw m5, m6, q2222
%if cpuflag(ssse3)
    psraw   m3, 8
%else
    psraw   m7, 15       ; a0_sign ^ clip_sign as mask
%endif
    pand    m4, m6
%if %1 > 4
    pshufhw m5, m5, q2222
%endif
%if cpuflag(ssse3)
    psignw  m4, m3
%else
    pxor    m4, m3
    pand    m5, m7
    psubw   m4, m3
%endif
    pand    m4, m5
    psubw   m0, m4
    paddw   m1, m4
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

    VC1_FILTER %1, 0
    mov%2 [r4+r3], m0
    mov%2 [r0],    m1
%endmacro

; 1st param: size of filter
;     NOTE: UNPACK_8TO16 this number of 8 bit numbers are in half a register
; 2nd (optional) param: temp register to use for storing words
%macro VC1_H_LOOP_FILTER 1-2
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

    VC1_FILTER %1, 0
    punpcklbw m0, m1
%if %0 > 1
    STORE_4_WORDS [r0-1], [r0+r1-1], [r0+2*r1-1], [r0+r3-1], m0, %2
    psrldq m0, 4
    STORE_4_WORDS [r4-1], [r4+r1-1], [r4+2*r1-1], [r4+r3-1], m0, %2
%else
    STORE_4_WORDS [r0-1], [r0+r1-1], [r0+2*r1-1], [r0+r3-1], m0, 0
    STORE_4_WORDS [r4-1], [r4+r1-1], [r4+2*r1-1], [r4+r3-1], m0, 4
%endif
%endmacro


%macro START_V_FILTER 0
    mov  r4, r0
    lea  r3, [4*r1]
    sub  r4, r3
    lea  r3, [r1+2*r1]
%endmacro

%macro START_H_FILTER 1
    lea  r3, [r1+2*r1]
%if %1 > 4
    lea  r4, [r0+4*r1]
%endif
%endmacro

INIT_XMM sse2
; void ff_vc1_v_loop_filter8_sse2(uint8_t *src, ptrdiff_t stride, int pq)
cglobal vc1_v_loop_filter8, 3,5,8
    START_V_FILTER
    VC1_V_LOOP_FILTER 8, q
    RET

; void ff_vc1_h_loop_filter8_sse2(uint8_t *src, ptrdiff_t stride, int pq)
cglobal vc1_h_loop_filter8, 3,5,8
    START_H_FILTER 8
    VC1_H_LOOP_FILTER 8, r2
    RET

INIT_XMM ssse3
; void ff_vc1_v_loop_filter4_ssse3(uint8_t *src, ptrdiff_t stride, int pq)
cglobal vc1_v_loop_filter4, 3,5,8
    START_V_FILTER
    VC1_V_LOOP_FILTER 4, d
    RET

; void ff_vc1_h_loop_filter4_ssse3(uint8_t *src, ptrdiff_t stride, int pq)
cglobal vc1_h_loop_filter4, 3,4,8
    START_H_FILTER 4
    movq           m0, [r0     -4]
    movq           m1, [r0+  r1-4]
    movq           m2, [r0+2*r1-4]
    movq           m3, [r0+  r3-4]
    punpcklbw      m0, m1
    punpcklbw      m2, m3
    SBUTTERFLY     wd, 0, 2, 1
    ; m0 now contains lines -4..-1, m2 0..4 as dwords
    pxor           m5, m5
    SBUTTERFLY     dq, 0, 2, 1
    ; m0 now contains lines -4 0 -3 1, m2 -2 2 -1 3
    UNPACK_8TO16   bw, 6, 0, 5
    UNPACK_8TO16   bw, 7, 2, 5
    ; m0, m2, m6, m7 contain two unpacked lines each, namely:
    ; m6: -4, 0; m0: -3, 1; m7: -2, 2; m2: -1, 3
    movhlps        m5, m0                        ; 1
    movhlps        m1, m6                        ; 0
    VC1_LOOP_FILTER_A0 m6, m0, m7, m2            ; m6: a1, a2
    mova           m0, m2
    VC1_LOOP_FILTER_A0 m7, m2, m1, m5

    VC1_FILTER      4, 1
    punpcklbw      m0, m1
    STORE_4_WORDS [r0-1], [r0+r1-1], [r0+2*r1-1], [r0+r3-1], m0, r2
    RET

; void ff_vc1_v_loop_filter8_ssse3(uint8_t *src, ptrdiff_t stride, int pq)
cglobal vc1_v_loop_filter8, 3,5,8
    START_V_FILTER
    VC1_V_LOOP_FILTER 8, q
    RET

; void ff_vc1_h_loop_filter8_ssse3(uint8_t *src, ptrdiff_t stride, int pq)
cglobal vc1_h_loop_filter8, 3,5,8
    START_H_FILTER 8
    VC1_H_LOOP_FILTER 8, r2
    RET

INIT_XMM sse4
; void ff_vc1_h_loop_filter8_sse4(uint8_t *src, ptrdiff_t stride, int pq)
cglobal vc1_h_loop_filter8, 3,5,8
    START_H_FILTER 8
    VC1_H_LOOP_FILTER 8
    RET
