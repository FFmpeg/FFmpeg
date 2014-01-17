;******************************************************************************
;* VP9 loop filter SIMD optimizations
;*
;* Copyright (C) 2013-2014 Clément Bœsch <u pkh me>
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

%if ARCH_X86_64

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

cextern pb_3
cextern pb_80

pb_4:   times 16 db 0x04
pb_10:  times 16 db 0x10
pb_40:  times 16 db 0x40
pb_81:  times 16 db 0x81
pb_f8:  times 16 db 0xf8
pb_fe:  times 16 db 0xfe

pw_4:   times  8 dw 4
pw_8:   times  8 dw 8

SECTION .text

; %1 = abs(%2-%3)
%macro ABSSUB 4 ; dst, src1 (RO), src2 (RO), tmp
    psubusb             %1, %3, %2
    psubusb             %4, %2, %3
    por                 %1, %4
%endmacro

; %1 = %1<=%2
%macro CMP_LTE 4 ; src/dst, cmp, tmp, pb_80
    pxor                %1, %4
    pcmpgtb             %3, %2, %1          ; cmp > src?
    pcmpeqb             %1, %2              ; cmp == src? XXX: avoid this with a -1/+1 well placed?
    por                 %1, %3              ; cmp >= src?
%endmacro

; %1 = abs(%2-%3) <= %4
%macro ABSSUB_CMP 6-7 [pb_80]; dst, src1, src2, cmp, tmp1, tmp2, [pb_80]
    ABSSUB              %1, %2, %3, %6      ; dst = abs(src1-src2)
    CMP_LTE             %1, %4, %6, %7      ; dst <= cmp
%endmacro

%macro MASK_APPLY 4 ; %1=new_data/dst %2=old_data %3=mask %4=tmp
    pand                %1, %3              ; new &= mask
    pandn               %4, %3, %2          ; tmp = ~mask & old
    por                 %1, %4              ; new&mask | old&~mask
%endmacro

%macro FILTER_SUBx2_ADDx2 8 ; %1=dst %2=h/l %3=cache %4=sub1 %5=sub2 %6=add1 %7=add2 %8=rshift
    punpck%2bw          %3, %4, m0
    psubw               %1, %3
    punpck%2bw          %3, %5, m0
    psubw               %1, %3
    punpck%2bw          %3, %6, m0
    paddw               %1, %3
    punpck%2bw          %3, %7, m0
    paddw               %1, %3
    mova                %3, %1
    psraw               %1, %8
%endmacro

%macro FILTER_INIT 7-8 ; tmp1, tmp2, cacheL, cacheH, dstp, filterid, [source]
    FILTER%6_INIT       %1, l, %3
    FILTER%6_INIT       %2, h, %4
    packuswb            %1, %2
%if %0 == 8
    MASK_APPLY          %1, %8, %7, %2
%else
    MASK_APPLY          %1, %5, %7, %2
%endif
    mova                %5, %1
%endmacro

%macro FILTER_UPDATE 11-12 ; tmp1, tmp2, cacheL, cacheH, dstp, -, -, +, +, rshift, [source]
    FILTER_SUBx2_ADDx2  %1, l, %3, %6, %7, %8, %9, %10
    FILTER_SUBx2_ADDx2  %2, h, %4, %6, %7, %8, %9, %10
    packuswb            %1, %2
%if %0 == 12
    MASK_APPLY          %1, %12, %11, %2
%else
    MASK_APPLY          %1, %5, %11, %2
%endif
    mova                %5, %1
%endmacro

%macro SRSHIFT3B_2X 4 ; reg1, reg2, [pb_10], tmp
    mova                %4, [pb_f8]
    pand                %1, %4
    pand                %2, %4
    psrlq               %1, 3
    psrlq               %2, 3
    pxor                %1, %3
    pxor                %2, %3
    psubb               %1, %3
    psubb               %2, %3
%endmacro

%macro EXTRACT_POS_NEG 3 ; i8, neg, pos
    pxor                %3, %3
    pxor                %2, %2
    pcmpgtb             %3, %1                          ; i8 < 0 mask
    psubb               %2, %1                          ; neg values (only the originally - will be kept)
    pand                %2, %3                          ; negative values of i8 (but stored as +)
    pandn               %3, %1                          ; positive values of i8
%endmacro

; clip_u8(u8 + i8)
%macro SIGN_ADD 5 ; dst, u8, i8, tmp1, tmp2
    EXTRACT_POS_NEG     %3, %4, %5
    psubusb             %1, %2, %4                      ; sub the negatives
    paddusb             %1, %5                          ; add the positives
%endmacro

; clip_u8(u8 - i8)
%macro SIGN_SUB 5 ; dst, u8, i8, tmp1, tmp2
    EXTRACT_POS_NEG     %3, %4, %5
    psubusb             %1, %2, %5                      ; sub the positives
    paddusb             %1, %4                          ; add the negatives
%endmacro

%macro FILTER6_INIT 3 ; %1=dst %2=h/l %3=cache
    punpck%2bw          %3, m14, m0                     ; p3: B->W
    mova                %1, %3                          ; p3
    paddw               %1, %3                          ; p3*2
    paddw               %1, %3                          ; p3*3
    punpck%2bw          %3, m15, m0                     ; p2: B->W
    paddw               %1, %3                          ; p3*3 + p2
    paddw               %1, %3                          ; p3*3 + p2*2
    punpck%2bw          %3, m10, m0                     ; p1: B->W
    paddw               %1, %3                          ; p3*3 + p2*2 + p1
    punpck%2bw          %3, m11, m0                     ; p0: B->W
    paddw               %1, %3                          ; p3*3 + p2*2 + p1 + p0
    punpck%2bw          %3, m12, m0                     ; q0: B->W
    paddw               %1, %3                          ; p3*3 + p2*2 + p1 + p0 + q0
    paddw               %1, [pw_4]                      ; p3*3 + p2*2 + p1 + p0 + q0 + 4
    mova                %3, %1                          ; base for next line (cache)
    psraw               %1, 3                           ; (p3*3 + p2*2 + p1 + p0 + q0 + 4) >> 3
