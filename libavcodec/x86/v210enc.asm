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

SECTION_RODATA

v210_enc_min_10: times 8 dw 0x4
v210_enc_max_10: times 8 dw 0x3fb

v210_enc_luma_mult_10: dw 4,1,16,4,1,16,0,0
v210_enc_luma_shuf_10: db -1,0,1,-1,2,3,4,5,-1,6,7,-1,8,9,10,11

v210_enc_chroma_mult_10: dw 1,4,16,0,16,1,4,0
v210_enc_chroma_shuf_10: db 0,1,8,9,-1,2,3,-1,10,11,4,5,-1,12,13,-1

v210_enc_min_8: times 16 db 0x1
v210_enc_max_8: times 16 db 0xfe

v210_enc_luma_shuf_8: db 6,-1,7,-1,8,-1,9,-1,10,-1,11,-1,-1,-1,-1,-1
v210_enc_luma_mult_8: dw 16,4,64,16,4,64,0,0

v210_enc_chroma_shuf1_8: db 0,-1,1,-1,2,-1,3,-1,8,-1,9,-1,10,-1,11,-1
v210_enc_chroma_shuf2_8: db 3,-1,4,-1,5,-1,7,-1,11,-1,12,-1,13,-1,15,-1

v210_enc_chroma_mult_8: dw 4,16,64,0,64,4,16,0

SECTION .text

%macro v210_planar_pack_10 0

; v210_planar_pack_10(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst, ptrdiff_t width)
cglobal v210_planar_pack_10, 5, 5, 4, y, u, v, dst, width
    lea     r0, [yq+2*widthq]
    add     uq, widthq
    add     vq, widthq
    neg     widthq

    mova    m2, [v210_enc_min_10]
    mova    m3, [v210_enc_max_10]

.loop
    movu    m0, [yq+2*widthq]
    CLIPW   m0, m2, m3

    movq    m1, [uq+widthq]
    movhps  m1, [vq+widthq]
    CLIPW   m1, m2, m3

    pmullw  m0, [v210_enc_luma_mult_10]
    pshufb  m0, [v210_enc_luma_shuf_10]

    pmullw  m1, [v210_enc_chroma_mult_10]
    pshufb  m1, [v210_enc_chroma_shuf_10]

    por     m0, m1

    movu    [dstq], m0

    add     dstq, mmsize
    add     widthq, 6
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
v210_planar_pack_10

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

.loop
    movu    m1, [yq+2*widthq]
    CLIPUB  m1, m4, m5

    punpcklbw m0, m1, m6
    ; can't unpack high bytes in the same way because we process
    ; only six bytes at a time
    pshufb  m1, [v210_enc_luma_shuf_8]

    pmullw  m0, [v210_enc_luma_mult_8]
    pmullw  m1, [v210_enc_luma_mult_8]
    pshufb  m0, [v210_enc_luma_shuf_10]
    pshufb  m1, [v210_enc_luma_shuf_10]

    movq    m3, [uq+widthq]
    movhps  m3, [vq+widthq]
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

    movu    [dstq], m0
    movu    [dstq+mmsize], m1

    add     dstq, 2*mmsize
    add     widthq, 6
    jl .loop

    RET
%endmacro

INIT_XMM ssse3
v210_planar_pack_8
INIT_XMM avx
v210_planar_pack_8
