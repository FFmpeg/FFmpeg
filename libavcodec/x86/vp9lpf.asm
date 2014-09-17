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

%include "libavutil/x86/x86util.asm"

%if ARCH_X86_64

SECTION_RODATA

cextern pb_3
cextern pb_80

pb_4:   times 16 db 0x04
pb_10:  times 16 db 0x10
pb_40:  times 16 db 0x40
pb_81:  times 16 db 0x81
pb_f8:  times 16 db 0xf8
pb_fe:  times 16 db 0xfe

cextern pw_4
cextern pw_8

; with mix functions, two 8-bit thresholds are stored in a 16-bit storage,
; the following mask is used to splat both in the same register
mask_mix: times 8 db 0
          times 8 db 1

mask_mix84: times 8 db 0xff
            times 8 db 0x00
mask_mix48: times 8 db 0x00
            times 8 db 0xff

SECTION .text

; %1 = abs(%2-%3)
%macro ABSSUB 4 ; dst, src1 (RO), src2 (RO), tmp
    psubusb             %1, %3, %2
    psubusb             %4, %2, %3
    por                 %1, %4
%endmacro

; %1 = %1<=%2
%macro CMP_LTE 3-4 ; src/dst, cmp, tmp, pb_80
%if %0 == 4
    pxor                %1, %4
%endif
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
    paddw               %3, %1
    psraw               %1, %3, %8
%endmacro

%macro FILTER_INIT 8 ; tmp1, tmp2, cacheL, cacheH, dstp, filterid, mask, source
    FILTER%6_INIT       %1, l, %3
    FILTER%6_INIT       %2, h, %4
    packuswb            %1, %2
    MASK_APPLY          %1, %8, %7, %2
    mova                %5, %1
%endmacro

%macro FILTER_UPDATE 11-14 ; tmp1, tmp2, cacheL, cacheH, dstp, -, -, +, +, rshift, mask, [source], [preload reg + value]
%if %0 == 13 ; no source + preload
    mova                %12, %13
%elif %0 == 14 ; source + preload
    mova                %13, %14
%endif
    FILTER_SUBx2_ADDx2  %1, l, %3, %6, %7, %8, %9, %10
    FILTER_SUBx2_ADDx2  %2, h, %4, %6, %7, %8, %9, %10
    packuswb            %1, %2
%if %0 == 12 || %0 == 14
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
    punpck%2bw          %1, m14, m0                     ; p3: B->W
    paddw               %3, %1, %1                      ; p3*2
    paddw               %3, %1                          ; p3*3
    punpck%2bw          %1, m15, m0                     ; p2: B->W
    paddw               %3, %1                          ; p3*3 + p2
    paddw               %3, %1                          ; p3*3 + p2*2
    punpck%2bw          %1, m10, m0                     ; p1: B->W
    paddw               %3, %1                          ; p3*3 + p2*2 + p1
    punpck%2bw          %1, m11, m0                     ; p0: B->W
    paddw               %3, %1                          ; p3*3 + p2*2 + p1 + p0
    punpck%2bw          %1, m12, m0                     ; q0: B->W
    paddw               %3, %1                          ; p3*3 + p2*2 + p1 + p0 + q0
    paddw               %3, [pw_4]                      ; p3*3 + p2*2 + p1 + p0 + q0 + 4
    psraw               %1, %3, 3                       ; (p3*3 + p2*2 + p1 + p0 + q0 + 4) >> 3
%endmacro

