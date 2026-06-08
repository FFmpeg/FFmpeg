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

SECTION_RODATA

align 16
read8_unpack2:  db   0,  2,  4,  6,  8, 10, 12, 14,  1,  3,  5,  7,  9, 11, 13, 15
read8_unpack3:  db   0,  3,  6,  9,  1,  4,  7, 10,  2,  5,  8, 11, -1, -1, -1, -1
read8_unpack4:  db   0,  4,  8, 12,  1,  5,  9, 13,  2,  6, 10, 14,  3,  7, 11, 15
read16_unpack2: db   0,  1,  4,  5,  8,  9, 12, 13,  2,  3,  6,  7, 10, 11, 14, 15
read16_unpack3: db   0,  1,  6,  7,  2,  3,  8,  9,  4,  5, 10, 11, -1, -1, -1, -1
read16_unpack4: db   0,  1,  8,  9,  2,  3, 10, 11,  4,  5, 12, 13,  6,  7, 14, 15
write8_pack2:   db   0,  8,  1,  9,  2, 10,  3, 11,  4, 12,  5, 13,  6, 14,  7, 15
write8_pack3:   db   0,  4,  8,  1,  5,  9,  2,  6, 10,  3,  7, 11, -1, -1, -1, -1
write16_pack3:  db   0,  1,  4,  5,  8,  9,  2,  3,  6,  7, 10, 11, -1, -1, -1, -1
%define write8_pack4  read8_unpack4
%define write16_pack4 read16_unpack2
%define write16_pack2 read16_unpack4

bits_mask:      db 128, 64, 32, 16,  8,  4,  2,  1,128, 64, 32, 16,  8,  4,  2,  1
bits_reverse:   db   7,  6,  5,  4,  3,  2,  1,  0, 15, 14, 13, 12, 11, 10,  9,  8
nibbles_pack:   times 8 dw 0x0110

swap16:     db  1,  0,  3,  2,  5,  4,  7,  6,  9,  8, 11, 10, 13, 12, 15, 14
swap32:     db  3,  2,  1,  0,  7,  6,  5,  4, 11, 10,  9,  8, 15, 14, 13, 12
expand16:   db  0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12, 14, 14
expand32:   db  0, 0, 0, 0, 4, 4, 4, 4, 8, 8,  8,  8, 12, 12, 12, 12

align 32
bits_shuf: times 8 db 0
           times 8 db 1
           times 8 db 2
           times 8 db 3

mask1: times 32 db 0x01
mask2: times 32 db 0x03
mask3: times 32 db 0x07
mask4: times 32 db 0x0F

const1b  equ mask1
const1w: times 16 dw 0x1

SECTION .text

;---------------------------------------------------------
; Helper macros for BITS-dependent sized instructions

%macro bitfn 2+
    %if BITS == 8
        %1%+b %2
    %elif BITS == 16
        %1%+w %2
    %elif BITS == 32
        %1%+d %2
    %endif
%endmacro

%define psllx           bitfn psll,
%define psrlx           bitfn psrl,
%define pcmpeqx         bitfn pcmpeq,
%define pmullx          bitfn pmull,
%define vpbroadcastx    bitfn vpbroadcast,

;---------------------------------------------------------
; Planar reads / writes

%macro READ_PLANAR 0
IF X,   movu mx, [in0q]
IF Y,   movu my, [in1q]
IF Z,   movu mz, [in2q]
IF W,   movu mw, [in3q]
%if V2
IF X,   movu mx2, [in0q + mmsize]
IF Y,   movu my2, [in1q + mmsize]
IF Z,   movu mz2, [in2q + mmsize]
IF W,   movu mw2, [in3q + mmsize]
%endif
        LOAD_CONT tmp0q
IF X,   add in0q, BLOCK_SIZE
IF Y,   add in1q, BLOCK_SIZE
IF Z,   add in2q, BLOCK_SIZE
IF W,   add in3q, BLOCK_SIZE
        CONTINUE tmp0q
