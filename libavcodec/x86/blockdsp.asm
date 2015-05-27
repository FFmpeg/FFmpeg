;******************************************************************************
;* SIMD-optimized clear block functions
;* Copyright (c) 2002 Michael Niedermayer
;* Copyright (c) 2008 Loren Merritt
;* Copyright (c) 2009 Fiona Glaser
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

SECTION .text

;----------------------------------------
; void ff_clear_block(int16_t *blocks);
;----------------------------------------
; %1 = number of xmm registers used
; %2 = number of inline store loops
%macro CLEAR_BLOCK 2
cglobal clear_block, 1, 1, %1, blocks
    ZERO  m0, m0
%assign %%i 0
%rep %2
    mova  [blocksq+mmsize*(0+%%i)], m0
    mova  [blocksq+mmsize*(1+%%i)], m0
    mova  [blocksq+mmsize*(2+%%i)], m0
    mova  [blocksq+mmsize*(3+%%i)], m0
    mova  [blocksq+mmsize*(4+%%i)], m0
    mova  [blocksq+mmsize*(5+%%i)], m0
    mova  [blocksq+mmsize*(6+%%i)], m0
    mova  [blocksq+mmsize*(7+%%i)], m0
%assign %%i %%i+8
%endrep
    RET
%endmacro

INIT_MMX mmx
%define ZERO pxor
CLEAR_BLOCK 0, 2
INIT_XMM sse
%define ZERO xorps
CLEAR_BLOCK 1, 1

;-----------------------------------------
; void ff_clear_blocks(int16_t *blocks);
;-----------------------------------------
; %1 = number of xmm registers used
%macro CLEAR_BLOCKS 1
cglobal clear_blocks, 1, 2, %1, blocks, len
    add   blocksq, 768
    mov      lenq, -768
    ZERO       m0, m0
.loop:
    mova  [blocksq+lenq+mmsize*0], m0
    mova  [blocksq+lenq+mmsize*1], m0
    mova  [blocksq+lenq+mmsize*2], m0
    mova  [blocksq+lenq+mmsize*3], m0
    mova  [blocksq+lenq+mmsize*4], m0
    mova  [blocksq+lenq+mmsize*5], m0
    mova  [blocksq+lenq+mmsize*6], m0
    mova  [blocksq+lenq+mmsize*7], m0
    add   lenq, mmsize*8
    js .loop
    RET
%endmacro

INIT_MMX mmx
%define ZERO pxor
CLEAR_BLOCKS 0
INIT_XMM sse
%define ZERO xorps
CLEAR_BLOCKS 1
