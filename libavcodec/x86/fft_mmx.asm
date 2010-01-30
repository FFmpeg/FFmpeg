;******************************************************************************
;* FFT transform with SSE/3DNow optimizations
;* Copyright (c) 2008 Loren Merritt
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
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

; These functions are not individually interchangeable with the C versions.
; While C takes arrays of FFTComplex, SSE/3DNow leave intermediate results
; in blocks as conventient to the vector size.
; i.e. {4x real, 4x imaginary, 4x real, ...} (or 2x respectively)

%include "x86inc.asm"

SECTION_RODATA

%define M_SQRT1_2 0.70710678118654752440
ps_root2: times 4 dd M_SQRT1_2
ps_root2mppm: dd -M_SQRT1_2, M_SQRT1_2, M_SQRT1_2, -M_SQRT1_2
ps_m1p1: dd 1<<31, 0

%assign i 16
%rep 13
cextern ff_cos_ %+ i
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

section .text align=16

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
    pxor     %3, [ps_m1p1 GLOBAL] ; {t8,t7}
    mova     %6, %1
    pswapd   %3, %3
    pfadd    %1, %5 ; {r0,i0}
    pfsub    %6, %5 ; {r2,i2}
    mova     %4, %2
    pfadd    %2, %3 ; {r1,i1}
    pfsub    %4, %3 ; {r3,i3}
    SWAP     %3, %6
%endmacro

; in:  %1={r0,i0,r1,i1} %2={r2,i2,r3,i3}
; out: %1={r0,r1,r2,r3} %2={i0,i1,i2,i3}
%macro T4_SSE 3
    mova     %3, %1
    shufps   %1, %2, 0x64 ; {r0,i0,r3,i2}
    shufps   %3, %2, 0xce ; {r1,i1,r2,i3}
    mova     %2, %1
    addps    %1, %3       ; {t1,t2,t6,t5}
    subps    %2, %3       ; {t3,t4,t8,t7}
    mova     %3, %1
    shufps   %1, %2, 0x44 ; {t1,t2,t3,t4}
    shufps   %3, %2, 0xbe ; {t6,t5,t7,t8}
    mova     %2, %1
    addps    %1, %3       ; {r0,i0,r1,i1}
    subps    %2, %3       ; {r2,i2,r3,i3}
    mova     %3, %1
    shufps   %1, %2, 0x88 ; {r0,r1,r2,r3}
    shufps   %3, %2, 0xdd ; {i0,i1,i2,i3}
    SWAP     %2, %3
%endmacro

%macro T8_SSE 6 ; r0,i0,r1,i1,t0,t1
    mova     %5, %3
    shufps   %3, %4, 0x44 ; {r4,i4,r6,i6}
    shufps   %5, %4, 0xee ; {r5,i5,r7,i7}
    mova     %6, %3
    subps    %3, %5       ; {r5,i5,r7,i7}
    addps    %6, %5       ; {t1,t2,t3,t4}
    mova     %5, %3
    shufps   %5, %5, 0xb1 ; {i5,r5,i7,r7}
    mulps    %3, [ps_root2mppm GLOBAL] ; {-r5,i5,r7,-i7}
    mulps    %5, [ps_root2 GLOBAL]
    addps    %3, %5       ; {t8,t7,ta,t9}
    mova     %5, %6
    shufps   %6, %3, 0x36 ; {t3,t2,t9,t8}
    shufps   %5, %3, 0x9c ; {t1,t4,t7,ta}
    mova     %3, %6
    addps    %6, %5       ; {t1,t2,t9,ta}
    subps    %3, %5       ; {t6,t5,tc,tb}
    mova     %5, %6
    shufps   %6, %3, 0xd8 ; {t1,t9,t5,tb}
    shufps   %5, %3, 0x8d ; {t2,ta,t6,tc}
    mova     %3, %1
    mova     %4, %2
    addps    %1, %6       ; {r0,r1,r2,r3}
    addps    %2, %5       ; {i0,i1,i2,i3}
    subps    %3, %6       ; {r4,r5,r6,r7}
    subps    %4, %5       ; {i4,i5,i6,i7}
