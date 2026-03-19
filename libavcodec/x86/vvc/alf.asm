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

CLASSIFY_SHUFFE: times 2    db 2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13
ARG_VAR_SHUFFE: times 2     db 0, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4

cextern pd_64
dd448: times 8             dd 512 - 64
dw3:  times 8              dd 3
dw5:  times 8              dd 5
dd15: times 8              dd 15

SECTION .text


%define ALF_NUM_COEFF_LUMA      12
%define ALF_NUM_COEFF_CHROMA     6
%define ALF_NUM_COEFF_CC         7

;%1-%3 out
;%4 clip or filter
%macro LOAD_LUMA_PARAMS 4
    movu                    m%1, [%4q + 0 * mmsize]
    movu                    m%2, [%4q + 1 * mmsize]
    movu                    m%3, [%4q + 2 * mmsize]
    ; we process mmsize/(2*ALF_BLOCK_SIZE) alf blocks,
    ; consuming ALF_NUM_COEFF_LUMA int16_t coeffs per alf block
    add                     %4q, 3 * mmsize
%endmacro

%macro LOAD_LUMA_PARAMS_W16 6
    LOAD_LUMA_PARAMS        %1, %2, %3, %4
    ;m%1 = 03 02 01 00
    ;m%2 = 07 06 05 04
    ;m%3 = 11 10 09 08

    shufpd                  m%5, m%1, m%2, 0011b        ;06 02 05 01
    shufpd                  m%6, m%3, m%5, 1001b        ;06 10 01 09

    shufpd                  m%1, m%1, m%6, 1100b        ;06 03 09 00
    shufpd                  m%2, m%2, m%6, 0110b        ;10 07 01 04
    shufpd                  m%3, m%3, m%5, 0110b        ;02 11 05 08

    vpermpd                 m%1, m%1, 01111000b         ;09 06 03 00
    shufpd                  m%2, m%2, m%2, 1001b        ;10 07 04 01
    vpermpd                 m%3, m%3, 10000111b         ;11 08 05 02
%endmacro

%macro LOAD_LUMA_PARAMS_W8 5
    LOAD_LUMA_PARAMS       %2, %3, %5, %4
    ;m%2 = 01 00
    ;m%3 = 03 02
    ;m%5 = 05 04

    shufpd                  m%1, m%2, m%3, 10b          ;03 00
    shufpd                  m%2, m%2, m%5, 01b          ;04 01
    shufpd                  m%3, m%3, m%5, 10b          ;05 02
%endmacro

; %1-%3 out
; %4    clip or filter
; %5-%6 tmp
%macro LOAD_LUMA_PARAMS 6
%if mmsize == 32
    LOAD_LUMA_PARAMS_W16 %1, %2, %3, %4, %5, %6
%else
    LOAD_LUMA_PARAMS_W8  %1, %2, %3, %4, %5
%endif
%endmacro

%macro LOAD_CHROMA_PARAMS 4
    ; LOAD_CHROMA_PARAMS_W %+ WIDTH %1, %2, %3, %4
    vpbroadcastq            m%1, [%3q]
    movd                   xm%2, [%3q + 8]
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

    psubw             m9, m2
    CLIPW             m9, m11, m12

    psubw            m10, m2
    CLIPW            m10, m11, m12

    punpckhwd        m13, m9, m10
    punpcklwd         m9, m9, m10

    pshufb           m12, filters, [param_shuffe_ %+ i]       ;filter
    punpcklwd        m10, m12, m12
    punpckhwd        m12, m12, m12

    pmaddwd           m9, m10
    pmaddwd          m12, m13

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
    cmp          vb_posd, 0
    je       %%vb_bottom
    cmp          vb_posd, 4
    jne         %%vb_end
%%vb_above:
    ; above: vb_pos == 4
    ; p1 = (y + i == vb_pos - 1) ? p0 : p1;
    ; p2 = (y + i == vb_pos - 1) ? p0 : p2;
    ; p3 = (y + i >= vb_pos - 2) ? p1 : p3;
    ; p4 = (y + i >= vb_pos - 2) ? p2 : p4;
    ; p5 = (y + i >= vb_pos - 3) ? p3 : p5;
    ; p6 = (y + i >= vb_pos - 3) ? p4 : p6;
    cmp               %1, 3
    cmove            s1q, srcq
    cmove            s2q, srcq

    cmp               %1, 1
    cmova            s3q, s1q
    cmova            s4q, s2q

    cmovae           s5q, s3q
    cmovae           s6q, s4q
    jmp         %%vb_end

