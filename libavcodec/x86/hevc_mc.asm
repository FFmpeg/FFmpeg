;*****************************************************************************
;* x86-optimized HEVC MC
;* Copyright 2015 Anton Khirnov
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

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

pw_1023: times 8 dw 1023

cextern hevc_qpel_coeffs
cextern hevc_qpel_coeffs8

cextern hevc_epel_coeffs
cextern hevc_epel_coeffs8

cextern pw_8
cextern pw_16
cextern pw_32
cextern pw_64

SECTION .text

; %1: width
; %2: bit depth
%macro COMMON_DEFS 2
    %assign blocksize            8
    %assign nb_blocks            ((%1 + blocksize - 1) / blocksize)
    %define last_block_truncated (blocksize * nb_blocks > %1)
    %if %2 > 8
        %define LOAD_BLOCK     movu
        %define LOAD_HALFBLOCK movq
        %assign pixelsize      2
    %else
        %define LOAD_BLOCK     movq
        %define LOAD_HALFBLOCK movd
        %assign pixelsize      1
    %endif
    %define STORE_BLOCK        mova
    %define STORE_HALFBLOCK    movq
%endmacro

; %1: block index
%macro BLOCK_DEFS 1
    %if last_block_truncated && %1 == nb_blocks - 1
        %define block_truncated 1
        %define LOAD            LOAD_HALFBLOCK
        %define STORE           STORE_HALFBLOCK
    %else
        %define block_truncated 0
        %define LOAD            LOAD_BLOCK
        %define STORE           STORE_BLOCK
    %endif
%endmacro


; hevc_get_pixels_<w>_<d>(int16_t *dst, ptrdiff_t dststride,
;                         pixel   *src, ptrdiff_t srcstride,
;                         int height, int mx, int my, int *mcbuffer)

; %1: block width
; %2: bit depth
; %3: log2 of height unroll
%macro GET_PIXELS 3
cglobal hevc_get_pixels_ %+ %1 %+ _ %+ %2, 5, 5, 2, dst, dststride, src, srcstride, height ; rest of the args unused

    %assign shift 14 - %2
    COMMON_DEFS %1, %2

%if pixelsize == 1
    pxor      m0, m0
%endif

    shr       heightd, %3

.loop:

%assign i 0
%rep (1 << %3)

%assign j 0
%rep nb_blocks

    BLOCK_DEFS j

    LOAD       m1, [srcq + j * pixelsize * blocksize]
%if pixelsize == 1
    punpcklbw  m1, m0
%endif
    psllw      m1, shift
    STORE      [dstq + j * 2 * blocksize], m1

%assign j (j + 1)
%endrep

    add       dstq, dststrideq
    add       srcq, srcstrideq

%assign i (i + 1)
%endrep

    dec heightd
    jg .loop
    RET
%endmacro

INIT_XMM sse2
GET_PIXELS 4,  8, 1
GET_PIXELS 8,  8, 1
GET_PIXELS 12, 8, 3
GET_PIXELS 16, 8, 2
GET_PIXELS 24, 8, 3
GET_PIXELS 32, 8, 3
GET_PIXELS 48, 8, 3
GET_PIXELS 64, 8, 3

GET_PIXELS 4,  10, 1
GET_PIXELS 8,  10, 1
GET_PIXELS 12, 10, 3
GET_PIXELS 16, 10, 2
GET_PIXELS 24, 10, 3
GET_PIXELS 32, 10, 3
GET_PIXELS 48, 10, 3
GET_PIXELS 64, 10, 3

; hevc_qpel_h/v_<w>_8(int16_t *dst, ptrdiff_t dststride,
;                     uint8_t *src, ptrdiff_t srcstride,
;                     int height, int mx, int my, int *mcbuffer)

; 8-bit qpel interpolation
; %1: block width
; %2: 0 - horizontal; 1 - vertical
%macro QPEL_8 2
%if %2
    %define postfix    v
    %define mvfrac     myq
    %define coeffsaddr r5q
    %define pixstride  srcstrideq
    %define pixstride3 r5q
    %define src_m3     r6q
%else
    %define postfix    h
    %define mvfrac     mxq
    %define coeffsaddr r6q
    %define pixstride  1
    %define pixstride3 3
    %define src_m3     (srcq - 3)
