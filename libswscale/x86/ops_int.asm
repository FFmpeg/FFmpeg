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

SECTION_RODATA

align 16
expand16_shuf:  db   0,  0,  2,  2,  4,  4,  6,  6,  8,  8, 10, 10, 12, 12, 14, 14
expand32_shuf:  db   0,  0,  0,  0,  4,  4,  4,  4,  8,  8,  8,  8, 12, 12, 12, 12

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

align 32
bits_shuf:      db   0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1, \
                     2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3
bits_mask:      db 128, 64, 32, 16,  8,  4,  2,  1,128, 64, 32, 16,  8,  4,  2,  1
bits_reverse:   db   7,  6,  5,  4,  3,  2,  1,  0, 15, 14, 13, 12, 11, 10,  9,  8,

align 32
mask1: times 32 db 0x01
mask2: times 32 db 0x03
mask3: times 32 db 0x07
mask4: times 32 db 0x0F

SECTION .text

;---------------------------------------------------------
; Global entry point. See `ops_common.asm` for info.

%macro process_fn 1 ; number of planes
cglobal sws_process%1_x86, 6, 6 + 2 * %1, 16
            ; Args:
            ;   execq, implq, bxd, yd as defined in ops_common.int
            ;   bx_end and y_end are initially in tmp0d / tmp1d
            ;   (see SwsOpFunc signature)
            ;
            ; Stack layout:
            ;   [rsp +  0] = [qword] impl->cont (address of first kernel)
            ;   [rsp +  8] = [qword] &impl[1]   (restore implq after chain)
            ;   [rsp + 16] = [dword] bx start   (restore after line finish)
            ;   [rsp + 20] = [dword] bx end     (loop counter limit)
            ;   [rsp + 24] = [dword] y end      (loop counter limit)
            sub rsp, 32
            mov [rsp + 16], bxd
            mov [rsp + 20], tmp0d ; bx_end
            mov [rsp + 24], tmp1d ; y_end
            mov tmp0q, [implq + SwsOpImpl.cont]
            add implq, SwsOpImpl.next
            mov [rsp +  0], tmp0q
            mov [rsp +  8], implq

            ; load plane pointers
            mov in0q,  [execq + SwsOpExec.in0]
IF %1 > 1,  mov in1q,  [execq + SwsOpExec.in1]
IF %1 > 2,  mov in2q,  [execq + SwsOpExec.in2]
IF %1 > 3,  mov in3q,  [execq + SwsOpExec.in3]
            mov out0q, [execq + SwsOpExec.out0]
IF %1 > 1,  mov out1q, [execq + SwsOpExec.out1]
IF %1 > 2,  mov out2q, [execq + SwsOpExec.out2]
IF %1 > 3,  mov out3q, [execq + SwsOpExec.out3]
            jmp [rsp] ; call into op chain

; Declare a separate global label for the return point, so that we can append
; it to the list of op function pointers from the C code, effectively ensuring
; that we end up here again after the op chain finishes processing a line.
; (See also: cglobal_label in x86inc.asm)
%if FORMAT_ELF
    global current_function %+ _return:function hidden
%elif FORMAT_MACHO && HAVE_PRIVATE_EXTERN
    global current_function %+ _return:private_extern
%else
    global current_function %+ _return
%endif
align function_align
current_function %+ _return:

            ; op chain always returns back here
            mov implq, [rsp + 8]
            inc bxd
            cmp bxd, [rsp + 20]
            jne .continue
            ; end of line
            inc yd
            cmp yd, [rsp + 24]
            je .end
            ; bump addresses to point to start of next line
            add in0q,  [execq + SwsOpExec.in_bump0]
IF %1 > 1,  add in1q,  [execq + SwsOpExec.in_bump1]
IF %1 > 2,  add in2q,  [execq + SwsOpExec.in_bump2]
IF %1 > 3,  add in3q,  [execq + SwsOpExec.in_bump3]
            add out0q, [execq + SwsOpExec.out_bump0]
IF %1 > 1,  add out1q, [execq + SwsOpExec.out_bump1]
IF %1 > 2,  add out2q, [execq + SwsOpExec.out_bump2]
IF %1 > 3,  add out3q, [execq + SwsOpExec.out_bump3]
            mov bxd, [rsp + 16]
