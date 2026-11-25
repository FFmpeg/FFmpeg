;******************************************************************************
;* VC1 motion compensation optimizations
;* Copyright (c) 2007 Christophe GISQUET <christophe.gisquet@free.fr>
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

pb_m4_18: times 8 db -4, 18
pb_53_m3: times 8 db 53, -3
pb_m3_53: times 8 db -3, 53
pb_18_m4: times 8 db 18, -4
pb_m4_36: times 8 db -4, 36
pb_36_m4: times 8 db 36, -4
pb_m4_53: times 8 db -4, 53
pb_m3_18: times 8 db -3, 18

pw_53: times 8 dw 53

cextern pb_80
cextern pw_9
cextern pw_18

SECTION .text

%define MOV8  movq
%define MOV16 movu

INIT_XMM ssse3
%macro HOR_8B 2

cglobal vc1_%1_mspel_mc10_%2, 4, 4, 6, dst, src, stride, rnd
    mova              m1, [pb_m4_53]
    mova              m2, [pb_m3_18]
    sub             rndd, 32
    jmp               vc1_%1_mspel_mc30_%2_after_prologue

cglobal vc1_%1_mspel_mc20_%2, 4, 4, 6, dst, src, stride, rnd
    mova              m1, [pb_m4_36]
    lea             rndd, [4*rndd-32]
    mova              m2, m1
    jmp               vc1_%1_mspel_mc30_%2_after_prologue

cglobal vc1_%1_mspel_mc30_%2, 4, 4, 6, dst, src, stride, rnd
    mova              m2, [pb_m4_53]
    mova              m1, [pb_m3_18]
    sub             rndd, 32

vc1_%1_mspel_mc30_%2_after_prologue:
    movd              m0, rndd
    WIN64_SPILL_XMM    7+(%2>>4)
%define hd  rndd
    mov               hd, %2
    SPLATW            m0, m0
.loop:
    MOV%2             m3, [srcq-1]
    MOV%2             m4, [srcq]
    MOV%2             m5, [srcq+1]
    MOV%2             m6, [srcq+2]

%if %2 == 8
    punpcklbw         m3, m4
    pmaddubsw         m3, m1
%ifidn %1,avg
    movq              m4, [dstq]
%endif
    punpcklbw         m6, m5
    pmaddubsw         m6, m2
    add             srcq, strideq
    psubw             m3, m0
    paddw             m3, m6
    psraw             m3, 6
    packuswb          m3, m3
%ifidn %1,avg
    pavgb             m3, m4
%endif
    movq          [dstq], m3
%else
    SBUTTERFLY        bw, 3, 4, 7
    pmaddubsw         m3, m1
    pmaddubsw         m4, m1
    SBUTTERFLY        bw, 6, 5, 7
    pmaddubsw         m6, m2
    pmaddubsw         m5, m2
    add             srcq, strideq
    psubw             m3, m0
    psubw             m4, m0
    paddw             m3, m6
    paddw             m4, m5
    psraw             m3, 6
    psraw             m4, 6
    packuswb          m3, m4
%ifidn %1, avg
    pavgb             m3, [dstq]
%endif
    mova          [dstq], m3
%endif
    add             dstq, strideq
    dec               hd
    jnz            .loop
    RET
%endmacro

HOR_8B put, 8
HOR_8B avg, 8

HOR_8B put, 16
HOR_8B avg, 16

%macro SETUP_COEFFS 3 ; width, coeff1, coeff2
    ASSERT (%3-%2 == 16)
%if ARCH_X86_64 || (%1 == 8)
    mova          m1, [%2]
    mova          m2, [%3]
%define COEFF0 m1
%define COEFF1 m2
%define M8 m8
%define M9 m9
%else
    lea           r4, [%2]
%define COEFF0 [r4]
%define COEFF1 [r4+(%3-%2)]
%define M8 m1
%define M9 m2
%endif
%endmacro

%macro VER_8B 2
cglobal vc1_%1_mspel_mc01_%2, 4, 4+ARCH_X86_32*(%2>>4), 6, dst, src, stride, rnd
    SETUP_COEFFS %2, pb_m4_18, pb_53_m3
    add             rndd, 31
    jmp               vc1_%1_mspel_mc03_%2_after_prologue

cglobal vc1_%1_mspel_mc02_%2, 4, 4+ARCH_X86_32*(%2>>4), 6, dst, src, stride, rnd
    SETUP_COEFFS %2, pb_m4_36, pb_36_m4
    lea             rndd, [4*rndd+28]
    jmp               vc1_%1_mspel_mc03_%2_after_prologue

