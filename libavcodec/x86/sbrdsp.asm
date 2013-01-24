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

%include "libavutil/x86/x86util.asm"

SECTION_RODATA
; mask equivalent for multiply by -1.0 1.0
ps_mask         times 2 dd 1<<31, 0
ps_neg          times 4 dd 1<<31

SECTION_TEXT

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

; static void sbr_hf_gen_c(float (*X_high)[2], const float (*X_low)[2],
;                          const float alpha0[2], const float alpha1[2],
;                          float bw, int start, int end)
;
cglobal sbr_hf_gen, 4,4,8, X_high, X_low, alpha0, alpha1, BW, S, E
    ; load alpha factors
%define bw m0
%if ARCH_X86_64 == 0 || WIN64
    movss      bw, BWm
%endif
    movlps     m2, [alpha1q]
    movlps     m1, [alpha0q]
    shufps     bw, bw, 0
    mulps      m2, bw             ; (a1[0] a1[1])*bw
    mulps      m1, bw             ; (a0[0] a0[1])*bw    = (a2 a3)
    mulps      m2, bw             ; (a1[0] a1[1])*bw*bw = (a0 a1)
    mova       m3, m1
    mova       m4, m2

    ; Set pointers
%if ARCH_X86_64 == 0 || WIN64
    ; start and end 6th and 7th args on stack
    mov        r2d, Sm
    mov        r3d, Em
%define  start r2q
%define  end   r3q
%else
; BW does not actually occupy a register, so shift by 1
%define  start BWq
%define  end   Sq
%endif
    sub      start, end          ; neg num of loops
    lea    X_highq, [X_highq + end*2*4]
    lea     X_lowq, [X_lowq  + end*2*4 - 2*2*4]
    shl      start, 3            ; offset from num loops

    mova        m0, [X_lowq + start]
    shufps      m3, m3, q1111
    shufps      m4, m4, q1111
    xorps       m3, [ps_mask]
    shufps      m1, m1, q0000
    shufps      m2, m2, q0000
    xorps       m4, [ps_mask]
.loop2:
    movu        m7, [X_lowq + start + 8]        ; BbCc
    mova        m6, m0
    mova        m5, m7
    shufps      m0, m0, q2301                   ; aAbB
    shufps      m7, m7, q2301                   ; bBcC
    mulps       m0, m4
    mulps       m7, m3
    mulps       m6, m2
    mulps       m5, m1
    addps       m7, m0
    mova        m0, [X_lowq + start +16]        ; CcDd
    addps       m7, m0
    addps       m6, m5
    addps       m7, m6
    mova  [X_highq + start], m7
    add     start, 16
    jnz         .loop2
    RET

cglobal sbr_sum64x5, 1,2,4,z
    lea    r1q, [zq+ 256]
.loop:
    mova    m0, [zq+   0]
    mova    m2, [zq+  16]
    mova    m1, [zq+ 256]
    mova    m3, [zq+ 272]
    addps   m0, [zq+ 512]
    addps   m2, [zq+ 528]
    addps   m1, [zq+ 768]
    addps   m3, [zq+ 784]
    addps   m0, [zq+1024]
    addps   m2, [zq+1040]
    addps   m0, m1
    addps   m2, m3
    mova  [zq], m0
    mova  [zq+16], m2
    add     zq, 32
    cmp     zq, r1q
    jne  .loop
    REP_RET

INIT_XMM sse
cglobal sbr_qmf_post_shuffle, 2,3,4,W,z
    lea              r2q, [zq + (64-4)*4]
    mova              m3, [ps_neg]
.loop:
    mova              m1, [zq]
    xorps             m0, m3, [r2q]
    shufps            m0, m0, m0, q0123
    unpcklps          m2, m0, m1
    unpckhps          m0, m0, m1
    mova       [Wq +  0], m2
    mova       [Wq + 16], m0
    add               Wq, 32
    sub              r2q, 16
    add               zq, 16
    cmp               zq, r2q
    jl             .loop
    REP_RET
