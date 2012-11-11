;*****************************************************************************
;* MMX/SSE2/AVX-optimized 10-bit H.264 qpel code
;*****************************************************************************
;* Copyright (C) 2011 x264 project
;*
;* Authors: Daniel Kang <daniel.d.kang@gmail.com>
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

SECTION_RODATA 32

cextern pw_16
cextern pw_1
cextern pb_0

pw_pixel_max: times 8 dw ((1 << 10)-1)

pad10: times 8 dw 10*1023
pad20: times 8 dw 20*1023
pad30: times 8 dw 30*1023
depad: times 4 dd 32*20*1023 + 512
depad2: times 8 dw 20*1023 + 16*1022 + 16
unpad: times 8 dw 16*1022/32 ; needs to be mod 16

tap1: times 4 dw  1, -5
tap2: times 4 dw 20, 20
tap3: times 4 dw -5,  1
pd_0f: times 4 dd 0xffff

SECTION .text


%macro AVG_MOV 2
    pavgw %2, %1
    mova  %1, %2
%endmacro

%macro ADDW 3
%if mmsize == 8
    paddw %1, %2
%else
    movu  %3, %2
    paddw %1, %3
%endif
%endmacro

%macro FILT_H 4
    paddw  %1, %4
    psubw  %1, %2  ; a-b
    psraw  %1, 2   ; (a-b)/4
    psubw  %1, %2  ; (a-b)/4-b
    paddw  %1, %3  ; (a-b)/4-b+c
    psraw  %1, 2   ; ((a-b)/4-b+c)/4
    paddw  %1, %3  ; ((a-b)/4-b+c)/4+c = (a-5*b+20*c)/16
%endmacro

%macro PRELOAD_V 0
    lea      r3, [r2*3]
    sub      r1, r3
    movu     m0, [r1+r2]
    movu     m1, [r1+r2*2]
    add      r1, r3
    movu     m2, [r1]
    movu     m3, [r1+r2]
    movu     m4, [r1+r2*2]
    add      r1, r3
%endmacro

%macro FILT_V 8
    movu     %6, [r1]
    paddw    %1, %6
    mova     %7, %2
    paddw    %7, %5
    mova     %8, %3
    paddw    %8, %4
    FILT_H   %1, %7, %8, [pw_16]
    psraw    %1, 1
    CLIPW    %1, [pb_0], [pw_pixel_max]
%endmacro

%macro MC 1
%define OP_MOV mova
INIT_MMX mmxext
%1 put, 4
INIT_XMM sse2
%1 put, 8

%define OP_MOV AVG_MOV
INIT_MMX mmxext
%1 avg, 4
INIT_XMM sse2
%1 avg, 8
%endmacro

%macro MCAxA_OP 7
%if ARCH_X86_32
cglobal %1_h264_qpel%4_%2_10, %5,%6,%7
    call stub_%1_h264_qpel%3_%2_10 %+ SUFFIX
    mov  r0, r0m
    mov  r1, r1m
    add  r0, %3*2
    add  r1, %3*2
    call stub_%1_h264_qpel%3_%2_10 %+ SUFFIX
    mov  r0, r0m
    mov  r1, r1m
    lea  r0, [r0+r2*%3]
    lea  r1, [r1+r2*%3]
    call stub_%1_h264_qpel%3_%2_10 %+ SUFFIX
    mov  r0, r0m
    mov  r1, r1m
    lea  r0, [r0+r2*%3+%3*2]
    lea  r1, [r1+r2*%3+%3*2]
    call stub_%1_h264_qpel%3_%2_10 %+ SUFFIX
    RET
%else ; ARCH_X86_64
cglobal %1_h264_qpel%4_%2_10, %5,%6 + 2,%7
    mov r%6, r0
%assign p1 %6+1
    mov r %+ p1, r1
    call stub_%1_h264_qpel%3_%2_10 %+ SUFFIX
    lea  r0, [r%6+%3*2]
    lea  r1, [r %+ p1+%3*2]
    call stub_%1_h264_qpel%3_%2_10 %+ SUFFIX
    lea  r0, [r%6+r2*%3]
    lea  r1, [r %+ p1+r2*%3]
    call stub_%1_h264_qpel%3_%2_10 %+ SUFFIX
    lea  r0, [r%6+r2*%3+%3*2]
    lea  r1, [r %+ p1+r2*%3+%3*2]