cglobal vc1_%1_mspel_mc03_%2, 4, 4+ARCH_X86_32*(%2>>4), 6, dst, src, stride, rnd
    SETUP_COEFFS %2, pb_m3_53, pb_18_m4
    add             rndd, 31

vc1_%1_mspel_mc03_%2_after_prologue:
    neg          strideq
    movd              m0, rndd
    WIN64_SPILL_XMM    8, 8+3*(%2>>4)
    MOV%2             m3, [srcq+strideq]
    neg          strideq
    MOV%2             m4, [srcq]
    MOV%2             m5, [srcq+strideq]
    SPLATW            m0, m0
%if %2 == 16
    WIN64_PUSH_XMM    11, 8
%endif
    lea             srcq, [srcq+2*strideq]
%define hd  rndd
%if %2 == 8
    punpcklbw         m3, m5
%else
    punpcklbw         m7, m3, m5
    punpckhbw         m3, m5
%endif
    mov               hd, %2

.loop:
    MOV%2             m6, [srcq]
%if %2 == 8
    pmaddubsw         m3, m1
    punpcklbw         m4, m6
    pmaddubsw         m7, m4, m2
    paddw             m3, m0
    add             srcq, strideq
    paddw             m7, m3
    mova              m3, m4
%ifidn %1, avg
    movq              m4, [dstq]
%endif
    psraw             m7, 6
%ifnidn %1, avg
    mova              m4, m5
%endif
    packuswb          m7, m7
%ifidn %1, avg
    pavgb             m7, m4
    mova              m4, m5
%endif
    movq          [dstq], m7
%else
    pmaddubsw         m7, COEFF0
    pmaddubsw         m3, COEFF0
    punpcklbw         M8, m4, m6
    punpckhbw         m4, m6
    pmaddubsw         M9, M8, COEFF1
    paddw             m7, m0
%if ARCH_X86_64
    pmaddubsw        m10, m4, m2
    paddw             m3, m0
    paddw             m9, m7
    mova              m7, m8
    psraw             m9, 6
    paddw            m10, m3
%else
    paddw             m3, m0
    paddw             M9, m7
    mova              m7, M8
    pmaddubsw         M8, m4, COEFF1
    psraw             M9, 6
    paddw             M8, m3
%endif
    add             srcq, strideq
    mova              m3, m4
%if ARCH_X86_64
    psraw            m10, 6
    packuswb          m9, m10
%else
    psraw             M8, 6
    packuswb          M9, M8
%endif
%ifidn %1, avg
    pavgb             M9, [dstq]
%endif
    mova              m4, m5
    mova          [dstq], M9
%endif
    add             dstq, strideq
    mova              m5, m6
    dec               hd
    jnz            .loop
    RET
%endmacro

VER_8B put, 8
VER_8B avg, 8

VER_8B put, 16
VER_8B avg, 16

%if ARCH_X86_64
%define BIAS_REG m8
%define COEFF0   m9
%define COEFF1  m10
%else
%define BIAS_REG m7
%endif

%macro FUNC_HEADER 6; put vs avg, size, shift_ver, shift_hor, coeff1, coeff2
%assign stack_size (2*%2+16)*%2+mmsize*ARCH_X86_32*(%2>>4)
%if ARCH_X86_64
cglobal vc1_%1_mspel_mc%4%3_%2, 4, 7, 13+2*(%2 / 16), stack_size, dst, src, stride, rnd, rnd1, unused, tmp
%define COEFF0    m9
%define COEFF1   m10
    mova          COEFF0, [%5]
    mova          COEFF1, [%6]
%else
cglobal vc1_%1_mspel_mc%4%3_%2, 0, 5, 8, stack_size, coeffs, src, stride, rnd
    mov             srcd, r1m
    mov          strided, r2m
    mov             rndd, r3m
    %define        rnd1d rndd
    lea          coeffsq, [%5]
%define COEFF0 [coeffsd]
%define COEFF1 [coeffsd+(%6-%5)]
%endif
    ASSERT (%6-%5 == 16)