%endmacro

; scheduled for cpu-bound sizes
%macro PASS_SMALL 3 ; (to load m4-m7), wre, wim
IF%1 mova    m4, Z(4)
IF%1 mova    m5, Z(5)
    mova     m0, %2 ; wre
    mova     m2, m4
    mova     m1, %3 ; wim
    mova     m3, m5
    mulps    m2, m0 ; r2*wre
IF%1 mova    m6, Z(6)
    mulps    m3, m1 ; i2*wim
IF%1 mova    m7, Z(7)
    mulps    m4, m1 ; r2*wim
    mulps    m5, m0 ; i2*wre
    addps    m2, m3 ; r2*wre + i2*wim
    mova     m3, m1
    mulps    m1, m6 ; r3*wim
    subps    m5, m4 ; i2*wre - r2*wim
    mova     m4, m0
    mulps    m3, m7 ; i3*wim
    mulps    m4, m6 ; r3*wre
    mulps    m0, m7 ; i3*wre
    subps    m4, m3 ; r3*wre - i3*wim
    mova     m3, Z(0)
    addps    m0, m1 ; i3*wre + r3*wim
    mova     m1, m4
    addps    m4, m2 ; t5
    subps    m1, m2 ; t3
    subps    m3, m4 ; r2
    addps    m4, Z(0) ; r0
    mova     m6, Z(2)
    mova   Z(4), m3
    mova   Z(0), m4
    mova     m3, m5
    subps    m5, m0 ; t4
    mova     m4, m6
    subps    m6, m5 ; r3
    addps    m5, m4 ; r1
    mova   Z(6), m6
    mova   Z(2), m5
    mova     m2, Z(3)
    addps    m3, m0 ; t6
    subps    m2, m1 ; i3
    mova     m7, Z(1)
    addps    m1, Z(3) ; i1
    mova   Z(7), m2
    mova   Z(3), m1
    mova     m4, m7
    subps    m7, m3 ; i2
    addps    m3, m4 ; i0
    mova   Z(5), m7
    mova   Z(1), m3
%endmacro

; scheduled to avoid store->load aliasing
%macro PASS_BIG 1 ; (!interleave)
    mova     m4, Z(4) ; r2
    mova     m5, Z(5) ; i2
    mova     m2, m4
    mova     m0, [wq] ; wre
    mova     m3, m5
    mova     m1, [wq+o1q] ; wim
    mulps    m2, m0 ; r2*wre
    mova     m6, Z(6) ; r3
    mulps    m3, m1 ; i2*wim
    mova     m7, Z(7) ; i3
    mulps    m4, m1 ; r2*wim
    mulps    m5, m0 ; i2*wre
    addps    m2, m3 ; r2*wre + i2*wim
    mova     m3, m1
    mulps    m1, m6 ; r3*wim
    subps    m5, m4 ; i2*wre - r2*wim
    mova     m4, m0
    mulps    m3, m7 ; i3*wim
    mulps    m4, m6 ; r3*wre
    mulps    m0, m7 ; i3*wre
    subps    m4, m3 ; r3*wre - i3*wim
    mova     m3, Z(0)
    addps    m0, m1 ; i3*wre + r3*wim
    mova     m1, m4
    addps    m4, m2 ; t5
    subps    m1, m2 ; t3
    subps    m3, m4 ; r2
    addps    m4, Z(0) ; r0
    mova     m6, Z(2)
    mova   Z(4), m3
    mova   Z(0), m4
    mova     m3, m5
    subps    m5, m0 ; t4
    mova     m4, m6
    subps    m6, m5 ; r3
    addps    m5, m4 ; r1
