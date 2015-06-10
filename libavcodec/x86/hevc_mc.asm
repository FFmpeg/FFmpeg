; /*
; * Provide SSE luma and chroma mc functions for HEVC decoding
; * Copyright (c) 2013 Pierre-Edouard LEPERE
; *
; * This file is part of FFmpeg.
; *
; * FFmpeg is free software; you can redistribute it and/or
; * modify it under the terms of the GNU Lesser General Public
; * License as published by the Free Software Foundation; either
; * version 2.1 of the License, or (at your option) any later version.
; *
; * FFmpeg is distributed in the hope that it will be useful,
; * but WITHOUT ANY WARRANTY; without even the implied warranty of
; * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; * Lesser General Public License for more details.
; *
; * You should have received a copy of the GNU Lesser General Public
; * License along with FFmpeg; if not, write to the Free Software
; * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
; */
%include "libavutil/x86/x86util.asm"

SECTION_RODATA 32
cextern pw_255
cextern pw_512
cextern pw_2048
cextern pw_8192
cextern pw_1023
cextern pw_1024
cextern pw_4096
%define pw_8 pw_512
%define pw_10 pw_2048
%define pw_12 pw_8192
%define pw_bi_10 pw_1024
%define pw_bi_12 pw_4096
%define max_pixels_8 pw_255
%define max_pixels_10 pw_1023
pw_bi_8:                times 16 dw  (1 <<  8)
max_pixels_12:          times 16 dw ((1 << 12)-1)
cextern pd_1
cextern pb_0

SECTION_TEXT 32
%macro EPEL_TABLE 4
hevc_epel_filters_%4_%1 times %2 d%3 -2, 58
                        times %2 d%3 10, -2
                        times %2 d%3 -4, 54
                        times %2 d%3 16, -2
                        times %2 d%3 -6, 46
                        times %2 d%3 28, -4
                        times %2 d%3 -4, 36
                        times %2 d%3 36, -4
                        times %2 d%3 -4, 28
                        times %2 d%3 46, -6
                        times %2 d%3 -2, 16
                        times %2 d%3 54, -4
                        times %2 d%3 -2, 10
                        times %2 d%3 58, -2
%endmacro


EPEL_TABLE  8,16, b, avx2
EPEL_TABLE 10, 8, w, avx2

EPEL_TABLE  8, 8, b, sse4
EPEL_TABLE 10, 4, w, sse4
EPEL_TABLE 12, 4, w, sse4

%macro QPEL_TABLE 4
hevc_qpel_filters_%4_%1 times %2 d%3  -1,  4
                        times %2 d%3 -10, 58
                        times %2 d%3  17, -5
                        times %2 d%3   1,  0
                        times %2 d%3  -1,  4
                        times %2 d%3 -11, 40
                        times %2 d%3  40,-11
                        times %2 d%3   4, -1
                        times %2 d%3   0,  1
                        times %2 d%3  -5, 17
                        times %2 d%3  58,-10
                        times %2 d%3   4, -1
%endmacro

QPEL_TABLE  8, 8, b, sse4
QPEL_TABLE 10, 4, w, sse4
QPEL_TABLE 12, 4, w, sse4

QPEL_TABLE  8,16, b, avx2
QPEL_TABLE 10, 8, w, avx2

%define MAX_PB_SIZE  64

%define hevc_qpel_filters_sse4_14 hevc_qpel_filters_sse4_10

%define hevc_qpel_filters_avx2_14 hevc_qpel_filters_avx2_10

%if ARCH_X86_64

%macro SIMPLE_BILOAD 4   ;width, tab, r1, r2
%if %1 <= 4
    movq              %3, [%2]                                              ; load data from source2
%elif %1 <= 8
    movdqa            %3, [%2]                                              ; load data from source2
%elif %1 <= 12
%if cpuflag(avx2)
    mova              %3, [%2]
%else
    movdqa            %3, [%2]                                              ; load data from source2
    movq              %4, [%2+16]                                           ; load data from source2
%endif ;avx
%elif %1 <= 16
%if cpuflag(avx2)
    mova              %3, [%2]
%else
    movdqa            %3, [%2]                                              ; load data from source2
    movdqa            %4, [%2+16]                                           ; load data from source2
%endif ; avx
%else ; %1 = 32
    mova              %3, [%2]
    mova              %4, [%2+32]
%endif
%endmacro

%macro SIMPLE_LOAD 4    ;width, bitd, tab, r1
%if %1 == 2 || (%2 == 8 && %1 <= 4)
    movd              %4, [%3]                                               ; load data from source
%elif %1 == 4 || (%2 == 8 && %1 <= 8)
    movq              %4, [%3]                                               ; load data from source
%elif notcpuflag(avx)
    movu              %4, [%3]                                               ; load data from source
%elif %1 <= 8 || (%2 == 8 && %1 <= 16)
    movdqu           %4, [%3]
%else
    movu              %4, [%3]
%endif
%endmacro


%macro EPEL_FILTER 5 ; bit depth, filter index, xmma, xmmb, gprtmp
%if cpuflag(avx2)
%assign %%offset 32
%ifdef PIC
    lea              %5q, [hevc_epel_filters_avx2_%1]
    %define FILTER %5q
%else
    %define FILTER hevc_epel_filters_avx2_%1
%endif
%else
%assign %%offset 16
%ifdef PIC
    lea              %5q, [hevc_epel_filters_sse4_%1]
    %define FILTER %5q
%else
    %define FILTER hevc_epel_filters_sse4_%1
%endif
%endif ;cpuflag(avx2)
    sub              %2q, 1
%if cpuflag(avx2)
    shl              %2q, 6                      ; multiply by 64
  %else
    shl              %2q, 5                      ; multiply by 32
%endif
    mova           %3, [FILTER + %2q]        ; get 2 first values of filters
    mova           %4, [FILTER + %2q+%%offset]     ; get 2 last values of filters
%endmacro

%macro EPEL_HV_FILTER 1
%if cpuflag(avx2)
%assign %%offset 32
%assign %%shift  6
%define %%table  hevc_epel_filters_avx2_%1
%else
%assign %%offset 16
%assign %%shift  5
%define %%table  hevc_epel_filters_sse4_%1
%endif

%ifdef PIC
    lea           r3srcq, [%%table]
    %define FILTER r3srcq
%else
    %define FILTER %%table
%endif
    sub              mxq, 1
    sub              myq, 1
    shl              mxq, %%shift                ; multiply by 32
    shl              myq, %%shift                ; multiply by 32
    mova             m14, [FILTER + mxq]        ; get 2 first values of filters
    mova             m15, [FILTER + mxq+%%offset]     ; get 2 last values of filters

%if cpuflag(avx2)
%define %%table  hevc_epel_filters_avx2_10
%else
%define %%table  hevc_epel_filters_sse4_10
%endif
%ifdef PIC
    lea           r3srcq, [%%table]
    %define FILTER r3srcq
