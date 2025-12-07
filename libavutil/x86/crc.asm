;*****************************************************************************
;* Copyright (c) 2025 Shreesh Adiga <16567adigashreesh@gmail.com>
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

%include "x86util.asm"

SECTION_RODATA
reverse_shuffle: db 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

partial_bytes_shuf_tab: db 255, 254, 253, 252, 251, 250, 249, 248,\
                           247, 246, 245, 244, 243, 242, 241, 240,\
                             0,   1,   2,   3,   4,   5,   6,   7,\
                             8,   9,  10,  11,  12,  13,  14,  15

SECTION .text

%macro FOLD_128_TO_64 4
; %1 LE ; %2 128 bit fold reg ; %3 pre-computed constant reg ; %4 tmp reg
%if %1 == 1
    mova      %4, %2
    pclmulqdq %2, %3, 0x00
    psrldq    %4, 8
    pxor      %2, %4
    mova      %4, %2
    psllq     %4, 32
    pclmulqdq %4, %3, 0x10
    pxor      %2, %4
%else
    movq      %4, %2
    pclmulqdq %2, %3, 0x11
    pslldq    %4, 4
    pxor      %4, %2
    mova      %2, %4
    pclmulqdq %4, %3, 0x01
    pxor      %2, %4
%endif
%endmacro

%macro FOLD_64_TO_32 4
; %1 LE ; %2 128 bit fold reg ; %3 pre-computed constant reg ; %4 tmp reg
%if %1 == 1
    pxor      %4, %4
    pblendw   %4, %2, 0xfc
    mova      %2, %4
    pclmulqdq %4, %3, 0x00
    pxor      %4, %2
    pclmulqdq %4, %3, 0x10
    pxor      %2, %4
    pextrd   eax, %2, 2
%else
    mova      %4, %2
    pclmulqdq %2, %3, 0x00
    pclmulqdq %2, %3, 0x11
    pxor      %2, %4
    movd     eax, %2
    bswap    eax
%endif
%endmacro

%macro FOLD_SINGLE 4
; %1 temp ; %2 fold reg ; %3 pre-computed constants ; %4 input data block
    mova      %1, %2
    pclmulqdq %1, %3, 0x01
    pxor      %1, %4
    pclmulqdq %2, %3, 0x10
    pxor      %2, %1
%endmacro

%macro XMM_SHIFT_LEFT 4
; %1 xmm input reg ; %2 shift bytes amount ; %3 temp xmm register ; %4 temp gpr
    lea    %4, [partial_bytes_shuf_tab]
    movu   %3, [%4 + 16 - (%2)]
    pshufb %1, %3
%endmacro

%macro MEMCPY_0_15 6
; %1 dst ; %2 src ; %3 len ; %4, %5 temp gpr register; %6 done label
    cmp %3, 8
    jae .between_8_15
    cmp %3, 4
    jae .between_4_7
    cmp %3, 1
    ja .between_2_3
    jb %6
    mov  %4b, [%2]
    mov [%1], %4b
    jmp %6

.between_8_15:
%if ARCH_X86_64
    mov           %4q, [%2]
    mov           %5q, [%2 + %3 - 8]
    mov          [%1], %4q
    mov [%1 + %3 - 8], %5q
    jmp %6
%else
    xor            %5, %5
.copy4b:
        mov       %4d, [%2 + %5]
        mov [%1 + %5], %4d
        add        %5, 4
        lea        %4, [%5 + 4]
        cmp        %4, %3
        jb        .copy4b

    mov           %4d, [%2 + %3 - 4]
    mov [%1 + %3 - 4], %4d
    jmp %6
%endif
.between_4_7:
    mov           %4d, [%2]
    mov           %5d, [%2 + %3 - 4]
    mov          [%1], %4d
    mov [%1 + %3 - 4], %5d
    jmp %6
.between_2_3:
    mov           %4w, [%2]
    mov           %5w, [%2 + %3 - 2]
    mov          [%1], %4w
    mov [%1 + %3 - 2], %5w
    ; fall through, %6 label is expected to be next instruction
%endmacro

