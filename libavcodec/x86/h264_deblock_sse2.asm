;*****************************************************************************
;* MMX/SSE2-optimized H.264 deblocking code
;*****************************************************************************
;* Copyright (C) 2005-2008 x264 project
;*
;* Authors: Loren Merritt <lorenm@u.washington.edu>
;*
;* This program is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License as published by
;* the Free Software Foundation; either version 2 of the License, or
;* (at your option) any later version.
;*
;* This program is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;* GNU General Public License for more details.
;*
;* You should have received a copy of the GNU General Public License
;* along with this program; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
;*****************************************************************************

%include "x86inc.asm"

SECTION_RODATA
pb_00: times 16 db 0x00
pb_01: times 16 db 0x01
pb_03: times 16 db 0x03
pb_a1: times 16 db 0xa1

SECTION .text

; expands to [base],...,[base+7*stride]
%define PASS8ROWS(base, base3, stride, stride3) \
    [base], [base+stride], [base+stride*2], [base3], \
    [base3+stride], [base3+stride*2], [base3+stride3], [base3+stride*4]

; in: 8 rows of 4 bytes in %1..%8
; out: 4 rows of 8 bytes in m0..m3
%macro TRANSPOSE4x8_LOAD 8
    movd       m0, %1
    movd       m2, %2
    movd       m1, %3
    movd       m3, %4
    punpcklbw  m0, m2
    punpcklbw  m1, m3
    movq       m2, m0
    punpcklwd  m0, m1
    punpckhwd  m2, m1

    movd       m4, %5
    movd       m6, %6
    movd       m5, %7
    movd       m7, %8
    punpcklbw  m4, m6
    punpcklbw  m5, m7
    movq       m6, m4
    punpcklwd  m4, m5
    punpckhwd  m6, m5

    movq       m1, m0
    movq       m3, m2
    punpckldq  m0, m4
    punpckhdq  m1, m4
    punpckldq  m2, m6
    punpckhdq  m3, m6
%endmacro

; in: 4 rows of 8 bytes in m0..m3
; out: 8 rows of 4 bytes in %1..%8
%macro TRANSPOSE8x4_STORE 8
    movq       m4, m0
    movq       m5, m1
    movq       m6, m2
    punpckhdq  m4, m4
    punpckhdq  m5, m5
    punpckhdq  m6, m6

    punpcklbw  m0, m1
    punpcklbw  m2, m3
    movq       m1, m0
    punpcklwd  m0, m2
    punpckhwd  m1, m2
    movd       %1, m0
    punpckhdq  m0, m0
    movd       %2, m0
    movd       %3, m1
    punpckhdq  m1, m1
    movd       %4, m1

    punpckhdq  m3, m3
    punpcklbw  m4, m5
    punpcklbw  m6, m3
    movq       m5, m4
    punpcklwd  m4, m6
    punpckhwd  m5, m6
    movd       %5, m4
    punpckhdq  m4, m4
    movd       %6, m4
    movd       %7, m5
    punpckhdq  m5, m5
    movd       %8, m5
%endmacro

%macro SBUTTERFLY 4
    movq       %4, %2
    punpckl%1  %2, %3
    punpckh%1  %4, %3
%endmacro

; in: 8 rows of 8 (only the middle 6 pels are used) in %1..%8
; out: 6 rows of 8 in [%9+0*16] .. [%9+5*16]
%macro TRANSPOSE6x8_MEM 9
    movq  m0, %1
    movq  m1, %2
    movq  m2, %3
    movq  m3, %4
    movq  m4, %5
    movq  m5, %6
    movq  m6, %7
    SBUTTERFLY bw, m0, m1, m7
    SBUTTERFLY bw, m2, m3, m1
    SBUTTERFLY bw, m4, m5, m3
    movq  [%9+0x10], m1
    SBUTTERFLY bw, m6, %8, m5
    SBUTTERFLY wd, m0, m2, m1
    SBUTTERFLY wd, m4, m6, m2
    punpckhdq m0, m4
    movq  [%9+0x00], m0
    SBUTTERFLY wd, m7, [%9+0x10], m6
    SBUTTERFLY wd, m3, m5, m4
    SBUTTERFLY dq, m7, m3, m0
    SBUTTERFLY dq, m1, m2, m5
    punpckldq m6, m4
    movq  [%9+0x10], m1
    movq  [%9+0x20], m5
    movq  [%9+0x30], m7
    movq  [%9+0x40], m0
    movq  [%9+0x50], m6
