;******************************************************************************
;* Copyright Nick Kurshev
;* Copyright Michael (michaelni@gmx.at)
;* Copyright 2018 Jokyo Images
;* Copyright Ivo van Poorten
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
pb_shuffle3102: db 3, 1, 0, 2, 7, 5, 4, 6, 11, 9, 8, 10, 15, 13, 12, 14
pb_shuffle2013: db 2, 0, 1, 3, 6, 4, 5, 7, 10, 8, 9, 11, 14, 12, 13, 15
pb_shuffle2130: db 2, 1, 3, 0, 6, 5, 7, 4, 10, 9, 11, 8, 14, 13, 15, 12
pb_shuffle1203: db 1, 2, 0, 3, 5, 6, 4, 7, 9, 10, 8, 11, 13, 14, 12, 15

%if HAVE_AVX512ICL_EXTERNAL
; shuffle vector to rearrange packuswb result to be linear
shuf_packus: db  0,  1,  2,  3, 16, 17, 18, 19, 32, 33, 34, 35, 48, 49, 50, 51,\
                 4,  5,  6,  7, 20, 21, 22, 23, 36, 37, 38, 39, 52, 53, 54, 55,\
                 8,  9, 10, 11, 24, 25, 26, 27, 40, 41, 42, 43, 56, 57, 58, 59,\
                12, 13, 14, 15, 28, 29, 30, 31, 44, 45, 46, 47, 60, 61, 62, 63

; shuffle vector to combine odd elements from two vectors to extract Y
shuf_perm2b: db  1,  3,   5,   7,   9,  11,  13,  15,  17,  19,  21,  23,  25,  27,  29,  31,\
                33, 35,  37,  39,  41,  43,  45,  47,  49,  51,  53,  55,  57,  59,  61,  63,\
                65, 67,  69,  71,  73,  75,  77,  79,  81,  83,  85,  87,  89,  91,  93,  95,\
                97, 99, 101, 103, 105, 107, 109, 111, 113, 115, 117, 119, 121, 123, 125, 127
%endif

%if HAVE_AVX2_EXTERNAL
; shuffle vector to rearrange packuswb result to be linear
shuf_packus_avx2: db  0, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0,\
                      2, 0, 0, 0, 6, 0, 0, 0, 3, 0, 0, 0, 7, 0, 0, 0,
%endif

SECTION .text

%macro RSHIFT_COPY 3
; %1 dst ; %2 src ; %3 shift
%if cpuflag(avx) || cpuflag(avx2) || cpuflag(avx512icl)
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
    movsxdifnidn      wq, wd
    mov               xq, wq

    add             srcq, wq
    add             dstq, wq
    neg               wq

%if mmsize == 64
    and                    xq, mmsize - 4
    shr                    xq, 2
    mov                  tmpd, -1
    shlx                 tmpd, tmpd, xd
    not                  tmpd
    kmovw                  k7, tmpd
    vmovdqu32       m1{k7}{z}, [srcq + wq]
    pshufb                 m1, m0
    vmovdqu32 [dstq + wq]{k7}, m1
    lea                    wq, [wq + 4 * xq]
%else
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
%endif

    ;check if src_size < mmsize
    cmp wq, 0
    jge .end

    .loop_simd:
        movu            m1, [srcq + wq]
        pshufb          m1, m0
        movu   [dstq + wq], m1
        add             wq, mmsize
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
SHUFFLE_BYTES 3, 1, 0, 2
SHUFFLE_BYTES 2, 0, 1, 3
SHUFFLE_BYTES 2, 1, 3, 0
SHUFFLE_BYTES 1, 2, 0, 3

%if ARCH_X86_64
%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
SHUFFLE_BYTES 2, 1, 0, 3
SHUFFLE_BYTES 0, 3, 2, 1
SHUFFLE_BYTES 1, 2, 3, 0
SHUFFLE_BYTES 3, 0, 1, 2
SHUFFLE_BYTES 3, 2, 1, 0
SHUFFLE_BYTES 3, 1, 0, 2
SHUFFLE_BYTES 2, 0, 1, 3
SHUFFLE_BYTES 2, 1, 3, 0
SHUFFLE_BYTES 1, 2, 0, 3
%endif
%endif

%if ARCH_X86_64
%if HAVE_AVX512ICL_EXTERNAL
INIT_ZMM avx512icl
SHUFFLE_BYTES 2, 1, 0, 3
SHUFFLE_BYTES 0, 3, 2, 1
SHUFFLE_BYTES 1, 2, 3, 0
SHUFFLE_BYTES 3, 0, 1, 2
SHUFFLE_BYTES 3, 2, 1, 0
SHUFFLE_BYTES 3, 1, 0, 2
SHUFFLE_BYTES 2, 0, 1, 3
SHUFFLE_BYTES 2, 1, 3, 0
SHUFFLE_BYTES 1, 2, 0, 3
%endif
%endif

;-----------------------------------------------------------------------------------------------
; uyvytoyuv422(uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
;              const uint8_t *src, int width, int height,
;              int lumStride, int chromStride, int srcStride)
;-----------------------------------------------------------------------------------------------
%macro UYVY_TO_YUV422 0
cglobal uyvytoyuv422, 9, 14, 8 + cpuflag(avx2) + cpuflag(avx512icl), ydst, udst, vdst, src, w, h, lum_stride, chrom_stride, src_stride, wtwo, whalf, tmp, x, back_w
    pxor         m0, m0
