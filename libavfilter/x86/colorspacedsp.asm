;*****************************************************************************
;* x86-optimized functions for colorspace filter
;*
;* Copyright (C) 2016 Ronald S. Bultje <rsbultje@gmail.com>
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

pw_1: times 8 dw 1
pw_2: times 8 dw 2
pw_4: times 8 dw 4
pw_8: times 8 dw 8
pw_16: times 8 dw 16
pw_64: times 8 dw 64
pw_128: times 8 dw 128
pw_256: times 8 dw 256
pw_512: times 8 dw 512
pw_1023: times 8 dw 1023
pw_1024: times 8 dw 1024
pw_2048: times 8 dw 2048
pw_4095: times 8 dw 4095
pw_8192: times 8 dw 8192
pw_16384: times 8 dw 16384

pd_1: times 4 dd 1
pd_2: times 4 dd 2
pd_128: times 4 dd 128
pd_512: times 4 dd 512
pd_2048: times 4 dd 2048
pd_8192: times 4 dd 8192
pd_32768: times 4 dd 32768
pd_131072: times 4 dd 131072

SECTION .text

; void ff_yuv2yuv_420p8to8_sse2(uint8_t *yuv_out[3], ptrdiff_t yuv_out_stride[3],
;                               uint8_t *yuv_in[3], ptrdiff_t yuv_in_stride[3],
;                               int w, int h, const int16_t yuv2yuv_coeffs[3][3][8],
;                               const int16_t yuv_offset[2][8])

%if ARCH_X86_64
%macro YUV2YUV_FN 4 ; in_bitdepth, out_bitdepth, log2_chroma_w (horiz), log2_chroma_h (vert)

%assign %%sh (14 + %1 - %2)
%assign %%rnd (1 << (%%sh - 1))
%assign %%uvinoff (128 << (%1 - 8))
%assign %%uvoutoff (128 << (%2 - 8))
%if %3 == 0
%assign %%ss 444
%elif %4 == 0
%assign %%ss 422
%else ; %4 == 1
%assign %%ss 420
%endif ; %3/%4
%if %2 != 8
%assign %%maxval (1 << %2) - 1
%endif ; %2 != 8

%assign %%ypsh %%sh - 1
%if %%ypsh > 14
%assign %%yoffsh %%ypsh - 13
%assign %%ypsh 14
%else
%assign %%yoffsh 1
%endif
%assign %%yprnd (1 << (%%yoffsh - 1))
%assign %%ypmul (1 << %%ypsh)

cglobal yuv2yuv_ %+ %%ss %+ p%1to%2, 8, 14, 16, 0 - (4 * mmsize), \
                                     yo, yos, yi, yis, w, h, c, yoff, ui, vi, uo, vo
%if %3 == 1
    inc             wd
    sar             wd, 1
%if %4 == 1
    inc             hd
    sar             hd, 1
%endif ; %4 == 1
%endif ; %3 == 1
    mov [rsp+3*mmsize+0], wd
    mov [rsp+3*mmsize+4], hd

    mova           m10, [cq]
    pxor           m11, m11
    mova           m12, [pd_ %+ %%uvoutoff]
    pslld          m12, %%sh
    paddd          m12, [pd_ %+ %%rnd]
    mova           m13, [pw_ %+ %%uvinoff]
    mova           m14, [yoffq+ 0]      ; y_off_in
    mova           m15, [yoffq+16]      ; y_off_out
%if %%yoffsh != 0
    psllw          m15, %%yoffsh
%endif
    paddw          m15, [pw_ %+ %%yprnd]
    punpcklwd      m10, m15
    mova           m15, [pw_ %+ %%ypmul]
    movh            m0, [cq+1*16]       ; cyu
    movh            m1, [cq+2*16]       ; cyv
    movh            m2, [cq+4*16]       ; cuu
    movh            m3, [cq+5*16]       ; cuv
    movh            m4, [cq+7*16]       ; cvu
    movh            m5, [cq+8*16]       ; cvv
    punpcklwd       m0, m1
    punpcklwd       m2, m3
    punpcklwd       m4, m5
    mova [rsp+0*mmsize], m0
    mova [rsp+1*mmsize], m2
    mova [rsp+2*mmsize], m4

    DEFINE_ARGS yo, yos, yi, yis, ui, vi, uo, vo, uis, vis, uos, vos, x, tmp

    mov            uiq, [yiq+gprsize*1]
    mov            viq, [yiq+gprsize*2]
    mov            yiq, [yiq+gprsize*0]
    mov            uoq, [yoq+gprsize*1]
    mov            voq, [yoq+gprsize*2]
    mov            yoq, [yoq+gprsize*0]
    mov           uisq, [yisq+gprsize*1]
    mov           visq, [yisq+gprsize*2]
    mov           yisq, [yisq+gprsize*0]
    mov           uosq, [yosq+gprsize*1]
    mov           vosq, [yosq+gprsize*2]
    mov           yosq, [yosq+gprsize*0]

.loop_v:
    xor             xq, xq

.loop_h:
%if %4 == 1
    lea           tmpq, [yiq+yisq]
%endif ; %4 == 1
%if %1 == 8
    movu            m0, [yiq+xq*(1<<%3)]        ; y00/01