%macro FILTER14_INIT 3 ; %1=dst %2=h/l %3=cache
    punpck%2bw          %1, m2, m0                      ; p7: B->W
    psllw               %3, %1, 3                       ; p7*8
    psubw               %3, %1                          ; p7*7
    punpck%2bw          %1, m3, m0                      ; p6: B->W
    paddw               %3, %1                          ; p7*7 + p6
    paddw               %3, %1                          ; p7*7 + p6*2
    punpck%2bw          %1, m8, m0                      ; p5: B->W
    paddw               %3, %1                          ; p7*7 + p6*2 + p5
    punpck%2bw          %1, m9, m0                      ; p4: B->W
    paddw               %3, %1                          ; p7*7 + p6*2 + p5 + p4
    punpck%2bw          %1, m14, m0                     ; p3: B->W
    paddw               %3, %1                          ; p7*7 + p6*2 + p5 + p4 + p3
    punpck%2bw          %1, m15, m0                     ; p2: B->W
    paddw               %3, %1                          ; p7*7 + p6*2 + p5 + .. + p2
    punpck%2bw          %1, m10, m0                     ; p1: B->W
    paddw               %3, %1                          ; p7*7 + p6*2 + p5 + .. + p1
    punpck%2bw          %1, m11, m0                     ; p0: B->W
    paddw               %3, %1                          ; p7*7 + p6*2 + p5 + .. + p0
    punpck%2bw          %1, m12, m0                     ; q0: B->W
    paddw               %3, %1                          ; p7*7 + p6*2 + p5 + .. + p0 + q0
    paddw               %3, [pw_8]                      ; p7*7 + p6*2 + p5 + .. + p0 + q0 + 8
    psraw               %1, %3, 4                       ; (p7*7 + p6*2 + p5 + .. + p0 + q0 + 8) >> 4
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

; transpose 16 half lines (high part) to 8 full centered lines
%macro TRANSPOSE16x8B 16
    punpcklbw   m%1,  m%2
    punpcklbw   m%3,  m%4
    punpcklbw   m%5,  m%6
    punpcklbw   m%7,  m%8
    punpcklbw   m%9,  m%10
    punpcklbw   m%11, m%12
    punpcklbw   m%13, m%14
    punpcklbw   m%15, m%16
    SBUTTERFLY  wd,  %1,  %3,  %2
    SBUTTERFLY  wd,  %5,  %7,  %2
    SBUTTERFLY  wd,  %9,  %11, %2
    SBUTTERFLY  wd,  %13, %15, %2
    SBUTTERFLY  dq,  %1,  %5,  %2
    SBUTTERFLY  dq,  %3,  %7,  %2
    SBUTTERFLY  dq,  %9,  %13, %2
    SBUTTERFLY  dq,  %11, %15, %2
    SBUTTERFLY  qdq, %1,  %9,  %2
    SBUTTERFLY  qdq, %3,  %11, %2
    SBUTTERFLY  qdq, %5,  %13, %2
    SBUTTERFLY  qdq, %7,  %15, %2
    SWAP %5, %1
    SWAP %6, %9
    SWAP %7, %1
    SWAP %8, %13
    SWAP %9, %3
    SWAP %10, %11
    SWAP %11, %1
    SWAP %12, %15
%endmacro

%macro DEFINE_REAL_P7_TO_Q7 0-1 0
%define P7 dst1q + 2*mstrideq  + %1
%define P6 dst1q +   mstrideq  + %1
%define P5 dst1q               + %1
%define P4 dst1q +    strideq  + %1
%define P3 dstq  + 4*mstrideq  + %1
%define P2 dstq  +   mstride3q + %1
%define P1 dstq  + 2*mstrideq  + %1
%define P0 dstq  +   mstrideq  + %1
%define Q0 dstq                + %1
%define Q1 dstq  +   strideq   + %1
%define Q2 dstq  + 2*strideq   + %1
%define Q3 dstq  +   stride3q  + %1
%define Q4 dstq  + 4*strideq   + %1
%define Q5 dst2q + mstrideq    + %1
%define Q6 dst2q               + %1
%define Q7 dst2q +  strideq    + %1
%endmacro

; ..............AB -> AAAAAAAABBBBBBBB
%macro SPLATB_MIX 1-2 [mask_mix]
%if cpuflag(ssse3)
    pshufb     %1, %2
%else
    punpcklbw  %1, %1
    punpcklwd  %1, %1
    punpckldq  %1, %1
%endif
%endmacro

%macro LOOPFILTER 2 ; %1=v/h %2=size1
    lea mstrideq, [strideq]
    neg mstrideq

    lea stride3q, [strideq+2*strideq]
    mov mstride3q, stride3q
    neg mstride3q

