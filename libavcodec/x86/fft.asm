;******************************************************************************
;* FFT transform with SSE/3DNow optimizations
;* Copyright (c) 2008 Loren Merritt
;* Copyright (c) 2011 Vitor Sessak
;*
;* This algorithm (though not any of the implementation details) is
;* based on libdjbfft by D. J. Bernstein.
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

; These functions are not individually interchangeable with the C versions.
; While C takes arrays of FFTComplex, SSE/3DNow leave intermediate results
; in blocks as conventient to the vector size.
; i.e. {4x real, 4x imaginary, 4x real, ...} (or 2x respectively)

%include "libavutil/x86/x86util.asm"

%if ARCH_X86_64
%define pointer resq
%else
%define pointer resd
%endif

struc FFTContext
    .nbits:    resd 1
    .reverse:  resd 1
    .revtab:   pointer 1
    .tmpbuf:   pointer 1
    .mdctsize: resd 1
    .mdctbits: resd 1
    .tcos:     pointer 1
    .tsin:     pointer 1
    .fftperm:  pointer 1
    .fftcalc:  pointer 1
    .imdctcalc:pointer 1
    .imdcthalf:pointer 1
endstruc

SECTION_RODATA 32

%define M_SQRT1_2 0.70710678118654752440
%define M_COS_PI_1_8 0.923879532511287
%define M_COS_PI_3_8 0.38268343236509

ps_cos16_1: dd 1.0, M_COS_PI_1_8, M_SQRT1_2, M_COS_PI_3_8, 1.0, M_COS_PI_1_8, M_SQRT1_2, M_COS_PI_3_8
ps_cos16_2: dd 0, M_COS_PI_3_8, M_SQRT1_2, M_COS_PI_1_8, 0, -M_COS_PI_3_8, -M_SQRT1_2, -M_COS_PI_1_8

ps_root2: times 8 dd M_SQRT1_2
ps_root2mppm: dd -M_SQRT1_2, M_SQRT1_2, M_SQRT1_2, -M_SQRT1_2, -M_SQRT1_2, M_SQRT1_2, M_SQRT1_2, -M_SQRT1_2
ps_p1p1m1p1: dd 0, 0, 1<<31, 0, 0, 0, 1<<31, 0

perm1: dd 0x00, 0x02, 0x03, 0x01, 0x03, 0x00, 0x02, 0x01
perm2: dd 0x00, 0x01, 0x02, 0x03, 0x01, 0x00, 0x02, 0x03
ps_p1p1m1p1root2: dd 1.0, 1.0, -1.0, 1.0, M_SQRT1_2, M_SQRT1_2, M_SQRT1_2, M_SQRT1_2
ps_m1m1p1m1p1m1m1m1: dd 1<<31, 1<<31, 0, 1<<31, 0, 1<<31, 1<<31, 1<<31
ps_m1p1: dd 1<<31, 0

cextern ps_neg

%assign i 16
%rep 13
cextern cos_ %+ i
%assign i i<<1
%endrep

%if ARCH_X86_64
    %define pointer dq
%else
    %define pointer dd
%endif

%macro IF0 1+
%endmacro
%macro IF1 1+
    %1
%endmacro

SECTION .text

%macro T2_3DNOW 4 ; z0, z1, mem0, mem1
    mova     %1, %3
    mova     %2, %1
    pfadd    %1, %4
    pfsub    %2, %4
%endmacro

%macro T4_3DNOW 6 ; z0, z1, z2, z3, tmp0, tmp1
    mova     %5, %3
    pfsub    %3, %4
    pfadd    %5, %4 ; {t6,t5}
    pxor     %3, [ps_m1p1] ; {t8,t7}
    mova     %6, %1
    movd [r0+12], %3
    punpckhdq %3, [r0+8]
    pfadd    %1, %5 ; {r0,i0}
    pfsub    %6, %5 ; {r2,i2}
    mova     %4, %2
    pfadd    %2, %3 ; {r1,i1}
    pfsub    %4, %3 ; {r3,i3}
    SWAP     %3, %6
%endmacro

;  in: %1 = {r0,i0,r2,i2,r4,i4,r6,i6}
;      %2 = {r1,i1,r3,i3,r5,i5,r7,i7}
;      %3, %4, %5 tmp
; out: %1 = {r0,r1,r2,r3,i0,i1,i2,i3}
;      %2 = {r4,r5,r6,r7,i4,i5,i6,i7}
%macro T8_AVX 5
    vsubps     %5, %1, %2       ; v  = %1 - %2
    vaddps     %3, %1, %2       ; w  = %1 + %2
    vmulps     %2, %5, [ps_p1p1m1p1root2]  ; v *= vals1
    vpermilps  %2, %2, [perm1]
    vblendps   %1, %2, %3, 0x33 ; q = {w1,w2,v4,v2,w5,w6,v7,v6}
    vshufps    %5, %3, %2, 0x4e ; r = {w3,w4,v1,v3,w7,w8,v8,v5}
    vsubps     %4, %5, %1       ; s = r - q
    vaddps     %1, %5, %1       ; u = r + q
    vpermilps  %1, %1, [perm2]  ; k  = {u1,u2,u3,u4,u6,u5,u7,u8}
    vshufps    %5, %4, %1, 0xbb
    vshufps    %3, %4, %1, 0xee
    vperm2f128 %3, %3, %5, 0x13
    vxorps     %4, %4, [ps_m1m1p1m1p1m1m1m1]  ; s *= {1,1,-1,-1,1,-1,-1,-1}
    vshufps    %2, %1, %4, 0xdd
    vshufps    %1, %1, %4, 0x88
    vperm2f128 %4, %2, %1, 0x02 ; v  = {k1,k3,s1,s3,k2,k4,s2,s4}
    vperm2f128 %1, %1, %2, 0x13 ; w  = {k6,k8,s6,s8,k5,k7,s5,s7}
    vsubps     %5, %1, %3
    vblendps   %1, %5, %1, 0x55 ; w -= {0,s7,0,k7,0,s8,0,k8}
    vsubps     %2, %4, %1       ; %2 = v - w
    vaddps     %1, %4, %1       ; %1 = v + w
