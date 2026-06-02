;******************************************************************************
;* Copyright (c) 2025-2026 Niklas Haas
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

%include "ops_include.asm"

%assign SWS_FILTER_SCALE (1 << 14)
%define FILTER_WIDTH (BLOCK_SIZE / 4)       ; always outputs F32
%define FILTER_SIZE  (FILTER_WIDTH * BYTES) ; in source bytes

SECTION_RODATA

align 32
bias16: times 16 dw 0x8000 ; shift unsigned to signed range

align 16
bias32:    times 4 dd 0x8000 * SWS_FILTER_SCALE
scale_inv: times 4 dd 0x38800000 ; 1.0f / SWS_FILTER_SCALE

SECTION .text

;---------------------------------------------------------
; Generic vertical filtering (add+mul and fma)

%macro FLOAD_SWS_PIXEL_U8 2 ; dst, src
        pmovzxbd  %1, %2
        vcvtdq2ps %1, %1
%endmacro

%macro FLOAD_SWS_PIXEL_U16 2 ; dst, src
        pmovzxwd  %1, %2
        vcvtdq2ps %1, %1
%endmacro

%macro FLOAD_SWS_PIXEL_U32 2 ; dst, src
        %error TODO, no clean way to convert U32 to F32 on AVX2
%endmacro

%macro FLOAD_SWS_PIXEL_F32 2 ; dst, src
        movu %1, %2
%endmacro

%macro ACCUM_MULADD 3 ; sum, src, weight
        mulps %2, %3
        addps %1, %2
%endmacro

%macro ACCUM_FMA 3 ; sum, src, weight
        fmaddps %1, %2, %3, %1
%endmacro

%macro filter_v_iter 1 ; accum_fn
        vbroadcastss m12, [weights]
IF X,   FLOAD_PX m8,  [in0q]
IF Y,   FLOAD_PX m9,  [in1q]
IF Z,   FLOAD_PX m10, [in2q]
IF W,   FLOAD_PX m11, [in3q]
IF X,   %1 mx, m8,  m12
IF Y,   %1 my, m9,  m12
IF Z,   %1 mz, m10, m12
IF W,   %1 mw, m11, m12
IF X,   FLOAD_PX m8,  [in0q + (FILTER_SIZE >> 1)]
IF Y,   FLOAD_PX m9,  [in1q + (FILTER_SIZE >> 1)]
IF Z,   FLOAD_PX m10, [in2q + (FILTER_SIZE >> 1)]
IF W,   FLOAD_PX m11, [in3q + (FILTER_SIZE >> 1)]
IF X,   %1 mx2, m8,  m12
IF Y,   %1 my2, m9,  m12
IF Z,   %1 mz2, m10, m12
IF W,   %1 mw2, m11, m12
%endmacro

%macro read_planar_fv 1 ; accum_fn
%xdefine FLOAD_PX FLOAD_ %+ TYPE
%xdefine weights tmp0q
%xdefine fltsize tmp1q

assert_idn FILTER_TYPE, SWS_PIXEL_F32
%assign SIZEOF_WEIGHT 4

        mov weights, [implq + SwsOpImpl.priv]     ; float *weights
        mov fltsize, [implq + SwsOpImpl.priv + 8] ; size_t filter_size
        ; weights += filter_size * y * sizeof(float)
        mov tmp2q, fltsize
        imul tmp2q, yq
        lea weights, [weights + SIZEOF_WEIGHT * tmp2q]
        filter_v_iter mulps
        dec fltsize
        jz .done
IF X,   push in0q
IF Y,   push in1q
IF Z,   push in2q
IF W,   push in3q
.loop:
IF X,   add in0q, [execq + SwsOpExec.in_stride0]
IF Y,   add in1q, [execq + SwsOpExec.in_stride1]
IF Z,   add in2q, [execq + SwsOpExec.in_stride2]
IF W,   add in3q, [execq + SwsOpExec.in_stride3]
        add weights, SIZEOF_WEIGHT
        filter_v_iter %1
        dec fltsize
        jnz .loop
