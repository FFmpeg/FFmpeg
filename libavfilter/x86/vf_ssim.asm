;*****************************************************************************
;* x86-optimized functions for ssim filter
;*
;* Copyright (C) 2015 Ronald S. Bultje <rsbultje@gmail.com>
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

pw_1: times 8 dw 1
ssim_c1: times 4 dd 416 ;(.01*.01*255*255*64 + .5)
ssim_c2: times 4 dd 235963 ;(.03*.03*255*255*64*63 + .5)

SECTION .text

%macro SSIM_4X4_LINE 1
%if ARCH_X86_64
cglobal ssim_4x4_line, 6, 8, %1, buf, buf_stride, ref, ref_stride, sums, w, buf_stride3, ref_stride3
%else
cglobal ssim_4x4_line, 5, 7, %1, buf, buf_stride, ref, ref_stride, sums, buf_stride3, ref_stride3
%define wd r5mp
%endif
    lea     ref_stride3q, [ref_strideq*3]
    lea     buf_stride3q, [buf_strideq*3]
%if notcpuflag(xop)
    pxor              m7, m7
    mova             m15, [pw_1]
%endif

.loop:
%if cpuflag(xop)
    pmovzxbw          m0, [bufq+buf_strideq*0]
    pmovzxbw          m1, [refq+ref_strideq*0]
    pmaddwd           m4, m0, m0
    pmaddwd           m6, m0, m1
    pmovzxbw          m2, [bufq+buf_strideq*1]
    vpmadcswd         m4, m1, m1, m4
    pmovzxbw          m3, [refq+ref_strideq*1]
    paddw             m0, m2
    vpmadcswd         m4, m2, m2, m4
    vpmadcswd         m6, m2, m3, m6
    paddw             m1, m3
    vpmadcswd         m4, m3, m3, m4

    pmovzxbw          m2, [bufq+buf_strideq*2]
    pmovzxbw          m3, [refq+ref_strideq*2]
    vpmadcswd         m4, m2, m2, m4
    vpmadcswd         m6, m2, m3, m6
    pmovzxbw          m5, [bufq+buf_stride3q]
    pmovzxbw          m7, [refq+ref_stride3q]
    vpmadcswd         m4, m3, m3, m4
    vpmadcswd         m6, m5, m7, m6
    paddw             m0, m2
    paddw             m1, m3
    vpmadcswd         m4, m5, m5, m4
    paddw             m0, m5
    paddw             m1, m7
    vpmadcswd         m4, m7, m7, m4
%else
    movh              m0, [bufq+buf_strideq*0]  ; a1
    movh              m1, [refq+ref_strideq*0]  ; b1
    movh              m2, [bufq+buf_strideq*1]  ; a2
    movh              m3, [refq+ref_strideq*1]  ; b2
    punpcklbw         m0, m7                    ; s1 [word]
    punpcklbw         m1, m7                    ; s2 [word]
    punpcklbw         m2, m7                    ; s1 [word]
    punpcklbw         m3, m7                    ; s2 [word]
    pmaddwd           m4, m0, m0                ; a1 * a1
    pmaddwd           m5, m1, m1                ; b1 * b1
    pmaddwd           m8, m2, m2                ; a2 * a2
    pmaddwd           m9, m3, m3                ; b2 * b2
    paddd             m4, m5                    ; ss
    paddd             m8, m9                    ; ss
    pmaddwd           m6, m0, m1                ; a1 * b1 = ss12
    pmaddwd           m5, m2, m3                ; a2 * b2 = ss12
    paddw             m0, m2
    paddw             m1, m3
    paddd             m6, m5                    ; s12
    paddd             m4, m8                    ; ss

    movh              m2, [bufq+buf_strideq*2]  ; a3
    movh              m3, [refq+ref_strideq*2]  ; b3
    movh              m5, [bufq+buf_stride3q]   ; a4
    movh              m8, [refq+ref_stride3q]   ; b4
    punpcklbw         m2, m7                    ; s1 [word]
    punpcklbw         m3, m7                    ; s2 [word]
    punpcklbw         m5, m7                    ; s1 [word]
    punpcklbw         m8, m7                    ; s2 [word]
    pmaddwd           m9, m2, m2                ; a3 * a3
    pmaddwd          m10, m3, m3                ; b3 * b3
    pmaddwd          m12, m5, m5                ; a4 * a4
    pmaddwd          m13, m8, m8                ; b4 * b4
    pmaddwd          m11, m2, m3                ; a3 * b3 = ss12
    pmaddwd          m14, m5, m8                ; a4 * b4 = ss12
    paddd             m9, m10
    paddd            m12, m13
    paddw             m0, m2
    paddw             m1, m3
    paddw             m0, m5
    paddw             m1, m8
    paddd             m6, m11
    paddd             m4, m9
    paddd             m6, m14
    paddd             m4, m12
%endif

    ; m0 = [word] s1 a,a,a,a,b,b,b,b
    ; m1 = [word] s2 a,a,a,a,b,b,b,b
    ; m4 = [dword] ss a,a,b,b
    ; m6 = [dword] s12 a,a,b,b