%endmacro

; In SSE mode do one fft4 transforms
; in:  %1={r0,i0,r2,i2} %2={r1,i1,r3,i3}
; out: %1={r0,r1,r2,r3} %2={i0,i1,i2,i3}
;
; In AVX mode do two fft4 transforms
; in:  %1={r0,i0,r2,i2,r4,i4,r6,i6} %2={r1,i1,r3,i3,r5,i5,r7,i7}
; out: %1={r0,r1,r2,r3,r4,r5,r6,r7} %2={i0,i1,i2,i3,i4,i5,i6,i7}
%macro T4_SSE 3
    subps    %3, %1, %2       ; {t3,t4,-t8,t7}
    addps    %1, %1, %2       ; {t1,t2,t6,t5}
    xorps    %3, %3, [ps_p1p1m1p1]
    shufps   %2, %1, %3, 0xbe ; {t6,t5,t7,t8}
    shufps   %1, %1, %3, 0x44 ; {t1,t2,t3,t4}
    subps    %3, %1, %2       ; {r2,i2,r3,i3}
    addps    %1, %1, %2       ; {r0,i0,r1,i1}
    shufps   %2, %1, %3, 0xdd ; {i0,i1,i2,i3}
    shufps   %1, %1, %3, 0x88 ; {r0,r1,r2,r3}
%endmacro

; In SSE mode do one FFT8
; in:  %1={r0,r1,r2,r3} %2={i0,i1,i2,i3} %3={r4,i4,r6,i6} %4={r5,i5,r7,i7}
; out: %1={r0,r1,r2,r3} %2={i0,i1,i2,i3} %1={r4,r5,r6,r7} %2={i4,i5,i6,i7}
;
; In AVX mode do two FFT8
; in:  %1={r0,i0,r2,i2,r8, i8, r10,i10} %2={r1,i1,r3,i3,r9, i9, r11,i11}
;      %3={r4,i4,r6,i6,r12,i12,r14,i14} %4={r5,i5,r7,i7,r13,i13,r15,i15}
; out: %1={r0,r1,r2,r3,r8, r9, r10,r11} %2={i0,i1,i2,i3,i8, i9, i10,i11}
;      %3={r4,r5,r6,r7,r12,r13,r14,r15} %4={i4,i5,i6,i7,i12,i13,i14,i15}
%macro T8_SSE 6
    addps    %6, %3, %4       ; {t1,t2,t3,t4}
    subps    %3, %3, %4       ; {r5,i5,r7,i7}
    shufps   %4, %3, %3, 0xb1 ; {i5,r5,i7,r7}
    mulps    %3, %3, [ps_root2mppm] ; {-r5,i5,r7,-i7}
    mulps    %4, %4, [ps_root2]
    addps    %3, %3, %4       ; {t8,t7,ta,t9}
    shufps   %4, %6, %3, 0x9c ; {t1,t4,t7,ta}
    shufps   %6, %6, %3, 0x36 ; {t3,t2,t9,t8}
    subps    %3, %6, %4       ; {t6,t5,tc,tb}
    addps    %6, %6, %4       ; {t1,t2,t9,ta}
    shufps   %5, %6, %3, 0x8d ; {t2,ta,t6,tc}
    shufps   %6, %6, %3, 0xd8 ; {t1,t9,t5,tb}
    subps    %3, %1, %6       ; {r4,r5,r6,r7}
    addps    %1, %1, %6       ; {r0,r1,r2,r3}
    subps    %4, %2, %5       ; {i4,i5,i6,i7}
    addps    %2, %2, %5       ; {i0,i1,i2,i3}
%endmacro

; scheduled for cpu-bound sizes
%macro PASS_SMALL 3 ; (to load m4-m7), wre, wim
IF%1 mova    m4, Z(4)
IF%1 mova    m5, Z(5)
    mova     m0, %2 ; wre
    mova     m1, %3 ; wim
    mulps    m2, m4, m0 ; r2*wre
IF%1 mova    m6, Z2(6)
    mulps    m3, m5, m1 ; i2*wim