%endmacro

; in: 8 rows of 8 in %1..%8
; out: 8 rows of 8 in %9..%16
%macro TRANSPOSE8x8_MEM 16
    movq  m0, %1
    movq  m1, %2
    movq  m2, %3
    movq  m3, %4
    movq  m4, %5
    movq  m5, %6
    movq  m6, %7
    SBUTTERFLY bw, m0, m1, m7
    SBUTTERFLY bw, m2, m3, m1
    SBUTTERFLY bw, m4, m5, m3
    SBUTTERFLY bw, m6, %8, m5
    movq  %9,  m3
    SBUTTERFLY wd, m0, m2, m3
    SBUTTERFLY wd, m4, m6, m2
    SBUTTERFLY wd, m7, m1, m6
    movq  %11, m2
    movq  m2,  %9
    SBUTTERFLY wd, m2, m5, m1
    SBUTTERFLY dq, m0, m4, m5
    SBUTTERFLY dq, m7, m2, m4
    movq  %9,  m0
    movq  %10, m5
    movq  %13, m7
    movq  %14, m4
    SBUTTERFLY dq, m3, %11, m0
    SBUTTERFLY dq, m6, m1, m5
    movq  %11, m3
    movq  %12, m0
    movq  %15, m6
    movq  %16, m5
%endmacro

; out: %4 = |%1-%2|>%3
; clobbers: %5
%macro DIFF_GT 5
    mova    %5, %2
    mova    %4, %1
    psubusb %5, %1
    psubusb %4, %2
    por     %4, %5
    psubusb %4, %3
%endmacro

; out: %4 = |%1-%2|>%3
; clobbers: %5
%macro DIFF_GT2 5
    mova    %5, %2
    mova    %4, %1
    psubusb %5, %1
    psubusb %4, %2
    psubusb %5, %3
    psubusb %4, %3
    pcmpeqb %4, %5
%endmacro

%macro SPLATW 1
%ifidn m0, xmm0
    pshuflw  %1, %1, 0
    punpcklqdq %1, %1
%else
    pshufw   %1, %1, 0
%endif
%endmacro

; in: m0=p1 m1=p0 m2=q0 m3=q1 %1=alpha-1 %2=beta-1
; out: m5=beta-1, m7=mask, %3=alpha-1
; clobbers: m4,m6
%macro LOAD_MASK 2-3
    movd     m4, %1
    movd     m5, %2
    SPLATW   m4
    SPLATW   m5
    packuswb m4, m4  ; 16x alpha-1
    packuswb m5, m5  ; 16x beta-1
%if %0>2
    mova     %3, m4
%endif
    DIFF_GT  m1, m2, m4, m7, m6 ; |p0-q0| > alpha-1
    DIFF_GT  m0, m1, m5, m4, m6 ; |p1-p0| > beta-1
    por      m7, m4
    DIFF_GT  m3, m2, m5, m4, m6 ; |q1-q0| > beta-1
    por      m7, m4
    pxor     m6, m6
    pcmpeqb  m7, m6
%endmacro

; in: m0=p1 m1=p0 m2=q0 m3=q1 m7=(tc&mask)
; out: m1=p0' m2=q0'
; clobbers: m0,3-6
%macro DEBLOCK_P0_Q0 0
    mova    m5, m1
    pxor    m5, m2           ; p0^q0
    pand    m5, [pb_01 GLOBAL] ; (p0^q0)&1
    pcmpeqb m4, m4
    pxor    m3, m4
    pavgb   m3, m0           ; (p1 - q1 + 256)>>1
    pavgb   m3, [pb_03 GLOBAL] ; (((p1 - q1 + 256)>>1)+4)>>1 = 64+2+(p1-q1)>>2
    pxor    m4, m1
    pavgb   m4, m2           ; (q0 - p0 + 256)>>1
    pavgb   m3, m5
    paddusb m3, m4           ; d+128+33
    mova    m6, [pb_a1 GLOBAL]
    psubusb m6, m3
    psubusb m3, [pb_a1 GLOBAL]
    pminub  m6, m7
    pminub  m3, m7
    psubusb m1, m6
    psubusb m2, m3
    paddusb m1, m3
    paddusb m2, m6
