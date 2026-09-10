;******************************************************************************
;* VC1 inverse transform
;* Copyright (c) 2009 Fiona Glaser
;* Copyright (c) 2026 Marcos Ashton Iglesias
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

SECTION_RODATA 32

; pmaddwd coefficient pairs of the 1-D transforms. The 8-point transform
; pairs inputs (0,4), (1,5), (2,6), (3,7); the 4-point one (0,2), (1,3).
pw_12:      times 8 dw  12,  12
pw_12_m12:  times 8 dw  12, -12
pw_16_6:    times 8 dw  16,   6
pw_6_m16:   times 8 dw   6, -16
pw_16_9:    times 8 dw  16,   9
pw_15_4:    times 8 dw  15,   4
pw_15_m16:  times 8 dw  15, -16
pw_m4_m9:   times 8 dw  -4,  -9
pw_9_4:     times 8 dw   9,   4
pw_m16_15:  times 8 dw -16,  15
pw_4_15:    times 8 dw   4,  15
pw_m9_m16:  times 8 dw  -9, -16
pw_17:      times 8 dw  17,  17
pw_17_m17:  times 8 dw  17, -17
pw_22_10:   times 8 dw  22,  10
pw_m10_22:  times 8 dw -10,  22
pd_4:       times 8 dd 4

cextern pd_1
cextern pd_64

SECTION .text

%macro INV_TRANS_INIT 1 ; width
    movd       m0, blockd
%if %1 == 4
    pshuflw    m0, m0, 0
%else
    SPLATW     m0, m0
%endif
    pxor       m1, m1
    psubw      m1, m0
    packuswb   m0, m0
    packuswb   m1, m1

    DEFINE_ARGS dest, linesize, linesize3
    lea    linesize3q, [linesizeq*3]
%endmacro

%macro INV_TRANS_PROCESS 1
    mov%1                  m2, [destq+linesizeq*0]
    mov%1                  m3, [destq+linesizeq*1]
    mov%1                  m4, [destq+linesizeq*2]
    mov%1                  m5, [destq+linesize3q]
    paddusb                m2, m0
    paddusb                m3, m0
    paddusb                m4, m0
    paddusb                m5, m0
    psubusb                m2, m1
    psubusb                m3, m1
    psubusb                m4, m1
    psubusb                m5, m1
    mov%1 [linesizeq*0+destq], m2
    mov%1 [linesizeq*1+destq], m3
    mov%1 [linesizeq*2+destq], m4
    mov%1 [linesize3q +destq], m5
%endmacro