IF%1 mova    m7, Z2(7)
    mulps    m4, m4, m1 ; r2*wim
    mulps    m5, m5, m0 ; i2*wre
    addps    m2, m2, m3 ; r2*wre + i2*wim
    mulps    m3, m1, m7 ; i3*wim
    subps    m5, m5, m4 ; i2*wre - r2*wim
    mulps    m1, m1, m6 ; r3*wim
    mulps    m4, m0, m6 ; r3*wre
    mulps    m0, m0, m7 ; i3*wre
    subps    m4, m4, m3 ; r3*wre - i3*wim
    mova     m3, Z(0)
    addps    m0, m0, m1 ; i3*wre + r3*wim
    subps    m1, m4, m2 ; t3
    addps    m4, m4, m2 ; t5
    subps    m3, m3, m4 ; r2
    addps    m4, m4, Z(0) ; r0
    mova     m6, Z(2)
    mova   Z(4), m3
    mova   Z(0), m4
    subps    m3, m5, m0 ; t4
    subps    m4, m6, m3 ; r3
    addps    m3, m3, m6 ; r1
    mova  Z2(6), m4
    mova   Z(2), m3
    mova     m2, Z(3)
    addps    m3, m5, m0 ; t6
    subps    m2, m2, m1 ; i3
    mova     m7, Z(1)
    addps    m1, m1, Z(3) ; i1
    mova  Z2(7), m2
    mova   Z(3), m1
    subps    m4, m7, m3 ; i2
    addps    m3, m3, m7 ; i0
    mova   Z(5), m4
    mova   Z(1), m3
%endmacro

; scheduled to avoid store->load aliasing
%macro PASS_BIG 1 ; (!interleave)
    mova     m4, Z(4) ; r2
    mova     m5, Z(5) ; i2
    mova     m0, [wq] ; wre
    mova     m1, [wq+o1q] ; wim
    mulps    m2, m4, m0 ; r2*wre
    mova     m6, Z2(6) ; r3
    mulps    m3, m5, m1 ; i2*wim
    mova     m7, Z2(7) ; i3
    mulps    m4, m4, m1 ; r2*wim
    mulps    m5, m5, m0 ; i2*wre
    addps    m2, m2, m3 ; r2*wre + i2*wim
    mulps    m3, m1, m7 ; i3*wim
    mulps    m1, m1, m6 ; r3*wim
    subps    m5, m5, m4 ; i2*wre - r2*wim
    mulps    m4, m0, m6 ; r3*wre
    mulps    m0, m0, m7 ; i3*wre
    subps    m4, m4, m3 ; r3*wre - i3*wim
    mova     m3, Z(0)
    addps    m0, m0, m1 ; i3*wre + r3*wim
    subps    m1, m4, m2 ; t3
    addps    m4, m4, m2 ; t5
    subps    m3, m3, m4 ; r2
    addps    m4, m4, Z(0) ; r0
    mova     m6, Z(2)
    mova   Z(4), m3
    mova   Z(0), m4
    subps    m3, m5, m0 ; t4
    subps    m4, m6, m3 ; r3
    addps    m3, m3, m6 ; r1
IF%1 mova Z2(6), m4
IF%1 mova  Z(2), m3
    mova     m2, Z(3)
    addps    m5, m5, m0 ; t6
    subps    m2, m2, m1 ; i3
    mova     m7, Z(1)
    addps    m1, m1, Z(3) ; i1
IF%1 mova Z2(7), m2
IF%1 mova  Z(3), m1
    subps    m6, m7, m5 ; i2
    addps    m5, m5, m7 ; i0
IF%1 mova  Z(5), m6
IF%1 mova  Z(1), m5
%if %1==0
    INTERL m1, m3, m7, Z, 2
    INTERL m2, m4, m0, Z2, 6

    mova     m1, Z(0)
    mova     m2, Z(4)

    INTERL m5, m1, m3, Z, 0
    INTERL m6, m2, m7, Z, 4
%endif
%endmacro

%macro PUNPCK 3
    mova      %3, %1
    punpckldq %1, %2
    punpckhdq %3, %2
%endmacro

%define Z(x) [r0+mmsize*x]
%define Z2(x) [r0+mmsize*x]
%define ZH(x) [r0+mmsize*x+mmsize/2]

INIT_YMM avx

%if HAVE_AVX_EXTERNAL
align 16
fft8_avx:
    mova      m0, Z(0)
    mova      m1, Z(1)
    T8_AVX    m0, m1, m2, m3, m4
    mova      Z(0), m0
    mova      Z(1), m1
    ret