%endmacro

%macro FILTER14_INIT 3 ; %1=dst %2=h/l %3=cache
    punpck%2bw          %1, m2, m0                      ; p7: B->W
    mova                %3, %1
    psllw               %1, 3                           ; p7*8
    psubw               %1, %3                          ; p7*7
    punpck%2bw          %3, m3, m0                      ; p6: B->W
    paddw               %1, %3                          ; p7*7 + p6
    paddw               %1, %3                          ; p7*7 + p6*2
    punpck%2bw          %3, m8, m0                      ; p5: B->W
    paddw               %1, %3                          ; p7*7 + p6*2 + p5
    punpck%2bw          %3, m9, m0                      ; p4: B->W
    paddw               %1, %3                          ; p7*7 + p6*2 + p5 + p4
    punpck%2bw          %3, m14, m0                     ; p3: B->W
    paddw               %1, %3                          ; p7*7 + p6*2 + p5 + p4 + p3
    punpck%2bw          %3, m15, m0                     ; p2: B->W
    paddw               %1, %3                          ; p7*7 + p6*2 + p5 + .. + p2
    punpck%2bw          %3, m10, m0                     ; p1: B->W
    paddw               %1, %3                          ; p7*7 + p6*2 + p5 + .. + p1
    punpck%2bw          %3, m11, m0                     ; p0: B->W
    paddw               %1, %3                          ; p7*7 + p6*2 + p5 + .. + p0
    punpck%2bw          %3, m12, m0                     ; q0: B->W
    paddw               %1, %3                          ; p7*7 + p6*2 + p5 + .. + p0 + q0
    paddw               %1, [pw_8]                      ; p7*7 + p6*2 + p5 + .. + p0 + q0 + 8
    mova                %3, %1                          ; base for next line (cache)
    psraw               %1, 4                           ; (p7*7 + p6*2 + p5 + .. + p0 + q0 + 8) >> 4
%endmacro

%macro TRANSPOSE16x16B 17
    mova %17, m%16
    SBUTTERFLY bw,  %1,  %2,  %16
    SBUTTERFLY bw,  %3,  %4,  %16
    SBUTTERFLY bw,  %5,  %6,  %16
    SBUTTERFLY bw,  %7,  %8,  %16
    SBUTTERFLY bw,  %9,  %10, %16
    SBUTTERFLY bw,  %11, %12, %16
    SBUTTERFLY bw,  %13, %14, %16
    mova m%16,  %17
    mova  %17, m%14
    SBUTTERFLY bw,  %15, %16, %14
    SBUTTERFLY wd,  %1,  %3,  %14
    SBUTTERFLY wd,  %2,  %4,  %14
    SBUTTERFLY wd,  %5,  %7,  %14
    SBUTTERFLY wd,  %6,  %8,  %14
    SBUTTERFLY wd,  %9,  %11, %14
    SBUTTERFLY wd,  %10, %12, %14
    SBUTTERFLY wd,  %13, %15, %14
    mova m%14,  %17
    mova  %17, m%12
    SBUTTERFLY wd,  %14, %16, %12
    SBUTTERFLY dq,  %1,  %5,  %12
    SBUTTERFLY dq,  %2,  %6,  %12
    SBUTTERFLY dq,  %3,  %7,  %12
    SBUTTERFLY dq,  %4,  %8,  %12
    SBUTTERFLY dq,  %9,  %13, %12
    SBUTTERFLY dq,  %10, %14, %12
    SBUTTERFLY dq,  %11, %15, %12
    mova m%12, %17
    mova  %17, m%8
    SBUTTERFLY dq,  %12, %16, %8
    SBUTTERFLY qdq, %1,  %9,  %8
    SBUTTERFLY qdq, %2,  %10, %8
    SBUTTERFLY qdq, %3,  %11, %8
    SBUTTERFLY qdq, %4,  %12, %8
    SBUTTERFLY qdq, %5,  %13, %8
    SBUTTERFLY qdq, %6,  %14, %8
    SBUTTERFLY qdq, %7,  %15, %8
    mova m%8, %17
    mova %17, m%1
    SBUTTERFLY qdq, %8,  %16, %1
    mova m%1, %17
    SWAP %2,  %9
    SWAP %3,  %5
    SWAP %4,  %13
    SWAP %6,  %11
    SWAP %8,  %15
    SWAP %12, %14
%endmacro

%macro LPF_16_16 1
    lea mstrideq, [strideq]
    neg mstrideq

