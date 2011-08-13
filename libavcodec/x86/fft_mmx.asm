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

%include "libavutil/x86/x86inc.asm"

%ifdef ARCH_X86_64
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
endstruc

SECTION_RODATA

%define M_SQRT1_2 0.70710678118654752440
%define M_COS_PI_1_8 0.923879532511287
%define M_COS_PI_3_8 0.38268343236509

align 32
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

%assign i 16
%rep 13
cextern cos_ %+ i
%assign i i<<1
%endrep

%ifdef ARCH_X86_64
    %define pointer dq
%else
    %define pointer dd
%endif

%macro IF0 1+
%endmacro
%macro IF1 1+
    %1
%endmacro

SECTION_TEXT

%macro T2_3DN 4 ; z0, z1, mem0, mem1
    mova     %1, %3
    mova     %2, %1
    pfadd    %1, %4
    pfsub    %2, %4
%endmacro

%macro T4_3DN 6 ; z0, z1, z2, z3, tmp0, tmp1
    mova     %5, %3
    pfsub    %3, %4
    pfadd    %5, %4 ; {t6,t5}
    pxor     %3, [ps_m1p1] ; {t8,t7}
    mova     %6, %1
    pswapd   %3, %3
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

INIT_YMM

%ifdef HAVE_AVX
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

INIT_XMM
%define movdqa  movaps

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


INIT_MMX

%macro FFT48_3DN 1
align 16
fft4%1:
    T2_3DN   m0, m1, Z(0), Z(1)
    mova     m2, Z(2)
    mova     m3, Z(3)
    T4_3DN   m0, m1, m2, m3, m4, m5
    PUNPCK   m0, m1, m4
    PUNPCK   m2, m3, m5
    mova   Z(0), m0
    mova   Z(1), m4
    mova   Z(2), m2
    mova   Z(3), m5
    ret

align 16
fft8%1:
    T2_3DN   m0, m1, Z(0), Z(1)
    mova     m2, Z(2)
    mova     m3, Z(3)
    T4_3DN   m0, m1, m2, m3, m4, m5
    mova   Z(0), m0
    mova   Z(2), m2
    T2_3DN   m4, m5,  Z(4),  Z(5)
    T2_3DN   m6, m7, Z2(6), Z2(7)
    pswapd   m0, m5
    pswapd   m2, m7
    pxor     m0, [ps_m1p1]
    pxor     m2, [ps_m1p1]
    pfsub    m5, m0
    pfadd    m7, m2
    pfmul    m5, [ps_root2]
    pfmul    m7, [ps_root2]
    T4_3DN   m1, m3, m5, m7, m0, m2
    mova   Z(5), m5
    mova  Z2(7), m7
    mova     m0, Z(0)
    mova     m2, Z(2)
    T4_3DN   m0, m2, m4, m6, m5, m7
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

FFT48_3DN _3dn2

%macro pswapd 2
%ifidn %1, %2
    movd [r0+12], %1
    punpckhdq %1, [r0+8]
%else
    movq  %1, %2
    psrlq %1, 32
    punpckldq %1, %2
%endif
%endmacro

FFT48_3DN _3dn


%define Z(x) [zq + o1q*(x&6) + mmsize*(x&1)]
%define Z2(x) [zq + o3q + mmsize*(x&1)]
%define ZH(x) [zq + o1q*(x&6) + mmsize*(x&1) + mmsize/2]
%define Z2H(x) [zq + o3q + mmsize*(x&1) + mmsize/2]

%macro DECL_PASS 2+ ; name, payload
align 16
%1:
DEFINE_ARGS z, w, n, o1, o3
    lea o3q, [nq*3]
    lea o1q, [nq*8]
    shl o3q, 4
.loop:
    %2
    add zq, mmsize*2
    add wq, mmsize
    sub nd, mmsize/8
    jg .loop
    rep ret
%endmacro

INIT_YMM

%ifdef HAVE_AVX
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
%endif

INIT_XMM

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

INIT_MMX
%define mulps pfmul
%define addps pfadd
%define subps pfsub
%define unpcklps punpckldq
%define unpckhps punpckhdq
DECL_PASS pass_3dn, PASS_SMALL 1, [wq], [wq+o1q]
DECL_PASS pass_interleave_3dn, PASS_BIG 0
%define pass_3dn2 pass_3dn
%define pass_interleave_3dn2 pass_interleave_3dn

%ifdef PIC
%define SECTION_REL - $$
%else
%define SECTION_REL
%endif

%macro FFT_DISPATCH 2; clobbers 5 GPRs, 8 XMMs
    lea r2, [dispatch_tab%1]
    mov r2, [r2 + (%2q-2)*gprsize]
