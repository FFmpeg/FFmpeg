;******************************************************************************
;* x86-optimized input routines; does shuffling of packed
;* YUV formats into individual planes, and converts RGB
;* into YUV planes also.
;* Copyright (c) 2012 Ronald S. Bultje <rsbultje@gmail.com>
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

%define RY 0x20DE
%define GY 0x4087
%define BY 0x0C88
%define RU 0xECFF
%define GU 0xDAC8
%define BU 0x3838
%define RV 0x3838
%define GV 0xD0E3
%define BV 0xF6E4

rgb_Yrnd:        times 4 dd 0x80100        ;  16.5 << 15
rgb_UVrnd:       times 4 dd 0x400100       ; 128.5 << 15
%define bgr_Ycoeff_12x4 16*4 + 16* 0 + tableq
%define bgr_Ycoeff_3x56 16*4 + 16* 1 + tableq
%define rgb_Ycoeff_12x4 16*4 + 16* 2 + tableq
%define rgb_Ycoeff_3x56 16*4 + 16* 3 + tableq
%define bgr_Ucoeff_12x4 16*4 + 16* 4 + tableq
%define bgr_Ucoeff_3x56 16*4 + 16* 5 + tableq
%define rgb_Ucoeff_12x4 16*4 + 16* 6 + tableq
%define rgb_Ucoeff_3x56 16*4 + 16* 7 + tableq
%define bgr_Vcoeff_12x4 16*4 + 16* 8 + tableq
%define bgr_Vcoeff_3x56 16*4 + 16* 9 + tableq
%define rgb_Vcoeff_12x4 16*4 + 16*10 + tableq
%define rgb_Vcoeff_3x56 16*4 + 16*11 + tableq

%define rgba_Ycoeff_rb 16*4 + 16*12 + tableq
%define rgba_Ycoeff_br 16*4 + 16*13 + tableq
%define rgba_Ycoeff_ga 16*4 + 16*14 + tableq
%define rgba_Ycoeff_ag 16*4 + 16*15 + tableq
%define rgba_Ucoeff_rb 16*4 + 16*16 + tableq
%define rgba_Ucoeff_br 16*4 + 16*17 + tableq
%define rgba_Ucoeff_ga 16*4 + 16*18 + tableq
%define rgba_Ucoeff_ag 16*4 + 16*19 + tableq
%define rgba_Vcoeff_rb 16*4 + 16*20 + tableq
%define rgba_Vcoeff_br 16*4 + 16*21 + tableq
%define rgba_Vcoeff_ga 16*4 + 16*22 + tableq
%define rgba_Vcoeff_ag 16*4 + 16*23 + tableq

; bgr_Ycoeff_12x4: times 2 dw BY, GY, 0, BY
; bgr_Ycoeff_3x56: times 2 dw RY, 0, GY, RY
; rgb_Ycoeff_12x4: times 2 dw RY, GY, 0, RY
; rgb_Ycoeff_3x56: times 2 dw BY, 0, GY, BY
; bgr_Ucoeff_12x4: times 2 dw BU, GU, 0, BU
; bgr_Ucoeff_3x56: times 2 dw RU, 0, GU, RU
; rgb_Ucoeff_12x4: times 2 dw RU, GU, 0, RU
; rgb_Ucoeff_3x56: times 2 dw BU, 0, GU, BU
; bgr_Vcoeff_12x4: times 2 dw BV, GV, 0, BV
; bgr_Vcoeff_3x56: times 2 dw RV, 0, GV, RV
; rgb_Vcoeff_12x4: times 2 dw RV, GV, 0, RV
; rgb_Vcoeff_3x56: times 2 dw BV, 0, GV, BV

; rgba_Ycoeff_rb:  times 4 dw RY, BY
; rgba_Ycoeff_br:  times 4 dw BY, RY
; rgba_Ycoeff_ga:  times 4 dw GY, 0
; rgba_Ycoeff_ag:  times 4 dw 0,  GY
; rgba_Ucoeff_rb:  times 4 dw RU, BU
; rgba_Ucoeff_br:  times 4 dw BU, RU
; rgba_Ucoeff_ga:  times 4 dw GU, 0
; rgba_Ucoeff_ag:  times 4 dw 0,  GU
; rgba_Vcoeff_rb:  times 4 dw RV, BV
; rgba_Vcoeff_br:  times 4 dw BV, RV
; rgba_Vcoeff_ga:  times 4 dw GV, 0
; rgba_Vcoeff_ag:  times 4 dw 0,  GV

shuf_rgb_12x4:   db 0, 0x80, 1, 0x80,  2, 0x80,  3, 0x80, \
                    6, 0x80, 7, 0x80,  8, 0x80,  9, 0x80
shuf_rgb_3x56:   db 2, 0x80, 3, 0x80,  4, 0x80,  5, 0x80, \
                    8, 0x80, 9, 0x80, 10, 0x80, 11, 0x80

