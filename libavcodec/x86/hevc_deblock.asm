;*****************************************************************************
;* SSE2-optimized HEVC deblocking code
;*****************************************************************************
;* Copyright (C) 2013 VTT
;*
;* Authors: Seppo Tomperi <seppo.tomperi@vtt.fi>
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

pw_pixel_max: times 8 dw ((1 << 10)-1)
pw_m2:        times 8 dw -2
pd_1 :        times 4 dd  1

cextern pw_4
cextern pw_8
cextern pw_m1

SECTION .text
INIT_XMM sse2

; expands to [base],...,[base+7*stride]
%define PASS8ROWS(base, base3, stride, stride3) \
    [base], [base+stride], [base+stride*2], [base3], \
    [base3+stride], [base3+stride*2], [base3+stride3], [base3+stride*4]

; in: 8 rows of 4 bytes in %4..%11
; out: 4 rows of 8 words in m0..m3
%macro TRANSPOSE4x8B_LOAD 8
    movd             m0, %1
    movd             m2, %2
    movd             m1, %3
    movd             m3, %4

    punpcklbw        m0, m2
    punpcklbw        m1, m3
    punpcklwd        m0, m1

    movd             m4, %5
    movd             m6, %6
    movd             m5, %7
    movd             m7, %8

    punpcklbw        m4, m6
    punpcklbw        m5, m7
    punpcklwd        m4, m5

    punpckhdq        m2, m0, m4
    punpckldq        m0, m4

    pxor             m5, m5
    punpckhbw        m1, m0, m5
    punpcklbw        m0, m5
    punpckhbw        m3, m2, m5
    punpcklbw        m2, m5
%endmacro

; in: 4 rows of 8 words in m0..m3
; out: 8 rows of 4 bytes in %1..%8
%macro TRANSPOSE8x4B_STORE 8
    packuswb         m0, m0
    packuswb         m1, m1
    packuswb         m2, m2
    packuswb         m3, m3

    punpcklbw        m0, m1
    punpcklbw        m2, m3

    punpckhwd        m6, m0, m2
    punpcklwd        m0, m2

    movd             %1, m0
    pshufd           m0, m0, 0x39
    movd             %2, m0
    pshufd           m0, m0, 0x39
    movd             %3, m0
    pshufd           m0, m0, 0x39
    movd             %4, m0

    movd             %5, m6
    pshufd           m6, m6, 0x39
    movd             %6, m6
    pshufd           m6, m6, 0x39
    movd             %7, m6
    pshufd           m6, m6, 0x39
    movd             %8, m6
%endmacro

; in: 8 rows of 4 words in %4..%11
; out: 4 rows of 8 words in m0..m3
%macro TRANSPOSE4x8W_LOAD 8
    movq             m0, %1
    movq             m2, %2
    movq             m1, %3
    movq             m3, %4

    punpcklwd        m0, m2
    punpcklwd        m1, m3
    punpckhdq        m2, m0, m1
    punpckldq        m0, m1

    movq             m4, %5
    movq             m6, %6
    movq             m5, %7
    movq             m7, %8

    punpcklwd        m4, m6
    punpcklwd        m5, m7
    punpckhdq        m6, m4, m5
    punpckldq        m4, m5

    punpckhqdq       m1, m0, m4
    punpcklqdq       m0, m4
    punpckhqdq       m3, m2, m6
    punpcklqdq       m2, m6

%endmacro

; in: 4 rows of 8 words in m0..m3
; out: 8 rows of 4 words in %1..%8
%macro TRANSPOSE8x4W_STORE 8
    pxor             m5, m5; zeros reg
    CLIPW            m0, m5, [pw_pixel_max]
    CLIPW            m1, m5, [pw_pixel_max]
    CLIPW            m2, m5, [pw_pixel_max]
    CLIPW            m3, m5, [pw_pixel_max]

    punpckhwd        m4, m0, m1
    punpcklwd        m0, m1
    punpckhwd        m5, m2, m3
    punpcklwd        m2, m3
    punpckhdq        m6, m0, m2
    punpckldq        m0, m2

    movq             %1, m0
    movhps           %2, m0
    movq             %3, m6
    movhps           %4, m6

    punpckhdq        m6, m4, m5
    punpckldq        m4, m5

    movq             %5, m4
    movhps           %6, m4
    movq             %7, m6
    movhps           %8, m6