IF W,   pop in3q
IF Z,   pop in2q
IF Y,   pop in1q
IF X,   pop in0q
.done:
        LOAD_CONT tmp0q
IF W,   add in3q, FILTER_SIZE
IF Z,   add in2q, FILTER_SIZE
IF Y,   add in1q, FILTER_SIZE
IF X,   add in0q, FILTER_SIZE
        CONTINUE tmp0q
%undef weights
%undef fltsize
%endmacro

%macro READ_PLANAR_FV 1
%xdefine FILTER_TYPE %1
    read_planar_fv ACCUM_MULADD
%endmacro

%macro READ_PLANAR_FV_FMA 1
%xdefine FILTER_TYPE %1
    read_planar_fv ACCUM_FMA
%endmacro

;---------------------------------------------------------
; Generic horizontal filtering (vpgatherdd)

%macro FILTER_H_SWS_PIXEL_U8 4 ; acc, acc2, src, first
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

%macro FILTER_H_SWS_PIXEL_U16 4 ; acc, acc2, src, first
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

%macro FILTER_H_SWS_PIXEL_F32 4 ; acc, acc2, src, first
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

%macro READ_PLANAR_FH 1
%xdefine FILTER_TYPE %1
%xdefine weights tmp0q
%xdefine fltsize tmp1q
%xdefine FILTER_H_PX FILTER_H_ %+ TYPE

assert_idn FILTER_TYPE, SWS_PIXEL_F32
%ifidn TYPE, SWS_PIXEL_F32
    %assign SIZEOF_WEIGHT 4 ; F32
%else
    %assign SIZEOF_WEIGHT 2 ; I16
%endif

        mov tmp0q,   [execq + SwsOpExec.in_offset_x]
        mov fltsize, [implq + SwsOpImpl.priv + 8] ; size_t filter_size
        mov tmp2d, bxd
        shl_log2 tmp2q, FILTER_WIDTH   ; x := bx * FILTER_WIDTH
        movu m14, [tmp0q + 4 * tmp2q]  ; &exec->in_offset_x[x]
        movu m15, [tmp0q + 4 * tmp2q + mmsize]
        mov weights, [implq + SwsOpImpl.priv]
    %ifidn TYPE, SWS_PIXEL_U16
        VBROADCASTI128 m10, [bias16]
        VBROADCASTI128 m11, [bias32]
    %endif
        imul tmp2q, fltsize
        lea weights, [weights + tmp2q * SIZEOF_WEIGHT] ; weights += x * filter_size
IF X,   FILTER_H_PX mx, mx2, in0q, 1
IF Y,   FILTER_H_PX my, my2, in1q, 1
IF Z,   FILTER_H_PX mz, mz2, in2q, 1
IF W,   FILTER_H_PX mw, mw2, in3q, 1
        sub fltsize, 4 / BYTES
        jz .done
IF X,   push in0q
IF Y,   push in1q
IF Z,   push in2q
IF W,   push in3q
.loop:
IF X,   add in0q, 4
IF Y,   add in1q, 4
IF Z,   add in2q, 4
IF W,   add in3q, 4
        add weights, BLOCK_WIDTH * SIZEOF_WEIGHT
IF X,   FILTER_H_PX mx, mx2, in0q, 0
IF Y,   FILTER_H_PX my, my2, in1q, 0
IF Z,   FILTER_H_PX mz, mz2, in2q, 0
IF W,   FILTER_H_PX mw, mw2, in3q, 0
        sub fltsize, 4 / BYTES
        jnz .loop
IF W,   pop in3q
IF Z,   pop in2q
IF Y,   pop in1q
IF X,   pop in0q
.done:
    %ifidn TYPE, SWS_PIXEL_U16
IF X,   paddd mx, m11
IF Y,   paddd my, m11
IF Z,   paddd mz, m11
IF W,   paddd mw, m11
IF X,   paddd mx2, m11
IF Y,   paddd my2, m11
IF Z,   paddd mz2, m11
IF W,   paddd mw2, m11
    %endif
    %ifnidn TYPE, SWS_PIXEL_F32
