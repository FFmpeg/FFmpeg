;*****************************************************************************
;* MMX/SSE2/AVX-optimized 10-bit H.264 iDCT code
;*****************************************************************************
;* Copyright (C) 2005-2011 x264 project
;*
;* Authors: Daniel Kang <daniel.d.kang@gmail.com>
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

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

pw_pixel_max: times 8 dw ((1 << 10)-1)
pd_32:        times 4 dd 32

SECTION .text

;-----------------------------------------------------------------------------
; void ff_h264_idct_add_10(pixel *dst, int16_t *block, int stride)
;-----------------------------------------------------------------------------
%macro STORE_DIFFx2 6
    psrad       %1, 6
    psrad       %2, 6
    packssdw    %1, %2
    movq        %3, [%5]
    movhps      %3, [%5+%6]
    paddsw      %1, %3
    CLIPW       %1, %4, [pw_pixel_max]
    movq      [%5], %1
    movhps [%5+%6], %1
%endmacro

%macro STORE_DIFF16 5
    psrad       %1, 6
    psrad       %2, 6
    packssdw    %1, %2
    paddsw      %1, [%5]
    CLIPW       %1, %3, %4
    mova      [%5], %1
%endmacro

;dst, in, stride
%macro IDCT4_ADD_10 3
    mova  m0, [%2+ 0]
    mova  m1, [%2+16]
    mova  m2, [%2+32]
    mova  m3, [%2+48]
    IDCT4_1D d,0,1,2,3,4,5
    TRANSPOSE4x4D 0,1,2,3,4
    paddd m0, [pd_32]
    IDCT4_1D d,0,1,2,3,4,5
    pxor  m5, m5
    mova [%2+ 0], m5
    mova [%2+16], m5
    mova [%2+32], m5
    mova [%2+48], m5
    STORE_DIFFx2 m0, m1, m4, m5, %1, %3
    lea   %1, [%1+%3*2]
    STORE_DIFFx2 m2, m3, m4, m5, %1, %3
%endmacro

%macro IDCT_ADD_10 0
cglobal h264_idct_add_10, 3,3
    movsxdifnidn r2, r2d
    IDCT4_ADD_10 r0, r1, r2
    RET
%endmacro

INIT_XMM sse2
IDCT_ADD_10
INIT_XMM avx
IDCT_ADD_10

;-----------------------------------------------------------------------------
; void ff_h264_idct_add16_10(pixel *dst, const int *block_offset,
;                            int16_t *block, int stride,
;                            const uint8_t nnzc[6*8])
;-----------------------------------------------------------------------------
;;;;;;; NO FATE SAMPLES TRIGGER THIS
%macro ADD4x4IDCT 0
add4x4_idct %+ SUFFIX:
    add   r5, r0
    mova  m0, [r2+ 0]
    mova  m1, [r2+16]
    mova  m2, [r2+32]
    mova  m3, [r2+48]
    IDCT4_1D d,0,1,2,3,4,5
    TRANSPOSE4x4D 0,1,2,3,4
    paddd m0, [pd_32]
    IDCT4_1D d,0,1,2,3,4,5
    pxor  m5, m5
    mova  [r2+ 0], m5
    mova  [r2+16], m5
    mova  [r2+32], m5
    mova  [r2+48], m5
    STORE_DIFFx2 m0, m1, m4, m5, r5, r3
    lea   r5, [r5+r3*2]
    STORE_DIFFx2 m2, m3, m4, m5, r5, r3
    ret
%endmacro

INIT_XMM sse2
ALIGN 16
ADD4x4IDCT
INIT_XMM avx
ALIGN 16
ADD4x4IDCT

%macro ADD16_OP 2
    cmp          byte [r4+%2], 0
    jz .skipblock%1
    mov         r5d, [r1+%1*4]
    call add4x4_idct %+ SUFFIX
.skipblock%1:
%if %1<15
    add          r2, 64
%endif
%endmacro