%endmacro

; in: 8 rows of 8 bytes in %1..%8
; out: 8 rows of 8 words in m0..m7
%macro TRANSPOSE8x8B_LOAD 8
    movq             m7, %1
    movq             m2, %2
    movq             m1, %3
    movq             m3, %4

    punpcklbw        m7, m2
    punpcklbw        m1, m3
    punpcklwd        m3, m7, m1
    punpckhwd        m7, m1

    movq             m4, %5
    movq             m6, %6
    movq             m5, %7
    movq            m15, %8

    punpcklbw        m4, m6
    punpcklbw        m5, m15
    punpcklwd        m9, m4, m5
    punpckhwd        m4, m5

    punpckldq        m1, m3, m9;  0, 1
    punpckhdq        m3, m9;  2, 3

    punpckldq        m5, m7, m4;  4, 5
    punpckhdq        m7, m4;  6, 7

    pxor            m13, m13

    punpcklbw        m0, m1, m13; 0 in 16 bit
    punpckhbw        m1, m13; 1 in 16 bit

    punpcklbw        m2, m3, m13; 2
    punpckhbw        m3, m13; 3

    punpcklbw        m4, m5, m13; 4
    punpckhbw        m5, m13; 5

    punpcklbw        m6, m7, m13; 6
    punpckhbw        m7, m13; 7
%endmacro


; in: 8 rows of 8 words in m0..m8
; out: 8 rows of 8 bytes in %1..%8
%macro TRANSPOSE8x8B_STORE 8
    packuswb         m0, m0
    packuswb         m1, m1
    packuswb         m2, m2
    packuswb         m3, m3
    packuswb         m4, m4
    packuswb         m5, m5
    packuswb         m6, m6
    packuswb         m7, m7

    punpcklbw        m0, m1
    punpcklbw        m2, m3

    punpckhwd        m8, m0, m2
    punpcklwd        m0, m2

    punpcklbw        m4, m5
    punpcklbw        m6, m7

    punpckhwd        m9, m4, m6
    punpcklwd        m4, m6

    punpckhdq       m10, m0, m4; 2, 3
    punpckldq        m0, m4;   0, 1

    punpckldq       m11, m8, m9;  4, 5
    punpckhdq        m8, m9;   6, 7
    movq             %1, m0
    movhps           %2, m0
    movq             %3, m10
    movhps           %4, m10
    movq             %5, m11
    movhps           %6, m11
    movq             %7, m8
    movhps           %8, m8
%endmacro

; in: 8 rows of 8 words in %1..%8
; out: 8 rows of 8 words in m0..m7
%macro TRANSPOSE8x8W_LOAD 8
    movdqu           m0, %1
    movdqu           m1, %2
    movdqu           m2, %3
    movdqu           m3, %4
    movdqu           m4, %5
    movdqu           m5, %6
    movdqu           m6, %7
    movdqu           m7, %8
    TRANSPOSE8x8W     0, 1, 2, 3, 4, 5, 6, 7, 8
%endmacro

; in: 8 rows of 8 words in m0..m8
; out: 8 rows of 8 words in %1..%8
%macro TRANSPOSE8x8W_STORE 8
    TRANSPOSE8x8W     0, 1, 2, 3, 4, 5, 6, 7, 8

    pxor             m8, m8
    CLIPW            m0, m8, [pw_pixel_max]
    CLIPW            m1, m8, [pw_pixel_max]
    CLIPW            m2, m8, [pw_pixel_max]
    CLIPW            m3, m8, [pw_pixel_max]
    CLIPW            m4, m8, [pw_pixel_max]
    CLIPW            m5, m8, [pw_pixel_max]
    CLIPW            m6, m8, [pw_pixel_max]
    CLIPW            m7, m8, [pw_pixel_max]

    movdqu           %1, m0
    movdqu           %2, m1
    movdqu           %3, m2
    movdqu           %4, m3
    movdqu           %5, m4
    movdqu           %6, m5
    movdqu           %7, m6
    movdqu           %8, m7