align 16
fft16_avx:
    mova       m2, Z(2)
    mova       m3, Z(3)
    T4_SSE     m2, m3, m7

    mova       m0, Z(0)
    mova       m1, Z(1)
    T8_AVX     m0, m1, m4, m5, m7

    mova       m4, [ps_cos16_1]
    mova       m5, [ps_cos16_2]
    vmulps     m6, m2, m4
    vmulps     m7, m3, m5
    vaddps     m7, m7, m6
    vmulps     m2, m2, m5
    vmulps     m3, m3, m4
    vsubps     m3, m3, m2
    vblendps   m2, m7, m3, 0xf0
    vperm2f128 m3, m7, m3, 0x21
    vaddps     m4, m2, m3
    vsubps     m2, m3, m2
    vperm2f128 m2, m2, m2, 0x01
    vsubps     m3, m1, m2
    vaddps     m1, m1, m2
    vsubps     m5, m0, m4
    vaddps     m0, m0, m4
    vextractf128   Z(0), m0, 0
    vextractf128  ZH(0), m1, 0
    vextractf128   Z(1), m0, 1
    vextractf128  ZH(1), m1, 1
    vextractf128   Z(2), m5, 0
    vextractf128  ZH(2), m3, 0
    vextractf128   Z(3), m5, 1
    vextractf128  ZH(3), m3, 1
    ret

align 16
fft32_avx:
    call fft16_avx

    mova m0, Z(4)
    mova m1, Z(5)

    T4_SSE      m0, m1, m4

    mova m2, Z(6)
    mova m3, Z(7)

    T8_SSE      m0, m1, m2, m3, m4, m6
    ; m0={r0,r1,r2,r3,r8, r9, r10,r11} m1={i0,i1,i2,i3,i8, i9, i10,i11}
    ; m2={r4,r5,r6,r7,r12,r13,r14,r15} m3={i4,i5,i6,i7,i12,i13,i14,i15}

    vperm2f128  m4, m0, m2, 0x20
    vperm2f128  m5, m1, m3, 0x20
    vperm2f128  m6, m0, m2, 0x31
    vperm2f128  m7, m1, m3, 0x31

    PASS_SMALL 0, [cos_32], [cos_32+32]

    ret

fft32_interleave_avx:
    call fft32_avx
    mov r2d, 32
.deint_loop:
    mova     m2, Z(0)
    mova     m3, Z(1)
    vunpcklps      m0, m2, m3
    vunpckhps      m1, m2, m3
    vextractf128   Z(0), m0, 0
    vextractf128  ZH(0), m1, 0
    vextractf128   Z(1), m0, 1
    vextractf128  ZH(1), m1, 1
    add r0, mmsize*2
    sub r2d, mmsize/4
    jg .deint_loop
    ret

%endif

INIT_XMM sse

align 16
fft4_avx:
fft4_sse:
    mova     m0, Z(0)
    mova     m1, Z(1)
    T4_SSE   m0, m1, m2
    mova   Z(0), m0
    mova   Z(1), m1
    ret

align 16
fft8_sse:
    mova     m0, Z(0)
    mova     m1, Z(1)
    T4_SSE   m0, m1, m2
    mova     m2, Z(2)
    mova     m3, Z(3)
    T8_SSE   m0, m1, m2, m3, m4, m5
    mova   Z(0), m0
    mova   Z(1), m1
    mova   Z(2), m2
    mova   Z(3), m3
    ret

align 16
fft16_sse:
    mova     m0, Z(0)
    mova     m1, Z(1)
    T4_SSE   m0, m1, m2
    mova     m2, Z(2)
    mova     m3, Z(3)
    T8_SSE   m0, m1, m2, m3, m4, m5
    mova     m4, Z(4)
    mova     m5, Z(5)
    mova   Z(0), m0
    mova   Z(1), m1
    mova   Z(2), m2
    mova   Z(3), m3
    T4_SSE   m4, m5, m6
    mova     m6, Z2(6)
    mova     m7, Z2(7)
    T4_SSE   m6, m7, m0
    PASS_SMALL 0, [cos_16], [cos_16+16]
    ret


%macro FFT48_3DNOW 0
align 16
fft4 %+ SUFFIX:
    T2_3DNOW m0, m1, Z(0), Z(1)
    mova     m2, Z(2)
    mova     m3, Z(3)
    T4_3DNOW m0, m1, m2, m3, m4, m5
    PUNPCK   m0, m1, m4
    PUNPCK   m2, m3, m5
    mova   Z(0), m0
    mova   Z(1), m4
    mova   Z(2), m2
    mova   Z(3), m5
    ret

align 16
fft8 %+ SUFFIX:
    T2_3DNOW m0, m1, Z(0), Z(1)
    mova     m2, Z(2)
    mova     m3, Z(3)
    T4_3DNOW m0, m1, m2, m3, m4, m5
    mova   Z(0), m0
    mova   Z(2), m2
    T2_3DNOW m4, m5,  Z(4),  Z(5)
    T2_3DNOW m6, m7, Z2(6), Z2(7)
    PSWAPD   m0, m5
    PSWAPD   m2, m7
    pxor     m0, [ps_m1p1]
    pxor     m2, [ps_m1p1]
    pfsub    m5, m0
    pfadd    m7, m2
    pfmul    m5, [ps_root2]
    pfmul    m7, [ps_root2]
    T4_3DNOW m1, m3, m5, m7, m0, m2
    mova   Z(5), m5
    mova  Z2(7), m7
    mova     m0, Z(0)
    mova     m2, Z(2)
    T4_3DNOW m0, m2, m4, m6, m5, m7
    PUNPCK   m0, m1, m5
    PUNPCK   m2, m3, m7
    mova   Z(0), m0
    mova   Z(1), m5
    mova   Z(2), m2
    mova   Z(3), m7
    PUNPCK   m4,  Z(5), m5
    PUNPCK   m6, Z2(7), m7
    mova   Z(4), m4
    mova   Z(5), m5
    mova  Z2(6), m6
    mova  Z2(7), m7
    ret
