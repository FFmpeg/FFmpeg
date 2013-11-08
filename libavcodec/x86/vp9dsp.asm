;******************************************************************************
;* VP9 SIMD optimizations
;*
;* Copyright (c) 2013 Ronald S. Bultje <rsbultje gmail com>
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

; FIXME share with vp8dsp.asm
pw_256:   times 8 dw 256

%macro F8_TAPS 8
times 8 db %1, %2
times 8 db %3, %4
times 8 db %5, %6
times 8 db %7, %8
%endmacro
; int8_t ff_filters_ssse3[3][15][4][16]
const filters_ssse3 ; smooth
                    F8_TAPS -3, -1,  32,  64,  38,   1, -3,  0
                    F8_TAPS -2, -2,  29,  63,  41,   2, -3,  0
                    F8_TAPS -2, -2,  26,  63,  43,   4, -4,  0
                    F8_TAPS -2, -3,  24,  62,  46,   5, -4,  0
                    F8_TAPS -2, -3,  21,  60,  49,   7, -4,  0
                    F8_TAPS -1, -4,  18,  59,  51,   9, -4,  0
                    F8_TAPS -1, -4,  16,  57,  53,  12, -4, -1
                    F8_TAPS -1, -4,  14,  55,  55,  14, -4, -1
                    F8_TAPS -1, -4,  12,  53,  57,  16, -4, -1
                    F8_TAPS  0, -4,   9,  51,  59,  18, -4, -1
                    F8_TAPS  0, -4,   7,  49,  60,  21, -3, -2
                    F8_TAPS  0, -4,   5,  46,  62,  24, -3, -2
                    F8_TAPS  0, -4,   4,  43,  63,  26, -2, -2
                    F8_TAPS  0, -3,   2,  41,  63,  29, -2, -2
                    F8_TAPS  0, -3,   1,  38,  64,  32, -1, -3
                    ; regular
                    F8_TAPS  0,  1,  -5, 126,   8,  -3,  1,  0
                    F8_TAPS -1,  3, -10, 122,  18,  -6,  2,  0
                    F8_TAPS -1,  4, -13, 118,  27,  -9,  3, -1
                    F8_TAPS -1,  4, -16, 112,  37, -11,  4, -1
                    F8_TAPS -1,  5, -18, 105,  48, -14,  4, -1
                    F8_TAPS -1,  5, -19,  97,  58, -16,  5, -1
                    F8_TAPS -1,  6, -19,  88,  68, -18,  5, -1
                    F8_TAPS -1,  6, -19,  78,  78, -19,  6, -1
                    F8_TAPS -1,  5, -18,  68,  88, -19,  6, -1
                    F8_TAPS -1,  5, -16,  58,  97, -19,  5, -1
                    F8_TAPS -1,  4, -14,  48, 105, -18,  5, -1
                    F8_TAPS -1,  4, -11,  37, 112, -16,  4, -1
                    F8_TAPS -1,  3,  -9,  27, 118, -13,  4, -1
                    F8_TAPS  0,  2,  -6,  18, 122, -10,  3, -1
                    F8_TAPS  0,  1,  -3,   8, 126,  -5,  1,  0
                    ; sharp
                    F8_TAPS -1,  3,  -7, 127,   8,  -3,  1,  0
                    F8_TAPS -2,  5, -13, 125,  17,  -6,  3, -1
                    F8_TAPS -3,  7, -17, 121,  27, -10,  5, -2
                    F8_TAPS -4,  9, -20, 115,  37, -13,  6, -2
                    F8_TAPS -4, 10, -23, 108,  48, -16,  8, -3
                    F8_TAPS -4, 10, -24, 100,  59, -19,  9, -3
                    F8_TAPS -4, 11, -24,  90,  70, -21, 10, -4
                    F8_TAPS -4, 11, -23,  80,  80, -23, 11, -4
                    F8_TAPS -4, 10, -21,  70,  90, -24, 11, -4
                    F8_TAPS -3,  9, -19,  59, 100, -24, 10, -4
                    F8_TAPS -3,  8, -16,  48, 108, -23, 10, -4
                    F8_TAPS -2,  6, -13,  37, 115, -20,  9, -4
                    F8_TAPS -2,  5, -10,  27, 121, -17,  7, -3
                    F8_TAPS -1,  3,  -6,  17, 125, -13,  5, -2
                    F8_TAPS  0,  1,  -3,   8, 127,  -7,  3, -1

SECTION .text

%macro filter_h_fn 1
%assign %%px mmsize/2
cglobal %1_8tap_1d_h_ %+ %%px, 6, 6, 11, dst, dstride, src, sstride, h, filtery
    mova        m6, [pw_256]
    mova        m7, [filteryq+ 0]
%if ARCH_X86_64 && mmsize > 8
    mova        m8, [filteryq+16]
    mova        m9, [filteryq+32]
    mova       m10, [filteryq+48]
%endif
.loop:
    movh        m0, [srcq-3]
    movh        m1, [srcq-2]
    movh        m2, [srcq-1]
    movh        m3, [srcq+0]
    movh        m4, [srcq+1]
    movh        m5, [srcq+2]
    punpcklbw   m0, m1
    punpcklbw   m2, m3
    movh        m1, [srcq+3]
    movh        m3, [srcq+4]
    add       srcq, sstrideq
    punpcklbw   m4, m5
    punpcklbw   m1, m3
    pmaddubsw   m0, m7
%if ARCH_X86_64 && mmsize > 8
    pmaddubsw   m2, m8
    pmaddubsw   m4, m9
    pmaddubsw   m1, m10