IF X,   vcvtdq2ps mx, mx
IF Y,   vcvtdq2ps my, my
IF Z,   vcvtdq2ps mz, mz
IF W,   vcvtdq2ps mw, mw
IF X,   vcvtdq2ps mx2, mx2
IF Y,   vcvtdq2ps my2, my2
IF Z,   vcvtdq2ps mz2, mz2
IF W,   vcvtdq2ps mw2, mw2
    %endif
        VBROADCASTI128 m12, [scale_inv]
        LOAD_CONT tmp0q
IF X,   mulps mx, m12
IF Y,   mulps my, m12
IF Z,   mulps mz, m12
IF W,   mulps mw, m12
IF X,   mulps mx2, m12
IF Y,   mulps my2, m12
IF Z,   mulps mz2, m12
IF W,   mulps mw2, m12
        CONTINUE tmp0q
%undef weights
%undef fltsize
%endmacro

;---------------------------------------------------------
; 4x4 transposed horizontal filtering

%macro ILOAD_SWS_PIXEL_U8 2 ; dst, src
        pmovzxbw %1, %2
%endmacro

%macro ILOAD_SWS_PIXEL_U16 2 ; dst, src
        movu %1, %2
        psubw %1, [bias16] ; shift into signed I16 range
%endmacro

%macro ILOAD_SWS_PIXEL_F32 2 ; dst, src
        movu %1, %2
%endmacro

; filter 4 adjacent pixels at the same time
%macro filter_h4_4x4 2 ; dst, src
%ifidn TYPE, SWS_PIXEL_F32
    %xdefine MUL mulps
    %xdefine ADD addps
%else
    %xdefine MUL pmaddwd
    %xdefine ADD paddd
%endif

        ILOAD_PX xm8,  [%2 + offset0q] ; {a0, a1, a2, a3}
        ILOAD_PX xm9,  [%2 + offset1q] ; {b0, b1, b2, b3}
        ILOAD_PX xm10, [%2 + offset2q] ; {c0, c1, c2, c3}
        ILOAD_PX xm11, [%2 + offset3q] ; {d0, d1, d2, d3}
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
        add %2, (16 / SIZEOF_WEIGHT) * BYTES ; pixels per xmm reg
        ILOAD_PX xm14, [%2 + offset0q]
        ILOAD_PX xm15, [%2 + offset1q]
        MUL xm14, [weights]
        MUL xm15, [weights + 16]
        ADD xm8, xm14
        ADD xm9, xm15
        ILOAD_PX xm14, [%2 + offset2q]
        ILOAD_PX xm15, [%2 + offset3q]
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

; filter low and high lanes separately and combine results for each plane
%macro filter_h8_4x4 1-2 ; offsets, dst_suffix
        movsxd offset0q, dword [%1 +  0]
        movsxd offset1q, dword [%1 +  4]
        movsxd offset2q, dword [%1 +  8]
        movsxd offset3q, dword [%1 + 12]
IF X,   filter_h4_4x4 xmx%2, in0q
IF Y,   filter_h4_4x4 xmy%2, in1q
IF Z,   filter_h4_4x4 xmz%2, in2q
IF W,   filter_h4_4x4 xmw%2, in3q
        add weights, fltsize
        movsxd offset0q, dword [%1 + 16]
        movsxd offset1q, dword [%1 + 20]
        movsxd offset2q, dword [%1 + 24]
        movsxd offset3q, dword [%1 + 28]
IF X,   filter_h4_4x4 xm12, in0q
IF Y,   filter_h4_4x4 xm13, in1q
IF X,   vinsertf128 mx%2, mx%2, xmm12, 1
IF Y,   vinsertf128 my%2, my%2, xmm13, 1
IF Z,   filter_h4_4x4 xm12, in2q
IF W,   filter_h4_4x4 xm13, in3q
IF Z,   vinsertf128 mz%2, mz%2, xmm12, 1
IF W,   vinsertf128 mw%2, mw%2, xmm13, 1
    %ifidn TYPE, SWS_PIXEL_U16
        VBROADCASTI128 m15, [bias32]