%endif

    COMMON_DEFS %1, 8

cglobal hevc_qpel_ %+ postfix %+ _ %+ %1 %+ _8, 7, 7, 7, dst, dststride, src, srcstride, height, mx, my
    and       mvfrac, 0x3
    dec       mvfrac
    shl       mvfrac, 4
    lea       coeffsaddr, [hevc_qpel_coeffs8]
    mova      m0,         [coeffsaddr + mvfrac]

    SPLATW    m1, m0, 1
    SPLATW    m2, m0, 2
    SPLATW    m3, m0, 3
    SPLATW    m0, m0, 0

%if %2
    lea       pixstride3, [srcstrideq + 2 * srcstrideq]
    mov       src_m3, srcq
    sub       src_m3, pixstride3
%endif

.loop

%assign i 0
%rep nb_blocks

    BLOCK_DEFS i

    LOAD m4, [src_m3 + i * blocksize]
    LOAD m5, [src_m3 + i * blocksize + 1 * pixstride]
    punpcklbw m4, m5
    pmaddubsw m4, m0

    LOAD m5, [src_m3 + i * blocksize + 2 * pixstride]
    LOAD m6, [srcq   + i * blocksize]
    punpcklbw m5, m6
    pmaddubsw m5, m1
    paddsw    m4, m5

    LOAD m5, [srcq + i * blocksize + 1 * pixstride]
    LOAD m6, [srcq + i * blocksize + 2 * pixstride]
    punpcklbw m5, m6
    pmaddubsw m5, m2
    paddsw    m4, m5

    LOAD m5, [srcq + i * blocksize +     pixstride3]
    LOAD m6, [srcq + i * blocksize + 4 * pixstride]
    punpcklbw m5, m6
    pmaddubsw m5, m3
    paddsw    m4, m5

    STORE [dstq + i * 2 * blocksize], m4

%assign i (i + 1)
%endrep

    add       dstq,   dststrideq
    add       srcq,   srcstrideq
%if %2
    add       src_m3, srcstrideq
%endif

    dec heightd
    jg .loop
    RET
%endmacro

INIT_XMM ssse3
QPEL_8 4,  0
QPEL_8 8,  0
QPEL_8 12, 0
QPEL_8 16, 0
QPEL_8 24, 0
QPEL_8 32, 0
QPEL_8 48, 0
QPEL_8 64, 0

QPEL_8 4,  1
QPEL_8 8,  1
QPEL_8 12, 1
QPEL_8 16, 1
QPEL_8 24, 1
QPEL_8 32, 1
QPEL_8 48, 1
QPEL_8 64, 1

; 16-bit qpel interpolation
; %1: block width
; %2: shift applied to the result
; %3: 0 - horizontal; 1 - vertical
%macro QPEL_16 3
%if %3
    %define mvfrac     myq
    %define pixstride  srcstrideq
    %define pixstride3 sstride3q
    %define src_m3     srcm3q
%else
    %define mvfrac     mxq
    %define pixstride  2
    %define pixstride3 6
    %define src_m3     (srcq - 6)
%endif

    COMMON_DEFS %1, 16

    and       mvfrac, 0x3
    dec       mvfrac
    shl       mvfrac, 4
    lea       coeffsregq, [hevc_qpel_coeffs]
    mova      m0,         [coeffsregq + mvfrac]

    pshufd    m1, m0, 0x55
    pshufd    m2, m0, 0xaa
    pshufd    m3, m0, 0xff
    pshufd    m0, m0, 0x00

%if %3
    lea       sstride3q, [srcstrideq + 2 * srcstrideq]
    mov       srcm3q, srcq
    sub       srcm3q, sstride3q
%endif

.loop