%ifidn %1, h
    lea dstq, [dstq + 8*strideq - 8] ; go from top center (h pos) to center left (v pos)
    lea                  dst1q, [dstq  + 8*mstrideq]        ; dst1 = &dst[stride * -8]
    lea                  dst2q, [dst1q + 1* strideq]        ; dst2 = &dst[stride * -7]
    movu                    m0, [dst1q             ]        ;  m0 = dst[stride * -8] (p7)
    movu                    m1, [dst2q             ]        ;  m1 = dst[stride * -7] (p6)
    movu                    m2, [dst1q + 2* strideq]        ;  m2 = dst[stride * -6] (p5)
    movu                    m3, [dst2q + 2* strideq]        ;  m3 = dst[stride * -5] (p4)
    lea                  dst1q, [dstq]                      ; dst1 = &dst[stride * +0]
    lea                  dst2q, [dstq + 1*strideq]          ; dst2 = &dst[stride * +1]
    movu                    m4, [dst1q + 4*mstrideq]        ;  m4 = dst[stride * -4] (p3)
    movu                    m5, [dst2q + 4*mstrideq]        ;  m5 = dst[stride * -3] (p2)
    movu                    m6, [dst1q + 2*mstrideq]        ;  m6 = dst[stride * -2] (p1)
    movu                    m7, [dst2q + 2*mstrideq]        ;  m7 = dst[stride * -1] (p0)
    movu                    m8, [dst1q]                     ;  m8 = dst[stride * +0] (q0)
    movu                    m9, [dst2q]                     ;  m9 = dst[stride * +1] (q1)
    movu                   m10, [dst1q + 2* strideq]        ; m10 = dst[stride * +2] (q2)
    movu                   m11, [dst2q + 2* strideq]        ; m11 = dst[stride * +3] (q3)
    movu                   m12, [dst1q + 4* strideq]        ; m12 = dst[stride * +4] (q4)
    movu                   m13, [dst2q + 4* strideq]        ; m13 = dst[stride * +5] (q5)
    lea                  dst1q, [dstq  + 8* strideq]        ; dst1 = &dst[stride * +8]
    movu                   m14, [dst1q + 2*mstrideq]        ; m14 = dst[stride * +6] (q6)
    movu                   m15, [dst1q + 1*mstrideq]        ; m15 = dst[stride * +7] (q7)
    TRANSPOSE16x16B 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, [rsp]
    mova           [rsp +   0],  m0                         ; dst[stride * -8] (p7)
    mova           [rsp +  16],  m1                         ; dst[stride * -7] (p6)
    mova           [rsp +  32],  m2                         ; dst[stride * -6] (p5)
    mova           [rsp +  48],  m3                         ; dst[stride * -5] (p4)
    mova           [rsp +  64],  m4                         ; dst[stride * -4] (p3)
    mova           [rsp +  80],  m5                         ; dst[stride * -3] (p2)
    mova           [rsp +  96],  m6                         ; dst[stride * -2] (p1)
    mova           [rsp + 112],  m7                         ; dst[stride * -1] (p0)
    mova           [rsp + 128],  m8                         ; dst[stride * +0] (q0)
    mova           [rsp + 144],  m9                         ; dst[stride * +1] (q1)
    mova           [rsp + 160], m10                         ; dst[stride * +2] (q2)
    mova           [rsp + 176], m11                         ; dst[stride * +3] (q3)
    mova           [rsp + 192], m12                         ; dst[stride * +4] (q4)
    mova           [rsp + 208], m13                         ; dst[stride * +5] (q5)
    mova           [rsp + 224], m14                         ; dst[stride * +6] (q6)
    mova           [rsp + 240], m15                         ; dst[stride * +7] (q7)
%endif

    ; calc fm mask
%if cpuflag(ssse3)
    pxor                m0, m0
%endif
    SPLATB_REG          m2, I, m0                       ; I I I I ...
    SPLATB_REG          m3, E, m0                       ; E E E E ...
    mova                m0, [pb_80]
    pxor                m2, m0
    pxor                m3, m0
%ifidn %1, v
    lea              dst1q, [dstq  + 2*mstrideq]        ; dst1 = &dst[stride * -2]
    lea              dst2q, [dstq  + 2* strideq]        ; dst2 = &dst[stride * +2]
    mova                m8, [dstq  + 4*mstrideq]        ; m8  = dst[stride * -4] (p3)
    mova                m9, [dst1q + 1*mstrideq]        ; m9  = dst[stride * -3] (p2)
    mova               m10, [dstq  + 2*mstrideq]        ; m10 = dst[stride * -2] (p1)
    mova               m11, [dstq  + 1*mstrideq]        ; m11 = dst[stride * -1] (p0)
    mova               m12, [dstq              ]        ; m12 = dst[stride * +0] (q0)
    mova               m13, [dstq  + 1* strideq]        ; m13 = dst[stride * +1] (q1)
    mova               m14, [dstq  + 2* strideq]        ; m14 = dst[stride * +2] (q2)
    mova               m15, [dst2q + 1* strideq]        ; m15 = dst[stride * +3] (q3)
%else
    SWAP                 8,  4, 12
    SWAP                 9,  5, 13
    SWAP                10,  6, 14
    SWAP                11,  7, 15
%endif
    ABSSUB_CMP          m5,  m8,  m9, m2, m6, m7, m0    ; m5 = abs(p3-p2) <= I
    ABSSUB_CMP          m1,  m9, m10, m2, m6, m7, m0    ; m1 = abs(p2-p1) <= I
    pand                m5, m1
    ABSSUB_CMP          m1, m10, m11, m2, m6, m7, m0    ; m1 = abs(p1-p0) <= I
    pand                m5, m1
    ABSSUB_CMP          m1, m12, m13, m2, m6, m7, m0    ; m1 = abs(q1-q0) <= I
    pand                m5, m1
    ABSSUB_CMP          m1, m13, m14, m2, m6, m7, m0    ; m1 = abs(q2-q1) <= I
    pand                m5, m1
    ABSSUB_CMP          m1, m14, m15, m2, m6, m7, m0    ; m1 = abs(q3-q2) <= I
    pand                m5, m1
    ABSSUB              m1, m11, m12, m7                ; abs(p0-q0)
    paddusb             m1, m1                          ; abs(p0-q0) * 2
    ABSSUB              m2, m10, m13, m7                ; abs(p1-q1)
    pand                m2, [pb_fe]                     ; drop lsb so shift can work
    psrlq               m2, 1                           ; abs(p1-q1)/2
    paddusb             m1, m2                          ; abs(p0-q0)*2 + abs(p1-q1)/2
    pxor                m1, m0
    pcmpgtb             m4, m3, m1                      ; E > X?
    pcmpeqb             m3, m1                          ; E == X?
    por                 m3, m4                          ; E >= X?
    pand                m3, m5                          ; fm final value

    ; (m3: fm, m8..15: p3 p2 p1 p0 q0 q1 q2 q3)
    ; calc flat8in and hev masks
    mova                m6, [pb_81]                     ; [1 1 1 1 ...] ^ 0x80
    ABSSUB_CMP          m2, m8, m11, m6, m4, m5         ; abs(p3 - p0) <= 1
    mova                m8, [pb_80]
    ABSSUB_CMP          m1, m9, m11, m6, m4, m5, m8     ; abs(p2 - p0) <= 1
    pand                m2, m1
    ABSSUB              m4, m10, m11, m5                ; abs(p1 - p0)
