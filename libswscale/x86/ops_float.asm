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
%define SWS_FILTER_SCALE (1 << 14)

SECTION_RODATA

align 32
bias16: times 16 dw 0x8000 ; shift unsigned to signed range
bias32: times  8 dd 0x8000 * SWS_FILTER_SCALE
scale_inv: times 8 dd 0x38800000 ; 1.0f / SWS_FILTER_SCALE

; block_size = mmsize * 2 / sizeof(float)  (two grouped registers)
%macro get_block_size 0
    %define block_size (mmsize >> 1)
%if mmsize == 64
    %define block_shift 5
%elif mmsize == 32
    %define block_shift 4
%elif mmsize == 16
    %define block_shift 3
%elif mmsize == 8
    %define block_shift 2
%else
    %error "Unsupported mmsize"
%endif
%endmacro

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

;---------------------------------------------------------
; Arithmetic operations

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

;---------------------------------------------------------
; Dithering

%macro dither0 0
op dither0
        ; constant offset for all channels
        vbroadcastss m8, [implq + SwsOpImpl.priv]
        LOAD_CONT tmp0q
IF X,   addps mx, m8
IF Y,   addps my, m8
IF Z,   addps mz, m8
IF W,   addps mw, m8
IF X,   addps mx2, m8
IF Y,   addps my2, m8
IF Z,   addps mz2, m8
IF W,   addps mw2, m8
        CONTINUE tmp0q
%endmacro

%macro dither_row 5 ; size_log2, comp_idx, matrix, out, out2
        mov tmp0w, [implq + SwsOpImpl.priv + (4 + %2) * 2] ; priv.u16[4 + i]
        ; test is tmp0w < 0
        test tmp0w, tmp0w
        js .skip%2
%if %1 == 1
        vbroadcastsd m8, [%3 + tmp0q]
        addps %4, m8
        addps %5, m8
%elif %1 == 2
        VBROADCASTI128 m8, [%3 + tmp0q]
        addps %4, m8
        addps %5, m8
%else
        addps %4, [%3 + tmp0q]
        addps %5, [%3 + tmp0q + mmsize * ((4 << %1) > mmsize)]
%endif
.skip%2:
%endmacro

%macro dither 1 ; size_log2
op dither%1
        ; dither matrix is stored indirectly at the private data address
        mov tmp1q, [implq + SwsOpImpl.priv]
        ; add y offset. note that for 2x2, we would only need to look at the
        ; sign of `y`, but this special case is ignored for simplicity reasons
        ; (and because the current upstream format code never generates matrices
        ; that small)
        mov tmp0d, yd
        and tmp0d, (1 << %1) - 1
        shl tmp0d, %1 + 2 ; * sizeof(float)
        add tmp1q, tmp0q
    %if (4 << %1) > 2 * mmsize
        ; need to add in x offset
        mov tmp0d, bxd
        shl tmp0d, 6 ; sizeof(float[16])
        and tmp0d, (4 << %1) - 1
        add tmp1q, tmp0q
    %endif
        dither_row %1, 0, tmp1q, mx, mx2
        dither_row %1, 1, tmp1q, my, my2
        dither_row %1, 2, tmp1q, mz, mz2
        dither_row %1, 3, tmp1q, mw, mw2
        CONTINUE
%endmacro

%macro dither_fns 0
        decl_common_patterns dither0
        dither 1
        dither 2
        dither 3
        dither 4
        dither 5
        dither 6
        dither 7
        dither 8
%endmacro

;---------------------------------------------------------
; Linear transformations

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
    IF NOP(0) && COL(4), addps %1, %2 ; first vector was not reused
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

;---------------------------------------------------------
; Filtering / scaling

%macro floadU8 2 ; dst, src
        pmovzxbd  %1, %2
        vcvtdq2ps %1, %1
%endmacro

%macro floadU16 2 ; dst, src
        pmovzxwd  %1, %2
        vcvtdq2ps %1, %1
%endmacro

%macro floadF32 2 ; dst, src
        movu %1, %2
%endmacro

%macro fmaccum 4 ; variant, dst, srcA, srcB
%ifidn %1, none
        mulps %2, %3, %4
%elifidn %1, fma_v
        fmaddps %2, %3, %4, %2
