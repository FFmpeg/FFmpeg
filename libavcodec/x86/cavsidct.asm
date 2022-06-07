; Chinese AVS video (AVS1-P2, JiZhun profile) decoder
; Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
;
; MMX-optimized DSP functions, based on H.264 optimizations by
; Michael Niedermayer and Loren Merritt
; Conversion from gcc syntax to x264asm syntax with modifications
; by Ronald S. Bultje <rsbultje@gmail.com>
;
; This file is part of FFmpeg.
;
; FFmpeg is free software; you can redistribute it and/or
; modify it under the terms of the GNU Lesser General Public
; License as published by the Free Software Foundation; either
; version 2.1 of the License, or (at your option) any later version.
;
; FFmpeg is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; Lesser General Public License for more details.
;
; You should have received a copy of the GNU Lesser General Public License
; along with FFmpeg; if not, write to the Free Software Foundation,
; Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

%include "libavutil/x86/x86util.asm"

cextern pw_4
cextern pw_64

SECTION .text

%macro CAVS_IDCT8_1D 2-3 1 ; source, round, init_load
%if %3 == 1
    mova            m4, [%1+7*16]       ; m4 = src7
    mova            m5, [%1+1*16]       ; m5 = src1
    mova            m2, [%1+5*16]       ; m2 = src5
    mova            m7, [%1+3*16]       ; m7 = src3
%else
    SWAP             1, 7
    SWAP             4, 6
%endif
    mova            m0, m4
    mova            m3, m5
    mova            m6, m2
    mova            m1, m7

    paddw           m4, m4              ; m4 = 2*src7
    paddw           m3, m3              ; m3 = 2*src1
    paddw           m6, m6              ; m6 = 2*src5
    paddw           m1, m1              ; m1 = 2*src3
    paddw           m0, m4              ; m0 = 3*src7
    paddw           m5, m3              ; m5 = 3*src1
    paddw           m2, m6              ; m2 = 3*src5
    paddw           m7, m1              ; m7 = 3*src3
    psubw           m5, m4              ; m5 = 3*src1 - 2*src7 = a0
    paddw           m7, m6              ; m7 = 3*src3 - 2*src5 = a1
    psubw           m1, m2              ; m1 = 2*src3 - 3*src5 = a2
    paddw           m3, m0              ; m3 = 2*src1 - 3*src7 = a3

    mova            m4, m5
    mova            m6, m7
    mova            m0, m3
    mova            m2, m1
    SUMSUB_BA     w, 7, 5               ; m7 = a0 + a1, m5 = a0 - a1
    paddw           m7, m3              ; m7 = a0 + a1 + a3
    paddw           m5, m1              ; m5 = a0 - a1 + a2
    paddw           m7, m7
    paddw           m5, m5
    paddw           m7, m6              ; m7 = b4
    paddw           m5, m4              ; m5 = b5

    SUMSUB_BA     w, 1, 3               ; m1 = a3 + a2, m3 = a3 - a2
    psubw           m4, m1              ; m4 = a0 - a2 - a3
    mova            m1, m4              ; m1 = a0 - a2 - a3
    psubw           m3, m6              ; m3 = a3 - a2 - a1
    paddw           m1, m1
    paddw           m3, m3
    psubw           m1, m2              ; m1 = b7
    paddw           m3, m0              ; m3 = b6

    mova            m2, [%1+2*16]       ; m2 = src2
    mova            m6, [%1+6*16]       ; m6 = src6
    mova            m4, m2
    mova            m0, m6
    psllw           m4, 2               ; m4 = 4*src2
    psllw           m6, 2               ; m6 = 4*src6
    paddw           m2, m4              ; m2 = 5*src2
    paddw           m0, m6              ; m0 = 5*src6
    paddw           m2, m2
    paddw           m0, m0
    psubw           m4, m0              ; m4 = 4*src2 - 10*src6 = a7
    paddw           m6, m2              ; m6 = 4*src6 + 10*src2 = a6

    mova            m2, [%1+0*16]       ; m2 = src0
    mova            m0, [%1+4*16]       ; m0 = src4
    SUMSUB_BA     w, 0, 2               ; m0 = src0 + src4, m2 = src0 - src4
    psllw           m0, 3
    psllw           m2, 3
    paddw           m0, %2              ; add rounding bias
    paddw           m2, %2              ; add rounding bias

    SUMSUB_BA     w, 6, 0               ; m6 = a4 + a6, m0 = a4 - a6
    SUMSUB_BA     w, 4, 2               ; m4 = a5 + a7, m2 = a5 - a7
    SUMSUB_BA     w, 7, 6               ; m7 = dst0, m6 = dst7
    SUMSUB_BA     w, 5, 4               ; m5 = dst1, m4 = dst6
    SUMSUB_BA     w, 3, 2               ; m3 = dst2, m2 = dst5
    SUMSUB_BA     w, 1, 0               ; m1 = dst3, m0 = dst4
%endmacro

INIT_XMM sse2
cglobal cavs_idct8, 2, 2, 8 + ARCH_X86_64, 0 - 8 * 16, out, in
    CAVS_IDCT8_1D  inq, [pw_4]
    psraw           m7, 3
    psraw           m6, 3
    psraw           m5, 3
    psraw           m4, 3
    psraw           m3, 3
    psraw           m2, 3
    psraw           m1, 3
    psraw           m0, 3
%if ARCH_X86_64
    TRANSPOSE8x8W    7, 5, 3, 1, 0, 2, 4, 6, 8
    mova    [rsp+4*16], m0
%else
    mova    [rsp+0*16], m4
    TRANSPOSE8x8W    7, 5, 3, 1, 0, 2, 4, 6, [rsp+0*16], [rsp+4*16], 1
%endif
    mova    [rsp+0*16], m7
    mova    [rsp+2*16], m3
    mova    [rsp+6*16], m4
    CAVS_IDCT8_1D  rsp, [pw_64], 0
    psraw           m7, 7
    psraw           m6, 7
    psraw           m5, 7
    psraw           m4, 7
    psraw           m3, 7
    psraw           m2, 7
    psraw           m1, 7
    psraw           m0, 7

    mova   [outq+0*16], m7
    mova   [outq+1*16], m5
    mova   [outq+2*16], m3
    mova   [outq+3*16], m1
    mova   [outq+4*16], m0
    mova   [outq+5*16], m2
    mova   [outq+6*16], m4
    mova   [outq+7*16], m6
    RET
