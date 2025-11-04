;
; Simple IDCT SSE2
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

SECTION_RODATA

%if ARCH_X86_32
cextern pb_80

d40000: dd 4 << 16, 0 ; must be 16-byte aligned
wm1010: dw 0, 0xffff, 0, 0xffff

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
    dw 1 << (ROW_SHIFT - 1), 0
    dw 1 << (ROW_SHIFT - 1), 0
    dw 1 << (ROW_SHIFT - 1), 1
    dw 1 << (ROW_SHIFT - 1), 0
    dw 1 << (ROW_SHIFT - 1), 1
    dw 1 << (ROW_SHIFT - 1), 0

    dw C4,  C4,  C4,  C4, C4,  C4,  C4,  C4
    dw C4, -C4,  C4, -C4, C4, -C4,  C4, -C4

    dw C2,  C6,  C2,  C6, C2,  C6,  C2,  C6
    dw C6, -C2,  C6, -C2, C6, -C2,  C6, -C2

    dw C1,  C3,  C1,  C3, C1,  C3,  C1,  C3
    dw C5,  C7,  C5,  C7, C5,  C7,  C5,  C7

    dw  C3, -C7,  C3, -C7,  C3, -C7,  C3, -C7
    dw -C1, -C5, -C1, -C5, -C1, -C5, -C1, -C5

    dw C5, -C1,  C5, -C1, C5, -C1,  C5, -C1
    dw C7,  C3,  C7,  C3, C7,  C3,  C7,  C3

    dw C7, -C5,  C7, -C5, C7, -C5,  C7, -C5
    dw C3, -C1,  C3, -C1, C3, -C1,  C3, -C1

SECTION .text

%macro DC_COND_IDCT 7
    movq             m0, [blockq + %1]  ; R4     R0      r4      r0
    movq             m1, [blockq + %2]  ; R6     R2      r6      r2
    movq             m2, [blockq + %3]  ; R3     R1      r3      r1
    movq             m3, [blockq + %4]  ; R7     R5      r7      r5
    movq             m4, [wm1010]
    pand             m4, m0
    por              m4, m1
    por              m4, m2
    por              m4, m3
    packssdw         m4, m4
    movd            t0d, m4
    or              t0d, t0d
    jz              %%1
    movq             m4, [coeffs + 32]  ; C4     C4      C4      C4
    pmaddwd          m4, m0             ; C4R4+C4R0      C4r4+C4r0
    movq             m5, [coeffs + 48]  ; -C4    C4      -C4     C4
    pmaddwd          m0, m5             ; -C4R4+C4R0     -C4r4+C4r0
    movq             m5, [coeffs + 64]  ; C6     C2      C6      C2
    pmaddwd          m5, m1             ; C6R6+C2R2      C6r6+C2r2
    movq             m6, [coeffs + 80]  ; -C2    C6      -C2     C6
    pmaddwd          m1, m6             ; -C2R6+C6R2     -C2r6+C6r2
    movq             m7, [coeffs + 96]  ; C3     C1      C3      C1
    pmaddwd          m7, m2             ; C3R3+C1R1      C3r3+C1r1
    paddd            m4, [coeffs + 16]
    movq             m6, m4             ; C4R4+C4R0      C4r4+C4r0
    paddd            m4, m5             ; A0             a0
    psubd            m6, m5             ; A3             a3
    movq             m5, [coeffs + 112] ; C7     C5      C7      C5
    pmaddwd          m5, m3             ; C7R7+C5R5      C7r7+C5r5
    paddd            m0, [coeffs + 16]
    paddd            m1, m0             ; A1             a1
    paddd            m0, m0
    psubd            m0, m1             ; A2             a2
    pmaddwd          m2, [coeffs + 128] ; -C7R3+C3R1     -C7r3+C3r1
    paddd            m7, m5             ; B0             b0
    movq             m5, [coeffs + 144] ; -C5    -C1     -C5     -C1
    pmaddwd          m5, m3             ; -C5R7-C1R5     -C5r7-C1r5
    paddd            m7, m4             ; A0+B0          a0+b0
    paddd            m4, m4             ; 2A0            2a0
    psubd            m4, m7             ; A0-B0          a0-b0
    paddd            m5, m2             ; B1             b1
    psrad            m7, %7
    psrad            m4, %7
    movq             m2, m1             ; A1             a1
    paddd            m1, m5             ; A1+B1          a1+b1
    psubd            m2, m5             ; A1-B1          a1-b1
    psrad            m1, %7
    psrad            m2, %7
    packssdw         m7, m1             ; A1+B1  a1+b1   A0+B0   a0+b0
    pshufd           m7, m7, 0xD8
    packssdw         m2, m4             ; A0-B0  a0-b0   A1-B1   a1-b1
    pshufd           m2, m2, 0xD8
    movq           [%5], m7
    movq             m1, [blockq + %3]  ; R3     R1      r3      r1
    movq             m4, [coeffs + 160] ; -C1    C5      -C1     C5
    movq      [24 + %5], m2
    pmaddwd          m4, m1             ; -C1R3+C5R1     -C1r3+C5r1
    movq             m7, [coeffs + 176] ; C3     C7      C3      C7
    pmaddwd          m1, [coeffs + 192] ; -C5R3+C7R1     -C5r3+C7r1
    pmaddwd          m7, m3             ; C3R7+C7R5      C3r7+C7r5
    movq             m2, m0             ; A2             a2
    pmaddwd          m3, [coeffs + 208] ; -C1R7+C3R5     -C1r7+C3r5
    paddd            m4, m7             ; B2             b2
    paddd            m2, m4             ; A2+B2          a2+b2
    psubd            m0, m4             ; a2-B2          a2-b2
    psrad            m2, %7
    psrad            m0, %7
    movq             m4, m6             ; A3             a3
    paddd            m3, m1             ; B3             b3
    paddd            m6, m3             ; A3+B3          a3+b3
    psubd            m4, m3             ; a3-B3          a3-b3
    psrad            m6, %7
    packssdw         m2, m6             ; A3+B3  a3+b3   A2+B2   a2+b2
    pshufd           m2, m2, 0xD8
    movq       [8 + %5], m2
    psrad            m4, %7
    packssdw         m4, m0             ; A2-B2  a2-b2   A3-B3   a3-b3
    pshufd           m4, m4, 0xD8
    movq      [16 + %5], m4
    jmp             %%2
