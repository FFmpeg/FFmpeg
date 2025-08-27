;******************************************************************************
;* VP9 motion compensation SIMD optimizations
;*
;* Copyright (c) 2013 Ronald S. Bultje <rsbultje gmail com>
;* Copyright (c) 2025 Two Orioles, LLC
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

cextern pw_256
cextern pw_64

%macro F8_SSSE3_TAPS 8
times 16 db %1, %2
times 16 db %3, %4
times 16 db %5, %6
times 16 db %7, %8
%endmacro

%macro F8_SSE2_TAPS 8
times 8 dw %1
times 8 dw %2
times 8 dw %3
times 8 dw %4
times 8 dw %5
times 8 dw %6
times 8 dw %7
times 8 dw %8
%endmacro

%macro F8_16BPP_TAPS 8
times 8 dw %1, %2
times 8 dw %3, %4
times 8 dw %5, %6
times 8 dw %7, %8
%endmacro

%macro FILTER 1
const filters_%1 ; smooth
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
%endmacro

%define F8_TAPS F8_SSSE3_TAPS
; int8_t ff_filters_ssse3[3][15][4][32]
FILTER ssse3
%define F8_TAPS F8_SSE2_TAPS
; int16_t ff_filters_sse2[3][15][8][8]
FILTER sse2
%define F8_TAPS F8_16BPP_TAPS
; int16_t ff_filters_16bpp[3][15][4][16]
FILTER 16bpp

%if HAVE_AVX512ICL_EXTERNAL && ARCH_X86_64
ALIGN 64
spel_h_perm16:  db  0,  1,  2,  3,  1,  2,  3,  4,  2,  3,  4,  5,  3,  4,  5,  6
                db  8,  9, 10, 11,  9, 10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14
                db 32, 33, 34, 35, 33, 34, 35, 36, 34, 35, 36, 37, 35, 36, 37, 38
                db 40, 41, 42, 43, 41, 42, 43, 44, 42, 43, 44, 45, 43, 44, 45, 46
spel_v_perm16:  db 32,  0, 33,  1, 34,  2, 35,  3, 36,  4, 37,  5, 38,  6, 39,  7
                db  0,  8,  1,  9,  2, 10,  3, 11,  4, 12,  5, 13,  6, 14,  7, 15
                db 40, 16, 41, 17, 42, 18, 43, 19, 44, 20, 45, 21, 46, 22, 47, 23
                db 16, 24, 17, 25, 18, 26, 19, 27, 20, 28, 21, 29, 22, 30, 23, 31
spel_v_perm32:  db  0, 32,  1, 33,  2, 34,  3, 35,  4, 36,  5, 37,  6, 38,  7, 39
                db  8, 40,  9, 41, 10, 42, 11, 43, 12, 44, 13, 45, 14, 46, 15, 47
                db 16, 48, 17, 49, 18, 50, 19, 51, 20, 52, 21, 53, 22, 54, 23, 55
                db 24, 56, 25, 57, 26, 58, 27, 59, 28, 60, 29, 61, 30, 62, 31, 63
spel_hv_perm4:  db 16, 32, 48,  8, 18, 34, 50, 10, 20, 36, 52, 12, 22, 38, 54, 14
                db 32, 48,  8, 24, 34, 50, 10, 26, 36, 52, 12, 28, 38, 54, 14, 30
                db 48,  8, 24, 40, 50, 10, 26, 42, 52, 12, 28, 44, 54, 14, 30, 46
                db  8, 24, 40, 56, 10, 26, 42, 58, 12, 28, 44, 60, 14, 30, 46, 62
spel_hv_perm8:  db 16, 32, 48,  8, 17, 33, 49,  9, 18, 34, 50, 10, 19, 35, 51, 11
                db 32, 48,  8, 24, 33, 49,  9, 25, 34, 50, 10, 26, 35, 51, 11, 27
                db 48,  8, 24, 40, 49,  9, 25, 41, 50, 10, 26, 42, 51, 11, 27, 43
                db  8, 24, 40, 56,  9, 25, 41, 57, 10, 26, 42, 58, 11, 27, 43, 59
spel_hv_perm16: db 32,  8, 33,  9, 34, 10, 35, 11, 36, 12, 37, 13, 38, 14, 39, 15
                db  8, 40,  9, 41, 10, 42, 11, 43, 12, 44, 13, 45, 14, 46, 15, 47
                db 48, 24, 49, 25, 50, 26, 51, 27, 52, 28, 53, 29, 54, 30, 55, 31
                db 24, 56, 25, 57, 26, 58, 27, 59, 28, 60, 29, 61, 30, 62, 31, 63
spel_h_shufB:   db  4,  5,  6,  7,  5,  6,  7,  8,  6,  7,  8,  9,  7,  8,  9, 10

%define spel_h_shufA (spel_h_perm16+ 0)
%define spel_h_shufC (spel_h_perm16+16)

vp9_spel_filter_regular: db   0,   1,  -5, 126,   8,  -3,   1,   0
                         db  -1,   3, -10, 122,  18,  -6,   2,   0
                         db  -1,   4, -13, 118,  27,  -9,   3,  -1
                         db  -1,   4, -16, 112,  37, -11,   4,  -1
                         db  -1,   5, -18, 105,  48, -14,   4,  -1
                         db  -1,   5, -19,  97,  58, -16,   5,  -1
                         db  -1,   6, -19,  88,  68, -18,   5,  -1
                         db  -1,   6, -19,  78,  78, -19,   6,  -1
                         db  -1,   5, -18,  68,  88, -19,   6,  -1
                         db  -1,   5, -16,  58,  97, -19,   5,  -1
                         db  -1,   4, -14,  48, 105, -18,   5,  -1
                         db  -1,   4, -11,  37, 112, -16,   4,  -1
                         db  -1,   3,  -9,  27, 118, -13,   4,  -1
                         db   0,   2,  -6,  18, 122, -10,   3,  -1
                         db   0,   1,  -3,   8, 126,  -5,   1,   0
vp9_spel_filter_sharp:   db  -1,   3,  -7, 127,   8,  -3,   1,   0
                         db  -2,   5, -13, 125,  17,  -6,   3,  -1
                         db  -3,   7, -17, 121,  27, -10,   5,  -2
                         db  -4,   9, -20, 115,  37, -13,   6,  -2
                         db  -4,  10, -23, 108,  48, -16,   8,  -3
                         db  -4,  10, -24, 100,  59, -19,   9,  -3
                         db  -4,  11, -24,  90,  70, -21,  10,  -4
                         db  -4,  11, -23,  80,  80, -23,  11,  -4
                         db  -4,  10, -21,  70,  90, -24,  11,  -4
                         db  -3,   9, -19,  59, 100, -24,  10,  -4
                         db  -3,   8, -16,  48, 108, -23,  10,  -4
                         db  -2,   6, -13,  37, 115, -20,   9,  -4
                         db  -2,   5, -10,  27, 121, -17,   7,  -3
                         db  -1,   3,  -6,  17, 125, -13,   5,  -2
                         db   0,   1,  -3,   8, 127,  -7,   3,  -1
