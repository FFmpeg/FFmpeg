;
; Simple IDCT MMX
;
; Copyright (c) 2001, 2002 Michael Niedermayer <michaelni@gmx.at>
;
; Conversion from gcc syntax to x264asm syntax with minimal modifications
; by James Darnley <jdarnley@obe.tv>.
;
; This file is part of FFmpeg.
;
; FFmpeg is free software; you can redistribute it and/or
; modify it under the terms of the GNU Lesser General Public
; License as published by the Free Software Foundation; either
; version 2.1 of the License, or (at your option) any later version.
;
; FFmpeg is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; Lesser General Public License for more details.
;
; You should have received a copy of the GNU Lesser General Public
; License along with FFmpeg; if not, write to the Free Software
; Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;/

%include "libavutil/x86/x86util.asm"

%if ARCH_X86_32
SECTION_RODATA

cextern pb_80

wm1010: dw 0, 0xffff, 0, 0xffff
d40000: dd 4 << 16, 0

; 23170.475006
; 22725.260826
; 21406.727617
; 19265.545870
; 16384.000000
; 12872.826198
; 8866.956905
; 4520.335430

%define C0 23170 ; cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
%define C1 22725 ; cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
%define C2 21407 ; cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
%define C3 19266 ; cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
%define C4 16383 ; cos(i*M_PI/16)*sqrt(2)*(1<<14) - 0.5
%define C5 12873 ; cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
%define C6 8867  ; cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
%define C7 4520  ; cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5

%define ROW_SHIFT 11
%define COL_SHIFT 20 ; 6

coeffs:
    dw 1 << (ROW_SHIFT - 1), 0
    dw 1 << (ROW_SHIFT - 1), 0
    dw 1 << (ROW_SHIFT - 1), 1
    dw 1 << (ROW_SHIFT - 1), 0

    dw C4,  C4,  C4,  C4
    dw C4, -C4,  C4, -C4

    dw C2,  C6,  C2,  C6
    dw C6, -C2,  C6, -C2

    dw C1,  C3,  C1,  C3
    dw C5,  C7,  C5,  C7

    dw C3, -C7,  C3, -C7
    dw -C1, -C5, -C1, -C5

    dw C5, -C1,  C5, -C1
    dw C7,  C3,  C7,  C3

    dw C7, -C5,  C7, -C5
    dw C3, -C1,  C3, -C1

SECTION .text

%macro DC_COND_IDCT 7
    movq            mm0, [blockq + %1]  ; R4     R0      r4      r0
    movq            mm1, [blockq + %2]  ; R6     R2      r6      r2
    movq            mm2, [blockq + %3]  ; R3     R1      r3      r1
    movq            mm3, [blockq + %4]  ; R7     R5      r7      r5
    movq            mm4, [wm1010]
    pand            mm4, mm0
    por             mm4, mm1
    por             mm4, mm2
    por             mm4, mm3
    packssdw        mm4, mm4
    movd            t0d, mm4
    or              t0d, t0d
    jz              %%1
    movq            mm4, [coeffs + 16]  ; C4     C4      C4      C4
    pmaddwd         mm4, mm0            ; C4R4+C4R0      C4r4+C4r0
    movq            mm5, [coeffs + 24]  ; -C4    C4      -C4     C4
    pmaddwd         mm0, mm5            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm5, [coeffs + 32]  ; C6     C2      C6      C2
    pmaddwd         mm5, mm1            ; C6R6+C2R2      C6r6+C2r2
    movq            mm6, [coeffs + 40]  ; -C2    C6      -C2     C6
    pmaddwd         mm1, mm6            ; -C2R6+C6R2     -C2r6+C6r2
    movq            mm7, [coeffs + 48]  ; C3     C1      C3      C1
    pmaddwd         mm7, mm2            ; C3R3+C1R1      C3r3+C1r1
    paddd           mm4, [coeffs + 8]
    movq            mm6, mm4            ; C4R4+C4R0      C4r4+C4r0
    paddd           mm4, mm5            ; A0             a0
    psubd           mm6, mm5            ; A3             a3
    movq            mm5, [coeffs + 56]  ; C7     C5      C7      C5
    pmaddwd         mm5, mm3            ; C7R7+C5R5      C7r7+C5r5
    paddd           mm0, [coeffs + 8]
    paddd           mm1, mm0            ; A1             a1
    paddd           mm0, mm0
    psubd           mm0, mm1            ; A2             a2
    pmaddwd         mm2, [coeffs + 64]  ; -C7R3+C3R1     -C7r3+C3r1
    paddd           mm7, mm5            ; B0             b0
    movq            mm5, [coeffs + 72]  ; -C5    -C1     -C5     -C1
    pmaddwd         mm5, mm3            ; -C5R7-C1R5     -C5r7-C1r5
    paddd           mm7, mm4            ; A0+B0          a0+b0
    paddd           mm4, mm4            ; 2A0            2a0
    psubd           mm4, mm7            ; A0-B0          a0-b0
    paddd           mm5, mm2            ; B1             b1
    psrad           mm7, %7
    psrad           mm4, %7
    movq            mm2, mm1            ; A1             a1
    paddd           mm1, mm5            ; A1+B1          a1+b1
    psubd           mm2, mm5            ; A1-B1          a1-b1
    psrad           mm1, %7
    psrad           mm2, %7
    packssdw        mm7, mm1            ; A1+B1  a1+b1   A0+B0   a0+b0
    packssdw        mm2, mm4            ; A0-B0  a0-b0   A1-B1   a1-b1
    movq           [%5], mm7
    movq            mm1, [blockq + %3]  ; R3     R1      r3      r1
    movq            mm4, [coeffs + 80]  ; -C1    C5      -C1     C5
    movq      [24 + %5], mm2
    pmaddwd         mm4, mm1            ; -C1R3+C5R1     -C1r3+C5r1
    movq            mm7, [coeffs + 88]  ; C3     C7      C3      C7
    pmaddwd         mm1, [coeffs + 96]  ; -C5R3+C7R1     -C5r3+C7r1
    pmaddwd         mm7, mm3            ; C3R7+C7R5      C3r7+C7r5
    movq            mm2, mm0            ; A2             a2
    pmaddwd         mm3, [coeffs + 104] ; -C1R7+C3R5     -C1r7+C3r5
    paddd           mm4, mm7            ; B2             b2
    paddd           mm2, mm4            ; A2+B2          a2+b2
    psubd           mm0, mm4            ; a2-B2          a2-b2
    psrad           mm2, %7
    psrad           mm0, %7
    movq            mm4, mm6            ; A3             a3
    paddd           mm3, mm1            ; B3             b3
    paddd           mm6, mm3            ; A3+B3          a3+b3
    psubd           mm4, mm3            ; a3-B3          a3-b3
    psrad           mm6, %7
    packssdw        mm2, mm6            ; A3+B3  a3+b3   A2+B2   a2+b2
    movq       [8 + %5], mm2
    psrad           mm4, %7
    packssdw        mm4, mm0            ; A2-B2  a2-b2   A3-B3   a3-b3
    movq      [16 + %5], mm4
    jmp             %%2