.continue:
            jmp [rsp]
.end:
            add rsp, 32
            RET
%endmacro

process_fn 1
process_fn 2
process_fn 3
process_fn 4

;---------------------------------------------------------
; Packed shuffle fast-path

; This is a special entry point for handling a subset of operation chains
; that can be reduced down to a single `pshufb` shuffle mask. For more details
; about when this works, refer to the documentation of `ff_sws_solve_shuffle`.
;
; We specialize this function for every possible combination of pixel strides.
; For example, gray -> gray16 is classified as an "8, 16" operation because it
; takes 8 bytes and expands them out to 16 bytes in each application of the
; 128-bit shuffle mask.
;
; Since pshufb can't shuffle across lanes, we only instantiate SSE4 versions for
; all shuffles that are not a clean multiple of 128 bits (e.g. rgb24 -> rgb0).
; For the clean multiples (e.g. rgba -> argb), we also define AVX2 and AVX512
; versions that can handle a larger number of bytes at once.

%macro packed_shuffle 2 ; size_in, size_out
cglobal packed_shuffle%1_%2, 6, 10, 2, \
    exec, shuffle, bx, y, bxend, yend, src, dst, src_stride, dst_stride
            mov srcq, [execq + SwsOpExec.in0]
            mov dstq, [execq + SwsOpExec.out0]
            mov src_strideq, [execq + SwsOpExec.in_stride0]
            mov dst_strideq, [execq + SwsOpExec.out_stride0]
            VBROADCASTI128 m1, [shuffleq]
            sub bxendd, bxd
            sub yendd, yd
            ; reuse now-unneeded regs
    %define srcidxq execq
            imul srcidxq, bxendq, -%1
%if %1 = %2
    %define dstidxq srcidxq
%else
    %define dstidxq shuffleq ; no longer needed reg
            imul dstidxq, bxendq, -%2
%endif
            sub srcq, srcidxq
            sub dstq, dstidxq
.loop:
    %if %1 <= 4
            movd m0, [srcq + srcidxq]
    %elif %1 <= 8
            movq m0, [srcq + srcidxq]
    %else
            movu m0, [srcq + srcidxq]
    %endif
            pshufb m0, m1
            movu [dstq + dstidxq], m0
            add srcidxq, %1
IF %1 != %2,add dstidxq, %2
            jnz .loop
            add srcq, src_strideq
            add dstq, dst_strideq
            imul srcidxq, bxendq, -%1
IF %1 != %2,imul dstidxq, bxendq, -%2
            dec yendd
            jnz .loop
            RET
%endmacro

INIT_XMM sse4
packed_shuffle  5, 15 ;  8 -> 24
packed_shuffle  4, 16 ;  8 -> 32, 16 -> 64
packed_shuffle  2, 12 ;  8 -> 48
packed_shuffle 16,  8 ; 16 -> 8
packed_shuffle 10, 15 ; 16 -> 24
packed_shuffle  8, 16 ; 16 -> 32, 32 -> 64
packed_shuffle  4, 12 ; 16 -> 48
packed_shuffle 15, 15 ; 24 -> 24
packed_shuffle 12, 16 ; 24 -> 32
packed_shuffle  6, 12 ; 24 -> 48
packed_shuffle 16, 12 ; 32 -> 24, 64 -> 48
packed_shuffle 16, 16 ; 32 -> 32, 64 -> 64
packed_shuffle  8, 12 ; 32 -> 48
packed_shuffle 12, 12 ; 48 -> 48

INIT_YMM avx2
packed_shuffle 32, 32

INIT_ZMM avx512
packed_shuffle 64, 64

;---------------------------------------------------------
; Planar reads / writes

%macro read_planar 1 ; elems
op read_planar%1
            movu mx, [in0q]
IF %1 > 1,  movu my, [in1q]
IF %1 > 2,  movu mz, [in2q]
IF %1 > 3,  movu mw, [in3q]
%if V2
            movu mx2, [in0q + mmsize]
IF %1 > 1,  movu my2, [in1q + mmsize]
IF %1 > 2,  movu mz2, [in2q + mmsize]
IF %1 > 3,  movu mw2, [in3q + mmsize]
%endif
            LOAD_CONT tmp0q
            add in0q, mmsize * (1 + V2)