%else
    %define FILTER %%table
%endif
    mova             m12, [FILTER + myq]        ; get 2 first values of filters
    mova             m13, [FILTER + myq+%%offset]     ; get 2 last values of filters
    lea           r3srcq, [srcstrideq*3]
%endmacro

%macro QPEL_FILTER 2

%if cpuflag(avx2)
%assign %%offset 32
%assign %%shift  7
%define %%table  hevc_qpel_filters_avx2_%1
%else
%assign %%offset 16
%assign %%shift  6
%define %%table  hevc_qpel_filters_sse4_%1
%endif

%ifdef PIC
    lea         rfilterq, [%%table]
%else
    %define rfilterq %%table
%endif
    sub              %2q, 1
    shl              %2q, %%shift                        ; multiply by 32
    mova             m12, [rfilterq + %2q]               ; get 4 first values of filters
    mova             m13, [rfilterq + %2q +   %%offset]  ; get 4 first values of filters
    mova             m14, [rfilterq + %2q + 2*%%offset]  ; get 4 first values of filters
    mova             m15, [rfilterq + %2q + 3*%%offset]  ; get 4 first values of filters
%endmacro

%macro EPEL_LOAD 4
%if (%1 == 8 && %4 <= 4)
%define %%load movd
%elif (%1 == 8 && %4 <= 8) || (%1 > 8 && %4 <= 4)
%define %%load movq
%else
%define %%load movdqu
%endif

    %%load            m0, [%2q ]
%ifnum %3
    %%load            m1, [%2q+  %3]
    %%load            m2, [%2q+2*%3]
    %%load            m3, [%2q+3*%3]
%else
    %%load            m1, [%2q+  %3q]
    %%load            m2, [%2q+2*%3q]
    %%load            m3, [%2q+r3srcq]
%endif
%if %1 == 8
%if %4 > 8
    SBUTTERFLY        bw, 0, 1, 7
    SBUTTERFLY        bw, 2, 3, 7
%else
    punpcklbw         m0, m1
    punpcklbw         m2, m3
%endif
%else
%if %4 > 4
    SBUTTERFLY        wd, 0, 1, 7
    SBUTTERFLY        wd, 2, 3, 7
%else
    punpcklwd         m0, m1
    punpcklwd         m2, m3
%endif
%endif
%endmacro


%macro QPEL_H_LOAD 4
%assign %%stride (%1+7)/8
%if %1 == 8
%if %3 <= 4
%define %%load movd
%elif %3 == 8
%define %%load movq
%else
%define %%load movu
%endif
%else
%if %3 == 2
%define %%load movd
%elif %3 == 4
%define %%load movq
%else
%define %%load movu
%endif
%endif
    %%load            m0, [%2-3*%%stride]        ;load data from source
    %%load            m1, [%2-2*%%stride]
    %%load            m2, [%2-%%stride  ]
    %%load            m3, [%2           ]
    %%load            m4, [%2+%%stride  ]
    %%load            m5, [%2+2*%%stride]
    %%load            m6, [%2+3*%%stride]
    %%load            m7, [%2+4*%%stride]

%if %1 == 8
%if %3 > 8
    SBUTTERFLY        wd, 0, 1, %4
    SBUTTERFLY        wd, 2, 3, %4
    SBUTTERFLY        wd, 4, 5, %4
    SBUTTERFLY        wd, 6, 7, %4
%else
    punpcklbw         m0, m1
    punpcklbw         m2, m3
    punpcklbw         m4, m5
    punpcklbw         m6, m7
%endif
%else
%if %3 > 4
    SBUTTERFLY        dq, 0, 1, %4
    SBUTTERFLY        dq, 2, 3, %4
    SBUTTERFLY        dq, 4, 5, %4
    SBUTTERFLY        dq, 6, 7, %4
%else
    punpcklwd         m0, m1
    punpcklwd         m2, m3
    punpcklwd         m4, m5
    punpcklwd         m6, m7
%endif
%endif
%endmacro

%macro QPEL_V_LOAD 5
    lea              %5q, [%2]
    sub              %5q, r3srcq
    movu              m0, [%5q            ]      ;load x- 3*srcstride
    movu              m1, [%5q+   %3q     ]      ;load x- 2*srcstride
    movu              m2, [%5q+ 2*%3q     ]      ;load x-srcstride
    movu              m3, [%2       ]      ;load x
    movu              m4, [%2+   %3q]      ;load x+stride
    movu              m5, [%2+ 2*%3q]      ;load x+2*stride
    movu              m6, [%2+r3srcq]      ;load x+3*stride
    movu              m7, [%2+ 4*%3q]      ;load x+4*stride
%if %1 == 8
%if %4 > 8
    SBUTTERFLY        bw, 0, 1, 8
    SBUTTERFLY        bw, 2, 3, 8
    SBUTTERFLY        bw, 4, 5, 8
    SBUTTERFLY        bw, 6, 7, 8
%else
    punpcklbw         m0, m1
    punpcklbw         m2, m3
    punpcklbw         m4, m5
    punpcklbw         m6, m7
%endif
%else
%if %4 > 4
    SBUTTERFLY        wd, 0, 1, 8
    SBUTTERFLY        wd, 2, 3, 8
    SBUTTERFLY        wd, 4, 5, 8
    SBUTTERFLY        wd, 6, 7, 8
%else
    punpcklwd         m0, m1
    punpcklwd         m2, m3
    punpcklwd         m4, m5
    punpcklwd         m6, m7
%endif
%endif
%endmacro

%macro PEL_12STORE2 3
    movd           [%1], %2
%endmacro
%macro PEL_12STORE4 3
    movq           [%1], %2
%endmacro
%macro PEL_12STORE6 3
    movq           [%1], %2
    psrldq            %2, 8
    movd         [%1+8], %2
%endmacro
%macro PEL_12STORE8 3
    movdqa         [%1], %2
%endmacro
%macro PEL_12STORE12 3
    movdqa         [%1], %2
    movq        [%1+16], %3
%endmacro
%macro PEL_12STORE16 3
    PEL_12STORE8      %1, %2, %3
    movdqa       [%1+16], %3
%endmacro

%macro PEL_10STORE2 3
    movd           [%1], %2
%endmacro
%macro PEL_10STORE4 3
    movq           [%1], %2
%endmacro
%macro PEL_10STORE6 3
    movq           [%1], %2
    psrldq            %2, 8
    movd         [%1+8], %2
%endmacro
%macro PEL_10STORE8 3
    movdqa         [%1], %2
%endmacro
%macro PEL_10STORE12 3
    movdqa         [%1], %2
    movq        [%1+16], %3
%endmacro
%macro PEL_10STORE16 3
%if cpuflag(avx2)
    movu            [%1], %2
%else
    PEL_10STORE8      %1, %2, %3
    movdqa       [%1+16], %3
%endif
%endmacro