%if mmsize == 64
    vpternlogd   m1, m1, m1, 0xff  ; m1 = _mm512_set1_epi8(0xff)
    movu         m8, [shuf_packus]
    movu         m9, [shuf_perm2b]
%else
    %if cpuflag(avx2)
        movu     m8, [shuf_packus_avx2]
    %endif
    pcmpeqw      m1, m1
%endif
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

%if mmsize == 64
    shr     xq, 1
    mov   tmpq, -1
    shlx  tmpq, tmpq, xq
    not   tmpq
    kmovq   k7, tmpq ; write mask for U/V
    kmovd   k1, tmpd ; write mask for 1st half of Y
    kmovw   k3, tmpd ; read mask for 1st vector
    shr   tmpq, 16
    kmovw   k4, tmpd ; read mask for 2nd vector
    shr   tmpq, 16
    kmovd   k2, tmpd ; write mask for 2nd half of Y
    kmovw   k5, tmpd ; read mask for 3rd vector
    shr   tmpd, 16
    kmovw   k6, tmpd ; read mask for 4th vector

    vmovdqu32  m2{k3}{z}, [srcq + wtwoq             ]
    vmovdqu32  m3{k4}{z}, [srcq + wtwoq + mmsize    ]
    vmovdqu32  m4{k5}{z}, [srcq + wtwoq + mmsize * 2]
    vmovdqu32  m5{k6}{z}, [srcq + wtwoq + mmsize * 3]

    ; extract y part 1
    mova                             m6, m9
    vpermi2b                         m6, m2, m3 ; UYVY UYVY -> YYYY using permute
    vmovdqu16          [ydstq + wq]{k1}, m6

    ; extract y part 2
    mova                             m7, m9
    vpermi2b                         m7, m4, m5 ; UYVY UYVY -> YYYY using permute
    vmovdqu16 [ydstq + wq + mmsize]{k2}, m7

    ; extract uv
    pand                         m2, m1     ; UxVx...
    pand                         m3, m1     ; UxVx...
    pand                         m4, m1     ; UxVx...
    pand                         m5, m1     ; UxVx...
    packuswb                     m2, m3     ; UVUV...
    packuswb                     m4, m5     ; UVUV...

    ; U
    pand                         m6, m2, m1 ; UxUx...
    pand                         m7, m4, m1 ; UxUx...
    packuswb                     m6, m7     ; UUUU
    vpermb                       m6, m8, m6
    vmovdqu8   [udstq + whalfq]{k7}, m6

    ; V
    psrlw                        m2, 8      ; VxVx...
    psrlw                        m4, 8      ; VxVx...
    packuswb                     m2, m4     ; VVVV
    vpermb                       m2, m8, m2
    vmovdqu8   [vdstq + whalfq]{k7}, m2

    lea      wq, [   wq + 2 * xq]
    lea   wtwoq, [wtwoq + 4 * xq]
    add  whalfq, xq
%else
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
%endif

    ; check if simd loop is need
    cmp      wq, 0
    jge .end_line

    .loop_simd:
        movu    m2, [srcq + wtwoq             ]
        movu    m3, [srcq + wtwoq + mmsize    ]
        movu    m4, [srcq + wtwoq + mmsize * 2]
        movu    m5, [srcq + wtwoq + mmsize * 3]

%if mmsize == 64
        ; extract y part 1
        mova                    m6, m9
        vpermi2b                m6, m2, m3 ; UYVY UYVY -> YYYY using permute
        movu          [ydstq + wq], m6

        ; extract y part 2
        mova                    m7, m9
        vpermi2b                m7, m4, m5 ; UYVY UYVY -> YYYY using permute
        movu [ydstq + wq + mmsize], m7
%else
        ; extract y part 1
        RSHIFT_COPY    m6, m2, 1 ; UYVY UYVY -> YVYU YVY...
        pand           m6, m1    ; YxYx YxYx...

        RSHIFT_COPY    m7, m3, 1 ; UYVY UYVY -> YVYU YVY...
        pand           m7, m1    ; YxYx YxYx...

        packuswb       m6, m7    ; YYYY YYYY...
%if mmsize == 32
        vpermq         m6, m6, 0xd8
%endif
        movu [ydstq + wq], m6

        ; extract y part 2
        RSHIFT_COPY    m6, m4, 1 ; UYVY UYVY -> YVYU YVY...
        pand           m6, m1    ; YxYx YxYx...

        RSHIFT_COPY    m7, m5, 1 ; UYVY UYVY -> YVYU YVY...
        pand           m7, m1    ; YxYx YxYx...

        packuswb       m6, m7    ; YYYY YYYY...
%if mmsize == 32
        vpermq         m6, m6, 0xd8
%endif
        movu [ydstq + wq + mmsize], m6
%endif

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
%if mmsize == 64
        vpermb   m6, m8, m6
%elif mmsize == 32
        vpermd   m6, m8, m6
%endif
        movu   [udstq + whalfq], m6


        ; V
        psrlw      m2, 8  ; VxVx...
        psrlw      m4, 8  ; VxVx...
        packuswb   m2, m4 ; VVVV
%if mmsize == 64
        vpermb     m2, m8, m2
%elif mmsize == 32
        vpermd     m2, m8, m2
%endif
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
%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
UYVY_TO_YUV422
%endif
%if HAVE_AVX512ICL_EXTERNAL
INIT_ZMM avx512icl
UYVY_TO_YUV422
%endif
%endif