%endmacro


; in: %2 clobbered
; out: %1
; mask in m11
; clobbers m10
%macro MASKED_COPY 2
    pand             %2, m11 ; and mask
    pandn           m10, m11, %1; and -mask
    por              %2, m10
    mova             %1, %2
%endmacro

; in: %2 clobbered
; out: %1
; mask in %3, will be clobbered
%macro MASKED_COPY2 3
    pand             %2, %3 ; and mask
    pandn            %3, %1; and -mask
    por              %2, %3
    mova             %1, %2
%endmacro

ALIGN 16
; input in m0 ... m3 and tcs in r2. Output in m1 and m2
%macro CHROMA_DEBLOCK_BODY 1
    psubw            m4, m2, m1; q0 - p0
    psubw            m5, m0, m3; p1 - q1
    psllw            m4, 2; << 2
    paddw            m5, m4;

    ;tc calculations
    movd             m6, [r2]; tc0
    add              r2, 4;
    punpcklwd        m6, m6
    movd             m7, [r2]; tc1
    punpcklwd        m7, m7
    shufps           m6, m7, 0; tc0, tc1
    pmullw           m4, m6, [pw_m1]; -tc0, -tc1
    ;end tc calculations

    paddw            m5, [pw_4]; +4
    psraw            m5, 3; >> 3

%if %1 > 8
    psllw            m4, %1-8; << (BIT_DEPTH - 8)
    psllw            m6, %1-8; << (BIT_DEPTH - 8)
%endif
    pmaxsw           m5, m4
    pminsw           m5, m6
    paddw            m1, m5; p0 + delta0
    psubw            m2, m5; q0 - delta0
%endmacro

; input in m0 ... m7, beta in r2 tcs in r3. Output in m1...m6
%macro LUMA_DEBLOCK_BODY 2
    psllw            m9, m2, 1; *2
    psubw           m10, m1, m9
    paddw           m10, m3
    ABS1            m10, m11 ; 0dp0, 0dp3 , 1dp0, 1dp3

    psllw            m9, m5, 1; *2
    psubw           m11, m6, m9
    paddw           m11, m4
    ABS1            m11, m13 ; 0dq0, 0dq3 , 1dq0, 1dq3

    ;beta calculations
%if %1 > 8
    shl             betaq, %1 - 8
%endif
    movd            m13, betad
    SPLATW          m13, m13, 0
    ;end beta calculations

    paddw            m9, m10, m11;   0d0, 0d3  ,  1d0, 1d3

    pshufhw         m14, m9,  q0033 ;0b00001111;  0d3 0d3 0d0 0d0 in high
    pshuflw         m14, m14, q0033 ;0b00001111;  1d3 1d3 1d0 1d0 in low

    pshufhw          m9, m9, q3300 ;0b11110000; 0d0 0d0 0d3 0d3
    pshuflw          m9, m9, q3300 ;0b11110000; 1d0 1d0 1d3 1d3

    paddw           m14, m9; 0d0+0d3, 1d0+1d3

    ;compare
    pcmpgtw         m15, m13, m14
    movmskps        r13, m15 ;filtering mask 0d0 + 0d3 < beta0 (bit 2 or 3) , 1d0 + 1d3 < beta1 (bit 0 or 1)
    test            r13, r13
    je              .bypassluma

    ;weak / strong decision compare to beta_2
    psraw           m15, m13, 2;   beta >> 2
    psllw            m8, m9, 1;
    pcmpgtw         m15, m8; (d0 << 1) < beta_2, (d3 << 1) < beta_2
    movmskps        r14, m15;
    ;end weak / strong decision

    ; weak filter nd_p/q calculation
    pshufd           m8, m10, 0x31
    psrld            m8, 16
    paddw            m8, m10
    movd            r7d, m8
    and              r7, 0xffff; 1dp0 + 1dp3
    pshufd           m8, m8, 0x4E
    movd            r8d, m8
    and              r8, 0xffff; 0dp0 + 0dp3

    pshufd           m8, m11, 0x31
    psrld            m8, 16
    paddw            m8, m11
    movd            r9d, m8
    and              r9, 0xffff; 1dq0 + 1dq3
    pshufd           m8, m8, 0x4E
    movd           r10d, m8
    and             r10, 0xffff; 0dq0 + 0dq3
    ; end calc for weak filter

    ; filtering mask
    mov             r11, r13
    shr             r11, 3
    movd            m15, r11d
    and             r13, 1
    movd            m11, r13d
    shufps          m11, m15, 0
    shl             r11, 1
    or              r13, r11

    pcmpeqd         m11, [pd_1]; filtering mask

    ;decide between strong and weak filtering
    ;tc25 calculations
    mov            r11d, [tcq];