%assign i 0
%rep nb_blocks

    BLOCK_DEFS i

    LOAD m4,  [src_m3 + i * 2 * blocksize]
    LOAD m5,  [src_m3 + i * 2 * blocksize + 1 * pixstride]
    LOAD m6,  [src_m3 + i * 2 * blocksize + 2 * pixstride]
    LOAD m7,  [srcq   + i * 2 * blocksize + 0 * pixstride]
    LOAD m8,  [srcq   + i * 2 * blocksize + 1 * pixstride]
    LOAD m9,  [srcq   + i * 2 * blocksize + 2 * pixstride]
    LOAD m10, [srcq   + i * 2 * blocksize +     pixstride3]
    LOAD m11, [srcq   + i * 2 * blocksize + 4 * pixstride]

    punpcklwd m12, m4, m5
    pmaddwd   m12, m0

    punpcklwd m13, m6, m7
    pmaddwd   m13, m1
    paddd     m12, m13

    punpcklwd m13, m8, m9
    pmaddwd   m13, m2
    paddd     m12, m13

    punpcklwd m13, m10, m11
    pmaddwd   m13, m3
    paddd     m12, m13
    psrad     m12, %2

    %if block_truncated == 0
        punpckhwd m4, m5
        pmaddwd   m4, m0

        punpckhwd m6, m7
        pmaddwd   m6, m1
        paddd     m4, m6

        punpckhwd m8, m9
        pmaddwd   m8, m2
        paddd     m4, m8

        punpckhwd m10, m11
        pmaddwd   m10, m3
        paddd     m4, m10

        psrad     m4, %2
    %endif
    packssdw  m12, m4
    STORE [dstq + i * 2 * blocksize], m12

%assign i (i + 1)
%endrep

    add       dstq,   dststrideq
    add       srcq,   srcstrideq
%if %3
    add       srcm3q, srcstrideq
%endif

    dec heightd
    jg .loop
    RET
%endmacro

%if ARCH_X86_64

%macro QPEL_H_10 1
cglobal hevc_qpel_h_ %+ %1 %+ _10, 7, 9, 14, dst, dststride, src, srcstride, height, mx, my, mcbuffer, coeffsreg
QPEL_16 %1, 2, 0
%endmacro

INIT_XMM avx
QPEL_H_10 4
QPEL_H_10 8
QPEL_H_10 12
QPEL_H_10 16
QPEL_H_10 24
QPEL_H_10 32
QPEL_H_10 48
QPEL_H_10 64

%macro QPEL_V_10 1
cglobal hevc_qpel_v_ %+ %1 %+ _10, 7, 10, 14, dst, dststride, src, srcstride, height, mx, my, sstride3, srcm3, coeffsreg
QPEL_16 %1, 2, 1
%endmacro

INIT_XMM avx
QPEL_V_10 4
QPEL_V_10 8
QPEL_V_10 12
QPEL_V_10 16
QPEL_V_10 24
QPEL_V_10 32
QPEL_V_10 48
QPEL_V_10 64

; hevc_qpel_hv_<w>(int16_t *dst, ptrdiff_t dststride,
;                  uint8_t *src, ptrdiff_t srcstride,
;                  int height, int mx, int my, int *mcbuffer)

%macro QPEL_HV 1
cglobal hevc_qpel_hv_ %+ %1, 7, 10, 14, dst, dststride, src, srcstride, height, mx, my, sstride3, srcm3, coeffsreg
QPEL_16 %1, 6, 1
%endmacro

INIT_XMM avx
QPEL_HV 4
QPEL_HV 8
QPEL_HV 12
QPEL_HV 16
QPEL_HV 24
QPEL_HV 32
QPEL_HV 48
QPEL_HV 64

%endif ; ARCH_X86_64

; hevc_epel_h/v_<w>_8(int16_t *dst, ptrdiff_t dststride,
;                     uint8_t *src, ptrdiff_t srcstride,
;                     int height, int mx, int my, int *mcbuffer)

; 8-bit epel interpolation
; %1: block width
; %2: 0 - horizontal; 1 - vertical
%macro EPEL_8 2
%if %2
    %define postfix    v
    %define mvfrac     myq
    %define coeffsaddr r5q
    %define pixstride  srcstrideq
    %define pixstride3 r5q
%else
    %define postfix    h
    %define mvfrac     mxq
    %define coeffsaddr r6q
    %define pixstride  1
    %define pixstride3 3
%endif

    COMMON_DEFS %1, 8

cglobal hevc_epel_ %+ postfix %+ _ %+ %1 %+ _8, 7, 7, 6, dst, dststride, src, srcstride, height, mx, my
    and       mvfrac, 0x7
    dec       mvfrac
    shl       mvfrac, 4
    lea       coeffsaddr, [hevc_epel_coeffs8]
    movq      m0,         [coeffsaddr + mvfrac]

    SPLATW    m1, m0, 1
    SPLATW    m0, m0, 0