%%1:
    pslld           mm0, 16
    paddd           mm0, [d40000]
    psrad           mm0, 13
    packssdw        mm0, mm0
    movq           [%5], mm0
    movq       [8 + %5], mm0
    movq      [16 + %5], mm0
    movq      [24 + %5], mm0
%%2:
%endmacro

%macro Z_COND_IDCT 8
    movq            mm0, [blockq + %1]  ; R4     R0      r4      r0
    movq            mm1, [blockq + %2]  ; R6     R2      r6      r2
    movq            mm2, [blockq + %3]  ; R3     R1      r3      r1
    movq            mm3, [blockq + %4]  ; R7     R5      r7      r5
    movq            mm4, mm0
    por             mm4, mm1
    por             mm4, mm2
    por             mm4, mm3
    packssdw        mm4, mm4
    movd            t0d, mm4
    or              t0d, t0d
    jz               %8
    movq            mm4, [coeffs + 16]  ; C4     C4      C4      C4
    pmaddwd         mm4, mm0            ; C4R4+C4R0      C4r4+C4r0
    movq            mm5, [coeffs + 24]  ; -C4    C4      -C4     C4
    pmaddwd         mm0, mm5            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm5, [coeffs + 32]  ; C6     C2      C6      C2
    pmaddwd         mm5, mm1            ; C6R6+C2R2      C6r6+C2r2
    movq            mm6, [coeffs + 40]  ; -C2    C6      -C2     C6
    pmaddwd         mm1, mm6            ; -C2R6+C6R2     -C2r6+C6r2
    movq            mm7, [coeffs + 48]  ; C3     C1      C3      C1
    pmaddwd         mm7, mm2            ; C3R3+C1R1      C3r3+C1r1
    paddd           mm4, [coeffs]
    movq            mm6, mm4            ; C4R4+C4R0      C4r4+C4r0
    paddd           mm4, mm5            ; A0             a0
    psubd           mm6, mm5            ; A3             a3
    movq            mm5, [coeffs + 56]  ; C7     C5      C7      C5
    pmaddwd         mm5, mm3            ; C7R7+C5R5      C7r7+C5r5
    paddd           mm0, [coeffs]
    paddd           mm1, mm0            ; A1             a1
    paddd           mm0, mm0
    psubd           mm0, mm1            ; A2             a2
    pmaddwd         mm2, [coeffs + 64]  ; -C7R3+C3R1     -C7r3+C3r1
    paddd           mm7, mm5            ; B0             b0
    movq            mm5, [coeffs + 72]  ; -C5    -C1     -C5     -C1
    pmaddwd         mm5, mm3            ; -C5R7-C1R5     -C5r7-C1r5
    paddd           mm7, mm4            ; A0+B0          a0+b0
    paddd           mm4, mm4            ; 2A0            2a0
    psubd           mm4, mm7            ; A0-B0          a0-b0
    paddd           mm5, mm2            ; B1             b1
    psrad           mm7, %7
    psrad           mm4, %7
    movq            mm2, mm1            ; A1             a1
    paddd           mm1, mm5            ; A1+B1          a1+b1
    psubd           mm2, mm5            ; A1-B1          a1-b1
    psrad           mm1, %7
    psrad           mm2, %7
    packssdw        mm7, mm1            ; A1+B1  a1+b1   A0+B0   a0+b0
    packssdw        mm2, mm4            ; A0-B0  a0-b0   A1-B1   a1-b1
    movq           [%5], mm7
    movq            mm1, [blockq + %3]  ; R3     R1      r3      r1
    movq            mm4, [coeffs + 80]  ; -C1    C5      -C1     C5
    movq      [24 + %5], mm2
    pmaddwd         mm4, mm1            ; -C1R3+C5R1     -C1r3+C5r1
    movq            mm7, [coeffs + 88]  ; C3     C7      C3      C7
    pmaddwd         mm1, [coeffs + 96]  ; -C5R3+C7R1     -C5r3+C7r1
    pmaddwd         mm7, mm3            ; C3R7+C7R5      C3r7+C7r5
    movq            mm2, mm0            ; A2             a2
    pmaddwd         mm3, [coeffs + 104] ; -C1R7+C3R5     -C1r7+C3r5
    paddd           mm4, mm7            ; B2             b2
    paddd           mm2, mm4            ; A2+B2          a2+b2
    psubd           mm0, mm4            ; a2-B2          a2-b2
    psrad           mm2, %7
    psrad           mm0, %7
    movq            mm4, mm6            ; A3             a3
    paddd           mm3, mm1            ; B3             b3
    paddd           mm6, mm3            ; A3+B3          a3+b3
    psubd           mm4, mm3            ; a3-B3          a3-b3
    psrad           mm6, %7
    packssdw        mm2, mm6            ; A3+B3  a3+b3   A2+B2   a2+b2
    movq       [8 + %5], mm2
    psrad           mm4, %7
    packssdw        mm4, mm0            ; A2-B2  a2-b2   A3-B3   a3-b3
    movq      [16 + %5], mm4
