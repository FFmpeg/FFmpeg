;******************************************************************************
;* VC1 inverse transform
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

%macro INV_TRANS_INIT 0
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