; The values from the vertical filter are scaled down (shifted) before
; the horizontal filter so that the calculations can be done in 16 bits.
; The coefficients of the 1/4 and 3/4 filter sum to 64, while the ones
; for the 1/2 filter sum to 16. Therefore two is added to the shift factor
; for each stage using 1/4 or 3/4 filter.
; Like in the purely horizontal and purely vertical filters, the 1/2 filter
; coefficients can of course be scaled by four if one also scales the rounding
; value and increases the downshift factor by two. We do this because then
; the intermediate shift only depends on the value of the horizontal shift.
;
; The coefficients used here contain negative values. The sum of the positive
; scaled 1/2 shift coefficients is 72 vs 71 for 1/4 and 3/4 shifts, so that
; the range of intermediate values from the first stage is most encompassing
; when the first stage uses a 1/2 shift. In this case the range is -8*255..72*255
; (apart from rounding).
; When the second stage uses 1/2 shifts, this range is shifted down by three
; and the range in the second stage does not fit into a signed 16bit value:
; The maximum is about 18*(72*255/8)-2*(-8*255/8)=41820. It fits into a signed
; word after adding a -128*128 bias. Using this bias implies that we can use
; packsswb and undo the bias on bytes.
; The same issue can happen when the second stage uses 1/4 or 3/4 shifts;
; but this can be addressed by combining the summands involving 53*coef and
; 18*coef with saturated addition.
;
; The bias is applied at the first stage by combining it with the first stage
; rnd value; we also apply the second-stage rnd-offset of 64 here
; (by adding 32).

%define IS_TWO(x) (1-((x-2)&1))       ; x == 2 for x in 1..3
%define SHIFT_FACTOR(x) 5-4*IS_TWO(x) ; x == 2 ? 1 : 5
%assign COMBINED_SHIFT (SHIFT_FACTOR(%3)+SHIFT_FACTOR(%4))/2
%assign ACTUAL_SHIFT (5+SHIFT_FACTOR(%4))/2
%assign BIAS -128*64*IS_TWO(%4)+32
%if %3 == 2
; The coefficients used for the 1/2 vertical shift are scaled by four
; (to reuse the purely vertical constants), so we need to scale rnd here.
    lea            rnd1d, [4*rndq+4*((1<<(COMBINED_SHIFT-1))-1)+BIAS]
%else
%if ARCH_X86_64
    lea            rnd1d, [rndq+(1<<(COMBINED_SHIFT-1))-1+BIAS]
%else
    add            rnd1d, (1<<(COMBINED_SHIFT-1))-1+BIAS
%endif
%endif
    call           put_mspel_ver_ %+ ACTUAL_SHIFT %+ _%2_ %+ cpuname
%endmacro

%macro FUNC_TRIPLE  3; put vs avg, size, shift2
    FUNC_HEADER %1, %2, 1, %3, pb_m4_18, pb_53_m3
    jmp %1_mspel_hor_16b_shift%3_%2_ %+ cpuname

    FUNC_HEADER %1, %2, 3, %3, pb_m3_53, pb_18_m4
    jmp %1_mspel_hor_16b_shift%3_%2_ %+ cpuname

    FUNC_HEADER %1, %2, 2, %3, pb_m4_36, pb_36_m4
%1_mspel_hor_16b_shift%3_%2_ %+ cpuname:
%define hd rnd1d
%if %3 == 1
    %define C1 PW_53
    %define C2 PW_18
%else
    %define C1 PW_18
    %define C2 PW_53
%endif
%if ARCH_X86_32
    DEFINE_ARGS dst, src, stride, rnd1
    mov             dstq, r0m
%endif
%if ARCH_X86_64 || %2 == 8
    mov             srcq, rsp
%else
    lea             srcq, [rsp+mmsize]
%endif

.loop:
    movu              m2, [srcq + 2 * 1]
    movu              m3, [srcq + 2 * 2]
%if %2 == 8
%if %3 == 2
    movu              m1, [srcq + 2 * 3]
    paddw             m2, m3
    paddw             m1, [srcq]
    pmullw            m2, m6
%ifidn %1,avg
    movq              m5, [dstq]
%endif
    paddw             m1, m0       ; 1,0,0,1+rnd
    add             srcq, 2*%2+16
    psubw             m2, m1       ; -1,9,9,-1-rnd
%else
    mova              m1, [srcq]
    movu              m4, [srcq + 2 * 3]
%if %3 == 3
    SWAP            1, 4
%endif
    pmullw            m2, C1       ; 0,53,0,0
    pmullw            m3, C2       ; 0,0,18,0
%ifidn %1,avg
    movq              m5, [dstq]
%endif
    paddw             m1, m4       ; 1,0,0,1
    psubw             m4, m0       ; 0,0,0,1-rnd
    psllw             m1, 2        ; 4,0,0,4
    add             srcq, 2*%2+16
    paddw             m2, m4       ; 0,53,0,1-rnd
    psubw             m3, m1       ; -4,0,18,-4
    paddsw            m2, m3       ; -4,53,18,-3-rnd
%endif
    psraw             m2, 7
%if %3 == 2
    packsswb          m2, m2
    pxor              m2, BIAS_REG ; undo -128 bias
%else
    packuswb          m2, m2
%endif
%ifidn %1,avg
    pavgb             m2, m5
