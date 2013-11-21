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

pw_11585x2: times 8 dw 23170

%macro VP9_IDCT_COEFFS 2
pw_m%1_%2: dw -%1, %2, -%1, %2, -%1, %2, -%1, %2
pw_%2_%1:  dw  %2, %1,  %2, %1,  %2, %1,  %2, %1
%endmacro

%macro VP9_IDCT_COEFFS_ALL 2
pw_%1x2: times 8 dw %1*2
pw_%2x2: times 8 dw %2*2
VP9_IDCT_COEFFS %1, %2
%endmacro

VP9_IDCT_COEFFS_ALL 15137,  6270
VP9_IDCT_COEFFS_ALL 16069,  3196
VP9_IDCT_COEFFS      9102, 13623

pd_8192: times 4 dd 8192
pw_2048: times 8 dw 2048
pw_1024: times 8 dw 1024

SECTION .text

;
; IDCT helpers
;

; (a*x + b*y + round) >> shift
%macro VP9_MULSUB_2W_2X 6 ; dst1, dst2, src (unchanged), round, coefs1, coefs2
    pmaddwd            m%1, m%3, %5
    pmaddwd            m%2, m%3, %6
    paddd              m%1, m%4
    paddd              m%2, m%4
    psrad              m%1, 14
    psrad              m%2, 14
%endmacro

%macro VP9_UNPACK_MULSUB_2W_4X 4 ; dst1, dst2, coef1, coef2
    punpckhwd           m6, m%2, m%1
    VP9_MULSUB_2W_2X     4, 5,  6, 7, [pw_m%3_%4], [pw_%4_%3]
    punpcklwd          m%2, m%1
    VP9_MULSUB_2W_2X    %1, 6, %2, 7, [pw_m%3_%4], [pw_%4_%3]
    packssdw           m%1, m4
    packssdw            m6, m5
    SWAP                %2, 6
%endmacro

%macro VP9_STORE_2X 2
    movh                m6, [dstq]
    movh                m7, [dstq+strideq]
    punpcklbw           m6, m4
    punpcklbw           m7, m4
    paddw               m6, %1
    paddw               m7, %2
    packuswb            m6, m4
    packuswb            m7, m4
    movh            [dstq], m6
    movh    [dstq+strideq], m7
%endmacro

;-------------------------------------------------------------------------------------------
; void vp9_idct_idct_4x4_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;-------------------------------------------------------------------------------------------

%macro VP9_IDCT4_1D_FINALIZE 0
    SUMSUB_BA            w, 3, 2, 4                         ; m3=t3+t0, m2=-t3+t0
    SUMSUB_BA            w, 1, 0, 4                         ; m1=t2+t1, m0=-t2+t1
    SWAP                 0, 3, 2                            ; 3102 -> 0123
%endmacro

%macro VP9_IDCT4_1D 0
    SUMSUB_BA            w, 2, 0, 4                         ; m2=IN(0)+IN(2) m0=IN(0)-IN(2)
    mova                m4, [pw_11585x2]
    pmulhrsw            m2, m4                              ; m2=t0
    pmulhrsw            m0, m4                              ; m0=t1
    VP9_UNPACK_MULSUB_2W_4X 1, 3, 15137, 6270               ; m1=t2, m3=t3
    VP9_IDCT4_1D_FINALIZE
%endmacro

; 2x2 top left corner
%macro VP9_IDCT4_2x2_1D 0
    pmulhrsw            m0, m5                              ; m0=t1
    mova                m2, m0                              ; m2=t0
    mova                m3, m1
    pmulhrsw            m1, m6                              ; m1=t2
    pmulhrsw            m3, m7                              ; m3=t3
    VP9_IDCT4_1D_FINALIZE
%endmacro

%macro VP9_IDCT4_WRITEOUT 0
    mova                m5, [pw_2048]
    pmulhrsw            m0, m5              ; (x*2048 + (1<<14))>>15 <=> (x+8)>>4
    pmulhrsw            m1, m5
    VP9_STORE_2X        m0, m1
    lea               dstq, [dstq+2*strideq]
    pmulhrsw            m2, m5
    pmulhrsw            m3, m5
    VP9_STORE_2X        m2, m3
%endmacro