IF X,   paddd mx%2, m15
IF Y,   paddd my%2, m15
IF Z,   paddd mz%2, m15
IF W,   paddd mw%2, m15
    %endif
    %ifnidn TYPE, SWS_PIXEL_F32
IF X,   vcvtdq2ps mx%2, mx%2
IF Y,   vcvtdq2ps my%2, my%2
IF Z,   vcvtdq2ps mz%2, mz%2
IF W,   vcvtdq2ps mw%2, mw%2
    %endif
%endmacro

%macro READ_PLANAR_FH_4X4 1
%xdefine FILTER_TYPE %1
%xdefine offset0q out0q
%xdefine offset1q out1q
%xdefine offset2q out2q
%xdefine offset3q out3q
%xdefine weights  tmp0q
%xdefine offsets  tmp2q
%xdefine fltsize  tmp1q
%xdefine ILOAD_PX ILOAD_ %+ TYPE

assert_idn FILTER_TYPE, SWS_PIXEL_F32
%ifidn TYPE, SWS_PIXEL_F32
    %assign SIZEOF_WEIGHT 4 ; F32
%else
    %assign SIZEOF_WEIGHT 2 ; I16
%endif

        ; reserve some registers for the inner loops
        push bxq
        push offset0q
        push offset1q
        push offset2q
        push offset3q
        shl_log2 bxq, FILTER_WIDTH ; x := bx * FILTER_WIDTH
        mov weights, [implq + SwsOpImpl.priv]        ; int16_t *weights
        mov tmp1d,   [implq + SwsOpImpl.priv + 8]    ; size_t filter_size
        mov offsets, [execq + SwsOpExec.in_offset_x] ; int32_t *offsets
        lea offsets, [offsets + 4 * bxq] ; offsets += x * sizeof(int32_t)
        imul bxq, fltsize
        add weights, bxq ; weights += x * filter_size
        shl fltsize, 2   ; fltsize *= 4 (number of pixels / iter)
        filter_h8_4x4 offsets
        add weights, fltsize
        filter_h8_4x4 offsets + 32, 2
        VBROADCASTI128 m10, [scale_inv]
        pop offset3q
        pop offset2q
        pop offset1q
        pop offset0q
        pop bxq
        LOAD_CONT tmp0q
IF X,   mulps mx, m10
IF Y,   mulps my, m10
IF Z,   mulps mz, m10
IF W,   mulps mw, m10
IF X,   mulps mx2, m10
IF Y,   mulps my2, m10
IF Z,   mulps mz2, m10
IF W,   mulps mw2, m10
        CONTINUE tmp0q
%undef offset0q
%undef offset1q
%undef offset2q
%undef offset3q
%undef weights
%undef offsets
%undef fltsize
%endmacro

;---------------------------------------------------------
; Arithmetic operations

%macro SCALE 0
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

%macro ADD 0
IF X,   vbroadcastss m8,  [implq + SwsOpImpl.priv + 0 * BYTES]
IF Y,   vbroadcastss m9,  [implq + SwsOpImpl.priv + 1 * BYTES]
IF Z,   vbroadcastss m10, [implq + SwsOpImpl.priv + 2 * BYTES]
IF W,   vbroadcastss m11, [implq + SwsOpImpl.priv + 3 * BYTES]
        LOAD_CONT tmp0q
IF X,   addps mx, m8
IF Y,   addps my, m9
IF Z,   addps mz, m10
IF W,   addps mw, m11
IF X,   addps mx2, m8
IF Y,   addps my2, m9
IF Z,   addps mz2, m10
IF W,   addps mw2, m11
        CONTINUE tmp0q
%endmacro

%macro MIN 0
IF X,   vbroadcastss m8,  [implq + SwsOpImpl.priv + 0 * BYTES]
IF Y,   vbroadcastss m9,  [implq + SwsOpImpl.priv + 1 * BYTES]
IF Z,   vbroadcastss m10, [implq + SwsOpImpl.priv + 2 * BYTES]
IF W,   vbroadcastss m11, [implq + SwsOpImpl.priv + 3 * BYTES]
        LOAD_CONT tmp0q
