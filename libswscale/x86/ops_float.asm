;******************************************************************************
;* Copyright (c) 2025 Niklas Haas
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

%include "ops_common.asm"

SECTION .text

;---------------------------------------------------------
; Pixel type conversions

%macro conv8to32f 0
op convert_U8_F32
        LOAD_CONT tmp0q
IF X,   vpsrldq xmx2, xmx, 8
IF Y,   vpsrldq xmy2, xmy, 8
IF Z,   vpsrldq xmz2, xmz, 8
IF W,   vpsrldq xmw2, xmw, 8
IF X,   pmovzxbd mx, xmx
IF Y,   pmovzxbd my, xmy
IF Z,   pmovzxbd mz, xmz
IF W,   pmovzxbd mw, xmw
IF X,   pmovzxbd mx2, xmx2
IF Y,   pmovzxbd my2, xmy2
IF Z,   pmovzxbd mz2, xmz2
IF W,   pmovzxbd mw2, xmw2
IF X,   vcvtdq2ps mx, mx
IF Y,   vcvtdq2ps my, my
IF Z,   vcvtdq2ps mz, mz
IF W,   vcvtdq2ps mw, mw
IF X,   vcvtdq2ps mx2, mx2
IF Y,   vcvtdq2ps my2, my2
IF Z,   vcvtdq2ps mz2, mz2
IF W,   vcvtdq2ps mw2, mw2
        CONTINUE tmp0q
%endmacro

%macro conv16to32f 0
op convert_U16_F32
        LOAD_CONT tmp0q
IF X,   vextracti128 xmx2, mx, 1
IF Y,   vextracti128 xmy2, my, 1
IF Z,   vextracti128 xmz2, mz, 1
IF W,   vextracti128 xmw2, mw, 1
IF X,   pmovzxwd mx, xmx
IF Y,   pmovzxwd my, xmy
IF Z,   pmovzxwd mz, xmz
IF W,   pmovzxwd mw, xmw
IF X,   pmovzxwd mx2, xmx2
IF Y,   pmovzxwd my2, xmy2
IF Z,   pmovzxwd mz2, xmz2
IF W,   pmovzxwd mw2, xmw2
IF X,   vcvtdq2ps mx, mx
IF Y,   vcvtdq2ps my, my
IF Z,   vcvtdq2ps mz, mz
IF W,   vcvtdq2ps mw, mw
IF X,   vcvtdq2ps mx2, mx2
IF Y,   vcvtdq2ps my2, my2
IF Z,   vcvtdq2ps mz2, mz2
IF W,   vcvtdq2ps mw2, mw2
        CONTINUE tmp0q
%endmacro

%macro conv32fto8 0
op convert_F32_U8
        LOAD_CONT tmp0q
IF X,   cvttps2dq mx, mx
IF Y,   cvttps2dq my, my
IF Z,   cvttps2dq mz, mz
IF W,   cvttps2dq mw, mw
IF X,   cvttps2dq mx2, mx2
IF Y,   cvttps2dq my2, my2
IF Z,   cvttps2dq mz2, mz2
IF W,   cvttps2dq mw2, mw2
IF X,   packusdw mx, mx2
IF Y,   packusdw my, my2
IF Z,   packusdw mz, mz2
IF W,   packusdw mw, mw2
IF X,   vextracti128 xmx2, mx, 1
IF Y,   vextracti128 xmy2, my, 1
IF Z,   vextracti128 xmz2, mz, 1
IF W,   vextracti128 xmw2, mw, 1
        vzeroupper
IF X,   packuswb xmx, xmx2
IF Y,   packuswb xmy, xmy2
IF Z,   packuswb xmz, xmz2
IF W,   packuswb xmw, xmw2
IF X,   vpshufd xmx, xmx, q3120
IF Y,   vpshufd xmy, xmy, q3120
IF Z,   vpshufd xmz, xmz, q3120
IF W,   vpshufd xmw, xmw, q3120
        CONTINUE tmp0q
