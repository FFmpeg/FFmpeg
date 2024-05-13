;******************************************************************************
;* VVC Adaptive Loop Filter SIMD optimizations
;*
;* Copyright (c) 2023-2024 Nuo Mi <nuomi2021@gmail.com>
;* Copyright (c) 2023-2024 Wu Jianhua <toqsxw@outlook.com>
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

%macro PARAM_SHUFFE 1
%assign i (%1  * 2)
%assign j ((i + 1) << 8) + (i)
param_shuffe_ %+ %1:
%rep 2
    times 4 dw j
    times 4 dw (j + 0x0808)
%endrep
%endmacro

PARAM_SHUFFE 0
PARAM_SHUFFE 1
PARAM_SHUFFE 2
PARAM_SHUFFE 3

dd448: times 8             dd 512 - 64
dw64: times 8              dd 64

SECTION .text


%define ALF_NUM_COEFF_LUMA      12
%define ALF_NUM_COEFF_CHROMA     6
%define ALF_NUM_COEFF_CC         7

;%1-%3 out
;%4 clip or filter
%macro LOAD_LUMA_PARAMS_W16 4
    lea                 offsetq, [3 * xq]                       ;xq * ALF_NUM_COEFF_LUMA / ALF_BLOCK_SIZE
    movu                    m%1, [%4q + 2 * offsetq + 0 * 32]   ; 2 * for sizeof(int16_t)
    movu                    m%2, [%4q + 2 * offsetq + 1 * 32]
    movu                    m%3, [%4q + 2 * offsetq + 2 * 32]
%endmacro

%macro LOAD_LUMA_PARAMS_W16 6
    LOAD_LUMA_PARAMS_W16    %1, %2, %3, %4
    ;m%1 = 03 02 01 00
    ;m%2 = 07 06 05 04
    ;m%3 = 11 10 09 08

    vshufpd                 m%5, m%1, m%2, 0011b        ;06 02 05 01
    vshufpd                 m%6, m%3, m%5, 1001b        ;06 10 01 09

    vshufpd                 m%1, m%1, m%6, 1100b        ;06 03 09 00
    vshufpd                 m%2, m%2, m%6, 0110b        ;10 07 01 04
    vshufpd                 m%3, m%3, m%5, 0110b        ;02 11 05 08

    vpermpd                 m%1, m%1, 01111000b         ;09 06 03 00
    vshufpd                 m%2, m%2, m%2, 1001b        ;10 07 04 01
    vpermpd                 m%3, m%3, 10000111b         ;11 08 05 02
%endmacro

; %1-%3 out
; %4    clip or filter
; %5-%6 tmp
%macro LOAD_LUMA_PARAMS 6
    LOAD_LUMA_PARAMS_W16 %1, %2, %3, %4, %5, %6
%endmacro

%macro LOAD_CHROMA_PARAMS 4
    ; LOAD_CHROMA_PARAMS_W %+ WIDTH %1, %2, %3, %4
    movq                   xm%1, [%3q]
    movd                   xm%2, [%3q + 8]
    vpbroadcastq            m%1, xm%1
    vpbroadcastq            m%2, xm%2
%endmacro

%macro LOAD_PARAMS 0
%if LUMA
    LOAD_LUMA_PARAMS          3, 4, 5, filter, 6, 7
    LOAD_LUMA_PARAMS          6, 7, 8, clip,   9, 10
%else
    LOAD_CHROMA_PARAMS        3, 4, filter, 5
    LOAD_CHROMA_PARAMS        6, 7, clip, 8
%endif
%endmacro

; FILTER(param_idx)
; input:   m2, m9, m10
; output:  m0, m1
; tmp:     m11-m13
%macro FILTER 1
    %assign i (%1 % 4)
    %assign j (%1 / 4 + 3)
    %assign k (%1 / 4 + 6)
    %define filters m %+ j
    %define clips m %+ k

    pshufb           m12, clips, [param_shuffe_ %+ i]        ;clip
    pxor             m11, m11
    psubw            m11, m12                                ;-clip

    vpsubw            m9, m2
    CLIPW             m9, m11, m12

    vpsubw           m10, m2
    CLIPW            m10, m11, m12

    vpunpckhwd       m13, m9, m10
    vpunpcklwd        m9, m9, m10

    pshufb           m12, filters, [param_shuffe_ %+ i]       ;filter
    vpunpcklwd       m10, m12, m12
    vpunpckhwd       m12, m12, m12

    vpmaddwd          m9, m10
    vpmaddwd         m12, m13

    paddd             m0, m9
    paddd             m1, m12
%endmacro