IF X,   minps mx, m8
IF Y,   minps my, m9
IF Z,   minps mz, m10
IF W,   minps mw, m11
IF X,   minps mx2, m8
IF Y,   minps my2, m9
IF Z,   minps mz2, m10
IF W,   minps mw2, m11
        CONTINUE tmp0q
%endmacro

%macro MAX 0
IF X,   vbroadcastss m8,  [implq + SwsOpImpl.priv + 0 * BYTES]
IF Y,   vbroadcastss m9,  [implq + SwsOpImpl.priv + 1 * BYTES]
IF Z,   vbroadcastss m10, [implq + SwsOpImpl.priv + 2 * BYTES]
IF W,   vbroadcastss m11, [implq + SwsOpImpl.priv + 3 * BYTES]
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

;---------------------------------------------------------
; Linear operations

%define LIN_MASK(I, J) (1 << (5 * (I) + (J)))

%macro linear_muladd 5 ; dst, src, use_coef, coef, use_fma
    %if INIT ; dst is already initialized
        %if %3 && %5
            fmaddps %1, %4, %2, %1
        %elif %3
            mulps %4, %2
            addps %1, %4
        %else
            addps %1, %2
        %endif
    %else
        %assign INIT 1
        %if %3
            mulps %1, %4, %2
        %else
            mova %1, %2
        %endif
    %endif
%endmacro

%macro linear_row 3-4 ; dst, src, row, suffix
%xdefine NEED(J) (!(ZERO_MASK & LIN_MASK(%3, J)))
%xdefine LOAD(J) (NEED(J) && !(ONE_MASK & LIN_MASK(%3, J)))
%xdefine FMA(J)  (EXACT_MASK & LIN_MASK(%3, J))
%assign INIT 0 ; track whether `dst` already contains data

    %if !(ZERO_MASK & LIN_MASK(%3, 4)) ; nonzero output offset
            %assign INIT 1
            vbroadcastss %1,  [%2 + 4 * BYTES]
    %endif
IF LOAD(0), vbroadcastss m12, [%2 + 0 * BYTES]
IF LOAD(1), vbroadcastss m13, [%2 + 1 * BYTES]
IF LOAD(2), vbroadcastss m14, [%2 + 2 * BYTES]
IF LOAD(3), vbroadcastss m15, [%2 + 3 * BYTES]
IF NEED(0), linear_muladd %1, mx%4, LOAD(0), m12, FMA(0)
IF NEED(1), linear_muladd %1, my%4, LOAD(1), m13, FMA(1)
IF NEED(2), linear_muladd %1, mz%4, LOAD(2), m14, FMA(2)
IF NEED(3), linear_muladd %1, mw%4, LOAD(3), m15, FMA(3)
            assert INIT, SWS_UOP_LINEAR should not contain empty rows
%endmacro

%macro LINEAR_FMA 3
%assign ONE_MASK   %1
%assign ZERO_MASK  %2
%assign EXACT_MASK %3

        mov tmp0q, [implq + SwsOpImpl.priv] ; address of matrix
        LOAD_CONT tmp1q
IF1 X,  linear_row m8,  tmp0q +  0 * BYTES, 0
IF1 Y,  linear_row m9,  tmp0q +  5 * BYTES, 1
IF1 Z,  linear_row m10, tmp0q + 10 * BYTES, 2
IF1 W,  linear_row m11, tmp0q + 15 * BYTES, 3
IF X,   mova mx, m8
IF Y,   mova my, m9
IF Z,   mova mz, m10
IF W,   mova mw, m11
IF1 X,  linear_row m8,  tmp0q +  0 * BYTES, 0, 2
IF1 Y,  linear_row m9,  tmp0q +  5 * BYTES, 1, 2
IF1 Z,  linear_row m10, tmp0q + 10 * BYTES, 2, 2
IF1 W,  linear_row m11, tmp0q + 15 * BYTES, 3, 2
IF X,   mova mx2, m8
IF Y,   mova my2, m9
IF Z,   mova mz2, m10
IF W,   mova mw2, m11
        CONTINUE tmp1q
