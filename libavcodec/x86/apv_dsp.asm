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

; Constant pairs for the 1-D transform butterfly, expanded so that they
; can be used directly as memory operands.
tmatrix_even_a: times 8 dw  64,  64
tmatrix_even_b: times 8 dw  64, -64
tmatrix_even_c: times 8 dw  84,  35
tmatrix_even_d: times 8 dw  35, -84
tmatrix_odd_0a: times 8 dw  89,  50
tmatrix_odd_0b: times 8 dw  75,  18
tmatrix_odd_1a: times 8 dw  75, -89
tmatrix_odd_1b: times 8 dw -18, -50
tmatrix_odd_2a: times 8 dw  50,  18
tmatrix_odd_2b: times 8 dw -89,  75
tmatrix_odd_3a: times 8 dw  18,  75
tmatrix_odd_3b: times 8 dw -50, -89

cextern pd_1
cextern pd_64
vec_1_19: times 8 dd 1 << 19

SECTION .text

; void ff_apv_decode_transquant_avx2(void *output,
;                                    ptrdiff_t pitch,
;                                    const int16_t *input,
;                                    const int16_t *qmatrix,
;                                    int bit_depth,
;                                    int qp_shift);

INIT_YMM avx2

; One pass of the 1-D DCT, as a butterfly.
; 16-bit input rows in m12-m15 as pairs 04 26 15 37.
; 32-bit output rows in m8-m15.
; The rounding constant is already added to the output.

%macro TRANSFORM_PASS 1 ; (rounding constant)
    ; Even part, from rows 0, 2, 4 and 6.
    vpmaddwd  m8,  m12, [tmatrix_even_a]
    vpmaddwd  m9,  m12, [tmatrix_even_b]
    vpmaddwd  m10, m13, [tmatrix_even_c]
    vpmaddwd  m11, m13, [tmatrix_even_d]
    vpaddd    m8,  m8,  %1
    vpaddd    m9,  m9,  %1
    vpaddd    m0,  m8,  m10
    vpaddd    m1,  m9,  m11
    vpsubd    m2,  m9,  m11
    vpsubd    m3,  m8,  m10

    ; Odd part, from rows 1, 3, 5 and 7.
    vpmaddwd  m8,  m14, [tmatrix_odd_0a]
    vpmaddwd  m9,  m15, [tmatrix_odd_0b]
    vpmaddwd  m10, m14, [tmatrix_odd_1a]
    vpmaddwd  m11, m15, [tmatrix_odd_1b]
    vpaddd    m4,  m8,  m9
    vpaddd    m5,  m10, m11
    vpmaddwd  m8,  m14, [tmatrix_odd_2a]
    vpmaddwd  m9,  m15, [tmatrix_odd_2b]
    vpmaddwd  m10, m14, [tmatrix_odd_3a]
    vpmaddwd  m11, m15, [tmatrix_odd_3b]
    vpaddd    m6,  m8,  m9
    vpaddd    m7,  m10, m11

    ; Combine.
    vpaddd    m8,  m0,  m4
    vpaddd    m9,  m1,  m5
    vpaddd    m10, m2,  m6
    vpaddd    m11, m3,  m7
    vpsubd    m12, m3,  m7
    vpsubd    m13, m2,  m6
    vpsubd    m14, m1,  m5
    vpsubd    m15, m0,  m4
%endmacro

cglobal apv_decode_transquant, 5, 7, 16, 32, output, pitch, input, qmatrix, bit_depth, qp_shift, tmp

    ; Building rounding constant to for the final normalisation and
    ; store it on the stack for use later.

    mova      m0,  [pd_1]
    mov       tmpd, 19
    sub       tmpd, bit_depthd
    movd      xm1, tmpd
    vpslld    m0,  m0,  xm1
    vpaddd    m0,  m0,  [vec_1_19]
    mova      [rsp], m0

    ; Load input and dequantise

    mova      m10, [pd_1]
    lea       tmpd, [bit_depthd - 2]
    movd      xm8, qp_shiftm
    movd      xm9, tmpd
    vpslld    m10, m10, xm9
    vpsrld    m10, m10, 1

    ; m8  = scalar qp_shift
    ; m9  = scalar bd_shift
    ; m10 = vector 1 << (bd_shift - 1)
    ; m11 = qmatrix load

