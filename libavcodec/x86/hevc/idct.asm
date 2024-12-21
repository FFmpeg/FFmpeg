;*******************************************************************************
;* SIMD-optimized IDCT functions for HEVC decoding
;* Copyright (c) 2014 Pierre-Edouard LEPERE
;* Copyright (c) 2014 James Almer
;* Copyright (c) 2016 Alexandra Hájková
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

pd_64: times 4 dd 64
pd_2048: times 4 dd 2048
pd_512: times 4 dd 512

; 4x4 transform coeffs
cextern pw_64
pw_64_m64: times 4 dw 64, -64
pw_83_36: times 4 dw 83, 36
pw_36_m83: times 4 dw 36, -83

; 8x8 transform coeffs
pw_89_75: times 4 dw 89, 75
pw_50_18: times 4 dw 50, 18

pw_75_m18: times 4 dw 75, -18
pw_m89_m50: times 4 dw -89, -50

pw_50_m89: times 4 dw 50, -89
pw_18_75: times 4 dw 18, 75

pw_18_m50: times 4 dw 18, -50
pw_75_m89: times 4 dw 75, -89

; 16x16 transformation coeffs
trans_coeffs16: times 4 dw 90, 87
times 4 dw 80, 70
times 4 dw 57, 43
times 4 dw 25, 9

times 4 dw 87, 57
times 4 dw 9, -43
times 4 dw -80, -90
times 4 dw -70, -25

times 4 dw 80, 9
times 4 dw -70, -87
times 4 dw -25, 57
times 4 dw 90, 43

times 4 dw 70, -43
times 4 dw -87, 9
times 4 dw 90, 25
times 4 dw -80, -57

times 4 dw 57, -80
times 4 dw -25, 90
times 4 dw -9, -87
times 4 dw 43, 70

times 4 dw 43, -90
times 4 dw 57, 25
times 4 dw -87, 70
times 4 dw 9, -80

times 4 dw 25, -70
times 4 dw 90, -80
times 4 dw 43, 9
times 4 dw -57, 87

times 4 dw 9, -25
times 4 dw 43, -57
times 4 dw 70, -80
times 4 dw 87, -90

; 32x32 transform coeffs
trans_coeff32: times 8 dw 90
times 4 dw 88, 85
times 4 dw 82, 78
times 4 dw 73, 67
times 4 dw 61, 54
times 4 dw 46, 38
times 4 dw 31, 22
times 4 dw 13, 4

times 4 dw 90, 82
times 4 dw 67, 46
times 4 dw 22, -4
times 4 dw -31, -54
times 4 dw -73, -85
times 4 dw -90, -88
times 4 dw -78, -61
times 4 dw -38, -13

times 4 dw 88, 67
times 4 dw 31, -13
times 4 dw -54, -82
times 4 dw -90, -78
times 4 dw -46, -4
times 4 dw 38, 73
times 4 dw 90, 85
times 4 dw 61, 22

times 4 dw 85, 46
times 4 dw -13, -67
times 4 dw -90, -73
times 4 dw -22, 38
times 4 dw 82, 88
times 4 dw 54, -4
times 4 dw -61, -90
times 4 dw -78, -31

times 4 dw 82, 22
times 4 dw -54, -90
times 4 dw -61, 13
times 4 dw 78, 85
times 4 dw 31, -46
times 4 dw -90, -67
times 4 dw 4, 73
times 4 dw 88, 38

times 4 dw 78, -4
times 4 dw -82, -73
times 4 dw 13, 85
times 4 dw 67, -22
times 4 dw -88, -61
times 4 dw 31, 90
times 4 dw 54, -38
times 4 dw -90, -46

times 4 dw 73, -31
times 4 dw -90, -22
times 4 dw 78, 67
times 4 dw -38, -90
times 4 dw -13, 82
times 4 dw 61, -46
times 4 dw -88, -4
times 4 dw 85, 54

times 4 dw 67, -54
times 4 dw -78, 38
times 4 dw 85, -22
times 4 dw -90, 4
times 4 dw 90, 13
times 4 dw -88, -31
times 4 dw 82, 46
times 4 dw -73, -61

times 4 dw 61, -73
times 4 dw -46, 82
times 4 dw 31, -88
times 4 dw -13, 90
times 4 dw -4, -90
times 4 dw 22, 85
times 4 dw -38, -78
times 4 dw 54, 67