%if UNIX64 == 0 ; fall through to function
    call stub_%1_h264_qpel%3_%2_10 %+ SUFFIX
    RET
%endif
%endif
%endmacro

;cpu, put/avg, mc, 4/8, ...
%macro cglobal_mc 6
%assign i %3*2
%if ARCH_X86_32 || cpuflag(sse2)
MCAxA_OP %1, %2, %3, i, %4,%5,%6
%endif

cglobal %1_h264_qpel%3_%2_10, %4,%5,%6
%if UNIX64 == 0 ; no prologue or epilogue for UNIX64
    call stub_%1_h264_qpel%3_%2_10 %+ SUFFIX
    RET
%endif

stub_%1_h264_qpel%3_%2_10 %+ SUFFIX:
%endmacro

;-----------------------------------------------------------------------------
; void h264_qpel_mc00(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro COPY4 0
    movu          m0, [r1     ]
    OP_MOV [r0     ], m0
    movu          m0, [r1+r2  ]
    OP_MOV [r0+r2  ], m0
    movu          m0, [r1+r2*2]
    OP_MOV [r0+r2*2], m0
    movu          m0, [r1+r3  ]
    OP_MOV [r0+r3  ], m0
%endmacro

%macro MC00 1
INIT_MMX mmxext
cglobal_mc %1, mc00, 4, 3,4,0
    lea           r3, [r2*3]
    COPY4
    ret

INIT_XMM sse2
cglobal %1_h264_qpel8_mc00_10, 3,4
    lea  r3, [r2*3]
    COPY4
    lea  r0, [r0+r2*4]
    lea  r1, [r1+r2*4]
    COPY4
    RET

cglobal %1_h264_qpel16_mc00_10, 3,4
    mov r3d, 8
.loop:
    movu           m0, [r1      ]
    movu           m1, [r1   +16]
    OP_MOV [r0      ], m0
    OP_MOV [r0   +16], m1
    movu           m0, [r1+r2   ]
    movu           m1, [r1+r2+16]
    OP_MOV [r0+r2   ], m0
    OP_MOV [r0+r2+16], m1
    lea            r0, [r0+r2*2]
    lea            r1, [r1+r2*2]
    dec r3d
    jg .loop
    REP_RET
%endmacro

%define OP_MOV mova
MC00 put

%define OP_MOV AVG_MOV
MC00 avg

;-----------------------------------------------------------------------------
; void h264_qpel_mc20(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro MC_CACHE 1
%define OP_MOV mova
INIT_MMX mmxext
%1 put, 4
INIT_XMM sse2, cache64
%1 put, 8
INIT_XMM ssse3, cache64
%1 put, 8
INIT_XMM sse2
%1 put, 8

%define OP_MOV AVG_MOV
INIT_MMX mmxext
%1 avg, 4
INIT_XMM sse2, cache64
%1 avg, 8
INIT_XMM ssse3, cache64
%1 avg, 8
INIT_XMM sse2
%1 avg, 8
%endmacro

%macro MC20 2
cglobal_mc %1, mc20, %2, 3,4,9
    mov     r3d, %2
    mova     m1, [pw_pixel_max]
%if num_mmregs > 8
    mova     m8, [pw_16]
    %define p16 m8
%else
    %define p16 [pw_16]
%endif
.nextrow:
%if %0 == 4
    movu     m2, [r1-4]
    movu     m3, [r1-2]
    movu     m4, [r1+0]
    ADDW     m2, [r1+6], m5
    ADDW     m3, [r1+4], m5
    ADDW     m4, [r1+2], m5
%else ; movu is slow on these processors
%if mmsize==16
    movu     m2, [r1-4]
    movu     m0, [r1+6]
    mova     m6, m0
    psrldq   m0, 6

    paddw    m6, m2
    PALIGNR  m3, m0, m2, 2, m5
    PALIGNR  m7, m0, m2, 8, m5
    paddw    m3, m7
    PALIGNR  m4, m0, m2, 4, m5
    PALIGNR  m7, m0, m2, 6, m5
    paddw    m4, m7
    SWAP      2, 6
%else
    movu     m2, [r1-4]
    movu     m6, [r1+4]
    PALIGNR  m3, m6, m2, 2, m5
    paddw    m3, m6
    PALIGNR  m4, m6, m2, 4, m5
    PALIGNR  m7, m6, m2, 6, m5
    paddw    m4, m7
    paddw    m2, [r1+6]
%endif
%endif

    FILT_H   m2, m3, m4, p16
    psraw    m2, 1
    pxor     m0, m0
    CLIPW    m2, m0, m1
    OP_MOV [r0], m2
    add      r0, r2
    add      r1, r2
    dec     r3d
    jg .nextrow
    rep ret
%endmacro

MC_CACHE MC20

;-----------------------------------------------------------------------------
; void h264_qpel_mc30(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro MC30 2
cglobal_mc %1, mc30, %2, 3,5,9
    lea r4, [r1+2]
    jmp stub_%1_h264_qpel%2_mc10_10 %+ SUFFIX %+ .body
%endmacro

MC_CACHE MC30

;-----------------------------------------------------------------------------
; void h264_qpel_mc10(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro MC10 2
cglobal_mc %1, mc10, %2, 3,5,9
    mov      r4, r1
.body:
    mov     r3d, %2
    mova     m1, [pw_pixel_max]
%if num_mmregs > 8
    mova     m8, [pw_16]
    %define p16 m8
%else
    %define p16 [pw_16]
%endif
.nextrow:
%if %0 == 4
    movu     m2, [r1-4]
    movu     m3, [r1-2]
    movu     m4, [r1+0]
    ADDW     m2, [r1+6], m5
    ADDW     m3, [r1+4], m5
    ADDW     m4, [r1+2], m5
%else ; movu is slow on these processors
%if mmsize==16
    movu     m2, [r1-4]
    movu     m0, [r1+6]
    mova     m6, m0
    psrldq   m0, 6

    paddw    m6, m2
    PALIGNR  m3, m0, m2, 2, m5
    PALIGNR  m7, m0, m2, 8, m5
    paddw    m3, m7
    PALIGNR  m4, m0, m2, 4, m5
    PALIGNR  m7, m0, m2, 6, m5
    paddw    m4, m7
    SWAP      2, 6
%else
    movu     m2, [r1-4]
    movu     m6, [r1+4]
    PALIGNR  m3, m6, m2, 2, m5
    paddw    m3, m6
    PALIGNR  m4, m6, m2, 4, m5
    PALIGNR  m7, m6, m2, 6, m5
    paddw    m4, m7
    paddw    m2, [r1+6]
%endif
%endif

    FILT_H   m2, m3, m4, p16
    psraw    m2, 1
    pxor     m0, m0
    CLIPW    m2, m0, m1
    movu     m3, [r4]
    pavgw    m2, m3
    OP_MOV [r0], m2
    add      r0, r2
    add      r1, r2
    add      r4, r2
    dec     r3d
    jg .nextrow
    rep ret
%endmacro

MC_CACHE MC10

;-----------------------------------------------------------------------------
; void h264_qpel_mc02(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro V_FILT 10
v_filt%9_%10_10
    add    r4, r2
.no_addr4:
    FILT_V m0, m1, m2, m3, m4, m5, m6, m7
    add    r1, r2
    add    r0, r2
    ret
%endmacro

INIT_MMX mmxext
RESET_MM_PERMUTATION
%assign i 0
%rep 4
V_FILT m0, m1, m2, m3, m4, m5, m6, m7, 4, i
SWAP 0,1,2,3,4,5
%assign i i+1
%endrep

INIT_XMM sse2
RESET_MM_PERMUTATION
%assign i 0
%rep 6
V_FILT m0, m1, m2, m3, m4, m5, m6, m7, 8, i
SWAP 0,1,2,3,4,5
%assign i i+1
%endrep

%macro MC02 2
cglobal_mc %1, mc02, %2, 3,4,8
    PRELOAD_V

    sub      r0, r2
%assign j 0
%rep %2
    %assign i (j % 6)
    call v_filt%2_ %+ i %+ _10.no_addr4
    OP_MOV [r0], m0
    SWAP 0,1,2,3,4,5
    %assign j j+1
%endrep
    ret
%endmacro

MC MC02

;-----------------------------------------------------------------------------
; void h264_qpel_mc01(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro MC01 2
cglobal_mc %1, mc01, %2, 3,5,8
    mov      r4, r1
.body:
    PRELOAD_V

    sub      r4, r2
    sub      r0, r2
%assign j 0
%rep %2
    %assign i (j % 6)
    call v_filt%2_ %+ i %+ _10
    movu     m7, [r4]
    pavgw    m0, m7
    OP_MOV [r0], m0
    SWAP 0,1,2,3,4,5
    %assign j j+1
%endrep
    ret
%endmacro

MC MC01

;-----------------------------------------------------------------------------
; void h264_qpel_mc03(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro MC03 2
cglobal_mc %1, mc03, %2, 3,5,8
    lea r4, [r1+r2]
    jmp stub_%1_h264_qpel%2_mc01_10 %+ SUFFIX %+ .body
%endmacro

MC MC03

;-----------------------------------------------------------------------------
; void h264_qpel_mc11(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro H_FILT_AVG 2-3
h_filt%1_%2_10:
;FILT_H with fewer registers and averaged with the FILT_V result
;m6,m7 are tmp registers, m0 is the FILT_V result, the rest are to be used next in the next iteration
;unfortunately I need three registers, so m5 will have to be re-read from memory
    movu     m5, [r4-4]
    ADDW     m5, [r4+6], m7
    movu     m6, [r4-2]
    ADDW     m6, [r4+4], m7
    paddw    m5, [pw_16]
    psubw    m5, m6  ; a-b
    psraw    m5, 2   ; (a-b)/4
    psubw    m5, m6  ; (a-b)/4-b
    movu     m6, [r4+0]
    ADDW     m6, [r4+2], m7
    paddw    m5, m6  ; (a-b)/4-b+c
    psraw    m5, 2   ; ((a-b)/4-b+c)/4
    paddw    m5, m6  ; ((a-b)/4-b+c)/4+c = (a-5*b+20*c)/16
    psraw    m5, 1
    CLIPW    m5, [pb_0], [pw_pixel_max]
;avg FILT_V, FILT_H
    pavgw    m0, m5
%if %0!=4
    movu     m5, [r1+r5]
%endif
    ret
%endmacro

INIT_MMX mmxext
RESET_MM_PERMUTATION
%assign i 0
%rep 3
H_FILT_AVG 4, i
SWAP 0,1,2,3,4,5
%assign i i+1
%endrep
H_FILT_AVG 4, i, 0

INIT_XMM sse2
RESET_MM_PERMUTATION
%assign i 0
%rep 6
%if i==1
H_FILT_AVG 8, i, 0
%else
H_FILT_AVG 8, i
%endif
SWAP 0,1,2,3,4,5
%assign i i+1
%endrep

%macro MC11 2
; this REALLY needs x86_64
cglobal_mc %1, mc11, %2, 3,6,8
    mov      r4, r1
.body:
    PRELOAD_V

    sub      r0, r2
    sub      r4, r2
    mov      r5, r2
    neg      r5
%assign j 0
%rep %2
    %assign i (j % 6)
    call v_filt%2_ %+ i %+ _10
    call h_filt%2_ %+ i %+ _10
%if %2==8 && i==1
    movu     m5, [r1+r5]
%endif
    OP_MOV [r0], m0
    SWAP 0,1,2,3,4,5
    %assign j j+1
%endrep
    ret
%endmacro

MC MC11

;-----------------------------------------------------------------------------
; void h264_qpel_mc31(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro MC31 2
cglobal_mc %1, mc31, %2, 3,6,8
    mov r4, r1
    add r1, 2
    jmp stub_%1_h264_qpel%2_mc11_10 %+ SUFFIX %+ .body
%endmacro

MC MC31

;-----------------------------------------------------------------------------
; void h264_qpel_mc13(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro MC13 2
cglobal_mc %1, mc13, %2, 3,7,12
    lea r4, [r1+r2]
    jmp stub_%1_h264_qpel%2_mc11_10 %+ SUFFIX %+ .body
%endmacro

MC MC13

;-----------------------------------------------------------------------------
; void h264_qpel_mc33(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro MC33 2
cglobal_mc %1, mc33, %2, 3,6,8
    lea r4, [r1+r2]
    add r1, 2
    jmp stub_%1_h264_qpel%2_mc11_10 %+ SUFFIX %+ .body
%endmacro

MC MC33

;-----------------------------------------------------------------------------
; void h264_qpel_mc22(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro FILT_H2 3
    psubw  %1, %2  ; a-b
    psubw  %2, %3  ; b-c
    psllw  %2, 2
    psubw  %1, %2  ; a-5*b+4*c
    psllw  %3, 4
    paddw  %1, %3  ; a-5*b+20*c
%endmacro

%macro FILT_VNRD 8
    movu     %6, [r1]
    paddw    %1, %6
    mova     %7, %2
    paddw    %7, %5
    mova     %8, %3
    paddw    %8, %4
    FILT_H2  %1, %7, %8
%endmacro

%macro HV 1
%if mmsize==16
%define PAD 12
%define COUNT 2
%else
%define PAD 4
%define COUNT 3
%endif
put_hv%1_10:
    neg      r2           ; This actually saves instructions
    lea      r1, [r1+r2*2-mmsize+PAD]
    lea      r4, [rsp+PAD+gprsize]
    mov     r3d, COUNT
.v_loop:
    movu     m0, [r1]
    sub      r1, r2
    movu     m1, [r1]
    sub      r1, r2
    movu     m2, [r1]
    sub      r1, r2
    movu     m3, [r1]
    sub      r1, r2
    movu     m4, [r1]
    sub      r1, r2
%assign i 0
%rep %1-1
    FILT_VNRD m0, m1, m2, m3, m4, m5, m6, m7
    psubw    m0, [pad20]
    movu     [r4+i*mmsize*3], m0
    sub      r1, r2
    SWAP 0,1,2,3,4,5
%assign i i+1
%endrep
    FILT_VNRD m0, m1, m2, m3, m4, m5, m6, m7
    psubw    m0, [pad20]
    movu     [r4+i*mmsize*3], m0
    add      r4, mmsize
    lea      r1, [r1+r2*8+mmsize]
%if %1==8
    lea      r1, [r1+r2*4]
%endif
    dec      r3d
    jg .v_loop
    neg      r2
    ret
%endmacro

INIT_MMX mmxext
HV 4
INIT_XMM sse2
HV 8

%macro H_LOOP 1
%if num_mmregs > 8
    %define s1 m8
    %define s2 m9
    %define s3 m10
    %define d1 m11
%else
    %define s1 [tap1]
    %define s2 [tap2]
    %define s3 [tap3]
    %define d1 [depad]
%endif
h%1_loop_op:
    movu       m1, [r1+mmsize-4]
    movu       m2, [r1+mmsize-2]
    mova       m3, [r1+mmsize+0]
    movu       m4, [r1+mmsize+2]
    movu       m5, [r1+mmsize+4]
    movu       m6, [r1+mmsize+6]
%if num_mmregs > 8
    pmaddwd    m1, s1
    pmaddwd    m2, s1
    pmaddwd    m3, s2
    pmaddwd    m4, s2
    pmaddwd    m5, s3
    pmaddwd    m6, s3
    paddd      m1, d1
    paddd      m2, d1
%else
    mova       m0, s1
    pmaddwd    m1, m0
    pmaddwd    m2, m0
    mova       m0, s2
    pmaddwd    m3, m0
    pmaddwd    m4, m0
    mova       m0, s3
    pmaddwd    m5, m0
    pmaddwd    m6, m0
    mova       m0, d1
    paddd      m1, m0
    paddd      m2, m0
%endif
    paddd      m3, m5
    paddd      m4, m6
    paddd      m1, m3
    paddd      m2, m4
    psrad      m1, 10
    psrad      m2, 10
    pslld      m2, 16
    pand       m1, [pd_0f]
    por        m1, m2
%if num_mmregs <= 8
    pxor       m0, m0
%endif
    CLIPW      m1, m0, m7
    add        r1, mmsize*3
    ret
%endmacro

INIT_MMX mmxext
H_LOOP 4
INIT_XMM sse2
H_LOOP 8

%macro MC22 2
cglobal_mc %1, mc22, %2, 3,7,12
%define PAD mmsize*8*4*2      ; SIZE*16*4*sizeof(pixel)
    mov      r6, rsp          ; backup stack pointer
    and     rsp, ~(mmsize-1)  ; align stack
    sub     rsp, PAD

    call put_hv%2_10

    mov       r3d, %2
    mova       m7, [pw_pixel_max]
%if num_mmregs > 8
    pxor       m0, m0
    mova       m8, [tap1]
    mova       m9, [tap2]
    mova      m10, [tap3]
    mova      m11, [depad]
%endif
    mov        r1, rsp
.h_loop:
    call h%2_loop_op

    OP_MOV   [r0], m1
    add        r0, r2
    dec       r3d
    jg .h_loop

    mov     rsp, r6          ; restore stack pointer
    ret
%endmacro

MC MC22

;-----------------------------------------------------------------------------
; void h264_qpel_mc12(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro MC12 2
cglobal_mc %1, mc12, %2, 3,7,12
%define PAD mmsize*8*4*2        ; SIZE*16*4*sizeof(pixel)
    mov        r6, rsp          ; backup stack pointer
    and       rsp, ~(mmsize-1)  ; align stack
    sub       rsp, PAD

    call put_hv%2_10

    xor       r4d, r4d
.body:
    mov       r3d, %2
    pxor       m0, m0
    mova       m7, [pw_pixel_max]
%if num_mmregs > 8
    mova       m8, [tap1]
    mova       m9, [tap2]
    mova      m10, [tap3]
    mova      m11, [depad]
%endif
    mov        r1, rsp
.h_loop:
    call h%2_loop_op

    movu       m3, [r1+r4-2*mmsize] ; movu needed for mc32, etc
    paddw      m3, [depad2]
    psrlw      m3, 5
    psubw      m3, [unpad]
    CLIPW      m3, m0, m7
    pavgw      m1, m3

    OP_MOV   [r0], m1
    add        r0, r2
    dec       r3d
    jg .h_loop

    mov     rsp, r6          ; restore stack pointer
    ret
%endmacro

MC MC12

;-----------------------------------------------------------------------------
; void h264_qpel_mc32(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro MC32 2
cglobal_mc %1, mc32, %2, 3,7,12
%define PAD mmsize*8*3*2  ; SIZE*16*4*sizeof(pixel)
    mov  r6, rsp          ; backup stack pointer
    and rsp, ~(mmsize-1)  ; align stack
    sub rsp, PAD

    call put_hv%2_10

    mov r4d, 2            ; sizeof(pixel)
    jmp stub_%1_h264_qpel%2_mc12_10 %+ SUFFIX %+ .body
%endmacro

MC MC32

;-----------------------------------------------------------------------------
; void h264_qpel_mc21(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro H_NRD 1
put_h%1_10:
    add       rsp, gprsize
    mov       r3d, %1
    xor       r4d, r4d
    mova       m6, [pad20]
.nextrow:
    movu       m2, [r5-4]
    movu       m3, [r5-2]
    movu       m4, [r5+0]
    ADDW       m2, [r5+6], m5
    ADDW       m3, [r5+4], m5
    ADDW       m4, [r5+2], m5

    FILT_H2    m2, m3, m4
    psubw      m2, m6
    mova [rsp+r4], m2
    add       r4d, mmsize*3
    add        r5, r2
    dec       r3d
    jg .nextrow
    sub       rsp, gprsize
    ret
%endmacro

INIT_MMX mmxext
H_NRD 4
INIT_XMM sse2
H_NRD 8

%macro MC21 2
cglobal_mc %1, mc21, %2, 3,7,12
    mov   r5, r1
.body:
%define PAD mmsize*8*3*2   ; SIZE*16*4*sizeof(pixel)
    mov   r6, rsp          ; backup stack pointer
    and  rsp, ~(mmsize-1)  ; align stack

    sub  rsp, PAD
    call put_h%2_10

    sub  rsp, PAD
    call put_hv%2_10

    mov r4d, PAD-mmsize    ; H buffer
    jmp stub_%1_h264_qpel%2_mc12_10 %+ SUFFIX %+ .body
%endmacro

MC MC21

;-----------------------------------------------------------------------------
; void h264_qpel_mc23(uint8_t *dst, uint8_t *src, int stride)
;-----------------------------------------------------------------------------
%macro MC23 2
cglobal_mc %1, mc23, %2, 3,7,12
    lea   r5, [r1+r2]
    jmp stub_%1_h264_qpel%2_mc21_10 %+ SUFFIX %+ .body
%endmacro

MC MC23