vp9_spel_filter_smooth:  db  -3,  -1,  32,  64,  38,   1,  -3,   0
                         db  -2,  -2,  29,  63,  41,   2,  -3,   0
                         db  -2,  -2,  26,  63,  43,   4,  -4,   0
                         db  -2,  -3,  24,  62,  46,   5,  -4,   0
                         db  -2,  -3,  21,  60,  49,   7,  -4,   0
                         db  -1,  -4,  18,  59,  51,   9,  -4,   0
                         db  -1,  -4,  16,  57,  53,  12,  -4,  -1
                         db  -1,  -4,  14,  55,  55,  14,  -4,  -1
                         db  -1,  -4,  12,  53,  57,  16,  -4,  -1
                         db   0,  -4,   9,  51,  59,  18,  -4,  -1
                         db   0,  -4,   7,  49,  60,  21,  -3,  -2
                         db   0,  -4,   5,  46,  62,  24,  -3,  -2
                         db   0,  -4,   4,  43,  63,  26,  -2,  -2
                         db   0,  -3,   2,  41,  63,  29,  -2,  -2
                         db   0,  -3,   1,  38,  64,  32,  -1,  -3

pb_02461357:    db  0,  2,  4,  6,  1,  3,  5,  7
pd_64:          dd 64
pw_m33:         times 2 dw -33
pb_4:           times 4 db 4
%endif

SECTION .text

%macro filter_sse2_h_fn 1
%assign %%px mmsize/2
cglobal vp9_%1_8tap_1d_h_ %+ %%px %+ _8, 6, 6, 15, dst, dstride, src, sstride, h, filtery
    pxor        m5, m5
    mova        m6, [pw_64]
    mova        m7, [filteryq+  0]
%if ARCH_X86_64 && mmsize > 8
    mova        m8, [filteryq+ 16]
    mova        m9, [filteryq+ 32]
    mova       m10, [filteryq+ 48]
    mova       m11, [filteryq+ 64]
    mova       m12, [filteryq+ 80]
    mova       m13, [filteryq+ 96]
    mova       m14, [filteryq+112]
%endif
.loop:
    movh        m0, [srcq-3]
    movh        m1, [srcq-2]
    movh        m2, [srcq-1]
    movh        m3, [srcq+0]
    movh        m4, [srcq+1]
    punpcklbw   m0, m5
    punpcklbw   m1, m5
    punpcklbw   m2, m5
    punpcklbw   m3, m5
    punpcklbw   m4, m5
    pmullw      m0, m7
%if ARCH_X86_64 && mmsize > 8
    pmullw      m1, m8
    pmullw      m2, m9
    pmullw      m3, m10
    pmullw      m4, m11
%else
    pmullw      m1, [filteryq+ 16]
    pmullw      m2, [filteryq+ 32]
    pmullw      m3, [filteryq+ 48]
    pmullw      m4, [filteryq+ 64]
%endif
    paddw       m0, m1
    paddw       m2, m3
    paddw       m0, m4
    movh        m1, [srcq+2]
    movh        m3, [srcq+3]
    movh        m4, [srcq+4]
    add       srcq, sstrideq
    punpcklbw   m1, m5
    punpcklbw   m3, m5
    punpcklbw   m4, m5
%if ARCH_X86_64 && mmsize > 8
    pmullw      m1, m12
    pmullw      m3, m13
    pmullw      m4, m14
%else
    pmullw      m1, [filteryq+ 80]
    pmullw      m3, [filteryq+ 96]
    pmullw      m4, [filteryq+112]
%endif
    paddw       m0, m1
    paddw       m3, m4
    paddw       m0, m6
    paddw       m2, m3
    paddsw      m0, m2
    psraw       m0, 7
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

INIT_MMX mmxext
filter_sse2_h_fn put
filter_sse2_h_fn avg

INIT_XMM sse2
filter_sse2_h_fn put
filter_sse2_h_fn avg

%macro filter_h_fn 1
%assign %%px mmsize/2
cglobal vp9_%1_8tap_1d_h_ %+ %%px %+ _8, 6, 6, 11, dst, dstride, src, sstride, h, filtery
    mova        m6, [pw_256]
    mova        m7, [filteryq+ 0]
%if ARCH_X86_64 && mmsize > 8
    mova        m8, [filteryq+32]
    mova        m9, [filteryq+64]
    mova       m10, [filteryq+96]
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
    pmaddubsw   m2, [filteryq+32]
    pmaddubsw   m4, [filteryq+64]
    pmaddubsw   m1, [filteryq+96]
%endif
    paddw       m0, m4
    paddw       m2, m1
    paddsw      m0, m2
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

%if ARCH_X86_64
%macro filter_hx2_fn 1
%assign %%px mmsize
cglobal vp9_%1_8tap_1d_h_ %+ %%px %+ _8, 6, 6, 14, dst, dstride, src, sstride, h, filtery
    mova       m13, [pw_256]
    mova        m8, [filteryq+ 0]
    mova        m9, [filteryq+32]
    mova       m10, [filteryq+64]
    mova       m11, [filteryq+96]
.loop:
    movu        m0, [srcq-3]
    movu        m1, [srcq-2]
    movu        m2, [srcq-1]
    movu        m3, [srcq+0]
    movu        m4, [srcq+1]
    movu        m5, [srcq+2]
    movu        m6, [srcq+3]
    movu        m7, [srcq+4]
    add       srcq, sstrideq
    SBUTTERFLY  bw, 0, 1, 12
    SBUTTERFLY  bw, 2, 3, 12
    SBUTTERFLY  bw, 4, 5, 12
    SBUTTERFLY  bw, 6, 7, 12
    pmaddubsw   m0, m8
    pmaddubsw   m1, m8
    pmaddubsw   m2, m9
    pmaddubsw   m3, m9
    pmaddubsw   m4, m10
    pmaddubsw   m5, m10
    pmaddubsw   m6, m11
    pmaddubsw   m7, m11
    paddw       m0, m4
    paddw       m1, m5
    paddw       m2, m6
    paddw       m3, m7
    paddsw      m0, m2
    paddsw      m1, m3
    pmulhrsw    m0, m13
    pmulhrsw    m1, m13
    packuswb    m0, m1
%ifidn %1, avg
    pavgb       m0, [dstq]
%endif
    mova    [dstq], m0
    add       dstq, dstrideq
    dec         hd
    jg .loop
    RET
%endmacro

INIT_XMM ssse3
filter_hx2_fn put
filter_hx2_fn avg

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
filter_hx2_fn put
filter_hx2_fn avg
%endif

%endif ; ARCH_X86_64

%macro filter_sse2_v_fn 1
%assign %%px mmsize/2
%if ARCH_X86_64
cglobal vp9_%1_8tap_1d_v_ %+ %%px %+ _8, 6, 8, 15, dst, dstride, src, sstride, h, filtery, src4, sstride3
%else
cglobal vp9_%1_8tap_1d_v_ %+ %%px %+ _8, 4, 7, 15, dst, dstride, src, sstride, filtery, src4, sstride3
    mov   filteryq, r5mp
%define hd r4mp
%endif
    pxor        m5, m5
    mova        m6, [pw_64]
    lea  sstride3q, [sstrideq*3]
    lea      src4q, [srcq+sstrideq]
    sub       srcq, sstride3q
    mova        m7, [filteryq+  0]
%if ARCH_X86_64 && mmsize > 8
    mova        m8, [filteryq+ 16]
    mova        m9, [filteryq+ 32]
    mova       m10, [filteryq+ 48]
    mova       m11, [filteryq+ 64]
    mova       m12, [filteryq+ 80]
    mova       m13, [filteryq+ 96]
    mova       m14, [filteryq+112]
%endif
.loop:
    ; FIXME maybe reuse loads from previous rows, or just
    ; more generally unroll this to prevent multiple loads of
    ; the same data?
    movh        m0, [srcq]
    movh        m1, [srcq+sstrideq]
    movh        m2, [srcq+sstrideq*2]
    movh        m3, [srcq+sstride3q]
    add       srcq, sstrideq
    movh        m4, [src4q]
    punpcklbw   m0, m5
    punpcklbw   m1, m5
    punpcklbw   m2, m5
    punpcklbw   m3, m5
    punpcklbw   m4, m5
    pmullw      m0, m7