%if %4 == 1
    movu            m2, [tmpq+xq*2]             ; y10/11
%endif ; %4 == 1
%if %3 == 1
    movh            m4, [uiq+xq]                ; u
    movh            m5, [viq+xq]                ; v
%else ; %3 != 1
    movu            m4, [uiq+xq]                ; u
    movu            m5, [viq+xq]                ; v
%endif ; %3 ==/!= 1
    punpckhbw       m1, m0, m11
    punpcklbw       m0, m11
%if %4 == 1
    punpckhbw       m3, m2, m11
    punpcklbw       m2, m11
%endif ; %4 == 1
%if %3 == 0
    punpckhbw       m2, m4, m11
    punpckhbw       m3, m5, m11
%endif ; %3 == 0
    punpcklbw       m4, m11
    punpcklbw       m5, m11
%else ; %1 != 8
    movu            m0, [yiq+xq*(2<<%3)]        ; y00/01
    movu            m1, [yiq+xq*(2<<%3)+mmsize] ; y00/01
%if %4 == 1
    movu            m2, [tmpq+xq*4]             ; y10/11
    movu            m3, [tmpq+xq*4+mmsize]      ; y10/11
%endif ; %4 == 1
    movu            m4, [uiq+xq*2]              ; u
    movu            m5, [viq+xq*2]              ; v
%if %3 == 0
    movu            m2, [uiq+xq*2+mmsize]
    movu            m3, [viq+xq*2+mmsize]
%endif ; %3 == 0
%endif ; %1 ==/!= 8
    psubw           m0, m14
    psubw           m1, m14
%if %4 == 1
    psubw           m2, m14
    psubw           m3, m14
%endif ; %4 == 1
    psubw           m4, m13
    psubw           m5, m13
%if %3 == 0
    psubw           m2, m13
    psubw           m3, m13
%endif ; %3 == 0

    SBUTTERFLY   wd, 4, 5, 6
    pmaddwd         m6, m4, [rsp+1*mmsize]
    pmaddwd         m7, m5, [rsp+1*mmsize]
%if %3 == 0
    SBUTTERFLY   wd, 2, 3, 8
    pmaddwd         m8, m2, [rsp+1*mmsize]
    pmaddwd         m9, m3, [rsp+1*mmsize]
%else ; %3 != 0
    pmaddwd         m8, m4, [rsp+2*mmsize]
    pmaddwd         m9, m5, [rsp+2*mmsize]
%endif
    paddd           m6, m12
    paddd           m7, m12
    paddd           m8, m12
    paddd           m9, m12
    psrad           m6, %%sh
    psrad           m7, %%sh
    psrad           m8, %%sh
    psrad           m9, %%sh
    packssdw        m6, m7
    packssdw        m8, m9
%if %2 == 8
    packuswb        m6, m8
%if %3 == 0
    movu      [uoq+xq], m6
%else ; %3 != 0
    movh      [uoq+xq], m6
    movhps    [voq+xq], m6
%endif ; %3 ==/!= 0
%else ; %2 != 8
    CLIPW           m6, m11, [pw_ %+ %%maxval]
    CLIPW           m8, m11, [pw_ %+ %%maxval]
    movu    [uoq+xq*2], m6
%if %3 == 0
    movu    [uoq+xq*2+mmsize], m8
%else ; %3 != 0
    movu    [voq+xq*2], m8
%endif ; %3 ==/!= 0
%endif ; %2 ==/!= 8

%if %3 == 0
    pmaddwd         m6, m4, [rsp+2*mmsize]
    pmaddwd         m7, m5, [rsp+2*mmsize]
    pmaddwd         m8, m2, [rsp+2*mmsize]
    pmaddwd         m9, m3, [rsp+2*mmsize]
    paddd           m6, m12
    paddd           m7, m12
    paddd           m8, m12
    paddd           m9, m12
    psrad           m6, %%sh
    psrad           m7, %%sh
    psrad           m8, %%sh
    psrad           m9, %%sh
    packssdw        m6, m7
    packssdw        m8, m9
%if %2 == 8
    packuswb        m6, m8
    movu      [voq+xq], m6
%else ; %2 != 8
    CLIPW           m6, m11, [pw_ %+ %%maxval]
    CLIPW           m8, m11, [pw_ %+ %%maxval]
    movu    [voq+xq*2], m6
    movu    [voq+xq*2+mmsize], m8
%endif ; %2 ==/!= 8
%endif ; %3 == 0

    pmaddwd         m4, [rsp+0*mmsize]
    pmaddwd         m5, [rsp+0*mmsize]          ; uv_val
%if %3 == 0
    pmaddwd         m2, [rsp+0*mmsize]
    pmaddwd         m3, [rsp+0*mmsize]
%endif ; %3 == 0

    ; unpack y pixels with m15 (shifted round + offset), then multiply
    ; by m10, add uv pixels, and we're done!
%if %3 == 1
    punpckhdq       m8, m4, m4
    punpckldq       m4, m4
    punpckhdq       m9, m5, m5
    punpckldq       m5, m5
%else ; %3 != 1
    SWAP             8, 5, 2
    SWAP             3, 9
