;************************************************************************
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
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"

%if ARCH_X86_64

SECTION_RODATA 32

; Full matrix for row transform.
const tmatrix_row
    dw  64,  89,  84,  75,  64,  50,  35,  18
    dw  64, -18, -84,  50,  64, -75, -35,  89
    dw  64,  75,  35, -18, -64, -89, -84, -50
    dw  64, -50, -35,  89, -64, -18,  84, -75
    dw  64,  50, -35, -89, -64,  18,  84,  75
    dw  64, -75,  35,  18, -64,  89, -84,  50
    dw  64,  18, -84, -50,  64,  75, -35, -89
    dw  64, -89,  84, -75,  64, -50,  35, -18

; Constant pairs for broadcast in column transform.
const tmatrix_col_even
    dw  64,  64,  64, -64
    dw  84,  35,  35, -84
const tmatrix_col_odd
    dw  89,  75,  50,  18
    dw  75, -18, -89, -50
    dw  50, -89,  18,  75
    dw  18, -50,  75, -89

; Memory targets for vpbroadcastd (register version requires AVX512).
cextern pd_1
cextern pd_64

SECTION .text

; void ff_apv_decode_transquant_avx2(void *output,
;                                    ptrdiff_t pitch,
;                                    const int16_t *input,
;                                    const int16_t *qmatrix,
;                                    int bit_depth,
;                                    int qp_shift);

INIT_YMM avx2

cglobal apv_decode_transquant, 5, 7, 16, output, pitch, input, qmatrix, bit_depth, qp_shift, tmp

    ; Load input and dequantise

    vpbroadcastd  m10, [pd_1]
    lea       tmpd, [bit_depthd - 2]
    movd      xm8, qp_shiftm
    movd      xm9, tmpd
    vpslld    m10, m10, xm9
    vpsrld    m10, m10, 1

    ; m8  = scalar qp_shift
    ; m9  = scalar bd_shift
    ; m10 = vector 1 << (bd_shift - 1)
    ; m11 = qmatrix load

%macro LOAD_AND_DEQUANT 2 ; (xmm input, constant offset)
    vpmovsxwd m%1, [inputq   + %2]
    vpmovsxwd m11, [qmatrixq + %2]
    vpmaddwd  m%1, m%1, m11
    vpslld    m%1, m%1, xm8
    vpaddd    m%1, m%1, m10
    vpsrad    m%1, m%1, xm9
    vpackssdw m%1, m%1, m%1
%endmacro

    LOAD_AND_DEQUANT 0, 0x00
    LOAD_AND_DEQUANT 1, 0x10
    LOAD_AND_DEQUANT 2, 0x20
    LOAD_AND_DEQUANT 3, 0x30
    LOAD_AND_DEQUANT 4, 0x40
    LOAD_AND_DEQUANT 5, 0x50
    LOAD_AND_DEQUANT 6, 0x60
    LOAD_AND_DEQUANT 7, 0x70

    ; mN = row N words 0 1 2 3 0 1 2 3 4 5 6 7 4 5 6 7

    ; Transform columns
    ; This applies a 1-D DCT butterfly

    vpunpcklwd  m12, m0,  m4
    vpunpcklwd  m13, m2,  m6
    vpunpcklwd  m14, m1,  m3
    vpunpcklwd  m15, m5,  m7

    ; m12 = rows 0 and 4 interleaved
    ; m13 = rows 2 and 6 interleaved
    ; m14 = rows 1 and 3 interleaved
    ; m15 = rows 5 and 7 interleaved

    lea         tmpq, [tmatrix_col_even]
    vpbroadcastd   m0, [tmpq + 0x00]
    vpbroadcastd   m1, [tmpq + 0x04]
    vpbroadcastd   m2, [tmpq + 0x08]
    vpbroadcastd   m3, [tmpq + 0x0c]

    vpmaddwd  m4,  m12, m0
    vpmaddwd  m5,  m12, m1
    vpmaddwd  m6,  m13, m2
    vpmaddwd  m7,  m13, m3
    vpaddd    m8,  m4,  m6
    vpaddd    m9,  m5,  m7
    vpsubd    m10, m5,  m7
    vpsubd    m11, m4,  m6

    lea         tmpq, [tmatrix_col_odd]
    vpbroadcastd   m0, [tmpq + 0x00]
    vpbroadcastd   m1, [tmpq + 0x04]
    vpbroadcastd   m2, [tmpq + 0x08]
    vpbroadcastd   m3, [tmpq + 0x0c]

    vpmaddwd  m4,  m14, m0
    vpmaddwd  m5,  m15, m1
    vpmaddwd  m6,  m14, m2
    vpmaddwd  m7,  m15, m3
    vpaddd    m12, m4,  m5
    vpaddd    m13, m6,  m7

    vpbroadcastd   m0, [tmpq + 0x10]
    vpbroadcastd   m1, [tmpq + 0x14]
    vpbroadcastd   m2, [tmpq + 0x18]
    vpbroadcastd   m3, [tmpq + 0x1c]

    vpmaddwd  m4,  m14, m0
    vpmaddwd  m5,  m15, m1
    vpmaddwd  m6,  m14, m2
    vpmaddwd  m7,  m15, m3
    vpaddd    m14, m4,  m5
    vpaddd    m15, m6,  m7

    vpaddd    m0,  m8,  m12
    vpaddd    m1,  m9,  m13
    vpaddd    m2,  m10, m14
    vpaddd    m3,  m11, m15
    vpsubd    m4,  m11, m15
    vpsubd    m5,  m10, m14
    vpsubd    m6,  m9,  m13
    vpsubd    m7,  m8,  m12

    ; Mid-transform normalisation
    ; Note that outputs here are fitted to 16 bits

    vpbroadcastd  m8, [pd_64]