%if %1 > 8
    shl             r11, %1 - 8
%endif
    movd             m8, r11d; tc0
    add             tcq, 4;
    mov             r3d, [tcq];
%if %1 > 8
    shl              r3, %1 - 8
%endif
    movd             m9, r3d; tc1
    add            r11d, r3d; tc0 + tc1
    jz             .bypassluma
    punpcklwd        m8, m8
    punpcklwd        m9, m9
    shufps           m8, m9, 0; tc0, tc1
    mova             m9, m8
    psllw            m8, 2; tc << 2
    pavgw            m8, m9; tc25 = ((tc * 5 + 1) >> 1)
    ;end tc25 calculations

    ;----beta_3 comparison-----
    psubw           m12, m0, m3;      p3 - p0
    ABS1            m12, m14; abs(p3 - p0)

    psubw           m15, m7, m4;      q3 - q0
    ABS1            m15, m14; abs(q3 - q0)

    paddw           m12, m15; abs(p3 - p0) + abs(q3 - q0)

    pshufhw         m12, m12, 0xf0 ;0b11110000;
    pshuflw         m12, m12, 0xf0 ;0b11110000;

    psraw           m13, 3; beta >> 3
    pcmpgtw         m13, m12;
    movmskps        r11, m13;
    and             r14, r11; strong mask , beta_2 and beta_3 comparisons
    ;----beta_3 comparison end-----
    ;----tc25 comparison---
    psubw           m12, m3, m4;      p0 - q0
    ABS1            m12, m14; abs(p0 - q0)

    pshufhw         m12, m12, 0xf0 ;0b11110000;
    pshuflw         m12, m12, 0xf0 ;0b11110000;

    pcmpgtw          m8, m12; tc25 comparisons
    movmskps        r11, m8;
    and             r14, r11; strong mask, beta_2, beta_3 and tc25 comparisons
    ;----tc25 comparison end---
    mov             r11, r14;
    shr             r11, 1;
    and             r14, r11; strong mask, bits 2 and 0

    pmullw          m14, m9, [pw_m2]; -tc * 2
    paddw            m9, m9

    and             r14, 5; 0b101
    mov             r11, r14; strong mask
    shr             r14, 2;
    movd            m12, r14d; store to xmm for mask generation
    shl             r14, 1
    and             r11, 1
    movd            m10, r11d; store to xmm for mask generation
    or              r14, r11; final strong mask, bits 1 and 0
    jz      .weakfilter

    shufps          m10, m12, 0
    pcmpeqd         m10, [pd_1]; strong mask

    mova            m13, [pw_4]; 4 in every cell
    pand            m11, m10; combine filtering mask and strong mask
    paddw           m12, m2, m3;          p1 +   p0
    paddw           m12, m4;          p1 +   p0 +   q0
    mova            m10, m12; copy
    paddw           m12, m12;       2*p1 + 2*p0 + 2*q0
    paddw           m12, m1;   p2 + 2*p1 + 2*p0 + 2*q0
    paddw           m12, m5;   p2 + 2*p1 + 2*p0 + 2*q0 + q1
    paddw           m12, m13;  p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4
    psraw           m12, 3;  ((p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4) >> 3)
    psubw           m12, m3; ((p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4) >> 3) - p0
    pmaxsw          m12, m14
    pminsw          m12, m9; av_clip( , -2 * tc, 2 * tc)
    paddw           m12, m3; p0'

    paddw           m15, m1, m10; p2 + p1 + p0 + q0
    psrlw           m13, 1; 2 in every cell
    paddw           m15, m13; p2 + p1 + p0 + q0 + 2
    psraw           m15, 2;  (p2 + p1 + p0 + q0 + 2) >> 2
    psubw           m15, m2;((p2 + p1 + p0 + q0 + 2) >> 2) - p1
    pmaxsw          m15, m14
    pminsw          m15, m9; av_clip( , -2 * tc, 2 * tc)
    paddw           m15, m2; p1'

    paddw            m8, m1, m0;     p3 +   p2
    paddw            m8, m8;   2*p3 + 2*p2
    paddw            m8, m1;   2*p3 + 3*p2
    paddw            m8, m10;  2*p3 + 3*p2 + p1 + p0 + q0
    paddw           m13, m13
    paddw            m8, m13;  2*p3 + 3*p2 + p1 + p0 + q0 + 4
    psraw            m8, 3;   (2*p3 + 3*p2 + p1 + p0 + q0 + 4) >> 3
    psubw            m8, m1; ((2*p3 + 3*p2 + p1 + p0 + q0 + 4) >> 3) - p2
    pmaxsw           m8, m14
    pminsw           m8, m9; av_clip( , -2 * tc, 2 * tc)
    paddw            m8, m1; p2'
    MASKED_COPY      m1, m8

    paddw            m8, m3, m4;         p0 +   q0
    paddw            m8, m5;         p0 +   q0 +   q1
    paddw            m8, m8;       2*p0 + 2*q0 + 2*q1
    paddw            m8, m2;  p1 + 2*p0 + 2*q0 + 2*q1
    paddw            m8, m6;  p1 + 2*p0 + 2*q0 + 2*q1 + q2
    paddw            m8, m13; p1 + 2*p0 + 2*q0 + 2*q1 + q2 + 4
    psraw            m8, 3;  (p1 + 2*p0 + 2*q0 + 2*q1 + q2 + 4) >>3
    psubw            m8, m4;
    pmaxsw           m8, m14
    pminsw           m8, m9; av_clip( , -2 * tc, 2 * tc)
    paddw            m8, m4; q0'
    MASKED_COPY      m2, m15

    paddw           m15, m3, m4;   p0 + q0
    paddw           m15, m5;   p0 + q0 + q1
    mova            m10, m15;
    paddw           m15, m6;   p0 + q0 + q1 + q2
    psrlw           m13, 1; 2 in every cell
    paddw           m15, m13;  p0 + q0 + q1 + q2 + 2
    psraw           m15, 2;   (p0 + q0 + q1 + q2 + 2) >> 2
    psubw           m15, m5; ((p0 + q0 + q1 + q2 + 2) >> 2) - q1
    pmaxsw          m15, m14
    pminsw          m15, m9; av_clip( , -2 * tc, 2 * tc)
    paddw           m15, m5; q1'

    paddw           m13, m7;      q3 + 2
    paddw           m13, m6;      q3 +  q2 + 2
    paddw           m13, m13;   2*q3 + 2*q2 + 4
    paddw           m13, m6;    2*q3 + 3*q2 + 4
    paddw           m13, m10;   2*q3 + 3*q2 + q1 + q0 + p0 + 4
    psraw           m13, 3;    (2*q3 + 3*q2 + q1 + q0 + p0 + 4) >> 3
    psubw           m13, m6;  ((2*q3 + 3*q2 + q1 + q0 + p0 + 4) >> 3) - q2
    pmaxsw          m13, m14
    pminsw          m13, m9; av_clip( , -2 * tc, 2 * tc)
    paddw           m13, m6; q2'

    MASKED_COPY      m6, m13
    MASKED_COPY      m5, m15
    MASKED_COPY      m4, m8
    MASKED_COPY      m3, m12