%endmacro

%macro IDCT1 6
    movq            mm0, %1             ; R4     R0      r4      r0
    movq            mm1, %2             ; R6     R2      r6      r2
    movq            mm2, %3             ; R3     R1      r3      r1
    movq            mm3, %4             ; R7     R5      r7      r5
    movq            mm4, [coeffs + 16]  ; C4     C4      C4      C4
    pmaddwd         mm4, mm0            ; C4R4+C4R0      C4r4+C4r0
    movq            mm5, [coeffs + 24]  ; -C4    C4      -C4     C4
    pmaddwd         mm0, mm5            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm5, [coeffs + 32]  ; C6     C2      C6      C2
    pmaddwd         mm5, mm1            ; C6R6+C2R2      C6r6+C2r2
    movq            mm6, [coeffs + 40]  ; -C2    C6      -C2     C6
    pmaddwd         mm1, mm6            ; -C2R6+C6R2     -C2r6+C6r2
    movq            mm6, mm4            ; C4R4+C4R0      C4r4+C4r0
    movq            mm7, [coeffs + 48]  ; C3     C1      C3      C1
    pmaddwd         mm7, mm2            ; C3R3+C1R1      C3r3+C1r1
    paddd           mm4, mm5            ; A0             a0
    psubd           mm6, mm5            ; A3             a3
    movq            mm5, mm0            ; -C4R4+C4R0     -C4r4+C4r0
    paddd           mm0, mm1            ; A1             a1
    psubd           mm5, mm1            ; A2             a2
    movq            mm1, [coeffs + 56]  ; C7     C5      C7      C5
    pmaddwd         mm1, mm3            ; C7R7+C5R5      C7r7+C5r5
    pmaddwd         mm2, [coeffs + 64]  ; -C7R3+C3R1     -C7r3+C3r1
    paddd           mm7, mm1            ; B0             b0
    movq            mm1, [coeffs + 72]  ; -C5    -C1     -C5     -C1
    pmaddwd         mm1, mm3            ; -C5R7-C1R5     -C5r7-C1r5
    paddd           mm7, mm4            ; A0+B0          a0+b0
    paddd           mm4, mm4            ; 2A0            2a0
    psubd           mm4, mm7            ; A0-B0          a0-b0
    paddd           mm1, mm2            ; B1             b1
    psrad           mm7, %6
    psrad           mm4, %6
    movq            mm2, mm0            ; A1             a1
    paddd           mm0, mm1            ; A1+B1          a1+b1
    psubd           mm2, mm1            ; A1-B1          a1-b1
    psrad           mm0, %6
    psrad           mm2, %6
    packssdw        mm7, mm7            ; A0+B0  a0+b0
    movd           [%5], mm7
    packssdw        mm0, mm0            ; A1+B1  a1+b1
    movd      [16 + %5], mm0
    packssdw        mm2, mm2            ; A1-B1  a1-b1
    movd      [96 + %5], mm2
    packssdw        mm4, mm4            ; A0-B0  a0-b0
    movd     [112 + %5], mm4
    movq            mm0, %3             ; R3     R1      r3      r1
    movq            mm4, [coeffs + 80]  ; -C1    C5      -C1     C5
    pmaddwd         mm4, mm0            ; -C1R3+C5R1     -C1r3+C5r1
    movq            mm7, [coeffs + 88]  ; C3     C7      C3      C7
    pmaddwd         mm0, [coeffs + 96]  ; -C5R3+C7R1     -C5r3+C7r1
    pmaddwd         mm7, mm3            ; C3R7+C7R5      C3r7+C7r5
    movq            mm2, mm5            ; A2             a2
    pmaddwd         mm3, [coeffs + 104] ; -C1R7+C3R5     -C1r7+C3r5
    paddd           mm4, mm7            ; B2             b2
    paddd           mm2, mm4            ; A2+B2          a2+b2
    psubd           mm5, mm4            ; a2-B2          a2-b2
    psrad           mm2, %6
    psrad           mm5, %6
    movq            mm4, mm6            ; A3             a3
    paddd           mm3, mm0            ; B3             b3
    paddd           mm6, mm3            ; A3+B3          a3+b3
    psubd           mm4, mm3            ; a3-B3          a3-b3
    psrad           mm6, %6
    psrad           mm4, %6
    packssdw        mm2, mm2            ; A2+B2  a2+b2
    packssdw        mm6, mm6            ; A3+B3  a3+b3
    movd      [32 + %5], mm2
    packssdw        mm4, mm4            ; A3-B3  a3-b3
    packssdw        mm5, mm5            ; A2-B2  a2-b2
    movd      [48 + %5], mm6
    movd      [64 + %5], mm4
    movd      [80 + %5], mm5
%endmacro