%endif ; %3 ==/!= 1
%if %4 == 1
    punpckhwd       m6, m2, m15
    punpcklwd       m2, m15
    punpckhwd       m7, m3, m15
    punpcklwd       m3, m15
    pmaddwd         m2, m10
    pmaddwd         m6, m10
    pmaddwd         m3, m10
    pmaddwd         m7, m10
    paddd           m2, m4
    paddd           m6, m8
    paddd           m3, m5
    paddd           m7, m9
    psrad           m2, %%sh
    psrad           m6, %%sh
    psrad           m3, %%sh
    psrad           m7, %%sh
    packssdw        m2, m6
    packssdw        m3, m7

    lea           tmpq, [yoq+yosq]
%if %2 == 8
    packuswb        m2, m3
    movu   [tmpq+xq*2], m2
%else ; %2 != 8
    CLIPW           m2, m11, [pw_ %+ %%maxval]
    CLIPW           m3, m11, [pw_ %+ %%maxval]
    movu   [tmpq+xq*4], m2
    movu [tmpq+xq*4+mmsize], m3
%endif ; %2 ==/!= 8
%endif ; %4 == 1

    punpckhwd       m6, m0, m15
    punpcklwd       m0, m15
    punpckhwd       m7, m1, m15
    punpcklwd       m1, m15
    pmaddwd         m0, m10
    pmaddwd         m6, m10
    pmaddwd         m1, m10
    pmaddwd         m7, m10
    paddd           m0, m4
    paddd           m6, m8
    paddd           m1, m5
    paddd           m7, m9
    psrad           m0, %%sh
    psrad           m6, %%sh
    psrad           m1, %%sh
    psrad           m7, %%sh
    packssdw        m0, m6
    packssdw        m1, m7

%if %2 == 8
    packuswb        m0, m1
    movu    [yoq+xq*(1<<%3)], m0
%else ; %2 != 8
    CLIPW           m0, m11, [pw_ %+ %%maxval]
    CLIPW           m1, m11, [pw_ %+ %%maxval]
    movu  [yoq+xq*(2<<%3)], m0
    movu [yoq+xq*(2<<%3)+mmsize], m1
%endif ; %2 ==/!= 8

    add             xq, mmsize >> %3
    cmp             xd, dword [rsp+3*mmsize+0]
    jl .loop_h

%if %4 == 1
    lea            yiq, [yiq+yisq*2]
    lea            yoq, [yoq+yosq*2]
%else ; %4 != 1
    add            yiq, yisq
    add            yoq, yosq
%endif ; %4 ==/!= 1
    add            uiq, uisq
    add            viq, visq
    add            uoq, uosq
    add            voq, vosq
    dec dword [rsp+3*mmsize+4]
    jg .loop_v

    RET
%endmacro

%macro YUV2YUV_FNS 2 ; ss_w, ss_h
YUV2YUV_FN  8,  8, %1, %2
YUV2YUV_FN 10,  8, %1, %2
YUV2YUV_FN 12,  8, %1, %2
YUV2YUV_FN  8, 10, %1, %2
YUV2YUV_FN 10, 10, %1, %2
YUV2YUV_FN 12, 10, %1, %2
YUV2YUV_FN  8, 12, %1, %2
YUV2YUV_FN 10, 12, %1, %2
YUV2YUV_FN 12, 12, %1, %2
%endmacro

INIT_XMM sse2
YUV2YUV_FNS 0, 0
YUV2YUV_FNS 1, 0
YUV2YUV_FNS 1, 1

; void ff_yuv2rgb_420p8_sse2(int16_t *rgb[3], ptrdiff_t rgb_stride,
;                            uint8_t *yuv[3], ptrdiff_t yuv_stride[3],
;                            int w, int h, const int16_t yuv2rgb_coeffs[3][3][8],
;                            const int16_t yuv_offset[8])
%macro YUV2RGB_FN 3 ; depth, log2_chroma_w (horiz), log2_chroma_h (vert)
%assign %%sh (%1 - 1)
%assign %%rnd (1 << (%%sh - 1))
%assign %%uvoff (1 << (%1 - 1))
%if %2 == 0
%assign %%ss 444
%elif %3 == 0
%assign %%ss 422
%else ; %3 == 1
%assign %%ss 420
%endif ; %2/%3

cglobal yuv2rgb_ %+ %%ss %+ p%1, 8, 14, 16, 0 - 8 * mmsize, \
                                rgb, rgbs, yuv, yuvs, ww, h, c, yoff
%if %2 == 1
    inc            wwd
    sar            wwd, 1
%endif ; %2 == 1
%if %3 == 1
    inc             hd
    sar             hd, 1