SECTION .text

;-----------------------------------------------------------------------------
; RGB to Y/UV.
;
; void <fmt>ToY_<opt>(uint8_t *dst, const uint8_t *src, int w);
; and
; void <fmt>toUV_<opt>(uint8_t *dstU, uint8_t *dstV, const uint8_t *src,
;                      const uint8_t *unused, int w);
;-----------------------------------------------------------------------------

; %1 = nr. of XMM registers
; %2 = rgb or bgr
%macro RGB24_TO_Y_FN 2-3
cglobal %2 %+ 24ToY, 6, 6, %1, dst, src, u1, u2, w, table
%if mmsize == 8
    mova           m5, [%2_Ycoeff_12x4]
    mova           m6, [%2_Ycoeff_3x56]
%define coeff1 m5
%define coeff2 m6
%elif ARCH_X86_64
    mova           m8, [%2_Ycoeff_12x4]
    mova           m9, [%2_Ycoeff_3x56]
%define coeff1 m8
%define coeff2 m9
%else ; x86-32 && mmsize == 16
%define coeff1 [%2_Ycoeff_12x4]
%define coeff2 [%2_Ycoeff_3x56]
%endif ; x86-32/64 && mmsize == 8/16
%if (ARCH_X86_64 || mmsize == 8) && %0 == 3
    jmp mangle(private_prefix %+ _ %+ %3 %+ 24ToY %+ SUFFIX).body
%else ; (ARCH_X86_64 && %0 == 3) || mmsize == 8
.body:
%if cpuflag(ssse3)
    mova           m7, [shuf_rgb_12x4]
%define shuf_rgb1 m7
%if ARCH_X86_64
    mova          m10, [shuf_rgb_3x56]
%define shuf_rgb2 m10
%else ; x86-32
%define shuf_rgb2 [shuf_rgb_3x56]
%endif ; x86-32/64
%endif ; cpuflag(ssse3)
%if ARCH_X86_64
    movsxd         wq, wd
%endif
    add            wq, wq
    add          dstq, wq
    neg            wq
%if notcpuflag(ssse3)
    pxor           m7, m7
%endif ; !cpuflag(ssse3)
    mova           m4, [rgb_Yrnd]
.loop:
%if cpuflag(ssse3)
    movu           m0, [srcq+0]           ; (byte) { Bx, Gx, Rx }[0-3]
    movu           m2, [srcq+12]          ; (byte) { Bx, Gx, Rx }[4-7]
    pshufb         m1, m0, shuf_rgb2      ; (word) { R0, B1, G1, R1, R2, B3, G3, R3 }
    pshufb         m0, shuf_rgb1          ; (word) { B0, G0, R0, B1, B2, G2, R2, B3 }
    pshufb         m3, m2, shuf_rgb2      ; (word) { R4, B5, G5, R5, R6, B7, G7, R7 }
    pshufb         m2, shuf_rgb1          ; (word) { B4, G4, R4, B5, B6, G6, R6, B7 }
%else ; !cpuflag(ssse3)
    movd           m0, [srcq+0]           ; (byte) { B0, G0, R0, B1 }
    movd           m1, [srcq+2]           ; (byte) { R0, B1, G1, R1 }
    movd           m2, [srcq+6]           ; (byte) { B2, G2, R2, B3 }
    movd           m3, [srcq+8]           ; (byte) { R2, B3, G3, R3 }
%if mmsize == 16 ; i.e. sse2
    punpckldq      m0, m2                 ; (byte) { B0, G0, R0, B1, B2, G2, R2, B3 }
    punpckldq      m1, m3                 ; (byte) { R0, B1, G1, R1, R2, B3, G3, R3 }
    movd           m2, [srcq+12]          ; (byte) { B4, G4, R4, B5 }
    movd           m3, [srcq+14]          ; (byte) { R4, B5, G5, R5 }
    movd           m5, [srcq+18]          ; (byte) { B6, G6, R6, B7 }
    movd           m6, [srcq+20]          ; (byte) { R6, B7, G7, R7 }
    punpckldq      m2, m5                 ; (byte) { B4, G4, R4, B5, B6, G6, R6, B7 }
    punpckldq      m3, m6                 ; (byte) { R4, B5, G5, R5, R6, B7, G7, R7 }
%endif ; mmsize == 16
    punpcklbw      m0, m7                 ; (word) { B0, G0, R0, B1, B2, G2, R2, B3 }
    punpcklbw      m1, m7                 ; (word) { R0, B1, G1, R1, R2, B3, G3, R3 }
    punpcklbw      m2, m7                 ; (word) { B4, G4, R4, B5, B6, G6, R6, B7 }
    punpcklbw      m3, m7                 ; (word) { R4, B5, G5, R5, R6, B7, G7, R7 }