%ifidn %1, h
%if %2 > 16
%define movx movh
    lea dstq, [dstq + 8*strideq - 4]
%else
%define movx movu
    lea dstq, [dstq + 8*strideq - 8] ; go from top center (h pos) to center left (v pos)
%endif
%endif

    lea dst1q, [dstq + 2*mstride3q]                         ; dst1q = &dst[stride * -6]
    lea dst2q, [dstq + 2* stride3q]                         ; dst2q = &dst[stride * +6]

    DEFINE_REAL_P7_TO_Q7

%ifidn %1, h
    movx                    m0, [P7]
    movx                    m1, [P6]
    movx                    m2, [P5]
    movx                    m3, [P4]
    movx                    m4, [P3]
    movx                    m5, [P2]
    movx                    m6, [P1]
    movx                    m7, [P0]
    movx                    m8, [Q0]
    movx                    m9, [Q1]
    movx                   m10, [Q2]
    movx                   m11, [Q3]
    movx                   m12, [Q4]
    movx                   m13, [Q5]
    movx                   m14, [Q6]
    movx                   m15, [Q7]
%define P7 rsp +   0
%define P6 rsp +  16
%define P5 rsp +  32
%define P4 rsp +  48
%define P3 rsp +  64
%define P2 rsp +  80
%define P1 rsp +  96
%define P0 rsp + 112
%define Q0 rsp + 128
%define Q1 rsp + 144
%define Q2 rsp + 160
%define Q3 rsp + 176
%define Q4 rsp + 192
%define Q5 rsp + 208
%define Q6 rsp + 224
%define Q7 rsp + 240

%if %2 == 16
    TRANSPOSE16x16B 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, [rsp]
    mova           [P7],  m0
    mova           [P6],  m1
    mova           [P5],  m2
    mova           [P4],  m3
%else
    TRANSPOSE16x8B 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
%endif
    mova           [P3],  m4
    mova           [P2],  m5
    mova           [P1],  m6
    mova           [P0],  m7
    mova           [Q0],  m8
    mova           [Q1],  m9
    mova           [Q2], m10
    mova           [Q3], m11
%if %2 == 16
    mova           [Q4], m12
    mova           [Q5], m13
    mova           [Q6], m14
    mova           [Q7], m15
%endif
%endif

    ; calc fm mask
%if %2 == 16
%if cpuflag(ssse3)
    pxor                m0, m0
%endif
    SPLATB_REG          m2, I, m0                       ; I I I I ...
    SPLATB_REG          m3, E, m0                       ; E E E E ...
%else
%if cpuflag(ssse3)
    mova                m0, [mask_mix]
%endif
    movd                m2, Id
    movd                m3, Ed
    SPLATB_MIX          m2, m0
    SPLATB_MIX          m3, m0
%endif
    mova                m0, [pb_80]
    pxor                m2, m0
    pxor                m3, m0
%ifidn %1, v
    mova                m8, [P3]
    mova                m9, [P2]
    mova               m10, [P1]
    mova               m11, [P0]
    mova               m12, [Q0]
    mova               m13, [Q1]
    mova               m14, [Q2]
    mova               m15, [Q3]
%else
    ; In case of horizontal, P3..Q3 are already present in some registers due
    ; to the previous transpose, so we just swap registers.
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
    ; calc flat8in (if not 44_16) and hev masks
    mova                m6, [pb_81]                     ; [1 1 1 1 ...] ^ 0x80
%if %2 != 44
    ABSSUB_CMP          m2, m8, m11, m6, m4, m5         ; abs(p3 - p0) <= 1
    mova                m8, [pb_80]
    ABSSUB_CMP          m1, m9, m11, m6, m4, m5, m8     ; abs(p2 - p0) <= 1
    pand                m2, m1
    ABSSUB              m4, m10, m11, m5                ; abs(p1 - p0)
%if %2 == 16
%if cpuflag(ssse3)
    pxor                m0, m0
%endif
    SPLATB_REG          m7, H, m0                       ; H H H H ...
%else
    movd                m7, Hd
    SPLATB_MIX          m7