IF %1 > 1,  add in1q, mmsize * (1 + V2)
IF %1 > 2,  add in2q, mmsize * (1 + V2)
IF %1 > 3,  add in3q, mmsize * (1 + V2)
            CONTINUE tmp0q
%endmacro

%macro write_planar 1 ; elems
op write_planar%1
            LOAD_CONT tmp0q
            movu [out0q], mx
IF %1 > 1,  movu [out1q], my
IF %1 > 2,  movu [out2q], mz
IF %1 > 3,  movu [out3q], mw
%if V2
            movu [out0q + mmsize], mx2
IF %1 > 1,  movu [out1q + mmsize], my2
IF %1 > 2,  movu [out2q + mmsize], mz2
IF %1 > 3,  movu [out3q + mmsize], mw2
%endif
            add out0q, mmsize * (1 + V2)
IF %1 > 1,  add out1q, mmsize * (1 + V2)
IF %1 > 2,  add out2q, mmsize * (1 + V2)
IF %1 > 3,  add out3q, mmsize * (1 + V2)
            FINISH tmp0q
%endmacro

%macro read_packed2 1 ; depth
op read%1_packed2
            movu m8,  [in0q + 0*mmsize]
            movu m9,  [in0q + 1*mmsize]
    IF V2,  movu m10, [in0q + 2*mmsize]
    IF V2,  movu m11, [in0q + 3*mmsize]
IF %1 < 32, VBROADCASTI128 m12, [read%1_unpack2]
            LOAD_CONT tmp0q
            add in0q, mmsize * (2 + V2 * 2)
%if %1 == 32
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
%if avx_enabled
            vpermq mx, mx, q3120       ; { X0 X1 | X2 X3 }
            vpermq my, my, q3120       ; { Y0 Y1 | Y2 Y3 }
    IF V2,  vpermq mx2, mx2, q3120
    IF V2,  vpermq my2, my2, q3120
%endif
            CONTINUE tmp0q
%endmacro

%macro write_packed2 1 ; depth
op write%1_packed2
IF %1 < 32, VBROADCASTI128 m12, [write%1_pack2]
            LOAD_CONT tmp0q
%if avx_enabled
            vpermq mx, mx, q3120       ; { X0 X2 | X1 X3 }
            vpermq my, my, q3120       ; { Y0 Y2 | Y1 Y3 }
    IF V2,  vpermq mx2, mx2, q3120
    IF V2,  vpermq my2, my2, q3120
%endif
            unpcklpd m8, mx, my        ; { X0 Y0 | X1 Y1 }
            unpckhpd m9, mx, my        ; { X2 Y2 | X3 Y3 }
    IF V2,  unpcklpd m10, mx2, my2
    IF V2,  unpckhpd m11, mx2, my2
%if %1 == 32
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
            movu [out0q + 0*mmsize], m8
            movu [out0q + 1*mmsize], m9
IF V2,      movu [out0q + 2*mmsize], m10
IF V2,      movu [out0q + 3*mmsize], m11
            add out0q, mmsize * (2 + V2 * 2)
            FINISH tmp0q
%endmacro

; helper macro reused for both 3 and 4 component packed reads
%macro read_packed_inner 7 ; x, y, z, w, addr, num, depth
            movu xm8,  [%5 + 0  * %6]
            movu xm9,  [%5 + 4  * %6]
            movu xm10, [%5 + 8  * %6]
            movu xm11, [%5 + 12 * %6]
    %if avx_enabled
            vinserti128 m8,  m8,  [%5 + 16 * %6], 1
            vinserti128 m9,  m9,  [%5 + 20 * %6], 1
            vinserti128 m10, m10, [%5 + 24 * %6], 1
            vinserti128 m11, m11, [%5 + 28 * %6], 1
    %endif
    %if %7 == 32
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
IF %6 > 3,  punpckhqdq %4, m10, m11     ; { W0 W1 W2 W3 | W4 W5 W6 W7 }
%endmacro

%macro read_packed 2 ; num, depth
op read%2_packed%1
IF %2 < 32, VBROADCASTI128 m12, [read%2_unpack%1]
            LOAD_CONT tmp0q
            read_packed_inner mx, my, mz, mw, in0q, %1, %2