%%1:
    pslld            m0, 16
    ; d40000 is only eight bytes long, so this will clobber
    ; the upper half of m0 with wm1010. It doesn't matter due to pshufd below.
    paddd            m0, [d40000]
    psrad            m0, 13
    packssdw         m0, m0
    pshufd           m0, m0, 0x0
    mova           [%5], m0
    mova      [16 + %5], m0
%%2:
%endmacro

%macro Z_COND_IDCT 8
    movq             m0, [blockq + %1]  ; R4     R0      r4      r0
    movq             m1, [blockq + %2]  ; R6     R2      r6      r2
    movq             m2, [blockq + %3]  ; R3     R1      r3      r1
    movq             m3, [blockq + %4]  ; R7     R5      r7      r5
    movq             m4, m0
    por              m4, m1
    por              m4, m2
    por              m4, m3
    packssdw         m4, m4
    movd            t0d, m4
    or              t0d, t0d
    jz               %8
    movq             m4, [coeffs + 32]  ; C4     C4      C4      C4
    pmaddwd          m4, m0             ; C4R4+C4R0      C4r4+C4r0
    movq             m5, [coeffs + 48]  ; -C4    C4      -C4     C4
    pmaddwd          m0, m5             ; -C4R4+C4R0     -C4r4+C4r0
    movq             m5, [coeffs + 64]  ; C6     C2      C6      C2
    pmaddwd          m5, m1             ; C6R6+C2R2      C6r6+C2r2
    movq             m6, [coeffs + 80]  ; -C2    C6      -C2     C6
    pmaddwd          m1, m6             ; -C2R6+C6R2     -C2r6+C6r2
    movq             m7, [coeffs + 96]  ; C3     C1      C3      C1
    pmaddwd          m7, m2             ; C3R3+C1R1      C3r3+C1r1
    paddd            m4, [coeffs]
    movq             m6, m4             ; C4R4+C4R0      C4r4+C4r0
    paddd            m4, m5             ; A0             a0
    psubd            m6, m5             ; A3             a3
    movq             m5, [coeffs + 112] ; C7     C5      C7      C5
    pmaddwd          m5, m3             ; C7R7+C5R5      C7r7+C5r5
    paddd            m0, [coeffs]
    paddd            m1, m0             ; A1             a1
    paddd            m0, m0
    psubd            m0, m1             ; A2             a2
    pmaddwd          m2, [coeffs + 128] ; -C7R3+C3R1     -C7r3+C3r1
    paddd            m7, m5             ; B0             b0
    movq             m5, [coeffs + 144] ; -C5    -C1     -C5     -C1
    pmaddwd          m5, m3             ; -C5R7-C1R5     -C5r7-C1r5
    paddd            m7, m4             ; A0+B0          a0+b0
    paddd            m4, m4             ; 2A0            2a0
    psubd            m4, m7             ; A0-B0          a0-b0
    paddd            m5, m2             ; B1             b1
    psrad            m7, %7
    psrad            m4, %7
    movq             m2, m1             ; A1             a1
    paddd            m1, m5             ; A1+B1          a1+b1
    psubd            m2, m5             ; A1-B1          a1-b1
    psrad            m1, %7
    psrad            m2, %7
    packssdw         m7, m1             ; A1+B1  a1+b1   A0+B0   a0+b0
    pshufd           m7, m7, 0xD8
    packssdw         m2, m4             ; A0-B0  a0-b0   A1-B1   a1-b1
    pshufd           m2, m2, 0xD8
    movq           [%5], m7
    movq             m1, [blockq + %3]  ; R3     R1      r3      r1
    movq             m4, [coeffs + 160] ; -C1    C5      -C1     C5
    movq      [24 + %5], m2
    pmaddwd          m4, m1             ; -C1R3+C5R1     -C1r3+C5r1
    movq             m7, [coeffs + 176] ; C3     C7      C3      C7
    pmaddwd          m1, [coeffs + 192] ; -C5R3+C7R1     -C5r3+C7r1
    pmaddwd          m7, m3             ; C3R7+C7R5      C3r7+C7r5
    movq             m2, m0             ; A2             a2
    pmaddwd          m3, [coeffs + 208] ; -C1R7+C3R5     -C1r7+C3r5
    paddd            m4, m7             ; B2             b2
    paddd            m2, m4             ; A2+B2          a2+b2
    psubd            m0, m4             ; a2-B2          a2-b2
    psrad            m2, %7
    psrad            m0, %7
    movq             m4, m6             ; A3             a3
    paddd            m3, m1             ; B3             b3
    paddd            m6, m3             ; A3+B3          a3+b3
    psubd            m4, m3             ; a3-B3          a3-b3
    psrad            m6, %7
    packssdw         m2, m6             ; A3+B3  a3+b3   A2+B2   a2+b2
    pshufd           m2, m2, 0xD8
    movq       [8 + %5], m2
    psrad            m4, %7
    packssdw         m4, m0             ; A2-B2  a2-b2   A3-B3   a3-b3
    pshufd           m4, m4, 0xD8
    movq      [16 + %5], m4