%macro IDCT_ADD16_10 0
cglobal h264_idct_add16_10, 5,6
    movsxdifnidn r3, r3d
    ADD16_OP 0, 4+1*8
    ADD16_OP 1, 5+1*8
    ADD16_OP 2, 4+2*8
    ADD16_OP 3, 5+2*8
    ADD16_OP 4, 6+1*8
    ADD16_OP 5, 7+1*8
    ADD16_OP 6, 6+2*8
    ADD16_OP 7, 7+2*8
    ADD16_OP 8, 4+3*8
    ADD16_OP 9, 5+3*8
    ADD16_OP 10, 4+4*8
    ADD16_OP 11, 5+4*8
    ADD16_OP 12, 6+3*8
    ADD16_OP 13, 7+3*8
    ADD16_OP 14, 6+4*8
    ADD16_OP 15, 7+4*8
    REP_RET
%endmacro

INIT_XMM sse2
IDCT_ADD16_10
INIT_XMM avx
IDCT_ADD16_10

;-----------------------------------------------------------------------------
; void ff_h264_idct_dc_add_10(pixel *dst, int16_t *block, int stride)
;-----------------------------------------------------------------------------
%macro IDCT_DC_ADD_OP_10 3
    pxor      m5, m5
%if avx_enabled
    paddw     m1, m0, [%1+0   ]
    paddw     m2, m0, [%1+%2  ]
    paddw     m3, m0, [%1+%2*2]
    paddw     m4, m0, [%1+%3  ]
%else
    mova      m1, [%1+0   ]
    mova      m2, [%1+%2  ]
    mova      m3, [%1+%2*2]
    mova      m4, [%1+%3  ]
    paddw     m1, m0
    paddw     m2, m0
    paddw     m3, m0
    paddw     m4, m0
%endif
    CLIPW     m1, m5, m6
    CLIPW     m2, m5, m6
    CLIPW     m3, m5, m6
    CLIPW     m4, m5, m6
    mova [%1+0   ], m1
    mova [%1+%2  ], m2
    mova [%1+%2*2], m3
    mova [%1+%3  ], m4
%endmacro

INIT_MMX mmxext
cglobal h264_idct_dc_add_10,3,3
    movsxdifnidn r2, r2d
    movd      m0, [r1]
    mov dword [r1], 0
    paddd     m0, [pd_32]
    psrad     m0, 6
    lea       r1, [r2*3]
    pshufw    m0, m0, 0
    mova      m6, [pw_pixel_max]
    IDCT_DC_ADD_OP_10 r0, r2, r1
    RET

;-----------------------------------------------------------------------------
; void ff_h264_idct8_dc_add_10(pixel *dst, int16_t *block, int stride)
;-----------------------------------------------------------------------------
%macro IDCT8_DC_ADD 0
cglobal h264_idct8_dc_add_10,3,4,7
    movsxdifnidn r2, r2d
    movd      m0, [r1]
    mov dword[r1], 0
    paddd     m0, [pd_32]
    psrad     m0, 6
    lea       r1, [r2*3]
    SPLATW    m0, m0, 0
    mova      m6, [pw_pixel_max]
    IDCT_DC_ADD_OP_10 r0, r2, r1
    lea       r0, [r0+r2*4]
    IDCT_DC_ADD_OP_10 r0, r2, r1
    RET
%endmacro

INIT_XMM sse2
IDCT8_DC_ADD
INIT_XMM avx
IDCT8_DC_ADD

;-----------------------------------------------------------------------------
; void ff_h264_idct_add16intra_10(pixel *dst, const int *block_offset,
;                                 int16_t *block, int stride,
;                                 const uint8_t nnzc[6*8])
;-----------------------------------------------------------------------------
%macro AC 1
.ac%1:
    mov  r5d, [r1+(%1+0)*4]
    call add4x4_idct %+ SUFFIX
    mov  r5d, [r1+(%1+1)*4]
    add  r2, 64
    call add4x4_idct %+ SUFFIX
    add  r2, 64
    jmp .skipadd%1
%endmacro