.weakfilter:
    not             r14; strong mask -> weak mask
    and             r14, r13; final weak filtering mask, bits 0 and 1
    jz             .store

    ; weak filtering mask
    mov             r11, r14
    shr             r11, 1
    movd            m12, r11d
    and             r14, 1
    movd            m11, r14d
    shufps          m11, m12, 0
    pcmpeqd         m11, [pd_1]; filtering mask

    mov             r13, betaq
    shr             r13, 1;
    add             betaq, r13
    shr             betaq, 3; ((beta + (beta >> 1)) >> 3))

    mova            m13, [pw_8]
    psubw           m12, m4, m3 ; q0 - p0
    psllw           m10, m12, 3; 8 * (q0 - p0)
    paddw           m12, m10 ; 9 * (q0 - p0)

    psubw           m10, m5, m2 ; q1 - p1
    psllw            m8, m10, 1; 2 * ( q1 - p1 )
    paddw           m10, m8; 3 * ( q1 - p1 )
    psubw           m12, m10; 9 * (q0 - p0) - 3 * ( q1 - p1 )
    paddw           m12, m13; + 8
    psraw           m12, 4; >> 4 , delta0
    PABSW           m13, m12; abs(delta0)


    psllw           m10, m9, 2; 8 * tc
    paddw           m10, m9; 10 * tc
    pcmpgtw         m10, m13
    pand            m11, m10

    psraw            m9, 1;   tc * 2 -> tc
    psraw           m14, 1; -tc * 2 -> -tc

    pmaxsw          m12, m14
    pminsw          m12, m9;  av_clip(delta0, -tc, tc)

    psraw            m9, 1;   tc -> tc / 2
    pmullw          m14, m9, [pw_m1]; -tc / 2

    pavgw           m15, m1, m3;   (p2 + p0 + 1) >> 1
    psubw           m15, m2;  ((p2 + p0 + 1) >> 1) - p1
    paddw           m15, m12; ((p2 + p0 + 1) >> 1) - p1 + delta0
    psraw           m15, 1;   (((p2 + p0 + 1) >> 1) - p1 + delta0) >> 1
    pmaxsw          m15, m14
    pminsw          m15, m9; av_clip(deltap1, -tc/2, tc/2)
    paddw           m15, m2; p1'

    ;beta calculations
    movd            m10, betad
    SPLATW          m10, m10, 0

    movd            m13, r7d; 1dp0 + 1dp3
    movd             m8, r8d; 0dp0 + 0dp3
    punpcklwd        m8, m8
    punpcklwd       m13, m13
    shufps          m13, m8, 0;
    pcmpgtw          m8, m10, m13
    pand             m8, m11
    ;end beta calculations
    MASKED_COPY2     m2, m15, m8; write p1'

    pavgw            m8, m6, m4;   (q2 + q0 + 1) >> 1
    psubw            m8, m5;  ((q2 + q0 + 1) >> 1) - q1
    psubw            m8, m12; ((q2 + q0 + 1) >> 1) - q1 - delta0)
    psraw            m8, 1;   ((q2 + q0 + 1) >> 1) - q1 - delta0) >> 1
    pmaxsw           m8, m14
    pminsw           m8, m9; av_clip(deltaq1, -tc/2, tc/2)
    paddw            m8, m5; q1'

    movd            m13, r9d;
    movd            m15, r10d;
    punpcklwd       m15, m15
    punpcklwd       m13, m13
    shufps          m13, m15, 0; dq0 + dq3

    pcmpgtw         m10, m13; compare to ((beta+(beta>>1))>>3)
    pand            m10, m11
    MASKED_COPY2     m5, m8, m10; write q1'

    paddw           m15, m3, m12 ; p0 + delta0
    MASKED_COPY      m3, m15

    psubw            m8, m4, m12 ; q0 - delta0
    MASKED_COPY      m4, m8