times 4 dw 54, -85
times 4 dw -4, 88
times 4 dw -46, -61
times 4 dw 82, 13
times 4 dw -90, 38
times 4 dw 67, -78
times 4 dw -22, 90
times 4 dw -31, -73

times 4 dw 46, -90
times 4 dw 38, 54
times 4 dw -90, 31
times 4 dw 61, -88
times 4 dw 22, 67
times 4 dw -85, 13
times 4 dw 73, -82
times 4 dw 4, 78

times 4 dw 38, -88
times 4 dw 73, -4
times 4 dw -67, 90
times 4 dw -46, -31
times 4 dw 85, -78
times 4 dw 13, 61
times 4 dw -90, 54
times 4 dw 22, -82

times 4 dw 31, -78
times 4 dw 90, -61
times 4 dw 4, 54
times 4 dw -88, 82
times 4 dw -38, -22
times 4 dw 73, -90
times 4 dw 67, -13
times 4 dw -46, 85

times 4 dw 22, -61
times 4 dw 85, -90
times 4 dw 73, -38
times 4 dw -4, 46
times 4 dw -78, 90
times 4 dw -82, 54
times 4 dw -13, -31
times 4 dw 67, -88

times 4 dw 13, -38
times 4 dw 61, -78
times 4 dw 88, -90
times 4 dw 85, -73
times 4 dw 54, -31
times 4 dw 4, 22
times 4 dw -46, 67
times 4 dw -82, 90

times 4 dw 4, -13
times 4 dw 22, -31
times 4 dw 38, -46
times 4 dw 54, -61
times 4 dw 67, -73
times 4 dw 78, -82
times 4 dw 85, -88
times 4 dw 90, -90

SECTION .text

; void ff_hevc_idct_HxW_dc_{8,10}_<opt>(int16_t *coeffs)
; %1 = HxW
; %2 = number of loops
; %3 = bitdepth
%macro IDCT_DC 3
cglobal hevc_idct_%1x%1_dc_%3, 1, 2, 1, coeff, tmp
    movsx             tmpd, word [coeffq]
    add               tmpd, (1 << (14 - %3)) + 1
    sar               tmpd, (15 - %3)
    movd               xm0, tmpd
    SPLATW              m0, xm0
    DEFINE_ARGS coeff, cnt
    mov               cntd, %2
.loop:
    mova [coeffq+mmsize*0], m0
    mova [coeffq+mmsize*1], m0
    mova [coeffq+mmsize*2], m0
    mova [coeffq+mmsize*3], m0
    add  coeffq, mmsize*8
    mova [coeffq+mmsize*-4], m0
    mova [coeffq+mmsize*-3], m0
    mova [coeffq+mmsize*-2], m0
    mova [coeffq+mmsize*-1], m0
    dec  cntd
    jg  .loop
    RET
%endmacro

; %1 = HxW
; %2 = bitdepth
%macro IDCT_DC_NL 2 ; No loop
cglobal hevc_idct_%1x%1_dc_%2, 1, 2, 1, coeff, tmp
    movsx             tmpd, word [coeffq]
    add               tmpd, (1 << (14 - %2)) + 1
    sar               tmpd, (15 - %2)
    movd                m0, tmpd
    SPLATW              m0, xm0
    mova [coeffq+mmsize*0], m0
    mova [coeffq+mmsize*1], m0
    mova [coeffq+mmsize*2], m0
    mova [coeffq+mmsize*3], m0
%if mmsize == 16
    mova [coeffq+mmsize*4], m0
    mova [coeffq+mmsize*5], m0
    mova [coeffq+mmsize*6], m0
    mova [coeffq+mmsize*7], m0
%endif
    RET
%endmacro

; IDCT 4x4, expects input in m0, m1
; %1 - shift
; %2 - 1/0 - SCALE and Transpose or not
; %3 - 1/0 add constant or not
%macro TR_4x4 3
    ; interleaves src0 with src2 to m0
    ;         and src1 with scr3 to m2
    ; src0: 00 01 02 03     m0: 00 20 01 21 02 22 03 23
    ; src1: 10 11 12 13 -->
    ; src2: 20 21 22 23     m1: 10 30 11 31 12 32 13 33
    ; src3: 30 31 32 33

    SBUTTERFLY wd, 0, 1, 2

    pmaddwd m2, m0, [pw_64]    ; e0
    pmaddwd m3, m1, [pw_83_36] ; o0
    pmaddwd m0, [pw_64_m64]    ; e1
    pmaddwd m1, [pw_36_m83]    ; o1