%endmacro

%macro IDCT1 6
    mova             m0, %1             ; R4     R0      r4      r0
    mova             m1, %2             ; R6     R2      r6      r2
    mova             m2, %3             ; R3     R1      r3      r1
    mova             m3, %4             ; R7     R5      r7      r5
    mova             m4, [coeffs + 32]  ; C4     C4      C4      C4
    pmaddwd          m4, m0             ; C4R4+C4R0      C4r4+C4r0
    mova             m5, [coeffs + 48]  ; -C4    C4      -C4     C4
    pmaddwd          m0, m5             ; -C4R4+C4R0     -C4r4+C4r0
    mova             m5, [coeffs + 64]  ; C6     C2      C6      C2
    pmaddwd          m5, m1             ; C6R6+C2R2      C6r6+C2r2
    mova             m6, [coeffs + 80]  ; -C2    C6      -C2     C6
    pmaddwd          m1, m6             ; -C2R6+C6R2     -C2r6+C6r2
    mova             m6, m4             ; C4R4+C4R0      C4r4+C4r0
    mova             m7, [coeffs + 96]  ; C3     C1      C3      C1
    pmaddwd          m7, m2             ; C3R3+C1R1      C3r3+C1r1
    paddd            m4, m5             ; A0             a0
    psubd            m6, m5             ; A3             a3
    mova             m5, m0             ; -C4R4+C4R0     -C4r4+C4r0
    paddd            m0, m1             ; A1             a1
    psubd            m5, m1             ; A2             a2
    mova             m1, [coeffs + 112] ; C7     C5      C7      C5
    pmaddwd          m1, m3             ; C7R7+C5R5      C7r7+C5r5
    pmaddwd          m2, [coeffs + 128] ; -C7R3+C3R1     -C7r3+C3r1
    paddd            m7, m1             ; B0             b0
    mova             m1, [coeffs + 144] ; -C5    -C1     -C5     -C1
    pmaddwd          m1, m3             ; -C5R7-C1R5     -C5r7-C1r5
    paddd            m7, m4             ; A0+B0          a0+b0
    paddd            m4, m4             ; 2A0            2a0
    psubd            m4, m7             ; A0-B0          a0-b0
    paddd            m1, m2             ; B1             b1
    psrad            m7, %6
    psrad            m4, %6
    mova             m2, m0             ; A1             a1
    paddd            m0, m1             ; A1+B1          a1+b1
    psubd            m2, m1             ; A1-B1          a1-b1
    psrad            m0, %6
    psrad            m2, %6
    packssdw         m7, m7             ; A0+B0  a0+b0
    movq           [%5], m7
    packssdw         m0, m0             ; A1+B1  a1+b1
    movq      [16 + %5], m0
    packssdw         m2, m2             ; A1-B1  a1-b1
    movq      [96 + %5], m2
    packssdw         m4, m4             ; A0-B0  a0-b0
    movq     [112 + %5], m4
    mova             m0, %3             ; R3     R1      r3      r1
    mova             m4, [coeffs + 160] ; -C1    C5      -C1     C5
    pmaddwd          m4, m0             ; -C1R3+C5R1     -C1r3+C5r1
    mova             m7, [coeffs + 176] ; C3     C7      C3      C7
    pmaddwd          m0, [coeffs + 192] ; -C5R3+C7R1     -C5r3+C7r1
    pmaddwd          m7, m3             ; C3R7+C7R5      C3r7+C7r5
    mova             m2, m5             ; A2             a2
    pmaddwd          m3, [coeffs + 208] ; -C1R7+C3R5     -C1r7+C3r5
    paddd            m4, m7             ; B2             b2
    paddd            m2, m4             ; A2+B2          a2+b2
    psubd            m5, m4             ; a2-B2          a2-b2
    psrad            m2, %6
    psrad            m5, %6
    mova             m4, m6             ; A3             a3
    paddd            m3, m0             ; B3             b3
    paddd            m6, m3             ; A3+B3          a3+b3
    psubd            m4, m3             ; a3-B3          a3-b3
    psrad            m6, %6
    psrad            m4, %6
    packssdw         m2, m2             ; A2+B2  a2+b2
    packssdw         m6, m6             ; A3+B3  a3+b3
    movq      [32 + %5], m2
    packssdw         m4, m4             ; A3-B3  a3-b3
    packssdw         m5, m5             ; A2-B2  a2-b2
    movq      [48 + %5], m6
    movq      [64 + %5], m4
    movq      [80 + %5], m5