; FILTER(param_idx, bottom, top, byte_offset)
; input:  param_idx, bottom, top, byte_offset
; output: m0, m1
; temp:   m9, m10
%macro FILTER 4
    LOAD_PIXELS      m10, [%2 + %4]
    LOAD_PIXELS       m9,  [%3 - %4]
    FILTER  %1
%endmacro

; GET_SRCS(line)
; brief:  get source lines
; input:  src, src_stride, vb_pos
; output: s1...s6
%macro GET_SRCS 1
    lea              s1q, [srcq + src_strideq]
    lea              s3q, [s1q  + src_strideq]
%if LUMA
    lea              s5q, [s3q  + src_strideq]
%endif
    neg      src_strideq
    lea              s2q, [srcq + src_strideq]
    lea              s4q, [s2q  + src_strideq]
%if LUMA
    lea              s6q, [s4q  + src_strideq]
%endif
    neg      src_strideq

%if LUMA
    cmp          vb_posq, 0
    je       %%vb_bottom
    cmp          vb_posq, 4
    jne         %%vb_end
%else
    cmp          vb_posq, 2
    jne         %%vb_end
    cmp               %1, 2
    jge      %%vb_bottom
%endif

%%vb_above:
    ; above
    ; p1 = (y + i == vb_pos - 1) ? p0 : p1;
    ; p2 = (y + i == vb_pos - 1) ? p0 : p2;
    ; p3 = (y + i >= vb_pos - 2) ? p1 : p3;
    ; p4 = (y + i >= vb_pos - 2) ? p2 : p4;
    ; p5 = (y + i >= vb_pos - 3) ? p3 : p5;
    ; p6 = (y + i >= vb_pos - 3) ? p4 : p6;
    dec          vb_posq
    cmp          vb_posq, %1
    cmove            s1q, srcq
    cmove            s2q, srcq

    dec          vb_posq
    cmp          vb_posq, %1
    cmovbe           s3q, s1q
    cmovbe           s4q, s2q

    dec          vb_posq
%if LUMA
    cmp          vb_posq, %1
    cmovbe           s5q, s3q
    cmovbe           s6q, s4q
%endif
    add          vb_posq, 3
    jmp         %%vb_end

%%vb_bottom:
    ; bottom
    ; p1 = (y + i == vb_pos    ) ? p0 : p1;
    ; p2 = (y + i == vb_pos    ) ? p0 : p2;
    ; p3 = (y + i <= vb_pos + 1) ? p1 : p3;
    ; p4 = (y + i <= vb_pos + 1) ? p2 : p4;
    ; p5 = (y + i <= vb_pos + 2) ? p3 : p5;
    ; p6 = (y + i <= vb_pos + 2) ? p4 : p6;
    cmp          vb_posq, %1
    cmove            s1q, srcq
    cmove            s2q, srcq

    inc          vb_posq
    cmp          vb_posq, %1
    cmovae           s3q, s1q
    cmovae           s4q, s2q

    inc          vb_posq
%if LUMA
    cmp          vb_posq, %1
    cmovae           s5q, s3q
    cmovae           s6q, s4q
%endif
    sub          vb_posq, 2
%%vb_end:
%endmacro

; SHIFT_VB(line)
; brief: shift filter result
; input:  m0, m1, vb_pos
; output: m0
; temp:   m9
%macro SHIFT_VB 1
%define SHIFT 7
%if LUMA
    cmp               %1, 3
    je      %%near_above
    cmp               %1, 0
    je      %%near_below
    jmp          %%no_vb
    %%near_above:
        cmp      vb_posq, 4
        je     %%near_vb
        jmp      %%no_vb
    %%near_below:
        cmp      vb_posq, 0
        je     %%near_vb
%else
    cmp               %1, 0
    je           %%no_vb
    cmp               %1, 3
    je           %%no_vb
    cmp          vb_posq, 2
    je         %%near_vb
%endif
%%no_vb:
    vpsrad            m0, SHIFT
    vpsrad            m1, SHIFT
    jmp      %%shift_end
%%near_vb:
    vpbroadcastd      m9, [dd448]
    paddd             m0, m9
    paddd             m1, m9
    vpsrad            m0, SHIFT + 3
    vpsrad            m1, SHIFT + 3
%%shift_end:
    vpackssdw         m0, m0, m1
%endmacro

; FILTER_VB(line)
; brief: filter pixels for luma and chroma
; input:  line
; output: m0, m1
; temp:   s0q...s1q
%macro FILTER_VB 1
    vpbroadcastd      m0, [dw64]
    vpbroadcastd      m1, [dw64]

    GET_SRCS %1
