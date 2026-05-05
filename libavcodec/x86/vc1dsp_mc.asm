;******************************************************************************
;* VC1 motion compensation optimizations
;* Copyright (c) 2007 Christophe GISQUET <christophe.gisquet@free.fr>
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

pb_m4_18: times 8 db -4, 18
pb_53_m3: times 8 db 53, -3
pb_m3_53: times 8 db -3, 53
pb_18_m4: times 8 db 18, -4
pb_m4_36: times 8 db -4, 36
pb_36_m4: times 8 db 36, -4
pb_m4_53: times 8 db -4, 53
pb_m3_18: times 8 db -3, 18

cextern pw_9
cextern pw_128

SECTION .text

%if HAVE_MMX_INLINE

; XXX some of these macros are not used right now, but they will in the future
;     when more functions are ported.

%macro OP_PUT 2 ; dst, src
%endmacro

%macro OP_AVG 2 ; dst, src
    pavgb           %1, %2
%endmacro

%macro NORMALIZE_MMX 1 ; shift
    paddw           m3, m7 ; +bias-r
    paddw           m4, m7 ; +bias-r
    psraw           m3, %1
    psraw           m4, %1
%endmacro

%macro TRANSFER_DO_PACK 2 ; op, dst
    packuswb        m3, m4
    %1              m3, [%2]
    mova          [%2], m3
%endmacro

%macro TRANSFER_DONT_PACK 2 ; op, dst
    %1              m3, [%2]
    %1              m3, [%2 + mmsize]
    mova          [%2], m3
    mova [mmsize + %2], m4
%endmacro

; see MSPEL_FILTER13_CORE for use as UNPACK macro
%macro DO_UNPACK 1 ; reg
    punpcklbw       %1, m0
%endmacro
%macro DONT_UNPACK 1 ; reg
%endmacro

; Compute the rounder 32-r or 8-r and unpacks it to m7
%macro LOAD_ROUNDER_MMX 1 ; round
    movd      m7, %1
    punpcklwd m7, m7
    punpckldq m7, m7
%endmacro

%macro SHIFT2_LINE 5 ; off, r0, r1, r2, r3
    paddw          m%3, m%4
    movh           m%2, [srcq + stride_neg2]
    pmullw         m%3, m6
    punpcklbw      m%2, m0
    movh           m%5, [srcq + strideq]
    psubw          m%3, m%2
    punpcklbw      m%5, m0
    paddw          m%3, m7
    psubw          m%3, m%5
    psraw          m%3, shift
    movu   [dstq + %1], m%3
    add           srcq, strideq
%endmacro

INIT_MMX mmx
; void ff_vc1_put_ver_16b_shift2_mmx(int16_t *dst, const uint8_t *src,
;                                    x86_reg stride, int rnd, int64_t shift)
; Sacrificing m6 makes it possible to pipeline loads from src
%if ARCH_X86_32
cglobal vc1_put_ver_16b_shift2, 3,6,0, dst, src, stride
    DECLARE_REG_TMP     3, 4, 5
    %define rnd r3mp
    %define shift qword r4m
%else ; X86_64
cglobal vc1_put_ver_16b_shift2, 4,7,0, dst, src, stride
    DECLARE_REG_TMP     4, 5, 6
    %define   rnd r3d
    ; We need shift either in memory or in a mm reg as it's used in psraw
    ; On WIN64, the arg is already on the stack
    ; On UNIX64, m5 doesn't seem to be used
%if WIN64
    %define shift r4mp
%else ; UNIX64
    %define shift m5
    mova shift, r4q
%endif ; WIN64
%endif ; X86_32
%define stride_neg2 t0q
%define stride_9minus4 t1q
%define i t2q
    mov       stride_neg2, strideq
    neg       stride_neg2
    add       stride_neg2, stride_neg2
    lea    stride_9minus4, [strideq * 9 - 4]
    mov                 i, 3
    LOAD_ROUNDER_MMX  rnd
    mova               m6, [pw_9]
    pxor               m0, m0
.loop:
    movh               m2, [srcq]
    add              srcq, strideq
    movh               m3, [srcq]
    punpcklbw          m2, m0
    punpcklbw          m3, m0
    SHIFT2_LINE         0, 1, 2, 3, 4
    SHIFT2_LINE        24, 2, 3, 4, 1
    SHIFT2_LINE        48, 3, 4, 1, 2
    SHIFT2_LINE        72, 4, 1, 2, 3
    SHIFT2_LINE        96, 1, 2, 3, 4
    SHIFT2_LINE       120, 2, 3, 4, 1
    SHIFT2_LINE       144, 3, 4, 1, 2
    SHIFT2_LINE       168, 4, 1, 2, 3
    sub              srcq, stride_9minus4
    add              dstq, 8
    dec                 i
        jnz         .loop
    RET
