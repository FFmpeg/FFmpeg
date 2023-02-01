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
    pxor               m0, m0

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

%macro INV_TRANS_INIT 0
    movsxdifnidn linesizeq, linesized
    movd       m0, blockd
    SPLATW     m0, m0
    pxor       m1, m1
    psubw      m1, m0
    packuswb   m0, m0
    packuswb   m1, m1

    DEFINE_ARGS dest, linesize, linesize3
    lea    linesize3q, [linesizeq*3]
%endmacro

%macro INV_TRANS_PROCESS 1
    mov%1                  m2, [destq+linesizeq*0]
    mov%1                  m3, [destq+linesizeq*1]
    mov%1                  m4, [destq+linesizeq*2]
    mov%1                  m5, [destq+linesize3q]
    paddusb                m2, m0
    paddusb                m3, m0
    paddusb                m4, m0
    paddusb                m5, m0
    psubusb                m2, m1
    psubusb                m3, m1
    psubusb                m4, m1
    psubusb                m5, m1
    mov%1 [linesizeq*0+destq], m2
    mov%1 [linesizeq*1+destq], m3
    mov%1 [linesizeq*2+destq], m4
    mov%1 [linesize3q +destq], m5
%endmacro

; ff_vc1_inv_trans_?x?_dc_mmxext(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
INIT_MMX mmxext
cglobal vc1_inv_trans_4x4_dc, 3,4,0, dest, linesize, block
    movsx         r3d, WORD [blockq]
    mov        blockd, r3d             ; dc
    shl        blockd, 4               ; 16 * dc
    lea        blockd, [blockq+r3+4]   ; 17 * dc + 4
    sar        blockd, 3               ; >> 3
    mov           r3d, blockd          ; dc
    shl        blockd, 4               ; 16 * dc
    lea        blockd, [blockq+r3+64]  ; 17 * dc + 64
    sar        blockd, 7               ; >> 7

    INV_TRANS_INIT

    INV_TRANS_PROCESS h
    RET

INIT_MMX mmxext
cglobal vc1_inv_trans_4x8_dc, 3,4,0, dest, linesize, block
    movsx         r3d, WORD [blockq]
    mov        blockd, r3d             ; dc
    shl        blockd, 4               ; 16 * dc
    lea        blockd, [blockq+r3+4]   ; 17 * dc + 4
    sar        blockd, 3               ; >> 3
    shl        blockd, 2               ;  4 * dc
    lea        blockd, [blockq*3+64]   ; 12 * dc + 64
    sar        blockd, 7               ; >> 7

    INV_TRANS_INIT

    INV_TRANS_PROCESS h
    lea         destq, [destq+linesizeq*4]
    INV_TRANS_PROCESS h
    RET

INIT_MMX mmxext
cglobal vc1_inv_trans_8x4_dc, 3,4,0, dest, linesize, block
    movsx      blockd, WORD [blockq]   ; dc
    lea        blockd, [blockq*3+1]    ;  3 * dc + 1
    sar        blockd, 1               ; >> 1
    mov           r3d, blockd          ; dc
    shl        blockd, 4               ; 16 * dc
    lea        blockd, [blockq+r3+64]  ; 17 * dc + 64
    sar        blockd, 7               ; >> 7

    INV_TRANS_INIT

    INV_TRANS_PROCESS a
    RET

INIT_MMX mmxext
cglobal vc1_inv_trans_8x8_dc, 3,3,0, dest, linesize, block
    movsx      blockd, WORD [blockq]   ; dc
    lea        blockd, [blockq*3+1]    ;  3 * dc + 1
    sar        blockd, 1               ; >> 1
    lea        blockd, [blockq*3+16]   ;  3 * dc + 16
    sar        blockd, 5               ; >> 5

    INV_TRANS_INIT

    INV_TRANS_PROCESS a
    lea         destq, [destq+linesizeq*4]
    INV_TRANS_PROCESS a
    RET