%ifdef PIC
    lea r3, [$$]
    add r2, r3
%endif
    call r2
%endmacro ; FFT_DISPATCH

%macro DECL_FFT 2-3 ; nbits, cpu, suffix
%xdefine list_of_fft fft4%2 SECTION_REL, fft8%2 SECTION_REL
%if %1>=5
%xdefine list_of_fft list_of_fft, fft16%2 SECTION_REL
%endif
%if %1>=6
%xdefine list_of_fft list_of_fft, fft32%3%2 SECTION_REL
%endif

%assign n 1<<%1
%rep 17-%1
%assign n2 n/2
%assign n4 n/4
%xdefine list_of_fft list_of_fft, fft %+ n %+ %3%2 SECTION_REL

align 16
fft %+ n %+ %3%2:
    call fft %+ n2 %+ %2
    add r0, n*4 - (n&(-2<<%1))
    call fft %+ n4 %+ %2
    add r0, n*2 - (n2&(-2<<%1))
    call fft %+ n4 %+ %2
    sub r0, n*6 + (n2&(-2<<%1))
    lea r1, [cos_ %+ n]
    mov r2d, n4/2
    jmp pass%3%2

%assign n n*2
%endrep
%undef n

align 8
dispatch_tab%3%2: pointer list_of_fft

section .text

; On x86_32, this function does the register saving and restoring for all of fft.
; The others pass args in registers and don't spill anything.
cglobal fft_dispatch%3%2, 2,5,8, z, nbits
    FFT_DISPATCH %3%2, nbits
%ifidn %2, _avx
    vzeroupper
%endif
    RET
%endmacro ; DECL_FFT

%ifdef HAVE_AVX
DECL_FFT 6, _avx
DECL_FFT 6, _avx, _interleave
%endif
DECL_FFT 5, _sse
DECL_FFT 5, _sse, _interleave
DECL_FFT 4, _3dn
DECL_FFT 4, _3dn, _interleave
DECL_FFT 4, _3dn2
DECL_FFT 4, _3dn2, _interleave

INIT_XMM
%undef mulps
%undef addps
%undef subps
%undef unpcklps
%undef unpckhps

%macro PREROTATER 5 ;-2*k, 2*k, input+n4, tcos+n8, tsin+n8
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

%macro DECL_IMDCT 2
cglobal imdct_half%1, 3,7,8; FFTContext *s, FFTSample *output, const FFTSample *input
%ifdef ARCH_X86_64
%define rrevtab r10
%define rtcos   r11
%define rtsin   r12
    push  r12
    push  r13
    push  r14
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
%ifndef ARCH_X86_64
    push  rtcos
    push  rtsin
%endif
    shr   r3, 1
    mov   rrevtab, [r0+FFTContext.revtab]
    add   rrevtab, r3
%ifndef ARCH_X86_64
    push  rrevtab
%endif

    sub   r3, 4
%ifdef ARCH_X86_64
    xor   r4, r4
    sub   r4, r3
%endif
.pre:
%ifndef ARCH_X86_64
;unspill
    xor   r4, r4
    sub   r4, r3
    mov   rtsin, [esp+4]
    mov   rtcos, [esp+8]
%endif

    PREROTATER r4, r3, r2, rtcos, rtsin
%ifdef ARCH_X86_64
    movzx  r5,  word [rrevtab+r4-4]
    movzx  r6,  word [rrevtab+r4-2]
    movzx  r13, word [rrevtab+r3]
    movzx  r14, word [rrevtab+r3+2]
    movlps [r1+r5 *8], xmm0
    movhps [r1+r6 *8], xmm0
    movlps [r1+r13*8], xmm1
    movhps [r1+r14*8], xmm1
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
    jns    .pre

    mov  r5, r0
    mov  r6, r1
    mov  r0, r1
    mov  r1d, [r5+FFTContext.nbits]

    FFT_DISPATCH %1, r1

    mov  r0d, [r5+FFTContext.mdctsize]
    add  r6, r0
    shr  r0, 1
%ifndef ARCH_X86_64
%define rtcos r2
%define rtsin r3
    mov  rtcos, [esp+8]
    mov  rtsin, [esp+4]
%endif
    neg  r0
    mov  r1, -mmsize
    sub  r1, r0
    %2 r0, r1, r6, rtcos, rtsin
%ifdef ARCH_X86_64
    pop  r14
    pop  r13
    pop  r12
%else
    add esp, 12
%endif
%ifidn avx_enabled, 1
    vzeroupper
%endif
    RET
%endmacro

DECL_IMDCT _sse, POSROTATESHUF

INIT_YMM

%ifdef HAVE_AVX
DECL_IMDCT _avx, POSROTATESHUF_AVX
%endif
