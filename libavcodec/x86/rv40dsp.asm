;******************************************************************************
;* MMX/SSE2-optimized functions for the RV40 decoder
;* Copyright (c) 2010 Ronald S. Bultje <rsbultje@gmail.com>
;* Copyright (c) 2010 Jason Garrett-Glaser <darkshikari@gmail.com>
;* Copyright (C) 2012 Christophe Gisquet <christophe.gisquet@gmail.com>
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

align 16
pw_1024:   times 8 dw 1 << (16 - 6) ; pw_1024

sixtap_filter_hb_m:  times 8 db   1, -5
                     times 8 db  52, 20
                     ; multiplied by 2 to have the same shift
                     times 8 db   2, -10
                     times 8 db  40,  40
                     ; back to normal
                     times 8 db   1, -5
                     times 8 db  20, 52

sixtap_filter_v_m:   times 8 dw   1
                     times 8 dw  -5
                     times 8 dw  52
                     times 8 dw  20
                     ; multiplied by 2 to have the same shift
                     times 8 dw   2
                     times 8 dw -10
                     times 8 dw  40
                     times 8 dw  40
                     ; back to normal
                     times 8 dw   1
                     times 8 dw  -5
                     times 8 dw  20
                     times 8 dw  52

%ifdef PIC
%define sixtap_filter_hw   picregq
%define sixtap_filter_hb   picregq
%define sixtap_filter_v    picregq
%define npicregs 1
%else
%define sixtap_filter_hw   sixtap_filter_hw_m
%define sixtap_filter_hb   sixtap_filter_hb_m
%define sixtap_filter_v    sixtap_filter_v_m
%define npicregs 0
%endif

filter_h6_shuf1: db 0, 1, 1, 2, 2, 3, 3, 4, 4, 5,  5, 6,  6,  7,  7,  8
filter_h6_shuf2: db 2, 3, 3, 4, 4, 5, 5, 6, 6, 7,  7, 8,  8,  9,  9, 10
filter_h6_shuf3: db 5, 4, 6, 5, 7, 6, 8, 7, 9, 8, 10, 9, 11, 10, 12, 11

cextern  pw_32
cextern  pw_16
cextern  pw_512

SECTION .text

;-----------------------------------------------------------------------------
; subpel MC functions:
;
; void [put|rv40]_rv40_qpel_[h|v]_<opt>(uint8_t *dst, int deststride,
;                                       uint8_t *src, int srcstride,
;                                       int len, int m);
;----------------------------------------------------------------------
%macro LOAD  2
%if WIN64
   movsxd   %1q, %1d
%endif
%ifdef PIC
   add      %1q, picregq
%else
   add      %1q, %2
%endif
%endmacro

%macro STORE 3
%ifidn %3, avg
    movh      %2, [dstq]
%endif
    packuswb  %1, %1
%ifidn %3, avg
%if cpuflag(3dnow)
    pavgusb   %1, %2
%else
    pavgb     %1, %2
%endif
%endif
    movh  [dstq], %1
%endmacro

%macro FILTER_V 1
cglobal %1_rv40_qpel_v, 6,6+npicregs,12, dst, dststride, src, srcstride, height, my, picreg
%ifdef PIC
    lea  picregq, [sixtap_filter_v_m]
%endif
    pxor      m7, m7
    LOAD      my, sixtap_filter_v

    ; read 5 lines
    sub     srcq, srcstrideq
    sub     srcq, srcstrideq
    movh      m0, [srcq]
    movh      m1, [srcq+srcstrideq]
    movh      m2, [srcq+srcstrideq*2]
    lea     srcq, [srcq+srcstrideq*2]
    add     srcq, srcstrideq
    movh      m3, [srcq]
    movh      m4, [srcq+srcstrideq]
    punpcklbw m0, m7
    punpcklbw m1, m7
    punpcklbw m2, m7
    punpcklbw m3, m7
    punpcklbw m4, m7

%ifdef m8
    mova      m8, [myq+ 0]
    mova      m9, [myq+16]
    mova     m10, [myq+32]
    mova     m11, [myq+48]
%define COEFF05  m8
%define COEFF14  m9
%define COEFF2   m10
%define COEFF3   m11
%else
%define COEFF05  [myq+ 0]
%define COEFF14  [myq+16]
%define COEFF2   [myq+32]
%define COEFF3   [myq+48]
%endif
.nextrow:
    mova      m6, m1
    movh      m5, [srcq+2*srcstrideq]      ; read new row
    paddw     m6, m4
    punpcklbw m5, m7
    pmullw    m6, COEFF14
    paddw     m0, m5
    pmullw    m0, COEFF05
    paddw     m6, m0
    mova      m0, m1
    paddw     m6, [pw_32]
    mova      m1, m2
    pmullw    m2, COEFF2
    paddw     m6, m2
    mova      m2, m3
    pmullw    m3, COEFF3
    paddw     m6, m3

    ; round/clip/store
    mova      m3, m4
    psraw     m6, 6
    mova      m4, m5
    STORE     m6, m5, %1

    ; go to next line
    add     dstq, dststrideq
    add     srcq, srcstrideq
    dec  heightd                           ; next row
    jg .nextrow
    REP_RET