%endmacro

INIT_XMM sse2
;-----------------------------------------------------------------------------
; void ff_hevc_v_loop_filter_chroma(uint8_t *_pix, ptrdiff_t _stride, int *_tc,
;                                   uint8_t *_no_p, uint8_t *_no_q);
;-----------------------------------------------------------------------------
cglobal hevc_v_loop_filter_chroma_8, 3, 6, 8
    sub              r0, 2
    lea              r5, [3 * r1]
    mov              r4, r0
    add              r0, r5
    TRANSPOSE4x8B_LOAD  PASS8ROWS(r4, r0, r1, r5)
    CHROMA_DEBLOCK_BODY 8
    TRANSPOSE8x4B_STORE PASS8ROWS(r4, r0, r1, r5)
    RET

cglobal hevc_v_loop_filter_chroma_10, 3, 6, 8
    sub              r0, 4
    lea              r5, [3 * r1]
    mov              r4, r0
    add              r0, r5
    TRANSPOSE4x8W_LOAD  PASS8ROWS(r4, r0, r1, r5)
    CHROMA_DEBLOCK_BODY 10
    TRANSPOSE8x4W_STORE PASS8ROWS(r4, r0, r1, r5)
    RET

;-----------------------------------------------------------------------------
; void ff_hevc_h_loop_filter_chroma(uint8_t *_pix, ptrdiff_t _stride, int *_tc,
;                                   uint8_t *_no_p, uint8_t *_no_q);
;-----------------------------------------------------------------------------
cglobal hevc_h_loop_filter_chroma_8, 3, 6, 8
    mov              r5, r0; pix
    sub              r5, r1
    sub              r5, r1
    movh             m0, [r5];      p1
    movh             m1, [r5 + r1]; p0
    movh             m2, [r0];      q0
    movh             m3, [r0 + r1]; q1
    pxor             m5, m5; zeros reg
    punpcklbw        m0, m5
    punpcklbw        m1, m5
    punpcklbw        m2, m5
    punpcklbw        m3, m5
    CHROMA_DEBLOCK_BODY  8
    packuswb          m1, m2
    movh       [r5 + r1], m1
    movhps          [r0], m1
    RET