%if ARCH_X86_64 && mmsize > 8
    pmullw      m1, m8
    pmullw      m2, m9
    pmullw      m3, m10
    pmullw      m4, m11
%else
    pmullw      m1, [filteryq+ 16]
    pmullw      m2, [filteryq+ 32]
    pmullw      m3, [filteryq+ 48]
    pmullw      m4, [filteryq+ 64]
%endif
    paddw       m0, m1
    paddw       m2, m3
    paddw       m0, m4
    movh        m1, [src4q+sstrideq]
    movh        m3, [src4q+sstrideq*2]
    movh        m4, [src4q+sstride3q]
    add      src4q, sstrideq
    punpcklbw   m1, m5
    punpcklbw   m3, m5
    punpcklbw   m4, m5
%if ARCH_X86_64 && mmsize > 8
    pmullw      m1, m12
    pmullw      m3, m13
    pmullw      m4, m14
%else
    pmullw      m1, [filteryq+ 80]
    pmullw      m3, [filteryq+ 96]
    pmullw      m4, [filteryq+112]
%endif
    paddw       m0, m1
    paddw       m3, m4
    paddw       m0, m6
    paddw       m2, m3
    paddsw      m0, m2
    psraw       m0, 7
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

INIT_MMX mmxext
filter_sse2_v_fn put
filter_sse2_v_fn avg

INIT_XMM sse2
filter_sse2_v_fn put
filter_sse2_v_fn avg

%macro filter_v_fn 1
%assign %%px mmsize/2
%if ARCH_X86_64
cglobal vp9_%1_8tap_1d_v_ %+ %%px %+ _8, 6, 8, 11, dst, dstride, src, sstride, h, filtery, src4, sstride3
%else
cglobal vp9_%1_8tap_1d_v_ %+ %%px %+ _8, 4, 7, 11, dst, dstride, src, sstride, filtery, src4, sstride3
    mov   filteryq, r5mp
%define hd r4mp
%endif
    mova        m6, [pw_256]
    lea  sstride3q, [sstrideq*3]
    lea      src4q, [srcq+sstrideq]
    sub       srcq, sstride3q
    mova        m7, [filteryq+ 0]
%if ARCH_X86_64 && mmsize > 8
    mova        m8, [filteryq+32]
    mova        m9, [filteryq+64]
    mova       m10, [filteryq+96]
%endif
.loop:
    ; FIXME maybe reuse loads from previous rows, or just more generally
    ; unroll this to prevent multiple loads of the same data?
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
    pmaddubsw   m2, [filteryq+32]
    pmaddubsw   m4, [filteryq+64]
    pmaddubsw   m1, [filteryq+96]
%endif
    paddw       m0, m4
    paddw       m2, m1
    paddsw      m0, m2
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

%if ARCH_X86_64

%macro filter_vx2_fn 1
%assign %%px mmsize
cglobal vp9_%1_8tap_1d_v_ %+ %%px %+ _8, 6, 8, 14, dst, dstride, src, sstride, h, filtery, src4, sstride3
    mova       m13, [pw_256]
    lea  sstride3q, [sstrideq*3]
    lea      src4q, [srcq+sstrideq]
    sub       srcq, sstride3q
    mova        m8, [filteryq+ 0]
    mova        m9, [filteryq+32]
    mova       m10, [filteryq+64]
    mova       m11, [filteryq+96]
.loop:
    ; FIXME maybe reuse loads from previous rows, or just
    ; more generally unroll this to prevent multiple loads of
    ; the same data?
    movu        m0, [srcq]
    movu        m1, [srcq+sstrideq]
    movu        m2, [srcq+sstrideq*2]
    movu        m3, [srcq+sstride3q]
    movu        m4, [src4q]
    movu        m5, [src4q+sstrideq]
    movu        m6, [src4q+sstrideq*2]
    movu        m7, [src4q+sstride3q]
    add       srcq, sstrideq
    add      src4q, sstrideq
    SBUTTERFLY  bw, 0, 1, 12
    SBUTTERFLY  bw, 2, 3, 12
    SBUTTERFLY  bw, 4, 5, 12
    SBUTTERFLY  bw, 6, 7, 12
    pmaddubsw   m0, m8
    pmaddubsw   m1, m8
    pmaddubsw   m2, m9
    pmaddubsw   m3, m9
    pmaddubsw   m4, m10
    pmaddubsw   m5, m10
    pmaddubsw   m6, m11
    pmaddubsw   m7, m11
    paddw       m0, m4
    paddw       m1, m5
    paddw       m2, m6
    paddw       m3, m7
    paddsw      m0, m2
    paddsw      m1, m3
    pmulhrsw    m0, m13
    pmulhrsw    m1, m13
    packuswb    m0, m1
%ifidn %1, avg
    pavgb       m0, [dstq]
%endif
    mova    [dstq], m0
    add       dstq, dstrideq
    dec         hd
    jg .loop
    RET
%endmacro

INIT_XMM ssse3
filter_vx2_fn put
filter_vx2_fn avg

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
filter_vx2_fn put
filter_vx2_fn avg
%endif

%endif ; ARCH_X86_64

%macro fpel_fn 6-8 0, 4
%if %2 == 4
%define %%srcfn movh
%define %%dstfn movh
%else
%define %%srcfn movu
%define %%dstfn mova
%endif

%if %7 == 8
%define %%pavg pavgb
%define %%szsuf _8
%elif %7 == 16
%define %%pavg pavgw
%define %%szsuf _16
%else
%define %%szsuf
%endif

%if %2 <= mmsize
cglobal vp9_%1%2 %+ %%szsuf, 5, 7, 4, dst, dstride, src, sstride, h, dstride3, sstride3
    lea  sstride3q, [sstrideq*3]
    lea  dstride3q, [dstrideq*3]
%else
cglobal vp9_%1%2 %+ %%szsuf, 5, 5, %8, dst, dstride, src, sstride, h
%endif
.loop:
    %%srcfn     m0, [srcq]
    %%srcfn     m1, [srcq+s%3]
    %%srcfn     m2, [srcq+s%4]
    %%srcfn     m3, [srcq+s%5]
%if %2/mmsize == 8
    %%srcfn     m4, [srcq+mmsize*4]
    %%srcfn     m5, [srcq+mmsize*5]
    %%srcfn     m6, [srcq+mmsize*6]
    %%srcfn     m7, [srcq+mmsize*7]
%endif
    lea       srcq, [srcq+sstrideq*%6]
%ifidn %1, avg
    %%pavg      m0, [dstq]
    %%pavg      m1, [dstq+d%3]
    %%pavg      m2, [dstq+d%4]
%if %2 == 4
    %%srcfn     m4, [dstq+d%5]
    %%pavg      m3, m4
%else
    %%pavg      m3, [dstq+d%5]
%endif
%if %2/mmsize == 8
    %%pavg      m4, [dstq+mmsize*4]
    %%pavg      m5, [dstq+mmsize*5]
    %%pavg      m6, [dstq+mmsize*6]
    %%pavg      m7, [dstq+mmsize*7]
%endif
%endif
    %%dstfn [dstq], m0
    %%dstfn [dstq+d%3], m1
    %%dstfn [dstq+d%4], m2
    %%dstfn [dstq+d%5], m3
%if %2/mmsize == 8
    %%dstfn [dstq+mmsize*4], m4
    %%dstfn [dstq+mmsize*5], m5
    %%dstfn [dstq+mmsize*6], m6
    %%dstfn [dstq+mmsize*7], m7
%endif
    lea       dstq, [dstq+dstrideq*%6]
    sub         hd, %6
    jnz .loop
    RET