%%vb_bottom:
    ; bottom: vb_pos == 0
    ; p1 = (y + i == vb_pos    ) ? p0 : p1;
    ; p2 = (y + i == vb_pos    ) ? p0 : p2;
    ; p3 = (y + i <= vb_pos + 1) ? p1 : p3;
    ; p4 = (y + i <= vb_pos + 1) ? p2 : p4;
    ; p5 = (y + i <= vb_pos + 2) ? p3 : p5;
    ; p6 = (y + i <= vb_pos + 2) ? p4 : p6;
    cmp               %1, 0
    cmove            s1q, srcq
    cmove            s2q, srcq

    cmp               %1, 2
    cmovb            s3q, s1q
    cmovb            s4q, s2q

    cmovbe           s5q, s3q
    cmovbe           s6q, s4q
%else ; chroma
    cmp          vb_posd, 2
    jne         %%vb_end
    cmp               %1, 2
    jge      %%vb_bottom
%%vb_above:
    cmp               %1, 1
%%vb_bottom:
    cmove            s1q, srcq
    cmove            s2q, srcq

    mov              s3q, s1q
    mov              s4q, s2q
%endif
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
        cmp      vb_posd, 4
        je     %%near_vb
        jmp      %%no_vb
    %%near_below:
        cmp      vb_posd, 0
        je     %%near_vb
%else
    cmp               %1, 0
    je           %%no_vb
    cmp               %1, 3
    je           %%no_vb
    cmp          vb_posd, 2
    je         %%near_vb
%endif
%%no_vb:
    psrad             m0, SHIFT
    psrad             m1, SHIFT
    jmp      %%shift_end
%%near_vb:
    vpbroadcastd      m9, [dd448]
    paddd             m0, m9
    paddd             m1, m9
    psrad             m0, SHIFT + 3
    psrad             m1, SHIFT + 3
%%shift_end:
    packssdw          m0, m0, m1
%endmacro

; FILTER_VB(line)
; brief: filter pixels for luma and chroma
; input:  line
; output: m0, m1
; temp:   s0q...s1q
%macro FILTER_VB 1
    vpbroadcastd      m0, [pd_64]
    vpbroadcastd      m1, [pd_64]

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
    pmovzxbw  %1, %2
%endif
%endmacro

; STORE_PIXELS_W16(dst, src)
%macro STORE_PIXELS_W16 2
    %if ps == 2
        movu       [%1],  m%2
    %else
        movu       [%1], xm%2
    %endif
%endmacro

%macro STORE_PIXELS_W8 2
    %if ps == 2
        movu       [%1], xm%2
    %else
        movq       [%1], xm%2
    %endif
%endmacro

; STORE_PIXELS_W4(dst, src, offset)
%macro STORE_PIXELS_W4 3
    %if ps == 2
        movq   [%1 + %3 * ps], xm%2
    %else
        movd        [%1 + %3], xm%2
    %endif
%endmacro

%macro STORE_PIXELS_W8LE 3
    cmp %3, 8
    jl .w4
    STORE_PIXELS_W8 %1, %2
    je .end
    vextracti128    xm%2, m%2, 1
    STORE_PIXELS_W4 %1, %2, 8
    jmp .end
.w4:
    STORE_PIXELS_W4 %1, %2, 0
.end:
%endmacro

; STORE_PIXELS(dst, src, width, tmp reg)
%macro STORE_PIXELS 4
    %ifidn %3, 16
        %if ps == 1
            vextracti128 xm%4, m%2, 1
            packuswb     xm%2, xm%4
        %endif
        STORE_PIXELS_W16  %1, %2
    %else
        %if LUMA
            %if ps == 1
                packuswb     xm%2, xm%2
            %endif
            STORE_PIXELS_W8   %1, %2
        %else
            %if ps == 1
                packuswb      m%2, m%2
            %endif
            STORE_PIXELS_W8LE %1, %2, %3
        %endif
    %endif
%endmacro

%macro FILTER_16x4 2
%if LUMA
    push clipq
    %define s6q clipq
%endif

    xor               xd, xd
%%filter_16x4_loop:
    LOAD_PIXELS       m2, [srcq]   ;p0

    FILTER_VB         xd

    ; sum += curr
    paddsw             m0, m2