%macro NORMALISE 1
    vpaddd    m%1, m%1, m8
    vpsrad    m%1, m%1, 7
    vpackssdw m%1, m%1, m%1
    vpermq    m%1, m%1, q3120
%endmacro

    NORMALISE 0
    NORMALISE 1
    NORMALISE 2
    NORMALISE 3
    NORMALISE 4
    NORMALISE 5
    NORMALISE 6
    NORMALISE 7

    ; mN = row N words 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7

    ; Transform rows
    ; This multiplies the rows directly by the transform matrix,
    ; avoiding the need to transpose anything

    lea       tmpq, [tmatrix_row]
    mova      m12, [tmpq + 0x00]
    mova      m13, [tmpq + 0x20]
    mova      m14, [tmpq + 0x40]
    mova      m15, [tmpq + 0x60]

%macro TRANS_ROW_STEP 1
    vpmaddwd  m8,  m%1, m12
    vpmaddwd  m9,  m%1, m13
    vpmaddwd  m10, m%1, m14
    vpmaddwd  m11, m%1, m15
    vphaddd   m8,  m8,  m9
    vphaddd   m10, m10, m11
    vphaddd   m%1, m8,  m10
%endmacro

    TRANS_ROW_STEP 0
    TRANS_ROW_STEP 1
    TRANS_ROW_STEP 2
    TRANS_ROW_STEP 3
    TRANS_ROW_STEP 4
    TRANS_ROW_STEP 5
    TRANS_ROW_STEP 6
    TRANS_ROW_STEP 7

    ; Renormalise, clip and store output

    vpbroadcastd  m14, [pd_1]
    mov       tmpd, 20
    sub       tmpd, bit_depthd
    movd      xm9, tmpd
    dec       tmpd
    movd      xm13, tmpd
    movd      xm15, bit_depthd
    vpslld    m8,  m14, xm13
    vpslld    m12, m14, xm15
    vpsrld    m10, m12, 1
    vpsubd    m12, m12, m14
    vpxor     m11, m11, m11

    ; m8  = vector 1 << (bd_shift - 1)
    ; m9  = scalar bd_shift
    ; m10 = vector 1 << (bit_depth - 1)
    ; m11 = zero
    ; m12 = vector (1 << bit_depth) - 1

    cmp       bit_depthd, 8
    jne       store_10

    lea       tmpq, [pitchq + 2*pitchq]
%macro NORMALISE_AND_STORE_8 4
    vpaddd    m%1, m%1, m8
    vpaddd    m%2, m%2, m8
    vpaddd    m%3, m%3, m8
    vpaddd    m%4, m%4, m8
    vpsrad    m%1, m%1, xm9
    vpsrad    m%2, m%2, xm9
    vpsrad    m%3, m%3, xm9
    vpsrad    m%4, m%4, xm9
    vpaddd    m%1, m%1, m10
    vpaddd    m%2, m%2, m10
    vpaddd    m%3, m%3, m10
    vpaddd    m%4, m%4, m10
    ; m%1 = A0-3 A4-7
    ; m%2 = B0-3 B4-7
    ; m%3 = C0-3 C4-7
    ; m%4 = D0-3 D4-7
    vpackusdw m%1, m%1, m%2
    vpackusdw m%3, m%3, m%4
    ; m%1 = A0-3 B0-3 A4-7 B4-7
    ; m%2 = C0-3 D0-3 C4-7 D4-7
    vpermq    m%1, m%1, q3120
    vpermq    m%2, m%3, q3120
    ; m%1 = A0-3 A4-7 B0-3 B4-7
    ; m%2 = C0-3 C4-7 D0-3 D4-7
    vpackuswb m%1, m%1, m%2
    ; m%1 = A0-3 A4-7 C0-3 C4-7 B0-3 B4-7 D0-3 D4-7
    vextracti128  xm%2, m%1, 1
    vmovq     [outputq],            xm%1
    vmovq     [outputq + pitchq],   xm%2
    vpextrq   [outputq + 2*pitchq], xm%1, 1
    vpextrq   [outputq + tmpq],     xm%2, 1
    lea       outputq, [outputq + 4*pitchq]
%endmacro

    NORMALISE_AND_STORE_8 0, 1, 2, 3
    NORMALISE_AND_STORE_8 4, 5, 6, 7

    RET

store_10:

%macro NORMALISE_AND_STORE_10 2
    vpaddd    m%1, m%1, m8
    vpaddd    m%2, m%2, m8
    vpsrad    m%1, m%1, xm9
    vpsrad    m%2, m%2, xm9
    vpaddd    m%1, m%1, m10
    vpaddd    m%2, m%2, m10
    vpmaxsd   m%1, m%1, m11
    vpmaxsd   m%2, m%2, m11
    vpminsd   m%1, m%1, m12
    vpminsd   m%2, m%2, m12
    ; m%1 = A0-3 A4-7
    ; m%2 = B0-3 B4-7
    vpackusdw m%1, m%1, m%2
    ; m%1 = A0-3 B0-3 A4-7 B4-7
    vpermq    m%1, m%1, q3120
    ; m%1 = A0-3 A4-7 B0-3 B4-7
    mova      [outputq], xm%1
    vextracti128  [outputq + pitchq], m%1, 1
    lea       outputq, [outputq + 2*pitchq]
%endmacro

    NORMALISE_AND_STORE_10 0, 1
    NORMALISE_AND_STORE_10 2, 3
    NORMALISE_AND_STORE_10 4, 5
    NORMALISE_AND_STORE_10 6, 7

    RET

%endif ; ARCH_X86_64