%endmacro

%macro conv32fto16 0
op convert_F32_U16
        LOAD_CONT tmp0q
IF X,   cvttps2dq mx, mx
IF Y,   cvttps2dq my, my
IF Z,   cvttps2dq mz, mz
IF W,   cvttps2dq mw, mw
IF X,   cvttps2dq mx2, mx2
IF Y,   cvttps2dq my2, my2
IF Z,   cvttps2dq mz2, mz2
IF W,   cvttps2dq mw2, mw2
IF X,   packusdw mx, mx2
IF Y,   packusdw my, my2
IF Z,   packusdw mz, mz2
IF W,   packusdw mw, mw2
IF X,   vpermq mx, mx, q3120
IF Y,   vpermq my, my, q3120
IF Z,   vpermq mz, mz, q3120
IF W,   vpermq mw, mw, q3120
        CONTINUE tmp0q
%endmacro

%macro min_max 0
op min
IF X,   vbroadcastss m8,  [implq + SwsOpImpl.priv + 0]
IF Y,   vbroadcastss m9,  [implq + SwsOpImpl.priv + 4]
IF Z,   vbroadcastss m10, [implq + SwsOpImpl.priv + 8]
IF W,   vbroadcastss m11, [implq + SwsOpImpl.priv + 12]
        LOAD_CONT tmp0q
IF X,   minps mx, mx, m8
IF Y,   minps my, my, m9
IF Z,   minps mz, mz, m10
IF W,   minps mw, mw, m11
IF X,   minps mx2, m8
IF Y,   minps my2, m9
IF Z,   minps mz2, m10
IF W,   minps mw2, m11
        CONTINUE tmp0q

op max
IF X,   vbroadcastss m8,  [implq + SwsOpImpl.priv + 0]
IF Y,   vbroadcastss m9,  [implq + SwsOpImpl.priv + 4]
IF Z,   vbroadcastss m10, [implq + SwsOpImpl.priv + 8]
IF W,   vbroadcastss m11, [implq + SwsOpImpl.priv + 12]
        LOAD_CONT tmp0q
IF X,   maxps mx, m8
IF Y,   maxps my, m9
IF Z,   maxps mz, m10
IF W,   maxps mw, m11
IF X,   maxps mx2, m8
IF Y,   maxps my2, m9
IF Z,   maxps mz2, m10
IF W,   maxps mw2, m11
        CONTINUE tmp0q
%endmacro

%macro scale 0
op scale
        vbroadcastss m8, [implq + SwsOpImpl.priv]
        LOAD_CONT tmp0q
IF X,   mulps mx, m8
IF Y,   mulps my, m8
IF Z,   mulps mz, m8
IF W,   mulps mw, m8
IF X,   mulps mx2, m8
IF Y,   mulps my2, m8
IF Z,   mulps mz2, m8
IF W,   mulps mw2, m8
        CONTINUE tmp0q
%endmacro

%macro load_dither_row 5 ; size_log2, y, addr, out, out2
        lea tmp0q, %2
        and tmp0q, (1 << %1) - 1
        shl tmp0q, %1+2
%if %1 == 2
        VBROADCASTI128 %4, [%3 + tmp0q]
%else
        mova %4, [%3 + tmp0q]
    %if (4 << %1) > mmsize
        mova %5, [%3 + tmp0q + mmsize]
    %endif
%endif
%endmacro

%macro dither 1 ; size_log2
op dither%1
        %define DX  m8
        %define DY  m9
        %define DZ  m10
        %define DW  m11
        %define DX2 DX
        %define DY2 DY
        %define DZ2 DZ
        %define DW2 DW
%if %1 == 0
        ; constant offset for all channels
        vbroadcastss DX, [implq + SwsOpImpl.priv]
        %define DY DX
        %define DZ DX
        %define DW DX
