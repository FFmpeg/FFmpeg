;*****************************************************************************
;* MMX/SSE2-optimized H.264 iDCT
;*****************************************************************************
;* Copyright (C) 2004-2005 Michael Niedermayer, Loren Merritt
;* Copyright (C) 2003-2008 x264 project
;*
;* Authors: Laurent Aimar <fenrir@via.ecp.fr>
;*          Loren Merritt <lorenm@u.washington.edu>
;*          Holger Lubitz <hal@duncan.ol.sub.de>
;*          Min Chen <chenm001.163.com>
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
;*****************************************************************************

%include "libavutil/x86/x86inc.asm"
%include "libavutil/x86/x86util.asm"

SECTION_RODATA

; FIXME this table is a duplicate from h264data.h, and will be removed once the tables from, h264 have been split
scan8_mem: db  4+ 1*8, 5+ 1*8, 4+ 2*8, 5+ 2*8
           db  6+ 1*8, 7+ 1*8, 6+ 2*8, 7+ 2*8
           db  4+ 3*8, 5+ 3*8, 4+ 4*8, 5+ 4*8
           db  6+ 3*8, 7+ 3*8, 6+ 4*8, 7+ 4*8
           db  4+ 6*8, 5+ 6*8, 4+ 7*8, 5+ 7*8
           db  6+ 6*8, 7+ 6*8, 6+ 7*8, 7+ 7*8
           db  4+ 8*8, 5+ 8*8, 4+ 9*8, 5+ 9*8
           db  6+ 8*8, 7+ 8*8, 6+ 9*8, 7+ 9*8
           db  4+11*8, 5+11*8, 4+12*8, 5+12*8
           db  6+11*8, 7+11*8, 6+12*8, 7+12*8
           db  4+13*8, 5+13*8, 4+14*8, 5+14*8
           db  6+13*8, 7+13*8, 6+14*8, 7+14*8
%ifdef PIC
%define npicregs 1
%define scan8 picregq
%else
%define npicregs 0
%define scan8 scan8_mem
%endif

cextern pw_32
cextern pw_1

SECTION .text

; %1=uint8_t *dst, %2=int16_t *block, %3=int stride
%macro IDCT4_ADD 3
    ; Load dct coeffs
    movq         m0, [%2]
    movq         m1, [%2+8]
    movq         m2, [%2+16]
    movq         m3, [%2+24]

    IDCT4_1D      w, 0, 1, 2, 3, 4, 5
    mova         m6, [pw_32]
    TRANSPOSE4x4W 0, 1, 2, 3, 4
    paddw        m0, m6
    IDCT4_1D      w, 0, 1, 2, 3, 4, 5
    pxor         m7, m7

    STORE_DIFFx2 m0, m1, m4, m5, m7, 6, %1, %3
    lea          %1, [%1+%3*2]
    STORE_DIFFx2 m2, m3, m4, m5, m7, 6, %1, %3
%endmacro

INIT_MMX
; ff_h264_idct_add_mmx(uint8_t *dst, int16_t *block, int stride)
cglobal h264_idct_add_8_mmx, 3, 3, 0
    IDCT4_ADD    r0, r1, r2
    RET

%macro IDCT8_1D 2
    mova         m0, m1
    psraw        m1, 1
    mova         m4, m5
    psraw        m4, 1
    paddw        m4, m5
    paddw        m1, m0
    paddw        m4, m7
    paddw        m1, m5
    psubw        m4, m0
    paddw        m1, m3

    psubw        m0, m3
    psubw        m5, m3
    psraw        m3, 1
    paddw        m0, m7
    psubw        m5, m7
    psraw        m7, 1
    psubw        m0, m3
    psubw        m5, m7

    mova         m7, m1
    psraw        m1, 2
    mova         m3, m4
    psraw        m3, 2
    paddw        m3, m0
    psraw        m0, 2
    paddw        m1, m5
    psraw        m5, 2
    psubw        m0, m4
    psubw        m7, m5

    mova         m5, m6
    psraw        m6, 1
    mova         m4, m2
    psraw        m4, 1
    paddw        m6, m2
    psubw        m4, m5

    mova         m2, %1
    mova         m5, %2
    SUMSUB_BA    w, 5, 2
    SUMSUB_BA    w, 6, 5
    SUMSUB_BA    w, 4, 2
    SUMSUB_BA    w, 7, 6
    SUMSUB_BA    w, 0, 4
    SUMSUB_BA    w, 3, 2
    SUMSUB_BA    w, 1, 5
    SWAP         7, 6, 4, 5, 2, 3, 1, 0 ; 70315246 -> 01234567