cglobal hevc_h_loop_filter_chroma_10, 3, 6, 8
    mov             r5, r0; pix
    sub             r5, r1
    sub             r5, r1
    movdqu          m0, [r5];      p1
    movdqu          m1, [r5+r1];   p0
    movdqu          m2, [r0];      q0
    movdqu          m3, [r0 + r1]; q1
    CHROMA_DEBLOCK_BODY 10
    pxor            m5, m5; zeros reg
    CLIPW           m1, m5, [pw_pixel_max]
    CLIPW           m2, m5, [pw_pixel_max]
    movdqu   [r5 + r1], m1
    movdqu        [r0], m2
    RET

%if ARCH_X86_64
INIT_XMM ssse3
;-----------------------------------------------------------------------------
; void ff_hevc_v_loop_filter_luma(uint8_t *_pix, ptrdiff_t _stride, int beta,
;                                 int *_tc, uint8_t *_no_p, uint8_t *_no_q);
;-----------------------------------------------------------------------------
cglobal hevc_v_loop_filter_luma_8, 4, 15, 16, pix, stride, beta, tc
    sub              r0, 4
    lea              r5, [3 * r1]
    mov              r6, r0
    add              r0, r5
    TRANSPOSE8x8B_LOAD  PASS8ROWS(r6, r0, r1, r5)
    LUMA_DEBLOCK_BODY 8, v
.store:
    TRANSPOSE8x8B_STORE PASS8ROWS(r6, r0, r1, r5)
.bypassluma:
    RET