%endif ; cpuflag(ssse3)
    add          srcq, 3 * mmsize / 2
    pmaddwd        m0, coeff1             ; (dword) { B0*BY + G0*GY, B1*BY, B2*BY + G2*GY, B3*BY }
    pmaddwd        m1, coeff2             ; (dword) { R0*RY, G1+GY + R1*RY, R2*RY, G3+GY + R3*RY }
    pmaddwd        m2, coeff1             ; (dword) { B4*BY + G4*GY, B5*BY, B6*BY + G6*GY, B7*BY }
    pmaddwd        m3, coeff2             ; (dword) { R4*RY, G5+GY + R5*RY, R6*RY, G7+GY + R7*RY }
    paddd          m0, m1                 ; (dword) { Bx*BY + Gx*GY + Rx*RY }[0-3]
    paddd          m2, m3                 ; (dword) { Bx*BY + Gx*GY + Rx*RY }[4-7]
    paddd          m0, m4                 ; += rgb_Yrnd, i.e. (dword) { Y[0-3] }
    paddd          m2, m4                 ; += rgb_Yrnd, i.e. (dword) { Y[4-7] }
    psrad          m0, 9
    psrad          m2, 9
    packssdw       m0, m2                 ; (word) { Y[0-7] }
    mova    [dstq+wq], m0
    add            wq, mmsize
    jl .loop
    REP_RET
%endif ; (ARCH_X86_64 && %0 == 3) || mmsize == 8
%endmacro

; %1 = nr. of XMM registers
; %2 = rgb or bgr
%macro RGB24_TO_UV_FN 2-3
cglobal %2 %+ 24ToUV, 7, 7, %1, dstU, dstV, u1, src, u2, w, table
%if ARCH_X86_64
    mova           m8, [%2_Ucoeff_12x4]
    mova           m9, [%2_Ucoeff_3x56]
    mova          m10, [%2_Vcoeff_12x4]
    mova          m11, [%2_Vcoeff_3x56]
%define coeffU1 m8
%define coeffU2 m9
%define coeffV1 m10
%define coeffV2 m11
%else ; x86-32
%define coeffU1 [%2_Ucoeff_12x4]
%define coeffU2 [%2_Ucoeff_3x56]
%define coeffV1 [%2_Vcoeff_12x4]
%define coeffV2 [%2_Vcoeff_3x56]
%endif ; x86-32/64
%if ARCH_X86_64 && %0 == 3
    jmp mangle(private_prefix %+ _ %+ %3 %+ 24ToUV %+ SUFFIX).body
%else ; ARCH_X86_64 && %0 == 3
.body:
%if cpuflag(ssse3)
    mova           m7, [shuf_rgb_12x4]
%define shuf_rgb1 m7
%if ARCH_X86_64
    mova          m12, [shuf_rgb_3x56]
%define shuf_rgb2 m12
%else ; x86-32
%define shuf_rgb2 [shuf_rgb_3x56]
%endif ; x86-32/64
%endif ; cpuflag(ssse3)
%if ARCH_X86_64
    movsxd         wq, dword r5m
%else ; x86-32
    mov            wq, r5m
%endif
    add            wq, wq
    add         dstUq, wq
    add         dstVq, wq
    neg            wq
    mova           m6, [rgb_UVrnd]
%if notcpuflag(ssse3)
    pxor           m7, m7
%endif
.loop:
%if cpuflag(ssse3)
    movu           m0, [srcq+0]           ; (byte) { Bx, Gx, Rx }[0-3]
    movu           m4, [srcq+12]          ; (byte) { Bx, Gx, Rx }[4-7]
    pshufb         m1, m0, shuf_rgb2      ; (word) { R0, B1, G1, R1, R2, B3, G3, R3 }
    pshufb         m0, shuf_rgb1          ; (word) { B0, G0, R0, B1, B2, G2, R2, B3 }
%else ; !cpuflag(ssse3)
    movd           m0, [srcq+0]           ; (byte) { B0, G0, R0, B1 }
    movd           m1, [srcq+2]           ; (byte) { R0, B1, G1, R1 }
    movd           m4, [srcq+6]           ; (byte) { B2, G2, R2, B3 }
    movd           m5, [srcq+8]           ; (byte) { R2, B3, G3, R3 }
%if mmsize == 16
    punpckldq      m0, m4                 ; (byte) { B0, G0, R0, B1, B2, G2, R2, B3 }
    punpckldq      m1, m5                 ; (byte) { R0, B1, G1, R1, R2, B3, G3, R3 }
    movd           m4, [srcq+12]          ; (byte) { B4, G4, R4, B5 }
    movd           m5, [srcq+14]          ; (byte) { R4, B5, G5, R5 }
%endif ; mmsize == 16
    punpcklbw      m0, m7                 ; (word) { B0, G0, R0, B1, B2, G2, R2, B3 }
    punpcklbw      m1, m7                 ; (word) { R0, B1, G1, R1, R2, B3, G3, R3 }