%endif ; %3 == 1
    pxor           m11, m11
    mova           m15, [yoffq]                 ; yoff
    movh           m14, [cq+  0]                ; cy
    movh           m10, [cq+ 32]                ; crv
    movh           m13, [cq+112]                ; cbu
    movh           m12, [cq+ 64]                ; cgu
    movh            m9, [cq+ 80]                ; cgv
    punpcklwd      m14, [pw_ %+ %%rnd]          ; cy, rnd
    punpcklwd      m13, m11                     ; cbu, 0
    punpcklwd      m11, m10                     ; 0, crv
    punpcklwd      m12, m9                      ; cgu, cgv
    mova [rsp+0*mmsize], m11
    mova [rsp+1*mmsize], m12
    mova [rsp+2*mmsize], m13
    mova [rsp+3*mmsize], m14
    pxor           m14, m14

    DEFINE_ARGS r, rgbs, y, ys, ww, h, g, b, u, v, us, vs, x, tmp

    mov             gq, [rq+1*gprsize]
    mov             bq, [rq+2*gprsize]
    mov             rq, [rq+0*gprsize]
    mov             uq, [yq+1*gprsize]
    mov             vq, [yq+2*gprsize]
    mov             yq, [yq+0*gprsize]
    mov            usq, [ysq+1*gprsize]
    mov            vsq, [ysq+2*gprsize]
    mov            ysq, [ysq+0*gprsize]

.loop_v:
    xor             xq, xq

.loop_h:
%if %3 == 1
    lea           tmpq, [yq+ysq]
%endif ; %3 == 1
%if %1 == 8
    movu            m0, [yq+xq*(1<<%2)]
%if %3 == 1
    movu            m2, [tmpq+xq*2]
%endif ; %3 == 1
%if %2 == 1
    movh            m4, [uq+xq]
    movh            m5, [vq+xq]
%else ; %2 != 1
    movu            m4, [uq+xq]
    movu            m5, [vq+xq]
%endif ; %2 ==/!= 1
    punpckhbw       m1, m0, m14
    punpcklbw       m0, m14
%if %3 == 1
    punpckhbw       m3, m2, m14
    punpcklbw       m2, m14
%endif ; %3 == 1
%if %2 == 0
    punpckhbw       m2, m4, m14
    punpckhbw       m3, m5, m14
%endif ; %2 == 0
    punpcklbw       m4, m14
    punpcklbw       m5, m14
%else ; %1 != 8
    movu            m0, [yq+xq*(2<<%2)]
    movu            m1, [yq+xq*(2<<%2)+mmsize]
%if %3 == 1
    movu            m2, [tmpq+xq*4]
    movu            m3, [tmpq+xq*4+mmsize]
%endif ; %3 == 1
    movu            m4, [uq+xq*2]
    movu            m5, [vq+xq*2]
%if %2 == 0
    movu            m2, [uq+xq*2+mmsize]
    movu            m3, [vq+xq*2+mmsize]
%endif ; %2 == 0
%endif ; %1 ==/!= 8
    psubw           m0, m15
    psubw           m1, m15
%if %3 == 1
    psubw           m2, m15
    psubw           m3, m15
%endif ; %3 == 1
    psubw           m4, [pw_ %+ %%uvoff]
    psubw           m5, [pw_ %+ %%uvoff]
    SBUTTERFLY   wd, 4, 5, 6
%if %2 == 0
    psubw           m2, [pw_ %+ %%uvoff]
    psubw           m3, [pw_ %+ %%uvoff]
    SBUTTERFLY   wd, 2, 3, 6
%endif ; %2 == 0

    ; calculate y+rnd full-resolution [0-3,6-9]
    punpckhwd       m6, m0, [pw_1]              ; y, 1
    punpcklwd       m0, [pw_1]                  ; y, 1
    punpckhwd       m7, m1, [pw_1]              ; y, 1
    punpcklwd       m1, [pw_1]                  ; y, 1
    pmaddwd         m0, [rsp+3*mmsize]
    pmaddwd         m6, [rsp+3*mmsize]
    pmaddwd         m1, [rsp+3*mmsize]
    pmaddwd         m7, [rsp+3*mmsize]
%if %3 == 1
    punpckhwd       m8, m2, [pw_1]              ; y, 1
    punpcklwd       m2, [pw_1]                  ; y, 1
    punpckhwd       m9, m3, [pw_1]              ; y, 1
    punpcklwd       m3, [pw_1]                  ; y, 1
    pmaddwd         m2, [rsp+3*mmsize]
    pmaddwd         m8, [rsp+3*mmsize]
    pmaddwd         m3, [rsp+3*mmsize]
    pmaddwd         m9, [rsp+3*mmsize]
    mova [rsp+4*mmsize], m2
    mova [rsp+5*mmsize], m8
    mova [rsp+6*mmsize], m3
    mova [rsp+7*mmsize], m9
%endif ; %3 == 1

    ; calculate r offsets (un-subsampled, then duplicate)
    pmaddwd        m10, m4, [rsp+0*mmsize]
%if %2 == 1
    pmaddwd        m12, m5, [rsp+0*mmsize]
    punpckhdq      m11, m10, m10
    punpckldq      m10, m10
    punpckhdq      m13, m12, m12
    punpckldq      m12, m12
%else ; %2 != 1
    pmaddwd        m11, m5, [rsp+0*mmsize]
    pmaddwd        m12, m2, [rsp+0*mmsize]
    pmaddwd        m13, m3, [rsp+0*mmsize]
%endif ; %2 ==/!= 1
%if %3 == 1
    paddd           m2, m10, [rsp+4*mmsize]
    paddd           m3, m11, [rsp+5*mmsize]
    paddd           m8, m12, [rsp+6*mmsize]
    paddd           m9, m13, [rsp+7*mmsize]