%endmacro

%define d16 16
%define s16 16
%define d32 32
%define s32 32
INIT_MMX mmx
fpel_fn put, 4,  strideq, strideq*2, stride3q, 4
fpel_fn put, 8,  strideq, strideq*2, stride3q, 4
INIT_MMX mmxext
fpel_fn avg, 4,  strideq, strideq*2, stride3q, 4, 8
fpel_fn avg, 8,  strideq, strideq*2, stride3q, 4, 8
INIT_XMM sse
fpel_fn put, 16, strideq, strideq*2, stride3q, 4
fpel_fn put, 32, mmsize,  strideq,   strideq+mmsize, 2
fpel_fn put, 64, mmsize,  mmsize*2,  mmsize*3, 1
fpel_fn put, 128, mmsize, mmsize*2,  mmsize*3, 1, 0, 8
INIT_XMM sse2
fpel_fn avg, 16, strideq, strideq*2, stride3q, 4, 8
fpel_fn avg, 32, mmsize,  strideq,   strideq+mmsize, 2, 8
fpel_fn avg, 64, mmsize,  mmsize*2,  mmsize*3, 1, 8
INIT_YMM avx
fpel_fn put, 32, strideq, strideq*2, stride3q, 4
fpel_fn put, 64, mmsize,  strideq,   strideq+mmsize, 2
fpel_fn put, 128, mmsize, mmsize*2,     mmsize*3, 1
%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
fpel_fn avg, 32, strideq, strideq*2, stride3q, 4, 8
fpel_fn avg, 64, mmsize,  strideq,   strideq+mmsize, 2, 8
%endif
INIT_MMX mmxext
fpel_fn avg,  8,  strideq, strideq*2, stride3q, 4, 16
INIT_XMM sse2
fpel_fn avg,  16, strideq, strideq*2, stride3q, 4, 16
fpel_fn avg,  32, mmsize,  strideq,   strideq+mmsize, 2, 16
fpel_fn avg,  64, mmsize,  mmsize*2,  mmsize*3, 1, 16
fpel_fn avg, 128, mmsize,  mmsize*2,  mmsize*3, 1, 16, 8
%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
fpel_fn avg,  32, strideq, strideq*2, stride3q, 4, 16
fpel_fn avg,  64, mmsize,  strideq,   strideq+mmsize, 2, 16
fpel_fn avg, 128, mmsize,  mmsize*2,  mmsize*3, 1, 16
%endif
%undef s16
%undef d16
%undef s32
%undef d32

%if HAVE_AVX512ICL_EXTERNAL && ARCH_X86_64
%macro PUT_8TAP_H 4-5 0 ; dst/src, tmp[1-3], vpermb
%if %5
    vpermb              m%2, m6, m%1
    vpermb              m%3, m7, m%1
    vpermb              m%4, m8, m%1
%else
%if %2 < %4 ; reuse a previous value if possible
    pshufb              m%2, m%1, m6
%endif
    pshufb              m%3, m%1, m7
    pshufb              m%4, m%1, m8
%endif
    mova                m%1, m5
    vpdpbusd            m%1, m%2, m9
    mova                m%2, m5
    vpdpbusd            m%2, m%3, m9
    vpdpbusd            m%1, m%3, m10
    vpdpbusd            m%2, m%4, m10
    packusdw            m%1, m%2
    psrlw               m%1, 7
%endmacro

%macro SPEL_H_INIT 2 ; put/avg, w
cglobal vp9_%1_8tap_smooth_%2h_8, 4, 7, 0
    lea                  r6, [vp9_spel_filter_smooth-8]
    jmp mangle(private_prefix %+ _vp9_%1_8tap_regular_%2h_8 %+ SUFFIX).main
cglobal vp9_%1_8tap_sharp_%2h_8, 4, 7, 0
    lea                  r6, [vp9_spel_filter_sharp-8]
    jmp mangle(private_prefix %+ _vp9_%1_8tap_regular_%2h_8 %+ SUFFIX).main
cglobal vp9_%1_8tap_regular_%2h_8, 4, 7, 0, dst, ds, src, ss, h, mx
    lea                  r6, [vp9_spel_filter_regular-8]
.main:
    mov                 mxd, mxm
    movifnidn            hd, hm
    sub                srcq, 3
    vpbroadcastd         m5, [pd_64]
    vpbroadcastd         m9, [r6+mxq*8+0]
    vpbroadcastd        m10, [r6+mxq*8+4]
%endmacro

%macro SPEL_V_INIT 2 ; put/avg, w
cglobal vp9_%1_8tap_smooth_%2v_8, 4, 7, 0
    lea                  r5, [vp9_spel_filter_smooth-8]
    jmp mangle(private_prefix %+ _vp9_%1_8tap_regular_%2v_8 %+ SUFFIX).main
cglobal vp9_%1_8tap_sharp_%2v_8, 4, 7, 0
    lea                  r5, [vp9_spel_filter_sharp-8]
    jmp mangle(private_prefix %+ _vp9_%1_8tap_regular_%2v_8 %+ SUFFIX).main
cglobal vp9_%1_8tap_regular_%2v_8, 4, 7, 0, dst, ds, src, ss, h, mx, my
    lea                  r5, [vp9_spel_filter_regular-8]
.main:
    mov                 myd, mym
    movifnidn            hd, hm
    lea                 myq, [r5+myq*8]
    vpbroadcastd         m7, [pw_256]
    vpbroadcastw         m8, [myq+0]
    vpbroadcastw         m9, [myq+2]
    lea                  r5, [ssq*3]
    vpbroadcastw        m10, [myq+4]
    sub                srcq, r5
    vpbroadcastw        m11, [myq+6]
%endmacro

%macro SPEL_HV_INIT 2 ; put/avg, w
cglobal vp9_%1_8tap_smooth_%2hv_8, 4, 8, 0
    lea                  r6, [vp9_spel_filter_smooth-8]
    jmp mangle(private_prefix %+ _vp9_%1_8tap_regular_%2hv_8 %+ SUFFIX).main
cglobal vp9_%1_8tap_sharp_%2hv_8, 4, 8, 0
    lea                  r6, [vp9_spel_filter_sharp-8]
    jmp mangle(private_prefix %+ _vp9_%1_8tap_regular_%2hv_8 %+ SUFFIX).main
cglobal vp9_%1_8tap_regular_%2hv_8, 4, 8, 0, dst, ds, src, ss, h, mx, my
    lea                  r6, [vp9_spel_filter_regular-8]
.main:
%if %2 == 16
    xor                r7d, r7d
.main2:
%endif
    mov                 mxd, mxm
    movifnidn            hd, hm
    sub                srcq, 3
    vpbroadcastd         m9, [r6+mxq*8+0]
    vpbroadcastd        m10, [r6+mxq*8+4]
    mov                 mxd, mym
    vpbroadcastd         m5, [pd_64]
    lea                 myq, [r6+mxq*8]
    lea                  r5, [ssq*3]
    sub                srcq, r5
%endmacro

%macro MC_AVX512 1 ; put/avg
    SPEL_H_INIT          %1, 4
    vbroadcasti32x4      m6, [spel_h_shufA]
    lea                  r5, [ssq*3]
    vbroadcasti32x4      m7, [spel_h_shufB]
    lea                  r6, [dsq*3]
    vbroadcasti32x4      m8, [spel_h_shufC]