%endif
    pxor                m7, m8
    pxor                m4, m8
    pcmpgtb             m0, m4, m7                      ; abs(p1 - p0) > H (1/2 hev condition)
    CMP_LTE             m4, m6, m5                      ; abs(p1 - p0) <= 1
    pand                m2, m4                          ; (flat8in)
    ABSSUB              m4, m13, m12, m1                ; abs(q1 - q0)
    pxor                m4, m8
    pcmpgtb             m5, m4, m7                      ; abs(q1 - q0) > H (2/2 hev condition)
    por                 m0, m5                          ; hev final value
    CMP_LTE             m4, m6, m5                      ; abs(q1 - q0) <= 1
    pand                m2, m4                          ; (flat8in)
    ABSSUB_CMP          m1, m14, m12, m6, m4, m5, m8    ; abs(q2 - q0) <= 1
    pand                m2, m1
    ABSSUB_CMP          m1, m15, m12, m6, m4, m5, m8    ; abs(q3 - q0) <= 1
    pand                m2, m1                          ; flat8in final value
%if %2 == 84 || %2 == 48
    pand                m2, [mask_mix%2]
%endif
%else
    mova                m6, [pb_80]
    movd                m7, Hd
    SPLATB_MIX          m7
    pxor                m7, m6
    ABSSUB              m4, m10, m11, m1                ; abs(p1 - p0)
    pxor                m4, m6
    pcmpgtb             m0, m4, m7                      ; abs(p1 - p0) > H (1/2 hev condition)
    ABSSUB              m4, m13, m12, m1                ; abs(q1 - q0)
    pxor                m4, m6
    pcmpgtb             m5, m4, m7                      ; abs(q1 - q0) > H (2/2 hev condition)
    por                 m0, m5                          ; hev final value
%endif

%if %2 == 16
    ; (m0: hev, m2: flat8in, m3: fm, m6: pb_81, m9..15: p2 p1 p0 q0 q1 q2 q3)
    ; calc flat8out mask
    mova                m8, [P7]
    mova                m9, [P6]
    ABSSUB_CMP          m1, m8, m11, m6, m4, m5         ; abs(p7 - p0) <= 1
    ABSSUB_CMP          m7, m9, m11, m6, m4, m5         ; abs(p6 - p0) <= 1
    pand                m1, m7
    mova                m8, [P5]
    mova                m9, [P4]
    ABSSUB_CMP          m7, m8, m11, m6, m4, m5         ; abs(p5 - p0) <= 1
    pand                m1, m7
    ABSSUB_CMP          m7, m9, m11, m6, m4, m5         ; abs(p4 - p0) <= 1
    pand                m1, m7
    mova                m14, [Q4]
    mova                m15, [Q5]
    ABSSUB_CMP          m7, m14, m12, m6, m4, m5        ; abs(q4 - q0) <= 1
    pand                m1, m7
    ABSSUB_CMP          m7, m15, m12, m6, m4, m5        ; abs(q5 - q0) <= 1
    pand                m1, m7
    mova                m14, [Q6]
    mova                m15, [Q7]
    ABSSUB_CMP          m7, m14, m12, m6, m4, m5        ; abs(q4 - q0) <= 1
    pand                m1, m7
    ABSSUB_CMP          m7, m15, m12, m6, m4, m5        ; abs(q5 - q0) <= 1
    pand                m1, m7                          ; flat8out final value
%endif

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

    ; (m0: hev, [m1: flat8out], [m2: flat8in], m3: fm, m8..15: p5 p4 p1 p0 q0 q1 q6 q7)
    ; filter2()
%if %2 != 44
    mova                m6, [pb_80]                     ; already in m6 if 44_16
%endif
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
%if %2 != 44
    pandn               m6, m2, m3                      ;  ~mask(in) & mask(fm)
    pand                m6, m0                          ; (~mask(in) & mask(fm)) & mask(hev)
%else
    pand                m6, m3, m0