%macro IDCT2 6
    movq            mm0, %1             ; R4     R0      r4      r0
    movq            mm1, %2             ; R6     R2      r6      r2
    movq            mm3, %4             ; R7     R5      r7      r5
    movq            mm4, [coeffs + 16]  ; C4     C4      C4      C4
    pmaddwd         mm4, mm0            ; C4R4+C4R0      C4r4+C4r0
    movq            mm5, [coeffs + 24]  ; -C4    C4      -C4     C4
    pmaddwd         mm0, mm5            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm5, [coeffs + 32]  ; C6     C2      C6      C2
    pmaddwd         mm5, mm1            ; C6R6+C2R2      C6r6+C2r2
    movq            mm6, [coeffs + 40]  ; -C2    C6      -C2     C6
    pmaddwd         mm1, mm6            ; -C2R6+C6R2     -C2r6+C6r2
    movq            mm6, mm4            ; C4R4+C4R0      C4r4+C4r0
    paddd           mm4, mm5            ; A0             a0
    psubd           mm6, mm5            ; A3             a3
    movq            mm5, mm0            ; -C4R4+C4R0     -C4r4+C4r0
    paddd           mm0, mm1            ; A1             a1
    psubd           mm5, mm1            ; A2             a2
    movq            mm1, [coeffs + 56]  ; C7     C5      C7      C5
    pmaddwd         mm1, mm3            ; C7R7+C5R5      C7r7+C5r5
    movq            mm7, [coeffs + 72]  ; -C5    -C1     -C5     -C1
    pmaddwd         mm7, mm3            ; -C5R7-C1R5     -C5r7-C1r5
    paddd           mm1, mm4            ; A0+B0          a0+b0
    paddd           mm4, mm4            ; 2A0            2a0
    psubd           mm4, mm1            ; A0-B0          a0-b0
    psrad           mm1, %6
    psrad           mm4, %6
    movq            mm2, mm0            ; A1             a1
    paddd           mm0, mm7            ; A1+B1          a1+b1
    psubd           mm2, mm7            ; A1-B1          a1-b1
    psrad           mm0, %6
    psrad           mm2, %6
    packssdw        mm1, mm1            ; A0+B0  a0+b0
    movd           [%5], mm1
    packssdw        mm0, mm0            ; A1+B1  a1+b1
    movd      [16 + %5], mm0
    packssdw        mm2, mm2            ; A1-B1  a1-b1
    movd      [96 + %5], mm2
    packssdw        mm4, mm4            ; A0-B0  a0-b0
    movd     [112 + %5], mm4
    movq            mm1, [coeffs + 88]  ; C3     C7      C3      C7
    pmaddwd         mm1, mm3            ; C3R7+C7R5      C3r7+C7r5
    movq            mm2, mm5            ; A2             a2
    pmaddwd         mm3, [coeffs + 104] ; -C1R7+C3R5     -C1r7+C3r5
    paddd           mm2, mm1            ; A2+B2          a2+b2
    psubd           mm5, mm1            ; a2-B2          a2-b2
    psrad           mm2, %6
    psrad           mm5, %6
    movq            mm1, mm6            ; A3             a3
    paddd           mm6, mm3            ; A3+B3          a3+b3
    psubd           mm1, mm3            ; a3-B3          a3-b3
    psrad           mm6, %6
    psrad           mm1, %6
    packssdw        mm2, mm2            ; A2+B2  a2+b2
    packssdw        mm6, mm6            ; A3+B3  a3+b3
    movd      [32 + %5], mm2
    packssdw        mm1, mm1            ; A3-B3  a3-b3
    packssdw        mm5, mm5            ; A2-B2  a2-b2
    movd      [48 + %5], mm6
    movd      [64 + %5], mm1
    movd      [80 + %5], mm5
%endmacro

%macro IDCT3 6
    movq            mm0, %1             ; R4     R0      r4      r0
    movq            mm3, %4             ; R7     R5      r7      r5
    movq            mm4, [coeffs + 16]  ; C4     C4      C4      C4
    pmaddwd         mm4, mm0            ; C4R4+C4R0      C4r4+C4r0
    movq            mm5, [coeffs + 24]  ; -C4    C4      -C4     C4
    pmaddwd         mm0, mm5            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm6, mm4            ; C4R4+C4R0      C4r4+C4r0
    movq            mm5, mm0            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm1, [coeffs + 56]  ; C7     C5      C7      C5
    pmaddwd         mm1, mm3            ; C7R7+C5R5      C7r7+C5r5
    movq            mm7, [coeffs + 72]  ; -C5    -C1     -C5     -C1
    pmaddwd         mm7, mm3            ; -C5R7-C1R5     -C5r7-C1r5
    paddd           mm1, mm4            ; A0+B0          a0+b0
    paddd           mm4, mm4            ; 2A0            2a0
    psubd           mm4, mm1            ; A0-B0          a0-b0
    psrad           mm1, %6
    psrad           mm4, %6
    movq            mm2, mm0            ; A1             a1
    paddd           mm0, mm7            ; A1+B1          a1+b1
    psubd           mm2, mm7            ; A1-B1          a1-b1
    psrad           mm0, %6
    psrad           mm2, %6
    packssdw        mm1, mm1            ; A0+B0  a0+b0
    movd           [%5], mm1
    packssdw        mm0, mm0            ; A1+B1  a1+b1
    movd      [16 + %5], mm0
    packssdw        mm2, mm2            ; A1-B1  a1-b1
    movd      [96 + %5], mm2
    packssdw        mm4, mm4            ; A0-B0  a0-b0
    movd     [112 + %5], mm4
    movq            mm1, [coeffs + 88]  ; C3     C7      C3      C7
    pmaddwd         mm1, mm3            ; C3R7+C7R5      C3r7+C7r5
    movq            mm2, mm5            ; A2             a2
    pmaddwd         mm3, [coeffs + 104] ; -C1R7+C3R5     -C1r7+C3r5
    paddd           mm2, mm1            ; A2+B2          a2+b2
    psubd           mm5, mm1            ; a2-B2          a2-b2
    psrad           mm2, %6
    psrad           mm5, %6
    movq            mm1, mm6            ; A3             a3
    paddd           mm6, mm3            ; A3+B3          a3+b3
    psubd           mm1, mm3            ; a3-B3          a3-b3
    psrad           mm6, %6
    psrad           mm1, %6
    packssdw        mm2, mm2            ; A2+B2  a2+b2
    packssdw        mm6, mm6            ; A3+B3  a3+b3
    movd      [32 + %5], mm2
    packssdw        mm1, mm1            ; A3-B3  a3-b3
    packssdw        mm5, mm5            ; A2-B2  a2-b2
    movd      [48 + %5], mm6
    movd      [64 + %5], mm1
    movd      [80 + %5], mm5
