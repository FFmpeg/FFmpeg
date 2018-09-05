;******************************************************************************
;* Copyright Nick Kurshev
;* Copyright Michael (michaelni@gmx.at)
;* Copyright 2018 Jokyo Images
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

pb_shuffle2103: db 2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15
pb_shuffle0321: db 0, 3, 2, 1, 4, 7, 6, 5, 8, 11, 10, 9, 12, 15, 14, 13
pb_shuffle1230: db 1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12
pb_shuffle3012: db 3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14
pb_shuffle3210: db 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12

SECTION .text

%macro RSHIFT_COPY 3
; %1 dst ; %2 src ; %3 shift
%if cpuflag(avx)
    psrldq  %1, %2, %3
%else
    mova           %1, %2
    RSHIFT         %1, %3
%endif
%endmacro

;------------------------------------------------------------------------------
; shuffle_bytes_## (const uint8_t *src, uint8_t *dst, int src_size)
;------------------------------------------------------------------------------
; %1-4 index shuffle
%macro SHUFFLE_BYTES 4
cglobal shuffle_bytes_%1%2%3%4, 3, 5, 2, src, dst, w, tmp, x
    VBROADCASTI128    m0, [pb_shuffle%1%2%3%4]
    movsxdifnidn wq, wd
    mov xq, wq

    add        srcq, wq
    add        dstq, wq
    neg          wq

;calc scalar loop
    and xq, mmsize-4
    je .loop_simd

.loop_scalar:
   mov          tmpb, [srcq + wq + %1]
   mov [dstq+wq + 0], tmpb
   mov          tmpb, [srcq + wq + %2]
   mov [dstq+wq + 1], tmpb
   mov          tmpb, [srcq + wq + %3]
   mov [dstq+wq + 2], tmpb
   mov          tmpb, [srcq + wq + %4]
   mov [dstq+wq + 3], tmpb
   add            wq, 4
   sub            xq, 4
   jg .loop_scalar

;check if src_size < mmsize
cmp wq, 0
jge .end

.loop_simd:
    movu           m1, [srcq+wq]
    pshufb         m1, m0
    movu    [dstq+wq], m1
    add            wq, mmsize
    jl .loop_simd

.end:
    RET
%endmacro

INIT_XMM ssse3
SHUFFLE_BYTES 2, 1, 0, 3
SHUFFLE_BYTES 0, 3, 2, 1
SHUFFLE_BYTES 1, 2, 3, 0
SHUFFLE_BYTES 3, 0, 1, 2
SHUFFLE_BYTES 3, 2, 1, 0

;-----------------------------------------------------------------------------------------------
; uyvytoyuv422(uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
;              const uint8_t *src, int width, int height,
;              int lumStride, int chromStride, int srcStride)
;-----------------------------------------------------------------------------------------------
%macro UYVY_TO_YUV422 0
cglobal uyvytoyuv422, 9, 14, 8, ydst, udst, vdst, src, w, h, lum_stride, chrom_stride, src_stride, wtwo, whalf, tmp, x, back_w
    pxor         m0, m0
    pcmpeqw      m1, m1
    psrlw        m1, 8

    movsxdifnidn            wq, wd
    movsxdifnidn   lum_strideq, lum_strided
    movsxdifnidn chrom_strideq, chrom_strided
    movsxdifnidn   src_strideq, src_strided

    mov     back_wq, wq
    mov      whalfq, wq
    shr      whalfq, 1     ; whalf = width / 2

    lea srcq, [srcq + wq * 2]
    add    ydstq, wq
    add    udstq, whalfq
    add    vdstq, whalfq

.loop_line:
    mov          xq, wq
    mov       wtwoq, wq
    add       wtwoq, wtwoq ; wtwo = width * 2

    neg       wq
    neg    wtwoq
    neg   whalfq

    ;calc scalar loop count
    and       xq, mmsize * 2 - 1
    je .loop_simd

    .loop_scalar:
        mov             tmpb, [srcq + wtwoq + 0]
        mov [udstq + whalfq], tmpb

        mov             tmpb, [srcq + wtwoq + 1]
        mov     [ydstq + wq], tmpb

        mov             tmpb, [srcq + wtwoq + 2]
        mov [vdstq + whalfq], tmpb

        mov             tmpb, [srcq + wtwoq + 3]
        mov [ydstq + wq + 1], tmpb

        add      wq, 2
        add   wtwoq, 4
        add  whalfq, 1
        sub      xq, 2
        jg .loop_scalar

    ; check if simd loop is need
    cmp      wq, 0
    jge .end_line

    .loop_simd:
        movu    m2, [srcq + wtwoq             ]
        movu    m3, [srcq + wtwoq + mmsize    ]
        movu    m4, [srcq + wtwoq + mmsize * 2]
        movu    m5, [srcq + wtwoq + mmsize * 3]

        ; extract y part 1
        RSHIFT_COPY    m6, m2, 1 ; UYVY UYVY -> YVYU YVY...
        pand           m6, m1; YxYx YxYx...

        RSHIFT_COPY    m7, m3, 1 ; UYVY UYVY -> YVYU YVY...
        pand           m7, m1 ; YxYx YxYx...

        packuswb       m6, m7 ; YYYY YYYY...
        movu [ydstq + wq], m6

        ; extract y part 2
        RSHIFT_COPY    m6, m4, 1 ; UYVY UYVY -> YVYU YVY...
        pand           m6, m1; YxYx YxYx...

        RSHIFT_COPY    m7, m5, 1 ; UYVY UYVY -> YVYU YVY...
        pand           m7, m1 ; YxYx YxYx...

        packuswb                m6, m7 ; YYYY YYYY...
        movu [ydstq + wq + mmsize], m6

        ; extract uv
        pand       m2, m1   ; UxVx...
        pand       m3, m1   ; UxVx...
        pand       m4, m1   ; UxVx...
        pand       m5, m1   ; UxVx...

        packuswb   m2, m3   ; UVUV...
        packuswb   m4, m5   ; UVUV...

        ; U
        pand       m6, m2, m1 ; UxUx...
        pand       m7, m4, m1 ; UxUx...

        packuswb m6, m7 ; UUUU
        movu   [udstq + whalfq], m6


        ; V
        psrlw      m2, 8  ; VxVx...
        psrlw      m4, 8  ; VxVx...
        packuswb   m2, m4 ; VVVV
        movu   [vdstq + whalfq], m2

        add   whalfq, mmsize
        add    wtwoq, mmsize * 4
        add       wq, mmsize * 2
        jl .loop_simd

    .end_line:
        add        srcq, src_strideq
        add        ydstq, lum_strideq
        add        udstq, chrom_strideq
        add        vdstq, chrom_strideq

        ;restore initial state of line variable
        mov           wq, back_wq
        mov          xq, wq
        mov      whalfq, wq
        shr      whalfq, 1     ; whalf = width / 2
        sub          hd, 1
        jg .loop_line

    RET
%endmacro

%if ARCH_X86_64
INIT_XMM sse2
UYVY_TO_YUV422

INIT_XMM avx
UYVY_TO_YUV422
%endif