%if %2
    lea       pixstride3, [srcstrideq + 2 * srcstrideq]
%endif
    sub       srcq, pixstride

.loop

%assign i 0
%rep nb_blocks

    BLOCK_DEFS i

    LOAD m2, [srcq + i * blocksize + 0 * pixstride]
    LOAD m3, [srcq + i * blocksize + 1 * pixstride]
    LOAD m4, [srcq + i * blocksize + 2 * pixstride]
    LOAD m5, [srcq + i * blocksize +     pixstride3]

    punpcklbw m2, m3
    punpcklbw m4, m5

    pmaddubsw m2, m0
    pmaddubsw m4, m1

    paddsw    m2, m4

    STORE [dstq + i * 2 * blocksize], m2

%assign i (i + 1)
%endrep

    add       dstq, dststrideq
    add       srcq, srcstrideq

    dec heightd
    jg .loop
    RET
%endmacro

INIT_XMM ssse3
EPEL_8 4,  0
EPEL_8 8,  0
EPEL_8 12, 0
EPEL_8 16, 0
EPEL_8 24, 0
EPEL_8 32, 0

EPEL_8 4,  1
EPEL_8 8,  1
EPEL_8 12, 1
EPEL_8 16, 1
EPEL_8 24, 1
EPEL_8 32, 1

%macro EPEL_16 3
%if %3
    %define mvfrac     myq
    %define pixstride  srcstrideq
    %define pixstride3 sstride3q
%else
    %define mvfrac     mxq
    %define pixstride  2
    %define pixstride3 6
%endif

    COMMON_DEFS %1, 16

    and       mvfrac, 0x7
    dec       mvfrac
    shl       mvfrac, 5
    lea       coeffsregq, [hevc_epel_coeffs]
    mova      m0, [coeffsregq + mvfrac]

    pshufd    m1, m0, 0x55
    pshufd    m0, m0, 0x00

%if %3
    lea       sstride3q, [srcstrideq + 2 * srcstrideq]
%endif
    sub       srcq, pixstride

.loop

%assign i 0
%rep nb_blocks

    BLOCK_DEFS i

    LOAD m2, [srcq + i * 2 * blocksize + 0 * pixstride]
    LOAD m3, [srcq + i * 2 * blocksize + 1 * pixstride]
    LOAD m4, [srcq + i * 2 * blocksize + 2 * pixstride]
    LOAD m5, [srcq + i * 2 * blocksize +     pixstride3]

    punpcklwd m6, m2, m3
    punpcklwd m7, m4, m5
    pmaddwd   m6, m0
    pmaddwd   m7, m1
    paddd     m6, m7
    psrad     m6, %2

    %if block_truncated == 0
        punpckhwd m2, m3
        punpckhwd m4, m5
        pmaddwd   m2, m0
        pmaddwd   m4, m1
        paddd     m2, m4
        psrad     m2, %2
    %endif
    packssdw  m6, m2
    STORE [dstq + i * 2 * blocksize], m6

%assign i (i + 1)
%endrep

    add       dstq,   dststrideq
    add       srcq,   srcstrideq

    dec heightd
    jg .loop
    RET
%endmacro

%if ARCH_X86_64

%macro EPEL_H_10 1
cglobal hevc_epel_h_ %+ %1 %+ _10, 8, 9, 8, dst, dststride, src, srcstride, height, mx, my, sstride3, coeffsreg
EPEL_16 %1, 2, 0
%endmacro

INIT_XMM avx
EPEL_H_10 4
EPEL_H_10 8
EPEL_H_10 12
EPEL_H_10 16
EPEL_H_10 24
EPEL_H_10 32

%macro EPEL_V_10 1
cglobal hevc_epel_v_ %+ %1 %+ _10, 8, 9, 8, dst, dststride, src, srcstride, height, mx, my, sstride3, coeffsreg
EPEL_16 %1, 2, 1
%endmacro

INIT_XMM avx
EPEL_V_10 4
EPEL_V_10 8
EPEL_V_10 12
EPEL_V_10 16
EPEL_V_10 24
EPEL_V_10 32