%endmacro

%macro IDCT2 6
    mova             m0, %1             ; R4     R0      r4      r0
    mova             m1, %2             ; R6     R2      r6      r2
    mova             m3, %4             ; R7     R5      r7      r5
    mova             m4, [coeffs + 32]  ; C4     C4      C4      C4
    pmaddwd          m4, m0             ; C4R4+C4R0      C4r4+C4r0
    mova             m5, [coeffs + 48]  ; -C4    C4      -C4     C4
    pmaddwd          m0, m5             ; -C4R4+C4R0     -C4r4+C4r0
    mova             m5, [coeffs + 64]  ; C6     C2      C6      C2
    pmaddwd          m5, m1             ; C6R6+C2R2      C6r6+C2r2
    mova             m6, [coeffs + 80]  ; -C2    C6      -C2     C6
    pmaddwd          m1, m6             ; -C2R6+C6R2     -C2r6+C6r2
    mova             m6, m4             ; C4R4+C4R0      C4r4+C4r0
    paddd            m4, m5             ; A0             a0
    psubd            m6, m5             ; A3             a3
    mova             m5, m0             ; -C4R4+C4R0     -C4r4+C4r0
    paddd            m0, m1             ; A1             a1
    psubd            m5, m1             ; A2             a2
    mova             m1, [coeffs + 112] ; C7     C5      C7      C5
    pmaddwd          m1, m3             ; C7R7+C5R5      C7r7+C5r5
    mova             m7, [coeffs + 144] ; -C5    -C1     -C5     -C1
    pmaddwd          m7, m3             ; -C5R7-C1R5     -C5r7-C1r5
    paddd            m1, m4             ; A0+B0          a0+b0
    paddd            m4, m4             ; 2A0            2a0
    psubd            m4, m1             ; A0-B0          a0-b0
    psrad            m1, %6
    psrad            m4, %6
    mova             m2, m0             ; A1             a1
    paddd            m0, m7             ; A1+B1          a1+b1
    psubd            m2, m7             ; A1-B1          a1-b1
    psrad            m0, %6
    psrad            m2, %6
    packssdw         m1, m1             ; A0+B0  a0+b0
    movq           [%5], m1
    packssdw         m0, m0             ; A1+B1  a1+b1
    movq      [16 + %5], m0
    packssdw         m2, m2             ; A1-B1  a1-b1
    movq      [96 + %5], m2
    packssdw         m4, m4             ; A0-B0  a0-b0
    movq     [112 + %5], m4
    mova             m1, [coeffs + 176] ; C3     C7      C3      C7
    pmaddwd          m1, m3             ; C3R7+C7R5      C3r7+C7r5
    mova             m2, m5             ; A2             a2
    pmaddwd          m3, [coeffs + 208] ; -C1R7+C3R5     -C1r7+C3r5
    paddd            m2, m1             ; A2+B2          a2+b2
    psubd            m5, m1             ; a2-B2          a2-b2
    psrad            m2, %6
    psrad            m5, %6
    mova             m1, m6             ; A3             a3
    paddd            m6, m3             ; A3+B3          a3+b3
    psubd            m1, m3             ; a3-B3          a3-b3
    psrad            m6, %6
    psrad            m1, %6
    packssdw         m2, m2             ; A2+B2  a2+b2
    packssdw         m6, m6             ; A3+B3  a3+b3
    movq      [32 + %5], m2
    packssdw         m1, m1             ; A3-B3  a3-b3
    packssdw         m5, m5             ; A2-B2  a2-b2
    movq      [48 + %5], m6
    movq      [64 + %5], m1
    movq      [80 + %5], m5
%endmacro

