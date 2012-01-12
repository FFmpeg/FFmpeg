;******************************************************************************
;* x86-optimized input routines; does shuffling of packed
;* YUV formats into individual planes, and converts RGB
;* into YUV planes also.
;* Copyright (c) 2012 Ronald S. Bultje <rsbultje@gmail.com>
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

SECTION_RODATA

SECTION .text

;-----------------------------------------------------------------------------
; YUYV/UYVY/NV12/NV21 packed pixel shuffling.
;
; void <fmt>ToY_<opt>(uint8_t *dst, const uint8_t *src, int w);
; and
; void <fmt>toUV_<opt>(uint8_t *dstU, uint8_t *dstV, const uint8_t *src,
;                      const uint8_t *unused, int w);
;-----------------------------------------------------------------------------

; %1 = a (aligned) or u (unaligned)
; %2 = yuyv or uyvy
%macro LOOP_YUYV_TO_Y 2
.loop_%1:
    mov%1          m0, [srcq+wq*2]        ; (byte) { Y0, U0, Y1, V0, ... }
    mov%1          m1, [srcq+wq*2+mmsize] ; (byte) { Y8, U4, Y9, V4, ... }
%ifidn %2, yuyv
    pand           m0, m2                 ; (word) { Y0, Y1, ..., Y7 }
    pand           m1, m2                 ; (word) { Y8, Y9, ..., Y15 }
%else ; uyvy
    psrlw          m0, 8                  ; (word) { Y0, Y1, ..., Y7 }
    psrlw          m1, 8                  ; (word) { Y8, Y9, ..., Y15 }
%endif ; yuyv/uyvy
    packuswb       m0, m1                 ; (byte) { Y0, ..., Y15 }
    mova    [dstq+wq], m0
    add            wq, mmsize
    jl .loop_%1
    REP_RET
%endmacro

; %1 = nr. of XMM registers
; %2 = yuyv or uyvy
; %3 = if specified, it means that unaligned and aligned code in loop
;      will be the same (i.e. YUYV+AVX), and thus we don't need to
;      split the loop in an aligned and unaligned case
%macro YUYV_TO_Y_FN 2-3
cglobal %2ToY, 5, 5, %1, dst, unused0, unused1, src, w
%ifdef ARCH_X86_64
    movsxd         wq, wd
%endif
    add          dstq, wq
%if mmsize == 16
    test         srcq, 15
%endif
    lea          srcq, [srcq+wq*2]
%ifidn %2, yuyv
    pcmpeqb        m2, m2                 ; (byte) { 0xff } x 16
    psrlw          m2, 8                  ; (word) { 0x00ff } x 8
%endif ; yuyv
%if mmsize == 16
    jnz .loop_u_start
    neg            wq
    LOOP_YUYV_TO_Y  a, %2
.loop_u_start:
    neg            wq
    LOOP_YUYV_TO_Y  u, %2
%else ; mmsize == 8
    neg            wq
    LOOP_YUYV_TO_Y  a, %2
%endif ; mmsize == 8/16
%endmacro

; %1 = a (aligned) or u (unaligned)
; %2 = yuyv or uyvy
%macro LOOP_YUYV_TO_UV 2
.loop_%1:
%ifidn %2, yuyv
    mov%1          m0, [srcq+wq*4]        ; (byte) { Y0, U0, Y1, V0, ... }
    mov%1          m1, [srcq+wq*4+mmsize] ; (byte) { Y8, U4, Y9, V4, ... }
    psrlw          m0, 8                  ; (word) { U0, V0, ..., U3, V3 }
    psrlw          m1, 8                  ; (word) { U4, V4, ..., U7, V7 }
%else ; uyvy
%if cpuflag(avx)
    vpand          m0, m2, [srcq+wq*4]        ; (word) { U0, V0, ..., U3, V3 }
    vpand          m1, m2, [srcq+wq*4+mmsize] ; (word) { U4, V4, ..., U7, V7 }
%else
    mov%1          m0, [srcq+wq*4]        ; (byte) { Y0, U0, Y1, V0, ... }
    mov%1          m1, [srcq+wq*4+mmsize] ; (byte) { Y8, U4, Y9, V4, ... }
    pand           m0, m2                 ; (word) { U0, V0, ..., U3, V3 }
    pand           m1, m2                 ; (word) { U4, V4, ..., U7, V7 }
%endif
%endif ; yuyv/uyvy
    packuswb       m0, m1                 ; (byte) { U0, V0, ..., U7, V7 }
    pand           m1, m0, m2             ; (word) { U0, U1, ..., U7 }
    psrlw          m0, 8                  ; (word) { V0, V1, ..., V7 }
%if mmsize == 16
    packuswb       m1, m0                 ; (byte) { U0, ... U7, V1, ... V7 }
    movh   [dstUq+wq], m1
    movhps [dstVq+wq], m1