%endmacro

%macro FILTER_H  1
cglobal %1_rv40_qpel_h, 6, 6+npicregs, 12, dst, dststride, src, srcstride, height, mx, picreg
%ifdef PIC
    lea  picregq, [sixtap_filter_v_m]
%endif
    pxor      m7, m7
    LOAD      mx, sixtap_filter_v
    mova      m6, [pw_32]
%ifdef m8
    mova      m8, [mxq+ 0]
    mova      m9, [mxq+16]
    mova     m10, [mxq+32]
    mova     m11, [mxq+48]
%define COEFF05  m8
%define COEFF14  m9
%define COEFF2   m10
%define COEFF3   m11
%else
%define COEFF05  [mxq+ 0]
%define COEFF14  [mxq+16]
%define COEFF2   [mxq+32]
%define COEFF3   [mxq+48]
%endif
.nextrow:
    movq      m0, [srcq-2]
    movq      m5, [srcq+3]
    movq      m1, [srcq-1]
    movq      m4, [srcq+2]
    punpcklbw m0, m7
    punpcklbw m5, m7
    punpcklbw m1, m7
    punpcklbw m4, m7
    movq      m2, [srcq-0]
    movq      m3, [srcq+1]
    paddw     m0, m5
    paddw     m1, m4
    punpcklbw m2, m7
    punpcklbw m3, m7
    pmullw    m0, COEFF05
    pmullw    m1, COEFF14
    pmullw    m2, COEFF2
    pmullw    m3, COEFF3
    paddw     m0, m6
    paddw     m1, m2
    paddw     m0, m3
    paddw     m0, m1
    psraw     m0, 6
    STORE     m0, m1, %1

    ; go to next line
    add     dstq, dststrideq
    add     srcq, srcstrideq
    dec  heightd            ; next row
    jg .nextrow
    REP_RET
%endmacro

%if ARCH_X86_32
INIT_MMX  mmx
FILTER_V  put
FILTER_H  put

INIT_MMX  mmxext
FILTER_V  avg
FILTER_H  avg

INIT_MMX  3dnow
FILTER_V  avg
FILTER_H  avg
%endif

INIT_XMM  sse2
FILTER_H  put
FILTER_H  avg
FILTER_V  put
FILTER_V  avg

%macro FILTER_SSSE3 1
cglobal %1_rv40_qpel_v, 6,6+npicregs,8, dst, dststride, src, srcstride, height, my, picreg
%ifdef PIC
    lea  picregq, [sixtap_filter_hb_m]
%endif

    ; read 5 lines
    sub     srcq, srcstrideq
    LOAD      my, sixtap_filter_hb
    sub     srcq, srcstrideq
    movh      m0, [srcq]
    movh      m1, [srcq+srcstrideq]
    movh      m2, [srcq+srcstrideq*2]
    lea     srcq, [srcq+srcstrideq*2]
    add     srcq, srcstrideq
    mova      m5, [myq]
    movh      m3, [srcq]
    movh      m4, [srcq+srcstrideq]
    lea     srcq, [srcq+2*srcstrideq]

.nextrow:
    mova      m6, m2
    punpcklbw m0, m1
    punpcklbw m6, m3
    pmaddubsw m0, m5
    pmaddubsw m6, [myq+16]
    movh      m7, [srcq]      ; read new row
    paddw     m6, m0
    mova      m0, m1
    mova      m1, m2
    mova      m2, m3
    mova      m3, m4
    mova      m4, m7
    punpcklbw m7, m3
    pmaddubsw m7, m5
    paddw     m6, m7
    pmulhrsw  m6, [pw_512]
    STORE     m6, m7, %1

    ; go to next line
    add     dstq, dststrideq
    add     srcq, srcstrideq
    dec       heightd                          ; next row
    jg       .nextrow
    REP_RET

cglobal %1_rv40_qpel_h, 6,6+npicregs,8, dst, dststride, src, srcstride, height, mx, picreg
%ifdef PIC
    lea  picregq, [sixtap_filter_hb_m]
%endif
    mova      m3, [filter_h6_shuf2]
    mova      m4, [filter_h6_shuf3]
    LOAD      mx, sixtap_filter_hb
    mova      m5, [mxq] ; set up 6tap filter in bytes
    mova      m6, [mxq+16]
    mova      m7, [filter_h6_shuf1]

.nextrow:
    movu      m0, [srcq-2]
    mova      m1, m0
    mova      m2, m0
    pshufb    m0, m7
    pshufb    m1, m3
    pshufb    m2, m4
    pmaddubsw m0, m5
    pmaddubsw m1, m6
    pmaddubsw m2, m5
    paddw     m0, m1
    paddw     m0, m2
    pmulhrsw  m0, [pw_512]
    STORE     m0, m1, %1

    ; go to next line
    add     dstq, dststrideq
    add     srcq, srcstrideq
    dec  heightd            ; next row
    jg .nextrow
    REP_RET