IF1 V2,     read_packed_inner mx2, my2, mz2, mw2, in0q + %1 * mmsize, %1, %2
            add in0q, %1 * mmsize * (1 + V2)
            CONTINUE tmp0q
%endmacro

%macro write_packed_inner 7 ; x, y, z, w, addr, num, depth
        punpckldq m8,  %1, %2       ; { X0 Y0 X1 Y1 | X4 Y4 X5 Y5 }
        punpckldq m9,  %3, %4       ; { Z0 W0 Z1 W1 | Z4 W4 Z5 W5 }
        punpckhdq m10, %1, %2       ; { X2 Y2 X3 Y3 | X6 Y6 X7 Y7 }
        punpckhdq m11, %3, %4       ; { Z2 W2 Z3 W3 | Z6 W6 Z7 W7 }
        punpcklqdq %1, m8, m9       ; { X0 Y0 Z0 W0 | X4 Y4 Z4 W4 }
        punpckhqdq %2, m8, m9       ; { X1 Y1 Z1 W1 | X5 Y5 Z5 W5 }
        punpcklqdq %3, m10, m11     ; { X2 Y2 Z2 W2 | X6 Y6 Z6 W6 }
        punpckhqdq %4, m10, m11     ; { X3 Y3 Z3 W3 | X7 Y7 Z7 W7 }
    %if %7 == 32
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
        movu [%5 +  0*%6], xm8
        movu [%5 +  4*%6], xm9
        movu [%5 +  8*%6], xm10
        movu [%5 + 12*%6], xm11
    %if avx_enabled
        vextracti128 [%5 + 16*%6], m8, 1
        vextracti128 [%5 + 20*%6], m9, 1
        vextracti128 [%5 + 24*%6], m10, 1
        vextracti128 [%5 + 28*%6], m11, 1
    %endif
%endmacro

%macro write_packed 2 ; num, depth
op write%2_packed%1
IF %2 < 32, VBROADCASTI128 m12, [write%2_pack%1]
            LOAD_CONT tmp0q
            write_packed_inner mx, my, mz, mw, out0q, %1, %2
IF1 V2,     write_packed_inner mx2, my2, mz2, mw2, out0q + %1 * mmsize, %1, %2
            add out0q, %1 * mmsize * (1 + V2)
            FINISH tmp0q
%endmacro

%macro rw_packed 1 ; depth
        read_packed2 %1
        read_packed 3, %1
        read_packed 4, %1
        write_packed2 %1
        write_packed 3, %1
        write_packed 4, %1
%endmacro

%macro read_nibbles 0
op read_nibbles1
%if avx_enabled
        movu xmx,  [in0q]
IF V2,  movu xmx2, [in0q + 16]
%else
        movq xmx,  [in0q]
IF V2,  movq xmx2, [in0q + 8]
%endif
        VBROADCASTI128 m8, [mask4]
        LOAD_CONT tmp0q
        add in0q, (mmsize >> 1) * (1 + V2)
        pmovzxbw mx, xmx
IF V2,  pmovzxbw mx2, xmx2
        psllw my, mx, 8
IF V2,  psllw my2, mx2, 8
        psrlw mx, 4
IF V2,  psrlw mx2, 4
        pand my, m8
IF V2,  pand my2, m8
        por mx, my
IF V2,  por mx2, my2
        CONTINUE tmp0q
%endmacro

%macro read_bits 0
op read_bits1
%if avx_enabled
        vpbroadcastd mx,  [in0q]
IF V2,  vpbroadcastd mx2, [in0q + 4]
%else
        movd mx, [in0q]
IF V2,  movd mx2, [in0q + 2]
%endif
        mova m8, [bits_shuf]
        VBROADCASTI128 m9,  [bits_mask]
        VBROADCASTI128 m10, [mask1]
        LOAD_CONT tmp0q
        add in0q, (mmsize >> 3) * (1 + V2)
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

; TODO: write_nibbles

%macro write_bits 0
op write_bits1
        VBROADCASTI128 m8, [bits_reverse]
        psllw mx,  7
IF V2,  psllw mx2, 7
        pshufb mx,  m8