.h_w4_loop:
    movu                xm0, [srcq+ssq*0]
    vinserti32x4        ym0, [srcq+ssq*1], 1
    vinserti32x4         m0, [srcq+ssq*2], 2
    vinserti32x4         m0, [srcq+r5   ], 3
    lea                srcq, [srcq+ssq*4]
    pshufb               m1, m0, m6
    pshufb               m0, m7
    mova                 m2, m5
    vpdpbusd             m2, m1, m9
    vpdpbusd             m2, m0, m10
    vpmovsdw            ym0, m2
    psraw               ym0, 7
    packuswb            ym0, ym0
    vextracti32x4       xm1, ym0, 1
%ifidn %1, avg
    movd               xmm2, [dstq+dsq*0]
    pinsrd             xmm2, [dstq+dsq*1], 1
    movd               xmm3, [dstq+dsq*2]
    pinsrd             xmm3, [dstq+r6   ], 1
    pavgb               xm0, xmm2
    pavgb               xm1, xmm3
%endif
    movd       [dstq+dsq*0], xm0
    pextrd     [dstq+dsq*1], xm0, 1
    movd       [dstq+dsq*2], xm1
    pextrd     [dstq+r6   ], xm1, 1
    lea                dstq, [dstq+dsq*4]
    sub                  hd, 4
    jg .h_w4_loop
    RET

    SPEL_H_INIT          %1, 8
    vbroadcasti32x4      m6, [spel_h_shufA]
    lea                  r5, [ssq*3]
    vbroadcasti32x4      m7, [spel_h_shufB]
    lea                  r6, [dsq*3]
    vbroadcasti32x4      m8, [spel_h_shufC]
.h_w8_loop:
    movu                xm0, [srcq+ssq*0]
    vinserti32x4        ym0, [srcq+ssq*1], 1
    vinserti32x4         m0, [srcq+ssq*2], 2
    vinserti32x4         m0, [srcq+r5   ], 3
    lea                srcq, [srcq+ssq*4]
    PUT_8TAP_H            0, 1, 2, 3
    vpmovuswb           ym0, m0
    vextracti32x4       xm1, ym0, 1
%ifidn %1, avg
    movq               xmm2, [dstq+dsq*0]
    movhps             xmm2, [dstq+dsq*1]
    movq               xmm3, [dstq+dsq*2]
    movhps             xmm3, [dstq+r6   ]
    pavgb               xm0, xmm2
    pavgb               xm1, xmm3
%endif
    movq       [dstq+dsq*0], xm0
    movhps     [dstq+dsq*1], xm0
    movq       [dstq+dsq*2], xm1
    movhps     [dstq+r6   ], xm1
    lea                dstq, [dstq+dsq*4]
    sub                  hd, 4
    jg .h_w8_loop
    RET

    SPEL_H_INIT          %1, 16
    mova                 m6, [spel_h_perm16]
    vpbroadcastd         m8, [pb_4]
    paddb                m7, m8, m6
    paddb                m8, m7
.h_w16_loop:
    movu                ym0, [srcq+ssq*0]
    vinserti32x8         m0, [srcq+ssq*1], 1
    lea                srcq, [srcq+ssq*2]
    PUT_8TAP_H            0, 1, 2, 3, 1
    vpmovuswb           ym0, m0
%ifidn %1, avg
    movu                xm1, [dstq+dsq*0]
    vinserti32x4        ym1, [dstq+dsq*1], 1
    pavgb               ym0, ym1
%endif
    mova         [dstq+dsq*0], xm0
    vextracti128 [dstq+dsq*1], ym0, 1
    lea                dstq, [dstq+dsq*2]
    sub                  hd, 2
    jg .h_w16_loop
    RET

    SPEL_H_INIT          %1, 32
    vbroadcasti32x4      m6, [spel_h_shufA]
    vbroadcasti32x4      m7, [spel_h_shufB]
    vbroadcasti32x4      m8, [spel_h_shufC]
.h_w32_loop:
    movu                ym0, [srcq+ssq*0+8*0]
    vinserti32x8         m0, [srcq+ssq*1+8*0], 1
    movu                ym1, [srcq+ssq*0+8*1]
    vinserti32x8         m1, [srcq+ssq*1+8*1], 1
    lea                srcq, [srcq+ssq*2]
    PUT_8TAP_H            0, 2, 3, 4
    PUT_8TAP_H            1, 4, 3, 2
    packuswb             m0, m1
%ifidn %1, avg
    movu                ym1, [dstq+dsq*0]
    vinserti32x8         m1, [dstq+dsq*1], 1
    pavgb                m0, m1
%endif
    mova          [dstq+dsq*0], ym0
    vextracti32x8 [dstq+dsq*1], m0, 1
    lea                dstq, [dstq+dsq*2]
    sub                  hd, 2
    jg .h_w32_loop
    RET

    SPEL_H_INIT          %1, 64
    vbroadcasti32x4      m6, [spel_h_shufA]
    vbroadcasti32x4      m7, [spel_h_shufB]
    vbroadcasti32x4      m8, [spel_h_shufC]
.h_w64_loop:
    movu                 m0, [srcq+8*0]
    movu                 m1, [srcq+8*1]
    add                srcq, ssq
    PUT_8TAP_H            0, 2, 3, 4
    PUT_8TAP_H            1, 4, 3, 2
    packuswb             m0, m1
%ifidn %1, avg
    pavgb                m0, [dstq]
%endif
    mova             [dstq], m0
    add                dstq, dsq
    dec                  hd
    jg .h_w64_loop
    RET

    SPEL_V_INIT          %1, 4
    movd               xmm2, [srcq+ssq*0]
    pinsrd             xmm2, [srcq+ssq*1], 1
    pinsrd             xmm2, [srcq+ssq*2], 2
    add                srcq, r5
    pinsrd             xmm2, [srcq+ssq*0], 3  ; 0 1 2 3
    movd               xmm3, [srcq+ssq*1]
    vpbroadcastd       xmm1, [srcq+ssq*2]
    add                srcq, r5
    vpbroadcastd       xmm0, [srcq+ssq*0]
    vpblendd           xmm3, xmm3, xmm1, 0x02 ; 4 5
    vpblendd           xmm1, xmm1, xmm0, 0x02 ; 5 6
    palignr            xmm4, xmm3, xmm2, 4    ; 1 2 3 4
    punpcklbw          xmm3, xmm1             ; 45 56
    punpcklbw          xmm1, xmm2, xmm4       ; 01 12
    punpckhbw          xmm2, xmm4             ; 23 34
%if WIN64
    movaps          [rsp+8], xmm6
%endif
.v_w4_loop:
    vpbroadcastd       xmm4, [srcq+ssq*1]
    lea                srcq, [srcq+ssq*2]
    pmaddubsw          xmm5, xmm1, xm8        ; a0 b0
    mova               xmm1, xmm2
    pmaddubsw          xmm6, xmm2, xm9        ; a1 b1
    mova               xmm2, xmm3
    pmaddubsw          xmm3, xm10             ; a2 b2
    paddw              xmm5, xmm3
    vpblendd           xmm3, xmm0, xmm4, 0x02 ; 6 7
    vpbroadcastd       xmm0, [srcq+ssq*0]
    vpblendd           xmm4, xmm0, 0x02       ; 7 8
    punpcklbw          xmm3, xmm4             ; 67 78
    pmaddubsw          xmm4, xmm3, xm11       ; a3 b3
    paddw              xmm6, xmm4
    paddsw             xmm5, xmm6
    pmulhrsw           xmm5, xm7
    packuswb           xmm5, xmm5
%ifidn %1, avg
    movd               xmm4, [dstq+dsq*0]
    pinsrd             xmm4, [dstq+dsq*1], 1
    pavgb              xmm5, xmm4
%endif
    movd       [dstq+dsq*0], xmm5
    pextrd     [dstq+dsq*1], xmm5, 1
    lea                dstq, [dstq+dsq*2]
    sub                  hd, 2
    jg .v_w4_loop