INIT_MMX ssse3
cglobal vp9_idct_idct_4x4_add, 4,4,0, dst, stride, block, eob

    cmp eobd, 4 ; 2x2 or smaller
    jg .idctfull

    cmp eobd, 1 ; faster path for when only DC is set
    jne .idct2x2

    movd                m0, [blockq]
    mova                m5, [pw_11585x2]
    pmulhrsw            m0, m5
    pmulhrsw            m0, m5
    pshufw              m0, m0, 0
    pxor                m4, m4
    movh          [blockq], m4
    mova                m5, [pw_2048]
    pmulhrsw            m0, m5              ; (x*2048 + (1<<14))>>15 <=> (x+8)>>4
    VP9_STORE_2X        m0, m0
    lea               dstq, [dstq+2*strideq]
    VP9_STORE_2X        m0, m0
    RET

; faster path for when only top left 2x2 block is set
.idct2x2:
    movd                m0, [blockq+0]
    movd                m1, [blockq+8]
    mova                m5, [pw_11585x2]
    mova                m6, [pw_6270x2]
    mova                m7, [pw_15137x2]
    VP9_IDCT4_2x2_1D
    TRANSPOSE4x4W  0, 1, 2, 3, 4
    VP9_IDCT4_2x2_1D
    pxor                m4, m4  ; used for the block reset, and VP9_STORE_2X
    movh       [blockq+ 0], m4
    movh       [blockq+ 8], m4
    VP9_IDCT4_WRITEOUT
    RET

.idctfull: ; generic full 4x4 idct/idct
    mova                m0, [blockq+ 0]
    mova                m1, [blockq+ 8]
    mova                m2, [blockq+16]
    mova                m3, [blockq+24]
    mova                m7, [pd_8192]       ; rounding
    VP9_IDCT4_1D
    TRANSPOSE4x4W  0, 1, 2, 3, 4
    VP9_IDCT4_1D
    pxor                m4, m4  ; used for the block reset, and VP9_STORE_2X
    mova       [blockq+ 0], m4
    mova       [blockq+ 8], m4
    mova       [blockq+16], m4
    mova       [blockq+24], m4
    VP9_IDCT4_WRITEOUT
    RET

;-------------------------------------------------------------------------------------------
; void vp9_idct_idct_8x8_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;-------------------------------------------------------------------------------------------

%if ARCH_X86_64 ; TODO: 32-bit? (32-bit limited to 8 xmm reg, we use 13 here)
%macro VP9_IDCT8_1D_FINALIZE 0
    SUMSUB_BA            w,  3, 10, 4                       ;  m3=t0+t7, m10=t0-t7
    SUMSUB_BA            w,  1,  2, 4                       ;  m1=t1+t6,  m2=t1-t6
    SUMSUB_BA            w, 11,  0, 4                       ; m11=t2+t5,  m0=t2-t5
    SUMSUB_BA            w,  9,  8, 4                       ;  m9=t3+t4,  m8=t3-t4
    SWAP                11, 10, 2
    SWAP                 3,  9, 0
%endmacro

%macro VP9_IDCT8_1D 0
    SUMSUB_BA            w, 8, 0, 4                         ; m8=IN(0)+IN(4) m0=IN(0)-IN(4)
    pmulhrsw            m8, m12                             ; m8=t0a
    pmulhrsw            m0, m12                             ; m0=t1a
    VP9_UNPACK_MULSUB_2W_4X 2, 10, 15137,  6270             ; m2=t2a, m10=t3a
    VP9_UNPACK_MULSUB_2W_4X 1, 11, 16069,  3196             ; m1=t4a, m11=t7a
    VP9_UNPACK_MULSUB_2W_4X 9,  3,  9102, 13623             ; m9=t5a,  m3=t6a
    SUMSUB_BA            w, 10,  8, 4                       ; m10=t0a+t3a (t0),  m8=t0a-t3a (t3)
    SUMSUB_BA            w,  2,  0, 4                       ;  m2=t1a+t2a (t1),  m0=t1a-t2a (t2)
    SUMSUB_BA            w,  9,  1, 4                       ;  m9=t4a+t5a (t4),  m1=t4a-t5a (t5a)
    SUMSUB_BA            w,  3, 11, 4                       ;  m3=t7a+t6a (t7), m11=t7a-t6a (t6a)
    SUMSUB_BA            w,  1, 11, 4                       ;  m1=t6a+t5a (t6), m11=t6a-t5a (t5)
    pmulhrsw            m1, m12                             ; m1=t6
    pmulhrsw           m11, m12                             ; m11=t5
    VP9_IDCT8_1D_FINALIZE
%endmacro