%endmacro

%macro IDCT8_1D_FULL 1
    mova         m7, [%1+112]
    mova         m6, [%1+ 96]
    mova         m5, [%1+ 80]
    mova         m3, [%1+ 48]
    mova         m2, [%1+ 32]
    mova         m1, [%1+ 16]
    IDCT8_1D   [%1], [%1+ 64]
%endmacro

; %1=int16_t *block, %2=int16_t *dstblock
%macro IDCT8_ADD_MMX_START 2
    IDCT8_1D_FULL %1
    mova       [%1], m7
    TRANSPOSE4x4W 0, 1, 2, 3, 7
    mova         m7, [%1]
    mova    [%2   ], m0
    mova    [%2+16], m1
    mova    [%2+32], m2
    mova    [%2+48], m3
    TRANSPOSE4x4W 4, 5, 6, 7, 3
    mova    [%2+ 8], m4
    mova    [%2+24], m5
    mova    [%2+40], m6
    mova    [%2+56], m7
%endmacro

; %1=uint8_t *dst, %2=int16_t *block, %3=int stride
%macro IDCT8_ADD_MMX_END 3
    IDCT8_1D_FULL %2
    mova    [%2   ], m5
    mova    [%2+16], m6
    mova    [%2+32], m7

    pxor         m7, m7
    STORE_DIFFx2 m0, m1, m5, m6, m7, 6, %1, %3
    lea          %1, [%1+%3*2]
    STORE_DIFFx2 m2, m3, m5, m6, m7, 6, %1, %3
    mova         m0, [%2   ]
    mova         m1, [%2+16]
    mova         m2, [%2+32]
    lea          %1, [%1+%3*2]
    STORE_DIFFx2 m4, m0, m5, m6, m7, 6, %1, %3
    lea          %1, [%1+%3*2]
    STORE_DIFFx2 m1, m2, m5, m6, m7, 6, %1, %3
%endmacro

INIT_MMX
; ff_h264_idct8_add_mmx(uint8_t *dst, int16_t *block, int stride)
cglobal h264_idct8_add_8_mmx, 3, 4, 0
    %assign pad 128+4-(stack_offset&7)
    SUB         rsp, pad

    add   word [r1], 32
    IDCT8_ADD_MMX_START r1  , rsp
    IDCT8_ADD_MMX_START r1+8, rsp+64
    lea          r3, [r0+4]
    IDCT8_ADD_MMX_END   r0  , rsp,   r2
    IDCT8_ADD_MMX_END   r3  , rsp+8, r2

    ADD         rsp, pad
    RET

; %1=uint8_t *dst, %2=int16_t *block, %3=int stride
%macro IDCT8_ADD_SSE 4
    IDCT8_1D_FULL %2
%if ARCH_X86_64
    TRANSPOSE8x8W 0, 1, 2, 3, 4, 5, 6, 7, 8
%else
    TRANSPOSE8x8W 0, 1, 2, 3, 4, 5, 6, 7, [%2], [%2+16]
%endif
    paddw        m0, [pw_32]

%if ARCH_X86_64 == 0
    mova    [%2   ], m0
    mova    [%2+16], m4
    IDCT8_1D   [%2], [%2+ 16]
    mova    [%2   ], m6
    mova    [%2+16], m7
%else
    SWAP          0, 8
    SWAP          4, 9
    IDCT8_1D     m8, m9
    SWAP          6, 8
    SWAP          7, 9
%endif

    pxor         m7, m7
    lea          %4, [%3*3]
    STORE_DIFF   m0, m6, m7, [%1     ]
    STORE_DIFF   m1, m6, m7, [%1+%3  ]
    STORE_DIFF   m2, m6, m7, [%1+%3*2]
    STORE_DIFF   m3, m6, m7, [%1+%4  ]
%if ARCH_X86_64 == 0
    mova         m0, [%2   ]
    mova         m1, [%2+16]
%else
    SWAP          0, 8
    SWAP          1, 9
%endif
    lea          %1, [%1+%3*4]
    STORE_DIFF   m4, m6, m7, [%1     ]
    STORE_DIFF   m5, m6, m7, [%1+%3  ]
    STORE_DIFF   m0, m6, m7, [%1+%3*2]
    STORE_DIFF   m1, m6, m7, [%1+%4  ]
%endmacro

INIT_XMM
; ff_h264_idct8_add_sse2(uint8_t *dst, int16_t *block, int stride)
cglobal h264_idct8_add_8_sse2, 3, 4, 10
    IDCT8_ADD_SSE r0, r1, r2, r3
    RET