%endmacro

; in: m1=p0 m2=q0
;     %1=p1 %2=q2 %3=[q2] %4=[q1] %5=tc0 %6=tmp
; out: [q1] = clip( (q2+((p0+q0+1)>>1))>>1, q1-tc0, q1+tc0 )
; clobbers: q2, tmp, tc0
%macro LUMA_Q1 6
    mova    %6, m1
    pavgb   %6, m2
    pavgb   %2, %6             ; avg(p2,avg(p0,q0))
    pxor    %6, %3
    pand    %6, [pb_01 GLOBAL] ; (p2^avg(p0,q0))&1
    psubusb %2, %6             ; (p2+((p0+q0+1)>>1))>>1
    mova    %6, %1
    psubusb %6, %5
    paddusb %5, %1
    pmaxub  %2, %6
    pminub  %2, %5
    mova    %4, %2
%endmacro

%ifdef ARCH_X86_64
;-----------------------------------------------------------------------------
; void x264_deblock_v_luma_sse2( uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0 )
;-----------------------------------------------------------------------------
INIT_XMM
cglobal x264_deblock_v_luma_sse2
    movd    m8, [r4] ; tc0
    lea     r4, [r1*3]
    dec     r2d        ; alpha-1
    neg     r4
    dec     r3d        ; beta-1
    add     r4, r0     ; pix-3*stride

    mova    m0, [r4+r1]   ; p1
    mova    m1, [r4+2*r1] ; p0
    mova    m2, [r0]      ; q0
    mova    m3, [r0+r1]   ; q1
    LOAD_MASK r2d, r3d

    punpcklbw m8, m8
    punpcklbw m8, m8 ; tc = 4x tc0[3], 4x tc0[2], 4x tc0[1], 4x tc0[0]
    pcmpeqb m9, m9
    pcmpeqb m9, m8
    pandn   m9, m7
    pand    m8, m9

    movdqa  m3, [r4] ; p2
    DIFF_GT2 m1, m3, m5, m6, m7 ; |p2-p0| > beta-1
    pand    m6, m9
    mova    m7, m8
    psubb   m7, m6
    pand    m6, m8
    LUMA_Q1 m0, m3, [r4], [r4+r1], m6, m4

    movdqa  m4, [r0+2*r1] ; q2
    DIFF_GT2 m2, m4, m5, m6, m3 ; |q2-q0| > beta-1
    pand    m6, m9
    pand    m8, m6
    psubb   m7, m6
    mova    m3, [r0+r1]
    LUMA_Q1 m3, m4, [r0+2*r1], [r0+r1], m8, m6

    DEBLOCK_P0_Q0
    mova    [r4+2*r1], m1
    mova    [r0], m2
    ret

