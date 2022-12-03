;******************************************************************************
;* V210 SIMD pack
;* Copyright (c) 2014 Kieran Kunhya <kierank@obe.tv>
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

SECTION_RODATA 64

cextern pw_4
%define v210_enc_min_10 pw_4
v210_enc_max_10: times 16 dw 0x3fb

v210_enc_luma_mult_10: times 2 dw 4,1,16,4,1,16,0,0
v210_enc_luma_shuf_10: times 2 db -1,0,1,-1,2,3,4,5,-1,6,7,-1,8,9,10,11

v210_enc_chroma_mult_10: times 2 dw 1,4,16,0,16,1,4,0
v210_enc_chroma_shuf_10: times 2 db 0,1,8,9,-1,2,3,-1,10,11,4,5,-1,12,13,-1

cextern pb_1
%define v210_enc_min_8 pb_1
cextern pb_FE
%define v210_enc_max_8 pb_FE

v210_enc_luma_shuf_8: times 2 db 6,-1,7,-1,8,-1,9,-1,10,-1,11,-1,-1,-1,-1,-1
v210_enc_luma_mult_8: times 2 dw 16,4,64,16,4,64,0,0

v210_enc_chroma_shuf1_8: times 2 db 0,-1,1,-1,2,-1,3,-1,8,-1,9,-1,10,-1,11,-1
v210_enc_chroma_shuf2_8: times 2 db 3,-1,4,-1,5,-1,7,-1,11,-1,12,-1,13,-1,15,-1

v210_enc_chroma_mult_8: times 2 dw 4,16,64,0,64,4,16,0

v210enc_8_permb: db 32, 0,48,-1 ,  1,33, 2,-1 , 49, 3,34,-1 ,  4,50, 5,-1
                 db 35, 6,51,-1 ,  7,36, 8,-1 , 52, 9,37,-1 , 10,53,11,-1
                 db 38,12,54,-1 , 13,39,14,-1 , 55,15,40,-1 , 16,56,17,-1
                 db 41,18,57,-1 , 19,42,20,-1 , 58,21,43,-1 , 22,59,23,-1
v210enc_8_shufb: db  0, 8, 1,-1 ,  9, 2,10,-1 ,  3,11, 4,-1 , 12, 5,13,-1
                 db  2,10, 3,-1 , 11, 4,12,-1 ,  5,13, 6,-1 , 14, 7,15,-1
v210enc_8_permd: dd 0,1,4,5, 1,2,5,6
v210enc_8_mult: db 4, 0, 64, 0
v210enc_8_mask: dd 255<<12

icl_perm_y: ; vpermb does not set bytes to zero when the high bit is set unlike pshufb
%assign i 0
%rep 8
    db -1,i+0,i+1,-1 , i+2,i+3,i+4,i+5
    %assign i i+6
%endrep

icl_perm_uv: ; vpermb does not set bytes to zero when the high bit is set unlike pshufb
%assign i 0
%rep 4
    db i+0,i+1,i+32,i+33 , -1,i+2,i+3,-1 , i+34,i+35,i+4,i+5 , -1,i+36,i+37,-1
    %assign i i+6
%endrep

icl_perm_y_kmask:  times 8 db 1111_0110b
icl_perm_uv_kmask: times 8 db 0110_1111b

icl_shift_y:  times 10 dw 2,0,4
              times 4 db 0 ; padding to 64 bytes
icl_shift_uv: times 5 dw 0,2,4
              times 2 db 0 ; padding to 32 bytes
              times 5 dw 4,0,2
              times 2 db 0 ; padding to 32 bytes

v210enc_10_permd_y:  dd 0,1,2,-1 , 3,4,5,-1
v210enc_10_shufb_y:  db -1,0,1,-1 , 2,3,4,5 , -1,6,7,-1 , 8,9,10,11
v210enc_10_permd_uv: dd 0,1,4,5 , 1,2,5,6
v210enc_10_shufb_uv: db 0,1, 8, 9 , -1,2,3,-1 , 10,11,4,5 , -1,12,13,-1
                     db 2,3,10,11 , -1,4,5,-1 , 12,13,6,7 , -1,14,15,-1

SECTION .text

%macro v210_planar_pack_10 0

; v210_planar_pack_10(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst, ptrdiff_t width)
cglobal v210_planar_pack_10, 5, 5, 4+cpuflag(avx2), y, u, v, dst, width
    lea     yq, [yq+2*widthq]
    add     uq, widthq
    add     vq, widthq
    neg     widthq

    mova    m2, [v210_enc_min_10]
    mova    m3, [v210_enc_max_10]