%endmacro

%macro IDCT4 6
    movq            mm0, %1             ; R4     R0      r4      r0
    movq            mm2, %3             ; R3     R1      r3      r1
    movq            mm3, %4             ; R7     R5      r7      r5
    movq            mm4, [coeffs + 16]  ; C4     C4      C4      C4
    pmaddwd         mm4, mm0            ; C4R4+C4R0      C4r4+C4r0
    movq            mm5, [coeffs + 24]  ; -C4    C4      -C4     C4
    pmaddwd         mm0, mm5            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm6, mm4            ; C4R4+C4R0      C4r4+C4r0
    movq            mm7, [coeffs + 48]  ; C3     C1      C3      C1
    pmaddwd         mm7, mm2            ; C3R3+C1R1      C3r3+C1r1
    movq            mm5, mm0            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm1, [coeffs + 56]  ; C7     C5      C7      C5
    pmaddwd         mm1, mm3            ; C7R7+C5R5      C7r7+C5r5
    pmaddwd         mm2, [coeffs + 64]  ; -C7R3+C3R1     -C7r3+C3r1
    paddd           mm7, mm1            ; B0             b0
    movq            mm1, [coeffs + 72]  ; -C5    -C1     -C5     -C1
    pmaddwd         mm1, mm3            ; -C5R7-C1R5     -C5r7-C1r5
    paddd           mm7, mm4            ; A0+B0          a0+b0
    paddd           mm4, mm4            ; 2A0            2a0
    psubd           mm4, mm7            ; A0-B0          a0-b0
    paddd           mm1, mm2            ; B1             b1
    psrad           mm7, %6
    psrad           mm4, %6
    movq            mm2, mm0            ; A1             a1
    paddd           mm0, mm1            ; A1+B1          a1+b1
    psubd           mm2, mm1            ; A1-B1          a1-b1
    psrad           mm0, %6
    psrad           mm2, %6
    packssdw        mm7, mm7            ; A0+B0  a0+b0
    movd           [%5], mm7
    packssdw        mm0, mm0            ; A1+B1  a1+b1
    movd      [16 + %5], mm0
    packssdw        mm2, mm2            ; A1-B1  a1-b1
    movd      [96 + %5], mm2
    packssdw        mm4, mm4            ; A0-B0  a0-b0
    movd     [112 + %5], mm4
    movq            mm0, %3             ; R3     R1      r3      r1
    movq            mm4, [coeffs + 80]  ; -C1    C5      -C1     C5
    pmaddwd         mm4, mm0            ; -C1R3+C5R1     -C1r3+C5r1
    movq            mm7, [coeffs + 88]  ; C3     C7      C3      C7
    pmaddwd         mm0, [coeffs + 96]  ; -C5R3+C7R1     -C5r3+C7r1
    pmaddwd         mm7, mm3            ; C3R7+C7R5      C3r7+C7r5
    movq            mm2, mm5            ; A2             a2
    pmaddwd         mm3, [coeffs + 104] ; -C1R7+C3R5     -C1r7+C3r5
    paddd           mm4, mm7            ; B2             b2
    paddd           mm2, mm4            ; A2+B2          a2+b2
    psubd           mm5, mm4            ; a2-B2          a2-b2
    psrad           mm2, %6
    psrad           mm5, %6
    movq            mm4, mm6            ; A3             a3
    paddd           mm3, mm0            ; B3             b3
    paddd           mm6, mm3            ; A3+B3          a3+b3
    psubd           mm4, mm3            ; a3-B3          a3-b3
    psrad           mm6, %6
    psrad           mm4, %6
    packssdw        mm2, mm2            ; A2+B2  a2+b2
    packssdw        mm6, mm6            ; A3+B3  a3+b3
    movd      [32 + %5], mm2
    packssdw        mm4, mm4            ; A3-B3  a3-b3
    packssdw        mm5, mm5            ; A2-B2  a2-b2
    movd      [48 + %5], mm6
    movd      [64 + %5], mm4
    movd      [80 + %5], mm5
%endmacro

%macro IDCT5 6
    movq            mm0, %1             ; R4     R0      r4      r0
    movq            mm2, %3             ; R3     R1      r3      r1
    movq            mm4, [coeffs + 16]  ; C4     C4      C4      C4
    pmaddwd         mm4, mm0            ; C4R4+C4R0      C4r4+C4r0
    movq            mm5, [coeffs + 24]  ; -C4    C4      -C4     C4
    pmaddwd         mm0, mm5            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm6, mm4            ; C4R4+C4R0      C4r4+C4r0
    movq            mm7, [coeffs + 48]  ; C3     C1      C3      C1
    pmaddwd         mm7, mm2            ; C3R3+C1R1      C3r3+C1r1
    movq            mm5, mm0            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm3, [coeffs + 64]
    pmaddwd         mm3, mm2            ; -C7R3+C3R1     -C7r3+C3r1
    paddd           mm7, mm4            ; A0+B0          a0+b0
    paddd           mm4, mm4            ; 2A0            2a0
    psubd           mm4, mm7            ; A0-B0          a0-b0
    psrad           mm7, %6
    psrad           mm4, %6
    movq            mm1, mm0            ; A1             a1
    paddd           mm0, mm3            ; A1+B1          a1+b1
    psubd           mm1, mm3            ; A1-B1          a1-b1
    psrad           mm0, %6
    psrad           mm1, %6
    packssdw        mm7, mm7            ; A0+B0  a0+b0
    movd           [%5], mm7
    packssdw        mm0, mm0            ; A1+B1  a1+b1
    movd      [16 + %5], mm0
    packssdw        mm1, mm1            ; A1-B1  a1-b1
    movd      [96 + %5], mm1
    packssdw        mm4, mm4            ; A0-B0  a0-b0
    movd     [112 + %5], mm4
    movq            mm4, [coeffs + 80]  ; -C1    C5      -C1     C5
    pmaddwd         mm4, mm2            ; -C1R3+C5R1     -C1r3+C5r1
    pmaddwd         mm2, [coeffs + 96]  ; -C5R3+C7R1     -C5r3+C7r1
    movq            mm1, mm5            ; A2             a2
    paddd           mm1, mm4            ; A2+B2          a2+b2
    psubd           mm5, mm4            ; a2-B2          a2-b2
    psrad           mm1, %6
    psrad           mm5, %6
    movq            mm4, mm6            ; A3             a3
    paddd           mm6, mm2            ; A3+B3          a3+b3
    psubd           mm4, mm2            ; a3-B3          a3-b3
    psrad           mm6, %6
    psrad           mm4, %6
    packssdw        mm1, mm1            ; A2+B2  a2+b2
    packssdw        mm6, mm6            ; A3+B3  a3+b3
    movd      [32 + %5], mm1
    packssdw        mm4, mm4            ; A3-B3  a3-b3
    packssdw        mm5, mm5            ; A2-B2  a2-b2
    movd      [48 + %5], mm6
    movd      [64 + %5], mm4
    movd      [80 + %5], mm5