%endmacro

%macro READ_NIBBLE 0
assert COMPS == 1
        VBROADCASTI128 m12, [mask4]
        LOAD_CONT tmp0q
        pmovzxbw mx,  [in0q]
IF V2,  pmovzxbw mx2, [in0q + (mmsize >> 1)]
        add in0q, BLOCK_WIDTH >> 1
        psllw m8, mx, 8     ; { 00 AB 00 CD ... }
        psrlw mx, 4         ; { 0A 00 0C 00 ... }
        pand m8, m12        ; { 00 0B 00 0D ... }
        por mx, m8          ; { 0A 0B 0C 0D ... }
%if V2
        psllw m9, mx2, 8
        psrlw mx2, 4
        pand m9, m12
        por mx2, m9
%endif
        CONTINUE tmp0q
%endmacro

%macro WRITE_NIBBLE 0
assert COMPS == 1
        VBROADCASTI128 m12, [nibbles_pack]
        pmaddubsw mx, m12
        packuswb mx, mx
%if V2
        pmaddubsw mx2, m12
        packuswb mx2, mx2
%endif
%if cpuflag(avx2)
        vpermq mx, mx, q3120
IF V2,  vpermq mx2, mx2, q3120
%endif
        movu [out0q], xmx
IF V2,  movu [out0q + (mmsize >> 1)], xmx2
        add out0q, BLOCK_WIDTH >> 1
        RET
%endmacro

%macro READ_BIT 0
assert COMPS == 1
%if cpuflag(avx2)
        vpbroadcastd mx,  [in0q]
IF V2,  vpbroadcastd mx2, [in0q + 4]
%else
        movd mx, [in0q]
IF V2,  movd mx2, [in0q + 2]
%endif
        mova m8, [bits_shuf]
        VBROADCASTI128 m9,  [bits_mask]
        VBROADCASTI128 m10, [const1b]
        LOAD_CONT tmp0q
        add in0q, BLOCK_WIDTH >> 3
        pshufb mx,  m8
IF V2,  pshufb mx2, m8
        pand mx,  m9
IF V2,  pand mx2, m9
        pcmpeqb mx,  m9
IF V2,  pcmpeqb mx2, m9
        pand mx,  m10
IF V2,  pand mx2, m10
        CONTINUE tmp0q
%endmacro

%macro WRITE_BIT 0
assert COMPS == 1
        VBROADCASTI128 m8, [bits_reverse]
        psllw mx,  7
IF V2,  psllw mx2, 7
        pshufb mx,  m8
IF V2,  pshufb mx2, m8
        pmovmskb tmp0d, mx
IF V2,  pmovmskb tmp1d, mx2
%if mmsize >= 32
        mov [out0q], tmp0d
IF V2,  mov [out0q + (mmsize >> 3)], tmp1d
%else
        mov [out0q], tmp0w
IF V2,  mov [out0q + (mmsize >> 3)], tmp1w
%endif
        add out0q, BLOCK_WIDTH >> 3
        RET
%endmacro

%macro WRITE_PLANAR 0
IF X,   movu [out0q], mx
IF Y,   movu [out1q], my
IF Z,   movu [out2q], mz
IF W,   movu [out3q], mw
%if V2
IF X,   movu [out0q + mmsize], mx2
IF Y,   movu [out1q + mmsize], my2
IF Z,   movu [out2q + mmsize], mz2
IF W,   movu [out3q + mmsize], mw2
%endif
IF X,   add out0q, BLOCK_WIDTH
IF Y,   add out1q, BLOCK_WIDTH
IF Z,   add out2q, BLOCK_WIDTH
IF W,   add out3q, BLOCK_WIDTH
        RET
%endmacro

;---------------------------------------------------------
; Packed reads, packing and unpacking

%macro read_packed2 0
        movu m8,  [in0q + 0 * mmsize]
        movu m9,  [in0q + 1 * mmsize]