%macro PEL_10STORE32 3
    PEL_10STORE16     %1, %2, %3
    movu         [%1+32], %3
%endmacro

%macro PEL_8STORE2 3
    pextrw          [%1], %2, 0
%endmacro
%macro PEL_8STORE4 3
    movd            [%1], %2
%endmacro
%macro PEL_8STORE6 3
    movd            [%1], %2
    pextrw        [%1+4], %2, 2
%endmacro
%macro PEL_8STORE8 3
    movq           [%1], %2
%endmacro
%macro PEL_8STORE12 3
    movq            [%1], %2
    psrldq            %2, 8
    movd          [%1+8], %2
%endmacro
%macro PEL_8STORE16 3
%if cpuflag(avx2)
    movdqu        [%1], %2
%else
    mova          [%1], %2
%endif ; avx
%endmacro
%macro PEL_8STORE32 3
    movu          [%1], %2
%endmacro

%macro LOOP_END 3
    add              %1q, 2*MAX_PB_SIZE          ; dst += dststride
    add              %2q, %3q                    ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
%endmacro


%macro MC_PIXEL_COMPUTE 2-3 ;width, bitdepth
%if %2 == 8
%if cpuflag(avx2) && %0 ==3
%if %1 > 16
    vextracti128 xm1, m0, 1
    pmovzxbw      m1, xm1
    psllw         m1, 14-%2
%endif
    pmovzxbw      m0, xm0
%else ; not avx
%if %1 > 8
    punpckhbw     m1, m0, m2
    psllw         m1, 14-%2
%endif
    punpcklbw     m0, m2
%endif
%endif ;avx
    psllw         m0, 14-%2
%endmacro

%macro EPEL_COMPUTE 4-8 ; bitdepth, width, filter1, filter2, HV/m0, m2, m1, m3
%if %0 == 8
%define %%reg0 %5
%define %%reg2 %6
%define %%reg1 %7
%define %%reg3 %8
%else
%define %%reg0 m0
%define %%reg2 m2
%define %%reg1 m1
%define %%reg3 m3
%endif
%if %1 == 8
%if cpuflag(avx2) && (%0 == 5)
%if %2 > 16
    vperm2i128    m10, m0, m1, q0301
%endif
    vinserti128    m0, m0, xm1, 1
    mova           m1, m10
%if %2 > 16
    vperm2i128    m10, m2, m3, q0301
%endif
    vinserti128    m2, m2, xm3, 1
    mova           m3, m10
%endif
    pmaddubsw      %%reg0, %3   ;x1*c1+x2*c2
    pmaddubsw      %%reg2, %4   ;x3*c3+x4*c4
    paddw          %%reg0, %%reg2
%if %2 > 8
    pmaddubsw      %%reg1, %3
    pmaddubsw      %%reg3, %4
    paddw          %%reg1, %%reg3
%endif
%else
    pmaddwd        %%reg0, %3
    pmaddwd        %%reg2, %4
    paddd          %%reg0, %%reg2
%if %2 > 4
    pmaddwd        %%reg1, %3
    pmaddwd        %%reg3, %4
    paddd          %%reg1, %%reg3
%if %1 != 8
    psrad          %%reg1, %1-8
%endif
%endif
%if %1 != 8
    psrad          %%reg0, %1-8
%endif
    packssdw       %%reg0, %%reg1
%endif
%endmacro

%macro QPEL_HV_COMPUTE 4     ; width, bitdepth, filter idx

%if cpuflag(avx2)
%assign %%offset 32
%define %%table  hevc_qpel_filters_avx2_%2
%else
%assign %%offset 16
%define %%table  hevc_qpel_filters_sse4_%2
%endif

%ifdef PIC
    lea         rfilterq, [%%table]
%else
    %define rfilterq %%table
%endif

%if %2 == 8
    pmaddubsw         m0, [rfilterq + %3q*8   ]   ;x1*c1+x2*c2
    pmaddubsw         m2, [rfilterq + %3q*8+%%offset]   ;x3*c3+x4*c4
    pmaddubsw         m4, [rfilterq + %3q*8+2*%%offset]   ;x5*c5+x6*c6
    pmaddubsw         m6, [rfilterq + %3q*8+3*%%offset]   ;x7*c7+x8*c8
    paddw             m0, m2
    paddw             m4, m6
    paddw             m0, m4
%else
    pmaddwd           m0, [rfilterq + %3q*8   ]
    pmaddwd           m2, [rfilterq + %3q*8+%%offset]
    pmaddwd           m4, [rfilterq + %3q*8+2*%%offset]
    pmaddwd           m6, [rfilterq + %3q*8+3*%%offset]
    paddd             m0, m2
    paddd             m4, m6
    paddd             m0, m4
%if %2 != 8
    psrad             m0, %2-8
%endif
%if %1 > 4
    pmaddwd           m1, [rfilterq + %3q*8   ]
    pmaddwd           m3, [rfilterq + %3q*8+%%offset]
    pmaddwd           m5, [rfilterq + %3q*8+2*%%offset]
    pmaddwd           m7, [rfilterq + %3q*8+3*%%offset]
    paddd             m1, m3
    paddd             m5, m7
    paddd             m1, m5
%if %2 != 8
    psrad             m1, %2-8
%endif
%endif
    p%4               m0, m1
%endif
%endmacro

%macro QPEL_COMPUTE 2-3     ; width, bitdepth
%if %2 == 8
%if cpuflag(avx2) && (%0 == 3)

    vperm2i128 m10, m0,  m1, q0301
    vinserti128 m0, m0, xm1, 1
    SWAP 1, 10

    vperm2i128 m10, m2,  m3, q0301
    vinserti128 m2, m2, xm3, 1
    SWAP 3, 10


    vperm2i128 m10, m4,  m5, q0301
    vinserti128 m4, m4, xm5, 1
    SWAP 5, 10

    vperm2i128 m10, m6,  m7, q0301
    vinserti128 m6, m6, xm7, 1
    SWAP 7, 10
%endif

    pmaddubsw         m0, m12   ;x1*c1+x2*c2
    pmaddubsw         m2, m13   ;x3*c3+x4*c4
    pmaddubsw         m4, m14   ;x5*c5+x6*c6
    pmaddubsw         m6, m15   ;x7*c7+x8*c8
    paddw             m0, m2
    paddw             m4, m6
    paddw             m0, m4
%if %1 > 8
    pmaddubsw         m1, m12
    pmaddubsw         m3, m13
    pmaddubsw         m5, m14
    pmaddubsw         m7, m15
    paddw             m1, m3
    paddw             m5, m7
    paddw             m1, m5
%endif
%else
    pmaddwd           m0, m12
    pmaddwd           m2, m13
    pmaddwd           m4, m14
    pmaddwd           m6, m15
    paddd             m0, m2
    paddd             m4, m6
    paddd             m0, m4
%if %2 != 8
    psrad             m0, %2-8
