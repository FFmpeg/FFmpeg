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

SECTION_RODATA 32

%if ARCH_X86_64

%if HAVE_AVX2_EXTERNAL

pw_0    times 2 dw   0
pw_1    times 2 dw   1
pw_4    times 2 dw   4
pw_12   times 2 dw  12
pw_256  times 2 dw 256

%macro AVG_JMP_TABLE 3-*
    %xdefine %1_%2_%3_table (%%table - 2*%4)
    %xdefine %%base %1_%2_%3_table
    %xdefine %%prefix mangle(private_prefix %+ _vvc_%1_%2bpc_%3)
    %%table:
    %rep %0 - 3
        dd %%prefix %+ .w%4 - %%base
        %rotate 1
    %endrep
%endmacro

AVG_JMP_TABLE    avg,  8, avx2,                2, 4, 8, 16, 32, 64, 128
AVG_JMP_TABLE    avg, 16, avx2,                2, 4, 8, 16, 32, 64, 128
AVG_JMP_TABLE  w_avg,  8, avx2,                2, 4, 8, 16, 32, 64, 128
AVG_JMP_TABLE  w_avg, 16, avx2,                2, 4, 8, 16, 32, 64, 128

SECTION .text

%macro AVG_W16_FN 3 ; bpc, op, count
    %assign %%i 0
    %rep %3
        %define off %%i
        AVG_LOAD_W16        0, off
        %2
        AVG_SAVE_W16       %1, 0, off


        AVG_LOAD_W16        1, off
        %2
        AVG_SAVE_W16       %1, 1, off

        %assign %%i %%i+1
    %endrep
%endmacro

%macro AVG_FN 2 ; bpc, op
   jmp                  wq

.w2:
    movd                xm0, [src0q]
    pinsrd              xm0, [src0q + AVG_SRC_STRIDE], 1
    movd                xm1, [src1q]
    pinsrd              xm1, [src1q + AVG_SRC_STRIDE], 1
    %2
    AVG_SAVE_W2          %1
    AVG_LOOP_END        .w2

.w4:
    movq                xm0, [src0q]
    pinsrq              xm0, [src0q + AVG_SRC_STRIDE], 1
    movq                xm1, [src1q]
    pinsrq              xm1, [src1q + AVG_SRC_STRIDE], 1
    %2
    AVG_SAVE_W4          %1

    AVG_LOOP_END        .w4

.w8:
    vinserti128         m0, m0, [src0q], 0
    vinserti128         m0, m0, [src0q + AVG_SRC_STRIDE], 1
    vinserti128         m1, m1, [src1q], 0
    vinserti128         m1, m1, [src1q + AVG_SRC_STRIDE], 1
    %2
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
%endmacro

%macro AVG   0
    paddsw               m0, m1
    pmulhrsw             m0, m2
    CLIPW                m0, m3, m4
%endmacro

%macro W_AVG 0
    punpckhwd            m5, m0, m1
    pmaddwd              m5, m3
    paddd                m5, m4
    psrad                m5, xm2

    punpcklwd            m0, m0, m1
    pmaddwd              m0, m3
    paddd                m0, m4
    psrad                m0, xm2

    packssdw             m0, m5
    CLIPW                m0, m6, m7
%endmacro

%macro AVG_LOAD_W16 2  ; line, offset
    movu               m0, [src0q + %1 * AVG_SRC_STRIDE + %2 * 32]
    movu               m1, [src1q + %1 * AVG_SRC_STRIDE + %2 * 32]
%endmacro

%macro AVG_SAVE_W2 1 ;bpc
    %if %1 == 16
        pextrd           [dstq], xm0, 0
        pextrd [dstq + strideq], xm0, 1
    %else
        packuswb           m0, m0
        pextrw           [dstq], xm0, 0
        pextrw [dstq + strideq], xm0, 1
    %endif
%endmacro

%macro AVG_SAVE_W4 1 ;bpc
    %if %1 == 16
        pextrq           [dstq], xm0, 0
        pextrq [dstq + strideq], xm0, 1
    %else
        packuswb           m0, m0
        pextrd           [dstq], xm0, 0
        pextrd [dstq + strideq], xm0, 1
    %endif