%endif ; cpuflag(ssse3)
    pmaddwd        m2, m0, coeffV1        ; (dword) { B0*BV + G0*GV, B1*BV, B2*BV + G2*GV, B3*BV }
    pmaddwd        m3, m1, coeffV2        ; (dword) { R0*BV, G1*GV + R1*BV, R2*BV, G3*GV + R3*BV }
    pmaddwd        m0, coeffU1            ; (dword) { B0*BU + G0*GU, B1*BU, B2*BU + G2*GU, B3*BU }
    pmaddwd        m1, coeffU2            ; (dword) { R0*BU, G1*GU + R1*BU, R2*BU, G3*GU + R3*BU }
    paddd          m0, m1                 ; (dword) { Bx*BU + Gx*GU + Rx*RU }[0-3]
    paddd          m2, m3                 ; (dword) { Bx*BV + Gx*GV + Rx*RV }[0-3]
%if cpuflag(ssse3)
    pshufb         m5, m4, shuf_rgb2      ; (word) { R4, B5, G5, R5, R6, B7, G7, R7 }
    pshufb         m4, shuf_rgb1          ; (word) { B4, G4, R4, B5, B6, G6, R6, B7 }
%else ; !cpuflag(ssse3)
%if mmsize == 16
    movd           m1, [srcq+18]          ; (byte) { B6, G6, R6, B7 }
    movd           m3, [srcq+20]          ; (byte) { R6, B7, G7, R7 }
    punpckldq      m4, m1                 ; (byte) { B4, G4, R4, B5, B6, G6, R6, B7 }
    punpckldq      m5, m3                 ; (byte) { R4, B5, G5, R5, R6, B7, G7, R7 }
%endif ; mmsize == 16 && !cpuflag(ssse3)
    punpcklbw      m4, m7                 ; (word) { B4, G4, R4, B5, B6, G6, R6, B7 }
    punpcklbw      m5, m7                 ; (word) { R4, B5, G5, R5, R6, B7, G7, R7 }
%endif ; cpuflag(ssse3)
    add          srcq, 3 * mmsize / 2
    pmaddwd        m1, m4, coeffU1        ; (dword) { B4*BU + G4*GU, B5*BU, B6*BU + G6*GU, B7*BU }
    pmaddwd        m3, m5, coeffU2        ; (dword) { R4*BU, G5*GU + R5*BU, R6*BU, G7*GU + R7*BU }
    pmaddwd        m4, coeffV1            ; (dword) { B4*BV + G4*GV, B5*BV, B6*BV + G6*GV, B7*BV }
    pmaddwd        m5, coeffV2            ; (dword) { R4*BV, G5*GV + R5*BV, R6*BV, G7*GV + R7*BV }
    paddd          m1, m3                 ; (dword) { Bx*BU + Gx*GU + Rx*RU }[4-7]
    paddd          m4, m5                 ; (dword) { Bx*BV + Gx*GV + Rx*RV }[4-7]
    paddd          m0, m6                 ; += rgb_UVrnd, i.e. (dword) { U[0-3] }
    paddd          m2, m6                 ; += rgb_UVrnd, i.e. (dword) { V[0-3] }
    paddd          m1, m6                 ; += rgb_UVrnd, i.e. (dword) { U[4-7] }
    paddd          m4, m6                 ; += rgb_UVrnd, i.e. (dword) { V[4-7] }
    psrad          m0, 9
    psrad          m2, 9
    psrad          m1, 9
    psrad          m4, 9
    packssdw       m0, m1                 ; (word) { U[0-7] }
    packssdw       m2, m4                 ; (word) { V[0-7] }
%if mmsize == 8
    mova   [dstUq+wq], m0
    mova   [dstVq+wq], m2
%else ; mmsize == 16
    mova   [dstUq+wq], m0
    mova   [dstVq+wq], m2
%endif ; mmsize == 8/16
    add            wq, mmsize
    jl .loop
    REP_RET
%endif ; ARCH_X86_64 && %0 == 3
%endmacro

; %1 = nr. of XMM registers for rgb-to-Y func
; %2 = nr. of XMM registers for rgb-to-UV func
%macro RGB24_FUNCS 2
RGB24_TO_Y_FN %1, rgb
RGB24_TO_Y_FN %1, bgr, rgb
RGB24_TO_UV_FN %2, rgb
RGB24_TO_UV_FN %2, bgr, rgb
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
RGB24_FUNCS 0, 0
%endif

INIT_XMM sse2
RGB24_FUNCS 10, 12

INIT_XMM ssse3
RGB24_FUNCS 11, 13

%if HAVE_AVX_EXTERNAL
INIT_XMM avx
RGB24_FUNCS 11, 13
%endif