; TODO: a lot of t* copies can probably be removed and merged with
; following SUMSUBs from VP9_IDCT8_1D_FINALIZE with AVX
%macro VP9_IDCT8_2x2_1D 0
    pmulhrsw            m0, m12                             ;  m0=t0
    mova                m3, m1
    pmulhrsw            m1, m6                              ;  m1=t4
    pmulhrsw            m3, m7                              ;  m3=t7
    mova                m2, m0                              ;  m2=t1
    mova               m10, m0                              ; m10=t2
    mova                m8, m0                              ;  m8=t3
    mova               m11, m3                              ; t5 = t7a ...
    mova                m9, m3                              ; t6 = t7a ...
    psubw              m11, m1                              ; t5 = t7a - t4a
    paddw               m9, m1                              ; t6 = t7a + t4a
    pmulhrsw           m11, m12                             ; m11=t5
    pmulhrsw            m9, m12                             ;  m9=t6
    SWAP                 0, 10
    SWAP                 9,  1
    VP9_IDCT8_1D_FINALIZE
%endmacro

%macro VP9_IDCT8_WRITEOUT 0
    mova                m5, [pw_1024]
    pmulhrsw            m0, m5              ; (x*1024 + (1<<14))>>15 <=> (x+16)>>5
    pmulhrsw            m1, m5
    VP9_STORE_2X        m0, m1
    lea               dstq, [dstq+2*strideq]
    pmulhrsw            m2, m5
    pmulhrsw            m3, m5
    VP9_STORE_2X        m2, m3
    lea               dstq, [dstq+2*strideq]
    pmulhrsw            m8, m5
    pmulhrsw            m9, m5
    VP9_STORE_2X        m8, m9
    lea               dstq, [dstq+2*strideq]
    pmulhrsw           m10, m5
    pmulhrsw           m11, m5
    VP9_STORE_2X       m10, m11
%endmacro

INIT_XMM ssse3
cglobal vp9_idct_idct_8x8_add, 4,4,13, dst, stride, block, eob

    mova               m12, [pw_11585x2]    ; often used

    cmp eobd, 3 ; top left corner or less
    jg .idctfull

    cmp eobd, 1 ; faster path for when only DC is set
    jne .idcttopleftcorner

    movd                m0, [blockq]
    pmulhrsw            m0, m12
    pmulhrsw            m0, m12
    SPLATW              m0, m0, 0
    pxor                m4, m4
    movd          [blockq], m4
    mova                m5, [pw_1024]
    pmulhrsw            m0, m5              ; (x*1024 + (1<<14))>>15 <=> (x+16)>>5
    VP9_STORE_2X        m0, m0
    lea               dstq, [dstq+2*strideq]
    VP9_STORE_2X        m0, m0
    lea               dstq, [dstq+2*strideq]
    VP9_STORE_2X        m0, m0
    lea               dstq, [dstq+2*strideq]
    VP9_STORE_2X        m0, m0
    RET

; faster path for when only left corner is set (3 input: DC, right to DC, below
; to DC). Note: also working with a 2x2 block
.idcttopleftcorner:
    movd                m0, [blockq+0]
    movd                m1, [blockq+16]
    mova                m6, [pw_3196x2]
    mova                m7, [pw_16069x2]
    VP9_IDCT8_2x2_1D
    TRANSPOSE8x8W  0, 1, 2, 3, 8, 9, 10, 11, 4
    VP9_IDCT8_2x2_1D
    pxor                m4, m4  ; used for the block reset, and VP9_STORE_2X
    movd       [blockq+ 0], m4
    movd       [blockq+16], m4
    VP9_IDCT8_WRITEOUT
    RET

.idctfull: ; generic full 8x8 idct/idct
    mova                m0, [blockq+  0]    ; IN(0)
    mova                m1, [blockq+ 16]    ; IN(1)
    mova                m2, [blockq+ 32]    ; IN(2)
    mova                m3, [blockq+ 48]    ; IN(3)
    mova                m8, [blockq+ 64]    ; IN(4)
    mova                m9, [blockq+ 80]    ; IN(5)
    mova               m10, [blockq+ 96]    ; IN(6)
    mova               m11, [blockq+112]    ; IN(7)
    mova                m7, [pd_8192]       ; rounding
    VP9_IDCT8_1D
    TRANSPOSE8x8W  0, 1, 2, 3, 8, 9, 10, 11, 4
    VP9_IDCT8_1D
    pxor                m4, m4  ; used for the block reset, and VP9_STORE_2X
    mova      [blockq+  0], m4
    mova      [blockq+ 16], m4
    mova      [blockq+ 32], m4
    mova      [blockq+ 48], m4
    mova      [blockq+ 64], m4
    mova      [blockq+ 80], m4
    mova      [blockq+ 96], m4
    mova      [blockq+112], m4
    VP9_IDCT8_WRITEOUT
    RET
%endif


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