%if cpuflag(xop)
    vphaddwq          m0, m0                    ; [dword] s1  a, 0, b, 0
    vphaddwq          m1, m1                    ; [dword] s2  a, 0, b, 0
    vphadddq          m4, m4                    ; [dword] ss  a, 0, b, 0
    vphadddq          m6, m6                    ; [dword] s12 a, 0, b, 0
    punpckhdq     m2, m0, m1                    ; [dword] s1  b, s2 b, 0, 0
    punpckldq         m0, m1                    ; [dword] s1  a, s2 a, 0, 0
    punpckhdq     m3, m4, m6                    ; [dword] ss  b, s12 b, 0, 0
    punpckldq         m4, m6                    ; [dword] ss  a, s12 a, 0, 0
    punpcklqdq    m1, m2, m3                    ; [dword] b s1, s2, ss, s12
    punpcklqdq        m0, m4                    ; [dword] a s1, s2, ss, s12
%else
    pmaddwd           m0, m15                   ; [dword] s1 a,a,b,b
    pmaddwd           m1, m15                   ; [dword] s2 a,a,b,b
    phaddd            m0, m4                    ; [dword] s1 a, b, ss a, b
    phaddd            m1, m6                    ; [dword] s2 a, b, s12 a, b
    punpckhdq     m2, m0, m1                    ; [dword] ss a, s12 a, ss b, s12 b
    punpckldq         m0, m1                    ; [dword] s1 a, s2 a, s1 b, s2 b
    punpckhqdq    m1, m0, m2                    ; [dword] b s1, s2, ss, s12
    punpcklqdq        m0, m2                    ; [dword] a s1, s2, ss, s12
%endif

    mova  [sumsq+     0], m0
    mova  [sumsq+mmsize], m1

    add             bufq, mmsize/2
    add             refq, mmsize/2
    add            sumsq, mmsize*2
    sub               wd, mmsize/8
    jg .loop
    RET
%endmacro

%if ARCH_X86_64
INIT_XMM ssse3
SSIM_4X4_LINE 16
%endif
%if HAVE_XOP_EXTERNAL
INIT_XMM xop
SSIM_4X4_LINE 8
%endif

INIT_XMM sse4
cglobal ssim_end_line, 3, 3, 7, sum0, sum1, w
    pxor              m0, m0
    pxor              m6, m6
.loop:
    mova              m1, [sum0q+mmsize*0]
    mova              m2, [sum0q+mmsize*1]
    mova              m3, [sum0q+mmsize*2]
    mova              m4, [sum0q+mmsize*3]
    paddd             m1, [sum1q+mmsize*0]
    paddd             m2, [sum1q+mmsize*1]
    paddd             m3, [sum1q+mmsize*2]
    paddd             m4, [sum1q+mmsize*3]
    paddd             m1, m2
    paddd             m2, m3
    paddd             m3, m4
    paddd             m4, [sum0q+mmsize*4]
    paddd             m4, [sum1q+mmsize*4]
    TRANSPOSE4x4D      1, 2, 3, 4, 5

    ; m1 = fs1, m2 = fs2, m3 = fss, m4 = fs12
    pslld             m3, 6
    pslld             m4, 6
    pmulld            m5, m1, m2                ; fs1 * fs2
    pmulld            m1, m1                    ; fs1 * fs1
    pmulld            m2, m2                    ; fs2 * fs2
    psubd             m3, m1
    psubd             m4, m5                    ; covariance
    psubd             m3, m2                    ; variance

    ; m1 = fs1 * fs1, m2 = fs2 * fs2, m3 = variance, m4 = covariance, m5 = fs1 * fs2
    paddd             m4, m4                    ; 2 * covariance
    paddd             m5, m5                    ; 2 * fs1 * fs2
    paddd             m1, m2                    ; fs1 * fs1 + fs2 * fs2
    paddd             m3, [ssim_c2]             ; variance + ssim_c2
    paddd             m4, [ssim_c2]             ; 2 * covariance + ssim_c2
    paddd             m5, [ssim_c1]             ; 2 * fs1 * fs2 + ssim_c1
    paddd             m1, [ssim_c1]             ; fs1 * fs1 + fs2 * fs2 + ssim_c1

    ; convert to float
    cvtdq2ps          m3, m3
    cvtdq2ps          m4, m4
    cvtdq2ps          m5, m5
    cvtdq2ps          m1, m1
    mulps             m4, m5
    mulps             m3, m1
    divps             m4, m3                    ; ssim_endl
    mova              m5, m4
    cvtps2pd          m3, m5
    movhlps           m5, m5
    cvtps2pd          m5, m5
    addpd             m0, m3                    ; ssim
    addpd             m6, m5                    ; ssim
    add            sum0q, mmsize*4
    add            sum1q, mmsize*4
    sub               wd, 4
    jg .loop

    ; subpd the ones we added too much
    test              wd, wd
    jz               .end
    add               wd, 4
    cmp               wd, 1
    jz               .skip3
    cmp               wd, 2
    jz               .skip2
.skip1:               ; 3 valid => skip 1 invalid
    psrldq            m5, 8
    subpd             m6, m5
    jmp              .end
.skip2:               ; 2 valid => skip 2 invalid
    subpd             m6, m5
    jmp              .end
.skip3:               ; 1 valid => skip 3 invalid
    psrldq            m3, 8
    subpd             m0, m3
    subpd             m6, m5

.end:
    addpd             m0, m6
    movhlps           m4, m0
    addpd             m0, m4
%if ARCH_X86_32
    movsd            r0m, m0
    fld        qword r0m
%endif
    RET