; %1 = nr. of XMM registers
; %2-5 = rgba, bgra, argb or abgr (in individual characters)
%macro RGB32_TO_Y_FN 5-6
cglobal %2%3%4%5 %+ ToY, 6, 6, %1, dst, src, u1, u2, w, table
    mova           m5, [rgba_Ycoeff_%2%4]
    mova           m6, [rgba_Ycoeff_%3%5]
%if %0 == 6
    jmp mangle(private_prefix %+ _ %+ %6 %+ ToY %+ SUFFIX).body
%else ; %0 == 6
.body:
%if ARCH_X86_64
    movsxd         wq, wd
%endif
    add            wq, wq
    sub            wq, mmsize - 1
    lea          srcq, [srcq+wq*2]
    add          dstq, wq
    neg            wq
    mova           m4, [rgb_Yrnd]
    pcmpeqb        m7, m7
    psrlw          m7, 8                  ; (word) { 0x00ff } x4
.loop:
    ; FIXME check alignment and use mova
    movu           m0, [srcq+wq*2+0]      ; (byte) { Bx, Gx, Rx, xx }[0-3]
    movu           m2, [srcq+wq*2+mmsize] ; (byte) { Bx, Gx, Rx, xx }[4-7]
    DEINTB          1,  0,  3,  2,  7     ; (word) { Gx, xx (m0/m2) or Bx, Rx (m1/m3) }[0-3]/[4-7]
    pmaddwd        m1, m5                 ; (dword) { Bx*BY + Rx*RY }[0-3]
    pmaddwd        m0, m6                 ; (dword) { Gx*GY }[0-3]
    pmaddwd        m3, m5                 ; (dword) { Bx*BY + Rx*RY }[4-7]
    pmaddwd        m2, m6                 ; (dword) { Gx*GY }[4-7]
    paddd          m0, m4                 ; += rgb_Yrnd
    paddd          m2, m4                 ; += rgb_Yrnd
    paddd          m0, m1                 ; (dword) { Y[0-3] }
    paddd          m2, m3                 ; (dword) { Y[4-7] }
    psrad          m0, 9
    psrad          m2, 9
    packssdw       m0, m2                 ; (word) { Y[0-7] }
    mova    [dstq+wq], m0
    add            wq, mmsize
    jl .loop
    sub            wq, mmsize - 1
    jz .end
    add            srcq, 2*mmsize - 2
    add            dstq, mmsize - 1
.loop2:
    movd           m0, [srcq+wq*2+0]      ; (byte) { Bx, Gx, Rx, xx }[0-3]
    DEINTB          1,  0,  3,  2,  7     ; (word) { Gx, xx (m0/m2) or Bx, Rx (m1/m3) }[0-3]/[4-7]
    pmaddwd        m1, m5                 ; (dword) { Bx*BY + Rx*RY }[0-3]
    pmaddwd        m0, m6                 ; (dword) { Gx*GY }[0-3]
    paddd          m0, m4                 ; += rgb_Yrnd
    paddd          m0, m1                 ; (dword) { Y[0-3] }
    psrad          m0, 9
    packssdw       m0, m0                 ; (word) { Y[0-7] }
    movd    [dstq+wq], m0
    add            wq, 2
    jl .loop2
.end:
    REP_RET
%endif ; %0 == 3
%endmacro

; %1 = nr. of XMM registers
; %2-5 = rgba, bgra, argb or abgr (in individual characters)
%macro RGB32_TO_UV_FN 5-6
cglobal %2%3%4%5 %+ ToUV, 7, 7, %1, dstU, dstV, u1, src, u2, w, table
%if ARCH_X86_64
    mova           m8, [rgba_Ucoeff_%2%4]
    mova           m9, [rgba_Ucoeff_%3%5]
    mova          m10, [rgba_Vcoeff_%2%4]
    mova          m11, [rgba_Vcoeff_%3%5]
%define coeffU1 m8
%define coeffU2 m9
%define coeffV1 m10
%define coeffV2 m11
%else ; x86-32
%define coeffU1 [rgba_Ucoeff_%2%4]
%define coeffU2 [rgba_Ucoeff_%3%5]
%define coeffV1 [rgba_Vcoeff_%2%4]
%define coeffV2 [rgba_Vcoeff_%3%5]
%endif ; x86-64/32
%if ARCH_X86_64 && %0 == 6
    jmp mangle(private_prefix %+ _ %+ %6 %+ ToUV %+ SUFFIX).body
%else ; ARCH_X86_64 && %0 == 6
.body:
%if ARCH_X86_64
    movsxd         wq, dword r5m
%else ; x86-32
    mov            wq, r5m
%endif
    add            wq, wq
    sub            wq, mmsize - 1
    add         dstUq, wq
    add         dstVq, wq
    lea          srcq, [srcq+wq*2]
    neg            wq
    pcmpeqb        m7, m7
    psrlw          m7, 8                  ; (word) { 0x00ff } x4
    mova           m6, [rgb_UVrnd]