%macro DC_ADD_MMX2_INIT 2-3
%if %0 == 2
    movsx        %1, word [%1]
    add          %1, 32
    sar          %1, 6
    movd         m0, %1d
    lea          %1, [%2*3]
%else
    add          %3, 32
    sar          %3, 6
    movd         m0, %3d
    lea          %3, [%2*3]
%endif
    pshufw       m0, m0, 0
    pxor         m1, m1
    psubw        m1, m0
    packuswb     m0, m0
    packuswb     m1, m1
%endmacro

%macro DC_ADD_MMX2_OP 4
    %1           m2, [%2     ]
    %1           m3, [%2+%3  ]
    %1           m4, [%2+%3*2]
    %1           m5, [%2+%4  ]
    paddusb      m2, m0
    paddusb      m3, m0
    paddusb      m4, m0
    paddusb      m5, m0
    psubusb      m2, m1
    psubusb      m3, m1
    psubusb      m4, m1
    psubusb      m5, m1
    %1    [%2     ], m2
    %1    [%2+%3  ], m3
    %1    [%2+%3*2], m4
    %1    [%2+%4  ], m5
%endmacro

INIT_MMX
; ff_h264_idct_dc_add_mmx2(uint8_t *dst, int16_t *block, int stride)
cglobal h264_idct_dc_add_8_mmx2, 3, 3, 0
    DC_ADD_MMX2_INIT r1, r2
    DC_ADD_MMX2_OP movh, r0, r2, r1
    RET

; ff_h264_idct8_dc_add_mmx2(uint8_t *dst, int16_t *block, int stride)
cglobal h264_idct8_dc_add_8_mmx2, 3, 3, 0
    DC_ADD_MMX2_INIT r1, r2
    DC_ADD_MMX2_OP mova, r0, r2, r1
    lea          r0, [r0+r2*4]
    DC_ADD_MMX2_OP mova, r0, r2, r1
    RET

; ff_h264_idct_add16_mmx(uint8_t *dst, const int *block_offset,
;             DCTELEM *block, int stride, const uint8_t nnzc[6*8])
cglobal h264_idct_add16_8_mmx, 5, 7 + npicregs, 0, dst, block_offset, block, stride, nnzc, cntr, coeff, picreg
    xor          r5, r5
%ifdef PIC
    lea     picregq, [scan8_mem]
%endif
.nextblock:
    movzx        r6, byte [scan8+r5]
    movzx        r6, byte [r4+r6]
    test         r6, r6
    jz .skipblock
    mov         r6d, dword [r1+r5*4]
    lea          r6, [r0+r6]
    IDCT4_ADD    r6, r2, r3
.skipblock:
    inc          r5
    add          r2, 32
    cmp          r5, 16
    jl .nextblock
    REP_RET

; ff_h264_idct8_add4_mmx(uint8_t *dst, const int *block_offset,
;                        DCTELEM *block, int stride, const uint8_t nnzc[6*8])
cglobal h264_idct8_add4_8_mmx, 5, 7 + npicregs, 0, dst, block_offset, block, stride, nnzc, cntr, coeff, picreg
    %assign pad 128+4-(stack_offset&7)
    SUB         rsp, pad

    xor          r5, r5
%ifdef PIC
    lea     picregq, [scan8_mem]
%endif
.nextblock:
    movzx        r6, byte [scan8+r5]
    movzx        r6, byte [r4+r6]
    test         r6, r6
    jz .skipblock
    mov         r6d, dword [r1+r5*4]
    add          r6, r0
    add   word [r2], 32
    IDCT8_ADD_MMX_START r2  , rsp
    IDCT8_ADD_MMX_START r2+8, rsp+64
    IDCT8_ADD_MMX_END   r6  , rsp,   r3
    mov         r6d, dword [r1+r5*4]
    lea          r6, [r0+r6+4]
    IDCT8_ADD_MMX_END   r6  , rsp+8, r3
.skipblock:
    add          r5, 4
    add          r2, 128
    cmp          r5, 16
    jl .nextblock
    ADD         rsp, pad
    RET

; ff_h264_idct_add16_mmx2(uint8_t *dst, const int *block_offset,
;                         DCTELEM *block, int stride, const uint8_t nnzc[6*8])
cglobal h264_idct_add16_8_mmx2, 5, 8 + npicregs, 0, dst1, block_offset, block, stride, nnzc, cntr, coeff, dst2, picreg
    xor          r5, r5
%ifdef PIC
    lea     picregq, [scan8_mem]