%endif
    MASK_APPLY          m7, m12, m6, m5                 ; m7 = filter2(q0) & mask / we write it in filter4()
    MASK_APPLY          m8, m11, m6, m5                 ; m8 = filter2(p0) & mask / we write it in filter4()

    ; (m0: hev, [m1: flat8out], [m2: flat8in], m3: fm, m7..m8: q0' p0', m10..13: p1 p0 q0 q1, m14: pb_10, m15: q0-p0)
    ; filter4()
    mova                m4, m15
    paddsb              m15, m4                         ; 2 * (q0 - p0)
    paddsb              m15, m4                         ; 3 * (q0 - p0)
    paddsb              m6, m15, [pb_4]                 ; m6:  f1 = clip(f + 4, 127)
    paddsb              m15, [pb_3]                     ; m15: f2 = clip(f + 3, 127)
    SRSHIFT3B_2X        m6, m15, m14, m9                ; f1 and f2 sign byte shift by 3
%if %2 != 44
%define p0tmp m7
%define q0tmp m9
    pandn               m5, m2, m3                      ;               ~mask(in) & mask(fm)
    pandn               m0, m5                          ; ~mask(hev) & (~mask(in) & mask(fm))
%else
%define p0tmp m1
%define q0tmp m2
    pandn               m0, m3
%endif
    SIGN_SUB            q0tmp, m12, m6, m4, m14         ; q0 - f1
    MASK_APPLY          q0tmp, m7, m0, m5               ; filter4(q0) & mask
    mova                [Q0], q0tmp
    SIGN_ADD            p0tmp, m11, m15, m4, m14        ; p0 + f2
    MASK_APPLY          p0tmp, m8, m0, m5               ; filter4(p0) & mask
    mova                [P0], p0tmp
    paddb               m6, [pb_80]                     ;
    pxor                m8, m8                          ;   f=(f1+1)>>1
    pavgb               m6, m8                          ;
    psubb               m6, [pb_40]                     ;
    SIGN_ADD            m7, m10, m6, m8, m9             ; p1 + f
    SIGN_SUB            m4, m13, m6, m8, m9             ; q1 - f
    MASK_APPLY          m7, m10, m0, m14                ; m7 = filter4(p1)
    MASK_APPLY          m4, m13, m0, m14                ; m4 = filter4(q1)
    mova                [P1], m7
    mova                [Q1], m4

    ; ([m1: flat8out], m2: flat8in, m3: fm, m10..13: p1 p0 q0 q1)
    ; filter6()
%if %2 != 44
    pxor                m0, m0
%if %2 > 16
    pand                m3, m2
%else
    pand                m2, m3                          ;               mask(fm) & mask(in)
    pandn               m3, m1, m2                      ; ~mask(out) & (mask(fm) & mask(in))
%endif
    mova               m14, [P3]
    mova               m15, [P2]
    mova                m8, [Q2]
    mova                m9, [Q3]
    FILTER_INIT         m4, m5, m6, m7, [P2], 6,                     m3, m15    ; [p2]
    FILTER_UPDATE       m6, m7, m4, m5, [P1], m14, m15, m10, m13, 3, m3         ; [p1] -p3 -p2 +p1 +q1
    FILTER_UPDATE       m4, m5, m6, m7, [P0], m14, m10, m11,  m8, 3, m3         ; [p0] -p3 -p1 +p0 +q2
    FILTER_UPDATE       m6, m7, m4, m5, [Q0], m14, m11, m12,  m9, 3, m3         ; [q0] -p3 -p0 +q0 +q3
    FILTER_UPDATE       m4, m5, m6, m7, [Q1], m15, m12, m13,  m9, 3, m3         ; [q1] -p2 -q0 +q1 +q3
    FILTER_UPDATE       m6, m7, m4, m5, [Q2], m10, m13,  m8,  m9, 3, m3,  m8    ; [q2] -p1 -q1 +q2 +q3
%endif

    ; (m0: 0, [m1: flat8out], m2: fm & flat8in, m8..15: q2 q3 p1 p0 q0 q1 p3 p2)
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