.loop:
    ; FIXME check alignment and use mova
    movu           m0, [srcq+wq*2+0]      ; (byte) { Bx, Gx, Rx, xx }[0-3]
    movu           m4, [srcq+wq*2+mmsize] ; (byte) { Bx, Gx, Rx, xx }[4-7]
    DEINTB          1,  0,  5,  4,  7     ; (word) { Gx, xx (m0/m4) or Bx, Rx (m1/m5) }[0-3]/[4-7]
    pmaddwd        m3, m1, coeffV1        ; (dword) { Bx*BV + Rx*RV }[0-3]
    pmaddwd        m2, m0, coeffV2        ; (dword) { Gx*GV }[0-3]
    pmaddwd        m1, coeffU1            ; (dword) { Bx*BU + Rx*RU }[0-3]
    pmaddwd        m0, coeffU2            ; (dword) { Gx*GU }[0-3]
    paddd          m3, m6                 ; += rgb_UVrnd
    paddd          m1, m6                 ; += rgb_UVrnd
    paddd          m2, m3                 ; (dword) { V[0-3] }
    paddd          m0, m1                 ; (dword) { U[0-3] }
    pmaddwd        m3, m5, coeffV1        ; (dword) { Bx*BV + Rx*RV }[4-7]
    pmaddwd        m1, m4, coeffV2        ; (dword) { Gx*GV }[4-7]
    pmaddwd        m5, coeffU1            ; (dword) { Bx*BU + Rx*RU }[4-7]
    pmaddwd        m4, coeffU2            ; (dword) { Gx*GU }[4-7]
    paddd          m3, m6                 ; += rgb_UVrnd
    paddd          m5, m6                 ; += rgb_UVrnd
    psrad          m0, 9
    paddd          m1, m3                 ; (dword) { V[4-7] }
    paddd          m4, m5                 ; (dword) { U[4-7] }
    psrad          m2, 9
    psrad          m4, 9
    psrad          m1, 9
    packssdw       m0, m4                 ; (word) { U[0-7] }
    packssdw       m2, m1                 ; (word) { V[0-7] }
%if mmsize == 8
    mova   [dstUq+wq], m0
    mova   [dstVq+wq], m2
%else ; mmsize == 16
    mova   [dstUq+wq], m0
    mova   [dstVq+wq], m2
%endif ; mmsize == 8/16
    add            wq, mmsize
    jl .loop
    sub            wq, mmsize - 1
    jz .end
    add            srcq , 2*mmsize - 2
    add            dstUq, mmsize - 1
    add            dstVq, mmsize - 1
.loop2:
    movd           m0, [srcq+wq*2]        ; (byte) { Bx, Gx, Rx, xx }[0-3]
    DEINTB          1,  0,  5,  4,  7     ; (word) { Gx, xx (m0/m4) or Bx, Rx (m1/m5) }[0-3]/[4-7]
    pmaddwd        m3, m1, coeffV1        ; (dword) { Bx*BV + Rx*RV }[0-3]
    pmaddwd        m2, m0, coeffV2        ; (dword) { Gx*GV }[0-3]
    pmaddwd        m1, coeffU1            ; (dword) { Bx*BU + Rx*RU }[0-3]
    pmaddwd        m0, coeffU2            ; (dword) { Gx*GU }[0-3]
    paddd          m3, m6                 ; += rgb_UVrnd
    paddd          m1, m6                 ; += rgb_UVrnd
    paddd          m2, m3                 ; (dword) { V[0-3] }
    paddd          m0, m1                 ; (dword) { U[0-3] }
    psrad          m0, 9
    psrad          m2, 9
    packssdw       m0, m0                 ; (word) { U[0-7] }
    packssdw       m2, m2                 ; (word) { V[0-7] }
    movd   [dstUq+wq], m0
    movd   [dstVq+wq], m2
    add            wq, 2
    jl .loop2
.end:
    REP_RET
%endif ; ARCH_X86_64 && %0 == 3
%endmacro

; %1 = nr. of XMM registers for rgb-to-Y func
; %2 = nr. of XMM registers for rgb-to-UV func
%macro RGB32_FUNCS 2
RGB32_TO_Y_FN %1, r, g, b, a
RGB32_TO_Y_FN %1, b, g, r, a, rgba
RGB32_TO_Y_FN %1, a, r, g, b, rgba
RGB32_TO_Y_FN %1, a, b, g, r, rgba

RGB32_TO_UV_FN %2, r, g, b, a
RGB32_TO_UV_FN %2, b, g, r, a, rgba
RGB32_TO_UV_FN %2, a, r, g, b, rgba
RGB32_TO_UV_FN %2, a, b, g, r, rgba
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
RGB32_FUNCS 0, 0
%endif