%if WIN64
    movaps             xmm6, [rsp+8]
%endif
    RET

    SPEL_V_INIT          %1, 8
    movq               xmm1, [srcq+ssq*0]
    vpbroadcastq       ymm0, [srcq+ssq*1]
    vpbroadcastq       ymm2, [srcq+ssq*2]
    add                srcq, r5
    vpbroadcastq       ymm5, [srcq+ssq*0]
    vpbroadcastq       ymm3, [srcq+ssq*1]
    vpbroadcastq       ymm4, [srcq+ssq*2]
    add                srcq, r5
    vpblendd           ymm1, ymm0, 0x30
    vpblendd           ymm0, ymm2, 0x30
    punpcklbw          ymm1, ymm0       ; 01 12
    vpbroadcastq       ymm0, [srcq+ssq*0]
    vpblendd           ymm2, ymm5, 0x30
    vpblendd           ymm5, ymm3, 0x30
    punpcklbw          ymm2, ymm5       ; 23 34
    vpblendd           ymm3, ymm4, 0x30
    vpblendd           ymm4, ymm0, 0x30
    punpcklbw          ymm3, ymm4       ; 45 56
%if WIN64
    movaps          [rsp+8], xmm6
%endif
.v_w8_loop:
    vpbroadcastq       ymm4, [srcq+ssq*1]
    lea                srcq, [srcq+ssq*2]
    pmaddubsw          ymm5, ymm1, ym8  ; a0 b0
    mova               ymm1, ymm2
    pmaddubsw          ymm6, ymm2, ym9  ; a1 b1
    mova               ymm2, ymm3
    pmaddubsw          ymm3, ym10       ; a2 b2
    paddw              ymm5, ymm3
    vpblendd           ymm3, ymm0, ymm4, 0x30
    vpbroadcastq       ymm0, [srcq+ssq*0]
    vpblendd           ymm4, ymm4, ymm0, 0x30
    punpcklbw          ymm3, ymm4       ; 67 78
    pmaddubsw          ymm4, ymm3, ym11 ; a3 b3
    paddw              ymm6, ymm4
    paddsw             ymm5, ymm6
    pmulhrsw           ymm5, ym7
    vextracti128       xmm4, ymm5, 1
    packuswb           xmm5, xmm4
%ifidn %1, avg
    movq               xmm4, [dstq+dsq*0]
    movhps             xmm4, [dstq+dsq*1]
    pavgb              xmm5, xmm4
%endif
    movq       [dstq+dsq*0], xmm5
    movhps     [dstq+dsq*1], xmm5
    lea                dstq, [dstq+dsq*2]
    sub                  hd, 2
    jg .v_w8_loop
%if WIN64
    movaps             xmm6, [rsp+8]
%endif
    vzeroupper
    RET

    SPEL_V_INIT          %1, 16
    mova                m12, [spel_v_perm16]
    vbroadcasti32x4      m1, [srcq+ssq*0]
    vbroadcasti32x4     ym4, [srcq+ssq*1]
    mov                 r6d, 0x0f
    vbroadcasti32x4      m2, [srcq+ssq*2]
    add                srcq, r5
    vbroadcasti32x4     ym5, [srcq+ssq*0]
    kmovb                k1, r6d
    vbroadcasti32x4      m3, [srcq+ssq*1]
    vbroadcasti32x4     ym6, [srcq+ssq*2]
    add                srcq, r5
    vbroadcasti32x4      m0, [srcq+ssq*0]
    vshufpd          m1{k1}, m4, m2, 0xcc
    vshufpd          m2{k1}, m5, m3, 0xcc
    vshufpd          m3{k1}, m6, m0, 0xcc
    vpermb               m1, m12, m1 ; 01 12
    vpermb               m2, m12, m2 ; 23 34
    vpermb               m3, m12, m3 ; 45 56
.v_w16_loop:
    pmaddubsw            m4, m1, m8  ; a0 b0
    mova                 m1, m2
    pmaddubsw            m5, m2, m9  ; a1 b1
    mova                 m2, m3
    pmaddubsw            m6, m3, m10 ; a2 b2
    mova                 m3, m0
    paddw                m4, m6
    vbroadcasti32x4     ym6, [srcq+ssq*1]
    lea                srcq, [srcq+ssq*2]
    vbroadcasti32x4      m0, [srcq+ssq*0]
    vshufpd          m3{k1}, m6, m0, 0xcc
    vpermb               m3, m12, m3 ; 67 78
    pmaddubsw            m6, m3, m11 ; a3 b3
    paddw                m5, m6
    paddsw               m4, m5
    pmulhrsw             m4, m7
    vextracti32x8       ym5, m4, 1
    packuswb            ym4, ym5
%ifidn %1, avg
    mova                xm5, [dstq+dsq*0]
    vinserti32x4        ym5, [dstq+dsq*1], 1
    pavgb               ym4, ym5
%endif
    mova          [dstq+dsq*0], xm4
    vextracti32x4 [dstq+dsq*1], ym4, 1
    lea                dstq, [dstq+dsq*2]
    sub                  hd, 2
    jg .v_w16_loop
    RET

    SPEL_V_INIT          %1, 32
    mova                m12, [spel_v_perm32]
    pmovzxbq            m14, [pb_02461357]
    vpshrdw             m13, m12, m12, 8
    movu                ym0, [srcq+ssq*0]
    vinserti32x8         m0, [srcq+ssq*1], 1
    vpermb               m1, m12, m0 ; 01
    vinserti32x8         m0, [srcq+ssq*2], 0
    add                srcq, r5
    vpermb               m2, m13, m0 ; 12
    vinserti32x8         m0, [srcq+ssq*0], 1
    vpermb               m3, m12, m0 ; 23
    vinserti32x8         m0, [srcq+ssq*1], 0
    vpermb               m4, m13, m0 ; 34
    vinserti32x8         m0, [srcq+ssq*2], 1
    add                srcq, r5
    vpermb               m5, m12, m0 ; 45
    vinserti32x8         m0, [srcq+ssq*0], 0
    vpermb               m6, m13, m0 ; 56
.v_w32_loop:
    vinserti32x8         m0, [srcq+ssq*1], 1
    lea                srcq, [srcq+ssq*2]
    pmaddubsw           m15, m1, m8
    mova                 m1, m3
    pmaddubsw           m16, m2, m8
    mova                 m2, m4
    pmaddubsw           m17, m3, m9
    mova                 m3, m5
    pmaddubsw           m18, m4, m9
    mova                 m4, m6
    pmaddubsw           m19, m5, m10
    vpermb               m5, m12, m0 ; 67
    vinserti32x8         m0, [srcq+ssq*0], 0
    pmaddubsw           m20, m6, m10
    vpermb               m6, m13, m0 ; 78
    paddw               m15, m19
    pmaddubsw           m19, m5, m11
    paddw               m16, m20
    pmaddubsw           m20, m6, m11
    paddw               m17, m19
    paddw               m18, m20
    paddsw              m15, m17
    paddsw              m16, m18
    pmulhrsw            m15, m7
    pmulhrsw            m16, m7
    packuswb            m15, m16
    vpermq              m15, m14, m15
%ifidn %1, avg
    mova               ym16, [dstq+dsq*0]
    vinserti32x8        m16, [dstq+dsq*1], 1
    pavgb               m15, m16
