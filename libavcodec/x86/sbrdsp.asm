;******************************************************************************
;* AAC Spectral Band Replication decoding functions
;* Copyright (C) 2012 Christophe Gisquet <christophe.gisquet@gmail.com>
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
; mask equivalent for multiply by -1.0 1.0
ps_mask         times 2 dd 1<<31, 0
ps_mask2        times 2 dd 0, 1<<31
ps_mask3        dd  0, 0, 0, 1<<31
ps_noise0       times 2 dd  1.0,  0.0,
ps_noise2       times 2 dd -1.0,  0.0
ps_noise13      dd  0.0,  1.0, 0.0, -1.0
                dd  0.0, -1.0, 0.0,  1.0
                dd  0.0,  1.0, 0.0, -1.0
cextern         sbr_noise_table
cextern         ps_neg

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

; void ff_sbr_hf_gen_sse(float (*X_high)[2], const float (*X_low)[2],
;                        const float alpha0[2], const float alpha1[2],
;                        float bw, int start, int end)
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

INIT_XMM sse
cglobal sbr_neg_odd_64, 1,2,4,z
    lea        r1q, [zq+256]
.loop:
    mova        m0, [zq+ 0]
    mova        m1, [zq+16]
    mova        m2, [zq+32]
    mova        m3, [zq+48]
    xorps       m0, [ps_mask2]
    xorps       m1, [ps_mask2]
    xorps       m2, [ps_mask2]
    xorps       m3, [ps_mask2]
    mova   [zq+ 0], m0
    mova   [zq+16], m1
    mova   [zq+32], m2
    mova   [zq+48], m3
    add         zq, 64
    cmp         zq, r1q
    jne      .loop
    REP_RET

; void ff_sbr_qmf_deint_bfly_sse2(float *v, const float *src0, const float *src1)
%macro SBR_QMF_DEINT_BFLY  0
cglobal sbr_qmf_deint_bfly, 3,5,8, v,src0,src1,vrev,c
    mov               cq, 64*4-2*mmsize
    lea            vrevq, [vq + 64*4]
.loop:
    mova              m0, [src0q+cq]
    mova              m1, [src1q]
    mova              m4, [src0q+cq+mmsize]
    mova              m5, [src1q+mmsize]
%if cpuflag(sse2)
    pshufd            m2, m0, q0123
    pshufd            m3, m1, q0123
    pshufd            m6, m4, q0123
    pshufd            m7, m5, q0123
%else
    shufps            m2, m0, m0, q0123
    shufps            m3, m1, m1, q0123
    shufps            m6, m4, m4, q0123
    shufps            m7, m5, m5, q0123
%endif
    addps             m5, m2
    subps             m0, m7
    addps             m1, m6
    subps             m4, m3
    mova         [vrevq], m1
    mova  [vrevq+mmsize], m5
    mova         [vq+cq], m0
    mova  [vq+cq+mmsize], m4
    add            src1q, 2*mmsize
    add            vrevq, 2*mmsize
    sub               cq, 2*mmsize
    jge            .loop
    REP_RET
%endmacro

INIT_XMM sse
SBR_QMF_DEINT_BFLY

INIT_XMM sse2
SBR_QMF_DEINT_BFLY

INIT_XMM sse2
cglobal sbr_qmf_pre_shuffle, 1,4,6,z
%define OFFSET  (32*4-2*mmsize)
    mov       r3q, OFFSET
    lea       r1q, [zq + (32+1)*4]
    lea       r2q, [zq + 64*4]
    mova       m5, [ps_neg]
.loop:
    movu       m0, [r1q]
    movu       m2, [r1q + mmsize]
    movu       m1, [zq + r3q + 4 + mmsize]
    movu       m3, [zq + r3q + 4]

    pxor       m2, m5
    pxor       m0, m5
    pshufd     m2, m2, q0123
    pshufd     m0, m0, q0123
    SBUTTERFLY dq, 2, 3, 4
    SBUTTERFLY dq, 0, 1, 4
    mova  [r2q + 2*r3q + 0*mmsize], m2
    mova  [r2q + 2*r3q + 1*mmsize], m3
    mova  [r2q + 2*r3q + 2*mmsize], m0
    mova  [r2q + 2*r3q + 3*mmsize], m1
    add       r1q, 2*mmsize
    sub       r3q, 2*mmsize
    jge      .loop
    movq       m2, [zq]
    movq    [r2q], m2
    REP_RET

%ifdef PIC
%define NREGS 1
%if UNIX64
%define NOISE_TABLE r6q ; r5q is m_max
%else
%define NOISE_TABLE r5q
%endif
%else
%define NREGS 0
%define NOISE_TABLE sbr_noise_table
%endif

%macro LOAD_NST  1
%ifdef PIC
    lea  NOISE_TABLE, [%1]
    mova          m0, [kxq + NOISE_TABLE]
%else
    mova          m0, [kxq + %1]
%endif
%endmacro