%else ; mmsize == 8
    packuswb       m1, m1                 ; (byte) { U0, ... U3 }
    packuswb       m0, m0                 ; (byte) { V0, ... V3 }
    movh   [dstUq+wq], m1
    movh   [dstVq+wq], m0
%endif ; mmsize == 8/16
    add            wq, mmsize / 2
    jl .loop_%1
    REP_RET
%endmacro

; %1 = nr. of XMM registers
; %2 = yuyv or uyvy
; %3 = if specified, it means that unaligned and aligned code in loop
;      will be the same (i.e. UYVY+AVX), and thus we don't need to
;      split the loop in an aligned and unaligned case
%macro YUYV_TO_UV_FN 2-3
cglobal %2ToUV, 4, 5, %1, dstU, dstV, unused, src, w
%ifdef ARCH_X86_64
    movsxd         wq, dword r5m
%else ; x86-32
    mov            wq, r5m
%endif
    add         dstUq, wq
    add         dstVq, wq
%if mmsize == 16 && %0 == 2
    test         srcq, 15
%endif
    lea          srcq, [srcq+wq*4]
    pcmpeqb        m2, m2                 ; (byte) { 0xff } x 16
    psrlw          m2, 8                  ; (word) { 0x00ff } x 8
    ; NOTE: if uyvy+avx, u/a are identical
%if mmsize == 16 && %0 == 2
    jnz .loop_u_start
    neg            wq
    LOOP_YUYV_TO_UV a, %2
.loop_u_start:
    neg            wq
    LOOP_YUYV_TO_UV u, %2
%else ; mmsize == 8
    neg            wq
    LOOP_YUYV_TO_UV a, %2
%endif ; mmsize == 8/16
%endmacro

; %1 = a (aligned) or u (unaligned)
; %2 = nv12 or nv21
%macro LOOP_NVXX_TO_UV 2
.loop_%1:
    mov%1          m0, [srcq+wq*2]        ; (byte) { U0, V0, U1, V1, ... }
    mov%1          m1, [srcq+wq*2+mmsize] ; (byte) { U8, V8, U9, V9, ... }
    pand           m2, m0, m5             ; (word) { U0, U1, ..., U7 }
    pand           m3, m1, m5             ; (word) { U8, U9, ..., U15 }
    psrlw          m0, 8                  ; (word) { V0, V1, ..., V7 }
    psrlw          m1, 8                  ; (word) { V8, V9, ..., V15 }
    packuswb       m2, m3                 ; (byte) { U0, ..., U15 }
    packuswb       m0, m1                 ; (byte) { V0, ..., V15 }
%ifidn %2, nv12
    mova   [dstUq+wq], m2
    mova   [dstVq+wq], m0
%else ; nv21
    mova   [dstVq+wq], m2
    mova   [dstUq+wq], m0
%endif ; nv12/21
    add            wq, mmsize
    jl .loop_%1
    REP_RET
%endmacro

; %1 = nr. of XMM registers
; %2 = nv12 or nv21
%macro NVXX_TO_UV_FN 2
cglobal %2ToUV, 4, 5, %1, dstU, dstV, unused, src, w
%ifdef ARCH_X86_64
    movsxd         wq, dword r5m
%else ; x86-32
    mov            wq, r5m
%endif
    add         dstUq, wq
    add         dstVq, wq
%if mmsize == 16
    test         srcq, 15
%endif
    lea          srcq, [srcq+wq*2]
    pcmpeqb        m5, m5                 ; (byte) { 0xff } x 16
    psrlw          m5, 8                  ; (word) { 0x00ff } x 8
%if mmsize == 16
    jnz .loop_u_start
    neg            wq
    LOOP_NVXX_TO_UV a, %2
.loop_u_start:
    neg            wq
    LOOP_NVXX_TO_UV u, %2
%else ; mmsize == 8
    neg            wq
    LOOP_NVXX_TO_UV a, %2
%endif ; mmsize == 8/16
%endmacro

%ifdef ARCH_X86_32
INIT_MMX mmx
YUYV_TO_Y_FN  0, yuyv
YUYV_TO_Y_FN  0, uyvy
YUYV_TO_UV_FN 0, yuyv
YUYV_TO_UV_FN 0, uyvy
NVXX_TO_UV_FN 0, nv12
NVXX_TO_UV_FN 0, nv21
%endif

INIT_XMM sse2
YUYV_TO_Y_FN  3, yuyv
YUYV_TO_Y_FN  2, uyvy
YUYV_TO_UV_FN 3, yuyv
YUYV_TO_UV_FN 3, uyvy
NVXX_TO_UV_FN 5, nv12
NVXX_TO_UV_FN 5, nv21

%ifdef HAVE_AVX
INIT_XMM avx
; in theory, we could write a yuy2-to-y using vpand (i.e. AVX), but
; that's not faster in practice
YUYV_TO_UV_FN 3, yuyv
YUYV_TO_UV_FN 3, uyvy, 1
NVXX_TO_UV_FN 5, nv12
NVXX_TO_UV_FN 5, nv21
%endif