%if %3 == 1
    %assign %%add 1 << (%1 - 1)
    mova  m4, [pd_ %+ %%add]
    paddd m2, m4
    paddd m0, m4
%endif

    SUMSUB_BADC d, 3, 2, 1, 0, 4

%if %2 == 1
    psrad m3, %1 ; e0 + o0
    psrad m1, %1 ; e1 + o1
    psrad m2, %1 ; e0 - o0
    psrad m0, %1 ; e1 - o1
    ;clip16
    packssdw m3, m1
    packssdw m0, m2
    ; Transpose
    SBUTTERFLY wd, 3, 0, 1
    SBUTTERFLY wd, 3, 0, 1
    SWAP 3, 1, 0
%else
    SWAP 3, 2, 0
%endif
%endmacro

%macro DEFINE_BIAS 1
    %assign shift (20 - %1)
    %assign c_add (1 << (shift - 1))
    %define arr_add pd_ %+ c_add
%endmacro

; %1 - bit_depth
; %2 - register add constant
; is loaded to
; shift = 20 - bit_depth
%macro LOAD_BIAS 2
    DEFINE_BIAS %1
    mova %2, [arr_add]
%endmacro

; %1, %2 - registers to load packed 16 bit values to
; %3, %4, %5, %6 - vertical offsets
; %7 - horizontal offset
%macro LOAD_BLOCK 7
    movq   %1, [r0 + %3 + %7]
    movhps %1, [r0 + %5 + %7]
    movq   %2, [r0 + %4 + %7]
    movhps %2, [r0 + %6 + %7]
%endmacro

; void ff_hevc_idct_4x4__{8,10}_<opt>(int16_t *coeffs, int col_limit)
; %1 = bitdepth
%macro IDCT_4x4 1
cglobal hevc_idct_4x4_%1, 1, 1, 5, coeffs
    mova m0, [coeffsq]
    mova m1, [coeffsq + 16]

    TR_4x4 7, 1, 1
    TR_4x4 20 - %1, 1, 1

    mova [coeffsq],      m0
    mova [coeffsq + 16], m1
    RET
%endmacro

; scale, pack (clip16) and store the residuals     0 e8[0] + o8[0] --> + %1
; 4 at one time (4 columns)                        1 e8[1] + o8[1]
; from %5: e8/16 + o8/16, with %1 offset                  ...
; and  %3: e8/16 - o8/16, with %2 offset           6 e8[1] - o8[1]
; %4 - shift                                       7 e8[0] - o8[0] --> + %2
%macro STORE_8 7
    psrad    %5, %4
    psrad    %3, %4
    packssdw %5, %3
    movq     [coeffsq + %1], %5
    movhps   [coeffsq + %2], %5
%endmacro

; %1 - horizontal offset
; %2 - shift
; %3, %4 - transform coeffs
; %5 - vertical offset for e8 + o8
; %6 - vertical offset for e8 - o8
; %7 - register with e8 inside
; %8 - block_size
; %9 - register to store e8 +o8
; %10 - register to store e8 - o8
%macro E8_O8 10
    pmaddwd m6, m4, %3
    pmaddwd m7, m5, %4

    paddd m6, m7
    paddd m7, m6, %7 ; o8 + e8
    psubd %7, m6     ; e8 - o8
%if %8 == 8
    STORE_8 %5 + %1, %6 + %1, %7, %2, m7, 0, 0
%else
    SWAP m7, %9
    SWAP %7, %10
%endif
%endmacro