%endif
    paddd          m10, m0
    paddd          m11, m6
    paddd          m12, m1
    paddd          m13, m7
%if %3 == 1
    psrad           m2, %%sh
    psrad           m3, %%sh
    psrad           m8, %%sh
    psrad           m9, %%sh
%endif ; %3 == 1
    psrad          m10, %%sh
    psrad          m11, %%sh
    psrad          m12, %%sh
    psrad          m13, %%sh
%if %3 == 1
    lea           tmpq, [rq+rgbsq*2]
    packssdw        m2, m3
    packssdw        m8, m9
    mova [tmpq+xq*4], m2
    mova [tmpq+xq*4+mmsize], m8
%endif ; %3 == 1
    packssdw       m10, m11
    packssdw       m12, m13
    mova   [rq+xq*(2 << %2)], m10
    mova   [rq+xq*(2 << %2)+mmsize], m12

    ; calculate g offsets (un-subsampled, then duplicate)
    pmaddwd        m10, m4, [rsp+1*mmsize]
%if %2 == 1
    pmaddwd        m12, m5, [rsp+1*mmsize]
    punpckhdq      m11, m10, m10
    punpckldq      m10, m10
    punpckhdq      m13, m12, m12
    punpckldq      m12, m12
%else ; %2 != 1
    pmaddwd        m11, m5, [rsp+1*mmsize]
    pmaddwd        m12, m2, [rsp+1*mmsize]
    pmaddwd        m13, m3, [rsp+1*mmsize]
%endif ; %2 ==/!= 1
%if %3 == 1
    paddd           m2, m10, [rsp+4*mmsize]
    paddd           m3, m11, [rsp+5*mmsize]
    paddd           m8, m12, [rsp+6*mmsize]
    paddd           m9, m13, [rsp+7*mmsize]
%endif ; %3 == 1
    paddd          m10, m0
    paddd          m11, m6
    paddd          m12, m1
    paddd          m13, m7
%if %3 == 1
    psrad           m2, %%sh
    psrad           m3, %%sh
    psrad           m8, %%sh
    psrad           m9, %%sh
%endif ; %3 == 1
    psrad          m10, %%sh
    psrad          m11, %%sh
    psrad          m12, %%sh
    psrad          m13, %%sh
%if %3 == 1
    lea           tmpq, [gq+rgbsq*2]
    packssdw        m2, m3
    packssdw        m8, m9
    mova [tmpq+xq*4], m2
    mova [tmpq+xq*4+mmsize], m8
%endif ; %3 == 1
    packssdw       m10, m11
    packssdw       m12, m13
    mova   [gq+xq*(2 << %2)], m10
    mova   [gq+xq*(2 << %2)+mmsize], m12

    ; calculate b offsets (un-subsampled, then duplicate)
    pmaddwd         m4, [rsp+2*mmsize]
    pmaddwd         m5, [rsp+2*mmsize]
%if %2 == 1
    punpckhdq       m2, m4, m4
    punpckldq       m4, m4
    punpckhdq       m3, m5, m5
    punpckldq       m5, m5
%else ; %2 != 1
    pmaddwd         m2, [rsp+2*mmsize]
    pmaddwd         m3, [rsp+2*mmsize]
    SWAP             2, 5
%endif ; %2 ==/!= 1
    paddd           m0, m4
    paddd           m6, m2
    paddd           m1, m5
    paddd           m7, m3
%if %3 == 1
    paddd           m4, [rsp+4*mmsize]
    paddd           m2, [rsp+5*mmsize]
    paddd           m5, [rsp+6*mmsize]
    paddd           m3, [rsp+7*mmsize]
%endif ; %3 == 1
    psrad           m0, %%sh
    psrad           m6, %%sh
    psrad           m1, %%sh
    psrad           m7, %%sh
%if %3 == 1
    psrad           m4, %%sh
    psrad           m2, %%sh
    psrad           m5, %%sh
    psrad           m3, %%sh
%endif ; %3 == 1
    packssdw        m0, m6
    packssdw        m1, m7
    movu   [bq+xq*(2 << %2)], m0
    movu   [bq+xq*(2 << %2)+mmsize], m1
%if %3 == 1
    lea           tmpq, [bq+rgbsq*2]
    packssdw        m4, m2
    packssdw        m5, m3
    movu [tmpq+xq*4], m4
    movu [tmpq+xq*4+mmsize], m5
%endif ; %3 == 1

    add             xd, mmsize >> %2
    cmp             xd, wwd
    jl .loop_h

    lea             rq, [rq+rgbsq*(2 << %3)]
    lea             gq, [gq+rgbsq*(2 << %3)]
    lea             bq, [bq+rgbsq*(2 << %3)]
%if %3 == 1
    lea             yq, [yq+ysq*2]
%else ; %3 != 0
    add             yq, ysq
%endif ; %3 ==/!= 1
    add             uq, usq
    add             vq, vsq
    dec             hd
    jg .loop_v

    RET
%endmacro

%macro YUV2RGB_FNS 2
YUV2RGB_FN  8, %1, %2
YUV2RGB_FN 10, %1, %2
YUV2RGB_FN 12, %1, %2
%endmacro

