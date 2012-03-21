;******************************************************************************
;* AAC Spectral Band Replication decoding functions
;* Copyright (C) 2012 Christophe Gisquet <christophe.gisquet@gmail.com>
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

;SECTION_RODATA
SECTION .text

INIT_XMM sse
cglobal sbr_sum_square, 2, 3, 6
    mov         r2, r1
    xorps       m0, m0
    xorps       m1, m1
    sar         r2, 3
    jz          .prepare
.loop:
    movu        m2, [r0 +  0]
    movu        m3, [r0 + 16]
    movu        m4, [r0 + 32]
    movu        m5, [r0 + 48]
    mulps       m2, m2
    mulps       m3, m3
    mulps       m4, m4
    mulps       m5, m5
    addps       m0, m2
    addps       m1, m3
    addps       m0, m4
    addps       m1, m5
    add         r0, 64
    dec         r2
    jnz         .loop
.prepare:
    and         r1, 7
    sar         r1, 1
    jz          .end
; len is a multiple of 2, thus there are at least 4 elements to process
.endloop:
    movu        m2, [r0]
    add         r0, 16
    mulps       m2, m2
    dec         r1
    addps       m0, m2
    jnz         .endloop
.end:
    addps       m0, m1
    movhlps     m2, m0
    addps       m0, m2
    movss       m1, m0
    shufps      m0, m0, 1
    addss       m0, m1
%if ARCH_X86_64 == 0
    movss       r0m,  m0
    fld         dword r0m
%endif
    RET

%define STEP  40*4*2
cglobal sbr_hf_g_filt, 5, 6, 5
    lea         r1, [r1 + 8*r4] ; offset by ixh elements into X_high
    mov         r5, r3
    and         r3, 0xFC
    lea         r2, [r2 + r3*4]
    lea         r0, [r0 + r3*8]
    neg         r3
    jz          .loop1
.loop4:
    movlps      m0, [r2 + 4*r3 + 0]
    movlps      m1, [r2 + 4*r3 + 8]
    movlps      m2, [r1 + 0*STEP]
    movlps      m3, [r1 + 2*STEP]
    movhps      m2, [r1 + 1*STEP]
    movhps      m3, [r1 + 3*STEP]
    unpcklps    m0, m0
    unpcklps    m1, m1
    mulps       m0, m2
    mulps       m1, m3
    movu        [r0 + 8*r3 +  0], m0
    movu        [r0 + 8*r3 + 16], m1
    add         r1, 4*STEP
    add         r3, 4
    jnz         .loop4
    and         r5, 3 ; number of single element loops
    jz          .end
.loop1: ; element 0 and 1 can be computed at the same time
    movss       m0, [r2]
    movlps      m2, [r1]
    unpcklps    m0, m0
    mulps       m2, m0
    movlps    [r0], m2
    add         r0, 8
    add         r2, 4
    add         r1, STEP
    dec         r5
    jnz         .loop1
.end:
    RET