%macro CRC 1
%define CTX r0+4
;-----------------------------------------------------------------------------------------------
; ff_crc[_le]_clmul(const uint8_t *ctx, uint32_t crc, const uint8_t *buffer, size_t length
;-----------------------------------------------------------------------------------------------
; %1 == 1 - LE format
%if %1 == 1
cglobal crc_le, 4, 6, 6+4*ARCH_X86_64, 0x10
%else
cglobal crc,    4, 6, 7+4*ARCH_X86_64, 0x10
%endif

%if ARCH_X86_32
    %define m10 m6
%endif

%if %1 == 0
    mova  m10, [reverse_shuffle]
%endif

    movd   m4, r1d
%if ARCH_X86_32
    ; skip 4x unrolled loop due to only 8 XMM reg being available in X86_32
    jmp   .less_than_64bytes
%else
    cmp    r3, 64
    jb    .less_than_64bytes
    movu   m1, [r2 +  0]
    movu   m3, [r2 + 16]
    movu   m2, [r2 + 32]
    movu   m0, [r2 + 48]
    pxor   m1, m4
%if %1 == 0
    pshufb m0, m10
    pshufb m1, m10
    pshufb m2, m10
    pshufb m3, m10
%endif
    mov    r4, 64
    cmp    r3, 128
    jb    .reduce_4x_to_1
    movu   m4, [CTX]

.fold_4x_loop:
        movu        m6, [r2 + r4 +  0]
        movu        m7, [r2 + r4 + 16]
        movu        m8, [r2 + r4 + 32]
        movu        m9, [r2 + r4 + 48]
%if %1 == 0
        pshufb      m6, m10
        pshufb      m7, m10
        pshufb      m8, m10
        pshufb      m9, m10
%endif
        FOLD_SINGLE m5, m1, m4, m6
        FOLD_SINGLE m5, m3, m4, m7
        FOLD_SINGLE m5, m2, m4, m8
        FOLD_SINGLE m5, m0, m4, m9
        add         r4, 64
        lea         r5, [r4 + 64]
        cmp         r5, r3
        jbe        .fold_4x_loop

.reduce_4x_to_1:
    movu        m4, [CTX + 16]
    FOLD_SINGLE m5, m1, m4, m3
    FOLD_SINGLE m5, m1, m4, m2
    FOLD_SINGLE m5, m1, m4, m0
%endif

.fold_1x_pre:
    lea  r5, [r4 + 16]
    cmp  r5, r3
    ja  .partial_block

.fold_1x_loop:
        movu        m2, [r2 + r4]
%if %1 == 0
        pshufb      m2, m10
%endif
        FOLD_SINGLE m5, m1, m4, m2
        add         r4, 16
        lea         r5, [r4 + 16]
        cmp         r5, r3
        jbe        .fold_1x_loop

.partial_block:
    cmp         r4, r3
    jae        .reduce_128_to_64
    movu        m2, [r2 + r3 - 16]
    and         r3, 0xf
    lea         r4, [partial_bytes_shuf_tab]
    movu        m0, [r3 + r4]
%if %1 == 0
    pshufb      m1, m10
%endif
    mova        m3, m1
    pcmpeqd     m5, m5 ; m5 = _mm_set1_epi8(0xff)
    pxor        m5, m0
    pshufb      m3, m5
    pblendvb    m2, m3, m0
    pshufb      m1, m0
%if %1 == 0
    pshufb      m1, m10
    pshufb      m2, m10
%endif
    FOLD_SINGLE m5, m1, m4, m2

.reduce_128_to_64:
    movu           m4, [CTX + 32]
    FOLD_128_TO_64 %1, m1, m4, m5
.reduce_64_to_32:
    movu           m4, [CTX + 48]
    FOLD_64_TO_32  %1, m1, m4, m5
    RET

.less_than_64bytes:
    cmp    r3, 16
    jb    .less_than_16bytes
    movu   m1, [r2]
    pxor   m1, m4
%if %1 == 0
    pshufb m1, m10
%endif
    mov    r4, 16
    movu   m4, [CTX + 16]
    jmp   .fold_1x_pre

.less_than_16bytes:
    pxor           m1, m1
    movu        [rsp], m1
    MEMCPY_0_15   rsp, r2, r3, r1, r4, .memcpy_done

.memcpy_done:
    movu           m1, [rsp]
    pxor           m1, m4
    cmp            r3, 5
    jb            .less_than_5bytes
    XMM_SHIFT_LEFT m1, (16 - r3), m2, r4
%if %1 == 0
    pshufb         m1, m10
%endif
    jmp           .reduce_128_to_64

.less_than_5bytes:
%if %1 == 0
    XMM_SHIFT_LEFT m1, (4 - r3), m2, r4
    movq          m10, [reverse_shuffle + 8] ; 0x0001020304050607
    pshufb         m1, m10
%else
    XMM_SHIFT_LEFT m1, (8 - r3), m2, r4
%endif
    jmp .reduce_64_to_32

%endmacro

INIT_XMM clmul
CRC 0
CRC 1