%endmacro

%if ARCH_X86_32
INIT_MMX 3dnowext
FFT48_3DNOW

INIT_MMX 3dnow
FFT48_3DNOW
%endif

%define Z(x) [zcq + o1q*(x&6) + mmsize*(x&1)]
%define Z2(x) [zcq + o3q + mmsize*(x&1)]
%define ZH(x) [zcq + o1q*(x&6) + mmsize*(x&1) + mmsize/2]
%define Z2H(x) [zcq + o3q + mmsize*(x&1) + mmsize/2]

%macro DECL_PASS 2+ ; name, payload
align 16
%1:
DEFINE_ARGS zc, w, n, o1, o3
    lea o3q, [nq*3]
    lea o1q, [nq*8]
    shl o3q, 4
.loop:
    %2
    add zcq, mmsize*2
    add  wq, mmsize
    sub  nd, mmsize/8
    jg .loop
    rep ret
%endmacro

%macro FFT_DISPATCH 2; clobbers 5 GPRs, 8 XMMs
    lea r2, [dispatch_tab%1]
    mov r2, [r2 + (%2q-2)*gprsize]
%ifdef PIC
    lea r3, [$$]
    add r2, r3
%endif
    call r2
%endmacro ; FFT_DISPATCH

INIT_YMM avx

%if HAVE_AVX_EXTERNAL
%macro INTERL_AVX 5
    vunpckhps      %3, %2, %1
    vunpcklps      %2, %2, %1
    vextractf128   %4(%5), %2, 0
    vextractf128  %4 %+ H(%5), %3, 0
    vextractf128   %4(%5 + 1), %2, 1
    vextractf128  %4 %+ H(%5 + 1), %3, 1
%endmacro

%define INTERL INTERL_AVX

DECL_PASS pass_avx, PASS_BIG 1
DECL_PASS pass_interleave_avx, PASS_BIG 0

cglobal fft_calc, 2,5,8
    mov     r3d, [r0 + FFTContext.nbits]
    mov     r0, r1
    mov     r1, r3
    FFT_DISPATCH _interleave %+ SUFFIX, r1
    REP_RET

%endif

INIT_XMM sse

%macro INTERL_SSE 5
    mova     %3, %2
    unpcklps %2, %1
    unpckhps %3, %1
    mova  %4(%5), %2
    mova  %4(%5+1), %3
%endmacro

%define INTERL INTERL_SSE

DECL_PASS pass_sse, PASS_BIG 1
DECL_PASS pass_interleave_sse, PASS_BIG 0

%macro FFT_CALC_FUNC 0
cglobal fft_calc, 2,5,8
    mov     r3d, [r0 + FFTContext.nbits]
    PUSH    r1
    PUSH    r3
    mov     r0, r1
    mov     r1, r3
    FFT_DISPATCH _interleave %+ SUFFIX, r1
    POP     rcx
    POP     r4
    cmp     rcx, 3+(mmsize/16)
    jg      .end
    mov     r2, -1
    add     rcx, 3
    shl     r2, cl
    sub     r4, r2
.loop:
%if mmsize == 8
    PSWAPD  m0, [r4 + r2 + 4]
    mova [r4 + r2 + 4], m0
%else
    movaps   xmm0, [r4 + r2]
    movaps   xmm1, xmm0
    unpcklps xmm0, [r4 + r2 + 16]
    unpckhps xmm1, [r4 + r2 + 16]
    movaps   [r4 + r2],      xmm0
    movaps   [r4 + r2 + 16], xmm1
%endif
    add      r2, mmsize*2
    jl       .loop
.end:
%if cpuflag(3dnow)
    femms
    RET
%else
    REP_RET
%endif
%endmacro

%if ARCH_X86_32
INIT_MMX 3dnow
FFT_CALC_FUNC
INIT_MMX 3dnowext
FFT_CALC_FUNC
%endif
INIT_XMM sse
FFT_CALC_FUNC

cglobal fft_permute, 2,7,1
    mov     r4,  [r0 + FFTContext.revtab]
    mov     r5,  [r0 + FFTContext.tmpbuf]
    mov     ecx, [r0 + FFTContext.nbits]
    mov     r2, 1
    shl     r2, cl
    xor     r0, r0
%if ARCH_X86_32
    mov     r1, r1m
%endif
.loop:
    movaps  xmm0, [r1 + 8*r0]
    movzx   r6, word [r4 + 2*r0]
    movzx   r3, word [r4 + 2*r0 + 2]
    movlps  [r5 + 8*r6], xmm0
    movhps  [r5 + 8*r3], xmm0
    add     r0, 2
    cmp     r0, r2
    jl      .loop
    shl     r2, 3
    add     r1, r2
    add     r5, r2
    neg     r2