%undef rnd
%undef shift
%undef stride_neg2
%undef stride_9minus4
%undef i

; void ff_vc1_*_hor_16b_shift2_mmx(uint8_t *dst, x86_reg stride,
;                                  const int16_t *src, int rnd);
; Data is already unpacked, so some operations can directly be made from
; memory.
%macro HOR_16B_SHIFT2 2 ; op, opname
cglobal vc1_%2_hor_16b_shift2, 4, 5, 0, dst, stride, src, rnd, h
    mov                hq, 8
    sub              srcq, 2
    sub              rndd, (-1+9+9-1) * 1024 ; add -1024 bias
    LOAD_ROUNDER_MMX rndd
    mova               m5, [pw_9]
    mova               m6, [pw_128]

.loop:
    mova               m1, [srcq + 2 * 0]
    mova               m2, [srcq + 2 * 0 + mmsize]
    mova               m3, [srcq + 2 * 1]
    mova               m4, [srcq + 2 * 1 + mmsize]
    paddw              m3, [srcq + 2 * 2]
    paddw              m4, [srcq + 2 * 2 + mmsize]
    paddw              m1, [srcq + 2 * 3]
    paddw              m2, [srcq + 2 * 3 + mmsize]
    pmullw             m3, m5
    pmullw             m4, m5
    psubw              m3, m1
    psubw              m4, m2
    NORMALIZE_MMX      7
    ; remove bias
    paddw              m3, m6
    paddw              m4, m6
    TRANSFER_DO_PACK   %1, dstq
    add              srcq, 24
    add              dstq, strideq
    dec                hq
        jnz         .loop

    RET
%endmacro

INIT_MMX mmx
HOR_16B_SHIFT2 OP_PUT, put

INIT_MMX mmxext
HOR_16B_SHIFT2 OP_AVG, avg
%endif ; HAVE_MMX_INLINE

%define MOV8  movq
%define MOV16 movu

INIT_XMM ssse3
%macro HOR_8B 2

cglobal vc1_%1_mspel_mc10_%2, 4, 4, 6, dst, src, stride, rnd
    mova              m1, [pb_m4_53]
    mova              m2, [pb_m3_18]
    sub             rndd, 32
    jmp               vc1_%1_mspel_mc30_%2_after_prologue

cglobal vc1_%1_mspel_mc20_%2, 4, 4, 6, dst, src, stride, rnd
    mova              m1, [pb_m4_36]
    lea             rndd, [4*rndd-32]
    mova              m2, m1
    jmp               vc1_%1_mspel_mc30_%2_after_prologue

cglobal vc1_%1_mspel_mc30_%2, 4, 4, 6, dst, src, stride, rnd
    mova              m2, [pb_m4_53]
    mova              m1, [pb_m3_18]
    sub             rndd, 32

vc1_%1_mspel_mc30_%2_after_prologue:
    movd              m0, rndd
    WIN64_SPILL_XMM    7+(%2>>4)
%define hd  rndd
    mov               hd, %2
    SPLATW            m0, m0
.loop:
    MOV%2             m3, [srcq-1]
    MOV%2             m4, [srcq]
    MOV%2             m5, [srcq+1]
    MOV%2             m6, [srcq+2]

%if %2 == 8
    punpcklbw         m3, m4
    pmaddubsw         m3, m1
%ifidn %1,avg
    movq              m4, [dstq]
%endif
    punpcklbw         m6, m5
    pmaddubsw         m6, m2
    add             srcq, strideq
    psubw             m3, m0
    paddw             m3, m6
    psraw             m3, 6
    packuswb          m3, m3
%ifidn %1,avg
    pavgb             m3, m4
%endif
    movq          [dstq], m3
%else
    SBUTTERFLY        bw, 3, 4, 7
    pmaddubsw         m3, m1
    pmaddubsw         m4, m1
    SBUTTERFLY        bw, 6, 5, 7
    pmaddubsw         m6, m2
    pmaddubsw         m5, m2
    add             srcq, strideq
    psubw             m3, m0
    psubw             m4, m0
    paddw             m3, m6
    paddw             m4, m5
    psraw             m3, 6
    psraw             m4, 6
    packuswb          m3, m4