;-----------------------------------------------------------------------------
; void x264_deblock_h_luma_sse2( uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0 )
;-----------------------------------------------------------------------------
INIT_MMX
cglobal x264_deblock_h_luma_sse2
    movsxd r10, esi
    lea    r11, [r10+r10*2]
    lea    rax, [r0-4]
    lea    r9,  [r0-4+r11]
    sub    rsp, 0x68
    %define pix_tmp rsp

    ; transpose 6x16 -> tmp space
    TRANSPOSE6x8_MEM  PASS8ROWS(rax, r9, r10, r11), pix_tmp
    lea    rax, [rax+r10*8]
    lea    r9,  [r9 +r10*8]
    TRANSPOSE6x8_MEM  PASS8ROWS(rax, r9, r10, r11), pix_tmp+8

    ; vertical filter
    ; alpha, beta, tc0 are still in r2d, r3d, r4
    ; don't backup rax, r9, r10, r11 because x264_deblock_v_luma_sse2 doesn't use them
    lea    r0, [pix_tmp+0x30]
    mov    esi, 0x10
    call   x264_deblock_v_luma_sse2

    ; transpose 16x4 -> original space  (only the middle 4 rows were changed by the filter)
    add    rax, 2
    add    r9,  2
    movq   m0, [pix_tmp+0x18]
    movq   m1, [pix_tmp+0x28]
    movq   m2, [pix_tmp+0x38]
    movq   m3, [pix_tmp+0x48]
    TRANSPOSE8x4_STORE  PASS8ROWS(rax, r9, r10, r11)

    shl    r10, 3
    sub    rax, r10
    sub    r9,  r10
    shr    r10, 3
    movq   m0, [pix_tmp+0x10]
    movq   m1, [pix_tmp+0x20]
    movq   m2, [pix_tmp+0x30]
    movq   m3, [pix_tmp+0x40]
    TRANSPOSE8x4_STORE  PASS8ROWS(rax, r9, r10, r11)

    add    rsp, 0x68
    ret

%else

%macro DEBLOCK_LUMA 3
;-----------------------------------------------------------------------------
; void x264_deblock_v8_luma_mmxext( uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0 )
;-----------------------------------------------------------------------------
cglobal x264_deblock_%2_luma_%1, 5,5
    lea     r4, [r1*3]
    dec     r2     ; alpha-1
    neg     r4
    dec     r3     ; beta-1
    add     r4, r0 ; pix-3*stride
    %assign pad 2*%3+12-(stack_offset&15)
    SUB     esp, pad

    mova    m0, [r4+r1]   ; p1
    mova    m1, [r4+2*r1] ; p0
    mova    m2, [r0]      ; q0
    mova    m3, [r0+r1]   ; q1
    LOAD_MASK r2, r3

    mov     r3, r4m
    movd    m4, [r3] ; tc0
    punpcklbw m4, m4
    punpcklbw m4, m4 ; tc = 4x tc0[3], 4x tc0[2], 4x tc0[1], 4x tc0[0]
    mova   [esp+%3], m4 ; tc
    pcmpeqb m3, m3
    pcmpgtb m4, m3
    pand    m4, m7
    mova   [esp], m4 ; mask

    mova    m3, [r4] ; p2
    DIFF_GT2 m1, m3, m5, m6, m7 ; |p2-p0| > beta-1
    pand    m6, m4
    pand    m4, [esp+%3] ; tc
    mova    m7, m4
    psubb   m7, m6
    pand    m6, m4
    LUMA_Q1 m0, m3, [r4], [r4+r1], m6, m4

    mova    m4, [r0+2*r1] ; q2
    DIFF_GT2 m2, m4, m5, m6, m3 ; |q2-q0| > beta-1
    mova    m5, [esp] ; mask
    pand    m6, m5
    mova    m5, [esp+%3] ; tc
    pand    m5, m6
    psubb   m7, m6
    mova    m3, [r0+r1]
    LUMA_Q1 m3, m4, [r0+2*r1], [r0+r1], m5, m6

    DEBLOCK_P0_Q0
    mova    [r4+2*r1], m1
    mova    [r0], m2
    ADD     esp, pad
    RET

;-----------------------------------------------------------------------------
; void x264_deblock_h_luma_mmxext( uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0 )
;-----------------------------------------------------------------------------
INIT_MMX
cglobal x264_deblock_h_luma_%1, 0,5
    mov    r0, r0m
    mov    r3, r1m
    lea    r4, [r3*3]
    sub    r0, 4
    lea    r1, [r0+r4]
    %assign pad 0x78-(stack_offset&15)
    SUB    esp, pad
%define pix_tmp esp+12

    ; transpose 6x16 -> tmp space
    TRANSPOSE6x8_MEM  PASS8ROWS(r0, r1, r3, r4), pix_tmp
    lea    r0, [r0+r3*8]
    lea    r1, [r1+r3*8]
    TRANSPOSE6x8_MEM  PASS8ROWS(r0, r1, r3, r4), pix_tmp+8

    ; vertical filter
    lea    r0, [pix_tmp+0x30]
    PUSH   dword r4m
    PUSH   dword r3m
    PUSH   dword r2m
    PUSH   dword 16
    PUSH   dword r0
    call   x264_deblock_%2_luma_%1