; hevc_epel_hv_<w>_8(int16_t *dst, ptrdiff_t dststride,
;                    int16_t *src, ptrdiff_t srcstride,
;                    int height, int mx, int my, int *mcbuffer)

%macro EPEL_HV 1
cglobal hevc_epel_hv_ %+ %1, 8, 9, 8, dst, dststride, src, srcstride, height, mx, my, sstride3, coeffsreg
EPEL_16 %1, 6, 1
%endmacro

INIT_XMM avx
EPEL_HV 4
EPEL_HV 8
EPEL_HV 12
EPEL_HV 16
EPEL_HV 24
EPEL_HV 32

%endif ; ARCH_X86_64

; hevc_put_unweighted_pred_<w>_<d>(pixel   *dst, ptrdiff_t dststride,
;                                  int16_t *src, ptrdiff_t srcstride,
;                                  int height)

%macro AVG 5
    %if %3
        %if %4 == 4
            movq %5, %2
            paddsw %1, %5
        %else
            paddsw %1, %2
        %endif
    %endif
%endmacro

; %1: 0 - one source; 1 - two sources
; %2: width
; %3: bit depth
%macro PUT_PRED 3
%if %1
cglobal hevc_put_unweighted_pred_avg_ %+ %2 %+ _ %+ %3, 6, 6, 4, dst, dststride, src, src2, srcstride, height
%else
cglobal hevc_put_unweighted_pred_ %+ %2 %+ _ %+ %3, 5, 5, 4, dst, dststride, src, srcstride, height
%endif

%assign shift       14 + %1 - %3
%assign offset      (1 << (shift - 1))
%define offset_data pw_ %+ offset

    mova        m0, [offset_data]

%if %3 > 8
    %define STORE_BLOCK movu
    %define STORE_HALF  movq

    %assign pixel_max ((1 << %3) - 1)
    %define pw_pixel_max pw_ %+ pixel_max
    pxor    m1, m1
    mova    m2, [pw_pixel_max]
%else
    %define STORE_BLOCK movq
    %define STORE_HALF  movd
%endif

.loop
%assign i 0
%rep (%2 + 7) / 8

    %if (i + 1) * 8 > %2
        %define LOAD movq
        %define STORE STORE_HALF
    %else
        %define LOAD mova
        %define STORE STORE_BLOCK
    %endif

    LOAD m3, [srcq  + 16 * i]
    AVG  m3, [src2q + 16 * i], %1, %3 - i * 8, m4

    paddsw m3, m0
    psraw  m3, shift

    %if %3 == 8
        packuswb m3, m3
        STORE [dstq + 8 * i], m3
    %else
        CLIPW m3, m1, m2
        STORE [dstq + 16 * i], m3
    %endif
%assign i (i + 1)
%endrep

    add dstq,  dststrideq
    add srcq,  srcstrideq
%if %1
    add src2q, srcstrideq
%endif

    dec         heightd
    jg          .loop
    RET
%endmacro

INIT_XMM sse2
PUT_PRED 0, 4,  8
PUT_PRED 1, 4,  8
PUT_PRED 0, 8,  8
PUT_PRED 1, 8,  8
PUT_PRED 0, 12, 8
PUT_PRED 1, 12, 8
PUT_PRED 0, 16, 8
PUT_PRED 1, 16, 8
PUT_PRED 0, 24, 8
PUT_PRED 1, 24, 8
PUT_PRED 0, 32, 8
PUT_PRED 1, 32, 8
PUT_PRED 0, 48, 8
PUT_PRED 1, 48, 8
PUT_PRED 0, 64, 8
PUT_PRED 1, 64, 8

PUT_PRED 0, 4,  10
PUT_PRED 1, 4,  10
PUT_PRED 0, 8,  10
PUT_PRED 1, 8,  10
PUT_PRED 0, 12, 10
PUT_PRED 1, 12, 10
PUT_PRED 0, 16, 10
PUT_PRED 1, 16, 10
PUT_PRED 0, 24, 10
PUT_PRED 1, 24, 10
PUT_PRED 0, 32, 10
PUT_PRED 1, 32, 10
PUT_PRED 0, 48, 10
PUT_PRED 1, 48, 10
PUT_PRED 0, 64, 10
PUT_PRED 1, 64, 10