%ifidn %1, avg
    pavgb             m3, [dstq]
%endif
    mova          [dstq], m3
%endif
    add             dstq, strideq
    dec               hd
    jnz            .loop
    RET
%endmacro

HOR_8B put, 8
HOR_8B avg, 8

HOR_8B put, 16
HOR_8B avg, 16

%macro SETUP_COEFFS 3 ; width, coeff1, coeff2
    ASSERT (%3-%2 == 16)
%if ARCH_X86_64 || (%1 == 8)
    mova          m1, [%2]
    mova          m2, [%3]
%define COEFF0 m1
%define COEFF1 m2
%define M8 m8
%define M9 m9
%else
    lea           r4, [%2]
%define COEFF0 [r4]
%define COEFF1 [r4+(%3-%2)]
%define M8 m1
%define M9 m2
%endif
%endmacro

%macro VER_8B 2
cglobal vc1_%1_mspel_mc01_%2, 4, 4+ARCH_X86_32*(%2>>4), 6, dst, src, stride, rnd
    SETUP_COEFFS %2, pb_m4_18, pb_53_m3
    add             rndd, 31
    jmp               vc1_%1_mspel_mc03_%2_after_prologue

cglobal vc1_%1_mspel_mc02_%2, 4, 4+ARCH_X86_32*(%2>>4), 6, dst, src, stride, rnd
    SETUP_COEFFS %2, pb_m4_36, pb_36_m4
    lea             rndd, [4*rndd+28]
    jmp               vc1_%1_mspel_mc03_%2_after_prologue

cglobal vc1_%1_mspel_mc03_%2, 4, 4+ARCH_X86_32*(%2>>4), 6, dst, src, stride, rnd
    SETUP_COEFFS %2, pb_m3_53, pb_18_m4
    add             rndd, 31

vc1_%1_mspel_mc03_%2_after_prologue:
    neg          strideq
    movd              m0, rndd
    WIN64_SPILL_XMM    8, 8+3*(%2>>4)
    MOV%2             m3, [srcq+strideq]
    neg          strideq
    MOV%2             m4, [srcq]
    MOV%2             m5, [srcq+strideq]
    SPLATW            m0, m0
%if %2 == 16
    WIN64_PUSH_XMM    11, 8
%endif
    lea             srcq, [srcq+2*strideq]
%define hd  rndd
%if %2 == 8
    punpcklbw         m3, m5
%else
    punpcklbw         m7, m3, m5
    punpckhbw         m3, m5
%endif
    mov               hd, %2

.loop:
    MOV%2             m6, [srcq]
%if %2 == 8
    pmaddubsw         m3, m1
    punpcklbw         m4, m6
    pmaddubsw         m7, m4, m2
    paddw             m3, m0
    add             srcq, strideq
    paddw             m7, m3
    mova              m3, m4
%ifidn %1, avg
    movq              m4, [dstq]
%endif
    psraw             m7, 6
%ifnidn %1, avg
    mova              m4, m5
%endif
    packuswb          m7, m7
%ifidn %1, avg
    pavgb             m7, m4
    mova              m4, m5
%endif
    movq          [dstq], m7
%else
    pmaddubsw         m7, COEFF0
    pmaddubsw         m3, COEFF0
    punpcklbw         M8, m4, m6
    punpckhbw         m4, m6
    pmaddubsw         M9, M8, COEFF1
    paddw             m7, m0
%if ARCH_X86_64
    pmaddubsw        m10, m4, m2
    paddw             m3, m0
    paddw             m9, m7
    mova              m7, m8
    psraw             m9, 6
    paddw            m10, m3
%else
    paddw             m3, m0
    paddw             M9, m7
    mova              m7, M8
    pmaddubsw         M8, m4, COEFF1
    psraw             M9, 6
    paddw             M8, m3
%endif
    add             srcq, strideq
    mova              m3, m4
%if ARCH_X86_64
    psraw            m10, 6
    packuswb          m9, m10
%else
    psraw             M8, 6
    packuswb          M9, M8
%endif
%ifidn %1, avg
    pavgb             M9, [dstq]
%endif
    mova              m4, m5
    mova          [dstq], M9
%endif
    add             dstq, strideq
    mova              m5, m6
    dec               hd
    jnz            .loop
    RET
%endmacro

VER_8B put, 8
VER_8B avg, 8

VER_8B put, 16
VER_8B avg, 16
