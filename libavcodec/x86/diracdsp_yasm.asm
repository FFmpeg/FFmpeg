;******************************************************************************
;* Copyright (c) 2010 David Conrad
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
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_RODATA
pw_7: times 8 dw 7

cextern pw_3
cextern pw_16
cextern pw_32
cextern pb_80

section .text

%macro UNPACK_ADD 6
    mov%5   %1, %3
    mov%6   m5, %4
    mova    m4, %1
    mova    %2, m5
    punpcklbw %1, m7
    punpcklbw m5, m7
    punpckhbw m4, m7
    punpckhbw %2, m7
    paddw   %1, m5
    paddw   %2, m4
%endmacro

%macro HPEL_FILTER 1
; dirac_hpel_filter_v_sse2(uint8_t *dst, uint8_t *src, int stride, int width);
cglobal dirac_hpel_filter_v_%1, 4,6,8, dst, src, stride, width, src0, stridex3
    mov     src0q, srcq
    lea     stridex3q, [3*strideq]
    sub     src0q, stridex3q
    pxor    m7, m7
.loop:
    ; 7*(src[0] + src[1])
    UNPACK_ADD m0, m1, [srcq], [srcq + strideq], a,a
    pmullw  m0, [pw_7]
    pmullw  m1, [pw_7]

    ; 3*( ... + src[-2] + src[3])
    UNPACK_ADD m2, m3, [src0q + strideq], [srcq + stridex3q], a,a
    paddw   m0, m2
    paddw   m1, m3
    pmullw  m0, [pw_3]
    pmullw  m1, [pw_3]

    ; ... - 7*(src[-1] + src[2])
    UNPACK_ADD m2, m3, [src0q + strideq*2], [srcq + strideq*2], a,a
    pmullw  m2, [pw_7]
    pmullw  m3, [pw_7]
    psubw   m0, m2
    psubw   m1, m3

    ; ... - (src[-3] + src[4])
    UNPACK_ADD m2, m3, [src0q], [srcq + strideq*4], a,a
    psubw   m0, m2
    psubw   m1, m3

    paddw   m0, [pw_16]
    paddw   m1, [pw_16]
    psraw   m0, 5
    psraw   m1, 5
    packuswb m0, m1
    mova    [dstq], m0
    add     dstq, mmsize
    add     srcq, mmsize
    add     src0q, mmsize
    sub     widthd, mmsize
    jg      .loop
    RET

; dirac_hpel_filter_h_sse2(uint8_t *dst, uint8_t *src, int width);
cglobal dirac_hpel_filter_h_%1, 3,3,8, dst, src, width
    dec     widthd
    pxor    m7, m7
    and     widthd, ~(mmsize-1)
.loop:
    ; 7*(src[0] + src[1])
    UNPACK_ADD m0, m1, [srcq + widthq], [srcq + widthq + 1], u,u
    pmullw  m0, [pw_7]
    pmullw  m1, [pw_7]

    ; 3*( ... + src[-2] + src[3])
    UNPACK_ADD m2, m3, [srcq + widthq - 2], [srcq + widthq + 3], u,u
    paddw   m0, m2
    paddw   m1, m3
    pmullw  m0, [pw_3]
    pmullw  m1, [pw_3]

    ; ... - 7*(src[-1] + src[2])
    UNPACK_ADD m2, m3, [srcq + widthq - 1], [srcq + widthq + 2], u,u
    pmullw  m2, [pw_7]
    pmullw  m3, [pw_7]
    psubw   m0, m2
    psubw   m1, m3

    ; ... - (src[-3] + src[4])
    UNPACK_ADD m2, m3, [srcq + widthq - 3], [srcq + widthq + 4], u,u
    psubw   m0, m2
    psubw   m1, m3

    paddw   m0, [pw_16]
    paddw   m1, [pw_16]
    psraw   m0, 5
    psraw   m1, 5
    packuswb m0, m1
    mova    [dstq + widthq], m0
    sub     widthd, mmsize
    jge     .loop
    RET
%endmacro

%macro PUT_RECT 1
; void put_rect_clamped(uint8_t *dst, int dst_stride, int16_t *src, int src_stride, int width, int height)
cglobal put_signed_rect_clamped_%1, 5,9,3, dst, dst_stride, src, src_stride, w, dst2, src2
    mova    m0, [pb_80]
    add     wd, (mmsize-1)
    and     wd, ~(mmsize-1)