%endif
    movq          [dstq], m2
%else ; size 16
%if %3 == 2
    movu              m1, [srcq + 2 * 3]
    paddw             m2, m3
    paddw             m1, [srcq]
    pmullw            m2, m6
    movu              m3, [srcq + 2 * 1 + mmsize]
    movu              m5, [srcq + 2 * 2 + mmsize]
    paddw             m1, m0       ; 1,0,0,1+rnd
    movu              m4, [srcq + 3 * 2 + mmsize]
    paddw             m3, m5
    psubw             m2, m1       ; -1,9,9,-1-rnd
    pmullw            m3, m6
    paddw             m4, [srcq + mmsize]
    psraw             m2, 7
    add             srcq, 2*%2+16
    paddw             m4, m0
    psubw             m3, m4
    psraw             m3, 7
    packsswb          m2, m3
    pxor              m2, BIAS_REG ; undo -128 bias
%else
    mova              m1, [srcq]
    movu              m4, [srcq + 2 * 3]
%if %3 == 3
    SWAP              1, 4
%endif
    pmullw            m2, C1       ; 0,53,0,0
    movu              m5, [srcq + 2 * 1 + mmsize]
    pmullw            m3, C2       ; 0,0,18,0
    paddw             m1, m4       ; 1,0,0,1
    movu              m6, [srcq + 2 * 2 + mmsize]
    pmullw            m5, C1       ; 0,53,0,0
    psubw             m4, m0       ; 0,0,0,1-rnd
    pmullw            m6, C2       ; 0,0,18,0
    mova              m7, [srcq + mmsize]
    psllw             m1, 2        ; 4,0,0,4

    paddw             m2, m4       ; 0,53,0,1-rnd
    movu              m4, [srcq + 3 * 2 + mmsize]
%if %3 == 3
    SWAP               4, 7
%endif
    psubw             m3, m1       ; -4,0,18,-4
    paddw             m7, m4       ; 1,0,0,1
    paddsw            m2, m3       ; -4,53,18,-3-rnd
    add             srcq, 2*%2+16
    psubw             m4, m0       ; 0,0,0,1-rnd
    psllw             m7, 2        ; 4,0,0,4
    paddw             m5, m4       ; 0,53,0,1-rnd
    psraw             m2, 7
    psubw             m6, m7       ; -4,0,18,-4
    paddsw            m5, m6       ; -4,53,18,-3-rnd
    psraw             m5, 7
    packuswb          m2, m5
%endif
%ifidn %1,avg
    pavgb             m2, [dstq]
%endif
    mova          [dstq], m2
%endif
    add             dstq, strideq
    dec               hd
    jnz            .loop
    RET
%endmacro

%macro VER_16B 2 ; size, shift
put_mspel_ver_%2_%1_ssse3:
%if ARCH_X86_32
    DEFINE_ARGS coeffs, src, stride, rnd1, tmp
%endif
%define hd     rnd1d
; Necessary to ensure that reading memory args works, after a call which
; pushed a return address on the stack. If the stack had to be realigned,
; memory args are accessed relative to the saved stack pointer (rstk)
; instead of rsp, which is unaffected by the call.
%ifidn rstk, rsp
%assign stack_offset stack_offset+gprsize
%endif
    neg          strideq
    dec             srcq
    movd              m0, rnd1d
    MOV%1             m2, [srcq+strideq]
    movd              m3, [srcq+strideq+%1]
    neg          strideq
%if %1 == 8 || ARCH_X86_64
%define RND BIAS_REG
    SPLATW           RND, m0
%else
%define RND [rsp+gprsize]
    SPLATW            m0, m0
    mova             RND, m0
%endif
%if ARCH_X86_64
    MOV%1             m4, [srcq]
    movd              m5, [srcq+%1]
%endif
    mov               hd, %1/4
    MOV%1             m6, [srcq+strideq]
%if ARCH_X86_64
    movd              m7, [srcq+strideq+%1]
    lea             srcq, [srcq+2*strideq]
%else
    movd              m4, [srcq+strideq+%1]
%endif
    lea             tmpq, [rsp+gprsize+mmsize*ARCH_X86_32*(%1>>4)]
%if %1 == 16
%if ARCH_X86_64
    punpcklbw        m13, m2, m6
%else
    punpcklbw         m7, m2, m6
%endif
    punpckhbw         m2, m6
%else ; size 8
    punpcklbw         m2, m6
%endif
%if ARCH_X86_64
    punpcklbw         m3, m7
%else
    punpcklbw         m3, m4