%endif
    mova          [dstq+dsq*0], ym15
    vextracti32x8 [dstq+dsq*1], m15, 1
    lea                dstq, [dstq+dsq*2]
    sub                  hd, 2
    jg .v_w32_loop
    vzeroupper
    RET

    SPEL_V_INIT          %1, 64
    movu                 m2, [srcq+ssq*0]
    movu                 m4, [srcq+ssq*1]
    movu                 m6, [srcq+ssq*2]
    add                srcq, r5
    movu                m13, [srcq+ssq*0]
    movu                m15, [srcq+ssq*1]
    movu                m17, [srcq+ssq*2]
    add                srcq, r5
    movu                 m0, [srcq+ssq*0]
    punpcklbw            m1, m2, m4   ; 01l
    punpckhbw            m2, m4       ; 01h
    punpcklbw            m3, m4, m6   ; 12l
    punpckhbw            m4, m6       ; 12h
    punpcklbw            m5, m6, m13  ; 23l
    punpckhbw            m6, m13      ; 23h
    punpcklbw           m12, m13, m15 ; 34l
    punpckhbw           m13, m15      ; 34h
    punpcklbw           m14, m15, m17 ; 45l
    punpckhbw           m15, m17      ; 45h
    punpcklbw           m16, m17, m0  ; 56l
    punpckhbw           m17, m0       ; 56h
%if WIN64
    movaps          [rsp+8], xmm6
%endif
.v_w64_loop:
    movu                m22, [srcq+ssq*1]
    pmaddubsw            m1, m8       ; a0l
    pmaddubsw           m18, m14, m10 ; a2l
    lea                srcq, [srcq+ssq*2]
    pmaddubsw            m2, m8       ; a0h
    pmaddubsw           m19, m15, m10 ; a2h
    paddw               m18, m1
    mova                 m1, m5
    paddw               m19, m2
    mova                 m2, m6
    pmaddubsw           m20, m5, m9   ; a1l
    mova                 m5, m14
    pmaddubsw           m21, m6, m9   ; a1h
    mova                 m6, m15
    punpcklbw           m14, m0, m22  ; 67l
    punpckhbw           m15, m0, m22  ; 67h
    pmaddubsw            m0, m14, m11 ; a3l
    paddw               m20, m0
    pmaddubsw            m0, m15, m11 ; a3h
    paddw               m21, m0
    movu                 m0, [srcq+ssq*0]
    paddsw              m18, m20
    paddsw              m19, m21
    pmaddubsw            m3, m8       ; b0l
    pmaddubsw           m20, m16, m10 ; b2l
    pmaddubsw            m4, m8       ; b0h
    pmaddubsw           m21, m17, m10 ; b2h
    pmulhrsw            m18, m7
    pmulhrsw            m19, m7
    paddw               m20, m3
    mova                 m3, m12
    paddw               m21, m4
    mova                 m4, m13
    packuswb            m18, m19
%ifidn %1, avg
    pavgb               m18, [dstq+dsq*0]
%endif
    mova       [dstq+dsq*0], m18
    pmaddubsw           m18, m12, m9  ; b1l
    mova                m12, m16
    punpcklbw           m16, m22, m0  ; 78l
    pmaddubsw           m19, m13, m9  ; b1h
    mova                m13, m17
    punpckhbw           m17, m22, m0  ; 78h
    pmaddubsw           m22, m16, m11 ; b3l
    paddw               m18, m22
    pmaddubsw           m22, m17, m11 ; b3h
    paddw               m19, m22
    paddsw              m18, m20
    paddsw              m19, m21
    pmulhrsw            m18, m7
    pmulhrsw            m19, m7
    packuswb            m18, m19
%ifidn %1, avg
    pavgb               m18, [dstq+dsq*1]
%endif
    mova       [dstq+dsq*1], m18
    lea                dstq, [dstq+dsq*2]
    sub                  hd, 2
    jg .v_w64_loop
%if WIN64
    movaps             xmm6, [rsp+8]
%endif
    vzeroupper
    RET

    SPEL_HV_INIT         %1, 4
    vbroadcasti32x4     ym2, [srcq+ssq*0]
    vinserti32x4         m2, [srcq+ssq*1], 2
    vbroadcasti32x4      m6, [spel_h_shufA]
    vinserti32x4         m2, [srcq+ssq*2], 3 ; _ 0 1 2
    add                srcq, r5
    movu                xm0, [srcq+ssq*0]
    vinserti32x4        ym0, [srcq+ssq*1], 1
    vbroadcasti32x4      m7, [spel_h_shufB]
    vinserti32x4         m0, [srcq+ssq*2], 2
    add                srcq, r5
    vpbroadcastd        m11, [myq+0]
    vinserti32x4         m0, [srcq+ssq*0], 3 ; 3 4 5 6
    vpbroadcastd        m12, [myq+4]
    lea                  r6, [dsq*3]
    mova                 m8, [spel_hv_perm4]
    pshufb               m4, m2, m6
    mova                 m1, m5
    vpdpbusd             m1, m4, m9
    pshufb               m4, m0, m6
    mova                 m3, m5
    vpdpbusd             m3, m4, m9
    pshufb               m2, m7
    pshufb               m0, m7
    vpdpbusd             m1, m2, m10
    vpdpbusd             m3, m0, m10
    psrad                m1, 7
    psrad                m0, m3, 7
    packuswb             m1, m0     ; _3   04   15   26
    vpermb               m1, m8, m1 ; 0123 1234 2345 3456
.hv_w4_loop:
    movu                xm4, [srcq+ssq*1]
    vinserti32x4        ym4, [srcq+ssq*2], 1
    vinserti32x4         m4, [srcq+r5   ], 2
    lea                srcq, [srcq+ssq*4]
    vinserti32x4         m4, [srcq+ssq*0], 3 ; 7 8 9 a
    mova                 m3, m5
    pshufb               m2, m4, m6
    vpdpbusd             m3, m2, m9
    mova                 m2, m5
    vpdpbusd             m2, m1, m11
    pshufb               m4, m7
    vpdpbusd             m3, m4, m10
    psrad                m3, 7
    packuswb             m1, m0, m3 ; 37   48   59   6a
    mova                 m0, m3
    vpermb               m1, m8, m1 ; 4567 5678 6789 789a
    vpdpbusd             m2, m1, m12
    psrad                m2, 7
    vpmovdw             ym2, m2
    packuswb            ym2, ym2
    vextracti32x4       xm3, ym2, 1
%ifidn %1, avg
    movd               xmm4, [dstq+dsq*0]
    pinsrd             xmm4, [dstq+dsq*1], 1
    pavgb               xm2, xmm4
    movd               xmm4, [dstq+dsq*2]
    pinsrd             xmm4, [dstq+r6   ], 1
    pavgb               xm3, xmm4
%endif
    movd       [dstq+dsq*0], xm2
    pextrd     [dstq+dsq*1], xm2, 1
    movd       [dstq+dsq*2], xm3
    pextrd     [dstq+r6   ], xm3, 1
    lea                dstq, [dstq+dsq*4]
    sub                  hd, 4
    jg .hv_w4_loop
    RET

    SPEL_HV_INIT         %1, 8
    vbroadcasti32x4     ym2, [srcq+ssq*0]
    vinserti32x4         m2, [srcq+ssq*1], 2
    vbroadcasti32x4      m6, [spel_h_shufA]
    vinserti32x4         m2, [srcq+ssq*2], 3 ; _ 0 1 2
    add                srcq, r5
    movu                xm0, [srcq+ssq*0]
    vinserti32x4        ym0, [srcq+ssq*1], 1
    vbroadcasti32x4      m7, [spel_h_shufB]
    vinserti32x4         m0, [srcq+ssq*2], 2
    add                srcq, r5
    vpbroadcastd        m11, [myq+0]
    vinserti32x4         m0, [srcq+ssq*0], 3 ; 3 4 5 6
    vpbroadcastd        m12, [myq+4]
    lea                  r6, [dsq*3]
    vbroadcasti32x4      m8, [spel_h_shufC]
    mova                m13, [spel_hv_perm8]
    vpaddd              m14, m13, [pb_4] {1to16}
    PUT_8TAP_H            2, 1, 3, 4
    PUT_8TAP_H            0, 1, 3, 4
    packuswb             m2, m0      ; _3   04   15   26
    vpermb               m1, m13, m2 ; 0123 1234 2345 3456 (abcd)
    vpermb               m2, m14, m2 ; 0123 1234 2345 3456 (efgh)