IF V2,  movu m10, [in0q + 2 * mmsize]
IF V2,  movu m11, [in0q + 3 * mmsize]
    %if BITS == 32
        shufps m8, m8, q3120
        shufps m9, m9, q3120
IF V2,  shufps m10, m10, q3120
IF V2,  shufps m11, m11, q3120
    %else
        pshufb m8, m12              ; { X0 Y0 | X1 Y1 }
        pshufb m9, m12              ; { X2 Y2 | X3 Y3 }
IF V2,  pshufb m10, m12
IF V2,  pshufb m11, m12
    %endif
        unpcklpd mx, m8, m9         ; { X0 X2 | X1 X3 }
        unpckhpd my, m8, m9         ; { Y0 Y2 | Y1 Y3 }
IF V2,  unpcklpd mx2, m10, m11
IF V2,  unpckhpd my2, m10, m11
    %if cpuflag(avx2)
        vpermq mx, mx, q3120       ; { X0 X1 | X2 X3 }
        vpermq my, my, q3120       ; { Y0 Y1 | Y2 Y3 }
IF V2,  vpermq mx2, mx2, q3120
IF V2,  vpermq my2, my2, q3120
    %endif
%endmacro

%macro read_packed34 5 ; x, y, z, w, addr
        movu xm8,  [%5 + 0  * COMPS]
        movu xm9,  [%5 + 4  * COMPS]
        movu xm10, [%5 + 8  * COMPS]
        movu xm11, [%5 + 12 * COMPS]
    %if cpuflag(avx2)
        vinserti128 m8,  m8,  [%5 + 16 * COMPS], 1
        vinserti128 m9,  m9,  [%5 + 20 * COMPS], 1
        vinserti128 m10, m10, [%5 + 24 * COMPS], 1
        vinserti128 m11, m11, [%5 + 28 * COMPS], 1
    %endif
    %if BITS == 32
        mova %1, m8
        mova %2, m9
        mova %3, m10
        mova %4, m11
    %else
        pshufb %1, m8,  m12         ; { X0 Y0 Z0 W0 | X4 Y4 Z4 W4 }
        pshufb %2, m9,  m12         ; { X1 Y1 Z1 W1 | X5 Y5 Z5 W5 }
        pshufb %3, m10, m12         ; { X2 Y2 Z2 W2 | X6 Y6 Z6 W6 }
        pshufb %4, m11, m12         ; { X3 Y3 Z3 W3 | X7 Y7 Z7 W7 }
    %endif
        punpckldq m8,  %1, %2       ; { X0 X1 Y0 Y1 | X4 X5 Y4 Y5 }
        punpckldq m9,  %3, %4       ; { X2 X3 Y2 Y3 | X6 X7 Y6 Y7 }
        punpckhdq m10, %1, %2       ; { Z0 Z1 W0 W1 | Z4 Z5 W4 W5 }
        punpckhdq m11, %3, %4       ; { Z2 Z3 W2 W3 | Z6 Z7 W6 W7 }
        punpcklqdq %1, m8, m9       ; { X0 X1 X2 X3 | X4 X5 X6 X7 }
        punpckhqdq %2, m8, m9       ; { Y0 Y1 Y2 Y3 | Y4 Y5 Y6 Y7 }
        punpcklqdq %3, m10, m11     ; { Z0 Z1 Z2 Z3 | Z4 Z5 Z6 Z7 }
IF W,   punpckhqdq %4, m10, m11     ; { W0 W1 W2 W3 | W4 W5 W6 W7 }
%endmacro

%macro READ_PACKED 0
    %if BITS < 32
        VBROADCASTI128 m12, [ read %+ BITS %+ _unpack %+ COMPS ]
    %endif
        LOAD_CONT tmp0q
    %if COMPS == 2
        read_packed2
    %else
        read_packed34 mx,  my,  mz,  mw,  in0q