%endif
.loop:
%assign         %%offset 0
%rep 4
%if %1 == 16
%if ARCH_X86_64
    movu             m11, [srcq]
    movd             m12, [srcq+16]
    pmaddubsw        m13, COEFF0
    pmaddubsw         m2, COEFF0
    punpcklbw        m14, m4, m11
    pmaddubsw         m3, COEFF0
    punpckhbw         m4, m11
    punpcklbw         m5, m12
    SWAP               6, 14
    SWAP               7, 13
%else
    movu              m4, [srcq]
    movd              m5, [srcq+16]
    pmaddubsw         m7, COEFF0
    movu              m0, [srcq+2*strideq]
    pmaddubsw         m2, COEFF0
    movd              m1, [srcq+2*strideq+16]
    pmaddubsw         m3, COEFF0
    punpcklbw         m6, m4, m0
    punpckhbw         m4, m0
    punpcklbw         m5, m1
%endif
    pmaddubsw         m0, m6, COEFF1
    add             srcq, strideq
    paddw             m7, RND
    pmaddubsw         m1, m4, COEFF1
    paddw             m2, RND
    paddw             m0, m7
    pmaddubsw         m7, m5, COEFF1
    paddw             m3, RND
    psraw             m0, %2
    paddw             m1, m2
    mova  [tmpq+%%offset], m0
    psraw             m1, %2
    paddw             m3, m7
    mova [tmpq+%%offset+mmsize], m1
    psraw             m3, %2
    movq [tmpq+%%offset+2*mmsize], m3
%if ARCH_X86_64
    SWAP               6, 14
    SWAP               7, 13, 14
%else
    SWAP               6, 7
%endif
%else ; size 8
%if ARCH_X86_64
    MOV%1            m11, [srcq]
    movd             m12, [srcq+%1]
    pmaddubsw         m2, COEFF0
    pmaddubsw         m3, COEFF0
    punpcklbw         m4, m11
    punpcklbw         m5, m12
%else
    MOV%1             m4, [srcq]
    movd              m5, [srcq+%1]
    pmaddubsw         m2, COEFF0
    pmaddubsw         m3, COEFF0
    MOV%1             m0, [srcq+2*strideq]
    movd              m1, [srcq+2*strideq+%1]
    punpcklbw         m4, m0
    punpcklbw         m5, m1
%endif
    pmaddubsw         m0, m4, COEFF1
    paddw             m2, RND
    pmaddubsw         m1, m5, COEFF1
    paddw             m3, RND
    add             srcq, strideq
    paddw             m0, m2
    paddw             m1, m3
    psraw             m0, %2
    psraw             m1, %2
    mova  [tmpq+%%offset], m0
    movq [tmpq+%%offset+16], m1
%endif
    %assign     %%offset %%offset+2*%1+16
%if ARCH_X86_64
    SWAP 2, 4, 6, 11
    SWAP 3, 5, 7, 12
%else
    SWAP 2, 4
    SWAP 3, 5
%endif
%endrep
    add             tmpq, 4*(2*%1+16)
    dec               hd
    jnz            .loop

; common init code for the various horizontal filtering codepaths
%if ARCH_X86_64
    movd              m0, rndd
%else
    movd              m0, r3m
%endif
%if %2 == 3
%if %1 == 8 || ARCH_X86_64
    packsswb    BIAS_REG, BIAS_REG              ; -128 aka pb_80
%else
    RESET_MM_PERMUTATION
    mova        BIAS_REG, [pb_80]
%endif
    mova              m6, [pw_9]
%elif %1 == 8 || ARCH_X86_64
    mova           PW_53, [pw_53]
    mova           PW_18, [pw_18]
%endif
    mov               hd, %1
    SPLATW            m0, m0
%ifidn rstk, rsp
%assign stack_offset stack_offset-gprsize
%endif
    ret
%endmacro

%macro SHIFT13_FUNCS 1 ; size
    FUNC_TRIPLE put, %1, 1
    FUNC_TRIPLE put, %1, 3
    VER_16B %1, 3
    FUNC_TRIPLE avg, %1, 1
    FUNC_TRIPLE avg, %1, 3
%endmacro

%macro SHIFT2_FUNCS 1; size
    FUNC_TRIPLE put, %1, 2
    VER_16B %1, 5
    FUNC_TRIPLE avg, %1, 2
%endmacro

%define PW_53  m6
%define PW_18  m7
SHIFT13_FUNCS 8
SHIFT2_FUNCS  8

%if ARCH_X86_64
%define PW_53  m8
%define PW_18  m9
%else
%define PW_53  [pw_53]
%define PW_18  [pw_18]
%endif
SHIFT13_FUNCS 16
SHIFT2_FUNCS  16