%if ps != 1
    ; clip to pixel
    CLIPW             m0, m14, m15
%endif

    STORE_PIXELS    dstq, 0, %1, 2

    lea             srcq, [srcq + src_strideq]
    lea             dstq, [dstq + dst_strideq]
    inc               xd
    cmp               xd, 4
    jl %%filter_16x4_loop

%ifnidn %2, 0
    mov               xq, src_strideq
    neg               xq
    lea             srcq, [srcq + xq * 4 + %2]
    mov               xq, dst_strideq
    neg               xq
    lea             dstq, [dstq + xq * 4 + %2]
%endif

%if LUMA
    pop clipq
%endif
%endmacro

; FILTER(bd, luma/chroma, bd of implementation to use)
%macro ALF_FILTER 3
%ifidn %2, luma
    %xdefine LUMA 1
%else
    %xdefine LUMA 0
%endif
%assign ps (%1+7) / 8 ; pixel size

; ******************************
; void ff_vvc_alf_filter_%2_%1_avx2(uint8_t *dst, ptrdiff_t dst_stride,
;      const uint8_t *src, ptrdiff_t src_stride, int width, int height,
;      const int16_t *filter, const int16_t *clip, int vb_pos);
; ******************************
cglobal vvc_alf_filter_%2_%1
%if !LUMA
; chroma does not use registers m5 and m8. Swap them to reduce the amount
; of nonvolatile registers on Win64. It also reduces codesize generally
; as encodings with high registers (m8-m15) take more bytes.
    %if ps != 1
        SWAP 5,15
        SWAP 8,14
    %else
        SWAP 5,12
        SWAP 8,13
    %endif
%elif WIN64 && (ps != 1)
; Swap m5 and m15, so that the register for the maximum pixel value
; ends up in a volatile register
    SWAP 5,15
%endif
%if ps != 1
  ; create pw_pixelmax for clipping
  pcmpeqw         m15, m15
  psrlw           m15, 16 - %1
%endif

%if %1 != %3
    jmp vvc_alf_filter_%2_%3_prologue
%else
vvc_alf_filter_%2_%1_prologue:
    PROLOGUE 9, 14+LUMA, 12+2*(ps!=1)+2*LUMA, dst, dst_stride, src, src_stride, width, height, filter, clip, vb_pos, \
    x, s1, s2, s3, s4, s5
%if ps != 1
    pxor             m14, m14
%endif

.loop:
    push            srcq
    push            dstq
    push          widthq

    .loop_w:
        cmp       widthd, 16
        jl   .loop_w_end

        LOAD_PARAMS
        FILTER_16x4   16, 16 * ps

        sub       widthd, 16
        jmp      .loop_w

.loop_w_end:
    cmp           widthd, 0
    je            .w_end

%if LUMA
SAVE_MM_PERMUTATION
INIT_XMM cpuname
LOAD_MM_PERMUTATION
%endif
    LOAD_PARAMS
    FILTER_16x4  widthd, 0
%if LUMA
INIT_YMM cpuname
%endif

.w_end:

    pop           widthq
    pop             dstq
    pop             srcq
    lea             srcq, [srcq + 4 * src_strideq]
    lea             dstq, [dstq + 4 * dst_strideq]

    sub          vb_posd, 4
    sub          heightd, 4
    jg             .loop
    RET
%endif
%endmacro

; FILTER(bd, bd of implementation to use)
%macro ALF_FILTER 2
    ALF_FILTER  %1, luma,   %2
    ALF_FILTER  %1, chroma, %2
%endmacro

%define ALF_GRADIENT_BORDER 2
%define ALF_BORDER_LUMA     3

; ******************************
; void ff_vvc_alf_classify_grad(int *gradient_sum, const uint8_t *src,
;      ptrdiff_t src_stride,  intptr_t width, intptr_t height, intptr_t vb_pos);
; ******************************
%macro ALF_CLASSIFY_GRAD 1
cglobal vvc_alf_classify_grad_%1bpc, 6, 14, 12, gradient_sum, src, src_stride, width, height, vb_pos, \
    x, y, s0, s1, s2, s3, vb_pos_below, src_stride3

    lea         src_stride3q, [src_strideq * 2 + src_strideq]

    lea        vb_pos_belowd, [vb_posd + ALF_GRADIENT_BORDER]

    ; src = src - ALF_BORDER_LUMA * src_stride - ALF_BORDER_LUMA
    sub                 srcq, src_stride3q
    sub                 srcq, ALF_BORDER_LUMA * ps

    add               widthd, ALF_GRADIENT_BORDER * 2
    add              heightd, ALF_GRADIENT_BORDER * 2

    xor                   yd, yd