%endif
%if %1 > 4
    pmaddwd           m1, m12
    pmaddwd           m3, m13
    pmaddwd           m5, m14
    pmaddwd           m7, m15
    paddd             m1, m3
    paddd             m5, m7
    paddd             m1, m5
%if %2 != 8
    psrad             m1, %2-8
%endif
%endif
%endif
%endmacro

%macro BI_COMPUTE 7-8     ; width, bitd, src1l, src1h, scr2l, scr2h, pw
    paddsw            %3, %5
%if %1 > 8
    paddsw            %4, %6
%endif
    UNI_COMPUTE       %1, %2, %3, %4, %7
%if %0 == 8 && cpuflag(avx2) && (%2 == 8)
    vpermq            %3, %3, 216
    vpermq            %4, %4, 216
%endif
%endmacro

%macro UNI_COMPUTE 5
    pmulhrsw          %3, %5
%if %1 > 8 || (%2 > 8 && %1 > 4)
    pmulhrsw          %4, %5
%endif
%if %2 == 8
    packuswb          %3, %4
%else
    CLIPW             %3, [pb_0], [max_pixels_%2]
%if (%1 > 8 && notcpuflag(avx)) || %1 > 16
    CLIPW             %4, [pb_0], [max_pixels_%2]
%endif
%endif
%endmacro


; ******************************
; void put_hevc_mc_pixels(int16_t *dst, ptrdiff_t dststride,
;                         uint8_t *_src, ptrdiff_t _srcstride,
;                         int height, int mx, int my)
; ******************************

%macro HEVC_PUT_HEVC_PEL_PIXELS 2
HEVC_PEL_PIXELS     %1, %2
HEVC_UNI_PEL_PIXELS %1, %2
HEVC_BI_PEL_PIXELS  %1, %2
%endmacro

%macro HEVC_PEL_PIXELS 2
cglobal hevc_put_hevc_pel_pixels%1_%2, 4, 4, 3, dst, src, srcstride,height
    pxor               m2, m2
.loop
    SIMPLE_LOAD       %1, %2, srcq, m0
    MC_PIXEL_COMPUTE  %1, %2, 1
    PEL_10STORE%1     dstq, m0, m1
    LOOP_END         dst, src, srcstride
    RET
 %endmacro

%macro HEVC_UNI_PEL_PIXELS 2
cglobal hevc_put_hevc_uni_pel_pixels%1_%2, 5, 5, 2, dst, dststride, src, srcstride,height
.loop
    SIMPLE_LOAD       %1, %2, srcq, m0
    PEL_%2STORE%1   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET
%endmacro

%macro HEVC_BI_PEL_PIXELS 2
cglobal hevc_put_hevc_bi_pel_pixels%1_%2, 6, 6, 6, dst, dststride, src, srcstride, src2, height
    pxor              m2, m2
    movdqa            m5, [pw_bi_%2]
.loop
    SIMPLE_LOAD       %1, %2, srcq, m0
    SIMPLE_BILOAD     %1, src2q, m3, m4
    MC_PIXEL_COMPUTE  %1, %2, 1
    BI_COMPUTE        %1, %2, m0, m1, m3, m4, m5, 1
    PEL_%2STORE%1   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    add            src2q, 2*MAX_PB_SIZE          ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET
%endmacro


; ******************************
; void put_hevc_epel_hX(int16_t *dst, ptrdiff_t dststride,
;                       uint8_t *_src, ptrdiff_t _srcstride,
;                       int height, int mx, int my, int width);
; ******************************


%macro HEVC_PUT_HEVC_EPEL 2
%if cpuflag(avx2)
%define XMM_REGS  11
%else
%define XMM_REGS  8
%endif

cglobal hevc_put_hevc_epel_h%1_%2, 5, 6, XMM_REGS, dst, src, srcstride, height, mx, rfilter
%assign %%stride ((%2 + 7)/8)
    EPEL_FILTER       %2, mx, m4, m5, rfilter
.loop
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m4, m5, 1
    PEL_10STORE%1      dstq, m0, m1
    LOOP_END         dst, src, srcstride
    RET

cglobal hevc_put_hevc_uni_epel_h%1_%2, 6, 7, XMM_REGS, dst, dststride, src, srcstride, height, mx, rfilter
%assign %%stride ((%2 + 7)/8)
    movdqa            m6, [pw_%2]
    EPEL_FILTER       %2, mx, m4, m5, rfilter
.loop
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m4, m5
    UNI_COMPUTE       %1, %2, m0, m1, m6
    PEL_%2STORE%1   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET

cglobal hevc_put_hevc_bi_epel_h%1_%2, 7, 8, XMM_REGS, dst, dststride, src, srcstride, src2, height, mx, rfilter
    movdqa            m6, [pw_bi_%2]
    EPEL_FILTER       %2, mx, m4, m5, rfilter
.loop
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m4, m5, 1
    SIMPLE_BILOAD     %1, src2q, m2, m3
    BI_COMPUTE        %1, %2, m0, m1, m2, m3, m6, 1
    PEL_%2STORE%1   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    add            src2q, 2*MAX_PB_SIZE          ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET

; ******************************
; void put_hevc_epel_v(int16_t *dst, ptrdiff_t dststride,
;                      uint8_t *_src, ptrdiff_t _srcstride,
;                      int height, int mx, int my, int width)
; ******************************

cglobal hevc_put_hevc_epel_v%1_%2, 4, 6, XMM_REGS, dst, src, srcstride, height, r3src, my
    movifnidn        myd, mym
    sub             srcq, srcstrideq
    EPEL_FILTER       %2, my, m4, m5, r3src
    lea           r3srcq, [srcstrideq*3]
.loop
    EPEL_LOAD         %2, srcq, srcstride, %1
    EPEL_COMPUTE      %2, %1, m4, m5, 1
    PEL_10STORE%1     dstq, m0, m1
    LOOP_END          dst, src, srcstride
    RET

cglobal hevc_put_hevc_uni_epel_v%1_%2, 5, 7, XMM_REGS, dst, dststride, src, srcstride, height, r3src, my
    movifnidn        myd, mym
    movdqa            m6, [pw_%2]
    sub             srcq, srcstrideq
    EPEL_FILTER       %2, my, m4, m5, r3src
    lea           r3srcq, [srcstrideq*3]
.loop
    EPEL_LOAD         %2, srcq, srcstride, %1
    EPEL_COMPUTE      %2, %1, m4, m5
    UNI_COMPUTE       %1, %2, m0, m1, m6
    PEL_%2STORE%1   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET


cglobal hevc_put_hevc_bi_epel_v%1_%2, 6, 8, XMM_REGS, dst, dststride, src, srcstride, src2, height, r3src, my
    movifnidn        myd, mym
    movdqa            m6, [pw_bi_%2]
    sub             srcq, srcstrideq
    EPEL_FILTER       %2, my, m4, m5, r3src
    lea           r3srcq, [srcstrideq*3]