; 8x4 residuals are processed and stored
; %1 - horizontal offset
; %2 - shift
; %3 - offset of the even row
; %4 - step: 1 for 8x8, 2 for 16x16, 4 for 32x32
; %5 - offset of the odd row
; %6 - block size
; %7 - 1/0 add a constant in TR_4x4 or not
; I want to add a constant for 8x8 transform but not for 16x16 and 32x32
%macro TR_8x4 7
    ; load 4 columns of even rows
    LOAD_BLOCK  m0, m1, 0, 2 * %4 * %3, %4 * %3, 3 * %4 * %3, %1

    TR_4x4 %2, 0, %7 ; e8: m0, m1, m2, m3, for 4 columns only

    ; load 4 columns of odd rows
    LOAD_BLOCK m4, m5, %4 * %5, 3 * %4 * %5, 5 * %4 * %5, 7 * %4 * %5, %1

    ; 00 01 02 03
    ; 10 11 12 13      m4: 10 30 11 31 12 32 13 33

    ; ...        -- >
    ;                  m5: 50 70 51 71 52 72 53 73
    ; 70 71 72 73
    SBUTTERFLY wd, 4, 5, 6

    E8_O8 %1, %2, [pw_89_75],  [pw_50_18],   0,      %5 * 7, m0, %6, m8, m15
    E8_O8 %1, %2, [pw_75_m18], [pw_m89_m50], %5,     %5 * 6, m1, %6, m9, m14
    E8_O8 %1, %2, [pw_50_m89], [pw_18_75],   %5 * 2, %5 * 5, m2, %6, m10, m13
    E8_O8 %1, %2, [pw_18_m50], [pw_75_m89],  %5 * 3, %5 * 4, m3, %6, m11, m12
%endmacro

%macro STORE_PACKED 7
    movq   [r0 + %3 + %7], %1
    movhps [r0 + %4 + %7], %1
    movq   [r0 + %5 + %7], %2
    movhps [r0 + %6 + %7], %2
%endmacro

; transpose 4x4 block packed
; in %1 and %2 registers
; %3 - temporary register
%macro TRANSPOSE_4x4 3
    SBUTTERFLY wd, %1, %2, %3
    SBUTTERFLY dq, %1, %2, %3
%endmacro

; %1 - horizontal offset of the block i
; %2 - vertical offset of the block i
; %3 - width in bytes
; %4 - vertical offset for the block j
; %5 - horizontal offset for the block j
%macro SWAP_BLOCKS 5
    ; M_j
    LOAD_BLOCK m4, m5, %4, %4 + %3, %4 + 2 * %3, %4 + 3 * %3, %5
    TRANSPOSE_4x4 4, 5, 6

    ; M_i
    LOAD_BLOCK m6, m7, %2, %2 + %3, %2 + 2 * %3, %2 + 3 * %3, %1

    STORE_PACKED m4, m5, %2, %2 + %3, %2 + 2 * %3, %2 + 3 * %3, %1

    ; transpose and store M_i
    SWAP m6, m4
    SWAP m7, m5
    TRANSPOSE_4x4 4, 5, 6
    STORE_PACKED m4, m5, %4, %4 + %3, %4 + 2 * %3, %4 + 3 * %3, %5
%endmacro

; %1 - horizontal offset
; %2 - vertical offset of the block
; %3 - width in bytes
%macro TRANSPOSE_BLOCK 3
    LOAD_BLOCK m4, m5, %2, %2 + %3, %2 + 2 * %3, %2 + 3 * %3, %1
    TRANSPOSE_4x4 4, 5, 6
    STORE_PACKED m4, m5, %2, %2 + %3, %2 + 2 * %3, %2 + 3 * %3, %1
%endmacro

%macro TRANSPOSE_8x8 0
cglobal hevc_idct_transpose_8x8, 0, 0, 0
    ; M1 M2 ^T = M1^t M3^t
    ; M3 M4      M2^t M4^t

    ; M1 4x4 block
    TRANSPOSE_BLOCK 0, 0, 16

    ; M2 and M3
    SWAP_BLOCKS 0, 64, 16, 0, 8

    ; M4
    TRANSPOSE_BLOCK 8, 64, 16

    ret
%endmacro

; void ff_hevc_idct_8x8_{8,10}_<opt>(int16_t *coeffs, int col_limit)
; %1 = bitdepth
%macro IDCT_8x8 1
cglobal hevc_idct_8x8_%1, 1, 1, 8, coeffs
    TR_8x4 0, 7, 32, 1, 16, 8, 1
    TR_8x4 8, 7, 32, 1, 16, 8, 1

    call hevc_idct_transpose_8x8_ %+ cpuname

    DEFINE_BIAS %1
    TR_8x4 0, shift, 32, 1, 16, 8, 1
    TR_8x4 8, shift, 32, 1, 16, 8, 1

    TAIL_CALL hevc_idct_transpose_8x8_ %+ cpuname, 1
%endmacro

; store intermedite e32 coeffs on stack
; as 16x4 matrix
; from m10: e8 + o8, with %6 offset
; and  %3:  e8 - o8, with %7 offset
; %4 - shift, unused here
%macro STORE_16 7
    mova [rsp + %6], %5
    mova [rsp + %7], %3