IF V2,  pshufb mx2, m8
        pmovmskb tmp0d, mx
IF V2,  pmovmskb tmp1d, mx2
%if avx_enabled
        mov [out0q],     tmp0d
IF V2,  mov [out0q + 4], tmp1d
%else
        mov [out0q],     tmp0d
IF V2,  mov [out0q + 2], tmp1d
%endif
        LOAD_CONT tmp0q
        add out0q, (mmsize >> 3) * (1 + V2)
        FINISH tmp0q
%endmacro

;--------------------------
; Pixel packing / unpacking

%macro pack_generic 3-4 0 ; x, y, z, w
op pack_%1%2%3%4
        ; pslld works for all sizes because the input should not overflow
IF %2,  pslld mx, %4+%3+%2
IF %3,  pslld my, %4+%3
IF %4,  pslld mz, %4
IF %2,  por mx, my
IF %3,  por mx, mz
IF %4,  por mx, mw
    %if V2
IF %2,  pslld mx2, %4+%3+%2
IF %3,  pslld my2, %4+%3
IF %4,  pslld mz2, %4
IF %2,  por mx2, my2
IF %3,  por mx2, mz2
IF %4,  por mx2, mw2
    %endif
        CONTINUE
%endmacro

%macro unpack 5-6 0 ; type, bits, x, y, z, w
op unpack_%3%4%5%6
        ; clear high bits by shifting left
IF %6,  vpsll%1 mw, mx, %2 - (%6)
IF %5,  vpsll%1 mz, mx, %2 - (%6+%5)
IF %4,  vpsll%1 my, mx, %2 - (%6+%5+%4)
        psrl%1 mx, %4+%5+%6
IF %4,  psrl%1 my, %2 - %4
IF %5,  psrl%1 mz, %2 - %5
IF %6,  psrl%1 mw, %2 - %6
    %if V2
IF %6,  vpsll%1 mw2, mx2, %2 - (%6)
IF %5,  vpsll%1 mz2, mx2, %2 - (%6+%5)
IF %4,  vpsll%1 my2, mx2, %2 - (%6+%5+%4)
        psrl%1 mx2, %4+%5+%6
IF %4,  psrl%1 my2, %2 - %4
IF %5,  psrl%1 mz2, %2 - %5
IF %6,  psrl%1 mw2, %2 - %6
    %endif
        CONTINUE
%endmacro

%macro unpack8 3 ; x, y, z
op unpack_%1%2%3 %+ 0
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
        CONTINUE
%endmacro

;---------------------------------------------------------
; Generic byte order shuffle (packed swizzle, endian, etc)

%macro shuffle 0
op shuffle
        VBROADCASTI128 m8, [implq + SwsOpImpl.priv]
        LOAD_CONT tmp0q
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

;---------------------------------------------------------
; Clearing

%macro clear_alpha 3 ; idx, vreg, vreg2
op clear_alpha%1
        LOAD_CONT tmp0q
        pcmpeqb %2, %2
IF V2,  mova %3, %2
        CONTINUE tmp0q
%endmacro

%macro clear_zero 3 ; idx, vreg, vreg2
op clear_zero%1
        LOAD_CONT tmp0q
        pxor %2, %2
IF V2,  mova %3, %2
        CONTINUE tmp0q
%endmacro

; note: the pattern is inverted for these functions; i.e. X=1 implies that we
; *keep* the X component, not that we clear it
%macro clear_generic 0
op clear
            LOAD_CONT tmp0q
%if avx_enabled
    IF !X,  vpbroadcastd mx, [implq + SwsOpImpl.priv + 0]
    IF !Y,  vpbroadcastd my, [implq + SwsOpImpl.priv + 4]
    IF !Z,  vpbroadcastd mz, [implq + SwsOpImpl.priv + 8]
    IF !W,  vpbroadcastd mw, [implq + SwsOpImpl.priv + 12]
%else ; !avx_enabled
    IF !X,  movd mx, [implq + SwsOpImpl.priv + 0]
    IF !Y,  movd my, [implq + SwsOpImpl.priv + 4]
    IF !Z,  movd mz, [implq + SwsOpImpl.priv + 8]
    IF !W,  movd mw, [implq + SwsOpImpl.priv + 12]
    IF !X,  pshufd mx, mx, 0
    IF !Y,  pshufd my, my, 0
    IF !Z,  pshufd mz, mz, 0
    IF !W,  pshufd mw, mw, 0