IF1 V2, read_packed34 mx2, my2, mz2, mw2, in0q + mmsize * COMPS
    %endif
        add in0q, BLOCK_SIZE * COMPS
        CONTINUE tmp0q
%endmacro

%macro write_packed2 0
    %if cpuflag(avx2)
        vpermq mx, mx, q3120       ; { X0 X2 | X1 X3 }
        vpermq my, my, q3120       ; { Y0 Y2 | Y1 Y3 }
IF V2,  vpermq mx2, mx2, q3120
IF V2,  vpermq my2, my2, q3120
    %endif
        unpcklpd m8, mx, my        ; { X0 Y0 | X1 Y1 }
        unpckhpd m9, mx, my        ; { X2 Y2 | X3 Y3 }
IF V2,  unpcklpd m10, mx2, my2
IF V2,  unpckhpd m11, mx2, my2
    %if BITS == 32
        shufps m8, m8, q3120
        shufps m9, m9, q3120
IF V2,  shufps m10, m10, q3120
IF V2,  shufps m11, m11, q3120
    %else
        pshufb m8, m12
        pshufb m9, m12
IF V2,  pshufb m10, m12
IF V2,  pshufb m11, m12
    %endif
        movu [out0q + 0 * mmsize], m8
        movu [out0q + 1 * mmsize], m9
IF V2,  movu [out0q + 2 * mmsize], m10
IF V2,  movu [out0q + 3 * mmsize], m11
%endmacro

%macro write_packed34 5 ; x, y, z, w, addr
        punpckldq m8,  %1, %2       ; { X0 Y0 X1 Y1 | X4 Y4 X5 Y5 }
        punpckldq m9,  %3, %4       ; { Z0 W0 Z1 W1 | Z4 W4 Z5 W5 }
        punpckhdq m10, %1, %2       ; { X2 Y2 X3 Y3 | X6 Y6 X7 Y7 }
        punpckhdq m11, %3, %4       ; { Z2 W2 Z3 W3 | Z6 W6 Z7 W7 }
        punpcklqdq %1, m8, m9       ; { X0 Y0 Z0 W0 | X4 Y4 Z4 W4 }
        punpckhqdq %2, m8, m9       ; { X1 Y1 Z1 W1 | X5 Y5 Z5 W5 }
        punpcklqdq %3, m10, m11     ; { X2 Y2 Z2 W2 | X6 Y6 Z6 W6 }
        punpckhqdq %4, m10, m11     ; { X3 Y3 Z3 W3 | X7 Y7 Z7 W7 }
    %if BITS == 32
        mova m8,  %1
        mova m9,  %2
        mova m10, %3
        mova m11, %4
    %else
        pshufb m8,  %1, m12
        pshufb m9,  %2, m12
        pshufb m10, %3, m12
        pshufb m11, %4, m12
    %endif
        movu [%5 +  0 * COMPS], xm8
        movu [%5 +  4 * COMPS], xm9
        movu [%5 +  8 * COMPS], xm10
        movu [%5 + 12 * COMPS], xm11
    %if cpuflag(avx2)
        vextracti128 [%5 + 16 * COMPS], m8,  1
        vextracti128 [%5 + 20 * COMPS], m9,  1
        vextracti128 [%5 + 24 * COMPS], m10, 1
        vextracti128 [%5 + 28 * COMPS], m11, 1
    %endif
%endmacro

%macro WRITE_PACKED 0
    %if BITS < 32
        VBROADCASTI128 m12, [ write %+ BITS %+ _pack %+ COMPS ]
    %endif
        LOAD_CONT tmp0q
    %if COMPS == 2
        write_packed2
    %else
        write_packed34 mx,  my,  mz,  mw,  out0q
IF1 V2, write_packed34 mx2, my2, mz2, mw2, out0q + mmsize * COMPS
    %endif
        add out0q, BLOCK_SIZE * COMPS
        RET
%endmacro

%macro PACK 4 ; x, y, z, w
        ; pslld works for all sizes because the input should not overflow
