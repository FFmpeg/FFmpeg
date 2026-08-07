;******************************************************************************
;* SIMD-optimized HEVC intra prediction
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

SECTION_RODATA 16

; planar weights: (size - 1 - x) is a suffix of pw_desc, (x + 1) a prefix of pw_asc
pw_desc: dw 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16
         dw 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0
pw_asc:  dw  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16
         dw 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32

; pshufb masks: zero-extend bytes to words without a dedicated zero register
pb_bcastw0: times 8 db 0, -1            ; byte 0 -> all eight words
pb_widen:   db 0, -1, 1, -1, 2, -1, 3, -1, 4, -1, 5, -1, 6, -1, 7, -1

; 4x4-specific: two rows per register
pb_top4dup: db 0, -1, 1, -1, 2, -1, 3, -1, 0, -1, 1, -1, 2, -1, 3, -1
pb_left01:  db 0, -1, 0, -1, 0, -1, 0, -1, 1, -1, 1, -1, 1, -1, 1, -1
pb_left23:  db 2, -1, 2, -1, 2, -1, 2, -1, 3, -1, 3, -1, 3, -1, 3, -1
pw_planar4_a:   dw 3, 2, 1, 0, 3, 2, 1, 0   ; size - 1 - x
pw_planar4_b:   dw 1, 2, 3, 4, 1, 2, 3, 4   ; x + 1
pw_planar4_vy0: dw 3, 3, 3, 3, 2, 2, 2, 2   ; size - 1 - y, rows 0-1
pw_planar4_vy1: dw 1, 1, 1, 1, 0, 0, 0, 0   ; rows 2-3
pw_planar4_yb0: dw 1, 1, 1, 1, 2, 2, 2, 2   ; y + 1, rows 0-1
pw_planar4_yb1: dw 3, 3, 3, 3, 4, 4, 4, 4   ; rows 2-3

cextern pw_4
cextern pw_8
cextern pw_16
cextern pw_32

SECTION .text

;-----------------------------------------------------------------------------
; void ff_hevc_pred_planar_<idx>_8(uint8_t *src, const uint8_t *top,
;                                  const uint8_t *left, ptrdiff_t stride)
;-----------------------------------------------------------------------------

; src[y*stride + x] = ((size-1-x)*left[y] + (x+1)*top[size] +
;                      (size-1-y)*top[x]  + (y+1)*left[size] + size) >> (log2 + 1)
;
; Every term but (size-1-x)*left[y] is linear in y, so for eight columns they
; collapse into one accumulator with the loop-invariant step left[size]-top[x].
; Running columns outermost keeps both in registers: a row costs one pmullw and
; two paddw with no reload of top[]. Words suffice, as each coefficient pair
; sums to size and the largest value is 2*32*255 + 32 = 16352 < 2^15.

; Set up the running accumulator for the eight columns starting at %6.
; (size-1)*top[x] is formed as (top[x] << log2) - top[x] so that no vector of
; size-1 has to be materialised.
; %1 = sum, %2 = delta, %3 = temp, %4 = size, %5 = index, %6 = first column
%macro PLANAR_INIT 6
    movd            %1, [topq + %4]
    pshufb          %1, [pb_bcastw0]        ; top[size]
    pmullw          %1, [pw_asc + (%6) * 2] ; (x+1)*top[size]
    movd            %2, [leftq + %4]
    pshufb          %2, [pb_bcastw0]        ; left[size]
    paddw           %1, %2
    paddw           %1, [pw_ %+ %4]         ; + left[size] + size
    movq            %3, [topq + (%6)]
    pshufb          %3, [pb_widen]          ; top[x]
    psubw           %2, %3                  ; delta = left[size] - top[x]
    psubw           %1, %3
    psllw           %3, (%5) + 2
    paddw           %1, %3                  ; + (size-1)*top[x] = sum(0)
%endmacro

; One group of eight columns, for the block size that is only eight wide.
; %1 = block size, %2 = index, %3 = first column
%macro PLANAR_CHUNK 3
    PLANAR_INIT     m0, m1, m2, %1, %2, %3
    lea           srcq, [baseq + %3]
    xor             yd, yd