; nbits >= 2 (FFT4) and sizeof(FFTComplex)=8 => at least 32B
.loopcopy:
    movaps  xmm0, [r5 + r2]
    movaps  xmm1, [r5 + r2 + 16]
    movaps  [r1 + r2], xmm0
    movaps  [r1 + r2 + 16], xmm1
    add     r2, 32
    jl      .loopcopy
    REP_RET

%macro IMDCT_CALC_FUNC 0
cglobal imdct_calc, 3,5,3
    mov     r3d, [r0 + FFTContext.mdctsize]
    mov     r4,  [r0 + FFTContext.imdcthalf]
    add     r1,  r3
    PUSH    r3
    PUSH    r1
%if ARCH_X86_32
    push    r2
    push    r1
    push    r0
%else
    sub     rsp, 8+32*WIN64 ; allocate win64 shadow space
%endif
    call    r4
%if ARCH_X86_32
    add     esp, 12
%else
    add     rsp, 8+32*WIN64
%endif
    POP     r1
    POP     r3
    lea     r0, [r1 + 2*r3]
    mov     r2, r3
    sub     r3, mmsize
    neg     r2
    mova    m2, [ps_neg]
.loop:
%if mmsize == 8
    PSWAPD  m0, [r1 + r3]
    PSWAPD  m1, [r0 + r2]
    pxor    m0, m2
%else
    mova    m0, [r1 + r3]
    mova    m1, [r0 + r2]
    shufps  m0, m0, 0x1b
    shufps  m1, m1, 0x1b
    xorps   m0, m2
%endif
    mova [r0 + r3], m1
    mova [r1 + r2], m0
    sub     r3, mmsize
    add     r2, mmsize
    jl      .loop
%if cpuflag(3dnow)
    femms
    RET
%else
    REP_RET
%endif
%endmacro

%if ARCH_X86_32
INIT_MMX 3dnow
IMDCT_CALC_FUNC
INIT_MMX 3dnowext
IMDCT_CALC_FUNC
%endif

INIT_XMM sse
IMDCT_CALC_FUNC

%if ARCH_X86_32
INIT_MMX 3dnow
%define mulps pfmul
%define addps pfadd
%define subps pfsub
%define unpcklps punpckldq
%define unpckhps punpckhdq
DECL_PASS pass_3dnow, PASS_SMALL 1, [wq], [wq+o1q]
DECL_PASS pass_interleave_3dnow, PASS_BIG 0
%define pass_3dnowext pass_3dnow
%define pass_interleave_3dnowext pass_interleave_3dnow
%endif

%ifdef PIC
%define SECTION_REL - $$
%else
%define SECTION_REL
%endif

%macro DECL_FFT 1-2 ; nbits, suffix
%ifidn %0, 1
%xdefine fullsuffix SUFFIX
%else
%xdefine fullsuffix %2 %+ SUFFIX
%endif
%xdefine list_of_fft fft4 %+ SUFFIX SECTION_REL, fft8 %+ SUFFIX SECTION_REL
%if %1>=5
%xdefine list_of_fft list_of_fft, fft16 %+ SUFFIX SECTION_REL
%endif
%if %1>=6
%xdefine list_of_fft list_of_fft, fft32 %+ fullsuffix SECTION_REL
%endif

%assign n 1<<%1
%rep 17-%1
%assign n2 n/2
%assign n4 n/4
%xdefine list_of_fft list_of_fft, fft %+ n %+ fullsuffix SECTION_REL

align 16
fft %+ n %+ fullsuffix:
    call fft %+ n2 %+ SUFFIX
    add r0, n*4 - (n&(-2<<%1))
    call fft %+ n4 %+ SUFFIX
    add r0, n*2 - (n2&(-2<<%1))
    call fft %+ n4 %+ SUFFIX
    sub r0, n*6 + (n2&(-2<<%1))
    lea r1, [cos_ %+ n]
    mov r2d, n4/2
    jmp pass %+ fullsuffix

%assign n n*2
%endrep
%undef n

align 8
dispatch_tab %+ fullsuffix: pointer list_of_fft
%endmacro ; DECL_FFT

%if HAVE_AVX_EXTERNAL
INIT_YMM avx
DECL_FFT 6
DECL_FFT 6, _interleave
%endif
INIT_XMM sse
DECL_FFT 5
DECL_FFT 5, _interleave
%if ARCH_X86_32
INIT_MMX 3dnow
DECL_FFT 4
DECL_FFT 4, _interleave
INIT_MMX 3dnowext
DECL_FFT 4
DECL_FFT 4, _interleave
%endif

INIT_XMM sse
%undef mulps
%undef addps
%undef subps
%undef unpcklps
%undef unpckhps