%if cpuflag(ssse3)
    pxor                m0, m0
%endif
    SPLATB_REG          m7, H, m0                       ; H H H H ...
    pxor                m7, m8
    pxor                m4, m8
    pcmpgtb             m0, m4, m7                      ; abs(p1 - p0) > H (1/2 hev condition)
    pxor                m4, m8
    mova                m1, m4
    CMP_LTE             m1, m6, m5, m8                  ; abs(p1 - p0) <= 1
    pand                m2, m1                          ; (flat8in)
    ABSSUB              m4, m13, m12, m1                ; abs(q1 - q0)
    pxor                m4, m8
    pcmpgtb             m5, m4, m7                      ; abs(q1 - q0) > H (2/2 hev condition)
    pxor                m4, m8
    por                 m0, m5                          ; hev final value
    mova                m1, m4
    CMP_LTE             m1, m6, m5, m8                  ; abs(q1 - q0) <= 1
    pand                m2, m1                          ; (flat8in)
    ABSSUB_CMP          m1, m14, m12, m6, m4, m5, m8    ; abs(q2 - q0) <= 1
    pand                m2, m1
    ABSSUB_CMP          m1, m15, m12, m6, m4, m5, m8    ; abs(q3 - q0) <= 1
    pand                m2, m1                          ; flat8in final value

    ; (m0: hev, m2: flat8in, m3: fm, m6: pb_81, m9..15: p2 p1 p0 q0 q1 q2 q3)
    ; calc flat8out mask
%ifidn %1, v
    lea                 dst2q, [dstq  + 8*mstrideq]     ; dst2 = &dst[stride * -8] (p7)
    lea                 dst1q, [dst2q + 1*strideq]      ; dst1 = &dst[stride * -7] (p6)
    mova                m8, [dst2q]                     ; m8 = p7
    mova                m9, [dst1q]                     ; m9 = p6
%else
    mova                m8, [rsp +  0]                  ; m8 = p7
    mova                m9, [rsp + 16]                  ; m9 = p6
%endif
    ABSSUB_CMP          m1, m8, m11, m6, m4, m5         ; abs(p7 - p0) <= 1
    ABSSUB_CMP          m7, m9, m11, m6, m4, m5         ; abs(p6 - p0) <= 1
    pand                m1, m7
%ifidn %1, v
    mova                m8, [dst1q + 1*strideq]         ; m8 = dst[stride * -6] (p5)
    mova                m9, [dst1q + 2*strideq]         ; m9 = dst[stride * -5] (p4)
%else
    mova                m8, [rsp + 32]                  ; m8 = p5
    mova                m9, [rsp + 48]                  ; m9 = p4
%endif
    ABSSUB_CMP          m7, m8, m11, m6, m4, m5         ; abs(p5 - p0) <= 1
    pand                m1, m7
    ABSSUB_CMP          m7, m9, m11, m6, m4, m5         ; abs(p4 - p0) <= 1
    pand                m1, m7
%ifidn %1, v
    lea                 dst2q, [dstq  + 4*strideq]      ; dst2 = &dst[stride * +4] (q4)
    lea                 dst1q, [dst2q + 1*strideq]      ; dst1 = &dst[stride * +5] (q5)
    mova                m14, [dst2q]                    ; m14 = q4
    mova                m15, [dst1q]                    ; m15 = q5
%else
    mova                m14, [rsp + 192]                ; m14 = q4
    mova                m15, [rsp + 208]                ; m15 = q5
%endif
    ABSSUB_CMP          m7, m14, m12, m6, m4, m5        ; abs(q4 - q0) <= 1
    pand                m1, m7
    ABSSUB_CMP          m7, m15, m12, m6, m4, m5        ; abs(q5 - q0) <= 1
    pand                m1, m7
%ifidn %1, v
    mova                m14, [dst1q + 1*strideq]        ; m14 = dst[stride * +6] (q6)
    mova                m15, [dst1q + 2*strideq]        ; m15 = dst[stride * +7] (q7)
%else
    mova                m14, [rsp + 224]                ; m14 = q6
    mova                m15, [rsp + 240]                ; m15 = q7