INIT_XMM sse2
; sbr_hf_apply_noise_0(float (*Y)[2], const float *s_m,
;                      const float *q_filt, int noise,
;                      int kx, int m_max)
cglobal sbr_hf_apply_noise_0, 5,5+NREGS+UNIX64,8, Y,s_m,q_filt,noise,kx,m_max
    mova       m0, [ps_noise0]
    jmp apply_noise_main

; sbr_hf_apply_noise_1(float (*Y)[2], const float *s_m,
;                      const float *q_filt, int noise,
;                      int kx, int m_max)
cglobal sbr_hf_apply_noise_1, 5,5+NREGS+UNIX64,8, Y,s_m,q_filt,noise,kx,m_max
    and       kxq, 1
    shl       kxq, 4
    LOAD_NST  ps_noise13
    jmp apply_noise_main

; sbr_hf_apply_noise_2(float (*Y)[2], const float *s_m,
;                      const float *q_filt, int noise,
;                      int kx, int m_max)
cglobal sbr_hf_apply_noise_2, 5,5+NREGS+UNIX64,8, Y,s_m,q_filt,noise,kx,m_max
    mova       m0, [ps_noise2]
    jmp apply_noise_main

; sbr_hf_apply_noise_3(float (*Y)[2], const float *s_m,
;                      const float *q_filt, int noise,
;                      int kx, int m_max)
cglobal sbr_hf_apply_noise_3, 5,5+NREGS+UNIX64,8, Y,s_m,q_filt,noise,kx,m_max
    and       kxq, 1
    shl       kxq, 4
    LOAD_NST  ps_noise13+16

apply_noise_main:
%if ARCH_X86_64 == 0 || WIN64
    mov       kxd, m_maxm
%define count kxq
%else
%define count m_maxq
%endif
    movsxdifnidn    noiseq, noised
    dec    noiseq
    shl    count, 2
%ifdef PIC
    lea NOISE_TABLE, [sbr_noise_table]
%endif
    lea        Yq, [Yq + 2*count]
    add      s_mq, count
    add   q_filtq, count
    shl    noiseq, 3
    pxor       m5, m5
    neg    count
.loop:
    mova       m1, [q_filtq + count]
    movu       m3, [noiseq + NOISE_TABLE + 1*mmsize]
    movu       m4, [noiseq + NOISE_TABLE + 2*mmsize]
    add    noiseq, 2*mmsize
    and    noiseq, 0x1ff<<3
    punpckhdq  m2, m1, m1
    punpckldq  m1, m1
    mulps      m1, m3 ; m2 = q_filt[m] * ff_sbr_noise_table[noise]
    mulps      m2, m4 ; m2 = q_filt[m] * ff_sbr_noise_table[noise]
    mova       m3, [s_mq + count]
    ; TODO: replace by a vpermd in AVX2
    punpckhdq  m4, m3, m3
    punpckldq  m3, m3
    pcmpeqd    m6, m3, m5 ; m6 == 0
    pcmpeqd    m7, m4, m5 ; m7 == 0
    mulps      m3, m0 ; s_m[m] * phi_sign
    mulps      m4, m0 ; s_m[m] * phi_sign
    pand       m1, m6
    pand       m2, m7
    movu       m6, [Yq + 2*count]
    movu       m7, [Yq + 2*count + mmsize]
    addps      m3, m1
    addps      m4, m2
    addps      m6, m3
    addps      m7, m4
    movu    [Yq + 2*count], m6
    movu    [Yq + 2*count + mmsize], m7
    add    count, mmsize
    jl      .loop
    RET

INIT_XMM sse
cglobal sbr_qmf_deint_neg, 2,4,4,v,src,vrev,c
%define COUNT  32*4
%define OFFSET 32*4
    mov        cq, -COUNT
    lea     vrevq, [vq + OFFSET + COUNT]
    add        vq, OFFSET-mmsize
    add      srcq, 2*COUNT
    mova       m3, [ps_neg]
.loop:
    mova       m0, [srcq + 2*cq + 0*mmsize]
    mova       m1, [srcq + 2*cq + 1*mmsize]
    shufps     m2, m0, m1, q2020
    shufps     m1, m0, q1313
    xorps      m2, m3
    mova     [vq], m1
    mova  [vrevq + cq], m2
    sub        vq, mmsize
    add        cq, mmsize
    jl      .loop
    REP_RET

%macro SBR_AUTOCORRELATE 0
cglobal sbr_autocorrelate, 2,3,8,32, x, phi, cnt
    mov   cntq, 37*8
    add     xq, cntq
    neg   cntq

%if cpuflag(sse3)
%define   MOVH  movsd
    movddup m5, [xq+cntq]
%else
%define   MOVH  movlps
    movlps  m5, [xq+cntq]
    movlhps m5, m5