IF%1 mova  Z(6), m6
IF%1 mova  Z(2), m5
    mova     m2, Z(3)
    addps    m3, m0 ; t6
    subps    m2, m1 ; i3
    mova     m7, Z(1)
    addps    m1, Z(3) ; i1
IF%1 mova  Z(7), m2
IF%1 mova  Z(3), m1
    mova     m4, m7
    subps    m7, m3 ; i2
    addps    m3, m4 ; i0
IF%1 mova  Z(5), m7
IF%1 mova  Z(1), m3
%if %1==0
    mova     m4, m5 ; r1
    mova     m0, m6 ; r3
    unpcklps m5, m1
    unpckhps m4, m1
    unpcklps m6, m2
    unpckhps m0, m2
    mova     m1, Z(0)
    mova     m2, Z(4)
    mova   Z(2), m5
    mova   Z(3), m4
    mova   Z(6), m6
    mova   Z(7), m0
    mova     m5, m1 ; r0
    mova     m4, m2 ; r2
    unpcklps m1, m3
    unpckhps m5, m3
    unpcklps m2, m7
    unpckhps m4, m7
    mova   Z(0), m1
    mova   Z(1), m5
    mova   Z(4), m2
    mova   Z(5), m4
%endif
%endmacro

%macro PUNPCK 3
    mova      %3, %1
    punpckldq %1, %2
    punpckhdq %3, %2
%endmacro

INIT_XMM
%define mova movaps

%define Z(x) [r0+mmsize*x]

align 16
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
    mova     m6, Z(6)
    mova     m7, Z(7)
    T4_SSE   m6, m7, m0
    PASS_SMALL 0, [ff_cos_16 GLOBAL], [ff_cos_16+16 GLOBAL]
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
    T2_3DN   m4, m5, Z(4), Z(5)
    T2_3DN   m6, m7, Z(6), Z(7)
    pswapd   m0, m5
    pswapd   m2, m7
    pxor     m0, [ps_m1p1 GLOBAL]
    pxor     m2, [ps_m1p1 GLOBAL]
    pfsub    m5, m0
    pfadd    m7, m2
    pfmul    m5, [ps_root2 GLOBAL]
    pfmul    m7, [ps_root2 GLOBAL]
    T4_3DN   m1, m3, m5, m7, m0, m2
    mova   Z(5), m5
    mova   Z(7), m7
    mova     m0, Z(0)
    mova     m2, Z(2)
    T4_3DN   m0, m2, m4, m6, m5, m7
    PUNPCK   m0, m1, m5
    PUNPCK   m2, m3, m7
    mova   Z(0), m0
    mova   Z(1), m5
    mova   Z(2), m2
    mova   Z(3), m7
    PUNPCK   m4, Z(5), m5
    PUNPCK   m6, Z(7), m7
    mova   Z(4), m4
    mova   Z(5), m5
    mova   Z(6), m6
    mova   Z(7), m7
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


%define Z(x) [zq + o1q*(x&6)*((x/6)^1) + o3q*(x/6) + mmsize*(x&1)]

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

INIT_XMM
%define mova movaps
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

%macro DECL_FFT 2-3 ; nbits, cpu, suffix
%xdefine list_of_fft fft4%2 SECTION_REL, fft8%2 SECTION_REL
%if %1==5
%xdefine list_of_fft list_of_fft, fft16%2 SECTION_REL
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
    lea r1, [ff_cos_ %+ n GLOBAL]
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
    lea r2, [dispatch_tab%3%2 GLOBAL]
    mov r2, [r2 + (nbitsq-2)*gprsize]
%ifdef PIC
    lea r3, [$$ GLOBAL]
    add r2, r3
%endif
    call r2
    RET
%endmacro ; DECL_FFT

DECL_FFT 5, _sse
DECL_FFT 5, _sse, _interleave
DECL_FFT 4, _3dn
DECL_FFT 4, _3dn, _interleave
DECL_FFT 4, _3dn2
DECL_FFT 4, _3dn2, _interleave