%macro IDCT3 6
    mova             m0, %1             ; R4     R0      r4      r0
    mova             m3, %4             ; R7     R5      r7      r5
    mova             m4, [coeffs + 32]  ; C4     C4      C4      C4
    pmaddwd          m4, m0             ; C4R4+C4R0      C4r4+C4r0
    mova             m5, [coeffs + 48]  ; -C4    C4      -C4     C4
    pmaddwd          m0, m5             ; -C4R4+C4R0     -C4r4+C4r0
    mova             m6, m4             ; C4R4+C4R0      C4r4+C4r0
    mova             m5, m0             ; -C4R4+C4R0     -C4r4+C4r0
    mova             m1, [coeffs + 112] ; C7     C5      C7      C5
    pmaddwd          m1, m3             ; C7R7+C5R5      C7r7+C5r5
    mova             m7, [coeffs + 144] ; -C5    -C1     -C5     -C1
    pmaddwd          m7, m3             ; -C5R7-C1R5     -C5r7-C1r5
    paddd            m1, m4             ; A0+B0          a0+b0
    paddd            m4, m4             ; 2A0            2a0
    psubd            m4, m1             ; A0-B0          a0-b0
    psrad            m1, %6
    psrad            m4, %6
    mova             m2, m0             ; A1             a1
    paddd            m0, m7             ; A1+B1          a1+b1
    psubd            m2, m7             ; A1-B1          a1-b1
    psrad            m0, %6
    psrad            m2, %6
    packssdw         m1, m1             ; A0+B0  a0+b0
    movq           [%5], m1
    packssdw         m0, m0             ; A1+B1  a1+b1
    movq      [16 + %5], m0
    packssdw         m2, m2             ; A1-B1  a1-b1
    movq      [96 + %5], m2
    packssdw         m4, m4             ; A0-B0  a0-b0
    movq     [112 + %5], m4
    mova             m1, [coeffs + 176] ; C3     C7      C3      C7
    pmaddwd          m1, m3             ; C3R7+C7R5      C3r7+C7r5
    mova             m2, m5             ; A2             a2
    pmaddwd          m3, [coeffs + 208] ; -C1R7+C3R5     -C1r7+C3r5
    paddd            m2, m1             ; A2+B2          a2+b2
    psubd            m5, m1             ; a2-B2          a2-b2
    psrad            m2, %6
    psrad            m5, %6
    mova             m1, m6             ; A3             a3
    paddd            m6, m3             ; A3+B3          a3+b3
    psubd            m1, m3             ; a3-B3          a3-b3
    psrad            m6, %6
    psrad            m1, %6
    packssdw         m2, m2             ; A2+B2  a2+b2
    packssdw         m6, m6             ; A3+B3  a3+b3
    movq      [32 + %5], m2
    packssdw         m1, m1             ; A3-B3  a3-b3
    packssdw         m5, m5             ; A2-B2  a2-b2
    movq      [48 + %5], m6
    movq      [64 + %5], m1
    movq      [80 + %5], m5
%endmacro

%macro IDCT4 6
    mova             m0, %1             ; R4     R0      r4      r0
    mova             m2, %3             ; R3     R1      r3      r1
    mova             m3, %4             ; R7     R5      r7      r5
    mova             m4, [coeffs + 32]  ; C4     C4      C4      C4
    pmaddwd          m4, m0             ; C4R4+C4R0      C4r4+C4r0
    mova             m5, [coeffs + 48]  ; -C4    C4      -C4     C4
    pmaddwd          m0, m5             ; -C4R4+C4R0     -C4r4+C4r0
    mova             m6, m4             ; C4R4+C4R0      C4r4+C4r0
    mova             m7, [coeffs + 96]  ; C3     C1      C3      C1
    pmaddwd          m7, m2             ; C3R3+C1R1      C3r3+C1r1
    mova             m5, m0             ; -C4R4+C4R0     -C4r4+C4r0
    mova             m1, [coeffs + 112] ; C7     C5      C7      C5
    pmaddwd          m1, m3             ; C7R7+C5R5      C7r7+C5r5
    pmaddwd          m2, [coeffs + 128] ; -C7R3+C3R1     -C7r3+C3r1
    paddd            m7, m1             ; B0             b0
    mova             m1, [coeffs + 144] ; -C5    -C1     -C5     -C1
    pmaddwd          m1, m3             ; -C5R7-C1R5     -C5r7-C1r5
    paddd            m7, m4             ; A0+B0          a0+b0
    paddd            m4, m4             ; 2A0            2a0
    psubd            m4, m7             ; A0-B0          a0-b0
    paddd            m1, m2             ; B1             b1
    psrad            m7, %6
    psrad            m4, %6
    mova             m2, m0             ; A1             a1
    paddd            m0, m1             ; A1+B1          a1+b1
    psubd            m2, m1             ; A1-B1          a1-b1
    psrad            m0, %6
    psrad            m2, %6
    packssdw         m7, m7             ; A0+B0  a0+b0
    movq           [%5], m7
    packssdw         m0, m0             ; A1+B1  a1+b1
    movq      [16 + %5], m0
    packssdw         m2, m2             ; A1-B1  a1-b1
    movq      [96 + %5], m2
    packssdw         m4, m4             ; A0-B0  a0-b0
    movq     [112 + %5], m4
    mova             m0, %3             ; R3     R1      r3      r1
    mova             m4, [coeffs + 160] ; -C1    C5      -C1     C5
    pmaddwd          m4, m0             ; -C1R3+C5R1     -C1r3+C5r1
    mova             m7, [coeffs + 176] ; C3     C7      C3      C7
    pmaddwd          m0, [coeffs + 192] ; -C5R3+C7R1     -C5r3+C7r1
    pmaddwd          m7, m3             ; C3R7+C7R5      C3r7+C7r5
    mova             m2, m5             ; A2             a2
    pmaddwd          m3, [coeffs + 208] ; -C1R7+C3R5     -C1r7+C3r5
    paddd            m4, m7             ; B2             b2
    paddd            m2, m4             ; A2+B2          a2+b2
    psubd            m5, m4             ; a2-B2          a2-b2
    psrad            m2, %6
    psrad            m5, %6
    mova             m4, m6             ; A3             a3
    paddd            m3, m0             ; B3             b3
    paddd            m6, m3             ; A3+B3          a3+b3
    psubd            m4, m3             ; a3-B3          a3-b3
    psrad            m6, %6
    psrad            m4, %6
    packssdw         m2, m2             ; A2+B2  a2+b2
    packssdw         m6, m6             ; A3+B3  a3+b3
    movq      [32 + %5], m2
    packssdw         m4, m4             ; A3-B3  a3-b3
    packssdw         m5, m5             ; A2-B2  a2-b2
    movq      [48 + %5], m6
    movq      [64 + %5], m4
    movq      [80 + %5], m5