%endmacro

INIT_XMM ssse3
FILTER_SSSE3  put
FILTER_SSSE3  avg

; %1=5bits weights?, %2=dst %3=src1 %4=src3 %5=stride if sse2
%macro RV40_WCORE  4-5
    movh       m4, [%3 + r6 + 0]
    movh       m5, [%4 + r6 + 0]
%if %0 == 4
%define OFFSET r6 + mmsize / 2
%else
    ; 8x8 block and sse2, stride was provided
%define OFFSET r6
    add        r6, r5
%endif
    movh       m6, [%3 + OFFSET]
    movh       m7, [%4 + OFFSET]

%if %1 == 0
    ; 14bits weights
    punpcklbw  m4, m0
    punpcklbw  m5, m0
    punpcklbw  m6, m0
    punpcklbw  m7, m0

    psllw      m4, 7
    psllw      m5, 7
    psllw      m6, 7
    psllw      m7, 7
    pmulhw     m4, m3
    pmulhw     m5, m2
    pmulhw     m6, m3
    pmulhw     m7, m2

    paddw      m4, m5
    paddw      m6, m7
%else
    ; 5bits weights
%if cpuflag(ssse3)
    punpcklbw  m4, m5
    punpcklbw  m6, m7

    pmaddubsw  m4, m3
    pmaddubsw  m6, m3
%else
    punpcklbw  m4, m0
    punpcklbw  m5, m0
    punpcklbw  m6, m0
    punpcklbw  m7, m0

    pmullw     m4, m3
    pmullw     m5, m2
    pmullw     m6, m3
    pmullw     m7, m2
    paddw      m4, m5
    paddw      m6, m7
%endif

%endif

    ; bias and shift down
%if cpuflag(ssse3)
    pmulhrsw   m4, m1
    pmulhrsw   m6, m1
%else
    paddw      m4, m1
    paddw      m6, m1
    psrlw      m4, 5
    psrlw      m6, 5
%endif

    packuswb   m4, m6
%if %0 == 5
    ; Only called for 8x8 blocks and sse2
    sub        r6, r5
    movh       [%2 + r6], m4
    add        r6, r5
    movhps     [%2 + r6], m4
%else
    mova       [%2 + r6], m4
%endif
%endmacro


%macro MAIN_LOOP   2
%if mmsize == 8
    RV40_WCORE %2, r0, r1, r2
%if %1 == 16
    RV40_WCORE %2, r0 + 8, r1 + 8, r2 + 8
%endif

    ; Prepare for next loop
    add        r6, r5
%else
%ifidn %1, 8
    RV40_WCORE %2, r0, r1, r2, r5
    ; Prepare 2 next lines
    add        r6, r5
%else
    RV40_WCORE %2, r0, r1, r2
    ; Prepare single next line
    add        r6, r5
%endif
%endif

%endmacro

; rv40_weight_func_%1(uint8_t *dst, uint8_t *src1, uint8_t *src2, int w1, int w2, int stride)
; %1=size  %2=num of xmm regs
; The weights are FP0.14 notation of fractions depending on pts.
; For timebases without rounding error (i.e. PAL), the fractions
; can be simplified, and several operations can be avoided.
; Therefore, we check here whether they are multiples of 2^9 for
; those simplifications to occur.
%macro RV40_WEIGHT  3
cglobal rv40_weight_func_%1_%2, 6, 7, 8
%if cpuflag(ssse3)
    mova       m1, [pw_1024]
%else
    mova       m1, [pw_16]
%endif
    pxor       m0, m0
    ; Set loop counter and increments
    mov        r6, r5
    shl        r6, %3
    add        r0, r6
    add        r1, r6
    add        r2, r6
    neg        r6

    movd       m2, r3d
    movd       m3, r4d
%ifidn %1,rnd
%define  RND   0
    SPLATW     m2, m2
%else
%define  RND   1
%if cpuflag(ssse3)
    punpcklbw  m3, m2
%else
    SPLATW     m2, m2
%endif
%endif
    SPLATW     m3, m3

.loop:
    MAIN_LOOP  %2, RND
    jnz        .loop
    REP_RET
%endmacro

INIT_MMX mmxext
RV40_WEIGHT   rnd,    8, 3
RV40_WEIGHT   rnd,   16, 4
RV40_WEIGHT   nornd,  8, 3
RV40_WEIGHT   nornd, 16, 4

INIT_XMM sse2
RV40_WEIGHT   rnd,    8, 3
RV40_WEIGHT   rnd,   16, 4
RV40_WEIGHT   nornd,  8, 3
RV40_WEIGHT   nornd, 16, 4

INIT_XMM ssse3
RV40_WEIGHT   rnd,    8, 3
RV40_WEIGHT   rnd,   16, 4
RV40_WEIGHT   nornd,  8, 3
RV40_WEIGHT   nornd, 16, 4