%endif
.nextblock:
    movzx        r6, byte [scan8+r5]
    movzx        r6, byte [r4+r6]
    test         r6, r6
    jz .skipblock
    cmp          r6, 1
    jnz .no_dc
    movsx        r6, word [r2]
    test         r6, r6
    jz .no_dc
    DC_ADD_MMX2_INIT r2, r3, r6
%if ARCH_X86_64 == 0
%define dst2q r1
%define dst2d r1d
%endif
    mov       dst2d, dword [r1+r5*4]
    lea       dst2q, [r0+dst2q]
    DC_ADD_MMX2_OP movh, dst2q, r3, r6
%if ARCH_X86_64 == 0
    mov          r1, r1m
%endif
    inc          r5
    add          r2, 32
    cmp          r5, 16
    jl .nextblock
    REP_RET
.no_dc:
    mov         r6d, dword [r1+r5*4]
    add          r6, r0
    IDCT4_ADD    r6, r2, r3
.skipblock:
    inc          r5
    add          r2, 32
    cmp          r5, 16
    jl .nextblock
    REP_RET

; ff_h264_idct_add16intra_mmx(uint8_t *dst, const int *block_offset,
;                             DCTELEM *block, int stride, const uint8_t nnzc[6*8])
cglobal h264_idct_add16intra_8_mmx, 5, 7 + npicregs, 0, dst, block_offset, block, stride, nnzc, cntr, coeff, picreg
    xor          r5, r5
%ifdef PIC
    lea     picregq, [scan8_mem]
%endif
.nextblock:
    movzx        r6, byte [scan8+r5]
    movzx        r6, byte [r4+r6]
    or          r6w, word [r2]
    test         r6, r6
    jz .skipblock
    mov         r6d, dword [r1+r5*4]
    add          r6, r0
    IDCT4_ADD    r6, r2, r3
.skipblock:
    inc          r5
    add          r2, 32
    cmp          r5, 16
    jl .nextblock
    REP_RET

; ff_h264_idct_add16intra_mmx2(uint8_t *dst, const int *block_offset,
;                              DCTELEM *block, int stride, const uint8_t nnzc[6*8])
cglobal h264_idct_add16intra_8_mmx2, 5, 8 + npicregs, 0, dst1, block_offset, block, stride, nnzc, cntr, coeff, dst2, picreg
    xor          r5, r5
%ifdef PIC
    lea     picregq, [scan8_mem]
%endif
.nextblock:
    movzx        r6, byte [scan8+r5]
    movzx        r6, byte [r4+r6]
    test         r6, r6
    jz .try_dc
    mov         r6d, dword [r1+r5*4]
    lea          r6, [r0+r6]
    IDCT4_ADD    r6, r2, r3
    inc          r5
    add          r2, 32
    cmp          r5, 16
    jl .nextblock
    REP_RET
.try_dc:
    movsx        r6, word [r2]
    test         r6, r6
    jz .skipblock
    DC_ADD_MMX2_INIT r2, r3, r6
%if ARCH_X86_64 == 0
%define dst2q r1
%define dst2d r1d
%endif
    mov       dst2d, dword [r1+r5*4]
    add       dst2q, r0
    DC_ADD_MMX2_OP movh, dst2q, r3, r6
%if ARCH_X86_64 == 0
    mov          r1, r1m
%endif
.skipblock:
    inc          r5
    add          r2, 32
    cmp          r5, 16
    jl .nextblock
    REP_RET

; ff_h264_idct8_add4_mmx2(uint8_t *dst, const int *block_offset,
;                         DCTELEM *block, int stride, const uint8_t nnzc[6*8])
cglobal h264_idct8_add4_8_mmx2, 5, 8 + npicregs, 0, dst1, block_offset, block, stride, nnzc, cntr, coeff, dst2, picreg
    %assign pad 128+4-(stack_offset&7)
    SUB         rsp, pad

    xor          r5, r5
%ifdef PIC
    lea     picregq, [scan8_mem]
%endif
.nextblock:
    movzx        r6, byte [scan8+r5]
    movzx        r6, byte [r4+r6]
    test         r6, r6
    jz .skipblock
    cmp          r6, 1
    jnz .no_dc
    movsx        r6, word [r2]
    test         r6, r6
    jz .no_dc
    DC_ADD_MMX2_INIT r2, r3, r6
%if ARCH_X86_64 == 0
%define dst2q r1
%define dst2d r1d
%endif
    mov       dst2d, dword [r1+r5*4]
    lea       dst2q, [r0+dst2q]
    DC_ADD_MMX2_OP mova, dst2q, r3, r6
    lea       dst2q, [dst2q+r3*4]
    DC_ADD_MMX2_OP mova, dst2q, r3, r6
