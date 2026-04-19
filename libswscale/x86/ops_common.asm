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

%include "ops_include.asm"

SECTION .text

;---------------------------------------------------------
; Global entry point. See `ops_include.asm` for info.

%macro process_fn 1 ; number of planes
cglobal sws_process%1_x86, 6, 7 + 2 * %1, 16
            ; Args:
            ;   execq, implq, bxd, yd as defined in ops_include.asm
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
            movsxdifnidn bxq, bxd
            movsxdifnidn yq, yd

            ; load plane pointers
            mov in0q,  [execq + SwsOpExec.in0]
IF %1 > 1,  mov in1q,  [execq + SwsOpExec.in1]
IF %1 > 2,  mov in2q,  [execq + SwsOpExec.in2]
IF %1 > 3,  mov in3q,  [execq + SwsOpExec.in3]
            mov out0q, [execq + SwsOpExec.out0]
IF %1 > 1,  mov out1q, [execq + SwsOpExec.out1]
IF %1 > 2,  mov out2q, [execq + SwsOpExec.out2]
IF %1 > 3,  mov out3q, [execq + SwsOpExec.out3]
.loop:
            call [rsp] ; call into op chain
            mov implq, [rsp + 8]
            inc bxd
            cmp bxd, [rsp + 20]
            jne .loop
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
            ; conditionally apply y bump (if non-NULL)
            mov tmp0q, [execq + SwsOpExec.in_bump_y]
            test tmp0q, tmp0q
            jz .loop
            movsxd tmp0q, [tmp0q + yq * 4 - 4] ; load (signed) y bump
%if %1 > 3
            mov tmp1q, tmp0q
            imul tmp1q, [execq + SwsOpExec.in_stride3]
            add in3q, tmp1q
%endif
%if %1 > 2
            mov tmp1q, tmp0q
            imul tmp1q, [execq + SwsOpExec.in_stride2]
            add in2q, tmp1q
%endif
%if %1 > 1
            mov tmp1q, tmp0q
            imul tmp1q, [execq + SwsOpExec.in_stride1]
            add in1q, tmp1q
%endif
            imul tmp0q, [execq + SwsOpExec.in_stride0]
            add in0q, tmp0q
            jmp .loop
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

%macro MOVSIZE 3 ; size, dst, src
    %if %1 <= 4
        movd %2, %3
    %elif %1 <= 8
        movq %2, %3
    %else
        movu %2, %3
    %endif
%endmacro

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
            MOVSIZE %1, m0, [srcq + srcidxq]
            pshufb m0, m1
            MOVSIZE %2, [dstq + dstidxq], m0
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
packed_shuffle 15,  5 ; 24 -> 8
packed_shuffle 15, 15 ; 24 -> 24
packed_shuffle 12, 16 ; 24 -> 32
packed_shuffle  6, 12 ; 24 -> 48
packed_shuffle 16,  4 ; 32 -> 8,  64 -> 16
packed_shuffle 16, 12 ; 32 -> 24, 64 -> 48
packed_shuffle 16, 16 ; 32 -> 32, 64 -> 64
packed_shuffle  8, 12 ; 32 -> 48
packed_shuffle 12, 12 ; 48 -> 48

INIT_YMM avx2
packed_shuffle 32, 32

INIT_ZMM avx512
packed_shuffle 64, 64