INIT_XMM sse2
YUV2RGB_FNS 0, 0
YUV2RGB_FNS 1, 0
YUV2RGB_FNS 1, 1

%macro RGB2YUV_FN 3 ; depth, log2_chroma_w (horiz), log2_chroma_h (vert)
%assign %%sh 29 - %1
%assign %%rnd (1 << (%%sh - 15))
%assign %%uvrnd ((128 << (%1 - 8)) << (%%sh - 14))
%if %1 != 8
%assign %%maxval ((1 << %1) - 1)
%endif ; %1 != 8
%if %2 == 0
%assign %%ss 444
%elif %3 == 0
%assign %%ss 422
%else ; %3 == 1
%assign %%ss 420
%endif ; %2/%3

cglobal rgb2yuv_ %+ %%ss %+ p%1, 8, 14, 16, 0 - 6 * mmsize, \
                                 yuv, yuvs, rgb, rgbs, ww, h, c, off
%if %2 == 1
    inc            wwd
    sar            wwd, 1
%endif ; %2 == 1
%if %3 == 1
    inc             hd
    sar             hd, 1
%endif ; %3 == 1

    ; prepare coeffs
    movh            m8, [offq]
    movh            m9, [pw_ %+ %%uvrnd]
    psllw           m8, %%sh - 14
    paddw           m9, [pw_ %+ %%rnd]
    paddw           m8, [pw_ %+ %%rnd]
    movh            m0, [cq+  0]
    movh            m1, [cq+ 16]
    movh            m2, [cq+ 32]
    movh            m3, [cq+ 48]
    movh            m4, [cq+ 64]
    movh            m5, [cq+ 80]
    movh            m6, [cq+112]
    movh            m7, [cq+128]
    punpcklwd       m0, m1
    punpcklwd       m2, m8
    punpcklwd       m3, m4
    punpcklwd       m4, m5, m9
    punpcklwd       m5, m6
    punpcklwd       m7, m9

    mova [rsp+0*mmsize], m0                 ; cry, cgy
    mova [rsp+1*mmsize], m2                 ; cby, off + rnd
    mova [rsp+2*mmsize], m3                 ; cru, cgu
    mova [rsp+3*mmsize], m4                 ; cburv, uvoff + rnd
    mova [rsp+4*mmsize], m5                 ; cburv, cgv
    mova [rsp+5*mmsize], m7                 ; cbv, uvoff + rnd


    DEFINE_ARGS y, ys, r, rgbs, ww, h, u, v, us, vs, g, b, tmp, x
    mov             gq, [rq+gprsize*1]
    mov             bq, [rq+gprsize*2]
    mov             rq, [rq+gprsize*0]
    mov             uq, [yq+gprsize*1]
    mov             vq, [yq+gprsize*2]
    mov             yq, [yq+gprsize*0]
    mov            usq, [ysq+gprsize*1]
    mov            vsq, [ysq+gprsize*2]
    mov            ysq, [ysq+gprsize*0]

    pxor           m15, m15
.loop_v:
    xor             xd, xd

.loop_h:
    ; top line y
    mova            m0, [rq+xq*(2<<%2)]
    mova            m3, [rq+xq*(2<<%2)+mmsize]
    mova            m1, [gq+xq*(2<<%2)]
    mova            m4, [gq+xq*(2<<%2)+mmsize]
    mova            m2, [bq+xq*(2<<%2)]
    mova            m5, [bq+xq*(2<<%2)+mmsize]

    punpcklwd       m6, m0, m1
    punpckhwd       m7, m0, m1
    punpcklwd       m8, m3, m4
    punpckhwd       m9, m3, m4
    punpcklwd      m10, m2, [pw_16384]
    punpckhwd      m11, m2, [pw_16384]
    punpcklwd      m12, m5, [pw_16384]
    punpckhwd      m13, m5, [pw_16384]

    pmaddwd         m6, [rsp+0*mmsize]
    pmaddwd         m7, [rsp+0*mmsize]
    pmaddwd         m8, [rsp+0*mmsize]
    pmaddwd         m9, [rsp+0*mmsize]
    pmaddwd        m10, [rsp+1*mmsize]
    pmaddwd        m11, [rsp+1*mmsize]
    pmaddwd        m12, [rsp+1*mmsize]
    pmaddwd        m13, [rsp+1*mmsize]
    paddd           m6, m10
    paddd           m7, m11
    paddd           m8, m12
    paddd           m9, m13
    psrad           m6, %%sh
    psrad           m7, %%sh
    psrad           m8, %%sh
    psrad           m9, %%sh
    packssdw        m6, m7
    packssdw        m8, m9
%if %1 == 8
    packuswb        m6, m8
    movu [yq+xq*(1<<%2)], m6
%else
    CLIPW           m6, m15, [pw_ %+ %%maxval]
    CLIPW           m8, m15, [pw_ %+ %%maxval]
    movu [yq+xq*(2<<%2)], m6
    movu [yq+xq*(2<<%2)+mmsize], m8
%endif

%if %2 == 1
    ; subsampling cached data
    pmaddwd         m0, [pw_1]
    pmaddwd         m1, [pw_1]
    pmaddwd         m2, [pw_1]
    pmaddwd         m3, [pw_1]
    pmaddwd         m4, [pw_1]
    pmaddwd         m5, [pw_1]