%endmacro

%macro IDCT6 6
    movq            mm0, [%1]           ; R4     R0      r4      r0
    movq            mm1, [%2]           ; R6     R2      r6      r2
    movq            mm4, [coeffs + 16]  ; C4     C4      C4      C4
    pmaddwd         mm4, mm0            ; C4R4+C4R0      C4r4+C4r0
    movq            mm5, [coeffs + 24]  ; -C4    C4      -C4     C4
    pmaddwd         mm0, mm5            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm5, [coeffs + 32]  ; C6     C2      C6      C2
    pmaddwd         mm5, mm1            ; C6R6+C2R2      C6r6+C2r2
    movq            mm6, [coeffs + 40]  ; -C2    C6      -C2     C6
    pmaddwd         mm1, mm6            ; -C2R6+C6R2     -C2r6+C6r2
    movq            mm6, mm4            ; C4R4+C4R0      C4r4+C4r0
    paddd           mm4, mm5            ; A0             a0
    psubd           mm6, mm5            ; A3             a3
    movq            mm5, mm0            ; -C4R4+C4R0     -C4r4+C4r0
    paddd           mm0, mm1            ; A1             a1
    psubd           mm5, mm1            ; A2             a2
    movq            mm2, [8 + %1]       ; R4     R0      r4      r0
    movq            mm3, [8 + %2]       ; R6     R2      r6      r2
    movq            mm1, [coeffs + 16]  ; C4     C4      C4      C4
    pmaddwd         mm1, mm2            ; C4R4+C4R0      C4r4+C4r0
    movq            mm7, [coeffs + 24]  ; -C4    C4      -C4     C4
    pmaddwd         mm2, mm7            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm7, [coeffs + 32]  ; C6     C2      C6      C2
    pmaddwd         mm7, mm3            ; C6R6+C2R2      C6r6+C2r2
    pmaddwd         mm3, [coeffs + 40]  ; -C2R6+C6R2     -C2r6+C6r2
    paddd           mm7, mm1            ; A0             a0
    paddd           mm1, mm1            ; 2C0            2c0
    psubd           mm1, mm7            ; A3             a3
    paddd           mm3, mm2            ; A1             a1
    paddd           mm2, mm2            ; 2C1            2c1
    psubd           mm2, mm3            ; A2             a2
    psrad           mm4, %6
    psrad           mm7, %6
    psrad           mm3, %6
    packssdw        mm4, mm7            ; A0     a0
    movq           [%5], mm4
    psrad           mm0, %6
    packssdw        mm0, mm3            ; A1     a1
    movq      [16 + %5], mm0
    movq      [96 + %5], mm0
    movq     [112 + %5], mm4
    psrad           mm5, %6
    psrad           mm6, %6
    psrad           mm2, %6
    packssdw        mm5, mm2            ; A2-B2  a2-b2
    movq      [32 + %5], mm5
    psrad           mm1, %6
    packssdw        mm6, mm1            ; A3+B3  a3+b3
    movq      [48 + %5], mm6
    movq      [64 + %5], mm6
    movq      [80 + %5], mm5
%endmacro