IF %2,  pslld mx, %4+%3+%2
IF %3,  pslld my, %4+%3
IF %4,  pslld mz, %4
IF Y,   por mx, my
IF Z,   por mx, mz
IF W,   por mx, mw
    %if V2
IF %2,  pslld mx2, %4+%3+%2
IF %3,  pslld my2, %4+%3
IF %4,  pslld mz2, %4
IF Y,   por mx2, my2
IF Z,   por mx2, mz2
IF W,   por mx2, mw2
    %endif
        CONTINUE
%endmacro

%macro UNPACK 4 ; x, y, z, w
        LOAD_CONT tmp0q
%if BITS == 8
        assert MASK == SWS_COMP_ELEMS(3)
        pand mz, mx, [mask%3]
        psrld my, mx, %3
        psrld mx, %3+%2
        pand my, [mask%2]
        pand mx, [mask%1]
    %if V2
        pand mz2, mx2, [mask%3]
        psrld my2, mx2, %3
        psrld mx2, %3+%2
        pand my2, [mask%2]
        pand mx2, [mask%1]
    %endif
%else
        ; clear high bits by shifting left
IF W,   psllx mw, mx, BITS - (%4)
IF Z,   psllx mz, mx, BITS - (%4+%3)
IF Y,   psllx my, mx, BITS - (%4+%3+%2)
        psrlx mx, %2+%3+%4
IF Y,   psrlx my, BITS - %2
IF Z,   psrlx mz, BITS - %3
IF W,   psrlx mw, BITS - %4
    %if V2
IF W,   psllx mw2, mx2, BITS - (%4)
IF Z,   psllx mz2, mx2, BITS - (%4+%3)
IF Y,   psllx my2, mx2, BITS - (%4+%3+%2)
        psrlx mx2, %2+%3+%4
IF Y,   psrlx my2, BITS - %2
IF Z,   psrlx mz2, BITS - %3
IF W,   psrlx mw2, BITS - %4
    %endif
%endif
        CONTINUE tmp0q
%endmacro

;---------------------------------------------------------
; Pixel type conversions

%macro cast8to16 4 ; reg, reg2, xreg, xreg2
IF V2,  vextracti128 %4, %1, 1
        pmovzxbw %1, %3
IF V2,  pmovzxbw %2, %4
%endmacro

%macro cast16to8 4 ; reg, reg2, xreg, xreg2
%if V2
        packuswb %1, %2
        vpermq %1, %1, q3120
%else
        vextracti128  %4, %1, 1
        packuswb %3, %4
%endif
%endmacro

%macro cast8to32 4 ; reg, reg2, xreg, xreg2
        psrldq %4, %3, 8
        pmovzxbd %1, %3
        pmovzxbd %2, %4
%endmacro

%macro cast32to8 4 ; reg, reg2, xreg, xreg2
        packusdw %1, %2
        vextracti128 %4, %1, 1
        packuswb %3, %4
        vpshufd %3, %3, q3120
%endmacro

%macro cast16to32 4 ; reg, reg2, xreg, xreg2
        vextracti128 %4, %1, 1
        pmovzxwd %1, %3
        pmovzxwd %2, %4
%endmacro

%macro cast32to16 4 ; reg, reg2, xreg, xreg2
        packusdw %1, %2
        vpermq %1, %1, q3120
%endmacro

%macro CAST_TO 0
%ifidn UOP, SWS_UOP_TO_U8
    %assign BITS_TO 8
%elifidn UOP, SWS_UOP_TO_U16
    %assign BITS_TO 16
%else
    %assign BITS_TO 32
%endif

        LOAD_CONT tmp0q
%ifidn TYPE, SWS_PIXEL_F32
IF X,   cvttps2dq mx, mx
IF Y,   cvttps2dq my, my
IF Z,   cvttps2dq mz, mz
IF W,   cvttps2dq mw, mw
IF X,   cvttps2dq mx2, mx2
IF Y,   cvttps2dq my2, my2
IF Z,   cvttps2dq mz2, mz2
IF W,   cvttps2dq mw2, mw2
%endif
%if BITS != BITS_TO
        ; integer size conversion