%endif
%if V2
    IF !X,  mova mx2, mx
    IF !Y,  mova my2, my
    IF !Z,  mova mz2, mz
    IF !W,  mova mw2, mw
%endif
            CONTINUE tmp0q
%endmacro

%macro clear_funcs 0
        decl_pattern 1, 1, 1, 0, clear_generic
        decl_pattern 0, 1, 1, 1, clear_generic
        decl_pattern 0, 0, 1, 1, clear_generic
        decl_pattern 1, 0, 0, 1, clear_generic
        decl_pattern 1, 1, 0, 0, clear_generic
        decl_pattern 0, 1, 0, 1, clear_generic
        decl_pattern 1, 0, 1, 0, clear_generic
        decl_pattern 1, 0, 0, 0, clear_generic
        decl_pattern 0, 1, 0, 0, clear_generic
        decl_pattern 0, 0, 1, 0, clear_generic
%endmacro

;---------------------------------------------------------
; Swizzling and duplicating

; mA := mB, mB := mC, ... mX := mA across both halves
%macro vrotate 2-* ; A, B, C, ...
    %rep %0
        %assign rot_a %1 + 4
        %assign rot_b %2 + 4
        mova m%1, m%2
        IF V2, mova m%[rot_a], m%[rot_b]
    %rotate 1
    %endrep
    %undef rot_a
    %undef rot_b
%endmacro

%macro swizzle_funcs 0
op swizzle_3012
    LOAD_CONT tmp0q
    vrotate 8, 0, 3, 2, 1
    CONTINUE tmp0q

op swizzle_3021
    LOAD_CONT tmp0q
    vrotate 8, 0, 3, 1
    CONTINUE tmp0q

op swizzle_2103
    LOAD_CONT tmp0q
    vrotate 8, 0, 2
    CONTINUE tmp0q

op swizzle_3210
    LOAD_CONT tmp0q
    vrotate 8, 0, 3
    vrotate 8, 1, 2
    CONTINUE tmp0q

op swizzle_3102
    LOAD_CONT tmp0q
    vrotate 8, 0, 3, 2
    CONTINUE tmp0q

op swizzle_3201
    LOAD_CONT tmp0q
    vrotate 8, 0, 3, 1, 2
    CONTINUE tmp0q

op swizzle_1203
    LOAD_CONT tmp0q
    vrotate 8, 0, 1, 2
    CONTINUE tmp0q

op swizzle_1023
    LOAD_CONT tmp0q
    vrotate 8, 0, 1
    CONTINUE tmp0q

op swizzle_2013
    LOAD_CONT tmp0q
    vrotate 8, 0, 2, 1
    CONTINUE tmp0q

op swizzle_2310
    LOAD_CONT tmp0q
    vrotate 8, 0, 2, 1, 3
    CONTINUE tmp0q

op swizzle_2130
    LOAD_CONT tmp0q
    vrotate 8, 0, 2, 3
    CONTINUE tmp0q

op swizzle_1230
    LOAD_CONT tmp0q
    vrotate 8, 0, 1, 2, 3
    CONTINUE tmp0q

op swizzle_1320
    LOAD_CONT tmp0q
    vrotate 8, 0, 1, 3
    CONTINUE tmp0q

op swizzle_0213
    LOAD_CONT tmp0q
    vrotate 8, 1, 2
    CONTINUE tmp0q

op swizzle_0231
    LOAD_CONT tmp0q
    vrotate 8, 1, 2, 3
    CONTINUE tmp0q

op swizzle_0312
    LOAD_CONT tmp0q
    vrotate 8, 1, 3, 2
    CONTINUE tmp0q

op swizzle_3120
    LOAD_CONT tmp0q
    vrotate 8, 0, 3
    CONTINUE tmp0q

op swizzle_0321
    LOAD_CONT tmp0q
    vrotate 8, 1, 3
    CONTINUE tmp0q

op swizzle_0003
    LOAD_CONT tmp0q
    mova my, mx
    mova mz, mx
