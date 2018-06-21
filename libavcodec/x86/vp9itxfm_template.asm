;******************************************************************************
;* VP9 IDCT SIMD optimizations
;*
;* Copyright (C) 2013 Clément Bœsch <u pkh me>
;* Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
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

%macro VP9_IWHT4_1D 0
    SWAP                 1, 2, 3
    paddw               m0, m2
    psubw               m3, m1
    psubw               m4, m0, m3
    psraw               m4, 1
    psubw               m5, m4, m1
    SWAP                 5, 1
    psubw               m4, m2
    SWAP                 4, 2
    psubw               m0, m1
    paddw               m3, m2
    SWAP                 3, 2, 1
%endmacro

; (a*x + b*y + round) >> shift
%macro VP9_MULSUB_2W_2X 5 ; dst1, dst2/src, round, coefs1, coefs2
    pmaddwd            m%1, m%2, %4
    pmaddwd            m%2,  %5
    paddd              m%1,  %3
    paddd              m%2,  %3
    psrad              m%1,  14
    psrad              m%2,  14
%endmacro

%macro VP9_MULSUB_2W_4X 7 ; dst1, dst2, coef1, coef2, rnd, tmp1/src, tmp2
    VP9_MULSUB_2W_2X    %7,  %6,  %5, [pw_m%3_%4], [pw_%4_%3]
    VP9_MULSUB_2W_2X    %1,  %2,  %5, [pw_m%3_%4], [pw_%4_%3]
    packssdw           m%1, m%7
    packssdw           m%2, m%6
%endmacro

%macro VP9_UNPACK_MULSUB_2W_4X 7-9 ; dst1, dst2, (src1, src2,) coef1, coef2, rnd, tmp1, tmp2
%if %0 == 7
    punpckhwd          m%6, m%2, m%1
    punpcklwd          m%2, m%1
    VP9_MULSUB_2W_4X   %1, %2, %3, %4, %5, %6, %7
%else
    punpckhwd          m%8, m%4, m%3
    punpcklwd          m%2, m%4, m%3
    VP9_MULSUB_2W_4X   %1, %2, %5, %6, %7, %8, %9
%endif
%endmacro

%macro VP9_IDCT4_1D_FINALIZE 0
    SUMSUB_BA            w, 3, 2, 4                         ; m3=t3+t0, m2=-t3+t0
    SUMSUB_BA            w, 1, 0, 4                         ; m1=t2+t1, m0=-t2+t1
    SWAP                 0, 3, 2                            ; 3102 -> 0123
%endmacro

%macro VP9_IDCT4_1D 0
%if cpuflag(ssse3)
    SUMSUB_BA            w, 2, 0, 4                         ; m2=IN(0)+IN(2) m0=IN(0)-IN(2)
    pmulhrsw            m2, m6                              ; m2=t0
    pmulhrsw            m0, m6                              ; m0=t1
%else ; <= sse2
    VP9_UNPACK_MULSUB_2W_4X 0, 2, 11585, 11585, m7, 4, 5    ; m0=t1, m1=t0
%endif
    VP9_UNPACK_MULSUB_2W_4X 1, 3, 15137, 6270, m7, 4, 5     ; m1=t2, m3=t3
    VP9_IDCT4_1D_FINALIZE
%endmacro

%macro VP9_IADST4_1D 0
    movq2dq           xmm0, m0
    movq2dq           xmm1, m1
    movq2dq           xmm2, m2
    movq2dq           xmm3, m3
%if cpuflag(ssse3)
    paddw               m3, m0
%endif
    punpcklwd         xmm0, xmm1
    punpcklwd         xmm2, xmm3
    pmaddwd           xmm1, xmm0, [pw_5283_13377]
    pmaddwd           xmm4, xmm0, [pw_9929_13377]
%if notcpuflag(ssse3)
    pmaddwd           xmm6, xmm0, [pw_13377_0]
%endif
    pmaddwd           xmm0, [pw_15212_m13377]
    pmaddwd           xmm3, xmm2, [pw_15212_9929]
%if notcpuflag(ssse3)
    pmaddwd           xmm7, xmm2, [pw_m13377_13377]
%endif
    pmaddwd           xmm2, [pw_m5283_m15212]
%if cpuflag(ssse3)
    psubw               m3, m2
%else
    paddd             xmm6, xmm7
%endif
    paddd             xmm0, xmm2
    paddd             xmm3, xmm5
    paddd             xmm2, xmm5
%if notcpuflag(ssse3)
    paddd             xmm6, xmm5
%endif
    paddd             xmm1, xmm3
    paddd             xmm0, xmm3
    paddd             xmm4, xmm2
    psrad             xmm1, 14
    psrad             xmm0, 14
    psrad             xmm4, 14
%if cpuflag(ssse3)
    pmulhrsw            m3, [pw_13377x2]        ; out2
%else
    psrad             xmm6, 14
%endif
    packssdw          xmm0, xmm0
    packssdw          xmm1, xmm1
    packssdw          xmm4, xmm4
%if notcpuflag(ssse3)
    packssdw          xmm6, xmm6
%endif
    movdq2q             m0, xmm0                ; out3
    movdq2q             m1, xmm1                ; out0
    movdq2q             m2, xmm4                ; out1
%if notcpuflag(ssse3)
    movdq2q             m3, xmm6                ; out2
%endif
    SWAP                 0, 1, 2, 3
%endmacro