.loop:
    movu        xm0, [yq+2*widthq]
%if cpuflag(avx2)
    vinserti128 m0,   m0, [yq+widthq*2+12], 1
%endif
    CLIPW   m0, m2, m3

    movq         xm1, [uq+widthq]
    movhps       xm1, [vq+widthq]
%if cpuflag(avx2)
    movq         xm4, [uq+widthq+6]
    movhps       xm4, [vq+widthq+6]
    vinserti128  m1,   m1, xm4, 1
%endif
    CLIPW   m1, m2, m3

    pmullw  m0, [v210_enc_luma_mult_10]
    pshufb  m0, [v210_enc_luma_shuf_10]

    pmullw  m1, [v210_enc_chroma_mult_10]
    pshufb  m1, [v210_enc_chroma_shuf_10]

    por     m0, m1

    movu    [dstq], m0

    add     dstq, mmsize
    add     widthq, (mmsize*3)/8
    jl .loop

    RET
%endmacro

%if HAVE_SSSE3_EXTERNAL
INIT_XMM ssse3
v210_planar_pack_10
%endif

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
v210_planar_pack_10
%endif

%macro v210_planar_pack_10_new 0

cglobal v210_planar_pack_10, 5, 5, 8+2*notcpuflag(avx512icl), y, u, v, dst, width
    lea     yq, [yq+2*widthq]
    add     uq, widthq
    add     vq, widthq
    neg     widthq

    %if cpuflag(avx512icl)
        movu  m6, [icl_perm_y]
        movu  m7, [icl_perm_uv]
        kmovq k1, [icl_perm_y_kmask]
        kmovq k2, [icl_perm_uv_kmask]
    %else
        movu           m6, [v210enc_10_permd_y]
        VBROADCASTI128 m7, [v210enc_10_shufb_y]
        movu           m8, [v210enc_10_permd_uv]
        movu           m9, [v210enc_10_shufb_uv]
    %endif
    movu  m2, [icl_shift_y]
    movu  m3, [icl_shift_uv]
    VBROADCASTI128 m4, [v210_enc_min_10] ; only ymm sized
    VBROADCASTI128 m5, [v210_enc_max_10] ; only ymm sized

    .loop:
        movu m0, [yq + widthq*2]
        %if cpuflag(avx512icl)
            movu         ym1, [uq + widthq*1]
            vinserti32x8 zm1, [vq + widthq*1], 1
        %else
            movu         xm1, [uq + widthq*1]
            vinserti128  ym1, [vq + widthq*1], 1
        %endif
        CLIPW m0, m4, m5
        CLIPW m1, m4, m5

        vpsllvw m0, m2
        vpsllvw m1, m3
        %if cpuflag(avx512icl)
            vpermb  m0{k1}{z}, m6, m0 ; make space for uv where the k-mask sets to zero
            vpermb  m1{k2}{z}, m7, m1 ; interleave uv and make space for y where the k-mask sets to zero
        %else
            vpermd m0, m6, m0
            pshufb m0, m7
            vpermd m1, m8, m1
            pshufb m1, m9
        %endif
        por     m0, m1

        movu  [dstq], m0
        add     dstq, mmsize
        add   widthq, (mmsize*3)/8
    jl .loop
RET

%endmacro

%if ARCH_X86_64
%if HAVE_AVX512_EXTERNAL
INIT_YMM avx512
v210_planar_pack_10_new
%endif
%endif

%if HAVE_AVX512ICL_EXTERNAL
INIT_ZMM avx512icl
v210_planar_pack_10_new
%endif

%macro v210_planar_pack_8 0

; v210_planar_pack_8(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dst, ptrdiff_t width)
cglobal v210_planar_pack_8, 5, 5, 7, y, u, v, dst, width
    add     yq, widthq
    shr     widthq, 1
    add     uq, widthq
    add     vq, widthq
    neg     widthq

    mova    m4, [v210_enc_min_8]
    mova    m5, [v210_enc_max_8]
    pxor    m6, m6

.loop:
    movu        xm1, [yq+widthq*2]
%if cpuflag(avx2)
    vinserti128 m1,   m1, [yq+widthq*2+12], 1