INIT_XMM sse2
RGB32_FUNCS 8, 12

%if HAVE_AVX_EXTERNAL
INIT_XMM avx
RGB32_FUNCS 8, 12
%endif

;-----------------------------------------------------------------------------
; YUYV/UYVY/NV12/NV21 packed pixel shuffling.
;
; void <fmt>ToY_<opt>(uint8_t *dst, const uint8_t *src, int w);
; and
; void <fmt>toUV_<opt>(uint8_t *dstU, uint8_t *dstV, const uint8_t *src,
;                      const uint8_t *unused, int w);
;-----------------------------------------------------------------------------

; %1 = a (aligned) or u (unaligned)
; %2 = yuyv or uyvy
%macro LOOP_YUYV_TO_Y 2
.loop_%1:
    mov%1          m0, [srcq+wq*2]        ; (byte) { Y0, U0, Y1, V0, ... }
    mov%1          m1, [srcq+wq*2+mmsize] ; (byte) { Y8, U4, Y9, V4, ... }
%ifidn %2, yuyv
    pand           m0, m2                 ; (word) { Y0, Y1, ..., Y7 }
    pand           m1, m2                 ; (word) { Y8, Y9, ..., Y15 }
%else ; uyvy
    psrlw          m0, 8                  ; (word) { Y0, Y1, ..., Y7 }
    psrlw          m1, 8                  ; (word) { Y8, Y9, ..., Y15 }
%endif ; yuyv/uyvy
    packuswb       m0, m1                 ; (byte) { Y0, ..., Y15 }
    mova    [dstq+wq], m0
    add            wq, mmsize
    jl .loop_%1
    REP_RET
%endmacro

; %1 = nr. of XMM registers
; %2 = yuyv or uyvy
; %3 = if specified, it means that unaligned and aligned code in loop
;      will be the same (i.e. YUYV+AVX), and thus we don't need to
;      split the loop in an aligned and unaligned case
%macro YUYV_TO_Y_FN 2-3
cglobal %2ToY, 5, 5, %1, dst, unused0, unused1, src, w
%if ARCH_X86_64
    movsxd         wq, wd
%endif
    add          dstq, wq
%if mmsize == 16
    test         srcq, 15
%endif
    lea          srcq, [srcq+wq*2]
%ifidn %2, yuyv
    pcmpeqb        m2, m2                 ; (byte) { 0xff } x 16
    psrlw          m2, 8                  ; (word) { 0x00ff } x 8
%endif ; yuyv
%if mmsize == 16
    jnz .loop_u_start
    neg            wq
    LOOP_YUYV_TO_Y  a, %2
.loop_u_start:
    neg            wq
    LOOP_YUYV_TO_Y  u, %2
%else ; mmsize == 8
    neg            wq
    LOOP_YUYV_TO_Y  a, %2
%endif ; mmsize == 8/16
%endmacro

; %1 = a (aligned) or u (unaligned)
; %2 = yuyv or uyvy
%macro LOOP_YUYV_TO_UV 2
.loop_%1:
%ifidn %2, yuyv
    mov%1          m0, [srcq+wq*4]        ; (byte) { Y0, U0, Y1, V0, ... }
    mov%1          m1, [srcq+wq*4+mmsize] ; (byte) { Y8, U4, Y9, V4, ... }
    psrlw          m0, 8                  ; (word) { U0, V0, ..., U3, V3 }
    psrlw          m1, 8                  ; (word) { U4, V4, ..., U7, V7 }
%else ; uyvy
%if cpuflag(avx)
    vpand          m0, m2, [srcq+wq*4]        ; (word) { U0, V0, ..., U3, V3 }
    vpand          m1, m2, [srcq+wq*4+mmsize] ; (word) { U4, V4, ..., U7, V7 }
%else
    mov%1          m0, [srcq+wq*4]        ; (byte) { Y0, U0, Y1, V0, ... }
    mov%1          m1, [srcq+wq*4+mmsize] ; (byte) { Y8, U4, Y9, V4, ... }
    pand           m0, m2                 ; (word) { U0, V0, ..., U3, V3 }
    pand           m1, m2                 ; (word) { U4, V4, ..., U7, V7 }
%endif
%endif ; yuyv/uyvy
    packuswb       m0, m1                 ; (byte) { U0, V0, ..., U7, V7 }
    pand           m1, m0, m2             ; (word) { U0, U1, ..., U7 }
    psrlw          m0, 8                  ; (word) { V0, V1, ..., V7 }
%if mmsize == 16
    packuswb       m1, m0                 ; (byte) { U0, ... U7, V1, ... V7 }
    movh   [dstUq+wq], m1
    movhps [dstVq+wq], m1
%else ; mmsize == 8
    packuswb       m1, m1                 ; (byte) { U0, ... U3 }
    packuswb       m0, m0                 ; (byte) { V0, ... V3 }
    movh   [dstUq+wq], m1
    movh   [dstVq+wq], m0