%assign last_block 16
%macro ADD16_OP_INTRA 2
    cmp      word [r4+%2], 0
    jnz .ac%1
    mov      r5d, [r2+ 0]
    or       r5d, [r2+64]
    jz .skipblock%1
    mov      r5d, [r1+(%1+0)*4]
    call idct_dc_add %+ SUFFIX
.skipblock%1:
%if %1<last_block-2
    add       r2, 128
%endif
.skipadd%1:
%endmacro

%macro IDCT_ADD16INTRA_10 0
idct_dc_add %+ SUFFIX:
    add       r5, r0
    movq      m0, [r2+ 0]
    movhps    m0, [r2+64]
    mov dword [r2+ 0], 0
    mov dword [r2+64], 0
    paddd     m0, [pd_32]
    psrad     m0, 6
    pshufhw   m0, m0, 0
    pshuflw   m0, m0, 0
    lea       r6, [r3*3]
    mova      m6, [pw_pixel_max]
    IDCT_DC_ADD_OP_10 r5, r3, r6
    ret

cglobal h264_idct_add16intra_10,5,7,8
    movsxdifnidn r3, r3d
    ADD16_OP_INTRA 0, 4+1*8
    ADD16_OP_INTRA 2, 4+2*8
    ADD16_OP_INTRA 4, 6+1*8
    ADD16_OP_INTRA 6, 6+2*8
    ADD16_OP_INTRA 8, 4+3*8
    ADD16_OP_INTRA 10, 4+4*8
    ADD16_OP_INTRA 12, 6+3*8
    ADD16_OP_INTRA 14, 6+4*8
    REP_RET
    AC 8
    AC 10
    AC 12
    AC 14
    AC 0
    AC 2
    AC 4
    AC 6
%endmacro

INIT_XMM sse2
IDCT_ADD16INTRA_10
INIT_XMM avx
IDCT_ADD16INTRA_10

%assign last_block 36
;-----------------------------------------------------------------------------
; void ff_h264_idct_add8_10(pixel **dst, const int *block_offset,
;                           int16_t *block, int stride,
;                           const uint8_t nnzc[6*8])
;-----------------------------------------------------------------------------
%macro IDCT_ADD8 0
cglobal h264_idct_add8_10,5,8,7
    movsxdifnidn r3, r3d
%if ARCH_X86_64
    mov      r7, r0
%endif
    add      r2, 1024
    mov      r0, [r0]
    ADD16_OP_INTRA 16, 4+ 6*8
    ADD16_OP_INTRA 18, 4+ 7*8
    add      r2, 1024-128*2
%if ARCH_X86_64
    mov      r0, [r7+gprsize]
%else
    mov      r0, r0m
    mov      r0, [r0+gprsize]
%endif
    ADD16_OP_INTRA 32, 4+11*8
    ADD16_OP_INTRA 34, 4+12*8
    REP_RET
    AC 16
    AC 18
    AC 32
    AC 34

%endmacro ; IDCT_ADD8

INIT_XMM sse2
IDCT_ADD8
INIT_XMM avx
IDCT_ADD8

;-----------------------------------------------------------------------------
; void ff_h264_idct8_add_10(pixel *dst, int16_t *block, int stride)
;-----------------------------------------------------------------------------
%macro IDCT8_1D 2
    SWAP      0, 1
    psrad     m4, m5, 1
    psrad     m1, m0, 1
    paddd     m4, m5
    paddd     m1, m0
    paddd     m4, m7
    paddd     m1, m5
    psubd     m4, m0
    paddd     m1, m3

    psubd     m0, m3
    psubd     m5, m3
    paddd     m0, m7
    psubd     m5, m7
    psrad     m3, 1
    psrad     m7, 1
    psubd     m0, m3
    psubd     m5, m7

    SWAP      1, 7
    psrad     m1, m7, 2
    psrad     m3, m4, 2
    paddd     m3, m0
    psrad     m0, 2
    paddd     m1, m5
    psrad     m5, 2
    psubd     m0, m4
    psubd     m7, m5

    SWAP      5, 6
    psrad     m4, m2, 1
    psrad     m6, m5, 1
    psubd     m4, m5
    paddd     m6, m2

    mova      m2, %1
    mova      m5, %2
    SUMSUB_BA d, 5, 2
    SUMSUB_BA d, 6, 5
    SUMSUB_BA d, 4, 2
    SUMSUB_BA d, 7, 6
    SUMSUB_BA d, 0, 4
    SUMSUB_BA d, 3, 2
    SUMSUB_BA d, 1, 5
    SWAP      7, 6, 4, 5, 2, 3, 1, 0 ; 70315246 -> 01234567