IF1 X,  cast %+ BITS %+ to %+ BITS_TO mx, mx2, xmx, xmx2
IF1 Y,  cast %+ BITS %+ to %+ BITS_TO my, my2, xmy, xmy2
IF1 Z,  cast %+ BITS %+ to %+ BITS_TO mz, mz2, xmz, xmz2
IF1 W,  cast %+ BITS %+ to %+ BITS_TO mw, mw2, xmw, xmw2
    %if cpuflag(avx2) && (BITS > BITS_TO && !V2 || BITS >= BITS_TO * 4)
        ; clear upper bits after reducing the register size
        vzeroupper ; TMP
    %endif
%endif
%ifidn UOP, SWS_UOP_TO_F32
IF X,   vcvtdq2ps mx, mx
IF Y,   vcvtdq2ps my, my
IF Z,   vcvtdq2ps mz, mz
IF W,   vcvtdq2ps mw, mw
IF X,   vcvtdq2ps mx2, mx2
IF Y,   vcvtdq2ps my2, my2
IF Z,   vcvtdq2ps mz2, mz2
IF W,   vcvtdq2ps mw2, mw2
%endif
        CONTINUE tmp0q
%endmacro

;---------------------------------------------------------
; Moving, copying and clearing

%macro MOVE 13 ; num, dst0..dst5, src0..src5
%assign NUM_MOVES %1
%define DST %2
%define SRC %8

        LOAD_CONT tmp0q
%rep NUM_MOVES
        %assign dstidx %2 < 0 ? 8 : %2
        %assign srcidx %8 < 0 ? 8 : %8
        mova m %+ dstidx, m %+ srcidx
    %if V2
        %assign dstidx dstidx + 4
        %assign srcidx srcidx + 4
        mova m %+ dstidx, m %+ srcidx
    %endif
%rotate 1
%endrep
        CONTINUE tmp0q
%endmacro

%macro clear 3 ; idx, reg, reg2
%if SWS_COMP_TEST(ZERO_MASK, %1)
        pxor %2, %2
%elif SWS_COMP_TEST(ONE_MASK, %1)
        pcmpeqb %2, %2
%elif cpuflag(avx)
        vpbroadcastd %2, [implq + SwsOpImpl.priv + 4 * %1]
%else
        movd %2, [implq + SwsOpImpl.priv + 4 * %1]
        pshufd %2, %2, 0
%endif
IF V2,  mova %3, %2
%endmacro

%macro CLEAR 2
%assign ONE_MASK  %1
%assign ZERO_MASK %2
IF1 X,  clear 0, mx, mx2
IF1 Y,  clear 1, my, my2
IF1 Z,  clear 2, mz, mz2
IF1 W,  clear 3, mw, mw2
        CONTINUE
%endmacro

;---------------------------------------------------------
; Bit manipulation

%macro SWAP_BYTES 0
        VBROADCASTI128 m8, [swap %+ BITS]
IF X,   pshufb mx, m8
IF Y,   pshufb my, m8
IF Z,   pshufb mz, m8
IF W,   pshufb mw, m8
%if V2
IF X,   pshufb mx2, m8
IF Y,   pshufb my2, m8
IF Z,   pshufb mz2, m8
IF W,   pshufb mw2, m8
%endif
        CONTINUE
%endmacro

%macro EXPAND_BIT 0
%if BITS == 8
        VBROADCASTI128 m8, [const1b]
%elif BITS == 16
        VBROADCASTI128 m8, [const1w]
%else
        assert 0, EXPAND_BIT is not implemented for 32-bit types
%endif
IF X,   pcmpeqx mx, m8
IF Y,   pcmpeqx my, m8
IF Z,   pcmpeqx mz, m8
IF W,   pcmpeqx mw, m8
%if V2
IF X,   pcmpeqx mx2, m8
IF Y,   pcmpeqx my2, m8
IF Z,   pcmpeqx mz2, m8
IF W,   pcmpeqx mw2, m8
%endif
        CONTINUE