%ifidn %2, v8
    add    dword [esp   ], 8 ; pix_tmp+0x38
    add    dword [esp+16], 2 ; tc0+2
    call   x264_deblock_%2_luma_%1
%endif
    ADD    esp, 20

    ; transpose 16x4 -> original space  (only the middle 4 rows were changed by the filter)
    mov    r0, r0m
    sub    r0, 2
    lea    r1, [r0+r4]

    movq   m0, [pix_tmp+0x10]
    movq   m1, [pix_tmp+0x20]
    movq   m2, [pix_tmp+0x30]
    movq   m3, [pix_tmp+0x40]
    TRANSPOSE8x4_STORE  PASS8ROWS(r0, r1, r3, r4)

    lea    r0, [r0+r3*8]
    lea    r1, [r1+r3*8]
    movq   m0, [pix_tmp+0x18]
    movq   m1, [pix_tmp+0x28]
    movq   m2, [pix_tmp+0x38]
    movq   m3, [pix_tmp+0x48]
    TRANSPOSE8x4_STORE  PASS8ROWS(r0, r1, r3, r4)

    ADD    esp, pad
    RET
%endmacro ; DEBLOCK_LUMA

INIT_XMM
DEBLOCK_LUMA sse2, v, 16

%endif ; ARCH



%macro LUMA_INTRA_P012 4 ; p0..p3 in memory
    mova  t0, p2
    mova  t1, p0
    pavgb t0, p1
    pavgb t1, q0
    pavgb t0, t1 ; ((p2+p1+1)/2 + (p0+q0+1)/2 + 1)/2
    mova  t5, t1
    mova  t2, p2
    mova  t3, p0
    paddb t2, p1
    paddb t3, q0
    paddb t2, t3
    mova  t3, t2
    mova  t4, t2
    psrlw t2, 1
    pavgb t2, mpb_00
    pxor  t2, t0
    pand  t2, mpb_01
    psubb t0, t2 ; p1' = (p2+p1+p0+q0+2)/4;

    mova  t1, p2
    mova  t2, p2
    pavgb t1, q1
    psubb t2, q1
    paddb t3, t3
    psubb t3, t2 ; p2+2*p1+2*p0+2*q0+q1
    pand  t2, mpb_01
    psubb t1, t2
    pavgb t1, p1
    pavgb t1, t5 ; (((p2+q1)/2 + p1+1)/2 + (p0+q0+1)/2 + 1)/2
    psrlw t3, 2
    pavgb t3, mpb_00
    pxor  t3, t1
    pand  t3, mpb_01
    psubb t1, t3 ; p0'a = (p2+2*p1+2*p0+2*q0+q1+4)/8

    mova  t3, p0
    mova  t2, p0
    pxor  t3, q1
    pavgb t2, q1
    pand  t3, mpb_01
    psubb t2, t3
    pavgb t2, p1 ; p0'b = (2*p1+p0+q0+2)/4

    pxor  t1, t2
    pxor  t2, p0
    pand  t1, mask1p
    pand  t2, mask0
    pxor  t1, t2
    pxor  t1, p0
    mova  %1, t1 ; store p0

    mova  t1, %4 ; p3
    mova  t2, t1
    pavgb t1, p2
    paddb t2, p2
    pavgb t1, t0 ; (p3+p2+1)/2 + (p2+p1+p0+q0+2)/4
    paddb t2, t2
    paddb t2, t4 ; 2*p3+3*p2+p1+p0+q0
    psrlw t2, 2
    pavgb t2, mpb_00
    pxor  t2, t1
    pand  t2, mpb_01
    psubb t1, t2 ; p2' = (2*p3+3*p2+p1+p0+q0+4)/8

    pxor  t0, p1
    pxor  t1, p2
    pand  t0, mask1p
    pand  t1, mask1p
    pxor  t0, p1
    pxor  t1, p2
    mova  %2, t0 ; store p1
    mova  %3, t1 ; store p2
