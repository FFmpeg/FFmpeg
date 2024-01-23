; /*
; * Provide SSE luma and chroma mc functions for HEVC/VVC decoding
; * Copyright (c) 2013 Pierre-Edouard LEPERE
; * Copyright (c) 2023-2024 Nuo Mi
; * Copyright (c) 2023-2024 Wu Jianhua
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
cextern pw_1023
cextern pw_1024
cextern pw_4096
cextern pw_8192
%define scale_8 pw_512
%define scale_10 pw_2048
%define scale_12 pw_8192
%define max_pixels_8 pw_255
%define max_pixels_10 pw_1023
max_pixels_12:          times 16 dw ((1 << 12)-1)
cextern pb_0

SECTION .text
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

%macro VPBROADCASTW 2
%if notcpuflag(avx2)
    movd           %1, %2
    pshuflw        %1, %1, 0
    punpcklwd      %1, %1
%else
    vpbroadcastw   %1, %2
%endif
%endmacro

%macro MC_4TAP_FILTER 4 ; bitdepth, filter, a, b,
    VPBROADCASTW   %3, [%2q + 0 * 2]  ; coeff 0, 1
    VPBROADCASTW   %4, [%2q + 1 * 2]  ; coeff 2, 3
%if %1 != 8
    pmovsxbw       %3, xmm%3
    pmovsxbw       %4, xmm%4
%endif
%endmacro

%macro MC_4TAP_HV_FILTER 1
    VPBROADCASTW  m12, [vfq + 0 * 2]  ; vf 0, 1
    VPBROADCASTW  m13, [vfq + 1 * 2]  ; vf 2, 3
    VPBROADCASTW  m14, [hfq + 0 * 2]  ; hf 0, 1
    VPBROADCASTW  m15, [hfq + 1 * 2]  ; hf 2, 3

    pmovsxbw      m12, xm12
    pmovsxbw      m13, xm13
%if %1 != 8
    pmovsxbw      m14, xm14
    pmovsxbw      m15, xm15
%endif
    lea           r3srcq, [srcstrideq*3]
%endmacro

%macro MC_8TAP_SAVE_FILTER 5    ;offset, mm registers
    mova [rsp + %1 + 0*mmsize], %2
    mova [rsp + %1 + 1*mmsize], %3
    mova [rsp + %1 + 2*mmsize], %4
    mova [rsp + %1 + 3*mmsize], %5
%endmacro

%macro MC_8TAP_FILTER 2-3 ;bitdepth, filter, offset
    VPBROADCASTW                      m12, [%2q + 0 * 2]  ; coeff 0, 1
    VPBROADCASTW                      m13, [%2q + 1 * 2]  ; coeff 2, 3
    VPBROADCASTW                      m14, [%2q + 2 * 2]  ; coeff 4, 5
    VPBROADCASTW                      m15, [%2q + 3 * 2]  ; coeff 6, 7
%if %0 == 3
    MC_8TAP_SAVE_FILTER                %3, m12, m13, m14, m15
%endif

%if %1 != 8
    pmovsxbw                          m12, xm12
    pmovsxbw                          m13, xm13
    pmovsxbw                          m14, xm14
    pmovsxbw                          m15, xm15
    %if %0 == 3
    MC_8TAP_SAVE_FILTER     %3 + 4*mmsize, m12, m13, m14, m15
    %endif
%elif %0 == 3
    pmovsxbw                          m8, xm12
    pmovsxbw                          m9, xm13
    pmovsxbw                         m10, xm14
    pmovsxbw                         m11, xm15
    MC_8TAP_SAVE_FILTER     %3 + 4*mmsize, m8, m9, m10, m11
%endif

%endmacro

%macro MC_4TAP_LOAD 4
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

%macro MC_8TAP_H_LOAD 4
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

%macro MC_8TAP_V_LOAD 5
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
    movdqu         [%1], %2
%endmacro
%macro PEL_12STORE12 3
    PEL_12STORE8     %1, %2, %3
    movq        [%1+16], %3
%endmacro
%macro PEL_12STORE16 3
%if cpuflag(avx2)
    movu            [%1], %2
%else
    PEL_12STORE8      %1, %2, %3
    movdqu       [%1+16], %3
%endif
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
    movdqu         [%1], %2
%endmacro
%macro PEL_10STORE12 3
    PEL_10STORE8     %1, %2, %3
    movq        [%1+16], %3
%endmacro
%macro PEL_10STORE16 3
%if cpuflag(avx2)
    movu            [%1], %2
%else
    PEL_10STORE8      %1, %2, %3
    movdqu       [%1+16], %3
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
    movu          [%1], %2
%endif ; avx
%endmacro
%macro PEL_8STORE32 3
    movu          [%1], %2
%endmacro

%macro LOOP_END 3
    add              %1q, dststrideq             ; dst += dststride
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

%macro MC_4TAP_COMPUTE 4-8 ; bitdepth, width, filter1, filter2, HV/m0, m2, m1, m3
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

%macro MC_8TAP_HV_COMPUTE 4     ; width, bitdepth, filter

%if %2 == 8
    pmaddubsw         m0, [%3q+0*mmsize]    ;x1*c1+x2*c2
    pmaddubsw         m2, [%3q+1*mmsize]    ;x3*c3+x4*c4
    pmaddubsw         m4, [%3q+2*mmsize]    ;x5*c5+x6*c6
    pmaddubsw         m6, [%3q+3*mmsize]    ;x7*c7+x8*c8
    paddw             m0, m2
    paddw             m4, m6
    paddw             m0, m4
%else
    pmaddwd           m0, [%3q+4*mmsize]
    pmaddwd           m2, [%3q+5*mmsize]
    pmaddwd           m4, [%3q+6*mmsize]
    pmaddwd           m6, [%3q+7*mmsize]
    paddd             m0, m2
    paddd             m4, m6
    paddd             m0, m4
%if %2 != 8
    psrad             m0, %2-8
%endif
%if %1 > 4
    pmaddwd           m1, [%3q+4*mmsize]
    pmaddwd           m3, [%3q+5*mmsize]
    pmaddwd           m5, [%3q+6*mmsize]
    pmaddwd           m7, [%3q+7*mmsize]
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


%macro MC_8TAP_COMPUTE 2-3     ; width, bitdepth
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
; void %1_put_pixels(int16_t *dst, ptrdiff_t dststride, const uint8_t *_src, ptrdiff_t srcstride,
;                         int height, const int8_t *hf, const int8_t *vf, int width)
; ******************************

%macro PUT_PIXELS 3
    MC_PIXELS       %1, %2, %3
    MC_UNI_PIXELS   %1, %2, %3
%endmacro

%macro MC_PIXELS 3
cglobal %1_put_pixels%2_%3, 5, 5, 3, dst, dststride, src, srcstride, height
    pxor              m2, m2
.loop:
    SIMPLE_LOAD       %2, %3, srcq, m0
    MC_PIXEL_COMPUTE  %2, %3, 1
    PEL_10STORE%2     dstq, m0, m1
    LOOP_END         dst, src, srcstride
    RET
%endmacro

%macro MC_UNI_PIXELS 3
cglobal %1_put_uni_pixels%2_%3, 5, 5, 2, dst, dststride, src, srcstride, height
.loop:
    SIMPLE_LOAD       %2, %3, srcq, m0
    PEL_%3STORE%2   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET
%endmacro

%macro PUT_4TAP 3
%if cpuflag(avx2)
%define XMM_REGS  11
%else
%define XMM_REGS  8
%endif

; ******************************
; void %1_put_4tap_hX(int16_t *dst, ptrdiff_t dststride,
;      const uint8_t *_src, ptrdiff_t _srcstride, int height, int8_t *hf, int8_t *vf, int width);
; ******************************
cglobal %1_put_4tap_h%2_%3, 6, 6, XMM_REGS, dst, dststride, src, srcstride, height, hf
%assign %%stride ((%3 + 7)/8)
    MC_4TAP_FILTER       %3, hf, m4, m5
.loop:
    MC_4TAP_LOAD         %3, srcq-%%stride, %%stride, %2
    MC_4TAP_COMPUTE      %3, %2, m4, m5, 1
    PEL_10STORE%2      dstq, m0, m1
    LOOP_END            dst, src, srcstride
    RET

; ******************************
; void %1_put_uni_4tap_hX(uint8_t *dst, ptrdiff_t dststride,
;      const uint8_t *_src, ptrdiff_t _srcstride, int height, int8_t *hf, int8_t *vf, int width);
; ******************************
cglobal %1_put_uni_4tap_h%2_%3, 6, 7, XMM_REGS, dst, dststride, src, srcstride, height, hf
%assign %%stride ((%3 + 7)/8)
    movdqa            m6, [scale_%3]
    MC_4TAP_FILTER    %3, hf, m4, m5
.loop:
    MC_4TAP_LOAD      %3, srcq-%%stride, %%stride, %2
    MC_4TAP_COMPUTE   %3, %2, m4, m5
    UNI_COMPUTE       %2, %3, m0, m1, m6
    PEL_%3STORE%2   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET

; ******************************
; void %1_put_4tap_v(int16_t *dst, ptrdiff_t dststride,
;      const uint8_t *_src, ptrdiff_t _srcstride, int height, int8_t *hf, int8_t *vf, int width)
; ******************************
cglobal %1_put_4tap_v%2_%3, 7, 7, XMM_REGS, dst, dststride, src, srcstride, height, r3src, vf
    sub             srcq, srcstrideq
    MC_4TAP_FILTER    %3, vf, m4, m5
    lea           r3srcq, [srcstrideq*3]
.loop:
    MC_4TAP_LOAD      %3, srcq, srcstride, %2
    MC_4TAP_COMPUTE   %3, %2, m4, m5, 1
    PEL_10STORE%2     dstq, m0, m1
    LOOP_END          dst, src, srcstride
    RET

; ******************************
; void %1_put_uni_4tap_vX(uint8_t *dst, ptrdiff_t dststride,
;      const uint8_t *_src, ptrdiff_t _srcstride, int height, int8_t *hf, int8_t *vf, int width);
; ******************************
cglobal %1_put_uni_4tap_v%2_%3, 7, 7, XMM_REGS, dst, dststride, src, srcstride, height, r3src, vf
    movdqa            m6, [scale_%3]
    sub             srcq, srcstrideq
    MC_4TAP_FILTER       %3, vf, m4, m5
    lea           r3srcq, [srcstrideq*3]
.loop:
    MC_4TAP_LOAD      %3, srcq, srcstride, %2
    MC_4TAP_COMPUTE   %3, %2, m4, m5
    UNI_COMPUTE       %2, %3, m0, m1, m6
    PEL_%3STORE%2   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET
%endmacro

%macro PUT_4TAP_HV 3
; ******************************
; void put_4tap_hv(int16_t *dst, ptrdiff_t dststride,
;      const uint8_t *_src, ptrdiff_t _srcstride, int height, int8_t *hf, int8_t *vf, int width)
; ******************************
cglobal %1_put_4tap_hv%2_%3, 7, 8, 16 , dst, dststride, src, srcstride, height, hf, vf, r3src
%assign %%stride ((%3 + 7)/8)
    sub                 srcq, srcstrideq
    MC_4TAP_HV_FILTER    %3
    MC_4TAP_LOAD         %3, srcq-%%stride, %%stride, %2
    MC_4TAP_COMPUTE      %3, %2, m14, m15
%if (%2 > 8 && (%3 == 8))
    SWAP              m8, m1
%endif
    SWAP              m4, m0
    add             srcq, srcstrideq
    MC_4TAP_LOAD         %3, srcq-%%stride, %%stride, %2
    MC_4TAP_COMPUTE      %3, %2, m14, m15
%if (%2 > 8 && (%3 == 8))
    SWAP              m9, m1
%endif
    SWAP              m5, m0
    add             srcq, srcstrideq
    MC_4TAP_LOAD         %3, srcq-%%stride, %%stride, %2
    MC_4TAP_COMPUTE      %3, %2, m14, m15
%if (%2 > 8 && (%3 == 8))
    SWAP             m10, m1
%endif
    SWAP              m6, m0
    add             srcq, srcstrideq
.loop:
    MC_4TAP_LOAD         %3, srcq-%%stride, %%stride, %2
    MC_4TAP_COMPUTE      %3, %2, m14, m15
%if (%2 > 8 && (%3 == 8))
    SWAP             m11, m1
%endif
    SWAP              m7, m0
    punpcklwd         m0, m4, m5
    punpcklwd         m2, m6, m7
%if %2 > 4
    punpckhwd         m1, m4, m5
    punpckhwd         m3, m6, m7
%endif
    MC_4TAP_COMPUTE      14, %2, m12, m13
%if (%2 > 8 && (%3 == 8))
    punpcklwd         m4, m8, m9
    punpcklwd         m2, m10, m11
    punpckhwd         m8, m8, m9
    punpckhwd         m3, m10, m11
    MC_4TAP_COMPUTE      14, %2, m12, m13, m4, m2, m8, m3
%if cpuflag(avx2)
    vinserti128       m2, m0, xm4, 1
    vperm2i128        m3, m0, m4, q0301
    PEL_10STORE%2     dstq, m2, m3
%else
    PEL_10STORE%2     dstq, m0, m4
%endif
%else
    PEL_10STORE%2     dstq, m0, m1
%endif
    movdqa            m4, m5
    movdqa            m5, m6
    movdqa            m6, m7
%if (%2 > 8 && (%3 == 8))
    mova              m8, m9
    mova              m9, m10
    mova             m10, m11
%endif
    LOOP_END         dst, src, srcstride
    RET

cglobal %1_put_uni_4tap_hv%2_%3, 7, 8, 16 , dst, dststride, src, srcstride, height, hf, vf, r3src
%assign %%stride ((%3 + 7)/8)
    sub                srcq, srcstrideq
    MC_4TAP_HV_FILTER    %3
    MC_4TAP_LOAD         %3, srcq-%%stride, %%stride, %2
    MC_4TAP_COMPUTE      %3, %2, m14, m15
%if (%2 > 8 && (%3 == 8))
    SWAP                 m8, m1
%endif
    SWAP                 m4, m0
    add                srcq, srcstrideq
    MC_4TAP_LOAD         %3, srcq-%%stride, %%stride, %2
    MC_4TAP_COMPUTE      %3, %2, m14, m15
%if (%2 > 8 && (%3 == 8))
    SWAP                 m9, m1
%endif
    SWAP                 m5, m0
    add                srcq, srcstrideq
    MC_4TAP_LOAD         %3, srcq-%%stride, %%stride, %2
    MC_4TAP_COMPUTE      %3, %2, m14, m15
%if (%2 > 8 && (%3 == 8))
    SWAP                m10, m1
%endif
    SWAP                 m6, m0
    add                srcq, srcstrideq
.loop:
    MC_4TAP_LOAD         %3, srcq-%%stride, %%stride, %2
    MC_4TAP_COMPUTE      %3, %2, m14, m15
%if (%2 > 8 && (%3 == 8))
    SWAP                m11, m1
%endif
    mova                 m7, m0
    punpcklwd            m0, m4, m5
    punpcklwd            m2, m6, m7
%if %2 > 4
    punpckhwd            m1, m4, m5
    punpckhwd            m3, m6, m7
%endif
    MC_4TAP_COMPUTE      14, %2, m12, m13
%if (%2 > 8 && (%3 == 8))
    punpcklwd            m4, m8, m9
    punpcklwd            m2, m10, m11
    punpckhwd            m8, m8, m9
    punpckhwd            m3, m10, m11
    MC_4TAP_COMPUTE      14, %2, m12, m13, m4, m2, m8, m3
    UNI_COMPUTE          %2, %3, m0, m4, [scale_%3]
%else
    UNI_COMPUTE          %2, %3, m0, m1, [scale_%3]
%endif
    PEL_%3STORE%2      dstq, m0, m1
    mova                 m4, m5
    mova                 m5, m6
    mova                 m6, m7
%if (%2 > 8 && (%3 == 8))
    mova                 m8, m9
    mova                 m9, m10
    mova                m10, m11
%endif
    add                dstq, dststrideq             ; dst += dststride
    add                srcq, srcstrideq             ; src += srcstride
    dec             heightd                         ; cmp height
    jnz               .loop                         ; height loop
    RET
%endmacro

; ******************************
; void put_8tap_hX_X_X(int16_t *dst, ptrdiff_t dststride, const uint8_t *_src, ptrdiff_t srcstride,
;                       int height, const int8_t *hf, const int8_t *vf, int width)
; ******************************

%macro PUT_8TAP 3
cglobal %1_put_8tap_h%2_%3, 6, 6, 16, dst, dststride, src, srcstride, height, hf
    MC_8TAP_FILTER          %3, hf
.loop:
    MC_8TAP_H_LOAD          %3, srcq, %2, 10
    MC_8TAP_COMPUTE         %2, %3, 1
%if %3 > 8
    packssdw                m0, m1
%endif
    PEL_10STORE%2         dstq, m0, m1
    LOOP_END               dst, src, srcstride
    RET

; ******************************
; void put_uni_8tap_hX_X_X(int16_t *dst, ptrdiff_t dststride, const uint8_t *_src, ptrdiff_t srcstride,
;                       int height, const int8_t *hf, const int8_t *vf, int width)
; ******************************
cglobal %1_put_uni_8tap_h%2_%3, 6, 7, 16 , dst, dststride, src, srcstride, height, hf
    mova                 m9, [scale_%3]
    MC_8TAP_FILTER       %3, hf
.loop:
    MC_8TAP_H_LOAD       %3, srcq, %2, 10
    MC_8TAP_COMPUTE      %2, %3
%if %3 > 8
    packssdw             m0, m1
%endif
    UNI_COMPUTE          %2, %3, m0, m1, m9
    PEL_%3STORE%2      dstq, m0, m1
    add                dstq, dststrideq             ; dst += dststride
    add                srcq, srcstrideq             ; src += srcstride
    dec             heightd                         ; cmp height
    jnz               .loop                         ; height loop
    RET


; ******************************
; void put_8tap_vX_X_X(int16_t *dst, ptrdiff_t dststride, const uint8_t *_src, ptrdiff_t srcstride,
;                      int height, const int8_t *hf, const int8_t *vf, int width)
; ******************************
cglobal %1_put_8tap_v%2_%3, 7, 8, 16, dst, dststride, src, srcstride, height, r3src, vf
    MC_8TAP_FILTER        %3, vf
    lea               r3srcq, [srcstrideq*3]
.loop:
    MC_8TAP_V_LOAD        %3, srcq, srcstride, %2, r7
    MC_8TAP_COMPUTE       %2, %3, 1
%if %3 > 8
    packssdw              m0, m1
%endif
    PEL_10STORE%2       dstq, m0, m1
    LOOP_END             dst, src, srcstride
    RET

; ******************************
; void put_uni_8tap_vX_X_X(int16_t *dst, ptrdiff_t dststride, const uint8_t *_src, ptrdiff_t srcstride,
;                       int height, const int8_t *hf, const int8_t *vf, int width)
; ******************************
cglobal %1_put_uni_8tap_v%2_%3, 7, 9, 16, dst, dststride, src, srcstride, height, r3src, vf
    MC_8TAP_FILTER    %3, vf
    movdqa            m9, [scale_%3]
    lea           r3srcq, [srcstrideq*3]
.loop:
    MC_8TAP_V_LOAD    %3, srcq, srcstride, %2, r8
    MC_8TAP_COMPUTE   %2, %3
%if %3 > 8
    packssdw          m0, m1
%endif
    UNI_COMPUTE       %2, %3, m0, m1, m9
    PEL_%3STORE%2   dstq, m0, m1
    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    RET

%endmacro


; ******************************
; void put_8tap_hvX_X(int16_t *dst, ptrdiff_t dststride, const uint8_t *_src, ptrdiff_t srcstride,
;                     int height, const int8_t *hf, const int8_t *vf, int width)
; ******************************
%macro PUT_8TAP_HV 3
cglobal %1_put_8tap_hv%2_%3, 7, 8, 16, 0 - mmsize*16, dst, dststride, src, srcstride, height, hf, vf, r3src
    MC_8TAP_FILTER           %3, hf, 0
    lea                     hfq, [rsp]
    MC_8TAP_FILTER           %3, vf, 8*mmsize
    lea                     vfq, [rsp + 8*mmsize]

    lea                  r3srcq, [srcstrideq*3]
    sub                    srcq, r3srcq

    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                     m8, m0
    add                    srcq, srcstrideq
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                     m9, m0
    add                    srcq, srcstrideq
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                    m10, m0
    add                    srcq, srcstrideq
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                    m11, m0
    add                    srcq, srcstrideq
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                    m12, m0
    add                    srcq, srcstrideq
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                    m13, m0
    add                    srcq, srcstrideq
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                    m14, m0
    add                    srcq, srcstrideq
.loop:
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                    m15, m0
    punpcklwd                m0, m8, m9
    punpcklwd                m2, m10, m11
    punpcklwd                m4, m12, m13
    punpcklwd                m6, m14, m15
%if %2 > 4
    punpckhwd                m1, m8, m9
    punpckhwd                m3, m10, m11
    punpckhwd                m5, m12, m13
    punpckhwd                m7, m14, m15
%endif
%if %2 <= 4
    movq                     m8, m9
    movq                     m9, m10
    movq                    m10, m11
    movq                    m11, m12
    movq                    m12, m13
    movq                    m13, m14
    movq                    m14, m15
%else
    movdqa                   m8, m9
    movdqa                   m9, m10
    movdqa                  m10, m11
    movdqa                  m11, m12
    movdqa                  m12, m13
    movdqa                  m13, m14
    movdqa                  m14, m15
%endif
    MC_8TAP_HV_COMPUTE       %2, 14, vf, ackssdw
    PEL_10STORE%2          dstq, m0, m1

    LOOP_END                dst, src, srcstride
    RET


cglobal %1_put_uni_8tap_hv%2_%3, 7, 9, 16, 0 - 16*mmsize, dst, dststride, src, srcstride, height, hf, vf, r3src
    MC_8TAP_FILTER           %3, hf, 0
    lea                     hfq, [rsp]
    MC_8TAP_FILTER           %3, vf, 8*mmsize
    lea                     vfq, [rsp + 8*mmsize]
    lea           r3srcq, [srcstrideq*3]
    sub             srcq, r3srcq
    MC_8TAP_H_LOAD       %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE   %2, %3, hf, ackssdw
    SWAP              m8, m0
    add             srcq, srcstrideq
    MC_8TAP_H_LOAD       %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE   %2, %3, hf, ackssdw
    SWAP              m9, m0
    add             srcq, srcstrideq
    MC_8TAP_H_LOAD       %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE   %2, %3, hf, ackssdw
    SWAP             m10, m0
    add             srcq, srcstrideq
    MC_8TAP_H_LOAD       %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE   %2, %3, hf, ackssdw
    SWAP             m11, m0
    add             srcq, srcstrideq
    MC_8TAP_H_LOAD       %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE   %2, %3, hf, ackssdw
    SWAP             m12, m0
    add             srcq, srcstrideq
    MC_8TAP_H_LOAD       %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE   %2, %3, hf, ackssdw
    SWAP             m13, m0
    add             srcq, srcstrideq
    MC_8TAP_H_LOAD       %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE   %2, %3, hf, ackssdw
    SWAP             m14, m0
    add             srcq, srcstrideq
.loop:
    MC_8TAP_H_LOAD       %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE   %2, %3, hf, ackssdw
    SWAP             m15, m0
    punpcklwd         m0, m8, m9
    punpcklwd         m2, m10, m11
    punpcklwd         m4, m12, m13
    punpcklwd         m6, m14, m15
%if %2 > 4
    punpckhwd         m1, m8, m9
    punpckhwd         m3, m10, m11
    punpckhwd         m5, m12, m13
    punpckhwd         m7, m14, m15
%endif
    MC_8TAP_HV_COMPUTE   %2, 14, vf, ackusdw
    UNI_COMPUTE       %2, %3, m0, m1, [scale_%3]
    PEL_%3STORE%2   dstq, m0, m1

%if %2 <= 4
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

%endmacro

%macro H2656PUT_PIXELS 2
    PUT_PIXELS h2656, %1, %2
%endmacro

%macro H2656PUT_4TAP 2
    PUT_4TAP h2656, %1, %2
%endmacro

%macro H2656PUT_4TAP_HV 2
    PUT_4TAP_HV h2656, %1, %2
%endmacro

%macro H2656PUT_8TAP 2
    PUT_8TAP h2656, %1, %2
%endmacro

%macro H2656PUT_8TAP_HV 2
    PUT_8TAP_HV h2656, %1, %2
%endmacro

%if ARCH_X86_64

INIT_XMM sse4
H2656PUT_PIXELS  2, 8
H2656PUT_PIXELS  4, 8
H2656PUT_PIXELS  6, 8
H2656PUT_PIXELS  8, 8
H2656PUT_PIXELS 12, 8
H2656PUT_PIXELS 16, 8

H2656PUT_PIXELS 2, 10
H2656PUT_PIXELS 4, 10
H2656PUT_PIXELS 6, 10
H2656PUT_PIXELS 8, 10

H2656PUT_PIXELS 2, 12
H2656PUT_PIXELS 4, 12
H2656PUT_PIXELS 6, 12
H2656PUT_PIXELS 8, 12

H2656PUT_4TAP 2,  8
H2656PUT_4TAP 4,  8
H2656PUT_4TAP 6,  8
H2656PUT_4TAP 8,  8

H2656PUT_4TAP 12,  8
H2656PUT_4TAP 16, 8

H2656PUT_4TAP 2, 10
H2656PUT_4TAP 4, 10
H2656PUT_4TAP 6, 10
H2656PUT_4TAP 8, 10

H2656PUT_4TAP 2, 12
H2656PUT_4TAP 4, 12
H2656PUT_4TAP 6, 12
H2656PUT_4TAP 8, 12

H2656PUT_4TAP_HV 2,  8
H2656PUT_4TAP_HV 4,  8
H2656PUT_4TAP_HV 6,  8
H2656PUT_4TAP_HV 8,  8
H2656PUT_4TAP_HV 16, 8

H2656PUT_4TAP_HV 2, 10
H2656PUT_4TAP_HV 4, 10
H2656PUT_4TAP_HV 6, 10
H2656PUT_4TAP_HV 8, 10

H2656PUT_4TAP_HV 2, 12
H2656PUT_4TAP_HV 4, 12
H2656PUT_4TAP_HV 6, 12
H2656PUT_4TAP_HV 8, 12

H2656PUT_8TAP  4,  8
H2656PUT_8TAP  8,  8
H2656PUT_8TAP 12, 8
H2656PUT_8TAP 16, 8

H2656PUT_8TAP 4, 10
H2656PUT_8TAP 8, 10

H2656PUT_8TAP 4, 12
H2656PUT_8TAP 8, 12

H2656PUT_8TAP_HV 4, 8
H2656PUT_8TAP_HV 8, 8

H2656PUT_8TAP_HV 4, 10
H2656PUT_8TAP_HV 8, 10

H2656PUT_8TAP_HV 4, 12
H2656PUT_8TAP_HV 8, 12

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2

H2656PUT_PIXELS  32, 8
H2656PUT_PIXELS  16, 10
H2656PUT_PIXELS  16, 12

H2656PUT_8TAP 32,  8
H2656PUT_8TAP 16, 10
H2656PUT_8TAP 16, 12

H2656PUT_8TAP_HV 32, 8
H2656PUT_8TAP_HV 16, 10
H2656PUT_8TAP_HV 16, 12

H2656PUT_4TAP 32,  8
H2656PUT_4TAP 16, 10
H2656PUT_4TAP 16, 12

H2656PUT_4TAP_HV 32, 8
H2656PUT_4TAP_HV 16, 10
H2656PUT_4TAP_HV 16, 12

%endif

%endif