%endif
    ABSSUB_CMP          m7, m14, m12, m6, m4, m5        ; abs(q4 - q0) <= 1
    pand                m1, m7
    ABSSUB_CMP          m7, m15, m12, m6, m4, m5        ; abs(q5 - q0) <= 1
    pand                m1, m7                          ; flat8out final value

    ; if (fm) {
    ;     if (out && in) filter_14()
    ;     else if (in)   filter_6()
    ;     else if (hev)  filter_2()
    ;     else           filter_4()
    ; }
    ;
    ; f14:                                                                            fm &  out &  in
    ; f6:  fm & ~f14 & in        => fm & ~(out & in) & in                          => fm & ~out &  in
    ; f2:  fm & ~f14 & ~f6 & hev => fm & ~(out & in) & ~(~out & in) & hev          => fm &  ~in &  hev
    ; f4:  fm & ~f14 & ~f6 & ~f2 => fm & ~(out & in) & ~(~out & in) & ~(~in & hev) => fm &  ~in & ~hev

    ; (m0: hev, m1: flat8out, m2: flat8in, m3: fm, m8..15: p5 p4 p1 p0 q0 q1 q6 q7)
    ; filter2()
    mova                m6, [pb_80]
    pxor                m15, m12, m6                    ; q0 ^ 0x80
    pxor                m14, m11, m6                    ; p0 ^ 0x80
    psubsb              m15, m14                        ; (signed) q0 - p0
    pxor                m4, m10, m6                     ; p1 ^ 0x80
    pxor                m5, m13, m6                     ; q1 ^ 0x80
    psubsb              m4, m5                          ; (signed) p1 - q1
    paddsb              m4, m15                         ;   (q0 - p0) + (p1 - q1)
    paddsb              m4, m15                         ; 2*(q0 - p0) + (p1 - q1)
    paddsb              m4, m15                         ; 3*(q0 - p0) + (p1 - q1)
    paddsb              m6, m4, [pb_4]                  ; m6: f1 = clip(f + 4, 127)
    paddsb              m4, [pb_3]                      ; m4: f2 = clip(f + 3, 127)
    mova                m14, [pb_10]                    ; will be reused in filter4()
    SRSHIFT3B_2X        m6, m4, m14, m7                 ; f1 and f2 sign byte shift by 3
    SIGN_SUB            m7, m12, m6, m5, m9             ; m7 = q0 - f1
    SIGN_ADD            m8, m11, m4, m5, m9             ; m8 = p0 + f2
    pandn               m6, m2, m3                      ;  ~mask(in) & mask(fm)
    pand                m6, m0                          ; (~mask(in) & mask(fm)) & mask(hev)
    MASK_APPLY          m7, m12, m6, m5                 ; m7 = filter2(q0) & mask / we write it in filter4()
    MASK_APPLY          m8, m11, m6, m5                 ; m8 = filter2(p0) & mask / we write it in filter4()

    ; (m0: hev, m1: flat8out, m2: flat8in, m3: fm, m7..m8: q0' p0', m10..13: p1 p0 q0 q1, m14: pb_10, m15: q0-p0)
    ; filter4()
    mova                m4, m15
    paddsb              m15, m4                         ; 2 * (q0 - p0)
    paddsb              m15, m4                         ; 3 * (q0 - p0)
    paddsb              m6, m15, [pb_4]                 ; m6:  f1 = clip(f + 4, 127)
    paddsb              m15, [pb_3]                     ; m15: f2 = clip(f + 3, 127)
    SRSHIFT3B_2X        m6, m15, m14, m9                ; f1 and f2 sign byte shift by 3
    pandn               m5, m2, m3                      ;               ~mask(in) & mask(fm)
    pandn               m0, m5                          ; ~mask(hev) & (~mask(in) & mask(fm))
    SIGN_SUB            m9, m12, m6, m4, m14            ; q0 - f1
    MASK_APPLY          m9, m7, m0, m5                  ; m9 = filter4(q0) & mask
%ifidn %1, v
    mova                [dstq], m9                      ; update q0
%else
    mova                [rsp + 128], m9                 ; update q0
%endif
    SIGN_ADD            m7, m11, m15, m4, m14           ; p0 + f2
    MASK_APPLY          m7, m8, m0, m5                  ; m7 = filter4(p0) & mask
%ifidn %1, v
    mova                [dstq + 1*mstrideq], m7         ; update p0
%else
    mova                [rsp + 112], m7                 ; update p0
%endif
    paddb               m6, [pb_80]                     ;
    pxor                m8, m8                          ;   f=(f1+1)>>1
    pavgb               m6, m8                          ;
    psubb               m6, [pb_40]                     ;
    SIGN_ADD            m7, m10, m6, m8, m9             ; p1 + f
    SIGN_SUB            m4, m13, m6, m8, m9             ; q1 - f
    MASK_APPLY          m7, m10, m0, m14                ; m7 = filter4(p1)
    MASK_APPLY          m4, m13, m0, m14                ; m4 = filter4(q1)
%ifidn %1, v
    mova                [dstq + 2*mstrideq], m7         ; update p1
    mova                [dstq + 1* strideq], m4         ; update q1
%else
    mova                [rsp +  96], m7                 ; update p1
    mova                [rsp + 144], m4                 ; update q1
%endif

    ; (m1: flat8out, m2: flat8in, m3: fm, m10..13: p1 p0 q0 q1)
    ; filter6()
    pxor                m0, m0
    pand                m2, m3                          ;               mask(fm) & mask(in)
    pandn               m3, m1, m2                      ; ~mask(out) & (mask(fm) & mask(in))