.loop
    EPEL_LOAD         %2, srcq, srcstride, %1
    EPEL_COMPUTE      %2, %1, m4, m5, 1
    SIMPLE_BILOAD     %1, src2q, m2, m3
    BI_COMPUTE        %1, %2, m0, m1, m2, m3, m6, 1
    PEL_%2STORE%1   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    add            src2q, 2*MAX_PB_SIZE          ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET
%endmacro


; ******************************
; void put_hevc_epel_hv(int16_t *dst, ptrdiff_t dststride,
;                       uint8_t *_src, ptrdiff_t _srcstride,
;                       int height, int mx, int my, int width)
; ******************************

%macro HEVC_PUT_HEVC_EPEL_HV 2
cglobal hevc_put_hevc_epel_hv%1_%2, 6, 7, 16 , dst, src, srcstride, height, mx, my, r3src
%assign %%stride ((%2 + 7)/8)
    sub             srcq, srcstrideq
    EPEL_HV_FILTER    %2
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m14, m15
%if (%1 > 8 && (%2 == 8))
    SWAP              m8, m1
%endif
    SWAP              m4, m0
    add             srcq, srcstrideq
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m14, m15
%if (%1 > 8 && (%2 == 8))
    SWAP              m9, m1
%endif
    SWAP              m5, m0
    add             srcq, srcstrideq
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m14, m15
%if (%1 > 8 && (%2 == 8))
    SWAP             m10, m1
%endif
    SWAP              m6, m0
    add             srcq, srcstrideq
.loop
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m14, m15
%if (%1 > 8 && (%2 == 8))
    SWAP             m11, m1
%endif
    SWAP              m7, m0
    punpcklwd         m0, m4, m5
    punpcklwd         m2, m6, m7
%if %1 > 4
    punpckhwd         m1, m4, m5
    punpckhwd         m3, m6, m7
%endif
    EPEL_COMPUTE      14, %1, m12, m13
%if (%1 > 8 && (%2 == 8))
    punpcklwd         m4, m8, m9
    punpcklwd         m2, m10, m11
    punpckhwd         m8, m8, m9
    punpckhwd         m3, m10, m11
    EPEL_COMPUTE      14, %1, m12, m13, m4, m2, m8, m3
%if cpuflag(avx2)
    vinserti128       m2, m0, xm4, 1
    vperm2i128        m3, m0, m4, q0301
    PEL_10STORE%1     dstq, m2, m3
%else
    PEL_10STORE%1     dstq, m0, m4
%endif
%else
    PEL_10STORE%1     dstq, m0, m1
%endif
    movdqa            m4, m5
    movdqa            m5, m6
    movdqa            m6, m7
%if (%1 > 8 && (%2 == 8))
    mova              m8, m9
    mova              m9, m10
    mova             m10, m11
%endif
    LOOP_END         dst, src, srcstride
    RET

cglobal hevc_put_hevc_uni_epel_hv%1_%2, 7, 8, 16 , dst, dststride, src, srcstride, height, mx, my, r3src
%assign %%stride ((%2 + 7)/8)
    sub             srcq, srcstrideq
    EPEL_HV_FILTER    %2
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m14, m15
%if (%1 > 8 && (%2 == 8))
    SWAP              m8, m1
%endif
    SWAP              m4, m0
    add             srcq, srcstrideq
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m14, m15
%if (%1 > 8 && (%2 == 8))
    SWAP              m9, m1
%endif
    SWAP              m5, m0
    add             srcq, srcstrideq
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m14, m15
%if (%1 > 8 && (%2 == 8))
    SWAP             m10, m1
%endif
    SWAP              m6, m0
    add             srcq, srcstrideq
.loop
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m14, m15
%if (%1 > 8 && (%2 == 8))
    SWAP             m11, m1
%endif
    mova              m7, m0
    punpcklwd         m0, m4, m5
    punpcklwd         m2, m6, m7
%if %1 > 4
    punpckhwd         m1, m4, m5
    punpckhwd         m3, m6, m7
%endif
    EPEL_COMPUTE      14, %1, m12, m13
%if (%1 > 8 && (%2 == 8))
    punpcklwd         m4, m8, m9
    punpcklwd         m2, m10, m11
    punpckhwd         m8, m8, m9
    punpckhwd         m3, m10, m11
    EPEL_COMPUTE      14, %1, m12, m13, m4, m2, m8, m3
    UNI_COMPUTE       %1, %2, m0, m4, [pw_%2]
%else
    UNI_COMPUTE       %1, %2, m0, m1, [pw_%2]
%endif
    PEL_%2STORE%1   dstq, m0, m1
    mova              m4, m5
    mova              m5, m6
    mova              m6, m7
%if (%1 > 8 && (%2 == 8))
    mova              m8, m9
    mova              m9, m10
    mova             m10, m11
%endif
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET

cglobal hevc_put_hevc_bi_epel_hv%1_%2, 8, 9, 16, dst, dststride, src, srcstride, src2, height, mx, my, r3src
%assign %%stride ((%2 + 7)/8)
    sub             srcq, srcstrideq
    EPEL_HV_FILTER    %2
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m14, m15
%if (%1 > 8 && (%2 == 8))
    SWAP              m8, m1
%endif
    SWAP              m4, m0
    add             srcq, srcstrideq
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m14, m15
%if (%1 > 8 && (%2 == 8))
    SWAP              m9, m1
%endif
    SWAP              m5, m0
    add             srcq, srcstrideq
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m14, m15
%if (%1 > 8 && (%2 == 8))
    SWAP             m10, m1
%endif
    SWAP              m6, m0
    add             srcq, srcstrideq
.loop
    EPEL_LOAD         %2, srcq-%%stride, %%stride, %1
    EPEL_COMPUTE      %2, %1, m14, m15
%if (%1 > 8 && (%2 == 8))
    SWAP             m11, m1
%endif
    SWAP              m7, m0
    punpcklwd         m0, m4, m5
    punpcklwd         m2, m6, m7
%if %1 > 4
    punpckhwd         m1, m4, m5
    punpckhwd         m3, m6, m7
%endif
    EPEL_COMPUTE      14, %1, m12, m13
%if (%1 > 8 && (%2 == 8))
    punpcklwd         m4, m8, m9
    punpcklwd         m2, m10, m11
    punpckhwd         m8, m8, m9
    punpckhwd         m3, m10, m11
    EPEL_COMPUTE      14, %1, m12, m13, m4, m2, m8, m3
    SIMPLE_BILOAD     %1, src2q, m8, m3
%if cpuflag(avx2)
    vinserti128       m1, m8, xm3, 1
    vperm2i128        m2, m8, m3, q0301
    BI_COMPUTE        %1, %2, m0, m4, m1, m2, [pw_bi_%2]
%else
    BI_COMPUTE        %1, %2, m0, m4, m8, m3, [pw_bi_%2]