%%loop:
    movd            m2, [leftq + yq]
    pshufb          m2, [pb_bcastw0]                    ; left[y]
    pmullw          m2, [pw_desc + (32 - %1 + %3) * 2]  ; (size-1-x)*left[y]
    paddw           m2, m0
    psrlw           m2, %2 + 3
    packuswb        m2, m2
    movq        [srcq], m2
    add           srcq, strideq
    paddw           m0, m1
    inc             yd
    cmp             yd, %1
    jl %%loop
%endmacro

; Two adjacent groups at once, so that a row is written with a single 16 byte
; store and the left[y] broadcast is shared between them.
; %1 = block size, %2 = index, %3 = first column
%macro PLANAR_CHUNK2 3
    PLANAR_INIT     m0, m1, m4, %1, %2, %3
    PLANAR_INIT     m2, m3, m4, %1, %2, %3 + 8
    lea           srcq, [baseq + %3]
    xor             yd, yd
%%loop:
    movd            m4, [leftq + yq]
    pshufb          m4, [pb_bcastw0]                        ; left[y]
    mova            m5, m4
    pmullw          m4, [pw_desc + (32 - %1 + %3) * 2]
    paddw           m4, m0
    psrlw           m4, %2 + 3
    pmullw          m5, [pw_desc + (32 - %1 + %3 + 8) * 2]
    paddw           m5, m2
    psrlw           m5, %2 + 3
    packuswb        m4, m5
    movu        [srcq], m4
    add           srcq, strideq
    paddw           m0, m1
    paddw           m2, m3
    inc             yd
    cmp             yd, %1
    jl %%loop
%endmacro

; %1 = block size, %2 = pred_planar index (log2(size) - 2)
%macro PRED_PLANAR 2
cglobal hevc_pred_planar_%2_8, 4, 6, 6, src, top, left, stride, base, y
    mov          baseq, srcq
%if %1 == 8
    PLANAR_CHUNK %1, %2, 0
%else
%assign %%off 0
%rep %1 / 16
    PLANAR_CHUNK2 %1, %2, %%off
%assign %%off %%off + 16
%endrep
%endif
    RET
%endmacro

; 4x4: fully unrolled, two rows per register
%macro PRED_PLANAR4 0
cglobal hevc_pred_planar_0_8, 4, 4, 5, src, top, left, stride
    movd            m0, [topq + 4]
    pshufb          m0, [pb_bcastw0]        ; top[4]
    pmullw          m0, [pw_planar4_b]      ; (x+1)*top[4]
    mova            m1, m0
    movd            m2, [leftq + 4]
    pshufb          m2, [pb_bcastw0]        ; left[4]
    mova            m3, m2
    pmullw          m3, [pw_planar4_yb0]
    paddw           m0, m3
    paddw           m0, [pw_4]              ; row-invariant terms, rows 0-1
    pmullw          m2, [pw_planar4_yb1]
    paddw           m1, m2
    paddw           m1, [pw_4]              ; rows 2-3
    movd            m2, [topq]
    pshufb          m2, [pb_top4dup]        ; top[0..3], both halves
    movd            m3, [leftq]
    mova            m4, m3
    pshufb          m3, [pb_left01]         ; left[0] x4 | left[1] x4
    pshufb          m4, [pb_left23]         ; left[2] x4 | left[3] x4
    pmullw          m3, [pw_planar4_a]
    paddw           m3, m0
    mova            m0, m2
    pmullw          m0, [pw_planar4_vy0]
    paddw           m3, m0
    psrlw           m3, 3
    pmullw          m4, [pw_planar4_a]
    paddw           m4, m1
    pmullw          m2, [pw_planar4_vy1]
    paddw           m4, m2
    psrlw           m4, 3
    packuswb        m3, m4
    movd        [srcq], m3
    psrldq          m3, 4
    movd [srcq + strideq], m3
    lea           srcq, [srcq + strideq * 2]
    psrldq          m3, 4
    movd        [srcq], m3
    psrldq          m3, 4
    movd [srcq + strideq], m3
    RET
%endmacro

INIT_XMM ssse3
PRED_PLANAR4
PRED_PLANAR  8, 1
PRED_PLANAR 16, 2
PRED_PLANAR 32, 3