%if ARCH_X86_64 == 0
    mov          r1, r1m
%endif
    add          r5, 4
    add          r2, 128
    cmp          r5, 16
    jl .nextblock

    ADD         rsp, pad
    RET
.no_dc:
    mov         r6d, dword [r1+r5*4]
    add          r6, r0
    add   word [r2], 32
    IDCT8_ADD_MMX_START r2  , rsp
    IDCT8_ADD_MMX_START r2+8, rsp+64
    IDCT8_ADD_MMX_END   r6  , rsp,   r3
    mov         r6d, dword [r1+r5*4]
    lea          r6, [r0+r6+4]
    IDCT8_ADD_MMX_END   r6  , rsp+8, r3
.skipblock:
    add          r5, 4
    add          r2, 128
    cmp          r5, 16
    jl .nextblock

    ADD         rsp, pad
    RET

INIT_XMM
; ff_h264_idct8_add4_sse2(uint8_t *dst, const int *block_offset,
;                         DCTELEM *block, int stride, const uint8_t nnzc[6*8])
cglobal h264_idct8_add4_8_sse2, 5, 8 + npicregs, 10, dst1, block_offset, block, stride, nnzc, cntr, coeff, dst2, picreg
    xor          r5, r5
%ifdef PIC
    lea     picregq, [scan8_mem]
%endif
.nextblock:
    movzx        r6, byte [scan8+r5]
    movzx        r6, byte [r4+r6]
    test         r6, r6
    jz .skipblock
    cmp          r6, 1
    jnz .no_dc
    movsx        r6, word [r2]
    test         r6, r6
    jz .no_dc
INIT_MMX
    DC_ADD_MMX2_INIT r2, r3, r6
%if ARCH_X86_64 == 0
%define dst2q r1
%define dst2d r1d
%endif
    mov       dst2d, dword [r1+r5*4]
    add       dst2q, r0
    DC_ADD_MMX2_OP mova, dst2q, r3, r6
    lea       dst2q, [dst2q+r3*4]
    DC_ADD_MMX2_OP mova, dst2q, r3, r6
%if ARCH_X86_64 == 0
    mov          r1, r1m
%endif
    add          r5, 4
    add          r2, 128
    cmp          r5, 16
    jl .nextblock
    REP_RET
.no_dc:
INIT_XMM
    mov       dst2d, dword [r1+r5*4]
    add       dst2q, r0
    IDCT8_ADD_SSE dst2q, r2, r3, r6
%if ARCH_X86_64 == 0
    mov          r1, r1m
%endif
.skipblock:
    add          r5, 4
    add          r2, 128
    cmp          r5, 16
    jl .nextblock
    REP_RET

INIT_MMX
h264_idct_add8_mmx_plane:
.nextblock:
    movzx        r6, byte [scan8+r5]
    movzx        r6, byte [r4+r6]
    or          r6w, word [r2]
    test         r6, r6
    jz .skipblock
%if ARCH_X86_64
    mov         r0d, dword [r1+r5*4]
    add          r0, [dst2q]
%else
    mov          r0, r1m ; XXX r1m here is actually r0m of the calling func
    mov          r0, [r0]
    add          r0, dword [r1+r5*4]
%endif
    IDCT4_ADD    r0, r2, r3
.skipblock:
    inc          r5
    add          r2, 32
    test         r5, 3
    jnz .nextblock
    rep ret

; ff_h264_idct_add8_mmx(uint8_t **dest, const int *block_offset,
;                       DCTELEM *block, int stride, const uint8_t nnzc[6*8])
cglobal h264_idct_add8_8_mmx, 5, 8 + npicregs, 0, dst1, block_offset, block, stride, nnzc, cntr, coeff, dst2, picreg
    mov          r5, 16
    add          r2, 512
%ifdef PIC
    lea     picregq, [scan8_mem]
%endif
%if ARCH_X86_64
    mov       dst2q, r0
%endif
    call         h264_idct_add8_mmx_plane
    mov          r5, 32
    add          r2, 384
%if ARCH_X86_64
    add       dst2q, gprsize
%else
    add        r0mp, gprsize
%endif
    call         h264_idct_add8_mmx_plane
    RET

h264_idct_add8_mmx2_plane:
.nextblock:
    movzx        r6, byte [scan8+r5]
    movzx        r6, byte [r4+r6]
    test         r6, r6
    jz .try_dc
