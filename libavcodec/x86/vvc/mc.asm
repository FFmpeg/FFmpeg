; /*
; * Provide SIMD MC functions for VVC decoding
; *
; * Copyright © 2021, VideoLAN and dav1d authors
; * Copyright © 2021, Two Orioles, LLC
; * All rights reserved.
; *
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

%define MAX_PB_SIZE 128

SECTION_RODATA

%if ARCH_X86_64

%if HAVE_AVX2_EXTERNAL

%macro AVG_JMP_TABLE 4-*
    %xdefine %1_%2_%4_table (%%table - 2*%5)
    %xdefine %%base %1_%2_%4_table
    %xdefine %%prefix mangle(private_prefix %+ _vvc_%1_%3_%4)
    %%table:
    %rep %0 - 4
        dd %%prefix %+ .w%5 - %%base
        %rotate 1
    %endrep
%endmacro

AVG_JMP_TABLE    avg,  8,  8, avx2,                2, 4, 8, 16, 32, 64, 128
AVG_JMP_TABLE    avg, 16, 10, avx2,                2, 4, 8, 16, 32, 64, 128
AVG_JMP_TABLE  w_avg,  8,  8, avx2,                2, 4, 8, 16, 32, 64, 128
AVG_JMP_TABLE  w_avg, 16, 10, avx2,                2, 4, 8, 16, 32, 64, 128

SECTION .text

%macro AVG_W16_FN 3 ; bpc, op, count
    %assign %%i 0
    %rep %3
        %define off %%i
        AVG_LOAD_W16        0, off
        %2                 %1, 16
        AVG_SAVE_W16       %1, 0, off


        AVG_LOAD_W16        1, off
        %2                 %1, 16
        AVG_SAVE_W16       %1, 1, off

        %assign %%i %%i+1
    %endrep
%endmacro

%macro AVG_FN 2-3 1; bpc, op, instantiate implementation
   jmp                  wq

%if %3
INIT_XMM cpuname
.w2:
    movd                xm0, [src0q]
    pinsrd              xm0, [src0q + AVG_SRC_STRIDE], 1
    movd                xm1, [src1q]
    pinsrd              xm1, [src1q + AVG_SRC_STRIDE], 1
    %2                   %1, 2
    AVG_SAVE_W2          %1
    AVG_LOOP_END        .w2

.w4:
    movq                xm0, [src0q]
    pinsrq              xm0, [src0q + AVG_SRC_STRIDE], 1
    movq                xm1, [src1q]
    pinsrq              xm1, [src1q + AVG_SRC_STRIDE], 1
    %2                   %1, 4
    AVG_SAVE_W4          %1

    AVG_LOOP_END        .w4

INIT_YMM cpuname
.w8:
    movu               xm0, [src0q]
    movu               xm1, [src1q]
    vinserti128         m0, m0, [src0q + AVG_SRC_STRIDE], 1
    vinserti128         m1, m1, [src1q + AVG_SRC_STRIDE], 1
    %2                  %1, 8
    AVG_SAVE_W8         %1

    AVG_LOOP_END       .w8

.w16:
    AVG_W16_FN          %1, %2, 1

    AVG_LOOP_END       .w16

.w32:
    AVG_W16_FN          %1, %2, 2

    AVG_LOOP_END       .w32

.w64:
    AVG_W16_FN          %1, %2, 4

    AVG_LOOP_END       .w64

.w128:
    AVG_W16_FN          %1, %2, 8

    AVG_LOOP_END       .w128

.ret:
    RET
%endif
%endmacro

%macro AVG   2 ; bpc, width
    paddsw               m0, m1
    pmulhrsw             m0, m2
%if %1 != 8
    CLIPW                m0, m3, m4
%endif
%endmacro

%macro W_AVG 2 ; bpc, width
%if %2 > 2
    punpckhwd            m5, m0, m1
    pmaddwd              m5, m3
    paddd                m5, m4
    psrad                m5, xm2
%endif

    punpcklwd            m0, m0, m1
    pmaddwd              m0, m3
    paddd                m0, m4
    psrad                m0, xm2

%if %2 == 2
    packssdw             m0, m0
%else
    packssdw             m0, m5
%endif
%if %1 != 8
    CLIPW                m0, m6, m7
%endif
%endmacro

%macro AVG_LOAD_W16 2  ; line, offset
    movu               m0, [src0q + %1 * AVG_SRC_STRIDE + %2 * 32]
    movu               m1, [src1q + %1 * AVG_SRC_STRIDE + %2 * 32]
%endmacro

%macro AVG_SAVE_W2 1 ;bpc
    %if %1 == 16
        movd             [dstq], xm0
        pextrd [dstq + strideq], xm0, 1
    %else
        packuswb           m0, m0
        pextrw           [dstq], xm0, 0
        pextrw [dstq + strideq], xm0, 1
    %endif
%endmacro

%macro AVG_SAVE_W4 1 ;bpc
    %if %1 == 16
        movq             [dstq], xm0
        pextrq [dstq + strideq], xm0, 1
    %else
        packuswb           m0, m0
        movd             [dstq], xm0
        pextrd [dstq + strideq], xm0, 1
    %endif
%endmacro

%macro AVG_SAVE_W8 1 ;bpc
    %if %1 == 16
        movu                    [dstq], xm0
        vextracti128  [dstq + strideq], m0, 1
    %else
        packuswb                    m0, m0
        vpermq                      m0, m0, 1000b
        movq                    [dstq], xm0
        pextrq        [dstq + strideq], xm0, 1
    %endif
%endmacro

%macro AVG_SAVE_W16 3 ; bpc, line, offset
    %if %1 == 16
        movu               [dstq + %2 * strideq + %3 * 32], m0
    %else
        packuswb                                        m0, m0
        vpermq                                          m0, m0, 1000b
        movu               [dstq + %2 * strideq + %3 * 16], xm0
    %endif
%endmacro

%macro AVG_LOOP_END 1
    sub                  hd, 2
    je                 .ret

    lea               src0q, [src0q + 2 * AVG_SRC_STRIDE]
    lea               src1q, [src1q + 2 * AVG_SRC_STRIDE]
    lea                dstq, [dstq + 2 * strideq]
    jmp                  %1
%endmacro

%define AVG_SRC_STRIDE MAX_PB_SIZE*2

;void ff_vvc_avg_%1_avx2(uint8_t *dst, ptrdiff_t dst_stride, const int16_t *src0,
;                        const int16_t *src1, int width, int height);
%macro VVC_AVG_AVX2 3
cglobal vvc_avg_%2, 4, 7, 5, dst, stride, src0, src1, w, h
    movifnidn            hd, hm

    pcmpeqw              m2, m2
%if %1 != 8
    pxor                 m3, m3             ; pixel min
%endif

    lea                  r6, [avg_%1 %+ SUFFIX %+ _table]
    tzcnt                wd, wm
    movsxd               wq, dword [r6+wq*4]
    psrlw                m4, m2, 16-%2      ; pixel max
    psubw                m2, m4, m2         ; 1 << bpp
    add                  wq, r6
    AVG_FN               %1, AVG, %3
%endmacro

;void ff_vvc_w_avg_%2_avx(uint8_t *dst, ptrdiff_t dst_stride,
;                         const int16_t *src0, const int16_t *src1, int width, int height,
;                         int denom, intptr_t w0, int w1, int o);
%macro VVC_W_AVG_AVX2 3
cglobal vvc_w_avg_%2, 4, 7+2*UNIX64, 6+2*(%1 != 8), dst, stride, src0, src1, w, h
%if UNIX64
    ; r6-r8 are volatile and not used for parameter passing
    DECLARE_REG_TMP 6, 7, 8
%else ; Win64
    ; r4-r6 are volatile and not used for parameter passing
    DECLARE_REG_TMP 4, 5, 6
%endif

    mov                 t1d, r6m                ; denom
    mov                 t0d, r9m                ; o0 + o1
    movifnidn           t2d, r8m                ; w1
    add                 t1d, 15-%2
%if %2 != 8
    shl                 t0d, %2 - 8
%endif
    movd                xm2, t1d                ; shift
    inc                 t0d                     ; ((o0 + o1) << (BIT_DEPTH - 8)) + 1
    shl                 t2d, 16
    movd                xm4, t0d
    mov                 t2w, r7m                ; w0
    movd                xm3, t2d
    vpbroadcastd         m3, xm3                ; w0, w1

%if %1 != 8
    pcmpeqw              m7, m7
    pxor                 m6, m6                 ; pixel min
    psrlw                m7, 16-%2              ; pixel max
%endif

    lea                 r6, [w_avg_%1 %+ SUFFIX %+ _table]
    tzcnt               wd, wm
    movsxd              wq, dword [r6+wq*4]

    pslld               xm4, xm2
    psrad               xm4, 1
    vpbroadcastd         m4, xm4                 ; offset

    movifnidn            hd, hm

    add                 wq, r6
    AVG_FN              %1, W_AVG, %3
%endmacro

INIT_YMM avx2

VVC_AVG_AVX2 16, 12, 0

VVC_W_AVG_AVX2 16, 12, 0

VVC_AVG_AVX2 16, 10, 1

VVC_W_AVG_AVX2 16, 10, 1

VVC_AVG_AVX2 8, 8, 1

VVC_W_AVG_AVX2 8, 8, 1
%endif

%endif