%endmacro

; %1, %2 - transform constants
; %3, %4 - regs with interleaved coeffs
; %5 - 1/0 SWAP or add
; %6, %7 - registers for intermidiate sums
; %8 - accumulator register
%macro ADD_ROWS 8
    pmaddwd %6, %3, %1
    pmaddwd %7, %4, %2
    paddd   %6, %7
%if %5 == 1
    SWAP %6, %8
%else
    paddd %8, %6
%endif
%endmacro

; %1 - transform coeffs
; %2, %3 offsets for storing e+o/e-o back to coeffsq
; %4 - shift
; %5 - add
; %6 - block_size
; %7 - register with e16
; %8, %9 - stack offsets for storing e+o/e-o
%macro E16_O16 9
    ADD_ROWS [%1],          [%1 +     16], m0, m1, 1, m5, m6, m7
    ADD_ROWS [%1 + 2 * 16], [%1 + 3 * 16], m2, m3, 0, m5, m6, m7

%if %6 == 8
    paddd %7, %5
%endif

    paddd m4, m7, %7 ; o16 + e16
    psubd %7, m7     ; e16 - o16
    STORE_%6 %2, %3, %7, %4, m4, %8, %9
%endmacro

%macro TR_16x4 10
    ; produce 8x4 matrix of e16 coeffs
    ; for 4 first rows and store it on stack (128 bytes)
    TR_8x4 %1, 7, %4, %5, %6, %8, 0

    ; load 8 even rows
    LOAD_BLOCK m0, m1, %9 * %6, %9 * 3 * %6, %9 * 5 * %6, %9 * 7 * %6, %1
    LOAD_BLOCK m2, m3, %9 * 9 * %6, %9 * 11 * %6, %9 * 13 * %6, %9 * 15 * %6, %1

    SBUTTERFLY wd, 0, 1, 4
    SBUTTERFLY wd, 2, 3, 4

    E16_O16 trans_coeffs16,               0 + %1, 15 * %6 + %1, %2, %3, %7, m8,       0, 15 * 16
    mova m8, %3
    E16_O16 trans_coeffs16 +     64,     %6 + %1, 14 * %6 + %1, %2, m8, %7, m9,      16, 14 * 16
    E16_O16 trans_coeffs16 + 2 * 64, 2 * %6 + %1, 13 * %6 + %1, %2, m8, %7, m10, 2 * 16, 13 * 16
    E16_O16 trans_coeffs16 + 3 * 64, 3 * %6 + %1, 12 * %6 + %1, %2, m8, %7, m11, 3 * 16, 12 * 16
    E16_O16 trans_coeffs16 + 4 * 64, 4 * %6 + %1, 11 * %6 + %1, %2, m8, %7, m12, 4 * 16, 11 * 16
    E16_O16 trans_coeffs16 + 5 * 64, 5 * %6 + %1, 10 * %6 + %1, %2, m8, %7, m13, 5 * 16, 10 * 16
    E16_O16 trans_coeffs16 + 6 * 64, 6 * %6 + %1,  9 * %6 + %1, %2, m8, %7, m14, 6 * 16,  9 * 16
    E16_O16 trans_coeffs16 + 7 * 64, 7 * %6 + %1,  8 * %6 + %1, %2, m8, %7, m15, 7 * 16,  8 * 16
%endmacro

%macro TRANSPOSE_16x16 0
cglobal hevc_idct_transpose_16x16, 0, 0, 0
; M1  M2  M3  M4 ^T      m1 m5 m9  m13   M_i^T = m_i
; M5  M6  M7  M8    -->  m2 m6 m10 m14
; M9  M10 M11 M12        m3 m7 m11 m15
; M13 M14 M15 M16        m4 m8 m12 m16

    ; M1 4x4 block
    TRANSPOSE_BLOCK 0, 0, 32

    ; M5, M2
    SWAP_BLOCKS 0, 128, 32, 0, 8
    ; M9, M3
    SWAP_BLOCKS 0, 256, 32, 0, 16
    ; M13, M4
    SWAP_BLOCKS 0, 384, 32, 0, 24

    ;M6
    TRANSPOSE_BLOCK 8, 128, 32

    ; M10, M7
    SWAP_BLOCKS 8, 256, 32, 128, 16
    ; M14, M8
    SWAP_BLOCKS 8, 384, 32, 128, 24

    ;M11
    TRANSPOSE_BLOCK 16, 256, 32

    ; M15, M12
    SWAP_BLOCKS 16, 384, 32, 256, 24

    ;M16
    TRANSPOSE_BLOCK 24, 384, 32

    ret