%if ARCH_X86_64
    movsxd   dst_strideq, dst_strided
    movsxd   src_strideq, src_strided
    mov   r7d, r5m
    mov   r8d, wd
    %define wspill r8d
    %define hd r7d
%else
    mov    r4m, wd
    %define wspill r4m
    %define hd r5mp
%endif

.loopy:
    lea     src2q, [srcq+src_strideq*2]
    lea     dst2q, [dstq+dst_strideq]
.loopx:
    sub      wd, mmsize
    mova     m1, [srcq +2*wq]
    mova     m2, [src2q+2*wq]
    packsswb m1, [srcq +2*wq+mmsize]
    packsswb m2, [src2q+2*wq+mmsize]
    paddb    m1, m0
    paddb    m2, m0
    mova    [dstq +wq], m1
    mova    [dst2q+wq], m2
    jg      .loopx

    lea   srcq, [srcq+src_strideq*4]
    lea   dstq, [dstq+dst_strideq*2]
    sub     hd, 2
    mov     wd, wspill
    jg      .loopy
    RET
%endm

%macro ADD_RECT 1
; void add_rect_clamped(uint8_t *dst, uint16_t *src, int stride, int16_t *idwt, int idwt_stride, int width, int height)
cglobal add_rect_clamped_%1, 7,9,3, dst, src, stride, idwt, idwt_stride, w, h
    mova    m0, [pw_32]
    add     wd, (mmsize-1)
    and     wd, ~(mmsize-1)

%if ARCH_X86_64
    movsxd   strideq, strided
    movsxd   idwt_strideq, idwt_strided
    mov   r8d, wd
    %define wspill r8d
%else
    mov    r5m, wd
    %define wspill r5m
%endif

.loop:
    sub     wd, mmsize
    movu    m1, [srcq +2*wq] ; FIXME: ensure alignment
    paddw   m1, m0
    psraw   m1, 6
    movu    m2, [srcq +2*wq+mmsize] ; FIXME: ensure alignment
    paddw   m2, m0
    psraw   m2, 6
    paddw   m1, [idwtq+2*wq]
    paddw   m2, [idwtq+2*wq+mmsize]
    packuswb m1, m2
    mova    [dstq +wq], m1
    jg      .loop

    lea   srcq, [srcq + 2*strideq]
    add   dstq, strideq
    lea  idwtq, [idwtq+ 2*idwt_strideq]
    sub     hd, 1
    mov     wd, wspill
    jg      .loop
    RET
%endm

%macro ADD_OBMC 2
; void add_obmc(uint16_t *dst, uint8_t *src, int stride, uint8_t *obmc_weight, int yblen)
cglobal add_dirac_obmc%1_%2, 6,6,5, dst, src, stride, obmc, yblen
    pxor        m4, m4
.loop:
%assign i 0
%rep %1 / mmsize
    mova        m0, [srcq+i]
    mova        m1, m0
    punpcklbw   m0, m4
    punpckhbw   m1, m4
    mova        m2, [obmcq+i]
    mova        m3, m2
   punpcklbw   m2, m4
    punpckhbw   m3, m4
    pmullw      m0, m2
    pmullw      m1, m3
    movu        m2, [dstq+2*i]
    movu        m3, [dstq+2*i+mmsize]
    paddw       m0, m2
    paddw       m1, m3
    movu        [dstq+2*i], m0
    movu        [dstq+2*i+mmsize], m1
%assign i i+mmsize
%endrep
    lea         srcq, [srcq+strideq]
    lea         dstq, [dstq+2*strideq]
    add         obmcq, 32
    sub         yblend, 1
    jg          .loop
    RET
%endm

INIT_MMX
%if ARCH_X86_64 == 0
PUT_RECT mmx
ADD_RECT mmx

HPEL_FILTER mmx
ADD_OBMC 32, mmx
ADD_OBMC 16, mmx
%endif
ADD_OBMC 8, mmx

INIT_XMM
PUT_RECT sse2
ADD_RECT sse2

HPEL_FILTER sse2
ADD_OBMC 32, sse2
ADD_OBMC 16, sse2
