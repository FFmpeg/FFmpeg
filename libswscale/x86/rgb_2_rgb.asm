;******************************************************************************
;* Copyright Nick Kurshev
;* Copyright Michael (michaelni@gmx.at)
;* Copyright 2018 Jokyo Images
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

SECTION_RODATA

pb_shuffle2103: db 2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15
pb_shuffle0321: db 0, 3, 2, 1, 4, 7, 6, 5, 8, 11, 10, 9, 12, 15, 14, 13
pb_shuffle1230: db 1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12
pb_shuffle3012: db 3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14
pb_shuffle3210: db 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12

SECTION .text

;------------------------------------------------------------------------------
; shuffle_bytes_## (const uint8_t *src, uint8_t *dst, int src_size)
;------------------------------------------------------------------------------
; %1-4 index shuffle
%macro SHUFFLE_BYTES 4
cglobal shuffle_bytes_%1%2%3%4, 3, 5, 2, src, dst, w, tmp, x
    VBROADCASTI128    m0, [pb_shuffle%1%2%3%4]
    movsxdifnidn wq, wd
    mov xq, wq

    add        srcq, wq
    add        dstq, wq
    neg          wq

;calc scalar loop
    and xq, mmsize-4
    je .loop_simd

.loop_scalar:
   mov          tmpb, [srcq + wq + %1]
   mov [dstq+wq + 0], tmpb
   mov          tmpb, [srcq + wq + %2]
   mov [dstq+wq + 1], tmpb
   mov          tmpb, [srcq + wq + %3]
   mov [dstq+wq + 2], tmpb
   mov          tmpb, [srcq + wq + %4]
   mov [dstq+wq + 3], tmpb
   add            wq, 4
   sub            xq, 4
   jg .loop_scalar

;check if src_size < mmsize
cmp wq, 0
jge .end

.loop_simd:
    movu           m1, [srcq+wq]
    pshufb         m1, m0
    movu    [dstq+wq], m1
    add            wq, mmsize
    jl .loop_simd

.end:
    RET
%endmacro

INIT_XMM ssse3
SHUFFLE_BYTES 2, 1, 0, 3
SHUFFLE_BYTES 0, 3, 2, 1
SHUFFLE_BYTES 1, 2, 3, 0
SHUFFLE_BYTES 3, 0, 1, 2
SHUFFLE_BYTES 3, 2, 1, 0