%macro LOAD_AND_DEQUANT 2 ; (register, constant offset)
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
    ; Interleave pairs of rows for the butterfly input.

    vpunpcklwd m12, m0,  m4
    vpunpcklwd m13, m2,  m6
    vpunpcklwd m14, m1,  m5
    vpunpcklwd m15, m3,  m7

    ; Transform columns.

    TRANSFORM_PASS [pd_64]

    ; Mid-transform normalisation.  The rounding constant was already
    ; added by the transform pass; outputs here are fitted to 16 bits.

    vpsrad    m8,  m8,  7
    vpsrad    m9,  m9,  7
    vpsrad    m10, m10, 7
    vpsrad    m11, m11, 7
    vpsrad    m12, m12, 7
    vpsrad    m13, m13, 7
    vpsrad    m14, m14, 7
    vpsrad    m15, m15, 7

    ; Transpose, recombining into the paired rows for the butterfly.

    vpackssdw m0,  m8,  m9
    vpackssdw m1,  m10, m11
    vpackssdw m2,  m12, m13
    vpackssdw m3,  m14, m15

    vperm2i128 m4, m0, m2, 0x20
    vperm2i128 m5, m1, m3, 0x20
    vperm2i128 m6, m0, m2, 0x31
    vperm2i128 m7, m1, m3, 0x31

    vpslld    m8,  m6,  16
    vpslld    m9,  m7,  16
    vpsrld    m10, m4,  16
    vpsrld    m11, m5,  16
    vpblendw  m8,  m4,  m8,  0xaa
    vpblendw  m9,  m5,  m9,  0xaa
    vpblendw  m10, m10, m6,  0xaa
    vpblendw  m11, m11, m7,  0xaa

    vshufps   m12, m8,  m9,  q2020
    vshufps   m13, m8,  m9,  q3131
    vshufps   m14, m10, m11, q2020
    vshufps   m15, m10, m11, q3131

    ; Transform rows.

    TRANSFORM_PASS [rsp]

    ; Renormalise, clip and store output.  The rounding constant was
    ; already added by the transform pass, and here we split the shift
    ; on either side of the pack (20 - bit_depth -> 4 and 16 - bit_depth)
    ; so that the saturation precisely captures the clip we want.

    mov       tmpd, 16
    sub       tmpd, bit_depthd
    movd      xm0, tmpd

    vpsrad    m8,  m8,  4
    vpsrad    m9,  m9,  4
    vpsrad    m10, m10, 4
    vpsrad    m11, m11, 4
    vpsrad    m12, m12, 4
    vpsrad    m13, m13, 4
    vpsrad    m14, m14, 4
    vpsrad    m15, m15, 4

    vpackusdw m3,  m8,  m12
    vpackusdw m4,  m9,  m13
    vpackusdw m5,  m10, m14
    vpackusdw m6,  m11, m15

    vpsrlw    m3,  m3,  xm0
    vpsrlw    m4,  m4,  xm0
    vpsrlw    m5,  m5,  xm0
    vpsrlw    m6,  m6,  xm0

    ; Transpose back for output.

    vpunpcklwd m7,  m3, m4
    vpunpckhwd m8,  m3, m4
    vpunpcklwd m9,  m5, m6
    vpunpckhwd m10, m5, m6

    vpunpckldq m11, m7, m9
    vpunpckhdq m12, m7, m9
    vpunpckldq m13, m8, m10
    vpunpckhdq m14, m8, m10

    vpunpcklqdq m0, m11, m13
    vpunpckhqdq m1, m11, m13
    vpunpcklqdq m2, m12, m14
    vpunpckhqdq m3, m12, m14

    ; mN = rows N and N+4

    ; Indexing from outputq reaches rows 0, 1, 2 and 4; indexing from a
    ; second base at row 7 with the pitch negated reaches rows 7, 6, 5
    ; and 3.  The negation means every store relative to outputq has to
    ; come first.

    lea       tmpq, [outputq + 8*pitchq]
    sub       tmpq, pitchq

    mova          [outputq],              xm0     ; row 0
    mova          [outputq +   pitchq],   xm1     ; row 1
    mova          [outputq + 2*pitchq],   xm2     ; row 2
    vextracti128  [outputq + 4*pitchq],   m0, 1   ; row 4

    neg       pitchq

    mova          [tmpq    + 4*pitchq],   xm3     ; row 3
    vextracti128  [tmpq    + 2*pitchq],   m1, 1   ; row 5
    vextracti128  [tmpq    +   pitchq],   m2, 1   ; row 6
    vextracti128  [tmpq],                 m3, 1   ; row 7

    RET

%endif ; ARCH_X86_64