%elif %1 == 1
        ; 2x2 matrix, only sign of y matters
        mov tmp0d, yd
        and tmp0d, 1
        shl tmp0d, 3
    %if X || Z
        ; dither matrix is stored directly in the private data
        vbroadcastsd DX, [implq + SwsOpImpl.priv + tmp0q]
    %endif
    %if Y || W
        xor tmp0d, 8
        vbroadcastsd DY, [implq + SwsOpImpl.priv + tmp0q]
    %endif
        %define DZ DX
        %define DW DY
%else
        ; matrix is at least 4x4, load all four channels with custom offset
    %if (4 << %1) > mmsize
        %define DX2 m12
        %define DY2 m13
        %define DZ2 m14
        %define DW2 m15
    %endif
        ; dither matrix is stored indirectly at the private data address
        mov tmp1q, [implq + SwsOpImpl.priv]
    %if (4 << %1) > 2 * mmsize
        ; need to add in x offset
        mov tmp0d, bxd
        shl tmp0d, 6 ; sizeof(float[16])
        and tmp0d, (4 << %1) - 1
        add tmp1q, tmp0q
    %endif
IF X,   load_dither_row %1, [yd + 0], tmp1q, DX, DX2
IF Y,   load_dither_row %1, [yd + 3], tmp1q, DY, DY2
IF Z,   load_dither_row %1, [yd + 2], tmp1q, DZ, DZ2
IF W,   load_dither_row %1, [yd + 5], tmp1q, DW, DW2
%endif
        LOAD_CONT tmp0q
IF X,   addps mx, DX
IF Y,   addps my, DY
IF Z,   addps mz, DZ
IF W,   addps mw, DW
IF X,   addps mx2, DX2
IF Y,   addps my2, DY2
IF Z,   addps mz2, DZ2
IF W,   addps mw2, DW2
        CONTINUE tmp0q
%endmacro

%macro dither_fns 0
        dither 0
        dither 1
        dither 2
        dither 3
        dither 4
        dither 5
        dither 6
        dither 7
        dither 8
%endmacro

%xdefine MASK(I, J)  (1 << (5 * (I) + (J)))
%xdefine MASK_OFF(I) MASK(I, 4)
%xdefine MASK_ROW(I) (0x1F << (5 * (I)))
%xdefine MASK_COL(J) (0x8421 << J)
%xdefine MASK_ALL    (1 << 20) - 1
%xdefine MASK_LUMA   MASK(0, 0) | MASK_OFF(0)
%xdefine MASK_ALPHA  MASK(3, 3) | MASK_OFF(3)
%xdefine MASK_DIAG3  MASK(0, 0) | MASK(1, 1) | MASK(2, 2)
%xdefine MASK_OFF3   MASK_OFF(0) | MASK_OFF(1) | MASK_OFF(2)
%xdefine MASK_MAT3   MASK(0, 0) | MASK(0, 1) | MASK(0, 2) |\
                     MASK(1, 0) | MASK(1, 1) | MASK(1, 2) |\
                     MASK(2, 0) | MASK(2, 1) | MASK(2, 2)
%xdefine MASK_DIAG4  MASK_DIAG3 | MASK(3, 3)
%xdefine MASK_OFF4   MASK_OFF3 | MASK_OFF(3)
%xdefine MASK_MAT4   MASK_ALL & ~MASK_OFF4