%endmacro

%macro IDCT5 6
    mova             m0, %1             ; R4     R0      r4      r0
    mova             m2, %3             ; R3     R1      r3      r1
    mova             m4, [coeffs + 32]  ; C4     C4      C4      C4
    pmaddwd          m4, m0             ; C4R4+C4R0      C4r4+C4r0
    mova             m5, [coeffs + 48]  ; -C4    C4      -C4     C4
    pmaddwd          m0, m5             ; -C4R4+C4R0     -C4r4+C4r0
    mova             m6, m4             ; C4R4+C4R0      C4r4+C4r0
    mova             m7, [coeffs + 96]  ; C3     C1      C3      C1
    pmaddwd          m7, m2             ; C3R3+C1R1      C3r3+C1r1
    mova             m5, m0             ; -C4R4+C4R0     -C4r4+C4r0
    mova             m3, [coeffs + 128]
    pmaddwd          m3, m2             ; -C7R3+C3R1     -C7r3+C3r1
    paddd            m7, m4             ; A0+B0          a0+b0
    paddd            m4, m4             ; 2A0            2a0
    psubd            m4, m7             ; A0-B0          a0-b0
    psrad            m7, %6
    psrad            m4, %6
    mova             m1, m0             ; A1             a1
    paddd            m0, m3             ; A1+B1          a1+b1
    psubd            m1, m3             ; A1-B1          a1-b1
    psrad            m0, %6
    psrad            m1, %6
    packssdw         m7, m7             ; A0+B0  a0+b0
    movq           [%5], m7
    packssdw         m0, m0             ; A1+B1  a1+b1
    movq      [16 + %5], m0
    packssdw         m1, m1             ; A1-B1  a1-b1
    movq      [96 + %5], m1
    packssdw         m4, m4             ; A0-B0  a0-b0
    movq     [112 + %5], m4
    mova             m4, [coeffs + 160] ; -C1    C5      -C1     C5
    pmaddwd          m4, m2             ; -C1R3+C5R1     -C1r3+C5r1
    pmaddwd          m2, [coeffs + 192] ; -C5R3+C7R1     -C5r3+C7r1
    mova             m1, m5             ; A2             a2
    paddd            m1, m4             ; A2+B2          a2+b2
    psubd            m5, m4             ; a2-B2          a2-b2
    psrad            m1, %6
    psrad            m5, %6
    mova             m4, m6             ; A3             a3
    paddd            m6, m2             ; A3+B3          a3+b3
    psubd            m4, m2             ; a3-B3          a3-b3
    psrad            m6, %6
    psrad            m4, %6
    packssdw         m1, m1             ; A2+B2  a2+b2
    packssdw         m6, m6             ; A3+B3  a3+b3
    movq      [32 + %5], m1
    packssdw         m4, m4             ; A3-B3  a3-b3
    packssdw         m5, m5             ; A2-B2  a2-b2
    movq      [48 + %5], m6
    movq      [64 + %5], m4
    movq      [80 + %5], m5
%endmacro

%macro IDCT6 6
    movq             m0, [%1]           ; R4     R0      r4      r0
    movhps           m0, [%1 + 16]
    movq             m1, [%2]           ; R6     R2      r6      r2
    movhps           m1, [%2 + 16]
    mova             m4, [coeffs + 32]  ; C4     C4      C4      C4
    pmaddwd          m4, m0             ; C4R4+C4R0      C4r4+C4r0
    mova             m5, [coeffs + 48]  ; -C4    C4      -C4     C4
    pmaddwd          m0, m5             ; -C4R4+C4R0     -C4r4+C4r0
    mova             m5, [coeffs + 64]  ; C6     C2      C6      C2
    pmaddwd          m5, m1             ; C6R6+C2R2      C6r6+C2r2
    mova             m6, [coeffs + 80]  ; -C2    C6      -C2     C6
    pmaddwd          m1, m6             ; -C2R6+C6R2     -C2r6+C6r2
    mova             m6, m4             ; C4R4+C4R0      C4r4+C4r0
    paddd            m4, m5             ; A0             a0
    psubd            m6, m5             ; A3             a3
    mova             m5, m0             ; -C4R4+C4R0     -C4r4+C4r0
    paddd            m0, m1             ; A1             a1
    psubd            m5, m1             ; A2             a2
    movq             m2, [%1 + 8]       ; R4     R0      r4      r0
    movhps           m2, [%1 + 24]
    movq             m3, [%2 + 8]       ; R6     R2      r6      r2
    movhps           m3, [%2 + 24]
    mova             m1, [coeffs + 32]  ; C4     C4      C4      C4
    pmaddwd          m1, m2             ; C4R4+C4R0      C4r4+C4r0
    mova             m7, [coeffs + 48]  ; -C4    C4      -C4     C4
    pmaddwd          m2, m7             ; -C4R4+C4R0     -C4r4+C4r0
    mova             m7, [coeffs + 64]  ; C6     C2      C6      C2
    pmaddwd          m7, m3             ; C6R6+C2R2      C6r6+C2r2
    pmaddwd          m3, [coeffs + 80]  ; -C2R6+C6R2     -C2r6+C6r2
    paddd            m7, m1             ; A0             a0
    paddd            m1, m1             ; 2C0            2c0
    psubd            m1, m7             ; A3             a3
    paddd            m3, m2             ; A1             a1
    paddd            m2, m2             ; 2C1            2c1
    psubd            m2, m3             ; A2             a2
    psrad            m4, %6
    psrad            m7, %6
    psrad            m3, %6
    packssdw         m4, m7             ; A0     a0
    pshufd           m4, m4, 0xD8
    mova           [%5], m4
    psrad            m0, %6
    packssdw         m0, m3             ; A1     a1
    pshufd           m0, m0, 0xD8
    mova      [16 + %5], m0
    mova      [96 + %5], m0
    mova     [112 + %5], m4
    psrad            m5, %6
    psrad            m6, %6
    psrad            m2, %6
    packssdw         m5, m2             ; A2-B2  a2-b2
    pshufd           m5, m5, 0xD8
    mova      [32 + %5], m5
    psrad            m1, %6
    packssdw         m6, m1             ; A3+B3  a3+b3
    pshufd           m6, m6, 0xD8
    mova      [48 + %5], m6
    mova      [64 + %5], m6
    mova      [80 + %5], m5