INIT_XMM sse2
; ff_vc1_inv_trans_?x?_dc_sse2(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
cglobal vc1_inv_trans_4x4_dc, 3,4,6, dest, linesize, block
    movsx         r3d, WORD [blockq]
    mov        blockd, r3d             ; dc
    shl        blockd, 4               ; 16 * dc
    lea        blockd, [blockq+r3+4]   ; 17 * dc + 4
    sar        blockd, 3               ; >> 3
    mov           r3d, blockd          ; dc
    shl        blockd, 4               ; 16 * dc
    lea        blockd, [blockq+r3+64]  ; 17 * dc + 64
    sar        blockd, 7               ; >> 7

    INV_TRANS_INIT  4

    INV_TRANS_PROCESS d
    RET

cglobal vc1_inv_trans_4x8_dc, 3,4,6, dest, linesize, block
    movsx         r3d, WORD [blockq]
    mov        blockd, r3d             ; dc
    shl        blockd, 4               ; 16 * dc
    lea        blockd, [blockq+r3+4]   ; 17 * dc + 4
    sar        blockd, 3               ; >> 3
    shl        blockd, 2               ;  4 * dc
    lea        blockd, [blockq*3+64]   ; 12 * dc + 64
    sar        blockd, 7               ; >> 7

    INV_TRANS_INIT  4

    INV_TRANS_PROCESS d
    lea         destq, [destq+linesizeq*4]
    INV_TRANS_PROCESS d
    RET

cglobal vc1_inv_trans_8x4_dc, 3,4,6, dest, linesize, block
    movsx      blockd, WORD [blockq]   ; dc
    lea        blockd, [blockq*3+1]    ;  3 * dc + 1
    sar        blockd, 1               ; >> 1
    mov           r3d, blockd          ; dc
    shl        blockd, 4               ; 16 * dc
    lea        blockd, [blockq+r3+64]  ; 17 * dc + 64
    sar        blockd, 7               ; >> 7

    INV_TRANS_INIT  8

    INV_TRANS_PROCESS q
    RET

cglobal vc1_inv_trans_8x8_dc, 3,3,6, dest, linesize, block
    movsx      blockd, WORD [blockq]   ; dc
    lea        blockd, [blockq*3+1]    ;  3 * dc + 1
    sar        blockd, 1               ; >> 1
    lea        blockd, [blockq*3+16]   ;  3 * dc + 16
    sar        blockd, 5               ; >> 5

    INV_TRANS_INIT  8

    INV_TRANS_PROCESS q
    lea         destq, [destq+linesizeq*4]
    INV_TRANS_PROCESS q
    RET

;-----------------------------------------------------------------------------
; 1-D 8-point inverse transform on the dword lanes of one register, exact
; 32-bit arithmetic.
; Inputs are pmaddwd pairs (x0,x4), (x1,x5), (x2,x6), (x3,x7), one lane per
; transformed vector; all inputs and temporaries are clobbered.
; Each finished output pair is packed to words as soon as it is complete so
; that the whole pass fits in 8 registers.
; %1-%4 = pairs, %5-%8 = temporaries, %9 = rounding constant, %10 = shift,
; %11 = 1 to add the extra 1 to outputs 4-7 (second pass of the C code)
; out: %7 = out0|out7, %8 = out1|out6, %3 = out2|out5, %2 = out3|out4
;      (low qword | high qword, as words)
;-----------------------------------------------------------------------------
%macro VC1_IDCT8_1D 11
    pmaddwd     m%5, m%1, [pw_12]    ; t1 = 12 * (x0 + x4)
    pmaddwd     m%1, [pw_12_m12]        ; t2 = 12 * (x0 - x4)
    pmaddwd     m%6, m%3, [pw_16_6]     ; t3 = 16 * x2 +  6 * x6
    pmaddwd     m%3, [pw_6_m16]         ; t4 =  6 * x2 - 16 * x6
    paddd       m%5, [%9]
    paddd       m%1, [%9]
    SUMSUB_BA    d, %6, %5              ; t5 = t1 + t3, t8 = t1 - t3
    SUMSUB_BA    d, %3, %1              ; t6 = t2 + t4, t7 = t2 - t4

    pmaddwd     m%7, m%2, [pw_16_9]
    pmaddwd     m%8, m%4, [pw_15_4]
    paddd       m%7, m%8                ; 16 * x1 + 15 * x3 +  9 * x5 +  4 * x7
    SUMSUB_BA    d, %7, %6              ; out0 = t5 + odd, out7 = t5 - odd
%if %11
    paddd       m%6, [pd_1]
%endif
    psrad       m%7, %10
    psrad       m%6, %10
    packssdw    m%7, m%6

    pmaddwd     m%8, m%2, [pw_15_m16]
    pmaddwd     m%6, m%4, [pw_m4_m9]
    paddd       m%8, m%6                ; 15 * x1 -  4 * x3 - 16 * x5 -  9 * x7
    SUMSUB_BA    d, %8, %3              ; out1 = t6 + odd, out6 = t6 - odd
%if %11
    paddd       m%3, [pd_1]
%endif
    psrad       m%8, %10
    psrad       m%3, %10
    packssdw    m%8, m%3

    pmaddwd     m%3, m%2, [pw_9_4]
    pmaddwd     m%6, m%4, [pw_m16_15]
    paddd       m%3, m%6                ;  9 * x1 - 16 * x3 +  4 * x5 + 15 * x7
    SUMSUB_BA    d, %3, %1              ; out2 = t7 + odd, out5 = t7 - odd
%if %11
    paddd       m%1, [pd_1]
%endif
    psrad       m%3, %10
    psrad       m%1, %10
    packssdw    m%3, m%1

    pmaddwd     m%2, [pw_4_15]
    pmaddwd     m%4, [pw_m9_m16]
    paddd       m%2, m%4                ;  4 * x1 -  9 * x3 + 15 * x5 - 16 * x7
    SUMSUB_BA    d, %2, %5              ; out3 = t8 + odd, out4 = t8 - odd
%if %11
    paddd       m%5, [pd_1]
%endif
    psrad       m%2, %10
    psrad       m%5, %10
    packssdw    m%2, m%5
%endmacro

;-----------------------------------------------------------------------------
; 1-D 4-point inverse transform on the dword lanes of one register.
; %1 = (x0,x2) pairs, %2 = (x1,x3) pairs, %3-%4 = temporaries,
; %5 = rounding constant, %6 = shift
; out (dwords): %4 = out0, %1 = out1, %2 = out2, %3 = out3
;-----------------------------------------------------------------------------
%macro VC1_IDCT4_1D 6
    pmaddwd     m%3, m%1, [pw_17]    ; t1 = 17 * (x0 + x2)
    pmaddwd     m%1, [pw_17_m17]        ; t2 = 17 * (x0 - x2)
    pmaddwd     m%4, m%2, [pw_22_10]    ; t3 = 22 * x1 + 10 * x3
    pmaddwd     m%2, [pw_m10_22]        ; t4 = 22 * x3 - 10 * x1
    paddd       m%3, [%5]
    paddd       m%1, [%5]
    SUMSUB_BA    d, %4, %3              ; out0 = t1 + t3, out3 = t1 - t3
    SUMSUB_BA    d, %2, %1              ; out2 = t2 + t4, out1 = t2 - t4
    psrad       m%4, %6
    psrad       m%1, %6
    psrad       m%2, %6
    psrad       m%3, %6
%endmacro

; Add the two 4-pixel rows held as words in m%1 (low qword to [%4], high
; qword to [%5]) to dest with saturation. %2, %3 = temporaries, %6 = zero
%macro ADD_ROWS_4 6
    movd        m%2, [%4]
    movd        m%3, [%5]
    punpckldq   m%2, m%3
    punpcklbw   m%2, m%6
    paddw       m%2, m%1
    packuswb    m%2, m%2
    movd      [%4], m%2
    psrlq       m%2, 32
    movd      [%5], m%2
%endmacro

; Add the 8-pixel row held as words in m%1 to [%3]. %2 = temporary, %4 = zero
%macro ADD_ROW_8 4
    movq        m%2, [%3]
    punpcklbw   m%2, m%4
    paddw       m%2, m%1
    packuswb    m%2, m%2
    movq      [%3], m%2
%endmacro

; The first pass is packed to words with signed saturation where the C code
; truncates. The 8-point rows sum to 90 in magnitude and the 4-point rows to 66,
; so the two agree up to |coeff| = 2912 (90 * 2912 + 4 <= 8 * 32767 + 7) and
; 3971, beyond the -2048..2047 the specification allows.
; 8x8 and 8x4 need a 16-byte aligned block. Unlike the C code, the transforms
; that add to dest leave block untouched.

INIT_XMM sse2
; void ff_vc1_inv_trans_4x4_sse2(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
cglobal vc1_inv_trans_4x4, 3, 4, 5, dest, linesize, block, linesize3
    movq        m0, [blockq+ 0]
    movhps      m0, [blockq+16]         ; rows 0, 1
    movq        m2, [blockq+32]
    movhps      m2, [blockq+48]         ; rows 2, 3
    pshuflw     m0, m0, q3120           ; x0 x2 x1 x3 per row
    pshufhw     m0, m0, q3120
    pshuflw     m2, m2, q3120
    pshufhw     m2, m2, q3120
    mova        m1, m0
    shufps      m0, m2, q2020           ; (x0,x2) pairs, one row per lane
    shufps      m1, m2, q3131           ; (x1,x3) pairs
    VC1_IDCT4_1D 0, 1, 2, 3, pd_4, 3    ; column 0: m3, 1: m0, 2: m1, 3: m2
    packssdw    m3, m0                  ; columns 0 and 1, one row per word
    packssdw    m1, m2                  ; columns 2 and 3
    pshuflw     m3, m3, q3120           ; row0 row2 row1 row3 per column
    pshufhw     m3, m3, q3120
    pshuflw     m1, m1, q3120
    pshufhw     m1, m1, q3120
    mova        m0, m3
    shufps      m3, m1, q2020           ; (row0,row2) pairs, one column per lane
    shufps      m0, m1, q3131           ; (row1,row3) pairs
    VC1_IDCT4_1D 3, 0, 1, 2, pd_64, 7   ; row 0: m2, 1: m3, 2: m0, 3: m1
    packssdw    m2, m3                  ; rows 0, 1
    packssdw    m0, m1                  ; rows 2, 3
    pxor        m4, m4
    lea         linesize3q, [linesizeq*3]
    ADD_ROWS_4  2, 1, 3, destq, destq+linesizeq, 4
    ADD_ROWS_4  0, 1, 3, destq+linesizeq*2, destq+linesize3q, 4
    RET

; void ff_vc1_inv_trans_8x4_sse2(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
cglobal vc1_inv_trans_8x4, 3, 4, 8, dest, linesize, block, linesize3
    mova        m0, [blockq+ 0]
    mova        m1, [blockq+16]
    mova        m2, [blockq+32]
    mova        m3, [blockq+48]
    pshufd      m4, m0, q1032           ; (x0,x4) (x1,x5) (x2,x6) (x3,x7) per row
    punpcklwd   m0, m4
    pshufd      m4, m1, q1032
    punpcklwd   m1, m4
    pshufd      m4, m2, q1032
    punpcklwd   m2, m4
    pshufd      m4, m3, q1032
    punpcklwd   m3, m4
    TRANSPOSE4x4D 0, 1, 2, 3, 4         ; one row per lane
    VC1_IDCT8_1D 0, 1, 2, 3, 4, 5, 6, 7, pd_4, 3, 0
    ; columns 0|7: m6, 1|6: m7, 2|5: m2, 3|4: m1, one row per word
    pshuflw     m6, m6, q3120           ; row0 row2 row1 row3 per column
    pshufhw     m6, m6, q3120
    pshuflw     m7, m7, q3120
    pshufhw     m7, m7, q3120
    pshuflw     m2, m2, q3120
    pshufhw     m2, m2, q3120
    pshuflw     m1, m1, q3120
    pshufhw     m1, m1, q3120
    punpckldq   m0, m6, m7              ; (row0,row2) (row1,row3) pairs, columns 0 1
    punpckhdq   m7, m6                  ;                              columns 6 7
    punpckldq   m3, m2, m1              ;                              columns 2 3
    punpckhdq   m1, m2                  ;                              columns 4 5
    punpcklqdq  m2, m0, m3              ; (row0,row2) pairs, columns 0-3
    punpckhqdq  m0, m3                  ; (row1,row3) pairs, columns 0-3
    punpcklqdq  m3, m1, m7              ; (row0,row2) pairs, columns 4-7
    punpckhqdq  m1, m7                  ; (row1,row3) pairs, columns 4-7
    VC1_IDCT4_1D 2, 0, 4, 5, pd_64, 7   ; row 0: m5, 1: m2, 2: m0, 3: m4
    VC1_IDCT4_1D 3, 1, 6, 7, pd_64, 7   ; row 0: m7, 1: m3, 2: m1, 3: m6
    packssdw    m5, m7
    packssdw    m2, m3
    packssdw    m0, m1
    packssdw    m4, m6
    pxor        m1, m1
    lea         linesize3q, [linesizeq*3]
    ADD_ROW_8   5, 3, destq, 1
    ADD_ROW_8   2, 3, destq+linesizeq, 1
    ADD_ROW_8   0, 3, destq+linesizeq*2, 1
    ADD_ROW_8   4, 3, destq+linesize3q, 1
    RET

; void ff_vc1_inv_trans_4x8_sse2(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
cglobal vc1_inv_trans_4x8, 3, 5, 8, dest, linesize, block, linesize3, dest4
    movq        m0, [blockq+  0]
    movhps      m0, [blockq+ 16]        ; rows 0, 1
    movq        m1, [blockq+ 32]
    movhps      m1, [blockq+ 48]        ; rows 2, 3
    movq        m2, [blockq+ 64]
    movhps      m2, [blockq+ 80]        ; rows 4, 5
    movq        m3, [blockq+ 96]
    movhps      m3, [blockq+112]        ; rows 6, 7
    pshuflw     m0, m0, q3120           ; x0 x2 x1 x3 per row
    pshufhw     m0, m0, q3120
    pshuflw     m1, m1, q3120
    pshufhw     m1, m1, q3120
    pshuflw     m2, m2, q3120
    pshufhw     m2, m2, q3120
    pshuflw     m3, m3, q3120
    pshufhw     m3, m3, q3120
    mova        m4, m0
    shufps      m0, m1, q2020           ; (x0,x2) pairs, rows 0-3
    shufps      m4, m1, q3131           ; (x1,x3) pairs, rows 0-3
    mova        m5, m2
    shufps      m2, m3, q2020           ; (x0,x2) pairs, rows 4-7
    shufps      m5, m3, q3131           ; (x1,x3) pairs, rows 4-7
    VC1_IDCT4_1D 0, 4, 1, 3, pd_4, 3    ; column 0: m3, 1: m0, 2: m4, 3: m1 (rows 0-3)
    VC1_IDCT4_1D 2, 5, 6, 7, pd_4, 3    ; column 0: m7, 1: m2, 2: m5, 3: m6 (rows 4-7)
    packssdw    m3, m7                  ; column 0, one row per word
    packssdw    m0, m2                  ; column 1
    packssdw    m4, m5                  ; column 2
    packssdw    m1, m6                  ; column 3
    pshufd      m2, m3, q1032           ; (row0,row4) (row1,row5) (row2,row6) (row3,row7)
    punpcklwd   m3, m2
    pshufd      m2, m0, q1032
    punpcklwd   m0, m2
    pshufd      m2, m4, q1032
    punpcklwd   m4, m2
    pshufd      m2, m1, q1032
    punpcklwd   m1, m2
    TRANSPOSE4x4D 3, 0, 4, 1, 2         ; one column per lane
    VC1_IDCT8_1D 3, 0, 4, 1, 2, 5, 6, 7, pd_64, 7, 1
    ; rows 0|7: m6, 1|6: m7, 2|5: m4, 3|4: m0
    pxor        m1, m1
    lea         linesize3q, [linesizeq*3]
    lea         dest4q, [destq+linesizeq*4]
    ADD_ROWS_4  6, 2, 3, destq, dest4q+linesize3q, 1
    ADD_ROWS_4  7, 2, 3, destq+linesizeq, dest4q+linesizeq*2, 1
    ADD_ROWS_4  4, 2, 3, destq+linesizeq*2, dest4q+linesizeq, 1
    ADD_ROWS_4  0, 2, 3, destq+linesize3q, dest4q, 1
    RET

%if ARCH_X86_64
; void ff_vc1_inv_trans_8x8_sse2(int16_t block[64])
cglobal vc1_inv_trans_8x8, 1, 1, 12, block
    mova        m0, [blockq+  0]
    mova        m1, [blockq+ 16]
    mova        m2, [blockq+ 32]
    mova        m3, [blockq+ 48]
    mova        m4, [blockq+ 64]
    mova        m5, [blockq+ 80]
    mova        m6, [blockq+ 96]
    mova        m7, [blockq+112]
    ; first pass down the columns; (row0,row4) (row1,row5) (row2,row6)
    ; (row3,row7) pairs, columns 0-3 in m8-m11 and columns 4-7 in m0-m3
    punpcklwd   m8, m0, m4
    punpckhwd   m0, m4
    punpcklwd   m9, m1, m5
    punpckhwd   m1, m5
    punpcklwd   m10, m2, m6
    punpckhwd   m2, m6
    punpcklwd   m11, m3, m7
    punpckhwd   m3, m7
    VC1_IDCT8_1D 8, 9, 10, 11, 4, 5, 6, 7, pd_4, 3, 0
    ; 0|7: m6, 1|6: m7, 2|5: m10, 3|4: m9 (columns 0-3)
    VC1_IDCT8_1D 0, 1, 2, 3, 4, 5, 8, 11, pd_4, 3, 0
    ; 0|7: m8, 1|6: m11, 2|5: m2, 3|4: m1 (columns 4-7)
    punpcklqdq  m0, m6, m8
    punpckhqdq  m6, m8
    punpcklqdq  m3, m7, m11
    punpckhqdq  m7, m11
    punpcklqdq  m4, m10, m2
    punpckhqdq  m10, m2
    punpcklqdq  m5, m9, m1
    punpckhqdq  m9, m1
    TRANSPOSE8x8W 0, 3, 4, 5, 9, 10, 7, 6, 1
    ; second pass down the columns of the transposed intermediate
    punpcklwd   m1, m0, m9
    punpckhwd   m0, m9
    punpcklwd   m2, m3, m10
    punpckhwd   m3, m10
    punpcklwd   m8, m4, m7
    punpckhwd   m4, m7
    punpcklwd   m11, m5, m6
    punpckhwd   m5, m6
    VC1_IDCT8_1D 1, 2, 8, 11, 9, 10, 7, 6, pd_64, 7, 1
    ; 0|7: m7, 1|6: m6, 2|5: m8, 3|4: m2 (columns 0-3)
    VC1_IDCT8_1D 0, 3, 4, 5, 1, 11, 9, 10, pd_64, 7, 1
    ; 0|7: m9, 1|6: m10, 2|5: m4, 3|4: m3 (columns 4-7)
    punpcklqdq  m0, m7, m9
    mova [blockq+  0], m0
    punpckhqdq  m7, m9
    mova [blockq+112], m7
    punpcklqdq  m1, m6, m10
    mova [blockq+ 16], m1
    punpckhqdq  m6, m10
    mova [blockq+ 96], m6
    punpcklqdq  m5, m8, m4
    mova [blockq+ 32], m5
    punpckhqdq  m8, m4
    mova [blockq+ 80], m8
    punpcklqdq  m11, m2, m3
    mova [blockq+ 48], m11
    punpckhqdq  m2, m3
    mova [blockq+ 64], m2
    RET

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
; void ff_vc1_inv_trans_8x8_avx2(int16_t block[64])
; Same as above with columns 0-3 in the low and 4-7 in the high 128-bit lane.
cglobal vc1_inv_trans_8x8, 1, 1, 8, block
    movu        m0, [blockq+ 0]         ; rows 0, 1; checkasm only guarantees
    movu        m1, [blockq+32]         ; rows 2, 3; 16-byte alignment
    movu        m2, [blockq+64]         ; rows 4, 5
    movu        m3, [blockq+96]         ; rows 6, 7
    vpermq      m0, m0, q3120           ; columns 0-3 of both rows | columns 4-7
    vpermq      m1, m1, q3120
    vpermq      m2, m2, q3120
    vpermq      m3, m3, q3120
    punpcklwd   m4, m0, m2              ; (row0,row4) pairs
    punpckhwd   m0, m2                  ; (row1,row5)
    punpcklwd   m2, m1, m3              ; (row2,row6)
    punpckhwd   m1, m3                  ; (row3,row7)
    VC1_IDCT8_1D 4, 0, 2, 1, 3, 5, 6, 7, pd_4, 3, 0
    ; 0|7: m6, 1|6: m7, 2|5: m2, 3|4: m0
    vpermq      m6, m6, q3120           ; row 0 | row 7
    vpermq      m7, m7, q3120           ; row 1 | row 6
    vpermq      m2, m2, q3120           ; row 2 | row 5
    vpermq      m0, m0, q3120           ; row 3 | row 4
    vperm2i128  m1, m6, m0, q0300       ; row 0 | row 4
    vperm2i128  m3, m0, m6, q0300       ; row 3 | row 7
    vperm2i128  m4, m7, m2, q0300       ; row 1 | row 5
    vperm2i128  m5, m2, m7, q0300       ; row 2 | row 6
    ; transpose the 4x8 halves of each lane into columns, which are the
    ; inputs of the second pass; the lanes then hold columns 0-3 | 4-7 again
    SBUTTERFLY  wd, 1, 4, 0
    SBUTTERFLY  wd, 5, 3, 0
    SBUTTERFLY  dq, 1, 5, 0             ; m1 = columns 0, 1; m5 = 2, 3
    SBUTTERFLY  dq, 4, 3, 0             ; m4 = columns 4, 5; m3 = 6, 7
    punpcklwd   m0, m1, m4              ; (0,4) pairs
    punpckhwd   m1, m4                  ; (1,5)
    punpcklwd   m2, m5, m3              ; (2,6)
    punpckhwd   m5, m3                  ; (3,7)
    VC1_IDCT8_1D 0, 1, 2, 5, 3, 4, 6, 7, pd_64, 7, 1
    ; 0|7: m6, 1|6: m7, 2|5: m2, 3|4: m1
    vpermq      m6, m6, q3120           ; row 0 | row 7
    vpermq      m7, m7, q3120           ; row 1 | row 6
    vpermq      m2, m2, q3120           ; row 2 | row 5
    vpermq      m1, m1, q3120           ; row 3 | row 4
    mova         [blockq+  0], xm6
    vextracti128 [blockq+112], m6, 1
    mova         [blockq+ 16], xm7
    vextracti128 [blockq+ 96], m7, 1
    mova         [blockq+ 32], xm2
    vextracti128 [blockq+ 80], m2, 1
    mova         [blockq+ 48], xm1
    vextracti128 [blockq+ 64], m1, 1
    RET
%endif ; HAVE_AVX2_EXTERNAL
%endif ; ARCH_X86_64