%endif
%else
    SIMPLE_BILOAD     %1, src2q, m8, m9
    BI_COMPUTE        %1, %2, m0, m1, m8, m9, [pw_bi_%2]
%endif
    PEL_%2STORE%1   dstq, m0, m4
    mova              m4, m5
    mova              m5, m6
    mova              m6, m7
%if (%1 > 8 && (%2 == 8))
    mova              m8, m9
    mova              m9, m10
    mova             m10, m11
%endif
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    add            src2q, 2*MAX_PB_SIZE          ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET
%endmacro

; ******************************
; void put_hevc_qpel_hX_X_X(int16_t *dst, ptrdiff_t dststride,
;                       uint8_t *_src, ptrdiff_t _srcstride,
;                       int height, int mx, int my, int width)
; ******************************

%macro HEVC_PUT_HEVC_QPEL 2
cglobal hevc_put_hevc_qpel_h%1_%2, 5, 6, 16, dst, src, srcstride, height, mx, rfilter
    QPEL_FILTER       %2, mx
.loop
    QPEL_H_LOAD       %2, srcq, %1, 10
    QPEL_COMPUTE      %1, %2, 1
%if %2 > 8
    packssdw          m0, m1
%endif
    PEL_10STORE%1     dstq, m0, m1
    LOOP_END          dst, src, srcstride
    RET

cglobal hevc_put_hevc_uni_qpel_h%1_%2, 6, 7, 16 , dst, dststride, src, srcstride, height, mx, rfilter
    mova              m9, [pw_%2]
    QPEL_FILTER       %2, mx
.loop
    QPEL_H_LOAD       %2, srcq, %1, 10
    QPEL_COMPUTE      %1, %2
%if %2 > 8
    packssdw          m0, m1
%endif
    UNI_COMPUTE       %1, %2, m0, m1, m9
    PEL_%2STORE%1   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET

cglobal hevc_put_hevc_bi_qpel_h%1_%2, 7, 8, 16 , dst, dststride, src, srcstride, src2, height, mx, rfilter
    movdqa            m9, [pw_bi_%2]
    QPEL_FILTER       %2, mx
.loop
    QPEL_H_LOAD       %2, srcq, %1, 10
    QPEL_COMPUTE      %1, %2, 1
%if %2 > 8
    packssdw          m0, m1
%endif
    SIMPLE_BILOAD     %1, src2q, m10, m11
    BI_COMPUTE        %1, %2, m0, m1, m10, m11, m9, 1
    PEL_%2STORE%1   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    add            src2q, 2*MAX_PB_SIZE          ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET


; ******************************
; void put_hevc_qpel_vX_X_X(int16_t *dst, ptrdiff_t dststride,
;                       uint8_t *_src, ptrdiff_t _srcstride,
;                       int height, int mx, int my, int width)
; ******************************

cglobal hevc_put_hevc_qpel_v%1_%2, 4, 8, 16, dst, src, srcstride, height, r3src, my, rfilter
    movifnidn        myd, mym
    lea           r3srcq, [srcstrideq*3]
    QPEL_FILTER       %2, my
.loop
    QPEL_V_LOAD       %2, srcq, srcstride, %1, r7
    QPEL_COMPUTE      %1, %2, 1
%if %2 > 8
    packssdw          m0, m1
%endif
    PEL_10STORE%1     dstq, m0, m1
    LOOP_END         dst, src, srcstride
    RET

cglobal hevc_put_hevc_uni_qpel_v%1_%2, 5, 9, 16, dst, dststride, src, srcstride, height, r3src, my, rfilter
    movifnidn        myd, mym
    movdqa            m9, [pw_%2]
    lea           r3srcq, [srcstrideq*3]
    QPEL_FILTER       %2, my
.loop
    QPEL_V_LOAD       %2, srcq, srcstride, %1, r8
    QPEL_COMPUTE      %1, %2
%if %2 > 8
    packssdw          m0, m1
%endif
    UNI_COMPUTE       %1, %2, m0, m1, m9
    PEL_%2STORE%1   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET

cglobal hevc_put_hevc_bi_qpel_v%1_%2, 6, 10, 16, dst, dststride, src, srcstride, src2, height, r3src, my, rfilter
    movifnidn        myd, mym
    movdqa            m9, [pw_bi_%2]
    lea           r3srcq, [srcstrideq*3]
    QPEL_FILTER       %2, my
.loop
    QPEL_V_LOAD       %2, srcq, srcstride, %1, r9
    QPEL_COMPUTE      %1, %2, 1
%if %2 > 8
    packssdw          m0, m1
%endif
    SIMPLE_BILOAD     %1, src2q, m10, m11
    BI_COMPUTE        %1, %2, m0, m1, m10, m11, m9, 1
    PEL_%2STORE%1   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    add            src2q, 2*MAX_PB_SIZE          ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET
%endmacro


; ******************************
; void put_hevc_qpel_hvX_X(int16_t *dst, ptrdiff_t dststride,
;                       uint8_t *_src, ptrdiff_t _srcstride,
;                       int height, int mx, int my)
; ******************************
%macro HEVC_PUT_HEVC_QPEL_HV 2
cglobal hevc_put_hevc_qpel_hv%1_%2, 6, 8, 16, dst, src, srcstride, height, mx, my, r3src, rfilter
%if cpuflag(avx2)
%assign %%shift  4
%else
%assign %%shift  3
%endif
    sub              mxq, 1
    sub              myq, 1
    shl              mxq, %%shift                ; multiply by 32
    shl              myq, %%shift                ; multiply by 32
    lea           r3srcq, [srcstrideq*3]
    sub             srcq, r3srcq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP              m8, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP              m9, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m10, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m11, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m12, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m13, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m14, m0
    add             srcq, srcstrideq
.loop
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m15, m0
    punpcklwd         m0, m8, m9
    punpcklwd         m2, m10, m11
    punpcklwd         m4, m12, m13
    punpcklwd         m6, m14, m15
%if %1 > 4
    punpckhwd         m1, m8, m9
    punpckhwd         m3, m10, m11
    punpckhwd         m5, m12, m13
    punpckhwd         m7, m14, m15
%endif
    QPEL_HV_COMPUTE   %1, 14, my, ackssdw
    PEL_10STORE%1     dstq, m0, m1
%if %1 <= 4
    movq              m8, m9
    movq              m9, m10
    movq             m10, m11
    movq             m11, m12
    movq             m12, m13
    movq             m13, m14
    movq             m14, m15
%else
    movdqa            m8, m9
    movdqa            m9, m10
    movdqa           m10, m11
    movdqa           m11, m12
    movdqa           m12, m13
    movdqa           m13, m14
    movdqa           m14, m15
%endif
    LOOP_END         dst, src, srcstride
    RET