%endmacro

; void ff_hevc_idct_16x16_{8,10}_<opt>(int16_t *coeffs, int col_limit)
; %1 = bitdepth
%macro IDCT_16x16 1
cglobal hevc_idct_16x16_%1, 1, 2, 16, coeffs
    mov r1d, 3
.loop16:
    TR_16x4 8 * r1, 7, [pd_64], 64, 2, 32, 8, 16, 1, 0
    dec r1d
    jge .loop16

    call hevc_idct_transpose_16x16_ %+ cpuname

    DEFINE_BIAS %1
    mov r1d, 3
.loop16_2:
    TR_16x4 8 * r1, shift, [arr_add], 64, 2, 32, 8, 16, 1, 1
    dec r1d
    jge .loop16_2

    TAIL_CALL hevc_idct_transpose_16x16_ %+ cpuname, 1
%endmacro

; scale, pack (clip16) and store the residuals     0 e32[0] + o32[0] --> %1
; 4 at one time (4 columns)                        1 e32[1] + o32[1]
; %1 - address to store e32 + o32
; %2 - address to store e32 - e32
; %5 - reg with e32 + o32                                  ...
; %3 - reg with e32 - o32                          30 e32[1] - o32[1]
; %4 - shift                                       31 e32[0] - o32[0] --> %2
%macro STORE_32 5
    psrad    %5, %4
    psrad    %3, %4
    packssdw %5, %3
    movq     [%1], %5
    movhps   [%2], %5
%endmacro

; %1 - transform coeffs
; %2 - stack offset for e32
; %2, %3 offsets for storing e+o/e-o back to coeffsq
; %4 - shift
; %5 - stack offset of e32
%macro E32_O32 5
    ADD_ROWS [%1],          [%1 +     16], m0, m1, 1, m8, m9, m10
    ADD_ROWS [%1 + 2 * 16], [%1 + 3 * 16], m2, m3, 0, m8, m9, m10
    ADD_ROWS [%1 + 4 * 16], [%1 + 5 * 16], m4, m5, 0, m8, m9, m10
    ADD_ROWS [%1 + 6 * 16], [%1 + 7 * 16], m6, m7, 0, m8, m9, m10

    paddd m11, m14, [rsp + %5]
    paddd m12, m10, m11 ; o32 + e32
    psubd m11, m10      ; e32 - o32
    STORE_32 %2, %3, m11, %4, m12
%endmacro

; %1 - horizontal offset
; %2 - bitdepth
%macro TR_32x4 3
    TR_16x4 %1, 7, [pd_64], 128, 4, 64, 16, 16, 2, 0

    LOAD_BLOCK m0, m1,      64,  3 * 64,  5 * 64,  7 * 64, %1
    LOAD_BLOCK m2, m3,  9 * 64, 11 * 64, 13 * 64, 15 * 64, %1
    LOAD_BLOCK m4, m5, 17 * 64, 19 * 64, 21 * 64, 23 * 64, %1
    LOAD_BLOCK m6, m7, 25 * 64, 27 * 64, 29 * 64, 31 * 64, %1

    SBUTTERFLY wd, 0, 1, 8
    SBUTTERFLY wd, 2, 3, 8
    SBUTTERFLY wd, 4, 5, 8
    SBUTTERFLY wd, 6, 7, 8

%if %3 == 1
    %assign shift 7
    mova m14, [pd_64]
%else
    LOAD_BIAS %2, m14
%endif

    lea r2, [trans_coeff32 + 15 * 128]
    lea r3, [coeffsq + %1]
    lea r4, [r3 + 16 * 64]
    mov r5d, 15 * 16
%%loop:
    E32_O32 r2, r3 + r5 * 4, r4, shift, r5
    sub r2, 128
    add r4, 64
    sub r5d, 16
    jge %%loop
%endmacro

%macro TRANSPOSE_32x32 0
cglobal hevc_idct_transpose_32x32, 0, 0, 0
    ; M0  M1 ... M7
    ; M8         M15
    ;
    ; ...
    ;
    ; M56        M63

    TRANSPOSE_BLOCK 0, 0, 64 ; M1
    mov r1d, 7
    mov r2d, 7 * 256
