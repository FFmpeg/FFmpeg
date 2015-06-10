;*****************************************************************************
;* SSE2-optimized HEVC deblocking code
;*****************************************************************************
;* Copyright (C) 2013 VTT
;*
;* Authors: Seppo Tomperi <seppo.tomperi@vtt.fi>
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

cextern pw_1023
%define pw_pixel_max_10 pw_1023
pw_pixel_max_12: times 8 dw ((1 << 12)-1)
pw_m2:           times 8 dw -2
pd_1 :           times 4 dd  1

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
    movd             m3, %8

    punpcklbw        m4, m6
    punpcklbw        m5, m3
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
    packuswb         m0, m2
    packuswb         m1, m3
    SBUTTERFLY bw, 0, 1, 2
    SBUTTERFLY wd, 0, 1, 2

    movd             %1, m0
    pshufd           m0, m0, 0x39
    movd             %2, m0
    pshufd           m0, m0, 0x39
    movd             %3, m0
    pshufd           m0, m0, 0x39
    movd             %4, m0

    movd             %5, m1
    pshufd           m1, m1, 0x39
    movd             %6, m1
    pshufd           m1, m1, 0x39
    movd             %7, m1
    pshufd           m1, m1, 0x39
    movd             %8, m1
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
    movq             m3, %8

    punpcklwd        m4, m6
    punpcklwd        m5, m3
    punpckhdq        m6, m4, m5
    punpckldq        m4, m5

    punpckhqdq       m1, m0, m4
    punpcklqdq       m0, m4
    punpckhqdq       m3, m2, m6
    punpcklqdq       m2, m6

%endmacro

; in: 4 rows of 8 words in m0..m3
; out: 8 rows of 4 words in %1..%8
%macro TRANSPOSE8x4W_STORE 9
    TRANSPOSE4x4W     0, 1, 2, 3, 4

    pxor             m5, m5; zeros reg
    CLIPW            m0, m5, %9
    CLIPW            m1, m5, %9
    CLIPW            m2, m5, %9
    CLIPW            m3, m5, %9

    movq             %1, m0
    movhps           %2, m0
    movq             %3, m1
    movhps           %4, m1
    movq             %5, m2
    movhps           %6, m2
    movq             %7, m3
    movhps           %8, m3
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
    packuswb         m0, m4
    packuswb         m1, m5
    packuswb         m2, m6
    packuswb         m3, m7
    TRANSPOSE2x4x4B   0, 1, 2, 3, 4

    movq             %1, m0
    movhps           %2, m0
    movq             %3, m1
    movhps           %4, m1
    movq             %5, m2
    movhps           %6, m2
    movq             %7, m3
    movhps           %8, m3
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
%macro TRANSPOSE8x8W_STORE 9
    TRANSPOSE8x8W     0, 1, 2, 3, 4, 5, 6, 7, 8

    pxor             m8, m8
    CLIPW            m0, m8, %9
    CLIPW            m1, m8, %9
    CLIPW            m2, m8, %9
    CLIPW            m3, m8, %9
    CLIPW            m4, m8, %9
    CLIPW            m5, m8, %9
    CLIPW            m6, m8, %9
    CLIPW            m7, m8, %9

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
    movq             m6, [tcq]; tc0
    punpcklwd        m6, m6
    pshufd           m6, m6, 0xA0; tc0, tc1
%if cpuflag(ssse3)
    psignw           m4, m6, [pw_m1]; -tc0, -tc1
%else
    pmullw           m4, m6, [pw_m1]; -tc0, -tc1
%endif
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

    pshufhw         m14, m9, 0x0f ;0b00001111;  0d3 0d3 0d0 0d0 in high
    pshuflw         m14, m14, 0x0f ;0b00001111;  1d3 1d3 1d0 1d0 in low

    pshufhw          m9, m9, 0xf0 ;0b11110000; 0d0 0d0 0d3 0d3
    pshuflw          m9, m9, 0xf0 ;0b11110000; 1d0 1d0 1d3 1d3

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
    movmskps        r6, m15;
    ;end weak / strong decision

    ; weak filter nd_p/q calculation
    pshufd           m8, m10, 0x31
    psrld            m8, 16
    paddw            m8, m10
    movd            r7d, m8
    pshufd           m8, m8, 0x4E
    movd            r8d, m8

    pshufd           m8, m11, 0x31
    psrld            m8, 16
    paddw            m8, m11
    movd            r9d, m8
    pshufd           m8, m8, 0x4E
    movd           r10d, m8
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
    mov             r3d, [tcq+4];
%if %1 > 8
    shl              r3, %1 - 8