%endmacro

%macro IDCT8_1D_FULL 1
    mova         m7, [%1+112*2]
    mova         m6, [%1+ 96*2]
    mova         m5, [%1+ 80*2]
    mova         m3, [%1+ 48*2]
    mova         m2, [%1+ 32*2]
    mova         m1, [%1+ 16*2]
    IDCT8_1D   [%1], [%1+ 64*2]
%endmacro

; %1=int16_t *block, %2=int16_t *dstblock
%macro IDCT8_ADD_SSE_START 2
    IDCT8_1D_FULL %1
%if ARCH_X86_64
    TRANSPOSE4x4D  0,1,2,3,8
    mova    [%2    ], m0
    TRANSPOSE4x4D  4,5,6,7,8
    mova    [%2+8*2], m4
%else
    mova         [%1], m7
    TRANSPOSE4x4D   0,1,2,3,7
    mova           m7, [%1]
    mova    [%2     ], m0
    mova    [%2+16*2], m1
    mova    [%2+32*2], m2
    mova    [%2+48*2], m3
    TRANSPOSE4x4D   4,5,6,7,3
    mova    [%2+ 8*2], m4
    mova    [%2+24*2], m5
    mova    [%2+40*2], m6
    mova    [%2+56*2], m7
%endif
%endmacro

; %1=uint8_t *dst, %2=int16_t *block, %3=int stride
%macro IDCT8_ADD_SSE_END 3
    IDCT8_1D_FULL %2
    mova  [%2     ], m6
    mova  [%2+16*2], m7

    pxor         m7, m7
    STORE_DIFFx2 m0, m1, m6, m7, %1, %3
    lea          %1, [%1+%3*2]
    STORE_DIFFx2 m2, m3, m6, m7, %1, %3
    mova         m0, [%2     ]
    mova         m1, [%2+16*2]
    lea          %1, [%1+%3*2]
    STORE_DIFFx2 m4, m5, m6, m7, %1, %3
    lea          %1, [%1+%3*2]
    STORE_DIFFx2 m0, m1, m6, m7, %1, %3
%endmacro

%macro IDCT8_ADD 0
cglobal h264_idct8_add_10, 3,4,16
    movsxdifnidn r2, r2d
%if UNIX64 == 0
    %assign pad 16-gprsize-(stack_offset&15)
    sub  rsp, pad
    call h264_idct8_add1_10 %+ SUFFIX
    add  rsp, pad
    RET
%endif

ALIGN 16
; TODO: does not need to use stack
h264_idct8_add1_10 %+ SUFFIX:
%assign pad 256+16-gprsize
    sub          rsp, pad
    add   dword [r1], 32