%ifidn %1, v
    lea              dst1q, [dstq + 2*strideq]          ; dst1 = &dst[stride * +2] (q2)
    mova                m8, [dst1q]                     ; m8 = q2
    mova                m9, [dst1q + 1*strideq]         ; m9 = q3
    lea              dst1q, [dstq + 4*mstrideq]         ; dst1 = &dst[stride * -4] (p3)
    lea              dst2q, [dst1q + 1*strideq]         ; dst2 = &dst[stride * -3] (p2)
    mova               m14, [dst1q]                     ; m14 = p3
    mova               m15, [dst2q]                     ; m15 = p2
    FILTER_INIT         m4, m5, m6, m7, [dst2q            ], 6,                     m3, m15 ; [p2]
    FILTER_UPDATE       m6, m7, m4, m5, [dst2q + 1*strideq], m14, m15, m10, m13, 3, m3      ; [p1] -p3 -p2 +p1 +q1
    FILTER_UPDATE       m4, m5, m6, m7, [dst2q + 2*strideq], m14, m10, m11,  m8, 3, m3      ; [p0] -p3 -p1 +p0 +q2
    FILTER_UPDATE       m6, m7, m4, m5, [dstq             ], m14, m11, m12,  m9, 3, m3      ; [q0] -p3 -p0 +q0 +q3
    FILTER_UPDATE       m4, m5, m6, m7, [dstq  + 1*strideq], m15, m12, m13,  m9, 3, m3      ; [q1] -p2 -q0 +q1 +q3
    FILTER_UPDATE       m6, m7, m4, m5, [dstq  + 2*strideq], m10, m13,  m8,  m9, 3, m3,  m8 ; [q2] -p1 -q1 +q2 +q3
%else
    mova               m14, [rsp +  64]                 ; m14 = p3
    mova               m15, [rsp +  80]                 ; m15 = p2
    mova                m8, [rsp + 160]                 ;  m8 = q2
    mova                m9, [rsp + 176]                 ;  m9 = q3
    FILTER_INIT         m4, m5, m6, m7, [rsp +  80], 6,                     m3, m15 ; [p2]
    FILTER_UPDATE       m6, m7, m4, m5, [rsp +  96], m14, m15, m10, m13, 3, m3      ; [p1] -p3 -p2 +p1 +q1
    FILTER_UPDATE       m4, m5, m6, m7, [rsp + 112], m14, m10, m11,  m8, 3, m3      ; [p0] -p3 -p1 +p0 +q2
    FILTER_UPDATE       m6, m7, m4, m5, [rsp + 128], m14, m11, m12,  m9, 3, m3      ; [q0] -p3 -p0 +q0 +q3
    FILTER_UPDATE       m4, m5, m6, m7, [rsp + 144], m15, m12, m13,  m9, 3, m3      ; [q1] -p2 -q0 +q1 +q3
    FILTER_UPDATE       m6, m7, m4, m5, [rsp + 160], m10, m13,  m8,  m9, 3, m3,  m8 ; [q2] -p1 -q1 +q2 +q3
%endif

    ; (m0: 0, m1: flat8out, m2: fm & flat8in, m8..15: q2 q3 p1 p0 q0 q1 p3 p2)
    ; filter14()
    ;
    ;                            m2  m3  m8  m9 m14 m15 m10 m11 m12 m13
    ;
    ;                                    q2  q3  p3  p2  p1  p0  q0  q1
    ; p6  -7                     p7  p6  p5  p4   .   .   .   .   .
    ; p5  -6  -p7 -p6 +p5 +q1     .   .   .                           .
    ; p4  -5  -p7 -p5 +p4 +q2     .       .   .                      q2
    ; p3  -4  -p7 -p4 +p3 +q3     .           .   .                  q3
    ; p2  -3  -p7 -p3 +p2 +q4     .               .   .              q4
    ; p1  -2  -p7 -p2 +p1 +q5     .                   .   .          q5
    ; p0  -1  -p7 -p1 +p0 +q6     .                       .   .      q6
    ; q0  +0  -p7 -p0 +q0 +q7     .                           .   .  q7
    ; q1  +1  -p6 -q0 +q1 +q7    q1   .                           .   .
    ; q2  +2  -p5 -q1 +q2 +q7     .  q2   .                           .
    ; q3  +3  -p4 -q2 +q3 +q7         .  q3   .                       .
    ; q4  +4  -p3 -q3 +q4 +q7             .  q4   .                   .
    ; q5  +5  -p2 -q4 +q5 +q7                 .  q5   .               .
    ; q6  +6  -p1 -q5 +q6 +q7                     .  q6   .           .

    pand            m1, m2                                                              ; mask(out) & (mask(fm) & mask(in))