%macro PREROTATER 5 ;-2*k, 2*k, input+n4, tcos+n8, tsin+n8
%if mmsize == 8 ; j*2+2-n4, n4-2-j*2, input+n4, tcos+n8, tsin+n8
    PSWAPD     m0, [%3+%2*4]
    movq       m2, [%3+%1*4-8]
    movq       m3, m0
    punpckldq  m0, m2
    punpckhdq  m2, m3
    movd       m1, [%4+%1*2-4] ; tcos[j]
    movd       m3, [%4+%2*2]   ; tcos[n4-j-1]
    punpckldq  m1, [%5+%1*2-4] ; tsin[j]
    punpckldq  m3, [%5+%2*2]   ; tsin[n4-j-1]

    mova       m4, m0
    PSWAPD     m5, m1
    pfmul      m0, m1
    pfmul      m4, m5
    mova       m6, m2
    PSWAPD     m5, m3
    pfmul      m2, m3
    pfmul      m6, m5
%if cpuflag(3dnowext)
    pfpnacc    m0, m4
    pfpnacc    m2, m6
%else
    SBUTTERFLY dq, 0, 4, 1
    SBUTTERFLY dq, 2, 6, 3
    pxor       m4, m7
    pxor       m6, m7
    pfadd      m0, m4
    pfadd      m2, m6
%endif
%else
    movaps   xmm0, [%3+%2*4]
    movaps   xmm1, [%3+%1*4-0x10]
    movaps   xmm2, xmm0
    shufps   xmm0, xmm1, 0x88
    shufps   xmm1, xmm2, 0x77
    movlps   xmm4, [%4+%2*2]
    movlps   xmm5, [%5+%2*2+0x0]
    movhps   xmm4, [%4+%1*2-0x8]
    movhps   xmm5, [%5+%1*2-0x8]
    movaps   xmm2, xmm0
    movaps   xmm3, xmm1
    mulps    xmm0, xmm5
    mulps    xmm1, xmm4
    mulps    xmm2, xmm4
    mulps    xmm3, xmm5
    subps    xmm1, xmm0
    addps    xmm2, xmm3
    movaps   xmm0, xmm1
    unpcklps xmm1, xmm2
    unpckhps xmm0, xmm2
%endif
%endmacro

%macro CMUL 6 ;j, xmm0, xmm1, 3, 4, 5
    mulps      m6, %3, [%5+%1]
    mulps      m7, %2, [%5+%1]
    mulps      %2, %2, [%6+%1]
    mulps      %3, %3, [%6+%1]
    subps      %2, %2, m6
    addps      %3, %3, m7
%endmacro

%macro POSROTATESHUF_AVX 5 ;j, k, z+n8, tcos+n8, tsin+n8
.post:
    vmovaps      ymm1,   [%3+%1*2]
    vmovaps      ymm0,   [%3+%1*2+0x20]
    vmovaps      ymm3,   [%3+%2*2]
    vmovaps      ymm2,   [%3+%2*2+0x20]

    CMUL         %1, ymm0, ymm1, %3, %4, %5
    CMUL         %2, ymm2, ymm3, %3, %4, %5
    vshufps      ymm1, ymm1, ymm1, 0x1b
    vshufps      ymm3, ymm3, ymm3, 0x1b
    vperm2f128   ymm1, ymm1, ymm1, 0x01
    vperm2f128   ymm3, ymm3, ymm3, 0x01
    vunpcklps    ymm6, ymm2, ymm1
    vunpckhps    ymm4, ymm2, ymm1
    vunpcklps    ymm7, ymm0, ymm3
    vunpckhps    ymm5, ymm0, ymm3

    vextractf128 [%3+%1*2],      ymm7, 0
    vextractf128 [%3+%1*2+0x10], ymm5, 0
    vextractf128 [%3+%1*2+0x20], ymm7, 1
    vextractf128 [%3+%1*2+0x30], ymm5, 1

    vextractf128 [%3+%2*2],      ymm6, 0
    vextractf128 [%3+%2*2+0x10], ymm4, 0
    vextractf128 [%3+%2*2+0x20], ymm6, 1
    vextractf128 [%3+%2*2+0x30], ymm4, 1
    sub      %2,   0x20
    add      %1,   0x20
    jl       .post
%endmacro

%macro POSROTATESHUF 5 ;j, k, z+n8, tcos+n8, tsin+n8
.post:
    movaps   xmm1, [%3+%1*2]
    movaps   xmm0, [%3+%1*2+0x10]
    CMUL     %1,   xmm0, xmm1, %3, %4, %5
    movaps   xmm5, [%3+%2*2]
    movaps   xmm4, [%3+%2*2+0x10]
    CMUL     %2,   xmm4, xmm5, %3, %4, %5
    shufps   xmm1, xmm1, 0x1b
    shufps   xmm5, xmm5, 0x1b
    movaps   xmm6, xmm4
    unpckhps xmm4, xmm1
    unpcklps xmm6, xmm1
    movaps   xmm2, xmm0
    unpcklps xmm0, xmm5
    unpckhps xmm2, xmm5
    movaps   [%3+%2*2],      xmm6
    movaps   [%3+%2*2+0x10], xmm4
    movaps   [%3+%1*2],      xmm0
    movaps   [%3+%1*2+0x10], xmm2
    sub      %2,   0x10
    add      %1,   0x10
    jl       .post
%endmacro