%endmacro

%macro EXPAND_BYTE 0
        assert BITS == 8
%ifidn UOP, SWS_UOP_EXPAND_PAIR
        %assign BITS_TO BITS * 2
%elifidn UOP, SWS_UOP_EXPAND_QUAD
        %assign BITS_TO BITS * 4
%endif
        VBROADCASTI128 m8, [expand %+ BITS_TO]
        LOAD_CONT tmp0q
IF1 X,  cast8to %+ BITS_TO mx, mx2, xmx, xmx2
IF1 Y,  cast8to %+ BITS_TO my, my2, xmy, xmy2
IF1 Z,  cast8to %+ BITS_TO mz, mz2, xmz, xmz2
IF1 W,  cast8to %+ BITS_TO mw, mw2, xmw, xmw2
IF X,   pshufb mx, m8
IF Y,   pshufb my, m8
IF Z,   pshufb mz, m8
IF W,   pshufb mw, m8
    %if V2
IF X,   pshufb mx2, m8
IF Y,   pshufb my2, m8
IF Z,   pshufb mz2, m8
IF W,   pshufb mw2, m8
    %endif
        CONTINUE tmp0q
%endmacro

%macro LSHIFT 1 ; shift
        LOAD_CONT tmp0q
IF X,   psllx mx, %1
IF Y,   psllx my, %1
IF Z,   psllx mz, %1
IF W,   psllx mw, %1
%if V2
IF X,   psllx mx2, %1
IF Y,   psllx my2, %1
IF Z,   psllx mz2, %1
IF W,   psllx mw2, %1
%endif
        CONTINUE tmp0q
%endmacro

%macro RSHIFT 1 ; shift
        LOAD_CONT tmp0q
IF X,   psrlx mx, %1
IF Y,   psrlx my, %1
IF Z,   psrlx mz, %1
IF W,   psrlx mw, %1
%if V2
IF X,   psrlx mx2, %1
IF Y,   psrlx my2, %1
IF Z,   psrlx mz2, %1
IF W,   psrlx mw2, %1
%endif
        CONTINUE tmp0q
%endmacro

;---------------------------------------------------------
; Arithmetic operations

%macro pmull4 6 ; suffix, x, y, z, w, k
IF X,   pmull%1 %2, %6
IF Y,   pmull%1 %3, %6
IF Z,   pmull%1 %4, %6
IF W,   pmull%1 %5, %6
%endmacro

%macro scale8 5 ; mx, my, mz, mw, k
IF X,   mova m8,  %1
IF Y,   mova m9,  %2
IF Z,   mova m10, %3
IF W,   mova m11, %4
IF X,   punpcklbw %1,  m15
IF Y,   punpcklbw %2,  m15
IF Z,   punpcklbw %3,  m15
IF W,   punpcklbw %4,  m15
IF X,   punpckhbw m8,  m15
IF Y,   punpckhbw m9,  m15
IF Z,   punpckhbw m10, m15
IF W,   punpckhbw m11, m15
        pmull4 w, %1, %2, %3,  %4,  m12
        pmull4 w, m8, m9, m10, m11, m12
IF X,   packuswb %1, m8
IF Y,   packuswb %2, m9
IF Z,   packuswb %3, m10
IF W,   packuswb %4, m11
%endmacro

%macro SCALE 0
        LOAD_CONT tmp0q
%if BITS == 8
        vpbroadcastw m12, [implq + SwsOpImpl.priv]
        pxor m15, m15
        scale8 mx,  my,  mz,  mw,  m12
IF1 V2, scale8 mx2, my2, mz2, mw2, m12
%else
        vpbroadcastx m12, [implq + SwsOpImpl.priv]
        pmull4 x, mx,  my,  mz,  mw,  m12