%ifidn %1, v
    lea             dst1q, [dstq  + 8*mstrideq]                                         ; dst1 = &dst[stride * -8] (p7)
    lea             dst2q, [dst1q + 1* strideq]                                         ; dst2 = &dst[stride * -7] (p6)
    mova            m2, [dst1q]                                                         ; m2 = p7
    mova            m3, [dst2q]                                                         ; m3 = p6
    mova            m8, [dst1q + 2*strideq]                                             ; m8 = p5 (dst[stride * -6])
    mova            m9, [dst2q + 2*strideq]                                             ; m9 = p4 (dst[stride * -5])
    FILTER_INIT     m4, m5, m6, m7, [dst2q], 14,                                m1,  m3 ; [p6]
    FILTER_UPDATE   m6, m7, m4, m5, [dst2q + 1*strideq],  m2,  m3,  m8, m13, 4, m1,  m8 ; [p5] -p7 -p6 +p5 +q1
    lea             dst1q, [dstq + 1*strideq]                                           ; dst1 = &dst[stride * +1] (q1)
    mova            m13, [dst1q + 1*strideq]                                            ; m13=dst[stride * +2] (q2)
    FILTER_UPDATE   m4, m5, m6, m7, [dst2q + 2*strideq],  m2,  m8,  m9, m13, 4, m1,  m9 ; [p4] -p7 -p5 +p4 +q2
    lea             dst2q, [dst2q + 4*strideq]
    mova            m13, [dst1q + 2*strideq]                                            ; m13=dst[stride * +3] (q3)
    FILTER_UPDATE   m6, m7, m4, m5, [dst2q + 1*mstrideq], m2,  m9, m14, m13, 4, m1, m14 ; [p3] -p7 -p4 +p3 +q3
    mova            m13, [dstq  + 4*strideq]                                            ; m13=dst[stride * +4] (q4)
    FILTER_UPDATE   m4, m5, m6, m7, [dst2q],              m2, m14, m15, m13, 4, m1      ; [p2] -p7 -p3 +p2 +q4
    mova            m13, [dst1q + 4*strideq]                                            ; m13=dst[stride * +5] (q5)
    FILTER_UPDATE   m6, m7, m4, m5, [dst2q + 1*strideq],  m2, m15, m10, m13, 4, m1      ; [p1] -p7 -p2 +p1 +q5
    lea             dst1q, [dst1q + 4*strideq]                                          ; dst1 = &dst[stride * +5] (q5)
    mova            m13, [dst1q + 1*strideq]                                            ; m13=dst[stride * +6] (q6)
    FILTER_UPDATE   m4, m5, m6, m7, [dst2q + 2*strideq],  m2, m10, m11, m13, 4, m1      ; [p0] -p7 -p1 +p0 +q6
    lea             dst2q, [dst2q + 4*strideq]
    mova            m13, [dst1q + 2*strideq]                                            ; m13=dst[stride * +7] (q7)
    FILTER_UPDATE   m6, m7, m4, m5, [dst2q + 1*mstrideq], m2, m11, m12, m13, 4, m1      ; [q0] -p7 -p0 +q0 +q7
    mova            m2, [dst2q]                                                         ; m2=dst[stride * +1] (q1)
    FILTER_UPDATE   m4, m5, m6, m7, [dst2q],              m3, m12,  m2, m13, 4, m1      ; [q1] -p6 -q0 +q1 +q7
    mova            m3, [dst2q + 1*strideq]                                             ; m3=dst[stride * +2] (q2)
    FILTER_UPDATE   m6, m7, m4, m5, [dst2q + 1*strideq],  m8,  m2,  m3, m13, 4, m1      ; [q2] -p5 -q1 +q2 +q7
    mova            m8, [dst2q + 2*strideq]                                             ; m8=dst[stride * +3] (q3)
    FILTER_UPDATE   m4, m5, m6, m7, [dst2q + 2*strideq],  m9,  m3,  m8, m13, 4, m1,  m8 ; [q3] -p4 -q2 +q3 +q7
    lea             dst2q, [dst2q + 4*strideq]
    mova            m9, [dst2q + 1*mstrideq]                                            ; m9=dst[stride * +4] (q4)
    FILTER_UPDATE   m6, m7, m4, m5, [dst2q + 1*mstrideq],m14,  m8,  m9, m13, 4, m1,  m9 ; [q4] -p3 -q3 +q4 +q7
    mova            m14, [dst2q]                                                        ; m14=dst[stride * +5] (q5)
    FILTER_UPDATE   m4, m5, m6, m7, [dst2q],             m15,  m9, m14, m13, 4, m1, m14 ; [q5] -p2 -q4 +q5 +q7
    mova            m15, [dst2q + 1*strideq]                                            ; m15=dst[stride * +6] (q6)
    FILTER_UPDATE   m6, m7, m4, m5, [dst2q + 1*strideq], m10, m14, m15, m13, 4, m1, m15 ; [q6] -p1 -q5 +q6 +q7
%else
    mova            m2, [rsp +  0]                                                      ; m2 = p7
    mova            m3, [rsp + 16]                                                      ; m3 = p6
    mova            m8, [rsp + 32]                                                      ; m8 = p5
    mova            m9, [rsp + 48]                                                      ; m9 = p4
    FILTER_INIT     m4, m5, m6, m7, [rsp +  16],  14,                   m1,  m3         ; [p6]
    FILTER_UPDATE   m6, m7, m4, m5, [rsp +  32],  m2,  m3,  m8, m13, 4, m1,  m8         ; [p5] -p7 -p6 +p5 +q1
    mova            m13, [rsp + 160]                                                    ; m13 = q2
    FILTER_UPDATE   m4, m5, m6, m7, [rsp +  48],  m2,  m8,  m9, m13, 4, m1,  m9         ; [p4] -p7 -p5 +p4 +q2
    mova            m13, [rsp + 176]                                                    ; m13 = q3
    FILTER_UPDATE   m6, m7, m4, m5, [rsp +  64],  m2,  m9, m14, m13, 4, m1, m14         ; [p3] -p7 -p4 +p3 +q3
    mova            m13, [rsp + 192]                                                    ; m13 = q4
    FILTER_UPDATE   m4, m5, m6, m7, [rsp +  80],  m2, m14, m15, m13, 4, m1              ; [p2] -p7 -p3 +p2 +q4
    mova            m13, [rsp + 208]                                                    ; m13 = q5
    FILTER_UPDATE   m6, m7, m4, m5, [rsp +  96],  m2, m15, m10, m13, 4, m1              ; [p1] -p7 -p2 +p1 +q5
    mova            m13, [rsp + 224]                                                    ; m13 = q6
    FILTER_UPDATE   m4, m5, m6, m7, [rsp + 112],  m2, m10, m11, m13, 4, m1              ; [p0] -p7 -p1 +p0 +q6
    mova            m13, [rsp + 240]                                                    ; m13 = q7
    FILTER_UPDATE   m6, m7, m4, m5, [rsp + 128],  m2, m11, m12, m13, 4, m1              ; [q0] -p7 -p0 +q0 +q7
    mova            m2,  [rsp + 144]                                                    ; m2 = q1
    FILTER_UPDATE   m4, m5, m6, m7, [rsp + 144],  m3, m12,  m2, m13, 4, m1              ; [q1] -p6 -q0 +q1 +q7
    mova            m3,  [rsp + 160]                                                    ; m3 = q2
    FILTER_UPDATE   m6, m7, m4, m5, [rsp + 160],  m8,  m2,  m3, m13, 4, m1              ; [q2] -p5 -q1 +q2 +q7
    mova            m8,  [rsp + 176]                                                    ; m8 = q3
    FILTER_UPDATE   m4, m5, m6, m7, [rsp + 176],  m9,  m3,  m8, m13, 4, m1,  m8         ; [q3] -p4 -q2 +q3 +q7
    mova            m9,  [rsp + 192]                                                    ; m9 = q4
    FILTER_UPDATE   m6, m7, m4, m5, [rsp + 192], m14,  m8,  m9, m13, 4, m1,  m9         ; [q4] -p3 -q3 +q4 +q7
    mova            m14, [rsp + 208]                                                    ; m14 = q5
    FILTER_UPDATE   m4, m5, m6, m7, [rsp + 208], m15,  m9, m14, m13, 4, m1, m14         ; [q5] -p2 -q4 +q5 +q7
    mova            m15, [rsp + 224]                                                    ; m15 = q6
    FILTER_UPDATE   m6, m7, m4, m5, [rsp + 224], m10, m14, m15, m13, 4, m1, m15         ; [q6] -p1 -q5 +q6 +q7