%endmacro

%macro LUMA_INTRA_SWAP_PQ 0
    %define q1 m0
    %define q0 m1
    %define p0 m2
    %define p1 m3
    %define p2 q2
    %define mask1p mask1q
%endmacro

%macro DEBLOCK_LUMA_INTRA 2
    %define p1 m0
    %define p0 m1
    %define q0 m2
    %define q1 m3
    %define t0 m4
    %define t1 m5
    %define t2 m6
    %define t3 m7
%ifdef ARCH_X86_64
    %define p2 m8
    %define q2 m9
    %define t4 m10
    %define t5 m11
    %define mask0 m12
    %define mask1p m13
    %define mask1q [rsp-24]
    %define mpb_00 m14
    %define mpb_01 m15
%else
    %define spill(x) [esp+16*x+((stack_offset+4)&15)]
    %define p2 [r4+r1]
    %define q2 [r0+2*r1]
    %define t4 spill(0)
    %define t5 spill(1)
    %define mask0 spill(2)
    %define mask1p spill(3)
    %define mask1q spill(4)
    %define mpb_00 [pb_00 GLOBAL]
    %define mpb_01 [pb_01 GLOBAL]
%endif

;-----------------------------------------------------------------------------
; void x264_deblock_v_luma_intra_sse2( uint8_t *pix, int stride, int alpha, int beta )
;-----------------------------------------------------------------------------
cglobal x264_deblock_%2_luma_intra_%1, 4,6
%ifndef ARCH_X86_64
    sub     esp, 0x60
%endif
    lea     r4, [r1*4]
    lea     r5, [r1*3] ; 3*stride
    dec     r2d        ; alpha-1
    jl .end
    neg     r4
    dec     r3d        ; beta-1
    jl .end
    add     r4, r0     ; pix-4*stride
    mova    p1, [r4+2*r1]
    mova    p0, [r4+r5]
    mova    q0, [r0]
    mova    q1, [r0+r1]
%ifdef ARCH_X86_64
    pxor    mpb_00, mpb_00
    mova    mpb_01, [pb_01 GLOBAL]
    LOAD_MASK r2d, r3d, t5 ; m5=beta-1, t5=alpha-1, m7=mask0
    SWAP    7, 12 ; m12=mask0
    pavgb   t5, mpb_00
    pavgb   t5, mpb_01 ; alpha/4+1
    movdqa  p2, [r4+r1]
    movdqa  q2, [r0+2*r1]
    DIFF_GT2 p0, q0, t5, t0, t3 ; t0 = |p0-q0| > alpha/4+1
    DIFF_GT2 p0, p2, m5, t2, t5 ; mask1 = |p2-p0| > beta-1
    DIFF_GT2 q0, q2, m5, t4, t5 ; t4 = |q2-q0| > beta-1
    pand    t0, mask0
    pand    t4, t0
    pand    t2, t0
    mova    mask1q, t4
    mova    mask1p, t2
%else
    LOAD_MASK r2d, r3d, t5 ; m5=beta-1, t5=alpha-1, m7=mask0
    mova    m4, t5
    mova    mask0, m7
    pavgb   m4, [pb_00 GLOBAL]
    pavgb   m4, [pb_01 GLOBAL] ; alpha/4+1
    DIFF_GT2 p0, q0, m4, m6, m7 ; m6 = |p0-q0| > alpha/4+1
    pand    m6, mask0
    DIFF_GT2 p0, p2, m5, m4, m7 ; m4 = |p2-p0| > beta-1
    pand    m4, m6
    mova    mask1p, m4
    DIFF_GT2 q0, q2, m5, m4, m7 ; m4 = |q2-q0| > beta-1
    pand    m4, m6
    mova    mask1q, m4
%endif
    LUMA_INTRA_P012 [r4+r5], [r4+2*r1], [r4+r1], [r4]
    LUMA_INTRA_SWAP_PQ
    LUMA_INTRA_P012 [r0], [r0+r1], [r0+2*r1], [r0+r5]
.end:
%ifndef ARCH_X86_64
    add     esp, 0x60
%endif
    RET