%endif
    MOVH    m7, [xq+cntq+8 ]
    MOVH    m1, [xq+cntq+16]
    shufps  m7, m7, q0110
    shufps  m1, m1, q0110
    mulps   m3, m5, m7   ;              x[0][0] * x[1][0], x[0][1] * x[1][1], x[0][0] * x[1][1], x[0][1] * x[1][0]
    mulps   m4, m5, m5   ;              x[0][0] * x[0][0], x[0][1] * x[0][1];
    mulps   m5, m1       ; real_sum2  = x[0][0] * x[2][0], x[0][1] * x[2][1]; imag_sum2 = x[0][0] * x[2][1], x[0][1] * x[2][0]
    movaps  [rsp   ], m3
    movaps  [rsp+16], m4
    add   cntq, 8

    MOVH    m2, [xq+cntq+16]
    movlhps m7, m7
    shufps  m2, m2, q0110
    mulps   m6, m7, m1   ; real_sum1  = x[1][0] * x[2][0], x[1][1] * x[2][1]; imag_sum1 += x[1][0] * x[2][1], x[1][1] * x[2][0]
    mulps   m4, m7, m2
    mulps   m7, m7       ; real_sum0  = x[1][0] * x[1][0], x[1][1] * x[1][1];
    addps   m5, m4       ; real_sum2 += x[1][0] * x[3][0], x[1][1] * x[3][1]; imag_sum2 += x[1][0] * x[3][1], x[1][1] * x[3][0]

align 16
.loop:
    add   cntq, 8
    MOVH    m0, [xq+cntq+16]
    movlhps m1, m1
    shufps  m0, m0, q0110
    mulps   m3, m1, m2
    mulps   m4, m1, m0
    mulps   m1, m1
    addps   m6, m3       ; real_sum1 += x[i][0] * x[i + 1][0], x[i][1] * x[i + 1][1]; imag_sum1 += x[i][0] * x[i + 1][1], x[i][1] * x[i + 1][0];
    addps   m5, m4       ; real_sum2 += x[i][0] * x[i + 2][0], x[i][1] * x[i + 2][1]; imag_sum2 += x[i][0] * x[i + 2][1], x[i][1] * x[i + 2][0];
    addps   m7, m1       ; real_sum0 += x[i][0] * x[i][0],     x[i][1] * x[i][1];
    add   cntq, 8
    MOVH    m1, [xq+cntq+16]
    movlhps m2, m2
    shufps  m1, m1, q0110
    mulps   m3, m2, m0
    mulps   m4, m2, m1
    mulps   m2, m2
    addps   m6, m3       ; real_sum1 += x[i][0] * x[i + 1][0], x[i][1] * x[i + 1][1]; imag_sum1 += x[i][0] * x[i + 1][1], x[i][1] * x[i + 1][0];
    addps   m5, m4       ; real_sum2 += x[i][0] * x[i + 2][0], x[i][1] * x[i + 2][1]; imag_sum2 += x[i][0] * x[i + 2][1], x[i][1] * x[i + 2][0];
    addps   m7, m2       ; real_sum0 += x[i][0] * x[i][0],     x[i][1] * x[i][1];
    add   cntq, 8
    MOVH    m2, [xq+cntq+16]
    movlhps m0, m0
    shufps  m2, m2, q0110
    mulps   m3, m0, m1
    mulps   m4, m0, m2
    mulps   m0, m0
    addps   m6, m3       ; real_sum1 += x[i][0] * x[i + 1][0], x[i][1] * x[i + 1][1]; imag_sum1 += x[i][0] * x[i + 1][1], x[i][1] * x[i + 1][0];
    addps   m5, m4       ; real_sum2 += x[i][0] * x[i + 2][0], x[i][1] * x[i + 2][1]; imag_sum2 += x[i][0] * x[i + 2][1], x[i][1] * x[i + 2][0];
    addps   m7, m0       ; real_sum0 += x[i][0] * x[i][0],     x[i][1] * x[i][1];
    jl .loop

    movlhps m1, m1
    mulps   m2, m1
    mulps   m1, m1
    addps   m2, m6       ; real_sum1 + x[38][0] * x[39][0], x[38][1] * x[39][1]; imag_sum1 + x[38][0] * x[39][1], x[38][1] * x[39][0];
    addps   m1, m7       ; real_sum0 + x[38][0] * x[38][0], x[38][1] * x[38][1];
    addps   m6, [rsp   ] ; real_sum1 + x[ 0][0] * x[ 1][0], x[ 0][1] * x[ 1][1]; imag_sum1 + x[ 0][0] * x[ 1][1], x[ 0][1] * x[ 1][0];
    addps   m7, [rsp+16] ; real_sum0 + x[ 0][0] * x[ 0][0], x[ 0][1] * x[ 0][1];

    xorps   m2, [ps_mask3]
    xorps   m5, [ps_mask3]
    xorps   m6, [ps_mask3]
    HADDPS  m2, m5, m3
    HADDPS  m7, m6, m4
%if cpuflag(sse3)
    movshdup m0, m1
%else
    movss   m0, m1
    shufps  m1, m1, q0001
%endif
    addss   m1, m0
    movaps  [phiq     ], m2
    movhps  [phiq+0x18], m7
    movss   [phiq+0x28], m7
    movss   [phiq+0x10], m1
    RET
%endmacro

INIT_XMM sse
SBR_AUTOCORRELATE
INIT_XMM sse3
SBR_AUTOCORRELATE