%if %3 == 1
    ; bottom line y, r/g portion only
    lea           tmpq, [rgbsq+xq*2]
    mova            m6, [rq+tmpq*2]
    mova            m9, [rq+tmpq*2+mmsize]
    mova            m7, [gq+tmpq*2]
    mova           m10, [gq+tmpq*2+mmsize]
    mova            m8, [bq+tmpq*2]
    mova           m11, [bq+tmpq*2+mmsize]

    punpcklwd      m12, m6, m7
    punpckhwd      m13, m6, m7
    punpcklwd      m14, m9, m10
    punpckhwd      m15, m9, m10

    ; release two more registers
    pmaddwd         m6, [pw_1]
    pmaddwd         m7, [pw_1]
    pmaddwd         m9, [pw_1]
    pmaddwd        m10, [pw_1]
    paddd           m0, m6
    paddd           m3, m9
    paddd           m1, m7
    paddd           m4, m10

    ; bottom line y, b/rnd portion only
    punpcklwd       m6, m8,  [pw_16384]
    punpckhwd       m7, m8,  [pw_16384]
    punpcklwd       m9, m11, [pw_16384]
    punpckhwd      m10, m11, [pw_16384]

    pmaddwd        m12, [rsp+0*mmsize]
    pmaddwd        m13, [rsp+0*mmsize]
    pmaddwd        m14, [rsp+0*mmsize]
    pmaddwd        m15, [rsp+0*mmsize]
    pmaddwd         m6, [rsp+1*mmsize]
    pmaddwd         m7, [rsp+1*mmsize]
    pmaddwd         m9, [rsp+1*mmsize]
    pmaddwd        m10, [rsp+1*mmsize]
    paddd          m12, m6
    paddd          m13, m7
    paddd          m14, m9
    paddd          m15, m10
    psrad          m12, %%sh
    psrad          m13, %%sh
    psrad          m14, %%sh
    psrad          m15, %%sh
    packssdw       m12, m13
    packssdw       m14, m15
    lea           tmpq, [yq+ysq]
%if %1 == 8
    packuswb       m12, m14
    movu   [tmpq+xq*2], m12
%else
    pxor           m15, m15
    CLIPW          m12, m15, [pw_ %+ %%maxval]
    CLIPW          m14, m15, [pw_ %+ %%maxval]
    movu   [tmpq+xq*4], m12
    movu [tmpq+xq*4+mmsize], m14
%endif

    ; complete subsampling of r/g/b pixels for u/v
    pmaddwd         m8, [pw_1]
    pmaddwd        m11, [pw_1]
    paddd           m2, m8
    paddd           m5, m11
    paddd           m0, [pd_2]
    paddd           m1, [pd_2]
    paddd           m2, [pd_2]
    paddd           m3, [pd_2]
    paddd           m4, [pd_2]
    paddd           m5, [pd_2]
    psrad           m0, 2
    psrad           m1, 2
    psrad           m2, 2
    psrad           m3, 2
    psrad           m4, 2
    psrad           m5, 2
%else ; %3 != 1
    paddd           m0, [pd_1]
    paddd           m1, [pd_1]
    paddd           m2, [pd_1]
    paddd           m3, [pd_1]
    paddd           m4, [pd_1]
    paddd           m5, [pd_1]
    psrad           m0, 1
    psrad           m1, 1
    psrad           m2, 1
    psrad           m3, 1
    psrad           m4, 1
    psrad           m5, 1
%endif ; %3 ==/!= 1
    packssdw        m0, m3
    packssdw        m1, m4
    packssdw        m2, m5
%endif ; %2 == 1

    ; convert u/v pixels
    SBUTTERFLY   wd, 0, 1, 6
    punpckhwd       m6, m2, [pw_16384]
    punpcklwd       m2, [pw_16384]

    pmaddwd         m7, m0, [rsp+2*mmsize]
    pmaddwd         m8, m1, [rsp+2*mmsize]
    pmaddwd         m9, m2, [rsp+3*mmsize]
    pmaddwd        m10, m6, [rsp+3*mmsize]
    pmaddwd         m0, [rsp+4*mmsize]
    pmaddwd         m1, [rsp+4*mmsize]
    pmaddwd         m2, [rsp+5*mmsize]
    pmaddwd         m6, [rsp+5*mmsize]
    paddd           m7, m9
    paddd           m8, m10
    paddd           m0, m2
    paddd           m1, m6
    psrad           m7, %%sh
    psrad           m8, %%sh
    psrad           m0, %%sh
    psrad           m1, %%sh
    packssdw        m7, m8
    packssdw        m0, m1
%if %2 == 1
%if %1 == 8
    packuswb        m7, m0
    movh       [uq+xq], m7
    movhps     [vq+xq], m7
%else
    CLIPW           m7, m15, [pw_ %+ %%maxval]
    CLIPW           m0, m15, [pw_ %+ %%maxval]
    movu     [uq+xq*2], m7
    movu     [vq+xq*2], m0