.hv_w8_loop:
    movu               xm18, [srcq+ssq*1]
    vinserti128        ym18, [srcq+ssq*2], 1
    vinserti32x4        m18, [srcq+r5   ], 2
    lea                srcq, [srcq+ssq*4]
    vinserti32x4        m18, [srcq+ssq*0], 3 ; 7 8 9 a
    PUT_8TAP_H           18, 4, 16, 17
    mova                m16, m5
    vpdpbusd            m16, m1, m11
    mova                m17, m5
    vpdpbusd            m17, m2, m11
    packuswb             m2, m0, m18 ; 37   48   59   6a
    mova                 m0, m18
    vpermb               m1, m13, m2 ; 4567 5678 6789 789a (abcd)
    vpermb               m2, m14, m2 ; 4567 5678 6789 789a (efgh)
    vpdpbusd            m16, m1, m12
    vpdpbusd            m17, m2, m12
    packusdw            m16, m17
    psrlw               m16, 7
    vpmovuswb          ym16, m16
    vextracti128       xm17, ym16, 1
%ifidn %1, avg
    movq               xm18, [dstq+dsq*0]
    movhps             xm18, [dstq+dsq*1]
    pavgb              xm16, xm18
    movq               xm18, [dstq+dsq*2]
    movhps             xm18, [dstq+r6   ]
    pavgb              xm17, xm18
%endif
    movq       [dstq+dsq*0], xm16
    movhps     [dstq+dsq*1], xm16
    movq       [dstq+dsq*2], xm17
    movhps     [dstq+r6   ], xm17
    lea                dstq, [dstq+dsq*4]
    sub                  hd, 4
    jg .hv_w8_loop
    vzeroupper
    RET

cglobal vp9_%1_8tap_smooth_32hv_8, 4, 8, 0
    lea                  r6, [vp9_spel_filter_smooth-8]
    mov                 r7d, 256*1
    jmp mangle(private_prefix %+ _vp9_%1_8tap_regular_16hv_8 %+ SUFFIX).main2
cglobal vp9_%1_8tap_sharp_32hv_8, 4, 8, 0
    lea                  r6, [vp9_spel_filter_sharp-8]
    mov                 r7d, 256*1
    jmp mangle(private_prefix %+ _vp9_%1_8tap_regular_16hv_8 %+ SUFFIX).main2
cglobal vp9_%1_8tap_regular_32hv_8, 4, 8, 0, dst, ds, src, ss, h, mx, my
    lea                  r6, [vp9_spel_filter_regular-8]
    mov                 r7d, 256*1
    jmp mangle(private_prefix %+ _vp9_%1_8tap_regular_16hv_8 %+ SUFFIX).main2
cglobal vp9_%1_8tap_smooth_64hv_8, 4, 8, 0
    lea                  r6, [vp9_spel_filter_smooth-8]
    mov                 r7d, 256*3
    jmp mangle(private_prefix %+ _vp9_%1_8tap_regular_16hv_8 %+ SUFFIX).main2
cglobal vp9_%1_8tap_sharp_64hv_8, 4, 8, 0
    lea                  r6, [vp9_spel_filter_sharp-8]
    mov                 r7d, 256*3
    jmp mangle(private_prefix %+ _vp9_%1_8tap_regular_16hv_8 %+ SUFFIX).main2
cglobal vp9_%1_8tap_regular_64hv_8, 4, 8, 0, dst, ds, src, ss, h, mx, my
    lea                  r6, [vp9_spel_filter_regular-8]
    mov                 r7d, 256*3
    jmp mangle(private_prefix %+ _vp9_%1_8tap_regular_16hv_8 %+ SUFFIX).main2

    SPEL_HV_INIT         %1, 16
    vpbroadcastw        m11, [myq+0]
    mova                 m6, [spel_h_perm16]
    vpbroadcastw        m12, [myq+2]
    vpbroadcastd         m8, [pb_4]
    vpbroadcastw        m13, [myq+4]
    vpbroadcastd        m15, [pw_256]
    vpbroadcastw        m14, [myq+6]
    mova                m19, [spel_hv_perm16]
    vpandd              m20, m19, [pw_m33] {1to16} ; even indices & ~32
    paddb                m7, m6, m8
    lea                 r6d, [hq+r7]
    paddb                m8, m7
%if WIN64
    push                 r8
%endif
.hv_w16_loop0:
    movu               ym16, [srcq+ssq*0]    ; 0
    movu               ym17, [srcq+ssq*1]
    lea                  r7, [srcq+r5]
    vinserti32x8        m17, [srcq+ssq*2], 1 ; 1 2
    movu               ym18, [r7+ssq*0]
    mov                  r8, dstq
    vinserti32x8        m18, [r7+ssq*1], 1   ; 3 4
    movu                ym0, [r7+ssq*2]
    add                  r7, r5
    vinserti32x8         m0, [r7+ssq*0], 1   ; 5 6
INIT_YMM avx512icl
    PUT_8TAP_H           16, 1, 2, 3, 1
INIT_ZMM avx512icl
    PUT_8TAP_H           17, 1, 2, 3, 1
    PUT_8TAP_H           18, 1, 2, 3, 1
    PUT_8TAP_H            0, 1, 2, 3, 1
    packuswb            m16, m17
    packuswb            m17, m18
    packuswb            m18, m0
    vpermb               m1, m20, m16 ; 01 12
    vpermb               m2, m19, m17 ; 23 34
    vpermb               m3, m19, m18 ; 45 56
.hv_w16_loop:
    movu               ym18, [r7+ssq*1]
    lea                  r7, [r7+ssq*2]
    vinserti32x8        m18, [r7+ssq*0], 1
    PUT_8TAP_H           18, 4, 16, 17, 1
    pmaddubsw           m16, m1, m11 ; a0 b0
    mova                 m1, m2
    pmaddubsw           m17, m2, m12 ; a1 b1
    mova                 m2, m3
    pmaddubsw            m3, m13     ; a2 b2
    packuswb             m4, m0, m18
    paddw               m16, m3
    vpermb               m3, m19, m4 ; 67 78
    mova                 m0, m18
    pmaddubsw            m4, m3, m14 ; a3 b3
    paddw               m17, m4
    paddsw              m16, m17
    pmulhrsw            m16, m15
    vextracti32x8      ym17, m16, 1
    packuswb           ym16, ym17
%ifidn %1, avg
    mova               xm17, [r8+dsq*0]
    vinserti128        ym17, [r8+dsq*1], 1
    pavgb              ym16, ym17
%endif
    mova         [r8+dsq*0], xm16
    vextracti128 [r8+dsq*1], ym16, 1
    lea                  r8, [r8+dsq*2]
    sub                  hd, 2
    jg .hv_w16_loop
    add                srcq, 16
    add                dstq, 16
    movzx                hd, r6b
    sub                 r6d, 1<<8
    jg .hv_w16_loop0
    vzeroupper
%if WIN64
    pop                  r8
%endif
    RET
%endmacro

INIT_ZMM avx512icl
MC_AVX512 put
MC_AVX512 avg

%endif