%endmacro

%macro IDCT7 6
    mova             m0, %1             ; R4     R0      r4      r0
    mova             m1, %2             ; R6     R2      r6      r2
    mova             m2, %3             ; R3     R1      r3      r1
    mova             m4, [coeffs + 32]  ; C4     C4      C4      C4
    pmaddwd          m4, m0             ; C4R4+C4R0      C4r4+C4r0
    mova             m5, [coeffs + 48]  ; -C4    C4      -C4     C4
    pmaddwd          m0, m5             ; -C4R4+C4R0     -C4r4+C4r0
    mova             m5, [coeffs + 64]  ; C6     C2      C6      C2
    pmaddwd          m5, m1             ; C6R6+C2R2      C6r6+C2r2
    mova             m6, [coeffs + 80]  ; -C2    C6      -C2     C6
    pmaddwd          m1, m6             ; -C2R6+C6R2     -C2r6+C6r2
    mova             m6, m4             ; C4R4+C4R0      C4r4+C4r0
    mova             m7, [coeffs + 96]  ; C3     C1      C3      C1
    pmaddwd          m7, m2             ; C3R3+C1R1      C3r3+C1r1
    paddd            m4, m5             ; A0             a0
    psubd            m6, m5             ; A3             a3
    mova             m5, m0             ; -C4R4+C4R0     -C4r4+C4r0
    paddd            m0, m1             ; A1             a1
    psubd            m5, m1             ; A2             a2
    mova             m1, [coeffs + 128]
    pmaddwd          m1, m2             ; -C7R3+C3R1     -C7r3+C3r1
    paddd            m7, m4             ; A0+B0          a0+b0
    paddd            m4, m4             ; 2A0            2a0
    psubd            m4, m7             ; A0-B0          a0-b0
    psrad            m7, %6
    psrad            m4, %6
    mova             m3, m0             ; A1             a1
    paddd            m0, m1             ; A1+B1          a1+b1
    psubd            m3, m1             ; A1-B1          a1-b1
    psrad            m0, %6
    psrad            m3, %6
    packssdw         m7, m7             ; A0+B0  a0+b0
    movq           [%5], m7
    packssdw         m0, m0             ; A1+B1  a1+b1
    movq      [16 + %5], m0
    packssdw         m3, m3             ; A1-B1  a1-b1
    movq      [96 + %5], m3
    packssdw         m4, m4             ; A0-B0  a0-b0
    movq     [112 + %5], m4
    mova             m4, [coeffs + 160] ; -C1    C5      -C1     C5
    pmaddwd          m4, m2             ; -C1R3+C5R1     -C1r3+C5r1
    pmaddwd          m2, [coeffs + 192] ; -C5R3+C7R1     -C5r3+C7r1
    mova             m3, m5             ; A2             a2
    paddd            m3, m4             ; A2+B2          a2+b2
    psubd            m5, m4             ; a2-B2          a2-b2
    psrad            m3, %6
    psrad            m5, %6
    mova             m4, m6             ; A3             a3
    paddd            m6, m2             ; A3+B3          a3+b3
    psubd            m4, m2             ; a3-B3          a3-b3
    psrad            m6, %6
    packssdw         m3, m3             ; A2+B2  a2+b2
    movq      [32 + %5], m3
    psrad            m4, %6
    packssdw         m6, m6             ; A3+B3  a3+b3
    movq      [48 + %5], m6
    packssdw         m4, m4             ; A3-B3  a3-b3
    packssdw         m5, m5             ; A2-B2  a2-b2
    movq      [64 + %5], m4
    movq      [80 + %5], m5
%endmacro