.loop_transpose:
    SWAP_BLOCKS 0, r2, 64, 0, r1 * 8
    sub r2d, 256
    dec r1d
    jg .loop_transpose

    TRANSPOSE_BLOCK 8, 256, 64 ; M9
    mov r1d, 6
    mov r2d, 512
    mov r3d, 16
.loop_transpose2:
    SWAP_BLOCKS 8, r2, 64, 256, r3
    add r3d, 8
    add r2d, 256
    dec r1d
    jg .loop_transpose2

    TRANSPOSE_BLOCK 2 * 8, 2 * 256, 64 ; M9
    mov r1d, 5
    mov r2d, 768
    mov r3d, 24
.loop_transpose3:
    SWAP_BLOCKS 2 * 8, r2, 64, 2 * 256, r3
    add r3d, 8
    add r2d, 256
    dec r1d
    jg .loop_transpose3

    TRANSPOSE_BLOCK 3 * 8, 3 * 256, 64 ; M27
    mov r1d, 4
    mov r2d, 1024
    mov r3d, 32
.loop_transpose4:
    SWAP_BLOCKS 3 * 8, r2, 64, 3 * 256, r3
    add r3d, 8
    add r2d, 256
    dec r1d
    jg .loop_transpose4

    TRANSPOSE_BLOCK 4 * 8, 4 * 256, 64 ; M36
    mov r1d, 3
    mov r2d, 1280
    mov r3d, 40
.loop_transpose5:
    SWAP_BLOCKS 4 * 8, r2, 64, 4 * 256, r3
    add r3d, 8
    add r2d, 256
    dec r1d
    jg .loop_transpose5

    TRANSPOSE_BLOCK 5 * 8, 5 * 256, 64 ; M45
    SWAP_BLOCKS 5 * 8, 6 * 256, 64, 5 * 256, 6 * 8
    SWAP_BLOCKS 5 * 8, 7 * 256, 64, 5 * 256, 7 * 8

    TRANSPOSE_BLOCK 6 * 8, 6 * 256, 64 ; M54
    SWAP_BLOCKS 6 * 8, 7 * 256, 64, 6 * 256, 7 * 8

    TRANSPOSE_BLOCK 7 * 8, 7 * 256, 64 ; M63

    ret
%endmacro

; void ff_hevc_idct_32x32_{8,10}_<opt>(int16_t *coeffs, int col_limit)
; %1 = bitdepth
%macro IDCT_32x32 1
cglobal hevc_idct_32x32_%1, 1, 6, 16, 256, coeffs
    mov r1d, 7
.loop32:
    TR_32x4 8 * r1, %1, 1
    dec r1d
    jge .loop32

    call hevc_idct_transpose_32x32_ %+ cpuname

    mov r1d, 7
.loop32_2:
    TR_32x4 8 * r1, %1, 0
    dec r1d
    jge .loop32_2

    TAIL_CALL hevc_idct_transpose_32x32_ %+ cpuname, 1
%endmacro

%macro INIT_IDCT_DC 1
INIT_MMX mmxext
IDCT_DC_NL  4,      %1

INIT_XMM sse2
IDCT_DC_NL  8,      %1
IDCT_DC    16,  4,  %1
IDCT_DC    32, 16,  %1

%if HAVE_AVX2_EXTERNAL
    INIT_YMM avx2
    IDCT_DC    16,  2,  %1
    IDCT_DC    32,  8,  %1
%endif ;HAVE_AVX2_EXTERNAL
%endmacro

%macro INIT_IDCT 2
INIT_XMM %2
%if %1 == 8
    TRANSPOSE_8x8
    %if ARCH_X86_64
        TRANSPOSE_16x16
        TRANSPOSE_32x32
    %endif
%endif
%if ARCH_X86_64
    IDCT_32x32 %1
    IDCT_16x16 %1
%endif
IDCT_8x8 %1
IDCT_4x4 %1
%endmacro

INIT_IDCT_DC 8
INIT_IDCT_DC 10
INIT_IDCT_DC 12
INIT_IDCT 8, sse2
INIT_IDCT 8, avx
INIT_IDCT 10, sse2
INIT_IDCT 10, avx
;INIT_IDCT 12, sse2
;INIT_IDCT 12, avx