%endmacro

;---------------------------------------------------------
; Dithering

%macro VBROADCAST 3 ; size, dst, src
    %if %1 == 4
        vbroadcastss %2, %3
    %elif %1 == 8
        vbroadcastsd %2, %3
    %elif %1 == 16
        VBROADCASTF128 %2, %3
    %else
        mova %2, %3
    %endif
%endmacro

%macro DITHER 5 ; x_off, y_off, z_off, w_off, size_log2
%assign SIZE_LOG2 %5
%assign SIZE      (1 << SIZE_LOG2)
%assign STRIDE    (SIZE * BYTES)
        ; dither matrix is stored indirectly at the private data address
        mov tmp1q, [implq + SwsOpImpl.priv]
        ; add y offset. note that for 2x2, we would only need to look at the
        ; sign of `y`, but this special case is ignored for simplicity reasons
        ; (and because the current upstream format code never generates matrices
        ; that small)
        mov tmp0d, yd
        and tmp0d, SIZE - 1
        shl_log2 tmp0d, STRIDE
        add tmp1q, tmp0q
    %if STRIDE > BLOCK_SIZE
        ; need to add in x offset
        mov tmp0d, bxd
        shl tmp0d, BLOCK_SIZE
        and tmp0d, STRIDE - 1
        add tmp1q, tmp0q
    %endif
    %xdefine dither_addr_x (tmp1q + %1 * STRIDE)
    %xdefine dither_addr_y (tmp1q + %2 * STRIDE)
    %xdefine dither_addr_z (tmp1q + %3 * STRIDE)
    %xdefine dither_addr_w (tmp1q + %4 * STRIDE)
    %if STRIDE > mmsize
        LOAD_CONT tmp0q
IF X,   addps mx,  [dither_addr_x]
IF Y,   addps my,  [dither_addr_y]
IF Z,   addps mz,  [dither_addr_z]
IF W,   addps mw,  [dither_addr_w]
IF X,   addps mx2, [dither_addr_x + mmsize]
IF Y,   addps my2, [dither_addr_y + mmsize]
IF Z,   addps mz2, [dither_addr_z + mmsize]
IF W,   addps mw2, [dither_addr_w + mmsize]
    %else
IF X,   VBROADCAST STRIDE, m8,  [dither_addr_x]
IF Y,   VBROADCAST STRIDE, m9,  [dither_addr_y]
IF Z,   VBROADCAST STRIDE, m10, [dither_addr_z]
IF W,   VBROADCAST STRIDE, m11, [dither_addr_w]
        LOAD_CONT tmp0q
IF X,   addps mx,  m8
IF Y,   addps my,  m9
IF Z,   addps mz,  m10
IF W,   addps mw,  m11
IF X,   addps mx2, m8
IF Y,   addps my2, m9
IF Z,   addps mz2, m10
IF W,   addps mw2, m11
    %endif
        CONTINUE tmp0q
%endmacro

;---------------------------------------------------------
; Instantiate above macros to generate all uop kernels

%macro decl_filter_ops 1 ; type
    decl_suffix _4x4,   DECL_%1_READ_PLANAR_FH (READ_PLANAR_FH_4X4)

    DECL_%1_READ_PLANAR_FH      (READ_PLANAR_FH)
    DECL_%1_READ_PLANAR_FV      (READ_PLANAR_FV)
    DECL_%1_READ_PLANAR_FV_FMA  (READ_PLANAR_FV_FMA)
%endmacro

%macro decl_float_ops 1 ; type
    DECL_%1_SCALE           (SCALE)
    DECL_%1_ADD             (ADD)
    DECL_%1_MIN             (MIN)
    DECL_%1_MAX             (MAX)
    DECL_%1_LINEAR_FMA      (LINEAR_FMA)
    DECL_%1_DITHER          (DITHER)
%endmacro

INIT_YMM avx2
%assign V2 1

decl_filter_ops U8
decl_filter_ops U16
decl_filter_ops F32
decl_float_ops  F32