%else
        mulps %3, %4
        addps %2, %3
%endif
%endmacro

%macro filter_v_iter 4 ; elems, type, sizeof_type, variant
            vbroadcastss m12, [weights]
            fload%2 m8,  [in0q]
IF %1 > 1,  fload%2 m9,  [in1q]
IF %1 > 2,  fload%2 m10, [in2q]
IF %1 > 3,  fload%2 m11, [in3q]
            fmaccum %4, mx, m8,  m12
IF %1 > 1,  fmaccum %4, my, m9,  m12
IF %1 > 2,  fmaccum %4, mz, m10, m12
IF %1 > 3,  fmaccum %4, mw, m11, m12
            fload%2 m8,  [in0q + (mmsize >> 2) * %3]
IF %1 > 1,  fload%2 m9,  [in1q + (mmsize >> 2) * %3]
IF %1 > 2,  fload%2 m10, [in2q + (mmsize >> 2) * %3]
IF %1 > 3,  fload%2 m11, [in3q + (mmsize >> 2) * %3]
            fmaccum %4, mx2, m8,  m12
IF %1 > 1,  fmaccum %4, my2, m9,  m12
IF %1 > 2,  fmaccum %4, mz2, m10, m12
IF %1 > 3,  fmaccum %4, mw2, m11, m12
%endmacro

%macro filter_v 4 ; elems, type, sizeof_type, variant
op filter_%4%1_%2
%xdefine weights tmp0q
%xdefine fltsize tmp1q
            mov weights, [implq + SwsOpImpl.priv]     ; float *weights
            mov fltsize, [implq + SwsOpImpl.priv + 8] ; size_t filter_size
            ; weights += filter_size * y * sizeof(float)
            mov tmp2q, fltsize
            imul tmp2q, yq
            lea weights, [weights + 4 * tmp2q]
            filter_v_iter %1, %2, %3, none
            dec fltsize
            jz .done
            push in0q
IF %1 > 1,  push in1q
IF %1 > 2,  push in2q
IF %1 > 3,  push in3q
.loop:
            add in0q, [execq + SwsOpExec.in_stride0]
IF %1 > 1,  add in1q, [execq + SwsOpExec.in_stride1]
IF %1 > 2,  add in2q, [execq + SwsOpExec.in_stride2]
IF %1 > 3,  add in3q, [execq + SwsOpExec.in_stride3]
            add weights, 4
            filter_v_iter %1, %2, %3, %4
            dec fltsize
            jnz .loop
IF %1 > 3,  pop in3q
IF %1 > 2,  pop in2q
IF %1 > 1,  pop in1q
            pop in0q
.done:
            LOAD_CONT tmp0q
IF %1 > 3,  add in3q, (mmsize >> 1) * %3
IF %1 > 2,  add in2q, (mmsize >> 1) * %3
IF %1 > 1,  add in1q, (mmsize >> 1) * %3
            add in0q, (mmsize >> 1) * %3
            CONTINUE tmp0q
%undef weights
%undef fltsize
%endmacro

%macro filter_h_iter_U8 4 ; acc, acc2, src, first
        pcmpeqb m12, m12
        pcmpeqb m13, m13
        vpgatherdd m8, [%3 + m14], m12 ; { ABCD | EFGH } 4 pixel per word
        vpgatherdd m9, [%3 + m15], m13 ; { IJKL | MNOP }
        ; unpack 4 bytes into separate 16-bit integer registers
        punpckhbw m10, m8, m12 ; { CCDD | GGHH } 2 pixels per word
        punpcklbw m8,  m8, m12 ; { AABB | EEFF }
        punpckhbw m11, m9, m12 ; { KKLL | OOPP }
        punpcklbw m9,  m9, m12 ; { IIJJ | MMNN }
        pmaddwd m8,  [weights]
        pmaddwd m10, [weights + mmsize]
        pmaddwd m9,  [weights + mmsize * 2]
        pmaddwd m11, [weights + mmsize * 3]
%if %4
        phaddd %1, m8, m10 ; { ABCD | EFGH }
        phaddd %2, m9, m11 ; { IJKL | MNOP }
%else
        phaddd m8, m10
        phaddd m9, m11
        paddd %1, m8
        paddd %2, m9
%endif
%endmacro