.loop_h:
    xor                   xd,  xd
    pxor                xm11, xm11 ; prev
    .loop_w:
        lea              s0q, [srcq + xq * ps]
        lea              s1q, [s0q + src_strideq]
        lea              s2q, [s0q + 2 * src_strideq]
        lea              s3q, [s0q + src_stride3q]

        cmp               yd, vb_pos_belowd
        cmove            s0q, s1q

        cmp               yd, vb_posd
        cmove            s3q, s2q

        LOAD_PIXELS       m0, [s0q]
        LOAD_PIXELS       m1, [s1q]
        LOAD_PIXELS       m2, [s2q]
        LOAD_PIXELS       m3, [s3q]

        LOAD_PIXELS       m4, [s0q + 2 * ps]
        LOAD_PIXELS       m5, [s1q + 2 * ps]
        LOAD_PIXELS       m6, [s2q + 2 * ps]
        LOAD_PIXELS       m7, [s3q + 2 * ps]

        pblendw           m8, m0, m1, 0xaa             ; nw
        pblendw           m9, m0, m5, 0x55             ; n
        pblendw           m4, m4, m5, 0xaa             ; ne
        pblendw          m10, m1, m2, 0xaa             ; w
        pblendw           m5, m5, m6, 0xaa             ; e
        pblendw           m3, m2, m3, 0xaa             ; sw
        pblendw           m2, m2, m7, 0x55             ; s

        pblendw           m0, m1, m6, 0x55
        paddw             m0, m0                       ; c

        pshufb            m1, m0, [CLASSIFY_SHUFFE]    ; d

        paddw             m9, m2                       ; n + s
        psubw             m9, m0                       ; (n + s) - c
        pabsw             m9, m9                       ; ver

        paddw             m5, m10                      ; w + e
        psubw             m5, m1                       ; (w + e) - d
        pabsw             m5, m5                       ; hor

        pblendw           m6, m6, m7, 0xaa             ; se
        paddw             m8, m6                       ; nw + se
        psubw             m8, m1                       ; (nw + se) - d
        pabsw             m8, m8                       ; di0

        paddw             m4, m3                       ; ne + sw
        psubw             m4, m1                       ; (nw + se) - d
        pabsw             m4, m4                       ; di1

        phaddw            m9, m5                       ; vh,  each word represent 2x2 pixels
        phaddw            m8, m4                       ; di,  each word represent 2x2 pixels
        phaddw            m0,  m9, m8                  ; all = each word represent 4x2 pixels, order is v_h_d0_d1 x 4

        vinserti128      m11, m11, xm0, 1
        pblendw           m1,  m0, m11, 0xaa           ; t

        phaddw            m1,  m0                      ; each word represent 8x2 pixels, adjacent word share 4x2 pixels

        vextracti128    xm11, m0, 1                    ; prev

        movu [gradient_sumq], m1

        add    gradient_sumq, 32
        add               xd, 16
        cmp               xd, widthd
        jl           .loop_w

    lea                 srcq, [srcq + 2 * src_strideq]
    add                   yd, 2
    cmp                   yd, heightd
    jl               .loop_h
    RET
%endmacro

; SAVE_CLASSIFY_PARAM_W16(dest, src)
%macro SAVE_CLASSIFY_PARAM_W16 2
    vpermq                  m%2, m%2, 11011000b
    lea                   tempq, [%1q + xq]
    movu                [tempq], xm%2
    vextracti128           xm%2, m%2, 1
    movu       [tempq + widthq], xm%2
%endmacro

; SAVE_CLASSIFY_PARAM_W8
%macro SAVE_CLASSIFY_PARAM_W8 2
    movq                   [%1], xm%2
    movhps        [%1 + widthq], xm%2
%endmacro

; SAVE_CLASSIFY_PARAM_W4
%macro SAVE_CLASSIFY_PARAM_W4 2
    movd                   [%1], xm%2
    punpckhqdq             xm%2, xm%2
    movd          [%1 + widthq], xm%2
%endmacro

; SAVE_CLASSIFY_PARAM_W(dest, src)
%macro SAVE_CLASSIFY_PARAM_W 2
    lea                  tempq, [%1q + xq]
    cmp                     wd, 8
    jl %%w4
    SAVE_CLASSIFY_PARAM_W8 tempq, %2
    je                   %%end
    vextracti128          xm%2, m%2, 1
    add                  tempq, 8