cglobal hevc_put_hevc_uni_qpel_hv%1_%2, 7, 9, 16 , dst, dststride, src, srcstride, height, mx, my, r3src, rfilter
%if cpuflag(avx2)
%assign %%shift  4
%else
%assign %%shift  3
%endif
    sub              mxq, 1
    sub              myq, 1
    shl              mxq, %%shift                ; multiply by 32
    shl              myq, %%shift                ; multiply by 32
    lea           r3srcq, [srcstrideq*3]
    sub             srcq, r3srcq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP              m8, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP              m9, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m10, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m11, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m12, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m13, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m14, m0
    add             srcq, srcstrideq
.loop
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m15, m0
    punpcklwd         m0, m8, m9
    punpcklwd         m2, m10, m11
    punpcklwd         m4, m12, m13
    punpcklwd         m6, m14, m15
%if %1 > 4
    punpckhwd         m1, m8, m9
    punpckhwd         m3, m10, m11
    punpckhwd         m5, m12, m13
    punpckhwd         m7, m14, m15
%endif
    QPEL_HV_COMPUTE   %1, 14, my, ackusdw
    UNI_COMPUTE       %1, %2, m0, m1, [pw_%2]
    PEL_%2STORE%1   dstq, m0, m1

%if %1 <= 4
    movq              m8, m9
    movq              m9, m10
    movq             m10, m11
    movq             m11, m12
    movq             m12, m13
    movq             m13, m14
    movq             m14, m15
%else
    mova            m8, m9
    mova            m9, m10
    mova           m10, m11
    mova           m11, m12
    mova           m12, m13
    mova           m13, m14
    mova           m14, m15
%endif
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET

cglobal hevc_put_hevc_bi_qpel_hv%1_%2, 8, 10, 16, dst, dststride, src, srcstride, src2, height, mx, my, r3src, rfilter
%if cpuflag(avx2)
%assign %%shift  4
%else
%assign %%shift  3
%endif
    sub              mxq, 1
    sub              myq, 1
    shl              mxq, %%shift                ; multiply by 32
    shl              myq, %%shift                ; multiply by 32
    lea           r3srcq, [srcstrideq*3]
    sub             srcq, r3srcq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP              m8, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP              m9, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m10, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m11, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m12, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m13, m0
    add             srcq, srcstrideq
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m14, m0
    add             srcq, srcstrideq
.loop
    QPEL_H_LOAD       %2, srcq, %1, 15
    QPEL_HV_COMPUTE   %1, %2, mx, ackssdw
    SWAP             m15, m0
    punpcklwd         m0, m8, m9
    punpcklwd         m2, m10, m11
    punpcklwd         m4, m12, m13
    punpcklwd         m6, m14, m15
%if %1 > 4
    punpckhwd         m1, m8, m9
    punpckhwd         m3, m10, m11
    punpckhwd         m5, m12, m13
    punpckhwd         m7, m14, m15
%endif
    QPEL_HV_COMPUTE   %1, 14, my, ackssdw
    SIMPLE_BILOAD     %1, src2q, m8, m9 ;m9 not used in this case
    BI_COMPUTE        %1, %2, m0, m1, m8, m9, [pw_bi_%2]
    PEL_%2STORE%1   dstq, m0, m1

%if %1 <= 4
    movq              m8, m9
    movq              m9, m10
    movq             m10, m11
    movq             m11, m12
    movq             m12, m13
    movq             m13, m14
    movq             m14, m15
%else
    movdqa            m8, m9
    movdqa            m9, m10
    movdqa           m10, m11
    movdqa           m11, m12
    movdqa           m12, m13
    movdqa           m13, m14
    movdqa           m14, m15
%endif
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    add            src2q, 2*MAX_PB_SIZE          ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET
%endmacro

%macro WEIGHTING_FUNCS 2
%if WIN64 || ARCH_X86_32
cglobal hevc_put_hevc_uni_w%1_%2, 4, 5, 7, dst, dststride, src, height, denom, wx, ox
    mov             r4d, denomm
%define SHIFT  r4d
%else
cglobal hevc_put_hevc_uni_w%1_%2, 6, 6, 7, dst, dststride, src, height, denom, wx, ox
%define SHIFT  denomd
%endif
    lea           SHIFT, [SHIFT+14-%2]          ; shift = 14 - bitd + denom
%if %1 <= 4
    pxor             m1, m1
%endif
    movd             m2, wxm        ; WX
    movd             m4, SHIFT      ; shift
%if %1 <= 4
    punpcklwd        m2, m1
%else
    punpcklwd        m2, m2
%endif
    dec           SHIFT
    movdqu           m5, [pd_1]
    movd             m6, SHIFT
    pshufd           m2, m2, 0
    mov           SHIFT, oxm
    pslld            m5, m6
%if %2 != 8
    shl           SHIFT, %2-8       ; ox << (bitd - 8)
%endif
    movd             m3, SHIFT      ; OX
    pshufd           m3, m3, 0
%if WIN64 || ARCH_X86_32
    mov           SHIFT, heightm
%endif
.loop
   SIMPLE_LOAD        %1, 10, srcq, m0
%if %1 <= 4
    punpcklwd         m0, m1
    pmaddwd           m0, m2
    paddd             m0, m5
    psrad             m0, m4
    paddd             m0, m3
%else
    pmulhw            m6, m0, m2
    pmullw            m0, m2
    punpckhwd         m1, m0, m6
    punpcklwd         m0, m6
    paddd             m0, m5
    paddd             m1, m5
    psrad             m0, m4
    psrad             m1, m4
    paddd             m0, m3
    paddd             m1, m3
%endif
    packssdw          m0, m1
%if %2 == 8
    packuswb          m0, m0
%else
    CLIPW             m0, [pb_0], [max_pixels_%2]
%endif
    PEL_%2STORE%1   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, 2*MAX_PB_SIZE          ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET

cglobal hevc_put_hevc_bi_w%1_%2, 4, 6, 10, dst, dststride, src, src2, height, denom, wx0, wx1, ox0, ox1
    movifnidn        r5d, denomm
%if %1 <= 4
    pxor              m1, m1
%endif
    movd              m2, wx0m         ; WX0
    lea              r5d, [r5d+14-%2]  ; shift = 14 - bitd + denom
    movd              m3, wx1m         ; WX1
    movd              m0, r5d          ; shift
%if %1 <= 4
    punpcklwd         m2, m1
    punpcklwd         m3, m1
%else
    punpcklwd         m2, m2
    punpcklwd         m3, m3
%endif
    inc              r5d
    movd              m5, r5d          ; shift+1
    pshufd            m2, m2, 0
    mov              r5d, ox0m
    pshufd            m3, m3, 0
    add              r5d, ox1m
%if %2 != 8
    shl              r5d, %2-8         ; ox << (bitd - 8)
%endif
    inc              r5d
    movd              m4, r5d          ; offset
    pshufd            m4, m4, 0
%if UNIX64
%define h heightd
%else
    mov              r5d, heightm