%macro filter_h_iter_U16 4 ; acc, acc2, src, first
        pcmpeqb m12, m12
        pcmpeqb m13, m13
        vpgatherdd m8, [%3 + m14], m12
        vpgatherdd m9, [%3 + m15], m13
        psubw m8, m10
        psubw m9, m10
%if %4
        pmaddwd %1, m8, [weights]
        pmaddwd %2, m9, [weights + mmsize]
%else
        pmaddwd m8, [weights]
        pmaddwd m9, [weights + mmsize]
        paddd %1, m8
        paddd %2, m9
%endif
%endmacro

%macro filter_h_iter_F32 4 ; acc, acc2, src, first
        pcmpeqb m12, m12
        pcmpeqb m13, m13
        vpgatherdd m8, [%3 + m14], m12
        vpgatherdd m9, [%3 + m15], m13
%if %4
        mulps %1, m8, [weights]
        mulps %2, m9, [weights + mmsize]
%else
        mulps m8, [weights]
        mulps m9, [weights + mmsize]
        addps %1, m8
        addps %2, m9
%endif
%endmacro

%macro filter_h 4 ; elems, type, sizeof_type, sizeof_weight
op filter_h%1_%2
%xdefine weights tmp0q
%xdefine fltsize tmp1q
            mov tmp0q,   [execq + SwsOpExec.in_offset_x]
            mov fltsize, [implq + SwsOpImpl.priv + 8] ; size_t filter_size
            get_block_size
            mov tmp2d, bxd
            shl tmp2q, block_shift         ; x := bx * block_size
            movu m14, [tmp0q + 4 * tmp2q]  ; &exec->in_offset_x[x]
            movu m15, [tmp0q + 4 * tmp2q + mmsize]
            mov weights, [implq + SwsOpImpl.priv]
%ifidn %2, U16
            mova m10, [bias16]
            mova m11, [bias32]
%endif
            imul tmp2q, fltsize
            lea weights, [weights + tmp2q * %4] ; weights += x * filter_size
            filter_h_iter_%2 mx, mx2, in0q, 1
IF1 %1 > 1, filter_h_iter_%2 my, my2, in1q, 1
IF1 %1 > 2, filter_h_iter_%2 mz, mz2, in2q, 1
IF1 %1 > 3, filter_h_iter_%2 mw, mw2, in3q, 1
            sub fltsize, 4 / %3
            jz .done
            push in0q
IF %1 > 1,  push in1q
IF %1 > 2,  push in2q
IF %1 > 3,  push in3q
.loop:
            add in0q, 4
IF %1 > 1,  add in1q, 4
IF %1 > 2,  add in2q, 4
IF %1 > 3,  add in3q, 4
            add weights, mmsize * 2 * (%4 / %3)
            filter_h_iter_%2 mx, mx2, in0q, 0
IF1 %1 > 1, filter_h_iter_%2 my, my2, in1q, 0
IF1 %1 > 2, filter_h_iter_%2 mz, mz2, in2q, 0
IF1 %1 > 3, filter_h_iter_%2 mw, mw2, in3q, 0
            sub fltsize, 4 / %3
            jnz .loop
IF %1 > 3,  pop in3q
IF %1 > 2,  pop in2q
IF %1 > 1,  pop in1q
            pop in0q
.done:
%ifidn %2, U16
            paddd mx, m11
IF %1 > 1,  paddd my, m11
IF %1 > 2,  paddd mz, m11
IF %1 > 3,  paddd mw, m11
            paddd mx2, m11
IF %1 > 1,  paddd my2, m11
IF %1 > 2,  paddd mz2, m11
IF %1 > 3,  paddd mw2, m11
%endif
%ifnidn %2, F32
            vcvtdq2ps mx, mx
IF %1 > 1,  vcvtdq2ps my, my
IF %1 > 2,  vcvtdq2ps mz, mz
IF %1 > 3,  vcvtdq2ps mw, mw
            vcvtdq2ps mx2, mx2
IF %1 > 1,  vcvtdq2ps my2, my2
IF %1 > 2,  vcvtdq2ps mz2, mz2
IF %1 > 3,  vcvtdq2ps mw2, mw2
%endif
            mova m12, [scale_inv]
            LOAD_CONT tmp0q
            mulps mx, m12