%if %2 == 16
    pand            m1, m2                                                              ; mask(out) & (mask(fm) & mask(in))
    mova            m2, [P7]
    mova            m3, [P6]
    mova            m8, [P5]
    mova            m9, [P4]
    FILTER_INIT     m4, m5, m6, m7, [P6],  14,                   m1,  m3
    FILTER_UPDATE   m6, m7, m4, m5, [P5],  m2,  m3,  m8, m13, 4, m1,  m8                ; [p5] -p7 -p6 +p5 +q1
    FILTER_UPDATE   m4, m5, m6, m7, [P4],  m2,  m8,  m9, m13, 4, m1,  m9, m13, [Q2]     ; [p4] -p7 -p5 +p4 +q2
    FILTER_UPDATE   m6, m7, m4, m5, [P3],  m2,  m9, m14, m13, 4, m1, m14, m13, [Q3]     ; [p3] -p7 -p4 +p3 +q3
    FILTER_UPDATE   m4, m5, m6, m7, [P2],  m2, m14, m15, m13, 4, m1,      m13, [Q4]     ; [p2] -p7 -p3 +p2 +q4
    FILTER_UPDATE   m6, m7, m4, m5, [P1],  m2, m15, m10, m13, 4, m1,      m13, [Q5]     ; [p1] -p7 -p2 +p1 +q5
    FILTER_UPDATE   m4, m5, m6, m7, [P0],  m2, m10, m11, m13, 4, m1,      m13, [Q6]     ; [p0] -p7 -p1 +p0 +q6
    FILTER_UPDATE   m6, m7, m4, m5, [Q0],  m2, m11, m12, m13, 4, m1,      m13, [Q7]     ; [q0] -p7 -p0 +q0 +q7
    FILTER_UPDATE   m4, m5, m6, m7, [Q1],  m3, m12,  m2, m13, 4, m1,       m2, [Q1]     ; [q1] -p6 -q0 +q1 +q7
    FILTER_UPDATE   m6, m7, m4, m5, [Q2],  m8,  m2,  m3, m13, 4, m1,       m3, [Q2]     ; [q2] -p5 -q1 +q2 +q7
    FILTER_UPDATE   m4, m5, m6, m7, [Q3],  m9,  m3,  m8, m13, 4, m1,  m8,  m8, [Q3]     ; [q3] -p4 -q2 +q3 +q7
    FILTER_UPDATE   m6, m7, m4, m5, [Q4], m14,  m8,  m9, m13, 4, m1,  m9,  m9, [Q4]     ; [q4] -p3 -q3 +q4 +q7
    FILTER_UPDATE   m4, m5, m6, m7, [Q5], m15,  m9, m14, m13, 4, m1, m14, m14, [Q5]     ; [q5] -p2 -q4 +q5 +q7
    FILTER_UPDATE   m6, m7, m4, m5, [Q6], m10, m14, m15, m13, 4, m1, m15, m15, [Q6]     ; [q6] -p1 -q5 +q6 +q7
%endif

%ifidn %1, h
%if %2 == 16
    mova                    m0, [P7]
    mova                    m1, [P6]
    mova                    m2, [P5]
    mova                    m3, [P4]
    mova                    m4, [P3]
    mova                    m5, [P2]
    mova                    m6, [P1]
    mova                    m7, [P0]
    mova                    m8, [Q0]
    mova                    m9, [Q1]
    mova                   m10, [Q2]
    mova                   m11, [Q3]
    mova                   m12, [Q4]
    mova                   m13, [Q5]
    mova                   m14, [Q6]
    mova                   m15, [Q7]
    TRANSPOSE16x16B 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, [rsp]
    DEFINE_REAL_P7_TO_Q7
    movu  [P7],  m0
    movu  [P6],  m1
    movu  [P5],  m2
    movu  [P4],  m3
    movu  [P3],  m4
    movu  [P2],  m5
    movu  [P1],  m6
    movu  [P0],  m7
    movu  [Q0],  m8
    movu  [Q1],  m9
    movu  [Q2], m10
    movu  [Q3], m11
    movu  [Q4], m12
    movu  [Q5], m13
    movu  [Q6], m14
    movu  [Q7], m15