%if ARCH_X86_64
    IDCT8_ADD_SSE_START r1, rsp
    SWAP 1,  9
    SWAP 2, 10
    SWAP 3, 11
    SWAP 5, 13
    SWAP 6, 14
    SWAP 7, 15
    IDCT8_ADD_SSE_START r1+16, rsp+128
    PERMUTE 1,9, 2,10, 3,11, 5,1, 6,2, 7,3, 9,13, 10,14, 11,15, 13,5, 14,6, 15,7
    IDCT8_1D [rsp], [rsp+128]
    SWAP 0,  8
    SWAP 1,  9
    SWAP 2, 10
    SWAP 3, 11
    SWAP 4, 12
    SWAP 5, 13
    SWAP 6, 14
    SWAP 7, 15
    IDCT8_1D [rsp+16], [rsp+144]
    psrad         m8, 6
    psrad         m0, 6
    packssdw      m8, m0
    paddsw        m8, [r0]
    pxor          m0, m0
    mova    [r1+  0], m0
    mova    [r1+ 16], m0
    mova    [r1+ 32], m0
    mova    [r1+ 48], m0
    mova    [r1+ 64], m0
    mova    [r1+ 80], m0
    mova    [r1+ 96], m0
    mova    [r1+112], m0
    mova    [r1+128], m0
    mova    [r1+144], m0
    mova    [r1+160], m0
    mova    [r1+176], m0
    mova    [r1+192], m0
    mova    [r1+208], m0
    mova    [r1+224], m0
    mova    [r1+240], m0
    CLIPW         m8, m0, [pw_pixel_max]
    mova        [r0], m8
    mova          m8, [pw_pixel_max]
    STORE_DIFF16  m9, m1, m0, m8, r0+r2
    lea           r0, [r0+r2*2]
    STORE_DIFF16 m10, m2, m0, m8, r0
    STORE_DIFF16 m11, m3, m0, m8, r0+r2
    lea           r0, [r0+r2*2]
    STORE_DIFF16 m12, m4, m0, m8, r0
    STORE_DIFF16 m13, m5, m0, m8, r0+r2
    lea           r0, [r0+r2*2]
    STORE_DIFF16 m14, m6, m0, m8, r0
    STORE_DIFF16 m15, m7, m0, m8, r0+r2
%else
    IDCT8_ADD_SSE_START r1,    rsp
    IDCT8_ADD_SSE_START r1+16, rsp+128
    lea           r3, [r0+8]
    IDCT8_ADD_SSE_END r0, rsp,    r2
    IDCT8_ADD_SSE_END r3, rsp+16, r2
    mova    [r1+  0], m7
    mova    [r1+ 16], m7
    mova    [r1+ 32], m7
    mova    [r1+ 48], m7
    mova    [r1+ 64], m7
    mova    [r1+ 80], m7
    mova    [r1+ 96], m7
    mova    [r1+112], m7
    mova    [r1+128], m7
    mova    [r1+144], m7
    mova    [r1+160], m7
    mova    [r1+176], m7
    mova    [r1+192], m7
    mova    [r1+208], m7
    mova    [r1+224], m7
    mova    [r1+240], m7
%endif ; ARCH_X86_64

    add          rsp, pad
    ret
%endmacro

INIT_XMM sse2
IDCT8_ADD
INIT_XMM avx
IDCT8_ADD

;-----------------------------------------------------------------------------
; void ff_h264_idct8_add4_10(pixel **dst, const int *block_offset,
;                            int16_t *block, int stride,
;                            const uint8_t nnzc[6*8])
;-----------------------------------------------------------------------------
;;;;;;; NO FATE SAMPLES TRIGGER THIS
%macro IDCT8_ADD4_OP 2
    cmp       byte [r4+%2], 0
    jz .skipblock%1
    mov      r0d, [r6+%1*4]
    add       r0, r5
    call h264_idct8_add1_10 %+ SUFFIX
.skipblock%1:
%if %1<12
    add       r1, 256
%endif
%endmacro

%macro IDCT8_ADD4 0
cglobal h264_idct8_add4_10, 0,7,16
    movsxdifnidn r3, r3d
    %assign pad 16-gprsize-(stack_offset&15)
    SUB      rsp, pad
    mov       r5, r0mp
    mov       r6, r1mp
    mov       r1, r2mp
    mov      r2d, r3m
    movifnidn r4, r4mp
    IDCT8_ADD4_OP  0, 4+1*8
    IDCT8_ADD4_OP  4, 6+1*8
    IDCT8_ADD4_OP  8, 4+3*8
    IDCT8_ADD4_OP 12, 6+3*8
    ADD       rsp, pad
    RET
%endmacro ; IDCT8_ADD4

INIT_XMM sse2
IDCT8_ADD4
INIT_XMM avx
IDCT8_ADD4