IF %1 > 1,  mulps my, m12
IF %1 > 2,  mulps mz, m12
IF %1 > 3,  mulps mw, m12
            mulps mx2, m12
IF %1 > 1,  mulps my2, m12
IF %1 > 2,  mulps mz2, m12
IF %1 > 3,  mulps mw2, m12
            CONTINUE tmp0q
%undef weights
%undef fltsize
%endmacro

%macro iloadU8 2 ; dst, src
        pmovzxbw %1, %2
%endmacro

%macro iloadU16 2 ; dst, src
        movu %1, %2
        psubw %1, [bias16] ; shift into signed I16 range
%endmacro

%macro iloadF32 2 ; dst, src
        movu %1, %2
%endmacro

; filter 4 adjacent pixels at the same time
%macro filter_h4 4 ; dst, src, type, sizeof_type
%ifidn %3, F32
    %xdefine MUL mulps
    %xdefine ADD addps
%else
    %xdefine MUL pmaddwd
    %xdefine ADD paddd
%endif
            iload%3 xm8,  [%2 + offset0q] ; {a0, a1, a2, a3}
            iload%3 xm9,  [%2 + offset1q] ; {b0, b1, b2, b3}
            iload%3 xm10, [%2 + offset2q] ; {c0, c1, c2, c3}
            iload%3 xm11, [%2 + offset3q] ; {d0, d1, d2, d3}
            MUL xm8,  [weights]
            MUL xm9,  [weights + 16]
            MUL xm10, [weights + 32]
            MUL xm11, [weights + 48]
            mov bxq, fltsize
            sub bxq, 64
            jz %%done
            push weights
            push %2
%%loop:
            add weights, 64
%ifidn %3, F32
            add %2, 4 * %4
%else
            add %2, 8 * %4
%endif
            iload%3 xm14, [%2 + offset0q]
            iload%3 xm15, [%2 + offset1q]
            MUL xm14, [weights]
            MUL xm15, [weights + 16]
            ADD xm8, xm14
            ADD xm9, xm15
            iload%3 xm14, [%2 + offset2q]
            iload%3 xm15, [%2 + offset3q]
            MUL xm14, [weights + 32]
            MUL xm15, [weights + 48]
            ADD xm10, xm14
            ADD xm11, xm15
            sub bxq, 64
            jnz %%loop
            pop %2
            pop weights
%%done:
            ; 4x4 transpose (on XMM size)
            punpckhdq  xm15, xm8,  xm9  ; {a2, b2, a3, b3}
            punpckldq  xm8,  xm9        ; {a0, b0, a1, b1}
            punpckhdq  xm9,  xm10, xm11 ; {c2, d2, c3, d3}
            punpckldq  xm10, xm11       ; {c0, d0, c1, d1}
            punpckhqdq xm11, xm8,  xm10 ; {a1, b1, c1, d1}
            punpcklqdq xm8,  xm10       ; {a0, b0, c0, d0}
            punpckhqdq xm10, xm15, xm9  ; {a3, b3, c3, d3}
            punpcklqdq xm15, xm9        ; {a2, b2, c2, d2}
            ADD xm8,  xm11 ; sum all even terms
            ADD xm15, xm10 ; sum all odd terms
            ADD %1, xm8, xm15
%undef MUL
%undef ADD
%endmacro

; filter low and high lines separately and combine results for each plane
%macro filter_h8 4-5 ; elems, type, sizeof_type, offsets, dst_suffix
            movsxd offset0q, dword [%4 +  0]
            movsxd offset1q, dword [%4 +  4]
            movsxd offset2q, dword [%4 +  8]
            movsxd offset3q, dword [%4 + 12]
            filter_h4 xmx%5, in0q, %2, %3
IF %1 > 1,  filter_h4 xmy%5, in1q, %2, %3
IF %1 > 2,  filter_h4 xmz%5, in2q, %2, %3
IF %1 > 3,  filter_h4 xmw%5, in3q, %2, %3
            add weights, fltsize
            movsxd offset0q, dword [%4 + 16]
            movsxd offset1q, dword [%4 + 20]
            movsxd offset2q, dword [%4 + 24]
            movsxd offset3q, dword [%4 + 28]
            filter_h4 xm12, in0q, %2, %3