%define h r5d
%endif
    pslld             m4, m0

.loop
   SIMPLE_LOAD        %1, 10, srcq,  m0
   SIMPLE_LOAD        %1, 10, src2q, m8
%if %1 <= 4
    punpcklwd         m0, m1
    punpcklwd         m8, m1
    pmaddwd           m0, m3
    pmaddwd           m8, m2
    paddd             m0, m4
    paddd             m0, m8
    psrad             m0, m5
%else
    pmulhw            m6, m0, m3
    pmullw            m0, m3
    pmulhw            m7, m8, m2
    pmullw            m8, m2
    punpckhwd         m1, m0, m6
    punpcklwd         m0, m6
    punpckhwd         m9, m8, m7
    punpcklwd         m8, m7
    paddd             m0, m8
    paddd             m1, m9
    paddd             m0, m4
    paddd             m1, m4
    psrad             m0, m5
    psrad             m1, m5
%endif
    packssdw          m0, m1
%if %2 == 8
    packuswb          m0, m0
%else
     CLIPW            m0, [pb_0], [max_pixels_%2]
%endif
    PEL_%2STORE%1   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, 2*MAX_PB_SIZE          ; src += srcstride
    add            src2q, 2*MAX_PB_SIZE          ; src2 += srcstride
    dec                h                         ; cmp height
    jnz               .loop                      ; height loop
    RET
%endmacro

INIT_XMM sse4                                    ; adds ff_ and _sse4 to function name

WEIGHTING_FUNCS 2, 8
WEIGHTING_FUNCS 4, 8
WEIGHTING_FUNCS 6, 8
WEIGHTING_FUNCS 8, 8

WEIGHTING_FUNCS 2, 10
WEIGHTING_FUNCS 4, 10
WEIGHTING_FUNCS 6, 10
WEIGHTING_FUNCS 8, 10

WEIGHTING_FUNCS 2, 12
WEIGHTING_FUNCS 4, 12
WEIGHTING_FUNCS 6, 12
WEIGHTING_FUNCS 8, 12

HEVC_PUT_HEVC_PEL_PIXELS  2, 8
HEVC_PUT_HEVC_PEL_PIXELS  4, 8
HEVC_PUT_HEVC_PEL_PIXELS  6, 8
HEVC_PUT_HEVC_PEL_PIXELS  8, 8
HEVC_PUT_HEVC_PEL_PIXELS 12, 8
HEVC_PUT_HEVC_PEL_PIXELS 16, 8

HEVC_PUT_HEVC_PEL_PIXELS 2, 10
HEVC_PUT_HEVC_PEL_PIXELS 4, 10
HEVC_PUT_HEVC_PEL_PIXELS 6, 10
HEVC_PUT_HEVC_PEL_PIXELS 8, 10

HEVC_PUT_HEVC_PEL_PIXELS 2, 12
HEVC_PUT_HEVC_PEL_PIXELS 4, 12
HEVC_PUT_HEVC_PEL_PIXELS 6, 12
HEVC_PUT_HEVC_PEL_PIXELS 8, 12

HEVC_PUT_HEVC_EPEL 2,  8
HEVC_PUT_HEVC_EPEL 4,  8
HEVC_PUT_HEVC_EPEL 6,  8
HEVC_PUT_HEVC_EPEL 8,  8
HEVC_PUT_HEVC_EPEL 12, 8
HEVC_PUT_HEVC_EPEL 16, 8


HEVC_PUT_HEVC_EPEL 2, 10
HEVC_PUT_HEVC_EPEL 4, 10
HEVC_PUT_HEVC_EPEL 6, 10
HEVC_PUT_HEVC_EPEL 8, 10

HEVC_PUT_HEVC_EPEL 2, 12
HEVC_PUT_HEVC_EPEL 4, 12
HEVC_PUT_HEVC_EPEL 6, 12
HEVC_PUT_HEVC_EPEL 8, 12

HEVC_PUT_HEVC_EPEL_HV 2,  8
HEVC_PUT_HEVC_EPEL_HV 4,  8
HEVC_PUT_HEVC_EPEL_HV 6,  8
HEVC_PUT_HEVC_EPEL_HV 8,  8
HEVC_PUT_HEVC_EPEL_HV 16, 8

HEVC_PUT_HEVC_EPEL_HV 2, 10
HEVC_PUT_HEVC_EPEL_HV 4, 10
HEVC_PUT_HEVC_EPEL_HV 6, 10
HEVC_PUT_HEVC_EPEL_HV 8, 10

HEVC_PUT_HEVC_EPEL_HV 2, 12
HEVC_PUT_HEVC_EPEL_HV 4, 12
HEVC_PUT_HEVC_EPEL_HV 6, 12
HEVC_PUT_HEVC_EPEL_HV 8, 12

HEVC_PUT_HEVC_QPEL 4,  8
HEVC_PUT_HEVC_QPEL 8,  8
HEVC_PUT_HEVC_QPEL 12, 8
HEVC_PUT_HEVC_QPEL 16, 8

HEVC_PUT_HEVC_QPEL 4, 10
HEVC_PUT_HEVC_QPEL 8, 10

HEVC_PUT_HEVC_QPEL 4, 12
HEVC_PUT_HEVC_QPEL 8, 12

HEVC_PUT_HEVC_QPEL_HV 2, 8
HEVC_PUT_HEVC_QPEL_HV 4, 8
HEVC_PUT_HEVC_QPEL_HV 6, 8
HEVC_PUT_HEVC_QPEL_HV 8, 8

HEVC_PUT_HEVC_QPEL_HV 2, 10
HEVC_PUT_HEVC_QPEL_HV 4, 10
HEVC_PUT_HEVC_QPEL_HV 6, 10
HEVC_PUT_HEVC_QPEL_HV 8, 10

HEVC_PUT_HEVC_QPEL_HV 2, 12
HEVC_PUT_HEVC_QPEL_HV 4, 12
HEVC_PUT_HEVC_QPEL_HV 6, 12
HEVC_PUT_HEVC_QPEL_HV 8, 12

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2  ; adds ff_ and _avx2 to function name & enables 256b registers : m0 for 256b, xm0 for 128b. cpuflag(avx2) = 1 / notcpuflag(avx) = 0

HEVC_PUT_HEVC_PEL_PIXELS 32, 8
HEVC_PUT_HEVC_PEL_PIXELS 16, 10

HEVC_PUT_HEVC_EPEL 32, 8
HEVC_PUT_HEVC_EPEL 16, 10

HEVC_PUT_HEVC_EPEL_HV 16, 10
HEVC_PUT_HEVC_EPEL_HV 32, 8

HEVC_PUT_HEVC_QPEL 32, 8

HEVC_PUT_HEVC_QPEL 16, 10

HEVC_PUT_HEVC_QPEL_HV 16, 10

%endif ;AVX2
%endif ; ARCH_X86_64