%else
    pmaddubsw   m2, [filteryq+16]
    pmaddubsw   m4, [filteryq+32]
    pmaddubsw   m1, [filteryq+48]
%endif
    paddw       m0, m2
    paddw       m4, m1
    paddsw      m0, m4
    pmulhrsw    m0, m6
%ifidn %1, avg
    movh        m1, [dstq]
%endif
    packuswb    m0, m0
%ifidn %1, avg
    pavgb       m0, m1
%endif
    movh    [dstq], m0
    add       dstq, dstrideq
    dec         hd
    jg .loop
    RET
%endmacro

INIT_MMX ssse3
filter_h_fn put
filter_h_fn avg

INIT_XMM ssse3
filter_h_fn put
filter_h_fn avg

%macro filter_v_fn 1
%assign %%px mmsize/2
%if ARCH_X86_64
cglobal %1_8tap_1d_v_ %+ %%px, 6, 8, 11, dst, dstride, src, sstride, h, filtery, src4, sstride3
%else
cglobal %1_8tap_1d_v_ %+ %%px, 4, 7, 11, dst, dstride, src, sstride, filtery, src4, sstride3
    mov   filteryq, r5mp
%define hd r4mp
%endif
    sub       srcq, sstrideq
    lea  sstride3q, [sstrideq*3]
    sub       srcq, sstrideq
    mova        m6, [pw_256]
    sub       srcq, sstrideq
    mova        m7, [filteryq+ 0]
    lea      src4q, [srcq+sstrideq*4]
%if ARCH_X86_64 && mmsize > 8
    mova        m8, [filteryq+16]
    mova        m9, [filteryq+32]
    mova       m10, [filteryq+48]
%endif
.loop:
    ; FIXME maybe reuse loads from previous rows, or just
    ; more generally unroll this to prevent multiple loads of
    ; the same data?
    movh        m0, [srcq]
    movh        m1, [srcq+sstrideq]
    movh        m2, [srcq+sstrideq*2]
    movh        m3, [srcq+sstride3q]
    movh        m4, [src4q]
    movh        m5, [src4q+sstrideq]
    punpcklbw   m0, m1
    punpcklbw   m2, m3
    movh        m1, [src4q+sstrideq*2]
    movh        m3, [src4q+sstride3q]
    add       srcq, sstrideq
    add      src4q, sstrideq
    punpcklbw   m4, m5
    punpcklbw   m1, m3
    pmaddubsw   m0, m7
%if ARCH_X86_64 && mmsize > 8
    pmaddubsw   m2, m8
    pmaddubsw   m4, m9
    pmaddubsw   m1, m10
%else
    pmaddubsw   m2, [filteryq+16]
    pmaddubsw   m4, [filteryq+32]
    pmaddubsw   m1, [filteryq+48]
%endif
    paddw       m0, m2
    paddw       m4, m1
    paddsw      m0, m4
    pmulhrsw    m0, m6
%ifidn %1, avg
    movh        m1, [dstq]
%endif
    packuswb    m0, m0
%ifidn %1, avg
    pavgb       m0, m1
%endif
    movh    [dstq], m0
    add       dstq, dstrideq
    dec         hd
    jg .loop
    RET
%endmacro

INIT_MMX ssse3
filter_v_fn put
filter_v_fn avg

INIT_XMM ssse3
filter_v_fn put
filter_v_fn avg

%macro fpel_fn 6
%if %2 == 4
%define %%srcfn movh
%define %%dstfn movh
%else
%define %%srcfn movu
%define %%dstfn mova
%endif

%if %2 <= 16
cglobal %1%2, 5, 7, 4, dst, dstride, src, sstride, h, dstride3, sstride3
    lea  sstride3q, [sstrideq*3]
    lea  dstride3q, [dstrideq*3]
%else
cglobal %1%2, 5, 5, 4, dst, dstride, src, sstride, h
%endif
.loop:
    %%srcfn     m0, [srcq]
    %%srcfn     m1, [srcq+s%3]
    %%srcfn     m2, [srcq+s%4]
    %%srcfn     m3, [srcq+s%5]
    lea       srcq, [srcq+sstrideq*%6]
%ifidn %1, avg
    pavgb       m0, [dstq]
    pavgb       m1, [dstq+d%3]
    pavgb       m2, [dstq+d%4]
    pavgb       m3, [dstq+d%5]
%endif
    %%dstfn [dstq], m0
    %%dstfn [dstq+d%3], m1
    %%dstfn [dstq+d%4], m2
    %%dstfn [dstq+d%5], m3
    lea       dstq, [dstq+dstrideq*%6]
    sub         hd, %6
    jnz .loop
    RET
%endmacro

%define d16 16
%define s16 16
INIT_MMX mmx
fpel_fn put, 4,  strideq, strideq*2, stride3q, 4
fpel_fn put, 8,  strideq, strideq*2, stride3q, 4
INIT_MMX sse
fpel_fn avg, 4,  strideq, strideq*2, stride3q, 4
fpel_fn avg, 8,  strideq, strideq*2, stride3q, 4
INIT_XMM sse
fpel_fn put, 16, strideq, strideq*2, stride3q, 4
fpel_fn put, 32, mmsize,  strideq,   strideq+mmsize, 2
fpel_fn put, 64, mmsize,  mmsize*2,  mmsize*3, 1
INIT_XMM sse2
fpel_fn avg, 16, strideq, strideq*2, stride3q, 4
fpel_fn avg, 32, mmsize,  strideq,   strideq+mmsize, 2
fpel_fn avg, 64, mmsize,  mmsize*2,  mmsize*3, 1
%undef s16
%undef d16