%macro linear_row 7 ; res, x, y, z, w, row, mask
%define COL(J) ((%7) & MASK(%6, J)) ; true if mask contains component J
%define NOP(J) (J == %6 && !COL(J)) ; true if J is untouched input component

    ; load weights
    IF COL(0),  vbroadcastss m12,  [tmp0q + %6 * 20 + 0]
    IF COL(1),  vbroadcastss m13,  [tmp0q + %6 * 20 + 4]
    IF COL(2),  vbroadcastss m14,  [tmp0q + %6 * 20 + 8]
    IF COL(3),  vbroadcastss m15,  [tmp0q + %6 * 20 + 12]

    ; initialize result vector as appropriate
    %if COL(4) ; offset
        vbroadcastss %1, [tmp0q + %6 * 20 + 16]
    %elif NOP(0)
        ; directly reuse first component vector if possible
        mova %1, %2
    %else
        xorps %1, %1
    %endif

    IF COL(0),  mulps m12, %2
    IF COL(1),  mulps m13, %3
    IF COL(2),  mulps m14, %4
    IF COL(3),  mulps m15, %5
    IF COL(0),  addps %1, m12
    IF NOP(0) && COL(4), addps %1, %3 ; first vector was not reused
    IF COL(1),  addps %1, m13
    IF NOP(1),  addps %1, %3
    IF COL(2),  addps %1, m14
    IF NOP(2),  addps %1, %4
    IF COL(3),  addps %1, m15
    IF NOP(3),  addps %1, %5
%endmacro

%macro linear_inner 5 ; x, y, z, w, mask
    %define ROW(I) ((%5) & MASK_ROW(I))
    IF1 ROW(0), linear_row m8,  %1, %2, %3, %4, 0, %5
    IF1 ROW(1), linear_row m9,  %1, %2, %3, %4, 1, %5
    IF1 ROW(2), linear_row m10, %1, %2, %3, %4, 2, %5
    IF1 ROW(3), linear_row m11, %1, %2, %3, %4, 3, %5
    IF ROW(0),  mova %1, m8
    IF ROW(1),  mova %2, m9
    IF ROW(2),  mova %3, m10
    IF ROW(3),  mova %4, m11
%endmacro

%macro linear_mask 2 ; name, mask
op %1
        mov tmp0q, [implq + SwsOpImpl.priv] ; address of matrix
        linear_inner mx,  my,  mz,  mw,  %2
        linear_inner mx2, my2, mz2, mw2, %2
        CONTINUE
%endmacro

; specialized functions for very simple cases
%macro linear_dot3 0
op dot3
        mov tmp0q, [implq + SwsOpImpl.priv]
        vbroadcastss m12,  [tmp0q + 0]
        vbroadcastss m13,  [tmp0q + 4]
        vbroadcastss m14,  [tmp0q + 8]
        LOAD_CONT tmp0q
        mulps mx, m12
        mulps m8, my, m13
        mulps m9, mz, m14
        addps mx, m8
        addps mx, m9
        mulps mx2, m12
        mulps m10, my2, m13
        mulps m11, mz2, m14
        addps mx2, m10
        addps mx2, m11
        CONTINUE tmp0q
%endmacro

%macro linear_fns 0
        linear_dot3
        linear_mask luma,       MASK_LUMA
        linear_mask alpha,      MASK_ALPHA
        linear_mask lumalpha,   MASK_LUMA | MASK_ALPHA
        linear_mask row0,       MASK_ROW(0)
        linear_mask row0a,      MASK_ROW(0) | MASK_ALPHA
        linear_mask diag3,      MASK_DIAG3
        linear_mask diag4,      MASK_DIAG4
        linear_mask diagoff3,   MASK_DIAG3 | MASK_OFF3
        linear_mask matrix3,    MASK_MAT3
        linear_mask affine3,    MASK_MAT3 | MASK_OFF3
        linear_mask affine3a,   MASK_MAT3 | MASK_OFF3 | MASK_ALPHA
        linear_mask matrix4,    MASK_MAT4
        linear_mask affine4,    MASK_MAT4 | MASK_OFF4
%endmacro

INIT_YMM avx2
decl_common_patterns conv8to32f
decl_common_patterns conv16to32f
decl_common_patterns conv32fto8
decl_common_patterns conv32fto16
decl_common_patterns min_max
decl_common_patterns scale
decl_common_patterns dither_fns
linear_fns