%endif

%ifidn %1, h
    mova                    m0, [rsp +   0]                                             ; dst[stride * -8] (p7)
    mova                    m1, [rsp +  16]                                             ; dst[stride * -7] (p6)
    mova                    m2, [rsp +  32]                                             ; dst[stride * -6] (p5)
    mova                    m3, [rsp +  48]                                             ; dst[stride * -5] (p4)
    mova                    m4, [rsp +  64]                                             ; dst[stride * -4] (p3)
    mova                    m5, [rsp +  80]                                             ; dst[stride * -3] (p2)
    mova                    m6, [rsp +  96]                                             ; dst[stride * -2] (p1)
    mova                    m7, [rsp + 112]                                             ; dst[stride * -1] (p0)
    mova                    m8, [rsp + 128]                                             ; dst[stride * +0] (q0)
    mova                    m9, [rsp + 144]                                             ; dst[stride * +1] (q1)
    mova                   m10, [rsp + 160]                                             ; dst[stride * +2] (q2)
    mova                   m11, [rsp + 176]                                             ; dst[stride * +3] (q3)
    mova                   m12, [rsp + 192]                                             ; dst[stride * +4] (q4)
    mova                   m13, [rsp + 208]                                             ; dst[stride * +5] (q5)
    mova                   m14, [rsp + 224]                                             ; dst[stride * +6] (q6)
    mova                   m15, [rsp + 240]                                             ; dst[stride * +7] (q7)
    TRANSPOSE16x16B 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, [rsp]
    lea                  dst1q, [dstq  + 8*mstrideq]                                    ; dst1 = &dst[stride * -8]
    lea                  dst2q, [dst1q + 1* strideq]                                    ; dst2 = &dst[stride * -7]
    movu  [dst1q             ],  m0                                                     ; dst[stride * -8] (p7)
    movu  [dst2q             ],  m1                                                     ; dst[stride * -7] (p6)
    movu  [dst1q + 2* strideq],  m2                                                     ; dst[stride * -6] (p5)
    movu  [dst2q + 2* strideq],  m3                                                     ; dst[stride * -5] (p4)
    lea                  dst1q, [dstq]                                                  ; dst1 = &dst[stride * +0]
    lea                  dst2q, [dstq + 1*strideq]                                      ; dst2 = &dst[stride * +1]
    movu  [dst1q + 4*mstrideq],  m4                                                     ; dst[stride * -4] (p3)
    movu  [dst2q + 4*mstrideq],  m5                                                     ; dst[stride * -3] (p2)
    movu  [dst1q + 2*mstrideq],  m6                                                     ; dst[stride * -2] (p1)
    movu  [dst2q + 2*mstrideq],  m7                                                     ; dst[stride * -1] (p0)
    movu  [dst1q             ],  m8                                                     ; dst[stride * +0] (q0)
    movu  [dst2q             ],  m9                                                     ; dst[stride * +1] (q1)
    movu  [dst1q + 2* strideq], m10                                                     ; dst[stride * +2] (q2)
    movu  [dst2q + 2* strideq], m11                                                     ; dst[stride * +3] (q3)
    movu  [dst1q + 4* strideq], m12                                                     ; dst[stride * +4] (q4)
    movu  [dst2q + 4* strideq], m13                                                     ; dst[stride * +5] (q5)
    lea                  dst1q, [dstq + 8*strideq]                                      ; dst1 = &dst[stride * +8]
    movu  [dst1q + 2*mstrideq], m14                                                     ; dst[stride * +6] (q6)
    movu  [dst1q + 1*mstrideq], m15                                                     ; dst[stride * +7] (q7)
%endif
%endmacro

%macro LPF_16_16_VH 1
INIT_XMM %1
cglobal vp9_loop_filter_v_16_16, 5,8,16,      dst, stride, E, I, H, mstride, dst1, dst2
    LPF_16_16 v
    RET
cglobal vp9_loop_filter_h_16_16, 5,8,16, 256, dst, stride, E, I, H, mstride, dst1, dst2
    LPF_16_16 h
    RET
%endmacro

LPF_16_16_VH sse2
LPF_16_16_VH ssse3
LPF_16_16_VH avx

%endif ; x86-64