INIT_MMX
%ifdef ARCH_X86_64
;-----------------------------------------------------------------------------
; void x264_deblock_h_luma_intra_sse2( uint8_t *pix, int stride, int alpha, int beta )
;-----------------------------------------------------------------------------
cglobal x264_deblock_h_luma_intra_%1
    movsxd r10, r1d
    lea    r11, [r10*3]
    lea    rax, [r0-4]
    lea    r9,  [r0-4+r11]
    sub    rsp, 0x88
    %define pix_tmp rsp

    ; transpose 8x16 -> tmp space
    TRANSPOSE8x8_MEM  PASS8ROWS(rax, r9, r10, r11), PASS8ROWS(pix_tmp, pix_tmp+0x30, 0x10, 0x30)
    lea    rax, [rax+r10*8]
    lea    r9,  [r9+r10*8]
    TRANSPOSE8x8_MEM  PASS8ROWS(rax, r9, r10, r11), PASS8ROWS(pix_tmp+8, pix_tmp+0x38, 0x10, 0x30)

    lea    r0,  [pix_tmp+0x40]
    mov    r1,  0x10
    call   x264_deblock_v_luma_intra_%1

    ; transpose 16x6 -> original space (but we can't write only 6 pixels, so really 16x8)
    lea    r9, [rax+r11]
    TRANSPOSE8x8_MEM  PASS8ROWS(pix_tmp+8, pix_tmp+0x38, 0x10, 0x30), PASS8ROWS(rax, r9, r10, r11)
    shl    r10, 3
    sub    rax, r10
    sub    r9,  r10
    shr    r10, 3
    TRANSPOSE8x8_MEM  PASS8ROWS(pix_tmp, pix_tmp+0x30, 0x10, 0x30), PASS8ROWS(rax, r9, r10, r11)
    add    rsp, 0x88
    ret
%else
cglobal x264_deblock_h_luma_intra_%1, 2,4
    lea    r3,  [r1*3]
    sub    r0,  4
    lea    r2,  [r0+r3]
%assign pad 0x8c-(stack_offset&15)
    SUB    rsp, pad
    %define pix_tmp rsp

    ; transpose 8x16 -> tmp space
    TRANSPOSE8x8_MEM  PASS8ROWS(r0, r2, r1, r3), PASS8ROWS(pix_tmp, pix_tmp+0x30, 0x10, 0x30)
    lea    r0,  [r0+r1*8]
    lea    r2,  [r2+r1*8]
    TRANSPOSE8x8_MEM  PASS8ROWS(r0, r2, r1, r3), PASS8ROWS(pix_tmp+8, pix_tmp+0x38, 0x10, 0x30)

    lea    r0,  [pix_tmp+0x40]
    PUSH   dword r3m
    PUSH   dword r2m
    PUSH   dword 16
    PUSH   r0
    call   x264_deblock_%2_luma_intra_%1
%ifidn %2, v8
    add    dword [rsp], 8 ; pix_tmp+8
    call   x264_deblock_%2_luma_intra_%1
%endif
    ADD    esp, 16

    mov    r1,  r1m
    mov    r0,  r0m
    lea    r3,  [r1*3]
    sub    r0,  4
    lea    r2,  [r0+r3]
    ; transpose 16x6 -> original space (but we can't write only 6 pixels, so really 16x8)
    TRANSPOSE8x8_MEM  PASS8ROWS(pix_tmp, pix_tmp+0x30, 0x10, 0x30), PASS8ROWS(r0, r2, r1, r3)
    lea    r0,  [r0+r1*8]
    lea    r2,  [r2+r1*8]
    TRANSPOSE8x8_MEM  PASS8ROWS(pix_tmp+8, pix_tmp+0x38, 0x10, 0x30), PASS8ROWS(r0, r2, r1, r3)
    ADD    rsp, pad
    RET
%endif ; ARCH_X86_64
%endmacro ; DEBLOCK_LUMA_INTRA

INIT_XMM
DEBLOCK_LUMA_INTRA sse2, v
%ifndef ARCH_X86_64
INIT_MMX
DEBLOCK_LUMA_INTRA mmxext, v8
%endif