cglobal hevc_v_loop_filter_luma_10, 4, 15, 16, pix, stride, beta, tc
    sub            pixq, 8
    lea              r5, [3 * strideq]
    mov              r6, pixq
    add            pixq, r5
    TRANSPOSE8x8W_LOAD  PASS8ROWS(r6, pixq, strideq, r5)
    LUMA_DEBLOCK_BODY 10, v
.store:
    TRANSPOSE8x8W_STORE PASS8ROWS(r6, r0, r1, r5)
.bypassluma:
    RET

;-----------------------------------------------------------------------------
; void ff_hevc_h_loop_filter_luma(uint8_t *_pix, ptrdiff_t _stride, int beta,
;                                 int *_tc, uint8_t *_no_p, uint8_t *_no_q);
;-----------------------------------------------------------------------------
cglobal hevc_h_loop_filter_luma_8, 4, 15, 16, pix, stride, beta, tc, count, pix0, src3stride
    lea     src3strideq, [3 * strideq]
    mov           pix0q, pixq
    sub           pix0q, src3strideq
    sub           pix0q, strideq
    movdqu           m0, [pix0q];               p3
    movdqu           m1, [pix0q +     strideq]; p2
    movdqu           m2, [pix0q + 2 * strideq]; p1
    movdqu           m3, [pix0q + src3strideq]; p0
    movdqu           m4, [pixq];                q0
    movdqu           m5, [pixq +     strideq];  q1
    movdqu           m6, [pixq + 2 * strideq];  q2
    movdqu           m7, [pixq + src3strideq];  q3
    pxor             m8, m8
    punpcklbw        m0, m8
    punpcklbw        m1, m8
    punpcklbw        m2, m8
    punpcklbw        m3, m8
    punpcklbw        m4, m8
    punpcklbw        m5, m8
    punpcklbw        m6, m8
    punpcklbw        m7, m8
    LUMA_DEBLOCK_BODY 8, h
.store:
    packuswb          m1, m2
    packuswb          m3, m4
    packuswb          m5, m6
    movh   [r5 +     r1], m1
    movhps [r5 + 2 * r1], m1
    movh   [r5 +     r6], m3
    movhps [r0         ], m3
    movh   [r0 +     r1], m5
    movhps [r0 + 2 * r1], m5
.bypassluma:
    RET

cglobal hevc_h_loop_filter_luma_10, 4, 15, 16, pix, stride, beta, tc, count, pix0, src3stride
    lea                  src3strideq, [3 * strideq]
    mov                        pix0q, pixq
    sub                        pix0q, src3strideq
    sub                        pix0q, strideq
    movdqu                        m0, [pix0q];               p3
    movdqu                        m1, [pix0q +     strideq]; p2
    movdqu                        m2, [pix0q + 2 * strideq]; p1
    movdqu                        m3, [pix0q + src3strideq]; p0
    movdqu                        m4, [pixq];                q0
    movdqu                        m5, [pixq  +     strideq]; q1
    movdqu                        m6, [pixq  + 2 * strideq]; q2
    movdqu                        m7, [pixq  + src3strideq]; q3
    LUMA_DEBLOCK_BODY             10, h
.store:
    pxor                          m8, m8; zeros reg
    CLIPW                         m1, m8, [pw_pixel_max]
    CLIPW                         m2, m8, [pw_pixel_max]
    CLIPW                         m3, m8, [pw_pixel_max]
    CLIPW                         m4, m8, [pw_pixel_max]
    CLIPW                         m5, m8, [pw_pixel_max]
    CLIPW                         m6, m8, [pw_pixel_max]
    movdqu     [pix0q +     strideq], m1;  p2
    movdqu     [pix0q + 2 * strideq], m2;  p1
    movdqu     [pix0q + src3strideq], m3;  p0
    movdqu     [pixq               ], m4;  q0
    movdqu     [pixq  +     strideq], m5;  q1
    movdqu     [pixq  + 2 * strideq], m6;  q2
.bypassluma:
    RET
%endif