%macro IDCT7 6
    movq            mm0, %1             ; R4     R0      r4      r0
    movq            mm1, %2             ; R6     R2      r6      r2
    movq            mm2, %3             ; R3     R1      r3      r1
    movq            mm4, [coeffs + 16]  ; C4     C4      C4      C4
    pmaddwd         mm4, mm0            ; C4R4+C4R0      C4r4+C4r0
    movq            mm5, [coeffs + 24]  ; -C4    C4      -C4     C4
    pmaddwd         mm0, mm5            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm5, [coeffs + 32]  ; C6     C2      C6      C2
    pmaddwd         mm5, mm1            ; C6R6+C2R2      C6r6+C2r2
    movq            mm6, [coeffs + 40]  ; -C2    C6      -C2     C6
    pmaddwd         mm1, mm6            ; -C2R6+C6R2     -C2r6+C6r2
    movq            mm6, mm4            ; C4R4+C4R0      C4r4+C4r0
    movq            mm7, [coeffs + 48]  ; C3     C1      C3      C1
    pmaddwd         mm7, mm2            ; C3R3+C1R1      C3r3+C1r1
    paddd           mm4, mm5            ; A0             a0
    psubd           mm6, mm5            ; A3             a3
    movq            mm5, mm0            ; -C4R4+C4R0     -C4r4+C4r0
    paddd           mm0, mm1            ; A1             a1
    psubd           mm5, mm1            ; A2             a2
    movq            mm1, [coeffs + 64]
    pmaddwd         mm1, mm2            ; -C7R3+C3R1     -C7r3+C3r1
    paddd           mm7, mm4            ; A0+B0          a0+b0
    paddd           mm4, mm4            ; 2A0            2a0
    psubd           mm4, mm7            ; A0-B0          a0-b0
    psrad           mm7, %6
    psrad           mm4, %6
    movq            mm3, mm0            ; A1             a1
    paddd           mm0, mm1            ; A1+B1          a1+b1
    psubd           mm3, mm1            ; A1-B1          a1-b1
    psrad           mm0, %6
    psrad           mm3, %6
    packssdw        mm7, mm7            ; A0+B0  a0+b0
    movd           [%5], mm7
    packssdw        mm0, mm0            ; A1+B1  a1+b1
    movd      [16 + %5], mm0
    packssdw        mm3, mm3            ; A1-B1  a1-b1
    movd      [96 + %5], mm3
    packssdw        mm4, mm4            ; A0-B0  a0-b0
    movd     [112 + %5], mm4
    movq            mm4, [coeffs + 80]  ; -C1    C5      -C1     C5
    pmaddwd         mm4, mm2            ; -C1R3+C5R1     -C1r3+C5r1
    pmaddwd         mm2, [coeffs + 96]  ; -C5R3+C7R1     -C5r3+C7r1
    movq            mm3, mm5            ; A2             a2
    paddd           mm3, mm4            ; A2+B2          a2+b2
    psubd           mm5, mm4            ; a2-B2          a2-b2
    psrad           mm3, %6
    psrad           mm5, %6
    movq            mm4, mm6            ; A3             a3
    paddd           mm6, mm2            ; A3+B3          a3+b3
    psubd           mm4, mm2            ; a3-B3          a3-b3
    psrad           mm6, %6
    packssdw        mm3, mm3            ; A2+B2  a2+b2
    movd      [32 + %5], mm3
    psrad           mm4, %6
    packssdw        mm6, mm6            ; A3+B3  a3+b3
    movd      [48 + %5], mm6
    packssdw        mm4, mm4            ; A3-B3  a3-b3
    packssdw        mm5, mm5            ; A2-B2  a2-b2
    movd      [64 + %5], mm4
    movd      [80 + %5], mm5
%endmacro

%macro IDCT8 6
    movq            mm0, [%1]           ; R4     R0      r4      r0
    movq            mm4, [coeffs + 16]  ; C4     C4      C4      C4
    pmaddwd         mm4, mm0            ; C4R4+C4R0      C4r4+C4r0
    movq            mm5, [coeffs + 24]  ; -C4    C4      -C4     C4
    pmaddwd         mm0, mm5            ; -C4R4+C4R0     -C4r4+C4r0
    psrad           mm4, %6
    psrad           mm0, %6
    movq            mm2, [8 + %1]       ; R4     R0      r4      r0
    movq            mm1, [coeffs + 16]  ; C4     C4      C4      C4
    pmaddwd         mm1, mm2            ; C4R4+C4R0      C4r4+C4r0
    movq            mm7, [coeffs + 24]  ; -C4    C4      -C4     C4
    pmaddwd         mm2, mm7            ; -C4R4+C4R0     -C4r4+C4r0
    movq            mm7, [coeffs + 32]  ; C6     C2      C6      C2
    psrad           mm1, %6
    packssdw        mm4, mm1            ; A0     a0
    movq           [%5], mm4
    psrad           mm2, %6
    packssdw        mm0, mm2            ; A1     a1
    movq      [16 + %5], mm0
    movq      [96 + %5], mm0
    movq     [112 + %5], mm4
    movq      [32 + %5], mm0
    movq      [48 + %5], mm4
    movq      [64 + %5], mm4
    movq      [80 + %5], mm0
%endmacro