%if ARCH_X86_64
    mov         r0d, dword [r1+r5*4]
    add          r0, [dst2q]
%else
    mov          r0, r1m ; XXX r1m here is actually r0m of the calling func
    mov          r0, [r0]
    add          r0, dword [r1+r5*4]
%endif
    IDCT4_ADD    r0, r2, r3
    inc          r5
    add          r2, 32
    test         r5, 3
    jnz .nextblock
    rep ret
.try_dc:
    movsx        r6, word [r2]
    test         r6, r6
    jz .skipblock
    DC_ADD_MMX2_INIT r2, r3, r6
%if ARCH_X86_64
    mov         r0d, dword [r1+r5*4]
    add          r0, [dst2q]
%else
    mov          r0, r1m ; XXX r1m here is actually r0m of the calling func
    mov          r0, [r0]
    add          r0, dword [r1+r5*4]
%endif
    DC_ADD_MMX2_OP movh, r0, r3, r6
.skipblock:
    inc          r5
    add          r2, 32
    test         r5, 3
    jnz .nextblock
    rep ret

; ff_h264_idct_add8_mmx2(uint8_t **dest, const int *block_offset,
;                        DCTELEM *block, int stride, const uint8_t nnzc[6*8])
cglobal h264_idct_add8_8_mmx2, 5, 8 + npicregs, 0, dst1, block_offset, block, stride, nnzc, cntr, coeff, dst2, picreg
    mov          r5, 16
    add          r2, 512
%if ARCH_X86_64
    mov       dst2q, r0
%endif
%ifdef PIC
    lea     picregq, [scan8_mem]
%endif
    call h264_idct_add8_mmx2_plane
    mov          r5, 32
    add          r2, 384
%if ARCH_X86_64
    add       dst2q, gprsize
%else
    add        r0mp, gprsize
%endif
    call h264_idct_add8_mmx2_plane
    RET

INIT_MMX
; r0 = uint8_t *dst, r2 = int16_t *block, r3 = int stride, r6=clobbered
h264_idct_dc_add8_mmx2:
    movd         m0, [r2   ]          ;  0 0 X D
    punpcklwd    m0, [r2+32]          ;  x X d D
    paddsw       m0, [pw_32]
    psraw        m0, 6
    punpcklwd    m0, m0               ;  d d D D
    pxor         m1, m1               ;  0 0 0 0
    psubw        m1, m0               ; -d-d-D-D
    packuswb     m0, m1               ; -d-d-D-D d d D D
    pshufw       m1, m0, 0xFA         ; -d-d-d-d-D-D-D-D
    punpcklwd    m0, m0               ;  d d d d D D D D
    lea          r6, [r3*3]
    DC_ADD_MMX2_OP movq, r0, r3, r6
    ret

ALIGN 16
INIT_XMM
; r0 = uint8_t *dst (clobbered), r2 = int16_t *block, r3 = int stride
h264_add8x4_idct_sse2:
    movq   m0, [r2+ 0]
    movq   m1, [r2+ 8]
    movq   m2, [r2+16]
    movq   m3, [r2+24]
    movhps m0, [r2+32]
    movhps m1, [r2+40]
    movhps m2, [r2+48]
    movhps m3, [r2+56]
    IDCT4_1D w,0,1,2,3,4,5
    TRANSPOSE2x4x4W 0,1,2,3,4
    paddw m0, [pw_32]
    IDCT4_1D w,0,1,2,3,4,5
    pxor  m7, m7
    STORE_DIFFx2 m0, m1, m4, m5, m7, 6, r0, r3
    lea   r0, [r0+r3*2]
    STORE_DIFFx2 m2, m3, m4, m5, m7, 6, r0, r3
    ret

%macro add16_sse2_cycle 2
    movzx       r0, word [r4+%2]
    test        r0, r0
    jz .cycle%1end
    mov        r0d, dword [r1+%1*8]
%if ARCH_X86_64
    add         r0, r5
%else
    add         r0, r0m
%endif
    call        h264_add8x4_idct_sse2
.cycle%1end:
%if %1 < 7
    add         r2, 64
%endif
%endmacro

; ff_h264_idct_add16_sse2(uint8_t *dst, const int *block_offset,
;                         DCTELEM *block, int stride, const uint8_t nnzc[6*8])
cglobal h264_idct_add16_8_sse2, 5, 5 + ARCH_X86_64, 8
%if ARCH_X86_64
    mov         r5, r0