%elif %2 == 44
    SWAP 0, 7   ; m0 = p1
    SWAP 3, 4   ; m3 = q1
    DEFINE_REAL_P7_TO_Q7 2
    SBUTTERFLY  bw, 0, 1, 8
    SBUTTERFLY  bw, 2, 3, 8
    SBUTTERFLY  wd, 0, 2, 8
    SBUTTERFLY  wd, 1, 3, 8
    SBUTTERFLY  dq, 0, 4, 8
    SBUTTERFLY  dq, 1, 5, 8
    SBUTTERFLY  dq, 2, 6, 8
    SBUTTERFLY  dq, 3, 7, 8
    movd  [P7], m0
    punpckhqdq m0, m8
    movd  [P6], m0
    movd  [Q0], m1
    punpckhqdq  m1, m9
    movd  [Q1], m1
    movd  [P3], m2
    punpckhqdq  m2, m10
    movd  [P2], m2
    movd  [Q4], m3
    punpckhqdq m3, m11
    movd  [Q5], m3
    movd  [P5], m4
    punpckhqdq m4, m12
    movd  [P4], m4
    movd  [Q2], m5
    punpckhqdq m5, m13
    movd  [Q3], m5
    movd  [P1], m6
    punpckhqdq m6, m14
    movd  [P0], m6
    movd  [Q6], m7
    punpckhqdq m7, m8
    movd  [Q7], m7
%else
    ; the following code do a transpose of 8 full lines to 16 half
    ; lines (high part). It is inlined to avoid the need of a staging area
    mova                    m0, [P3]
    mova                    m1, [P2]
    mova                    m2, [P1]
    mova                    m3, [P0]
    mova                    m4, [Q0]
    mova                    m5, [Q1]
    mova                    m6, [Q2]
    mova                    m7, [Q3]
    DEFINE_REAL_P7_TO_Q7
    SBUTTERFLY  bw,  0,  1, 8
    SBUTTERFLY  bw,  2,  3, 8
    SBUTTERFLY  bw,  4,  5, 8
    SBUTTERFLY  bw,  6,  7, 8
    SBUTTERFLY  wd,  0,  2, 8
    SBUTTERFLY  wd,  1,  3, 8
    SBUTTERFLY  wd,  4,  6, 8
    SBUTTERFLY  wd,  5,  7, 8
    SBUTTERFLY  dq,  0,  4, 8
    SBUTTERFLY  dq,  1,  5, 8
    SBUTTERFLY  dq,  2,  6, 8
    SBUTTERFLY  dq,  3,  7, 8
    movh  [P7], m0
    punpckhqdq m0, m8
    movh  [P6], m0
    movh  [Q0], m1
    punpckhqdq  m1, m9
    movh  [Q1], m1
    movh  [P3], m2
    punpckhqdq  m2, m10
    movh  [P2], m2
    movh  [Q4], m3
    punpckhqdq m3, m11
    movh  [Q5], m3
    movh  [P5], m4
    punpckhqdq m4, m12
    movh  [P4], m4
    movh  [Q2], m5
    punpckhqdq m5, m13
    movh  [Q3], m5
    movh  [P1], m6
    punpckhqdq m6, m14
    movh  [P0], m6
    movh  [Q6], m7
    punpckhqdq m7, m8
    movh  [Q7], m7
%endif
%endif

    RET
%endmacro

%macro LPF_16_VH 2
INIT_XMM %2
cglobal vp9_loop_filter_v_%1_16, 5,10,16,      dst, stride, E, I, H, mstride, dst1, dst2, stride3, mstride3
    LOOPFILTER v, %1
cglobal vp9_loop_filter_h_%1_16, 5,10,16, 256, dst, stride, E, I, H, mstride, dst1, dst2, stride3, mstride3
    LOOPFILTER h, %1
%endmacro

%macro LPF_16_VH_ALL_OPTS 1
LPF_16_VH %1, sse2
LPF_16_VH %1, ssse3
LPF_16_VH %1, avx
%endmacro

LPF_16_VH_ALL_OPTS 16
LPF_16_VH_ALL_OPTS 44
LPF_16_VH_ALL_OPTS 48
LPF_16_VH_ALL_OPTS 84
LPF_16_VH_ALL_OPTS 88

%endif ; x86-64