%endif
    add            r11d, r3d; tc0 + tc1
    jz             .bypassluma
    movd             m9, r3d; tc1
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
    and             r6, r11; strong mask , beta_2 and beta_3 comparisons
    ;----beta_3 comparison end-----
    ;----tc25 comparison---
    psubw           m12, m3, m4;      p0 - q0
    ABS1            m12, m14; abs(p0 - q0)

    pshufhw         m12, m12, 0xf0 ;0b11110000;
    pshuflw         m12, m12, 0xf0 ;0b11110000;

    pcmpgtw          m8, m12; tc25 comparisons
    movmskps        r11, m8;
    and             r6, r11; strong mask, beta_2, beta_3 and tc25 comparisons
    ;----tc25 comparison end---
    mov             r11, r6;
    shr             r11, 1;
    and             r6, r11; strong mask, bits 2 and 0

    pmullw          m14, m9, [pw_m2]; -tc * 2
    paddw            m9, m9

    and             r6, 5; 0b101
    mov             r11, r6; strong mask
    shr             r6, 2;
    movd            m12, r6d; store to xmm for mask generation
    shl             r6, 1
    and             r11, 1
    movd            m10, r11d; store to xmm for mask generation
    or              r6, r11; final strong mask, bits 1 and 0
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
    not             r6; strong mask -> weak mask
    and             r6, r13; final weak filtering mask, bits 0 and 1
    jz             .store

    ; weak filtering mask
    mov             r11, r6
    shr             r11, 1
    movd            m12, r11d
    and             r6, 1
    movd            m11, r6d
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
%if cpuflag(ssse3)
    psignw          m14, m9, [pw_m1]; -tc / 2
%else
    pmullw          m14, m9, [pw_m1]; -tc / 2
%endif

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

;-----------------------------------------------------------------------------
; void ff_hevc_v_loop_filter_chroma(uint8_t *_pix, ptrdiff_t _stride, int32_t *tc,
;                                   uint8_t *_no_p, uint8_t *_no_q);
;-----------------------------------------------------------------------------
%macro LOOP_FILTER_CHROMA 0
cglobal hevc_v_loop_filter_chroma_8, 3, 5, 7, pix, stride, tc, pix0, r3stride
    sub            pixq, 2
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE4x8B_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 8
    TRANSPOSE8x4B_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    RET

cglobal hevc_v_loop_filter_chroma_10, 3, 5, 7, pix, stride, tc, pix0, r3stride
    sub            pixq, 4
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE4x8W_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 10
    TRANSPOSE8x4W_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq), [pw_pixel_max_10]
    RET

cglobal hevc_v_loop_filter_chroma_12, 3, 5, 7, pix, stride, tc, pix0, r3stride
    sub            pixq, 4
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE4x8W_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 12
    TRANSPOSE8x4W_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq), [pw_pixel_max_12]
    RET

;-----------------------------------------------------------------------------
; void ff_hevc_h_loop_filter_chroma(uint8_t *_pix, ptrdiff_t _stride, int32_t *tc,
;                                   uint8_t *_no_p, uint8_t *_no_q);
;-----------------------------------------------------------------------------
cglobal hevc_h_loop_filter_chroma_8, 3, 4, 7, pix, stride, tc, pix0
    mov           pix0q, pixq
    sub           pix0q, strideq
    sub           pix0q, strideq
    movq             m0, [pix0q];    p1
    movq             m1, [pix0q+strideq]; p0
    movq             m2, [pixq];    q0
    movq             m3, [pixq+strideq]; q1
    pxor             m5, m5; zeros reg
    punpcklbw        m0, m5
    punpcklbw        m1, m5
    punpcklbw        m2, m5
    punpcklbw        m3, m5
    CHROMA_DEBLOCK_BODY  8
    packuswb         m1, m2
    movh[pix0q+strideq], m1
    movhps       [pixq], m1
    RET

cglobal hevc_h_loop_filter_chroma_10, 3, 4, 7, pix, stride, tc, pix0
    mov          pix0q, pixq
    sub          pix0q, strideq
    sub          pix0q, strideq
    movu            m0, [pix0q];    p1
    movu            m1, [pix0q+strideq]; p0
    movu            m2, [pixq];    q0
    movu            m3, [pixq+strideq]; q1
    CHROMA_DEBLOCK_BODY 10
    pxor            m5, m5; zeros reg
    CLIPW           m1, m5, [pw_pixel_max_10]
    CLIPW           m2, m5, [pw_pixel_max_10]
    movu [pix0q+strideq], m1
    movu        [pixq], m2
    RET

cglobal hevc_h_loop_filter_chroma_12, 3, 4, 7, pix, stride, tc, pix0
    mov          pix0q, pixq
    sub          pix0q, strideq
    sub          pix0q, strideq
    movu            m0, [pix0q];    p1
    movu            m1, [pix0q+strideq]; p0
    movu            m2, [pixq];    q0
    movu            m3, [pixq+strideq]; q1
    CHROMA_DEBLOCK_BODY 12
    pxor            m5, m5; zeros reg
    CLIPW           m1, m5, [pw_pixel_max_12]
    CLIPW           m2, m5, [pw_pixel_max_12]
    movu [pix0q+strideq], m1
    movu        [pixq], m2
    RET
%endmacro

INIT_XMM sse2
LOOP_FILTER_CHROMA
INIT_XMM avx
LOOP_FILTER_CHROMA