%if V2
    mova my2, mx2
    mova mz2, mx2
%endif
    CONTINUE tmp0q

op swizzle_0001
    LOAD_CONT tmp0q
    mova mw, my
    mova mz, mx
    mova my, mx
%if V2
    mova mw2, my2
    mova mz2, mx2
    mova my2, mx2
%endif
    CONTINUE tmp0q

op swizzle_3000
    LOAD_CONT tmp0q
    mova my, mx
    mova mz, mx
    mova mx, mw
    mova mw, my
%if V2
    mova my2, mx2
    mova mz2, mx2
    mova mx2, mw2
    mova mw2, my2
%endif
    CONTINUE tmp0q

op swizzle_1000
    LOAD_CONT tmp0q
    mova mz, mx
    mova mw, mx
    mova mx, my
    mova my, mz
%if V2
    mova mz2, mx2
    mova mw2, mx2
    mova mx2, my2
    mova my2, mz2
%endif
    CONTINUE tmp0q
%endmacro

;---------------------------------------------------------
; Pixel type conversions

%macro conv8to16 1 ; type
op %1_U8_U16
            LOAD_CONT tmp0q
%if V2
    %if avx_enabled
    IF X,   vextracti128 xmx2, mx, 1
    IF Y,   vextracti128 xmy2, my, 1
    IF Z,   vextracti128 xmz2, mz, 1
    IF W,   vextracti128 xmw2, mw, 1
    %else
    IF X,   psrldq xmx2, mx, 8
    IF Y,   psrldq xmy2, my, 8
    IF Z,   psrldq xmz2, mz, 8
    IF W,   psrldq xmw2, mw, 8
    %endif
    IF X,   pmovzxbw mx2, xmx2
    IF Y,   pmovzxbw my2, xmy2
    IF Z,   pmovzxbw mz2, xmz2
    IF W,   pmovzxbw mw2, xmw2
%endif ; V2
    IF X,   pmovzxbw mx, xmx
    IF Y,   pmovzxbw my, xmy
    IF Z,   pmovzxbw mz, xmz
    IF W,   pmovzxbw mw, xmw

%ifidn %1, expand
            VBROADCASTI128 m8, [expand16_shuf]
    %if V2
    IF X,   pshufb mx2, m8
    IF Y,   pshufb my2, m8
    IF Z,   pshufb mz2, m8
    IF W,   pshufb mw2, m8
    %endif
    IF X,   pshufb mx, m8
    IF Y,   pshufb my, m8
    IF Z,   pshufb mz, m8
    IF W,   pshufb mw, m8
%endif ; expand
            CONTINUE tmp0q
%endmacro

%macro conv16to8 0
op convert_U16_U8
        LOAD_CONT tmp0q
%if V2
        ; this code technically works for the !V2 case as well, but slower
IF X,   packuswb mx, mx2
IF Y,   packuswb my, my2
IF Z,   packuswb mz, mz2
IF W,   packuswb mw, mw2
IF X,   vpermq mx, mx, q3120
IF Y,   vpermq my, my, q3120
IF Z,   vpermq mz, mz, q3120
IF W,   vpermq mw, mw, q3120
%else
IF X,   vextracti128  xm8, mx, 1
IF Y,   vextracti128  xm9, my, 1
IF Z,   vextracti128 xm10, mz, 1
IF W,   vextracti128 xm11, mw, 1
        vzeroupper
IF X,   packuswb xmx, xm8
IF Y,   packuswb xmy, xm9
IF Z,   packuswb xmz, xm10
IF W,   packuswb xmw, xm11
%endif
        CONTINUE tmp0q
%endmacro

%macro conv8to32 1 ; type
op %1_U8_U32
        LOAD_CONT tmp0q
IF X,   psrldq xmx2, xmx, 8
IF Y,   psrldq xmy2, xmy, 8
IF Z,   psrldq xmz2, xmz, 8
IF W,   psrldq xmw2, xmw, 8
IF X,   pmovzxbd mx, xmx
IF Y,   pmovzxbd my, xmy
IF Z,   pmovzxbd mz, xmz
IF W,   pmovzxbd mw, xmw
IF X,   pmovzxbd mx2, xmx2
IF Y,   pmovzxbd my2, xmy2
IF Z,   pmovzxbd mz2, xmz2
IF W,   pmovzxbd mw2, xmw2
%ifidn %1, expand
        VBROADCASTI128 m8, [expand32_shuf]