%endif
    CLIPUB  m1, m4, m5

    punpcklbw m0, m1, m6
    ; can't unpack high bytes in the same way because we process
    ; only six bytes at a time
    pshufb  m1, [v210_enc_luma_shuf_8]

    pmullw  m0, [v210_enc_luma_mult_8]
    pmullw  m1, [v210_enc_luma_mult_8]
    pshufb  m0, [v210_enc_luma_shuf_10]
    pshufb  m1, [v210_enc_luma_shuf_10]

    movq         xm3, [uq+widthq]
    movhps       xm3, [vq+widthq]
%if cpuflag(avx2)
    movq         xm2, [uq+widthq+6]
    movhps       xm2, [vq+widthq+6]
    vinserti128  m3,   m3, xm2, 1
%endif
    CLIPUB  m3, m4, m5

    ; shuffle and multiply to get the same packing as in 10-bit
    pshufb  m2, m3, [v210_enc_chroma_shuf1_8]
    pshufb  m3, [v210_enc_chroma_shuf2_8]

    pmullw  m2, [v210_enc_chroma_mult_8]
    pmullw  m3, [v210_enc_chroma_mult_8]
    pshufb  m2, [v210_enc_chroma_shuf_10]
    pshufb  m3, [v210_enc_chroma_shuf_10]

    por     m0, m2
    por     m1, m3

    movu         [dstq],    xm0
    movu         [dstq+16], xm1
%if cpuflag(avx2)
    vextracti128 [dstq+32], m0, 1
    vextracti128 [dstq+48], m1, 1
%endif

    add     dstq, 2*mmsize
    add     widthq, (mmsize*3)/8
    jl .loop

    RET
%endmacro

%if HAVE_SSSE3_EXTERNAL
INIT_XMM ssse3
v210_planar_pack_8
%endif
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
v210_planar_pack_8
%endif

%macro v210_planar_pack_8_new 0

cglobal v210_planar_pack_8, 5, 5, 7+notcpuflag(avx512icl), y, u, v, dst, width
    add     yq, widthq
    shr     widthq, 1
    add     uq, widthq
    add     vq, widthq
    neg     widthq

    %if cpuflag(avx512icl)
        mova m2, [v210enc_8_permb]
    %else
        mova m2, [v210enc_8_permd]
    %endif
    vpbroadcastd   m3, [v210enc_8_mult]
    VBROADCASTI128 m4, [v210_enc_min_8] ; only ymm sized
    VBROADCASTI128 m5, [v210_enc_max_8] ; only ymm sized
    vpbroadcastd   m6, [v210enc_8_mask]
    %if notcpuflag(avx512icl)
        movu m7, [v210enc_8_shufb]
    %endif

    .loop:
        %if cpuflag(avx512icl)
            movu         ym1, [yq + 2*widthq]
            vinserti32x4  m1, [uq + 1*widthq], 2
            vinserti32x4  m1, [vq + 1*widthq], 3
            vpermb        m1, m2, m1                 ; uyvx yuyx vyux yvyx
        %else
            movq         xm0, [uq + 1*widthq]        ; uuuu uuxx
            movq         xm1, [vq + 1*widthq]        ; vvvv vvxx
            punpcklbw    xm1, xm0, xm1               ; uvuv uvuv uvuv xxxx
            vinserti128   m1, m1, [yq + 2*widthq], 1 ; uvuv uvuv uvuv xxxx yyyy yyyy yyyy xxxx
            vpermd        m1, m2, m1                 ; uvuv uvxx yyyy yyxx xxuv uvuv xxyy yyyy
            pshufb        m1, m7                     ; uyv0 yuy0 vyu0 yvy0
        %endif
        CLIPUB       m1, m4, m5

        pmaddubsw  m0, m1, m3 ; shift high and low samples of each dword and mask out other bits
        pslld      m1,  4     ; shift center sample of each dword
        %if cpuflag(avx512)
            vpternlogd m0, m1, m6, 0xd8 ; C?B:A ; merge and mask out bad bits from B
        %else
            pand       m1, m6, m1
            por        m0, m0, m1
        %endif

        movu  [dstq], m0
        add     dstq, mmsize
        add   widthq, (mmsize*3)/16
    jl .loop
RET

%endmacro

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
v210_planar_pack_8_new
%endif

%if HAVE_AVX512_EXTERNAL
INIT_YMM avx512
v210_planar_pack_8_new
%endif

%if HAVE_AVX512ICL_EXTERNAL
INIT_ZMM avx512icl
v210_planar_pack_8_new
%endif