IF1 V2, pmull4 x, mx2, my2, mz2, mw2, m12
%endif
        CONTINUE tmp0q
%endmacro

%macro ADD 0
assert 0, SWS_UOP_ADD is not implemented for integer types
%endmacro

%macro MIN 0
assert 0, SWS_UOP_MIN is not implemented for integer types
%endmacro

%macro MAX 0
assert 0, SWS_UOP_MAX is not implemented for integer types
%endmacro

%macro LINEAR_FMA 0-*
assert 0, SWS_UOP_LINEAR_FMA is not implemented for integer types
%endmacro

%macro DITHER 0-*
assert 0, SWS_UOP_DITHER is not implemented for integer types
%endmacro

;---------------------------------------------------------
; Instantiate above macros to generate all uop kernels

%macro decl_ops 1 ; type
    DECL_%1_READ_PACKED     (READ_PACKED)
    DECL_%1_READ_NIBBLE     (READ_NIBBLE)
    DECL_%1_READ_BIT        (READ_BIT)
    DECL_%1_WRITE_PACKED    (WRITE_PACKED)
    DECL_%1_WRITE_NIBBLE    (WRITE_NIBBLE)
    DECL_%1_WRITE_BIT       (WRITE_BIT)
    DECL_%1_MOVE            (MOVE)
    DECL_%1_SWAP_BYTES      (SWAP_BYTES)
    DECL_%1_EXPAND_BIT      (EXPAND_BIT)
    DECL_%1_SCALE           (SCALE)
    DECL_%1_ADD             (ADD)
    DECL_%1_MIN             (MIN)
    DECL_%1_MAX             (MAX)
    DECL_%1_UNPACK          (UNPACK)
    DECL_%1_PACK            (PACK)
    DECL_%1_LSHIFT          (LSHIFT)
    DECL_%1_RSHIFT          (RSHIFT)
    DECL_%1_LINEAR_FMA      (LINEAR_FMA)
    DECL_%1_DITHER          (DITHER)
%endmacro

%macro decl_type_invariant 0
    DECL_U8_READ_PLANAR     (READ_PLANAR)
    DECL_U8_WRITE_PLANAR    (WRITE_PLANAR)
    DECL_U8_CLEAR           (CLEAR)
%endmacro

%macro decl_cast_u16 0
    DECL_U8_TO_U16          (CAST_TO)
    DECL_U16_TO_U8          (CAST_TO)
    DECL_U8_EXPAND_PAIR     (EXPAND_BYTE)
%endmacro

%macro decl_cast_u32 0
    DECL_U8_TO_U32          (CAST_TO)
    DECL_U32_TO_U8          (CAST_TO)
    DECL_U16_TO_U32         (CAST_TO)
    DECL_U32_TO_U16         (CAST_TO)
    DECL_U8_EXPAND_QUAD     (EXPAND_BYTE)
%endmacro

%macro decl_cast_f32 0
    DECL_U8_TO_F32          (CAST_TO)
    DECL_F32_TO_U8          (CAST_TO)
    DECL_U16_TO_F32         (CAST_TO)
    DECL_F32_TO_U16         (CAST_TO)
    DECL_U32_TO_F32         (CAST_TO)
    DECL_F32_TO_U32         (CAST_TO)
%endmacro

INIT_XMM sse4
decl_v2 0, decl_ops U8
decl_v2 1, decl_ops U8
decl_v2 0, decl_type_invariant
decl_v2 1, decl_type_invariant

INIT_YMM avx2
decl_v2 0, decl_ops U8
decl_v2 1, decl_ops U8
decl_v2 0, decl_ops U16
decl_v2 1, decl_ops U16
decl_v2 0, decl_type_invariant
decl_v2 1, decl_type_invariant
decl_v2 0, decl_cast_u16
decl_v2 1, decl_cast_u16

decl_v2 1, decl_ops U32
decl_v2 1, decl_cast_u32
decl_v2 1, decl_cast_f32