%if ARCH_X86_64
%macro LOOP_FILTER_LUMA 0
;-----------------------------------------------------------------------------
; void ff_hevc_v_loop_filter_luma(uint8_t *_pix, ptrdiff_t _stride, int beta,
;                                 int32_t *tc, uint8_t *_no_p, uint8_t *_no_q);
;-----------------------------------------------------------------------------
cglobal hevc_v_loop_filter_luma_8, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    sub            pixq, 4
    lea           pix0q, [3 * r1]
    mov     src3strideq, pixq
    add            pixq, pix0q
    TRANSPOSE8x8B_LOAD  PASS8ROWS(src3strideq, pixq, r1, pix0q)
    LUMA_DEBLOCK_BODY 8, v
.store:
    TRANSPOSE8x8B_STORE PASS8ROWS(src3strideq, pixq, r1, pix0q)
.bypassluma:
    RET

cglobal hevc_v_loop_filter_luma_10, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    sub            pixq, 8
    lea           pix0q, [3 * strideq]
    mov     src3strideq, pixq
    add            pixq, pix0q
    TRANSPOSE8x8W_LOAD  PASS8ROWS(src3strideq, pixq, strideq, pix0q)
    LUMA_DEBLOCK_BODY 10, v
.store:
    TRANSPOSE8x8W_STORE PASS8ROWS(src3strideq, pixq, r1, pix0q), [pw_pixel_max_10]
.bypassluma:
    RET

cglobal hevc_v_loop_filter_luma_12, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    sub            pixq, 8
    lea           pix0q, [3 * strideq]
    mov     src3strideq, pixq
    add            pixq, pix0q
    TRANSPOSE8x8W_LOAD  PASS8ROWS(src3strideq, pixq, strideq, pix0q)
    LUMA_DEBLOCK_BODY 12, v
.store:
    TRANSPOSE8x8W_STORE PASS8ROWS(src3strideq, pixq, r1, pix0q), [pw_pixel_max_12]
.bypassluma:
    RET

;-----------------------------------------------------------------------------
; void ff_hevc_h_loop_filter_luma(uint8_t *_pix, ptrdiff_t _stride, int beta,
;                                 int32_t *tc, uint8_t *_no_p, uint8_t *_no_q);
;-----------------------------------------------------------------------------
cglobal hevc_h_loop_filter_luma_8, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    lea     src3strideq, [3 * strideq]
    mov           pix0q, pixq
    sub           pix0q, src3strideq
    sub           pix0q, strideq
    movq             m0, [pix0q];               p3
    movq             m1, [pix0q +     strideq]; p2
    movq             m2, [pix0q + 2 * strideq]; p1
    movq             m3, [pix0q + src3strideq]; p0
    movq             m4, [pixq];                q0
    movq             m5, [pixq +     strideq];  q1
    movq             m6, [pixq + 2 * strideq];  q2
    movq             m7, [pixq + src3strideq];  q3
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
    movh   [pix0q +     strideq], m1
    movhps [pix0q + 2 * strideq], m1
    movh   [pix0q + src3strideq], m3
    movhps [pixq               ], m3
    movh   [pixq  +     strideq], m5
    movhps [pixq  + 2 * strideq], m5
.bypassluma:
    RET

cglobal hevc_h_loop_filter_luma_10, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
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
    CLIPW                         m1, m8, [pw_pixel_max_10]
    CLIPW                         m2, m8, [pw_pixel_max_10]
    CLIPW                         m3, m8, [pw_pixel_max_10]
    CLIPW                         m4, m8, [pw_pixel_max_10]
    CLIPW                         m5, m8, [pw_pixel_max_10]
    CLIPW                         m6, m8, [pw_pixel_max_10]
    movdqu     [pix0q +     strideq], m1;  p2
    movdqu     [pix0q + 2 * strideq], m2;  p1
    movdqu     [pix0q + src3strideq], m3;  p0
    movdqu     [pixq               ], m4;  q0
    movdqu     [pixq  +     strideq], m5;  q1
    movdqu     [pixq  + 2 * strideq], m6;  q2
.bypassluma:
    RET

cglobal hevc_h_loop_filter_luma_12, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
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
    LUMA_DEBLOCK_BODY             12, h
.store:
    pxor                          m8, m8; zeros reg
    CLIPW                         m1, m8, [pw_pixel_max_12]
    CLIPW                         m2, m8, [pw_pixel_max_12]
    CLIPW                         m3, m8, [pw_pixel_max_12]
    CLIPW                         m4, m8, [pw_pixel_max_12]
    CLIPW                         m5, m8, [pw_pixel_max_12]
    CLIPW                         m6, m8, [pw_pixel_max_12]
    movdqu     [pix0q +     strideq], m1;  p2
    movdqu     [pix0q + 2 * strideq], m2;  p1
    movdqu     [pix0q + src3strideq], m3;  p0
    movdqu     [pixq               ], m4;  q0
    movdqu     [pixq  +     strideq], m5;  q1
    movdqu     [pixq  + 2 * strideq], m6;  q2
.bypassluma:
    RET

%endmacro

INIT_XMM sse2
LOOP_FILTER_LUMA
INIT_XMM ssse3
LOOP_FILTER_LUMA
INIT_XMM avx
LOOP_FILTER_LUMA
%endif