%%w4:
    SAVE_CLASSIFY_PARAM_W4 tempq, %2
%%end:
%endmacro

%macro ALF_CLASSIFY_H8 0
    ; first line, sum of 16x4 pixels (includes borders)
    lea            gradq, [gradient_sumq + 2 * xq]
    movu              m0, [gradq]
    movu              m1, [gradq + sum_strideq]
    movu              m2, [gradq + 2 * sum_strideq]

    movd             xm8, yd
    movd             xm4, vb_posd
    pcmpeqb          xm5, xm5
    pcmpeqd          xm8, xm4      ; y == vb_pos
    pxor             xm8, xm5      ; y != vb_pos
    vpbroadcastd      m8, xm8

    vpbroadcastd      m9, [dw3]
    paddd             m9, m8        ; ac = (y != vb_pos) ? 2 : 3

    pblendvb          m3, m10, [gradq + sum_stride3q], m8

    ; extent to dword to avoid overflow
    punpckhwd         m4, m0, m10
    punpcklwd         m0, m0, m10
    punpckhwd         m5, m1, m10
    punpcklwd         m1, m1, m10
    punpckhwd         m6, m2, m10
    paddd             m4, m5
    punpcklwd         m5, m2, m10
    punpckhwd         m7, m3, m10
    punpcklwd         m3, m3, m10

    paddd             m0, m1
    paddd             m6, m7
    paddd             m5, m3

    ; sum of the first row
    paddd             m0, m0, m5         ; low
    paddd             m1, m4, m6         ; high

    lea            gradq, [gradq + 2 * sum_strideq]

    pblendvb          m2, m10, m2, m8

    movu              m3, [gradq + sum_strideq]
    movu              m4, [gradq + 2 * sum_strideq]
    movu              m5, [gradq + sum_stride3q]

    punpckhwd         m6, m2, m10
    punpcklwd         m2, m2, m10
    punpckhwd         m7, m3, m10
    punpcklwd         m3, m3, m10
    punpckhwd         m8, m4, m10
    punpcklwd         m4, m4, m10
    paddd             m6, m7
    punpckhwd         m7, m5, m10
    punpcklwd         m5, m5, m10

    paddd             m2, m3
    paddd             m8, m7
    paddd             m4, m5

    ; sum of the second row
    paddd             m2, m2, m4         ; low
    paddd             m3, m8, m6         ; high

    punpckldq         m4, m0, m2
    punpckhdq         m5, m0, m2
    punpckldq         m6, m1, m3
    punpckhdq         m7, m1, m3

    ; each dword represent 4x2 alf blocks
    ; the order is 01452367
    punpckldq         m0, m4, m6         ; sum_v
    punpckhdq         m1, m4, m6         ; sum_h
    punpckldq         m2, m5, m7         ; sum_d0
    punpckhdq         m3, m5, m7         ; sum_d1

    pcmpgtd           m4, m0, m1         ; dir_hv - 1
    pmaxsd            m5, m0, m1         ; hv1
    pminsd            m6, m0, m1         ; hv0

    paddd             m0, m1;            ; sum_hv

    pcmpgtd           m7, m2, m3         ; dir_d - 1
    pmaxsd            m8, m2, m3         ; d1
    pminsd            m3, m2, m3         ; d0

    ; *transpose_idx = dir_d * 2 + dir_hv;
    vpbroadcastd      m1, [dw3]
    paddd             m7, m7
    paddd             m7, m4
    paddd             m7, m1
    SAVE_CLASSIFY_PARAM transpose_idx, 7

    psrlq             m1, m8, 32
    psrlq             m2, m6, 32
    pmuldq            m4, m1, m2         ; d1 * hv0 high
    psrlq             m1, m3, 32
    psrlq             m2, m5, 32
    pmuldq            m7, m1, m2         ; d0 * hv1 high
    pcmpgtq           m7, m4, m7         ; dir1 - 1 high

    pmuldq            m1, m8, m6         ; d1 * hv0 low
    pmuldq            m2, m3, m5         ; d0 * hv1 low
    pcmpgtq           m1, m2             ; dir1 - 1 low

    vpblendd          m1, m1, m7, 0xaa   ; dir1 - 1

    pblendvb          m2, m5, m8, m1     ; hvd1
    pblendvb          m3, m6, m3, m1     ; hvd0

    ;*class_idx = arg_var[av_clip_uintp2(sum_hv * ac >> (BIT_DEPTH - 1), 4)];
    pmulld            m0, m9             ; sum_hv * ac