%endmacro

%macro AVG_SAVE_W8 1 ;bpc
    %if %1 == 16
        vextracti128            [dstq], m0, 0
        vextracti128  [dstq + strideq], m0, 1
    %else
        packuswb                    m0, m0
        vpermq                      m0, m0, 1000b
        pextrq                  [dstq], xm0, 0
        pextrq        [dstq + strideq], xm0, 1
    %endif
%endmacro

%macro AVG_SAVE_W16 3 ; bpc, line, offset
    %if %1 == 16
        movu               [dstq + %2 * strideq + %3 * 32], m0
    %else
        packuswb                                        m0, m0
        vpermq                                          m0, m0, 1000b
        vextracti128       [dstq + %2 * strideq + %3 * 16], m0, 0
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

;void ff_vvc_avg_%1bpc_avx2(uint8_t *dst, ptrdiff_t dst_stride,
;   const int16_t *src0, const int16_t *src1, intptr_t width, intptr_t height, intptr_t pixel_max);
%macro VVC_AVG_AVX2 1
cglobal vvc_avg_%1bpc, 4, 7, 5, dst, stride, src0, src1, w, h, bd
    movifnidn            hd, hm

    pxor                 m3, m3             ; pixel min
    vpbroadcastw         m4, bdm            ; pixel max

    movifnidn           bdd, bdm
    inc                 bdd
    tzcnt               bdd, bdd            ; bit depth

    sub                 bdd, 8
    movd                xm0, bdd
    vpbroadcastd         m1, [pw_4]
    pminuw               m0, m1
    vpbroadcastd         m2, [pw_256]
    psllw                m2, xm0                ; shift

    lea                  r6, [avg_%1 %+ SUFFIX %+ _table]
    tzcnt                wd, wm
    movsxd               wq, dword [r6+wq*4]
    add                  wq, r6
    AVG_FN               %1, AVG
%endmacro

;void ff_vvc_w_avg_%1bpc_avx(uint8_t *dst, ptrdiff_t dst_stride,
;    const int16_t *src0, const int16_t *src1, intptr_t width, intptr_t height,
;    intptr_t denom, intptr_t w0, intptr_t w1,  intptr_t o0, intptr_t o1, intptr_t pixel_max);
%macro VVC_W_AVG_AVX2 1
cglobal vvc_w_avg_%1bpc, 4, 8, 8, dst, stride, src0, src1, w, h, t0, t1

    movifnidn            hd, hm

    movifnidn           t0d, r8m                ; w1
    shl                 t0d, 16
    mov                 t0w, r7m                ; w0
    movd                xm3, t0d
    vpbroadcastd         m3, xm3                ; w0, w1

    pxor                m6, m6                  ;pixel min
    vpbroadcastw        m7, r11m                ;pixel max

    mov                 t1q, rcx                ; save ecx
    mov                 ecx, r11m
    inc                 ecx                     ; bd
    tzcnt               ecx, ecx
    sub                 ecx, 8
    mov                 t0d, r9m                ; o0
    add                 t0d, r10m               ; o1
    shl                 t0d, cl
    inc                 t0d                     ;((o0 + o1) << (BIT_DEPTH - 8)) + 1

    neg                 ecx
    add                 ecx, 4                  ; bd - 12
    cmovl               ecx, [pw_0]
    add                 ecx, 3
    add                 ecx, r6m
    movd                xm2, ecx                ; shift

    dec                ecx
    shl                t0d, cl
    movd               xm4, t0d
    vpbroadcastd        m4, xm4                 ; offset
    mov                rcx, t1q                 ; restore ecx

    lea                 r6, [w_avg_%1 %+ SUFFIX %+ _table]
    tzcnt               wd, wm
    movsxd              wq, dword [r6+wq*4]
    add                 wq, r6
    AVG_FN              %1, W_AVG
%endmacro

INIT_YMM avx2

VVC_AVG_AVX2 16

VVC_AVG_AVX2 8

VVC_W_AVG_AVX2 16

VVC_W_AVG_AVX2 8
%endif

%endif