%macro IDCT8 6
    movq             m0, [%1]           ; R4     R0      r4      r0
    movhps           m0, [%1 + 16]
    mova             m4, [coeffs + 32]  ; C4     C4      C4      C4
    pmaddwd          m4, m0             ; C4R4+C4R0      C4r4+C4r0
    mova             m5, [coeffs + 48]  ; -C4    C4      -C4     C4
    pmaddwd          m0, m5             ; -C4R4+C4R0     -C4r4+C4r0
    psrad            m4, %6
    psrad            m0, %6
    movq             m2, [%1 + 8]       ; R4     R0      r4      r0
    movhps           m2, [%1 + 24]
    mova             m1, [coeffs + 32]  ; C4     C4      C4      C4
    pmaddwd          m1, m2             ; C4R4+C4R0      C4r4+C4r0
    mova             m7, [coeffs + 48]  ; -C4    C4      -C4     C4
    pmaddwd          m2, m7             ; -C4R4+C4R0     -C4r4+C4r0
    mova             m7, [coeffs + 64]  ; C6     C2      C6      C2
    psrad            m1, %6
    packssdw         m4, m1             ; A0     a0
    pshufd           m4, m4, 0xD8
    mova           [%5], m4
    psrad            m2, %6
    packssdw         m0, m2             ; A1     a1
    pshufd           m0, m0, 0xD8
    mova      [16 + %5], m0
    mova      [96 + %5], m0
    mova     [112 + %5], m4
    mova      [32 + %5], m0
    mova      [48 + %5], m4
    mova      [64 + %5], m4
    mova      [80 + %5], m0
%endmacro

%macro IDCT 0
    DC_COND_IDCT  0,   8,  16,  24, rsp +  0, null, 11
    Z_COND_IDCT  32,  40,  48,  56, rsp + 32, null, 11, %%4
    Z_COND_IDCT  64,  72,  80,  88, rsp + 64, null, 11, %%2
    Z_COND_IDCT  96, 104, 112, 120, rsp + 96, null, 11, %%1

    IDCT1 [rsp +  0], [rsp + 64], [rsp + 32], [rsp +  96], blockq +  0, 20
    IDCT1 [rsp + 16], [rsp + 80], [rsp + 48], [rsp + 112], blockq +  8, 20
    jmp %%9

    ALIGN 16
    %%4:
    Z_COND_IDCT 64,  72,  80,  88, rsp + 64, null, 11, %%6
    Z_COND_IDCT 96, 104, 112, 120, rsp + 96, null, 11, %%5

    IDCT2 [rsp +  0], [rsp + 64], [rsp + 32], [rsp +  96], blockq +  0, 20
    IDCT2 [rsp + 16], [rsp + 80], [rsp + 48], [rsp + 112], blockq +  8, 20
    jmp %%9

    ALIGN 16
    %%6:
    Z_COND_IDCT 96, 104, 112, 120, rsp + 96, null, 11, %%7

    IDCT3 [rsp +  0], [rsp + 64], [rsp + 32], [rsp +  96], blockq +  0, 20
    IDCT3 [rsp + 16], [rsp + 80], [rsp + 48], [rsp + 112], blockq +  8, 20
    jmp %%9

    ALIGN 16
    %%2:
    Z_COND_IDCT 96, 104, 112, 120, rsp + 96, null, 11, %%3

    IDCT4 [rsp +  0], [rsp + 64], [rsp + 32], [rsp +  96], blockq +  0, 20
    IDCT4 [rsp + 16], [rsp + 80], [rsp + 48], [rsp + 112], blockq +  8, 20
    jmp %%9

    ALIGN 16
    %%3:

    IDCT5 [rsp +  0], [rsp + 64], [rsp + 32], [rsp +  96], blockq +  0, 20
    IDCT5 [rsp + 16], [rsp + 80], [rsp + 48], [rsp + 112], blockq +  8, 20
    jmp %%9

    ALIGN 16
    %%5:

    IDCT6 rsp +  0, rsp + 64, rsp + 32, rsp +  96, blockq +  0, 20
    jmp %%9

    ALIGN 16
    %%1:

    IDCT7 [rsp +  0], [rsp + 64], [rsp + 32], [rsp +  96], blockq +  0, 20
    IDCT7 [rsp + 16], [rsp + 80], [rsp + 48], [rsp + 112], blockq +  8, 20
    jmp %%9

    ALIGN 16
    %%7:

    IDCT8 rsp +  0, rsp + 64, rsp + 32, rsp +  96, blockq +  0, 20

    %%9:
%endmacro

%macro PUT_PIXELS_CLAMPED_HALF 1
    mova     m0, [blockq+mmsize*0+%1]
    mova     m1, [blockq+mmsize*2+%1]
    packuswb m0, [blockq+mmsize*1+%1]
    packuswb m1, [blockq+mmsize*3+%1]
    movq           [pixelsq], m0
    movhps  [lsizeq+pixelsq], m0
    movq  [2*lsizeq+pixelsq], m1
    movhps [lsize3q+pixelsq], m1
%endmacro

%macro ADD_PIXELS_CLAMPED 1
    mova       m0, [blockq+mmsize*0+%1]
    mova       m1, [blockq+mmsize*1+%1]
    movq       m2, [pixelsq]
    movq       m3, [pixelsq+lsizeq]
    punpcklbw  m2, m4
    punpcklbw  m3, m4
    paddsw     m0, m2
    paddsw     m1, m3
    packuswb   m0, m1
    movq       [pixelsq], m0
    movhps     [pixelsq+lsizeq], m0
%endmacro

INIT_XMM sse2

cglobal simple_idct, 1, 2, 8, 128, block, t0
    IDCT
RET

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