%macro CMUL_3DNOW 6
    mova       m6, [%1+%2*2]
    mova       %3, [%1+%2*2+8]
    mova       %4, m6
    mova       m7, %3
    pfmul      m6, [%5+%2]
    pfmul      %3, [%6+%2]
    pfmul      %4, [%6+%2]
    pfmul      m7, [%5+%2]
    pfsub      %3, m6
    pfadd      %4, m7
%endmacro

%macro POSROTATESHUF_3DNOW 5 ;j, k, z+n8, tcos+n8, tsin+n8
.post:
    CMUL_3DNOW %3, %1, m0, m1, %4, %5
    CMUL_3DNOW %3, %2, m2, m3, %4, %5
    movd  [%3+%1*2+ 0], m0
    movd  [%3+%2*2+12], m1
    movd  [%3+%2*2+ 0], m2
    movd  [%3+%1*2+12], m3
    psrlq      m0, 32
    psrlq      m1, 32
    psrlq      m2, 32
    psrlq      m3, 32
    movd  [%3+%1*2+ 8], m0
    movd  [%3+%2*2+ 4], m1
    movd  [%3+%2*2+ 8], m2
    movd  [%3+%1*2+ 4], m3
    sub        %2, 8
    add        %1, 8
    jl         .post
%endmacro

%macro DECL_IMDCT 1
cglobal imdct_half, 3,12,8; FFTContext *s, FFTSample *output, const FFTSample *input
%if ARCH_X86_64
%define rrevtab r7
%define rtcos   r8
%define rtsin   r9
%else
%define rrevtab r6
%define rtsin   r6
%define rtcos   r5
%endif
    mov   r3d, [r0+FFTContext.mdctsize]
    add   r2, r3
    shr   r3, 1
    mov   rtcos, [r0+FFTContext.tcos]
    mov   rtsin, [r0+FFTContext.tsin]
    add   rtcos, r3
    add   rtsin, r3
%if ARCH_X86_64 == 0
    push  rtcos
    push  rtsin
%endif
    shr   r3, 1
    mov   rrevtab, [r0+FFTContext.revtab]
    add   rrevtab, r3
%if ARCH_X86_64 == 0
    push  rrevtab
%endif

%if mmsize == 8
    sub   r3, 2
%else
    sub   r3, 4
%endif
%if ARCH_X86_64 || mmsize == 8
    xor   r4, r4
    sub   r4, r3
%endif
%if notcpuflag(3dnowext) && mmsize == 8
    movd  m7, [ps_neg]
%endif
.pre:
%if ARCH_X86_64 == 0
;unspill
%if mmsize != 8
    xor   r4, r4
    sub   r4, r3
%endif
    mov   rtcos, [esp+8]
    mov   rtsin, [esp+4]
%endif

    PREROTATER r4, r3, r2, rtcos, rtsin
%if mmsize == 8
    mov    r6, [esp]                ; rrevtab = ptr+n8
    movzx  r5,  word [rrevtab+r4-2] ; rrevtab[j]
    movzx  r6,  word [rrevtab+r3]   ; rrevtab[n4-j-1]
    mova [r1+r5*8], m0
    mova [r1+r6*8], m2
    add    r4, 2
    sub    r3, 2
%else
%if ARCH_X86_64
    movzx  r5,  word [rrevtab+r4-4]
    movzx  r6,  word [rrevtab+r4-2]
    movzx  r10, word [rrevtab+r3]
    movzx  r11, word [rrevtab+r3+2]
    movlps [r1+r5 *8], xmm0
    movhps [r1+r6 *8], xmm0
    movlps [r1+r10*8], xmm1
    movhps [r1+r11*8], xmm1
    add    r4, 4
%else
    mov    r6, [esp]
    movzx  r5, word [r6+r4-4]
    movzx  r4, word [r6+r4-2]
    movlps [r1+r5*8], xmm0
    movhps [r1+r4*8], xmm0
    movzx  r5, word [r6+r3]
    movzx  r4, word [r6+r3+2]
    movlps [r1+r5*8], xmm1
    movhps [r1+r4*8], xmm1
%endif
    sub    r3, 4
%endif
    jns    .pre

    mov  r5, r0
    mov  r6, r1
    mov  r0, r1
    mov  r1d, [r5+FFTContext.nbits]

    FFT_DISPATCH SUFFIX, r1

    mov  r0d, [r5+FFTContext.mdctsize]
    add  r6, r0
    shr  r0, 1
%if ARCH_X86_64 == 0
%define rtcos r2
%define rtsin r3
    mov  rtcos, [esp+8]
    mov  rtsin, [esp+4]
%endif
    neg  r0
    mov  r1, -mmsize
    sub  r1, r0
    %1 r0, r1, r6, rtcos, rtsin
%if ARCH_X86_64 == 0
    add esp, 12
%endif
%if mmsize == 8
    femms
%endif
    RET
%endmacro

DECL_IMDCT POSROTATESHUF

%if ARCH_X86_32
INIT_MMX 3dnow
DECL_IMDCT POSROTATESHUF_3DNOW

INIT_MMX 3dnowext
DECL_IMDCT POSROTATESHUF_3DNOW
%endif

INIT_YMM avx

%if HAVE_AVX_EXTERNAL
DECL_IMDCT POSROTATESHUF_AVX
%endif
