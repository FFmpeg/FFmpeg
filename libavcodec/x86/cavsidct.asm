; Chinese AVS video (AVS1-P2, JiZhun profile) decoder
; Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
;
; MMX-optimized DSP functions, based on H.264 optimizations by
; Michael Niedermayer and Loren Merritt
; Conversion from gcc syntax to x264asm syntax with modifications
; by Ronald S. Bultje <rsbultje@gmail.com>
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
; You should have received a copy of the GNU Lesser General Public License
; along with FFmpeg; if not, write to the Free Software Foundation,
; Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

; Horizontal pass. A line is stored as src0 src2 src4 src6 src1 src3 src5 src7
; (FF_IDCT_PERM_LIBMPEG2). row_xy_01 holds the weights of srcx and srcy in
; out0 (low half) and out1 (high half), doubled for two lines at once.
row_02_01: dw  8,  10,  8,  10,   8,   4,   8,   4
row_02_23: dw  8,  -4,  8,  -4,   8, -10,   8, -10
row_46_01: dw  8,   4,  8,   4,  -8, -10,  -8, -10
row_46_23: dw -8,  10, -8,  10,   8,  -4,   8,  -4
row_13_01: dw 10,   9, 10,   9,   9,  -2,   9,  -2
row_13_23: dw  6, -10,  6, -10,   2,  -6,   2,  -6
row_57_01: dw  6,   2,  6,   2, -10,  -6, -10,  -6
row_57_23: dw  2,   9,  2,   9,   9, -10,   9, -10

; Vertical pass, on the interleaved words of lines n and n+4.
pw_8_8:    times 4 dw   8,   8
pw_8_m8:   times 4 dw   8,  -8
pw_10_4:   times 4 dw  10,   4
pw_4_m10:  times 4 dw   4, -10
pw_10_6:   times 4 dw  10,   6
pw_9_2:    times 4 dw   9,   2
pw_9_m10:  times 4 dw   9, -10
pw_m2_m6:  times 4 dw  -2,  -6
pw_6_2:    times 4 dw   6,   2
pw_m10_9:  times 4 dw -10,   9
pw_2_9:    times 4 dw   2,   9
pw_m6_m10: times 4 dw  -6, -10

pw_8_0:    times 4 dw   8,   0
pd_4:      times 4 dd   4

cextern pb_0

SECTION .text

; Horizontal pass of lines %1 and %1+4. Line %1 gets the two lines' words
; of columns 0-3 interleaved, line %1+4 those of columns 4-7.
%macro CAVS_IDCT8_ROWS 1
    mova            m1, [blockq+(%1)*16]
    punpckldq       m0, m1, [blockq+(%1)*16+64] ; src0 src2 x2, src4 src6 x2
    punpckhdq       m1, [blockq+(%1)*16+64]     ; src1 src3 x2, src5 src7 x2
    pshufd          m2, m0, q1010
    pshufd          m0, m0, q3232
    pmaddwd         m3, m2, [row_02_23]
    pmaddwd         m2, [row_02_01]
    pmaddwd         m4, m0, [row_46_23]
    pmaddwd         m0, [row_46_01]
    paddd           m2, m0              ; even part of out0 and out1
    paddd           m3, m4              ; even part of out2 and out3
    pshufd          m0, m1, q1010
    pshufd          m1, m1, q3232
    pmaddwd         m4, m0, [row_13_23]
    pmaddwd         m0, [row_13_01]
    pmaddwd         m5, m1, [row_57_23]
    pmaddwd         m1, [row_57_01]
    paddd           m0, m1              ; odd part of out0 and out1
    paddd           m4, m5              ; odd part of out2 and out3
    paddd           m2, [pd_4]
    paddd           m3, [pd_4]
    psubd           m1, m2, m0          ; out7 out6
    psubd           m5, m3, m4          ; out5 out4
    paddd           m2, m0              ; out0 out1
    paddd           m3, m4              ; out2 out3
    packssdw        m2, m3              ; the clip to 16 bits
    packssdw        m5, m1
    pshufd          m5, m5, q2301       ; out4 out5 out6 out7
    psraw           m2, 3
    psraw           m5, 3
%if %1 == 0
    paddw           m2, [pw_8_0]        ; rounding of the vertical pass,
    paddw           m5, [pw_8_0]        ; which multiplies line 0 by 8
%endif
    mova [blockq+(%1)*16], m2
    mova [blockq+(%1)*16+64], m5
%endmacro

; Even part of the vertical pass: m0 and m4 get %1 + %2 and %1 - %2 of
; columns 0-3, m2 and m5 those of columns 4-7.
%macro CAVS_IDCT8_EVEN 2
    mova            m0, [blockq+0*16]
    mova            m1, [blockq+2*16]
    mova            m2, [blockq+0*16+64]
    mova            m3, [blockq+2*16+64]
    pmaddwd         m0, [%1]
    pmaddwd         m1, [%2]
    pmaddwd         m2, [%1]
    pmaddwd         m3, [%2]
    psubd           m4, m0, m1
    psubd           m5, m2, m3
    paddd           m0, m1
    paddd           m2, m3
%endmacro

; Add lines n and 7-n to the picture: even part in m%1 (columns 0-3) and
; m%2 (columns 4-7), odd part weights %3 and %4, line addresses %5 and %6.
%macro CAVS_IDCT8_OUT 6
    mova            m1, [blockq+1*16]
    mova            m3, [blockq+3*16]
    pmaddwd         m1, [%3]
    pmaddwd         m3, [%4]
    paddd           m1, m3
    psubd           m3, m%1, m1
    paddd          m%1, m1
    mova            m1, [blockq+1*16+64]
    mova            m6, [blockq+3*16+64]
    pmaddwd         m1, [%3]
    pmaddwd         m6, [%4]
    paddd           m1, m6
    psubd           m6, m%2, m1
    paddd          m%2, m1
    packssdw       m%1, m%2             ; the clip to 16 bits
    packssdw        m3, m6
    psraw          m%1, 7
    psraw           m3, 7
    movq            m1, [%5]
    movq            m6, [%6]
    punpcklbw       m1, [pb_0]
    punpcklbw       m6, [pb_0]
    paddw          m%1, m1
    paddw           m3, m6
    packuswb       m%1, m3
    movq          [%5], m%1
    movhps        [%6], m%1
%endmacro

; void ff_cavs_idct8_add(uint8_t *dst, int16_t *block, ptrdiff_t stride)
INIT_XMM sse2
cglobal cavs_idct8_add, 3, 5, 7, dst, block, stride
    CAVS_IDCT8_ROWS 0
    CAVS_IDCT8_ROWS 1
    CAVS_IDCT8_ROWS 2
    CAVS_IDCT8_ROWS 3

    lea             r3, [strideq*3]
    lea             r4, [dstq+strideq*4]
    CAVS_IDCT8_EVEN pw_8_8,  pw_10_4
    CAVS_IDCT8_OUT  0, 2, pw_10_6,  pw_9_2,    dstq,           r4+r3
    CAVS_IDCT8_OUT  4, 5, pw_2_9,   pw_m6_m10, dstq+r3,        r4
    CAVS_IDCT8_EVEN pw_8_m8, pw_4_m10
    CAVS_IDCT8_OUT  0, 2, pw_9_m10, pw_m2_m6,  dstq+strideq,   r4+strideq*2
    CAVS_IDCT8_OUT  4, 5, pw_6_2,   pw_m10_9,  dstq+strideq*2, r4+strideq
    RET