%endif
%else ; %2 != 1
    ; second set of u/v pixels
    SBUTTERFLY   wd, 3, 4, 6
    punpckhwd       m6, m5, [pw_16384]
    punpcklwd       m5, [pw_16384]

    pmaddwd         m8, m3, [rsp+2*mmsize]
    pmaddwd         m9, m4, [rsp+2*mmsize]
    pmaddwd        m10, m5, [rsp+3*mmsize]
    pmaddwd        m11, m6, [rsp+3*mmsize]
    pmaddwd         m3, [rsp+4*mmsize]
    pmaddwd         m4, [rsp+4*mmsize]
    pmaddwd         m5, [rsp+5*mmsize]
    pmaddwd         m6, [rsp+5*mmsize]
    paddd           m8, m10
    paddd           m9, m11
    paddd           m3, m5
    paddd           m4, m6
    psrad           m8, %%sh
    psrad           m9, %%sh
    psrad           m3, %%sh
    psrad           m4, %%sh
    packssdw        m8, m9
    packssdw        m3, m4

%if %1 == 8
    packuswb        m7, m8
    packuswb        m0, m3
    movu       [uq+xq], m7
    movu       [vq+xq], m0
%else
    CLIPW           m7, m15, [pw_ %+ %%maxval]
    CLIPW           m0, m15, [pw_ %+ %%maxval]
    CLIPW           m8, m15, [pw_ %+ %%maxval]
    CLIPW           m3, m15, [pw_ %+ %%maxval]
    movu     [uq+xq*2], m7
    movu [uq+xq*2+mmsize], m8
    movu     [vq+xq*2], m0
    movu [vq+xq*2+mmsize], m3
%endif
%endif ; %2 ==/!= 1

    add             xq, mmsize >> %2
    cmp             xd, wwd
    jl .loop_h

%if %3 == 0
    add             yq, ysq
%else ; %3 != 0
    lea             yq, [yq+ysq*2]
%endif ; %3 ==/!= 0
    add             uq, usq
    add             vq, vsq
    lea             rq, [rq+rgbsq*(2<<%3)]
    lea             gq, [gq+rgbsq*(2<<%3)]
    lea             bq, [bq+rgbsq*(2<<%3)]
    dec             hd
    jg .loop_v

    RET
%endmacro

%macro RGB2YUV_FNS 2
RGB2YUV_FN  8, %1, %2
RGB2YUV_FN 10, %1, %2
RGB2YUV_FN 12, %1, %2
%endmacro

INIT_XMM sse2
RGB2YUV_FNS 0, 0
RGB2YUV_FNS 1, 0
RGB2YUV_FNS 1, 1

; void ff_multiply3x3_sse2(int16_t *data[3], ptrdiff_t stride,
;                          int w, int h, const int16_t coeff[3][3][8])
INIT_XMM sse2
cglobal multiply3x3, 5, 7, 16, data, stride, ww, h, c
    movh            m0, [cq+  0]
    movh            m1, [cq+ 32]
    movh            m2, [cq+ 48]
    movh            m3, [cq+ 80]
    movh            m4, [cq+ 96]
    movh            m5, [cq+128]
    punpcklwd       m0, [cq+ 16]
    punpcklwd       m1, [pw_8192]
    punpcklwd       m2, [cq+ 64]
    punpcklwd       m3, [pw_8192]
    punpcklwd       m4, [cq+112]
    punpcklwd       m5, [pw_8192]

    DEFINE_ARGS data0, stride, ww, h, data1, data2, x
    shl        strideq, 1
    mov         data1q, [data0q+gprsize*1]
    mov         data2q, [data0q+gprsize*2]
    mov         data0q, [data0q+gprsize*0]

.loop_v:
    xor             xd, xd

.loop_h:
    mova            m6, [data0q+xq*2]
    mova            m7, [data1q+xq*2]
    mova            m8, [data2q+xq*2]
    SBUTTERFLY   wd, 6, 7, 9
    punpckhwd       m9, m8, [pw_1]
    punpcklwd       m8, [pw_1]

    pmaddwd        m10, m6, m0
    pmaddwd        m11, m7, m0
    pmaddwd        m12, m8, m1
    pmaddwd        m13, m9, m1
    paddd          m10, m12
    paddd          m11, m13
    psrad          m10, 14
    psrad          m11, 14

    pmaddwd        m12, m6, m2
    pmaddwd        m13, m7, m2
    pmaddwd        m14, m8, m3
    pmaddwd        m15, m9, m3
    paddd          m12, m14
    paddd          m13, m15
    psrad          m12, 14
    psrad          m13, 14

    pmaddwd         m6, m4
    pmaddwd         m7, m4
    pmaddwd         m8, m5
    pmaddwd         m9, m5
    paddd           m6, m8
    paddd           m7, m9
    psrad           m6, 14
    psrad           m7, 14

    packssdw       m10, m11
    packssdw       m12, m13
    packssdw        m6, m7

    mova [data0q+xq*2], m10
    mova [data1q+xq*2], m12
    mova [data2q+xq*2], m6

    add             xd, mmsize / 2
    cmp             xd, wwd
    jl .loop_h

    add         data0q, strideq
    add         data1q, strideq
    add         data2q, strideq
    dec             hd
    jg .loop_v

    RET
%endif