IF %1 > 1,  filter_h4 xm13, in1q, %2, %3
            vinsertf128 mx%5, mx%5, xmm12, 1
IF %1 > 1,  vinsertf128 my%5, my%5, xmm13, 1
IF %1 > 2,  filter_h4 xm12, in2q, %2, %3
IF %1 > 3,  filter_h4 xm13, in3q, %2, %3
IF %1 > 2,  vinsertf128 mz%5, mz%5, xmm12, 1
IF %1 > 3,  vinsertf128 mw%5, mw%5, xmm13, 1
%ifidn %2, U16
            mova m15, [bias32]
            paddd mx%5, m15
IF %1 > 1,  paddd my%5, m15
IF %1 > 2,  paddd mz%5, m15
IF %1 > 3,  paddd mw%5, m15
%endif
%ifnidn %2, F32
            vcvtdq2ps mx%5, mx%5
IF %1 > 1,  vcvtdq2ps my%5, my%5
IF %1 > 2,  vcvtdq2ps mz%5, mz%5
IF %1 > 3,  vcvtdq2ps mw%5, mw%5
%endif
%endmacro

%macro filter_4x4_h 3 ; elems, type, sizeof_type
op filter_4x4_h%1_%2
%xdefine offset0q out0q
%xdefine offset1q out1q
%xdefine offset2q out2q
%xdefine offset3q out3q
%xdefine weights  tmp0q
%xdefine offsets  tmp2q
%xdefine fltsize  tmp1q
            ; reserve some registers for the inner loops
            push bxq
            push offset0q
            push offset1q
            push offset2q
            push offset3q
            get_block_size
            shl bxq, block_shift ; x := bx * block_size
            mov weights, [implq + SwsOpImpl.priv]        ; int16_t *weights
            mov tmp1d,   [implq + SwsOpImpl.priv + 8]    ; size_t filter_size
            mov offsets, [execq + SwsOpExec.in_offset_x] ; int32_t *offsets
            lea offsets, [offsets + 4 * bxq] ; offsets += x * sizeof(int32_t)
            imul bxq, fltsize
            add weights, bxq ; weights += x * filter_size
            shl fltsize, 2   ; fltsize *= 4 (number of pixels / iter)
            filter_h8 %1, %2, %3, offsets
            add weights, fltsize
            filter_h8 %1, %2, %3, offsets + 32, 2
            mova m10, [scale_inv]
            mulps mx, m10
IF %1 > 1,  mulps my, m10
IF %1 > 2,  mulps mz, m10
IF %1 > 3,  mulps mw, m10
            mulps mx2, m10
IF %1 > 1,  mulps my2, m10
IF %1 > 2,  mulps mz2, m10
IF %1 > 3,  mulps mw2, m10
            pop offset3q
            pop offset2q
            pop offset1q
            pop offset0q
            pop bxq
            CONTINUE
%undef offset0q
%undef offset1q
%undef offset2q
%undef offset3q
%undef weights
%undef offsets
%undef fltsize
%endmacro

%macro generic_filter_fns 3 ; type, sizeof_type, sizeof_weight
        filter_v 1, %1, %2, v
        filter_v 2, %1, %2, v
        filter_v 3, %1, %2, v
        filter_v 4, %1, %2, v

        filter_v 1, %1, %2, fma_v
        filter_v 2, %1, %2, fma_v
        filter_v 3, %1, %2, fma_v
        filter_v 4, %1, %2, fma_v

        filter_h 1, %1, %2, %3
        filter_h 2, %1, %2, %3
        filter_h 3, %1, %2, %3
        filter_h 4, %1, %2, %3

        filter_4x4_h 1, %1, %2
        filter_4x4_h 2, %1, %2
        filter_4x4_h 3, %1, %2
        filter_4x4_h 4, %1, %2
%endmacro

%macro filter_fns 0
    generic_filter_fns U8,  1, 2
    generic_filter_fns U16, 2, 2
    generic_filter_fns F32, 4, 4
%endmacro

INIT_YMM avx2
decl_common_patterns conv8to32f
decl_common_patterns conv16to32f
decl_common_patterns conv32fto8
decl_common_patterns conv32fto16
decl_common_patterns min_max
decl_common_patterns scale
dither_fns
linear_fns
filter_fns