IF X,   pshufb mx, m8
IF Y,   pshufb my, m8
IF Z,   pshufb mz, m8
IF W,   pshufb mw, m8
IF X,   pshufb mx2, m8
IF Y,   pshufb my2, m8
IF Z,   pshufb mz2, m8
IF W,   pshufb mw2, m8
%endif ; expand
        CONTINUE tmp0q
%endmacro

%macro conv32to8 0
op convert_U32_U8
        LOAD_CONT tmp0q
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

%macro conv16to32 0
op convert_U16_U32
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
        CONTINUE tmp0q
%endmacro

%macro conv32to16 0
op convert_U32_U16
        LOAD_CONT tmp0q
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
; Shifting

%macro lshift16 0
op lshift16
        vmovq xm8, [implq + SwsOpImpl.priv]
        LOAD_CONT tmp0q
IF X,   psllw mx, xm8
IF Y,   psllw my, xm8
IF Z,   psllw mz, xm8
IF W,   psllw mw, xm8
%if V2
IF X,   psllw mx2, xm8
IF Y,   psllw my2, xm8
IF Z,   psllw mz2, xm8
IF W,   psllw mw2, xm8
%endif
        CONTINUE tmp0q
%endmacro

%macro rshift16 0
op rshift16
        vmovq xm8, [implq + SwsOpImpl.priv]
        LOAD_CONT tmp0q
IF X,   psrlw mx, xm8
IF Y,   psrlw my, xm8
IF Z,   psrlw mz, xm8
IF W,   psrlw mw, xm8
%if V2
IF X,   psrlw mx2, xm8
IF Y,   psrlw my2, xm8
IF Z,   psrlw mz2, xm8
IF W,   psrlw mw2, xm8
%endif
        CONTINUE tmp0q
%endmacro

;---------------------------------------------------------
; Macro instantiations for kernel functions

%macro funcs_u8 0
    read_planar 1
    read_planar 2
    read_planar 3
    read_planar 4
    write_planar 1
    write_planar 2
    write_planar 3
    write_planar 4

    rw_packed 8
    read_nibbles
    read_bits
    write_bits

    pack_generic 1, 2, 1
    pack_generic 3, 3, 2
    pack_generic 2, 3, 3
    unpack8 1, 2, 1
    unpack8 3, 3, 2
    unpack8 2, 3, 3

    clear_alpha 0, mx, mx2
    clear_alpha 1, my, my2
    clear_alpha 3, mw, mw2
    clear_zero  0, mx, mx2
    clear_zero  1, my, my2
    clear_zero  3, mw, mw2
    clear_funcs
    swizzle_funcs

    decl_common_patterns shuffle
%endmacro

%macro funcs_u16 0
    rw_packed 16
    pack_generic  4, 4, 4
    pack_generic  5, 5, 5
    pack_generic  5, 6, 5
    unpack w, 16, 4, 4, 4
    unpack w, 16, 5, 5, 5
    unpack w, 16, 5, 6, 5
    decl_common_patterns conv8to16 convert
    decl_common_patterns conv8to16 expand
    decl_common_patterns conv16to8
    decl_common_patterns lshift16
    decl_common_patterns rshift16
%endmacro

INIT_XMM sse4
decl_v2 0, funcs_u8
decl_v2 1, funcs_u8

INIT_YMM avx2
decl_v2 0, funcs_u8
decl_v2 1, funcs_u8
decl_v2 0, funcs_u16
decl_v2 1, funcs_u16

INIT_YMM avx2
decl_v2 1, rw_packed 32
decl_v2 1, pack_generic  10, 10, 10,  2
decl_v2 1, pack_generic   2, 10, 10, 10
decl_v2 1, unpack d, 32, 10, 10, 10,  2
decl_v2 1, unpack d, 32,  2, 10, 10, 10
decl_common_patterns conv8to32 convert
decl_common_patterns conv8to32 expand
decl_common_patterns conv32to8
decl_common_patterns conv16to32
decl_common_patterns conv32to16