%endif ; mmsize == 8/16
    add            wq, mmsize / 2
    jl .loop_%1
    REP_RET
%endmacro

; %1 = nr. of XMM registers
; %2 = yuyv or uyvy
; %3 = if specified, it means that unaligned and aligned code in loop
;      will be the same (i.e. UYVY+AVX), and thus we don't need to
;      split the loop in an aligned and unaligned case
%macro YUYV_TO_UV_FN 2-3
cglobal %2ToUV, 4, 5, %1, dstU, dstV, unused, src, w
%if ARCH_X86_64
    movsxd         wq, dword r5m
%else ; x86-32
    mov            wq, r5m
%endif
    add         dstUq, wq
    add         dstVq, wq
%if mmsize == 16 && %0 == 2
    test         srcq, 15
%endif
    lea          srcq, [srcq+wq*4]
    pcmpeqb        m2, m2                 ; (byte) { 0xff } x 16
    psrlw          m2, 8                  ; (word) { 0x00ff } x 8
    ; NOTE: if uyvy+avx, u/a are identical
%if mmsize == 16 && %0 == 2
    jnz .loop_u_start
    neg            wq
    LOOP_YUYV_TO_UV a, %2
.loop_u_start:
    neg            wq
    LOOP_YUYV_TO_UV u, %2
%else ; mmsize == 8
    neg            wq
    LOOP_YUYV_TO_UV a, %2
%endif ; mmsize == 8/16
%endmacro

; %1 = a (aligned) or u (unaligned)
; %2 = nv12 or nv21
%macro LOOP_NVXX_TO_UV 2
.loop_%1:
    mov%1          m0, [srcq+wq*2]        ; (byte) { U0, V0, U1, V1, ... }
    mov%1          m1, [srcq+wq*2+mmsize] ; (byte) { U8, V8, U9, V9, ... }
    pand           m2, m0, m5             ; (word) { U0, U1, ..., U7 }
    pand           m3, m1, m5             ; (word) { U8, U9, ..., U15 }
    psrlw          m0, 8                  ; (word) { V0, V1, ..., V7 }
    psrlw          m1, 8                  ; (word) { V8, V9, ..., V15 }
    packuswb       m2, m3                 ; (byte) { U0, ..., U15 }
    packuswb       m0, m1                 ; (byte) { V0, ..., V15 }
%ifidn %2, nv12
    mova   [dstUq+wq], m2
    mova   [dstVq+wq], m0
%else ; nv21
    mova   [dstVq+wq], m2
    mova   [dstUq+wq], m0
%endif ; nv12/21
    add            wq, mmsize
    jl .loop_%1
    REP_RET
%endmacro

; %1 = nr. of XMM registers
; %2 = nv12 or nv21
%macro NVXX_TO_UV_FN 2
cglobal %2ToUV, 4, 5, %1, dstU, dstV, unused, src, w
%if ARCH_X86_64
    movsxd         wq, dword r5m
%else ; x86-32
    mov            wq, r5m
%endif
    add         dstUq, wq
    add         dstVq, wq
%if mmsize == 16
    test         srcq, 15
%endif
    lea          srcq, [srcq+wq*2]
    pcmpeqb        m5, m5                 ; (byte) { 0xff } x 16
    psrlw          m5, 8                  ; (word) { 0x00ff } x 8
%if mmsize == 16
    jnz .loop_u_start
    neg            wq
    LOOP_NVXX_TO_UV a, %2
.loop_u_start:
    neg            wq
    LOOP_NVXX_TO_UV u, %2
%else ; mmsize == 8
    neg            wq
    LOOP_NVXX_TO_UV a, %2
%endif ; mmsize == 8/16
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
YUYV_TO_Y_FN  0, yuyv
YUYV_TO_Y_FN  0, uyvy
YUYV_TO_UV_FN 0, yuyv
YUYV_TO_UV_FN 0, uyvy
NVXX_TO_UV_FN 0, nv12
NVXX_TO_UV_FN 0, nv21
%endif

INIT_XMM sse2
YUYV_TO_Y_FN  3, yuyv
YUYV_TO_Y_FN  2, uyvy
YUYV_TO_UV_FN 3, yuyv
YUYV_TO_UV_FN 3, uyvy
NVXX_TO_UV_FN 5, nv12
NVXX_TO_UV_FN 5, nv21

%if HAVE_AVX_EXTERNAL
INIT_XMM avx
; in theory, we could write a yuy2-to-y using vpand (i.e. AVX), but
; that's not faster in practice
YUYV_TO_UV_FN 3, yuyv
YUYV_TO_UV_FN 3, uyvy, 1
NVXX_TO_UV_FN 5, nv12
NVXX_TO_UV_FN 5, nv21
%endif