%macro PUT_WEIGHTED_PRED 3
%if %1
cglobal hevc_put_weighted_pred_avg_ %+ %2 %+ _ %+ %3, 11, 11, 8, denom, weight0, weight1, offset0, offset1, dst, dststride, src0, src1, srcstride, height
%else
cglobal hevc_put_weighted_pred_ %+ %2 %+ _ %+ %3, 8, 8, 8, denom, weight0, offset0, dst, dststride, src0, srcstride, height
%endif

    and         denomd, 0xff
    movsx       weight0d, weight0w
    movsx       offset0d, offset0w
%if %1
    movsx       weight1d, weight1w
    movsx       offset1d, offset1w
%endif

    add         denomd, 14 + %1 - %3
    movd        m0, denomd

%if %3 > 8
    %assign     pixel_max ((1 << %3) - 1)
    %define     pw_pixel_max pw_ %+ pixel_max
    pxor        m4, m4
    mova        m5, [pw_pixel_max]

    shl         offset0d, %3 - 8
%if %1
    shl         offset1d, %3 - 8
%endif
%endif

%if %1
    lea         offset0d, [offset0d + offset1d + 1]
%else
    lea         offset0d, [2 * offset0d + 1]
%endif
    movd        m1, offset0d
    SPLATD      m1
    pslld       m1, m0
    psrad       m1, 1

    movd        m2, weight0d
    SPLATD      m2
%if %1
    movd        m3, weight1d
    SPLATD      m3
%endif

.loop
%assign i 0
%rep (%2 + 3) / 4

    pmovsxwd   m6, [src0q + 8 * i]
    pmulld     m6, m2

%if %1
    pmovsxwd   m7, [src1q + 8 * i]
    pmulld     m7, m3
    paddd      m6, m7
%endif

    paddd      m6, m1
    psrad      m6, m0

    packssdw   m6, m6

%if %3 > 8
    CLIPW      m6, m4, m5
    movq       [dstq + 8 * i], m6
%else
    packuswb   m6, m6
    movd [dstq + 4 * i], m6
%endif

%assign i (i + 1)
%endrep

    add dstq,  dststrideq
    add src0q, srcstrideq
%if %1
    add src1q, srcstrideq
%endif

    dec         heightd
    jg          .loop
    RET
%endmacro

%if ARCH_X86_64
INIT_XMM sse4
PUT_WEIGHTED_PRED 0, 4,  8
PUT_WEIGHTED_PRED 1, 4,  8
PUT_WEIGHTED_PRED 0, 8,  8
PUT_WEIGHTED_PRED 1, 8,  8
PUT_WEIGHTED_PRED 0, 12, 8
PUT_WEIGHTED_PRED 1, 12, 8
PUT_WEIGHTED_PRED 0, 16, 8
PUT_WEIGHTED_PRED 1, 16, 8
PUT_WEIGHTED_PRED 0, 24, 8
PUT_WEIGHTED_PRED 1, 24, 8
PUT_WEIGHTED_PRED 0, 32, 8
PUT_WEIGHTED_PRED 1, 32, 8
PUT_WEIGHTED_PRED 0, 48, 8
PUT_WEIGHTED_PRED 1, 48, 8
PUT_WEIGHTED_PRED 0, 64, 8
PUT_WEIGHTED_PRED 1, 64, 8

PUT_WEIGHTED_PRED 0, 4,  10
PUT_WEIGHTED_PRED 1, 4,  10
PUT_WEIGHTED_PRED 0, 8,  10
PUT_WEIGHTED_PRED 1, 8,  10
PUT_WEIGHTED_PRED 0, 12, 10
PUT_WEIGHTED_PRED 1, 12, 10
PUT_WEIGHTED_PRED 0, 16, 10
PUT_WEIGHTED_PRED 1, 16, 10
PUT_WEIGHTED_PRED 0, 24, 10
PUT_WEIGHTED_PRED 1, 24, 10
PUT_WEIGHTED_PRED 0, 32, 10
PUT_WEIGHTED_PRED 1, 32, 10
PUT_WEIGHTED_PRED 0, 48, 10
PUT_WEIGHTED_PRED 1, 48, 10
PUT_WEIGHTED_PRED 0, 64, 10
PUT_WEIGHTED_PRED 1, 64, 10

%endif ; ARCH_X86_64
