; XVID MPEG-4 VIDEO CODEC
;
; Conversion from gcc syntax to x264asm syntax with modifications
; by Christophe Gisquet <christophe.gisquet@gmail.com>
;
; ===========     SSE2 inverse discrete cosine transform     ===========
;
; Copyright(C) 2003 Pascal Massimino <skal@planet-d.net>
;
; Conversion to gcc syntax with modifications
; by Alexander Strange <astrange@ithinksw.com>
;
; Originally from dct/x86_asm/fdct_sse2_skal.asm in Xvid.
;
; Vertical pass is an implementation of the scheme:
;  Loeffler C., Ligtenberg A., and Moschytz C.S.:
;  Practical Fast 1D DCT Algorithm with Eleven Multiplications,
;  Proc. ICASSP 1989, 988-991.
;
; Horizontal pass is a double 4x4 vector/matrix multiplication,
; (see also Intel's Application Note 922:
;  http://developer.intel.com/vtune/cbts/strmsimd/922down.htm
;  Copyright (C) 1999 Intel Corporation)
;
; More details at http://skal.planet-d.net/coding/dct.html
;
; =======     MMX and XMM forward discrete cosine transform     =======
;
; Copyright(C) 2001 Peter Ross <pross@xvid.org>
;
; Originally provided by Intel at AP-922
; http://developer.intel.com/vtune/cbts/strmsimd/922down.htm
; (See more app notes at http://developer.intel.com/vtune/cbts/strmsimd/appnotes.htm)
; but in a limited edition.
; New macro implements a column part for precise iDCT
; The routine precision now satisfies IEEE standard 1180-1990.
;
; Copyright(C) 2000-2001 Peter Gubanov <peter@elecard.net.ru>
; Rounding trick Copyright(C) 2000 Michel Lespinasse <walken@zoy.org>
;
; http://www.elecard.com/peter/idct.html
; http://www.linuxvideo.org/mpeg2dec/
;
; These examples contain code fragments for first stage iDCT 8x8
; (for rows) and first stage DCT 8x8 (for columns)
;
; conversion to gcc syntax by Michael Niedermayer
;
; ======================================================================
;
; This file is part of FFmpeg.
;
; FFmpeg is free software; you can redistribute it and/or
; modify it under the terms of the GNU Lesser General Public
; License as published by the Free Software Foundation; either
; version 2.1 of the License, or (at your option) any later version.
;
; FFmpeg is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; Lesser General Public License for more details.
;
; You should have received a copy of the GNU Lesser General Public License
; along with FFmpeg; if not, write to the Free Software Foundation,
; Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

%include "libavutil/x86/x86util.asm"

SECTION_RODATA
; Similar to tg_1_16 in MMX code
tan1:   times 8 dw 13036
tan2:   times 8 dw 27146
tan3:   times 8 dw 43790
sqrt2:  times 8 dw 23170

; SSE2 tables
iTab1:  dw 0x4000, 0x539f, 0xc000, 0xac61, 0x4000, 0xdd5d, 0x4000, 0xdd5d
        dw 0x4000, 0x22a3, 0x4000, 0x22a3, 0xc000, 0x539f, 0x4000, 0xac61
        dw 0x3249, 0x11a8, 0x4b42, 0xee58, 0x11a8, 0x4b42, 0x11a8, 0xcdb7
        dw 0x58c5, 0x4b42, 0xa73b, 0xcdb7, 0x3249, 0xa73b, 0x4b42, 0xa73b
iTab2:  dw 0x58c5, 0x73fc, 0xa73b, 0x8c04, 0x58c5, 0xcff5, 0x58c5, 0xcff5
        dw 0x58c5, 0x300b, 0x58c5, 0x300b, 0xa73b, 0x73fc, 0x58c5, 0x8c04
        dw 0x45bf, 0x187e, 0x6862, 0xe782, 0x187e, 0x6862, 0x187e, 0xba41
        dw 0x7b21, 0x6862, 0x84df, 0xba41, 0x45bf, 0x84df, 0x6862, 0x84df
iTab3:  dw 0x539f, 0x6d41, 0xac61, 0x92bf, 0x539f, 0xd2bf, 0x539f, 0xd2bf
        dw 0x539f, 0x2d41, 0x539f, 0x2d41, 0xac61, 0x6d41, 0x539f, 0x92bf
        dw 0x41b3, 0x1712, 0x6254, 0xe8ee, 0x1712, 0x6254, 0x1712, 0xbe4d
        dw 0x73fc, 0x6254, 0x8c04, 0xbe4d, 0x41b3, 0x8c04, 0x6254, 0x8c04
iTab4:  dw 0x4b42, 0x6254, 0xb4be, 0x9dac, 0x4b42, 0xd746, 0x4b42, 0xd746
        dw 0x4b42, 0x28ba, 0x4b42, 0x28ba, 0xb4be, 0x6254, 0x4b42, 0x9dac
        dw 0x3b21, 0x14c3, 0x587e, 0xeb3d, 0x14c3, 0x587e, 0x14c3, 0xc4df
        dw 0x6862, 0x587e, 0x979e, 0xc4df, 0x3b21, 0x979e, 0x587e, 0x979e

%if ARCH_X86_32
; -----------------------------------------------------------------------------
;
; The first stage iDCT 8x8 - inverse DCTs of rows
;
; -----------------------------------------------------------------------------
; The 8-point inverse DCT direct algorithm
; -----------------------------------------------------------------------------
;
; static const short w[32] = {
;     FIX(cos_4_16),  FIX(cos_2_16),  FIX(cos_4_16),  FIX(cos_6_16),
;     FIX(cos_4_16),  FIX(cos_6_16), -FIX(cos_4_16), -FIX(cos_2_16),
;     FIX(cos_4_16), -FIX(cos_6_16), -FIX(cos_4_16),  FIX(cos_2_16),
;     FIX(cos_4_16), -FIX(cos_2_16),  FIX(cos_4_16), -FIX(cos_6_16),
;     FIX(cos_1_16),  FIX(cos_3_16),  FIX(cos_5_16),  FIX(cos_7_16),
;     FIX(cos_3_16), -FIX(cos_7_16), -FIX(cos_1_16), -FIX(cos_5_16),
;     FIX(cos_5_16), -FIX(cos_1_16),  FIX(cos_7_16),  FIX(cos_3_16),
;     FIX(cos_7_16), -FIX(cos_5_16),  FIX(cos_3_16), -FIX(cos_1_16) };
;
; #define DCT_8_INV_ROW(x, y)
; {
;     int a0, a1, a2, a3, b0, b1, b2, b3;
;
;     a0 = x[0] * w[0]  + x[2] * w[1]  + x[4] * w[2]  + x[6] * w[3];
;     a1 = x[0] * w[4]  + x[2] * w[5]  + x[4] * w[6]  + x[6] * w[7];
;     a2 = x[0] * w[8]  + x[2] * w[9]  + x[4] * w[10] + x[6] * w[11];
;     a3 = x[0] * w[12] + x[2] * w[13] + x[4] * w[14] + x[6] * w[15];
;     b0 = x[1] * w[16] + x[3] * w[17] + x[5] * w[18] + x[7] * w[19];
;     b1 = x[1] * w[20] + x[3] * w[21] + x[5] * w[22] + x[7] * w[23];
;     b2 = x[1] * w[24] + x[3] * w[25] + x[5] * w[26] + x[7] * w[27];
;     b3 = x[1] * w[28] + x[3] * w[29] + x[5] * w[30] + x[7] * w[31];
;
;     y[0] = SHIFT_ROUND(a0 + b0);
;     y[1] = SHIFT_ROUND(a1 + b1);
;     y[2] = SHIFT_ROUND(a2 + b2);
;     y[3] = SHIFT_ROUND(a3 + b3);
;     y[4] = SHIFT_ROUND(a3 - b3);
;     y[5] = SHIFT_ROUND(a2 - b2);
;     y[6] = SHIFT_ROUND(a1 - b1);
;     y[7] = SHIFT_ROUND(a0 - b0);
; }
;
; -----------------------------------------------------------------------------
;
; In this implementation the outputs of the iDCT-1D are multiplied
;     for rows 0,4 - by cos_4_16,
;     for rows 1,7 - by cos_1_16,
;     for rows 2,6 - by cos_2_16,
;     for rows 3,5 - by cos_3_16
; and are shifted to the left for better accuracy.
;
; For the constants used,
;     FIX(float_const) = (short) (float_const * (1 << 15) + 0.5)
;
; -----------------------------------------------------------------------------

; -----------------------------------------------------------------------------
; Tables for mmx processors
; -----------------------------------------------------------------------------

; Table for rows 0,4 - constants are multiplied by cos_4_16
tab_i_04_mmx: dw  16384,  16384,  16384, -16384
              dw  21407,   8867,   8867, -21407 ; w07 w05 w03 w01
              dw  16384, -16384,  16384,  16384 ; w14 w12 w10 w08
              dw  -8867,  21407, -21407,  -8867 ; w15 w13 w11 w09
              dw  22725,  12873,  19266, -22725 ; w22 w20 w18 w16
              dw  19266,   4520,  -4520, -12873 ; w23 w21 w19 w17
              dw  12873,   4520,   4520,  19266 ; w30 w28 w26 w24
              dw -22725,  19266, -12873, -22725 ; w31 w29 w27 w25
; Table for rows 1,7 - constants are multiplied by cos_1_16
              dw  22725,  22725,  22725, -22725 ; movq-> w06 w04 w02 w00
              dw  29692,  12299,  12299, -29692 ; w07 w05 w03 w01
              dw  22725, -22725,  22725,  22725 ; w14 w12 w10 w08
              dw -12299,  29692, -29692, -12299 ; w15 w13 w11 w09
              dw  31521,  17855,  26722, -31521 ; w22 w20 w18 w16
              dw  26722,   6270,  -6270, -17855 ; w23 w21 w19 w17
              dw  17855,   6270,   6270,  26722 ; w30 w28 w26 w24
              dw -31521,  26722, -17855, -31521 ; w31 w29 w27 w25
; Table for rows 2,6 - constants are multiplied by cos_2_16
              dw  21407,  21407,  21407, -21407 ; movq-> w06 w04 w02 w00
              dw  27969,  11585,  11585, -27969 ; w07 w05 w03 w01
              dw  21407, -21407,  21407,  21407 ; w14 w12 w10 w08
              dw -11585,  27969, -27969, -11585 ; w15 w13 w11 w09
              dw  29692,  16819,  25172, -29692 ; w22 w20 w18 w16
              dw  25172,   5906,  -5906, -16819 ; w23 w21 w19 w17
              dw  16819,   5906,   5906,  25172 ; w30 w28 w26 w24
              dw -29692,  25172, -16819, -29692 ; w31 w29 w27 w25
; Table for rows 3,5 - constants are multiplied by cos_3_16
              dw  19266,  19266,  19266, -19266 ; movq-> w06 w04 w02 w00
              dw  25172,  10426,  10426, -25172 ; w07 w05 w03 w01
              dw  19266, -19266,  19266,  19266 ; w14 w12 w10 w08
              dw -10426,  25172, -25172, -10426 ; w15 w13 w11 w09
              dw  26722,  15137,  22654, -26722 ; w22 w20 w18 w16
              dw  22654,   5315,  -5315, -15137 ; w23 w21 w19 w17
              dw  15137,   5315,   5315,  22654 ; w30 w28 w26 w24
              dw -26722,  22654, -15137, -26722 ; w31 w29 w27 w25

; -----------------------------------------------------------------------------
; Tables for xmm processors
; -----------------------------------------------------------------------------

; %3 for rows 0,4 - constants are multiplied by cos_4_16
tab_i_04_xmm: dw  16384,  21407,  16384,   8867 ; movq-> w05 w04 w01 w00
              dw  16384,   8867, -16384, -21407 ; w07 w06 w03 w02
              dw  16384,  -8867,  16384, -21407 ; w13 w12 w09 w08
              dw -16384,  21407,  16384,  -8867 ; w15 w14 w11 w10
              dw  22725,  19266,  19266,  -4520 ; w21 w20 w17 w16
              dw  12873,   4520, -22725, -12873 ; w23 w22 w19 w18
              dw  12873, -22725,   4520, -12873 ; w29 w28 w25 w24
              dw   4520,  19266,  19266, -22725 ; w31 w30 w27 w26
; %3 for rows 1,7 - constants are multiplied by cos_1_16
              dw  22725,  29692,  22725,  12299 ; movq-> w05 w04 w01 w00
              dw  22725,  12299, -22725, -29692 ; w07 w06 w03 w02
              dw  22725, -12299,  22725, -29692 ; w13 w12 w09 w08
              dw -22725,  29692,  22725, -12299 ; w15 w14 w11 w10
              dw  31521,  26722,  26722,  -6270 ; w21 w20 w17 w16
              dw  17855,   6270, -31521, -17855 ; w23 w22 w19 w18
              dw  17855, -31521,   6270, -17855 ; w29 w28 w25 w24
              dw   6270,  26722,  26722, -31521 ; w31 w30 w27 w26
; %3 for rows 2,6 - constants are multiplied by cos_2_16
              dw  21407,  27969,  21407,  11585 ; movq-> w05 w04 w01 w00
              dw  21407,  11585, -21407, -27969 ; w07 w06 w03 w02
              dw  21407, -11585,  21407, -27969 ; w13 w12 w09 w08
              dw -21407,  27969,  21407, -11585 ; w15 w14 w11 w10
              dw  29692,  25172,  25172,  -5906 ; w21 w20 w17 w16
              dw  16819,   5906, -29692, -16819 ; w23 w22 w19 w18
              dw  16819, -29692,   5906, -16819 ; w29 w28 w25 w24
              dw   5906,  25172,  25172, -29692 ; w31 w30 w27 w26
; %3 for rows 3,5 - constants are multiplied by cos_3_16
              dw  19266,  25172,  19266,  10426 ; movq-> w05 w04 w01 w00
              dw  19266,  10426, -19266, -25172 ; w07 w06 w03 w02
              dw  19266, -10426,  19266, -25172 ; w13 w12 w09 w08
              dw -19266,  25172,  19266, -10426 ; w15 w14 w11 w10
              dw  26722,  22654,  22654,  -5315 ; w21 w20 w17 w16
              dw  15137,   5315, -26722, -15137 ; w23 w22 w19 w18
              dw  15137, -26722,   5315, -15137 ; w29 w28 w25 w24
              dw   5315,  22654,  22654, -26722 ; w31 w30 w27 w26
%endif ; ~ARCH_X86_32

; Similar to rounder_0 in MMX code
; 4 first similar, then: 4*8->6*16  5*8->4*16  6/7*8->5*16
walkenIdctRounders: times 4 dd 65536
                    times 4 dd  3597
                    times 4 dd  2260
                    times 4 dd  1203
                    times 4 dd   120
                    times 4 dd   512
                    times 2 dd     0

pb_127: times 8 db 127

SECTION .text

; Temporary storage before the column pass
%define ROW1 xmm6
%define ROW3 xmm4
%define ROW5 xmm5
%define ROW7 xmm7

%macro CLEAR_ODD 1
    pxor      %1, %1
%endmacro
%macro PUT_ODD 1
    pshufhw   %1, xmm2, 0x1B
%endmacro

%macro MOV32 2
%if ARCH_X86_32
    movdqa    %2, %1
%endif
%endmacro

%macro CLEAR_EVEN 1
%if ARCH_X86_64
    CLEAR_ODD %1
%endif
%endmacro

%macro PUT_EVEN 1
%if ARCH_X86_64
    PUT_ODD   %1
%else
    pshufhw xmm2, xmm2, 0x1B
    movdqa    %1, xmm2
%endif
%endmacro

%if ARCH_X86_64
%define ROW0  xmm8
%define REG0  ROW0
%define ROW2  xmm9
%define REG2  ROW2
%define ROW4  xmm10
%define REG4  ROW4
%define ROW6  xmm11
%define REG6  ROW6
%define XMMS  xmm12
%define SREG2 REG2
%define TAN3  xmm13
%define TAN1  xmm14
%else
%define ROW0  [BLOCK + 0*16]
%define REG0  xmm4
%define ROW2  [BLOCK + 2*16]
%define REG2  xmm4
%define ROW4  [BLOCK + 4*16]
%define REG4  xmm6
%define ROW6  [BLOCK + 6*16]
%define REG6  xmm6
%define XMMS  xmm2
%define SREG2 xmm7
%define TAN3  xmm0
%define TAN1  xmm2
%endif

%macro JZ  2
    test      %1, %1
    jz       .%2
%endmacro

%macro JNZ  2
    test      %1, %1
    jnz      .%2
%endmacro

%macro TEST_ONE_ROW 4 ; src, reg, clear, arg
    %3        %4
    movq     mm1, [%1]
    por      mm1, [%1 + 8]
    paddusb  mm1, mm0
    pmovmskb  %2, mm1
%endmacro

;row1, row2, reg1, reg2, clear1, arg1, clear2, arg2
%macro  TEST_TWO_ROWS  8
    %5         %6
    %7         %8
    movq      mm1, [%1 + 0]
    por       mm1, [%1 + 8]
    movq      mm2, [%2 + 0]
    por       mm2, [%2 + 8]
    paddusb   mm1, mm0
    paddusb   mm2, mm0
    pmovmskb   %3, mm1
    pmovmskb   %4, mm2
%endmacro

; IDCT pass on rows.
%macro iMTX_MULT   4-5 ; src, table, put, arg, rounder
    movdqa       xmm3, [%1]
    movdqa       xmm0, xmm3
    pshufd       xmm1, xmm3, 0x11 ; 4602
    punpcklqdq   xmm0, xmm0       ; 0246
    pmaddwd      xmm0, [%2]
    pmaddwd      xmm1, [%2+16]
    pshufd       xmm2, xmm3, 0xBB ; 5713
    punpckhqdq   xmm3, xmm3       ; 1357
    pmaddwd      xmm2, [%2+32]
    pmaddwd      xmm3, [%2+48]
    paddd        xmm0, xmm1
    paddd        xmm2, xmm3
%if %0 == 5
    paddd        xmm0, [walkenIdctRounders+%5]
%endif
    movdqa       xmm3, xmm2
    paddd        xmm2, xmm0
    psubd        xmm0, xmm3
    psrad        xmm2, 11
    psrad        xmm0, 11
    packssdw     xmm2, xmm0
    %3           %4
%endmacro

%macro iLLM_HEAD 0
    movdqa   TAN3, [tan3]
    movdqa   TAN1, [tan1]
%endmacro

%macro FIRST_HALF 2  ; %1=dct  %2=type(normal,add,put)
    psraw    xmm5, 6
    psraw    REG0, 6
    psraw    TAN3, 6
    psraw    xmm3, 6
    ; dct coeffs must still be written for AC prediction
%if %2 == 0
    movdqa   [%1+1*16], TAN3
    movdqa   [%1+2*16], xmm3
    movdqa   [%1+5*16], REG0
    movdqa   [%1+6*16], xmm5
%else
    ; Must now load args as gprs are no longer used for masks
    ; DEST is set to where address of dest was loaded
    %if ARCH_X86_32
        %if %2 == 2 ; Not enough xmms, store
    movdqa   [%1+1*16], TAN3
    movdqa   [%1+2*16], xmm3
    movdqa   [%1+5*16], REG0
    movdqa   [%1+6*16], xmm5
        %endif
    %xdefine DEST r2q ; BLOCK is r0, stride r1
    movifnidn DEST, destm
    movifnidn strideq, stridem
    %else
    %xdefine DEST r0q
    %endif
    lea      r3q, [3*strideq]
    %if %2 == 1
    packuswb TAN3, xmm3
    packuswb xmm5, REG0
    movq     [DEST + strideq], TAN3
    movhps   [DEST + 2*strideq], TAN3
    ; REG0 and TAN3 are now available (and likely used in second half)
    %endif
%endif
%endmacro

%macro SECOND_HALF 6 ; %1=dct  %2=type(normal,add,put) 3-6: xmms
    psraw    %3, 6
    psraw    %4, 6
    psraw    %5, 6
    psraw    %6, 6
    ; dct coeffs must still be written for AC prediction
%if %2 == 0
    movdqa   [%1+0*16], %3
    movdqa   [%1+3*16], %5
    movdqa   [%1+4*16], %6
    movdqa   [%1+7*16], %4
%elif %2 == 1
    packuswb %3, %5
    packuswb %6, %4
    ; address of dest may have been loaded
    movq     [DEST], %3
    movhps   [DEST + r3q], %3
    lea      DEST, [DEST + 4*strideq]
    movq     [DEST], %6
    movhps   [DEST + r3q], %6
    ; and now write remainder of first half
    movq     [DEST + 2*strideq], xmm5
    movhps   [DEST + strideq], xmm5
%elif %2 == 2
    pxor        xmm0, xmm0
    %if ARCH_X86_32
    ; free: m3 REG0=m4 m5
    ; input: m1, m7, m2, m6
    movq        xmm3, [DEST+0*strideq]
    movq        xmm4, [DEST+1*strideq]
    punpcklbw   xmm3, xmm0
    punpcklbw   xmm4, xmm0
    paddsw      xmm3, %3
    paddsw      xmm4, [%1 + 1*16]
    movq          %3, [DEST+2*strideq]
    movq        xmm5, [DEST+      r3q]
    punpcklbw     %3, xmm0
    punpcklbw   xmm5, xmm0
    paddsw        %3, [%1 + 2*16]
    paddsw      xmm5, %5
    packuswb    xmm3, xmm4
    packuswb      %3, xmm5
    movq    [DEST+0*strideq], xmm3
    movhps  [DEST+1*strideq], xmm3
    movq    [DEST+2*strideq], %3
    movhps  [DEST+      r3q], %3
    lea         DEST, [DEST+4*strideq]
    movq        xmm3, [DEST+0*strideq]
    movq        xmm4, [DEST+1*strideq]
    movq          %3, [DEST+2*strideq]
    movq        xmm5, [DEST+      r3q]
    punpcklbw   xmm3, xmm0
    punpcklbw   xmm4, xmm0
    punpcklbw     %3, xmm0
    punpcklbw   xmm5, xmm0
    paddsw      xmm3, %6
    paddsw      xmm4, [%1 + 5*16]
    paddsw        %3, [%1 + 6*16]
    paddsw      xmm5, %4
    packuswb    xmm3, xmm4
    packuswb      %3, xmm5
    movq    [DEST+0*strideq], xmm3
    movhps  [DEST+1*strideq], xmm3
    movq    [DEST+2*strideq], %3
    movhps  [DEST+      r3q], %3
    %else
    ; l1:TAN3=m13  l2:m3  l5:REG0=m8 l6=m5
    ; input: m1, m7/SREG2=m9, TAN1=m14, REG4=m10
    movq        xmm2, [DEST+0*strideq]
    movq        xmm4, [DEST+1*strideq]
    movq       xmm12, [DEST+2*strideq]
    movq       xmm11, [DEST+      r3q]
    punpcklbw   xmm2, xmm0
    punpcklbw   xmm4, xmm0
    punpcklbw  xmm12, xmm0
    punpcklbw  xmm11, xmm0
    paddsw      xmm2, %3
    paddsw      xmm4, TAN3
    paddsw     xmm12, xmm3
    paddsw     xmm11, %5
    packuswb    xmm2, xmm4
    packuswb   xmm12, xmm11
    movq    [DEST+0*strideq], xmm2
    movhps  [DEST+1*strideq], xmm2
    movq    [DEST+2*strideq], xmm12
    movhps  [DEST+      r3q], xmm12
    lea         DEST, [DEST+4*strideq]
    movq        xmm2, [DEST+0*strideq]
    movq        xmm4, [DEST+1*strideq]
    movq       xmm12, [DEST+2*strideq]
    movq       xmm11, [DEST+      r3q]
    punpcklbw   xmm2, xmm0
    punpcklbw   xmm4, xmm0
    punpcklbw  xmm12, xmm0
    punpcklbw  xmm11, xmm0
    paddsw      xmm2, %6
    paddsw      xmm4, REG0
    paddsw     xmm12, xmm5
    paddsw     xmm11, %4
    packuswb    xmm2, xmm4
    packuswb   xmm12, xmm11
    movq    [DEST+0*strideq], xmm2
    movhps  [DEST+1*strideq], xmm2
    movq    [DEST+2*strideq], xmm12
    movhps  [DEST+      r3q], xmm12
    %endif
%endif
%endmacro


; IDCT pass on columns.
%macro iLLM_PASS  2  ; %1=dct  %2=type(normal,add,put)
    movdqa   xmm1, TAN3
    movdqa   xmm3, TAN1
    pmulhw   TAN3, xmm4
    pmulhw   xmm1, xmm5
    paddsw   TAN3, xmm4
    paddsw   xmm1, xmm5
    psubsw   TAN3, xmm5
    paddsw   xmm1, xmm4
    pmulhw   xmm3, xmm7
    pmulhw   TAN1, xmm6
    paddsw   xmm3, xmm6
    psubsw   TAN1, xmm7
    movdqa   xmm7, xmm3
    movdqa   xmm6, TAN1
    psubsw   xmm3, xmm1
    psubsw   TAN1, TAN3
    paddsw   xmm1, xmm7
    paddsw   TAN3, xmm6
    movdqa   xmm6, xmm3
    psubsw   xmm3, TAN3
    paddsw   TAN3, xmm6
    movdqa   xmm4, [sqrt2]
    pmulhw   xmm3, xmm4
    pmulhw   TAN3, xmm4
    paddsw   TAN3, TAN3
    paddsw   xmm3, xmm3
    movdqa   xmm7, [tan2]
    MOV32    ROW2, REG2
    MOV32    ROW6, REG6
    movdqa   xmm5, xmm7
    pmulhw   xmm7, REG6
    pmulhw   xmm5, REG2
    paddsw   xmm7, REG2
    psubsw   xmm5, REG6
    MOV32    ROW0, REG0
    MOV32    ROW4, REG4
    MOV32    TAN1, [BLOCK]
    movdqa   XMMS, REG0
    psubsw   REG0, REG4
    paddsw   REG4, XMMS
    movdqa   XMMS, REG4
    psubsw   REG4, xmm7
    paddsw   xmm7, XMMS
    movdqa   XMMS, REG0
    psubsw   REG0, xmm5
    paddsw   xmm5, XMMS
    movdqa   XMMS, xmm5
    psubsw   xmm5, TAN3
    paddsw   TAN3, XMMS
    movdqa   XMMS, REG0
    psubsw   REG0, xmm3
    paddsw   xmm3, XMMS
    MOV32    [BLOCK], TAN1

    FIRST_HALF %1, %2

    movdqa   xmm0, xmm7
    movdqa   xmm4, REG4
    psubsw   xmm7, xmm1
    psubsw   REG4, TAN1
    paddsw   xmm1, xmm0
    paddsw   TAN1, xmm4

    SECOND_HALF %1, %2, xmm1, xmm7, TAN1, REG4
%endmacro

; IDCT pass on columns, assuming rows 4-7 are zero
%macro iLLM_PASS_SPARSE   2 ; %1=dct   %2=type(normal,put,add)
    pmulhw   TAN3, xmm4
    paddsw   TAN3, xmm4
    movdqa   xmm3, xmm6
    pmulhw   TAN1, xmm6
    movdqa   xmm1, xmm4
    psubsw   xmm3, xmm1
    paddsw   xmm1, xmm6
    movdqa   xmm6, TAN1
    psubsw   TAN1, TAN3
    paddsw   TAN3, xmm6
    movdqa   xmm6, xmm3
    psubsw   xmm3, TAN3
    paddsw   TAN3, xmm6
    movdqa   xmm4, [sqrt2]
    pmulhw   xmm3, xmm4
    pmulhw   TAN3, xmm4
    paddsw   TAN3, TAN3
    paddsw   xmm3, xmm3
    movdqa   xmm5, [tan2]
    MOV32    ROW2, SREG2
    pmulhw   xmm5, SREG2
    MOV32    ROW0, REG0
    movdqa   xmm6, REG0
    psubsw   xmm6, SREG2
    paddsw  SREG2, REG0
    MOV32    TAN1, [BLOCK]
    movdqa   XMMS, REG0
    psubsw   REG0, xmm5
    paddsw   xmm5, XMMS
    movdqa   XMMS, xmm5
    psubsw   xmm5, TAN3
    paddsw   TAN3, XMMS
    movdqa   XMMS, REG0
    psubsw   REG0, xmm3
    paddsw   xmm3, XMMS
    MOV32    [BLOCK], TAN1

    FIRST_HALF %1, %2

    movdqa   xmm0, SREG2
    movdqa   xmm4, xmm6
    psubsw  SREG2, xmm1
    psubsw   xmm6, TAN1
    paddsw   xmm1, xmm0
    paddsw   TAN1, xmm4

    SECOND_HALF %1, %2, xmm1, SREG2, TAN1, xmm6
%endmacro

%macro IDCT_SSE2 1 ; 0=normal  1=put  2=add
%if %1 == 0 || ARCH_X86_32
    %define GPR0  r1d
    %define GPR1  r2d
    %define GPR2  r3d
    %define GPR3  r4d
    %define NUM_GPRS 5
%else
    %define GPR0  r3d
    %define GPR1  r4d
    %define GPR2  r5d
    %define GPR3  r6d
    %define NUM_GPRS 7
%endif
%if %1 == 0
cglobal xvid_idct, 1, NUM_GPRS, 8+7*ARCH_X86_64, block
%xdefine BLOCK blockq
%else
    %if %1 == 1
cglobal xvid_idct_put, 0, NUM_GPRS, 8+7*ARCH_X86_64, dest, stride, block
    %else
cglobal xvid_idct_add, 0, NUM_GPRS, 8+7*ARCH_X86_64, dest, stride, block
    %endif
    %if ARCH_X86_64
    %xdefine BLOCK blockq
    %else
    mov    r0q, blockm
    %xdefine BLOCK r0q
    %endif
%endif
    movq           mm0, [pb_127]
    iMTX_MULT      BLOCK + 0*16, iTab1, PUT_EVEN, ROW0, 0*16
    iMTX_MULT      BLOCK + 1*16, iTab2, PUT_ODD, ROW1,  1*16
    iMTX_MULT      BLOCK + 2*16, iTab3, PUT_EVEN, ROW2, 2*16

    TEST_TWO_ROWS  BLOCK + 3*16, BLOCK + 4*16, GPR0, GPR1, CLEAR_ODD, ROW3, CLEAR_EVEN, ROW4 ; a, c
    JZ   GPR0, col1
    iMTX_MULT      BLOCK + 3*16, iTab4, PUT_ODD, ROW3,  3*16
.col1:
    TEST_TWO_ROWS  BLOCK + 5*16, BLOCK + 6*16, GPR0, GPR2, CLEAR_ODD, ROW5, CLEAR_EVEN, ROW6 ; a, d
    TEST_ONE_ROW   BLOCK + 7*16, GPR3, CLEAR_ODD, ROW7 ; esi

    iLLM_HEAD
    JNZ  GPR1, 2
    JNZ  GPR0, 3
    JNZ  GPR2, 4
    JNZ  GPR3, 5
    iLLM_PASS_SPARSE BLOCK, %1
    jmp .6
.2:
    iMTX_MULT     BLOCK + 4*16, iTab1, PUT_EVEN, ROW4
.3:
    iMTX_MULT     BLOCK + 5*16, iTab4, PUT_ODD, ROW5,  4*16
    JZ   GPR2, col2
.4:
    iMTX_MULT     BLOCK + 6*16, iTab3, PUT_EVEN, ROW6, 5*16
.col2:
    JZ   GPR3, col3
.5:
    iMTX_MULT     BLOCK + 7*16, iTab2, PUT_ODD, ROW7,  5*16
.col3:
%if ARCH_X86_32
    iLLM_HEAD
%endif
    iLLM_PASS     BLOCK, %1
.6:
    RET
%endmacro

INIT_XMM sse2
IDCT_SSE2 0
IDCT_SSE2 1
IDCT_SSE2 2

%if ARCH_X86_32

; %1=offset  %2=tab_offset
; %3=rnd_offset where 4*8->6*16  5*8->4*16  6/7*8->5*16
%macro DCT_8_INV_ROW  3
    movq       mm0, [r0+16*%1+0]  ; 0 ; x3 x2 x1 x0
    movq       mm1, [r0+16*%1+8]  ; 1 ; x7 x6 x5 x4
    movq       mm2, mm0       ; 2 ; x3 x2 x1 x0
    movq       mm3, [%2+ 0]   ; 3 ; w06 w04 w02 w00
%if cpuflag(mmxext)
    pshufw     mm0, mm0, 0x88 ; x2 x0 x2 x0
    movq       mm4, [%2+ 8]   ; 4 ; w07 w06 w03 w02
    movq       mm5, mm1       ; 5 ; x7 x6 x5 x4
    pmaddwd    mm3, mm0       ; x2*w05+x0*w04 x2*w01+x0*w00
    movq       mm6, [%2+32]   ; 6 ; w21 w20 w17 w16
    pshufw     mm1, mm1, 0x88 ; x6 x4 x6 x4
    pmaddwd    mm4, mm1       ; x6*w07+x4*w06 x6*w03+x4*w02
    movq       mm7, [%2+40]   ; 7; w23 w22 w19 w18
    pshufw     mm2, mm2, 0xdd ; x3 x1 x3 x1
    pmaddwd    mm6, mm2       ; x3*w21+x1*w20 x3*w17+x1*w16
    pshufw     mm5, mm5, 0xdd ; x7 x5 x7 x5
    pmaddwd    mm7, mm5       ; x7*w23+x5*w22 x7*w19+x5*w18
    paddd      mm3, [walkenIdctRounders + %3]      ; +%3
    pmaddwd    mm0, [%2+16]   ; x2*w13+x0*w12 x2*w09+x0*w08
    paddd      mm3, mm4       ; 4 ; a1=sum(even1) a0=sum(even0)
    pmaddwd    mm1, [%2+24]   ; x6*w15+x4*w14 x6*w11+x4*w10
    movq       mm4, mm3       ; 4 ; a1 a0
    pmaddwd    mm2, [%2+48]   ; x3*w29+x1*w28 x3*w25+x1*w24
    paddd      mm6, mm7       ; 7 ; b1=sum(odd1) b0=sum(odd0)
    pmaddwd    mm5, [%2+56]   ; x7*w31+x5*w30 x7*w27+x5*w26
    paddd      mm3, mm6       ; a1+b1 a0+b0
    paddd      mm0, [walkenIdctRounders + %3]      ; +%3
    psrad      mm3, 11        ; y1=a1+b1 y0=a0+b0
    paddd      mm0, mm1       ; 1 ; a3=sum(even3) a2=sum(even2)
    psubd      mm4, mm6       ; 6 ; a1-b1 a0-b0
    movq       mm7, mm0       ; 7 ; a3 a2
    paddd      mm2, mm5       ; 5 ; b3=sum(odd3) b2=sum(odd2)
    paddd      mm0, mm2       ; a3+b3 a2+b2
    psrad      mm4, 11        ; y6=a1-b1 y7=a0-b0
    psubd      mm7, mm2       ; 2 ; a3-b3 a2-b2
    psrad      mm0, 11        ; y3=a3+b3 y2=a2+b2
    psrad      mm7, 11        ; y4=a3-b3 y5=a2-b2
    packssdw   mm3, mm0       ; 0 ; y3 y2 y1 y0
    packssdw   mm7, mm4       ; 4 ; y6 y7 y4 y5
    movq  [r0+16*%1+0], mm3       ; 3 ; save y3 y2 y1 y0
    pshufw     mm7, mm7, 0xb1 ; y7 y6 y5 y4
%else
    punpcklwd  mm0, mm1       ; x5 x1 x4 x0
    movq       mm5, mm0       ; 5 ; x5 x1 x4 x0
    punpckldq  mm0, mm0       ; x4 x0 x4 x0
    movq       mm4, [%2+ 8]   ; 4 ; w07 w05 w03 w01
    punpckhwd  mm2, mm1       ; 1 ; x7 x3 x6 x2
    pmaddwd    mm3, mm0       ; x4*w06+x0*w04 x4*w02+x0*w00
    movq       mm6, mm2       ; 6 ; x7 x3 x6 x2
    movq       mm1, [%2+32]   ; 1 ; w22 w20 w18 w16
    punpckldq  mm2, mm2       ; x6 x2 x6 x2
    pmaddwd    mm4, mm2       ; x6*w07+x2*w05 x6*w03+x2*w01
    punpckhdq  mm5, mm5       ; x5 x1 x5 x1
    pmaddwd    mm0, [%2+16]   ; x4*w14+x0*w12 x4*w10+x0*w08
    punpckhdq  mm6, mm6       ; x7 x3 x7 x3
    movq       mm7, [%2+40]   ; 7 ; w23 w21 w19 w17
    pmaddwd    mm1, mm5       ; x5*w22+x1*w20 x5*w18+x1*w16
    paddd      mm3, [walkenIdctRounders + %3]     ; +%3
    pmaddwd    mm7, mm6       ; x7*w23+x3*w21 x7*w19+x3*w17
    pmaddwd    mm2, [%2+24]   ; x6*w15+x2*w13 x6*w11+x2*w09
    paddd      mm3, mm4       ; 4 ; a1=sum(even1) a0=sum(even0)
    pmaddwd    mm5, [%2+48]   ; x5*w30+x1*w28 x5*w26+x1*w24
    movq       mm4, mm3       ; 4 ; a1 a0
    pmaddwd    mm6, [%2+56]   ; x7*w31+x3*w29 x7*w27+x3*w25
    paddd      mm1, mm7       ; 7 ; b1=sum(odd1) b0=sum(odd0)
    paddd      mm0, [walkenIdctRounders + %3]     ; +%3
    psubd      mm3, mm1       ; a1-b1 a0-b0
    psrad      mm3, 11        ; y6=a1-b1 y7=a0-b0
    paddd      mm1, mm4       ; 4 ; a1+b1 a0+b0
    paddd      mm0, mm2       ; 2 ; a3=sum(even3) a2=sum(even2)
    psrad      mm1, 11        ; y1=a1+b1 y0=a0+b0
    paddd      mm5, mm6       ; 6 ; b3=sum(odd3) b2=sum(odd2)
    movq       mm4, mm0       ; 4 ; a3 a2
    paddd      mm0, mm5       ; a3+b3 a2+b2
    psubd      mm4, mm5       ; 5 ; a3-b3 a2-b2
    psrad      mm0, 11        ; y3=a3+b3 y2=a2+b2
    psrad      mm4, 11        ; y4=a3-b3 y5=a2-b2
    packssdw   mm1, mm0       ; 0 ; y3 y2 y1 y0
    packssdw   mm4, mm3       ; 3 ; y6 y7 y4 y5
    movq       mm7, mm4       ; 7 ; y6 y7 y4 y5
    psrld      mm4, 16        ; 0 y6 0 y4
    pslld      mm7, 16        ; y7 0 y5 0
    movq  [r0+16*%1+0], mm1   ; 1 ; save y3 y2 y1 y0
    por        mm7, mm4       ; 4 ; y7 y6 y5 y4
%endif
    movq  [r0+16*%1+8], mm7   ; 7 ; save y7 y6 y5 y4
%endmacro

; -----------------------------------------------------------------------------
;
; The first stage DCT 8x8 - forward DCTs of columns
;
; The %2puts are multiplied
; for rows 0,4 - on cos_4_16,
; for rows 1,7 - on cos_1_16,
; for rows 2,6 - on cos_2_16,
; for rows 3,5 - on cos_3_16
; and are shifted to the left for rise of accuracy
;
; -----------------------------------------------------------------------------
;
; The 8-point scaled forward DCT algorithm (26a8m)
;
; -----------------------------------------------------------------------------
;
;#define DCT_8_FRW_COL(x, y)
; {
;     short t0, t1, t2, t3, t4, t5, t6, t7;
;     short tp03, tm03, tp12, tm12, tp65, tm65;
;     short tp465, tm465, tp765, tm765;
;
;     t0 = LEFT_SHIFT(x[0] + x[7]);
;     t1 = LEFT_SHIFT(x[1] + x[6]);
;     t2 = LEFT_SHIFT(x[2] + x[5]);
;     t3 = LEFT_SHIFT(x[3] + x[4]);
;     t4 = LEFT_SHIFT(x[3] - x[4]);
;     t5 = LEFT_SHIFT(x[2] - x[5]);
;     t6 = LEFT_SHIFT(x[1] - x[6]);
;     t7 = LEFT_SHIFT(x[0] - x[7]);
;
;     tp03 = t0 + t3;
;     tm03 = t0 - t3;
;     tp12 = t1 + t2;
;     tm12 = t1 - t2;
;
;     y[0] = tp03 + tp12;
;     y[4] = tp03 - tp12;
;
;     y[2] = tm03 + tm12 * tg_2_16;
;     y[6] = tm03 * tg_2_16 - tm12;
;
;     tp65 = (t6 + t5) * cos_4_16;
;     tm65 = (t6 - t5) * cos_4_16;
;
;     tp765 = t7 + tp65;
;     tm765 = t7 - tp65;
;     tp465 = t4 + tm65;
;     tm465 = t4 - tm65;
;
;     y[1] = tp765 + tp465 * tg_1_16;
;     y[7] = tp765 * tg_1_16 - tp465;
;     y[5] = tm765 * tg_3_16 + tm465;
;     y[3] = tm765 - tm465 * tg_3_16;
; }
;
; -----------------------------------------------------------------------------

; -----------------------------------------------------------------------------
; DCT_8_INV_COL_4  INP,OUT
; -----------------------------------------------------------------------------
%macro DCT_8_INV_COL 1
    movq        mm0, [tan3]
    movq        mm3, [%1+16*3]
    movq        mm1, mm0 ; tg_3_16
    movq        mm5, [%1+16*5]
    pmulhw      mm0, mm3 ; x3*(tg_3_16-1)
    movq        mm4, [tan1]
    pmulhw      mm1, mm5 ; x5*(tg_3_16-1)
    movq        mm7, [%1+16*7]
    movq        mm2, mm4 ; tg_1_16
    movq        mm6, [%1+16*1]
    pmulhw      mm4, mm7 ; x7*tg_1_16
    paddsw      mm0, mm3 ; x3*tg_3_16
    pmulhw      mm2, mm6 ; x1*tg_1_16
    paddsw      mm1, mm3 ; x3+x5*(tg_3_16-1)
    psubsw      mm0, mm5 ; x3*tg_3_16-x5 = tm35
    movq        mm3, [sqrt2]
    paddsw      mm1, mm5 ; x3+x5*tg_3_16 = tp35
    paddsw      mm4, mm6 ; x1+tg_1_16*x7 = tp17
    psubsw      mm2, mm7 ; x1*tg_1_16-x7 = tm17
    movq        mm5, mm4 ; tp17
    movq        mm6, mm2 ; tm17
    paddsw      mm5, mm1 ; tp17+tp35 = b0
    psubsw      mm6, mm0 ; tm17-tm35 = b3
    psubsw      mm4, mm1 ; tp17-tp35 = t1
    paddsw      mm2, mm0 ; tm17+tm35 = t2
    movq        mm7, [tan2]
    movq        mm1, mm4 ; t1
    movq  [%1+3*16], mm5 ; save b0
    paddsw      mm1, mm2 ; t1+t2
    movq  [%1+5*16], mm6 ; save b3
    psubsw      mm4, mm2 ; t1-t2
    movq        mm5, [%1+2*16]
    movq        mm0, mm7 ; tg_2_16
    movq        mm6, [%1+6*16]
    pmulhw      mm0, mm5 ; x2*tg_2_16
    pmulhw      mm7, mm6 ; x6*tg_2_16
    pmulhw      mm1, mm3 ; ocos_4_16*(t1+t2) = b1/2
    movq        mm2, [%1+0*16]
    pmulhw      mm4, mm3 ; ocos_4_16*(t1-t2) = b2/2
    psubsw      mm0, mm6 ; t2*tg_2_16-x6 = tm26
    movq        mm3, mm2 ; x0
    movq        mm6, [%1+4*16]
    paddsw      mm7, mm5 ; x2+x6*tg_2_16 = tp26
    paddsw      mm2, mm6 ; x0+x4 = tp04
    psubsw      mm3, mm6 ; x0-x4 = tm04
    movq        mm5, mm2 ; tp04
    movq        mm6, mm3 ; tm04
    psubsw      mm2, mm7 ; tp04-tp26 = a3
    paddsw      mm3, mm0 ; tm04+tm26 = a1
    paddsw      mm1, mm1 ; b1
    paddsw      mm4, mm4 ; b2
    paddsw      mm5, mm7 ; tp04+tp26 = a0
    psubsw      mm6, mm0 ; tm04-tm26 = a2
    movq        mm7, mm3 ; a1
    movq        mm0, mm6 ; a2
    paddsw      mm3, mm1 ; a1+b1
    paddsw      mm6, mm4 ; a2+b2
    psraw       mm3, 6   ; dst1
    psubsw      mm7, mm1 ; a1-b1
    psraw       mm6, 6   ; dst2
    psubsw      mm0, mm4 ; a2-b2
    movq        mm1, [%1+3*16] ; load b0
    psraw       mm7, 6   ; dst6
    movq        mm4, mm5 ; a0
    psraw       mm0, 6   ; dst5
    movq  [%1+1*16], mm3
    paddsw      mm5, mm1 ; a0+b0
    movq  [%1+2*16], mm6
    psubsw      mm4, mm1 ; a0-b0
    movq        mm3, [%1+5*16] ; load b3
    psraw       mm5, 6   ; dst0
    movq        mm6, mm2 ; a3
    psraw       mm4, 6   ; dst7
    movq  [%1+5*16], mm0
    paddsw      mm2, mm3 ; a3+b3
    movq  [%1+6*16], mm7
    psubsw      mm6, mm3 ; a3-b3
    movq  [%1+0*16], mm5
    psraw       mm2, 6   ; dst3
    movq  [%1+7*16], mm4
    psraw       mm6, 6   ; dst4
    movq  [%1+3*16], mm2
    movq  [%1+4*16], mm6
%endmacro

%macro XVID_IDCT_MMX 0
cglobal xvid_idct, 1, 1, 0, block
%if cpuflag(mmxext)
%define TAB tab_i_04_xmm
%else
%define TAB tab_i_04_mmx
%endif
    ; Process each row - beware of rounder offset
    DCT_8_INV_ROW  0, TAB + 64 * 0, 0*16
    DCT_8_INV_ROW  1, TAB + 64 * 1, 1*16
    DCT_8_INV_ROW  2, TAB + 64 * 2, 2*16
    DCT_8_INV_ROW  3, TAB + 64 * 3, 3*16
    DCT_8_INV_ROW  4, TAB + 64 * 0, 6*16
    DCT_8_INV_ROW  5, TAB + 64 * 3, 4*16
    DCT_8_INV_ROW  6, TAB + 64 * 2, 5*16
    DCT_8_INV_ROW  7, TAB + 64 * 1, 5*16

    ; Process the columns (4 at a time)
    DCT_8_INV_COL  r0+0
    DCT_8_INV_COL  r0+8

    RET
%endmacro

INIT_MMX mmx
XVID_IDCT_MMX
INIT_MMX mmxext
XVID_IDCT_MMX

%endif ; ~ARCH_X86_32