%endif
    ; unrolling of the loop leads to an average performance gain of
    ; 20-25%
    add16_sse2_cycle 0, 0xc
    add16_sse2_cycle 1, 0x14
    add16_sse2_cycle 2, 0xe
    add16_sse2_cycle 3, 0x16
    add16_sse2_cycle 4, 0x1c
    add16_sse2_cycle 5, 0x24
    add16_sse2_cycle 6, 0x1e
    add16_sse2_cycle 7, 0x26
    RET

%macro add16intra_sse2_cycle 2
    movzx       r0, word [r4+%2]
    test        r0, r0
    jz .try%1dc
    mov        r0d, dword [r1+%1*8]
%if ARCH_X86_64
    add         r0, r7
%else
    add         r0, r0m
%endif
    call        h264_add8x4_idct_sse2
    jmp .cycle%1end
.try%1dc:
    movsx       r0, word [r2   ]
    or         r0w, word [r2+32]
    jz .cycle%1end
    mov        r0d, dword [r1+%1*8]
%if ARCH_X86_64
    add         r0, r7
%else
    add         r0, r0m
%endif
    call        h264_idct_dc_add8_mmx2
.cycle%1end:
%if %1 < 7
    add         r2, 64
%endif
%endmacro

; ff_h264_idct_add16intra_sse2(uint8_t *dst, const int *block_offset,
;                              DCTELEM *block, int stride, const uint8_t nnzc[6*8])
cglobal h264_idct_add16intra_8_sse2, 5, 7 + ARCH_X86_64, 8
%if ARCH_X86_64
    mov         r7, r0
%endif
    add16intra_sse2_cycle 0, 0xc
    add16intra_sse2_cycle 1, 0x14
    add16intra_sse2_cycle 2, 0xe
    add16intra_sse2_cycle 3, 0x16
    add16intra_sse2_cycle 4, 0x1c
    add16intra_sse2_cycle 5, 0x24
    add16intra_sse2_cycle 6, 0x1e
    add16intra_sse2_cycle 7, 0x26
    RET

%macro add8_sse2_cycle 2
    movzx       r0, word [r4+%2]
    test        r0, r0
    jz .try%1dc
%if ARCH_X86_64
    mov        r0d, dword [r1+(%1&1)*8+64*(1+(%1>>1))]
    add         r0, [r7]
%else
    mov         r0, r0m
    mov         r0, [r0]
    add         r0, dword [r1+(%1&1)*8+64*(1+(%1>>1))]
%endif
    call        h264_add8x4_idct_sse2
    jmp .cycle%1end
.try%1dc:
    movsx       r0, word [r2   ]
    or         r0w, word [r2+32]
    jz .cycle%1end
%if ARCH_X86_64
    mov        r0d, dword [r1+(%1&1)*8+64*(1+(%1>>1))]
    add         r0, [r7]
%else
    mov         r0, r0m
    mov         r0, [r0]
    add         r0, dword [r1+(%1&1)*8+64*(1+(%1>>1))]
%endif
    call        h264_idct_dc_add8_mmx2
.cycle%1end:
%if %1 == 1
    add         r2, 384+64
%elif %1 < 3
    add         r2, 64
%endif
%endmacro

; ff_h264_idct_add8_sse2(uint8_t **dest, const int *block_offset,
;                        DCTELEM *block, int stride, const uint8_t nnzc[6*8])
cglobal h264_idct_add8_8_sse2, 5, 7 + ARCH_X86_64, 8
    add          r2, 512
%if ARCH_X86_64
    mov          r7, r0
%endif
    add8_sse2_cycle 0, 0x34
    add8_sse2_cycle 1, 0x3c
%if ARCH_X86_64
    add          r7, gprsize
%else
    add        r0mp, gprsize
%endif
    add8_sse2_cycle 2, 0x5c
    add8_sse2_cycle 3, 0x64
    RET

;void ff_h264_luma_dc_dequant_idct_mmx(DCTELEM *output, DCTELEM *input, int qmul)

%macro WALSH4_1D 5
    SUMSUB_BADC w, %4, %3, %2, %1, %5
    SUMSUB_BADC w, %4, %2, %3, %1, %5
    SWAP %1, %4, %3
%endmacro

%macro DEQUANT_MMX 3
    mova        m7, [pw_1]
    mova        m4, %1
    punpcklwd   %1, m7
    punpckhwd   m4, m7
    mova        m5, %2
    punpcklwd   %2, m7
    punpckhwd   m5, m7
    movd        m7, t3d
    punpckldq   m7, m7
    pmaddwd     %1, m7
    pmaddwd     %2, m7
    pmaddwd     m4, m7
    pmaddwd     m5, m7
    psrad       %1, %3
    psrad       %2, %3
    psrad       m4, %3
    psrad       m5, %3
    packssdw    %1, m4
    packssdw    %2, m5