%if LUMA
    FILTER         0,  s5q,  s6q,  0 * ps
    FILTER         1,  s3q,  s4q,  1 * ps
    FILTER         2,  s3q,  s4q,  0 * ps
    FILTER         3,  s3q,  s4q, -1 * ps
    FILTER         4,  s1q,  s2q,  2 * ps
    FILTER         5,  s1q,  s2q,  1 * ps
    FILTER         6,  s1q,  s2q,  0 * ps
    FILTER         7,  s1q,  s2q, -1 * ps
    FILTER         8,  s1q,  s2q, -2 * ps
    FILTER         9, srcq, srcq,  3 * ps
    FILTER        10, srcq, srcq,  2 * ps
    FILTER        11, srcq, srcq,  1 * ps
%else
    FILTER         0,  s3q,  s4q,  0 * ps
    FILTER         1,  s1q,  s2q,  1 * ps
    FILTER         2,  s1q,  s2q,  0 * ps
    FILTER         3,  s1q,  s2q, -1 * ps
    FILTER         4, srcq, srcq,  2 * ps
    FILTER         5, srcq, srcq,  1 * ps
%endif
    SHIFT_VB %1
%endmacro

; LOAD_PIXELS(dest, src)
%macro LOAD_PIXELS 2
%if ps == 2
    movu      %1, %2
%else
    vpmovzxbw %1, %2
%endif
%endmacro

; STORE_PIXELS(dst, src)
%macro STORE_PIXELS 2
    %if ps == 2
        movu         %1, m%2
    %else
        vpackuswb   m%2, m%2
        vpermq      m%2, m%2, 0x8
        movu         %1, xm%2
    %endif
%endmacro

%macro FILTER_16x4 0
%if LUMA
    push clipq
    push strideq
    %define s1q clipq
    %define s2q strideq
%else
    %define s1q s5q
    %define s2q s6q
%endif

    %define s3q pixel_maxq
    %define s4q offsetq
    push xq

    xor               xq, xq
%%filter_16x4_loop:
    LOAD_PIXELS       m2, [srcq]   ;p0

    FILTER_VB         xq

    paddw             m0, m2

    ; clip to pixel
    CLIPW             m0, m14, m15

    STORE_PIXELS  [dstq], 0

    lea             srcq, [srcq + src_strideq]
    lea             dstq, [dstq + dst_strideq]
    inc               xq
    cmp               xq, 4
    jl %%filter_16x4_loop

    mov               xq, src_strideq
    neg               xq
    lea             srcq, [srcq + xq * 4]
    mov               xq, dst_strideq
    neg               xq
    lea             dstq, [dstq + xq * 4]

    pop xq

%if LUMA
    pop strideq
    pop clipq
%endif
%endmacro

; FILTER(bpc, luma/chroma)
%macro ALF_FILTER 2
%xdefine BPC   %1
%ifidn %2, luma
    %xdefine LUMA 1
%else
    %xdefine LUMA 0
%endif

; ******************************
; void vvc_alf_filter_%2_%1bpc_avx2(uint8_t *dst, ptrdiff_t dst_stride,
;      const uint8_t *src, ptrdiff_t src_stride, const ptrdiff_t width, cosnt ptr_diff_t height,
;      const int16_t *filter, const int16_t *clip, ptrdiff_t stride, ptrdiff_t vb_pos, ptrdiff_t pixel_max);
; ******************************
cglobal vvc_alf_filter_%2_%1bpc, 11, 15, 16, 0-0x28, dst, dst_stride, src, src_stride, width, height, filter, clip, stride, vb_pos, pixel_max, \
    offset, x, s5, s6
%define ps (%1 / 8) ; pixel size
    movd            xm15, pixel_maxd
    vpbroadcastw     m15, xm15
    pxor             m14, m14

.loop:
    push            srcq
    push            dstq
    xor               xd, xd

    .loop_w:
        LOAD_PARAMS
        FILTER_16x4

        add         srcq, 16 * ps
        add         dstq, 16 * ps
        add           xd, 16
        cmp           xd, widthd
        jl       .loop_w

    pop             dstq
    pop             srcq
    lea             srcq, [srcq + 4 * src_strideq]
    lea             dstq, [dstq + 4 * dst_strideq]

    lea          filterq, [filterq + 2 * strideq]
    lea            clipq, [clipq + 2 * strideq]

    sub          vb_posq, 4
    sub          heightq, 4
    jg             .loop
    RET
%endmacro

; FILTER(bpc)
%macro ALF_FILTER 1
    ALF_FILTER  %1, luma
    ALF_FILTER  %1, chroma
%endmacro

%if ARCH_X86_64
%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
ALF_FILTER   16
ALF_FILTER   8
%endif
%endif