%if ps != 1
    vpsrlvd           m0, m0, m11
%else
    psrld             m0, 7
%endif
    pminsd            m0, [dd15]
    movu              m6, [ARG_VAR_SHUFFE]
    pshufb            m6, m0             ; class_idx

    vpbroadcastd      m0, [dw5]

    ; if (hvd1 * 2 > 9 * hvd0)
    ;   *class_idx += ((dir1 << 1) + 2) * 5;
    ; else if (hvd1 > 2 * hvd0)
    ;   *class_idx += ((dir1 << 1) + 1) * 5;
    paddd             m7,  m3, m3
    pcmpgtd           m7,  m2, m7        ; hvd1 > 2 * hvd0
    pand              m7, m0
    paddd             m6,  m7            ; class_idx

    paddd             m8, m2, m2
    pslld             m2, m3, 3
    paddd             m2, m3
    pcmpgtd           m8, m2             ; hvd1 * 2 > 9 * hvd0
    pand              m8, m0
    paddd             m6, m8             ; class_idx

    pandn             m1, m7
    paddd             m1, m1             ; dir1 << 1
    paddd             m6, m1             ; class_idx

    SAVE_CLASSIFY_PARAM class_idx, 6
%endmacro

%macro ALF_CLASSIFY_16x8 0
%define SAVE_CLASSIFY_PARAM SAVE_CLASSIFY_PARAM_W16
    ALF_CLASSIFY_H8
%undef SAVE_CLASSIFY_PARAM
%endmacro

%macro ALF_CLASSIFY_Wx8 0
%define SAVE_CLASSIFY_PARAM SAVE_CLASSIFY_PARAM_W
    ALF_CLASSIFY_H8
%undef SAVE_CLASSIFY_PARAM
%endmacro

; ******************************
;void ff_vvc_alf_classify(int *class_idx, int *transpose_idx, const int *gradient_sum,
;      intptr_t width, intptr_t height, intptr_t vb_pos, int *gradient_tmp, intptr_t bit_depth);
; ******************************
%macro ALF_CLASSIFY 1
%define ps (%1 / 8)
ALF_CLASSIFY_GRAD %1
cglobal vvc_alf_classify_%1bpc, 7, 14, 11+(ps!=1), class_idx, transpose_idx, gradient_sum, width, height, vb_pos, bit_depth, \
    x, y, grad, sum_stride, sum_stride3, temp, w

%if ps != 1
    sub       bit_depthd, 1
    movd            xm11, bit_depthd
    vpbroadcastd     m11, xm11
%endif

    ; now we can use gradient to get class idx and transpose idx
    lea      sum_strideq, [widthd + ALF_GRADIENT_BORDER * 2]
    add      sum_strideq, 15
    and      sum_strideq, ~15               ; align to 16
    add      sum_strideq, sum_strideq       ; two rows a time

    add    gradient_sumq, 8                 ; first 4 words are garbage

    lea     sum_stride3q, [3 * sum_strideq]

    xor               yd, yd
    and          vb_posd, ~7                ; floor align to 8
    pxor            xm10, xm10

.loop_sum_h:
    xor               xd,  xd
    .loop_sum_w16:
        lea           wd, [widthd]
        sub           wd, xd
        cmp           wd, 16
        jl .loop_sum_w16_end

        ALF_CLASSIFY_16x8

        add           xd, 16
        jmp .loop_sum_w16
    .loop_sum_w16_end:

    cmp               wd, 0
    je   .loop_sum_w_end

    ALF_CLASSIFY_Wx8

.loop_sum_w_end:
    lea    gradient_sumq, [gradient_sumq + 4 * sum_strideq]
    lea   transpose_idxq, [transpose_idxq + 2 * widthq]
    lea       class_idxq, [class_idxq + 2 * widthq]

    add               yd, 8
    cmp               yd, heightd
    jl        .loop_sum_h

    RET
%endmacro

%if ARCH_X86_64
%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
ALF_FILTER   12, 10
ALF_FILTER   10, 10
ALF_CLASSIFY 16
ALF_FILTER   8,  8
ALF_CLASSIFY 8
%endif
%endif