%endmacro

%macro STORE_WORDS_MMX 5
    movd  t0d, %1
    psrlq  %1, 32
    movd  t1d, %1
    mov [t2+%2*32], t0w
    mov [t2+%4*32], t1w
    shr   t0d, 16
    shr   t1d, 16
    mov [t2+%3*32], t0w
    mov [t2+%5*32], t1w
%endmacro

%macro DEQUANT_STORE_MMX 1
    DEQUANT_MMX m0, m1, %1
    STORE_WORDS_MMX m0,  0,  1,  4,  5
    STORE_WORDS_MMX m1,  2,  3,  6,  7

    DEQUANT_MMX m2, m3, %1
    STORE_WORDS_MMX m2,  8,  9, 12, 13
    STORE_WORDS_MMX m3, 10, 11, 14, 15
%endmacro

%macro STORE_WORDS_SSE 9
    movd  t0d, %1
    psrldq  %1, 4
    movd  t1d, %1
    psrldq  %1, 4
    mov [t2+%2*32], t0w
    mov [t2+%4*32], t1w
    shr   t0d, 16
    shr   t1d, 16
    mov [t2+%3*32], t0w
    mov [t2+%5*32], t1w
    movd  t0d, %1
    psrldq  %1, 4
    movd  t1d, %1
    mov [t2+%6*32], t0w
    mov [t2+%8*32], t1w
    shr   t0d, 16
    shr   t1d, 16
    mov [t2+%7*32], t0w
    mov [t2+%9*32], t1w
%endmacro

%macro DEQUANT_STORE_SSE2 1
    movd      xmm4, t3d
    movq      xmm5, [pw_1]
    pshufd    xmm4, xmm4, 0
    movq2dq   xmm0, m0
    movq2dq   xmm1, m1
    movq2dq   xmm2, m2
    movq2dq   xmm3, m3
    punpcklwd xmm0, xmm5
    punpcklwd xmm1, xmm5
    punpcklwd xmm2, xmm5
    punpcklwd xmm3, xmm5
    pmaddwd   xmm0, xmm4
    pmaddwd   xmm1, xmm4
    pmaddwd   xmm2, xmm4
    pmaddwd   xmm3, xmm4
    psrad     xmm0, %1
    psrad     xmm1, %1
    psrad     xmm2, %1
    psrad     xmm3, %1
    packssdw  xmm0, xmm1
    packssdw  xmm2, xmm3
    STORE_WORDS_SSE xmm0,  0,  1,  4,  5,  2,  3,  6,  7
    STORE_WORDS_SSE xmm2,  8,  9, 12, 13, 10, 11, 14, 15
%endmacro

%macro IDCT_DC_DEQUANT 2
cglobal h264_luma_dc_dequant_idct_%1, 3,4,%2
    ; manually spill XMM registers for Win64 because
    ; the code here is initialized with INIT_MMX
    WIN64_SPILL_XMM %2
    movq        m3, [r1+24]
    movq        m2, [r1+16]
    movq        m1, [r1+ 8]
    movq        m0, [r1+ 0]
    WALSH4_1D    0,1,2,3,4
    TRANSPOSE4x4W 0,1,2,3,4
    WALSH4_1D    0,1,2,3,4

; shift, tmp, output, qmul
%if WIN64
    DECLARE_REG_TMP 0,3,1,2
    ; we can't avoid this, because r0 is the shift register (ecx) on win64
    xchg        r0, t2
%elif ARCH_X86_64
    DECLARE_REG_TMP 3,1,0,2
%else
    DECLARE_REG_TMP 1,3,0,2
%endif

    cmp        t3d, 32767
    jg .big_qmul
    add        t3d, 128 << 16
%ifidn %1,mmx
    DEQUANT_STORE_MMX 8
%else
    DEQUANT_STORE_SSE2 8
%endif
    RET
.big_qmul:
    bsr        t0d, t3d
    add        t3d, 128 << 16
    mov        t1d, 7
    cmp        t0d, t1d
    cmovg      t0d, t1d
    inc        t1d
    shr        t3d, t0b
    sub        t1d, t0d
%ifidn %1,mmx
    movd        m6, t1d
    DEQUANT_STORE_MMX m6
%else
    movd      xmm6, t1d
    DEQUANT_STORE_SSE2 xmm6
%endif
    RET
%endmacro

INIT_MMX
IDCT_DC_DEQUANT mmx, 0
IDCT_DC_DEQUANT sse2, 7