%macro IDCT 0
    DC_COND_IDCT  0,   8,  16,  24, rsp +  0, null, 11
    Z_COND_IDCT  32,  40,  48,  56, rsp + 32, null, 11, %%4
    Z_COND_IDCT  64,  72,  80,  88, rsp + 64, null, 11, %%2
    Z_COND_IDCT  96, 104, 112, 120, rsp + 96, null, 11, %%1

    IDCT1 [rsp +  0], [rsp + 64], [rsp + 32], [rsp +  96], blockq +  0, 20
    IDCT1 [rsp +  8], [rsp + 72], [rsp + 40], [rsp + 104], blockq +  4, 20
    IDCT1 [rsp + 16], [rsp + 80], [rsp + 48], [rsp + 112], blockq +  8, 20
    IDCT1 [rsp + 24], [rsp + 88], [rsp + 56], [rsp + 120], blockq + 12, 20
    jmp %%9

    ALIGN 16
    %%4:
    Z_COND_IDCT 64,  72,  80,  88, rsp + 64, null, 11, %%6
    Z_COND_IDCT 96, 104, 112, 120, rsp + 96, null, 11, %%5

    IDCT2 [rsp +  0], [rsp + 64], [rsp + 32], [rsp +  96], blockq +  0, 20
    IDCT2 [rsp +  8], [rsp + 72], [rsp + 40], [rsp + 104], blockq +  4, 20
    IDCT2 [rsp + 16], [rsp + 80], [rsp + 48], [rsp + 112], blockq +  8, 20
    IDCT2 [rsp + 24], [rsp + 88], [rsp + 56], [rsp + 120], blockq + 12, 20
    jmp %%9

    ALIGN 16
    %%6:
    Z_COND_IDCT 96, 104, 112, 120, rsp + 96, null, 11, %%7

    IDCT3 [rsp +  0], [rsp + 64], [rsp + 32], [rsp +  96], blockq +  0, 20
    IDCT3 [rsp +  8], [rsp + 72], [rsp + 40], [rsp + 104], blockq +  4, 20
    IDCT3 [rsp + 16], [rsp + 80], [rsp + 48], [rsp + 112], blockq +  8, 20
    IDCT3 [rsp + 24], [rsp + 88], [rsp + 56], [rsp + 120], blockq + 12, 20
    jmp %%9

    ALIGN 16
    %%2:
    Z_COND_IDCT 96, 104, 112, 120, rsp + 96, null, 11, %%3

    IDCT4 [rsp +  0], [rsp + 64], [rsp + 32], [rsp +  96], blockq +  0, 20
    IDCT4 [rsp +  8], [rsp + 72], [rsp + 40], [rsp + 104], blockq +  4, 20
    IDCT4 [rsp + 16], [rsp + 80], [rsp + 48], [rsp + 112], blockq +  8, 20
    IDCT4 [rsp + 24], [rsp + 88], [rsp + 56], [rsp + 120], blockq + 12, 20
    jmp %%9

    ALIGN 16
    %%3:

    IDCT5 [rsp +  0], [rsp + 64], [rsp + 32], [rsp +  96], blockq +  0, 20
    IDCT5 [rsp +  8], [rsp + 72], [rsp + 40], [rsp + 104], blockq +  4, 20
    IDCT5 [rsp + 16], [rsp + 80], [rsp + 48], [rsp + 112], blockq +  8, 20
    IDCT5 [rsp + 24], [rsp + 88], [rsp + 56], [rsp + 120], blockq + 12, 20
    jmp %%9

    ALIGN 16
    %%5:

    IDCT6 rsp +  0, rsp + 64, rsp + 32, rsp +  96, blockq +  0, 20
    IDCT6 rsp + 16, rsp + 80, rsp + 48, rsp + 112, blockq +  8, 20
    jmp %%9

    ALIGN 16
    %%1:

    IDCT7 [rsp +  0], [rsp + 64], [rsp + 32], [rsp +  96], blockq +  0, 20
    IDCT7 [rsp +  8], [rsp + 72], [rsp + 40], [rsp + 104], blockq +  4, 20
    IDCT7 [rsp + 16], [rsp + 80], [rsp + 48], [rsp + 112], blockq +  8, 20
    IDCT7 [rsp + 24], [rsp + 88], [rsp + 56], [rsp + 120], blockq + 12, 20
    jmp %%9

    ALIGN 16
    %%7:

    IDCT8 rsp +  0, rsp + 64, rsp + 32, rsp +  96, blockq +  0, 20
    IDCT8 rsp + 16, rsp + 80, rsp + 48, rsp + 112, blockq +  8, 20

    %%9:
%endmacro

%macro PUT_PIXELS_CLAMPED_HALF 1
    mova     m0, [blockq+mmsize*0+%1]
    mova     m1, [blockq+mmsize*2+%1]
%if mmsize == 8
    mova     m2, [blockq+mmsize*4+%1]
    mova     m3, [blockq+mmsize*6+%1]
%endif
    packuswb m0, [blockq+mmsize*1+%1]
    packuswb m1, [blockq+mmsize*3+%1]
%if mmsize == 8
    packuswb m2, [blockq+mmsize*5+%1]
    packuswb m3, [blockq+mmsize*7+%1]
    movq           [pixelsq], m0
    movq    [lsizeq+pixelsq], m1
    movq  [2*lsizeq+pixelsq], m2
    movq   [lsize3q+pixelsq], m3
%else
    movq           [pixelsq], m0
    movhps  [lsizeq+pixelsq], m0
    movq  [2*lsizeq+pixelsq], m1
    movhps [lsize3q+pixelsq], m1
%endif
%endmacro

%macro ADD_PIXELS_CLAMPED 1
    mova       m0, [blockq+mmsize*0+%1]
    mova       m1, [blockq+mmsize*1+%1]
%if mmsize == 8
    mova       m5, [blockq+mmsize*2+%1]
    mova       m6, [blockq+mmsize*3+%1]
%endif
    movq       m2, [pixelsq]
    movq       m3, [pixelsq+lsizeq]
%if mmsize == 8
    mova       m7, m2
    punpcklbw  m2, m4
    punpckhbw  m7, m4
    paddsw     m0, m2
    paddsw     m1, m7
    mova       m7, m3
    punpcklbw  m3, m4
    punpckhbw  m7, m4
    paddsw     m5, m3
    paddsw     m6, m7
%else
    punpcklbw  m2, m4
    punpcklbw  m3, m4
    paddsw     m0, m2
    paddsw     m1, m3
%endif
    packuswb   m0, m1
%if mmsize == 8
    packuswb   m5, m6
    movq       [pixelsq], m0
    movq       [pixelsq+lsizeq], m5
%else
    movq       [pixelsq], m0
    movhps     [pixelsq+lsizeq], m0
%endif
%endmacro

INIT_MMX mmx

cglobal simple_idct, 1, 2, 8, 128, block, t0
    IDCT
RET

INIT_XMM sse2

cglobal simple_idct_put, 3, 5, 8, 128, pixels, lsize, block, lsize3, t0
    IDCT
    lea lsize3q, [lsizeq*3]
    PUT_PIXELS_CLAMPED_HALF 0
    lea pixelsq, [pixelsq+lsizeq*4]
    PUT_PIXELS_CLAMPED_HALF 64
RET

cglobal simple_idct_add, 3, 4, 8, 128, pixels, lsize, block, t0
    IDCT
    pxor       m4, m4
    ADD_PIXELS_CLAMPED 0
    lea        pixelsq, [pixelsq+lsizeq*2]
    ADD_PIXELS_CLAMPED 32
    lea        pixelsq, [pixelsq+lsizeq*2]
    ADD_PIXELS_CLAMPED 64
    lea        pixelsq, [pixelsq+lsizeq*2]
    ADD_PIXELS_CLAMPED 96
RET
%endif
