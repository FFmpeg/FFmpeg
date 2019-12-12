;*****************************************************************************
;* x86-optimized functions for volume filter
;* Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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

SECTION_RODATA 32

pd_1_256:     times 4 dq 0x3F70000000000000
pd_int32_max: times 4 dq 0x41DFFFFFFFC00000
pw_1:         times 8 dw 1
pw_128:       times 8 dw 128
pq_128:       times 2 dq 128

SECTION .text

;------------------------------------------------------------------------------
; void ff_scale_samples_s16(uint8_t *dst, const uint8_t *src, int len,
;                           int volume)
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal scale_samples_s16, 4,4,4, dst, src, len, volume
    movd        m0, volumem
    pshuflw     m0, m0, 0
    punpcklwd   m0, [pw_1]
    mova        m1, [pw_128]
    lea       lenq, [lend*2-mmsize]
.loop:
    ; dst[i] = av_clip_int16((src[i] * volume + 128) >> 8);
    mova        m2, [srcq+lenq]
    punpcklwd   m3, m2, m1
    punpckhwd   m2, m1
    pmaddwd     m3, m0
    pmaddwd     m2, m0
    psrad       m3, 8
    psrad       m2, 8
    packssdw    m3, m2
    mova  [dstq+lenq], m3
    sub       lenq, mmsize
    jge .loop
    REP_RET

;------------------------------------------------------------------------------
; void ff_scale_samples_s32(uint8_t *dst, const uint8_t *src, int len,
;                           int volume)
;------------------------------------------------------------------------------

%macro SCALE_SAMPLES_S32 0
cglobal scale_samples_s32, 4,4,4, dst, src, len, volume
%if ARCH_X86_32 && cpuflag(avx)
    vbroadcastss   xmm2, volumem
%else
    movd           xmm2, volumed
    pshufd         xmm2, xmm2, 0
%endif
    CVTDQ2PD         m2, xmm2
    mulpd            m2, m2, [pd_1_256]
    mova             m3, [pd_int32_max]
    lea            lenq, [lend*4-mmsize]
.loop:
    CVTDQ2PD         m0, [srcq+lenq         ]
    CVTDQ2PD         m1, [srcq+lenq+mmsize/2]
    mulpd            m0, m0, m2
    mulpd            m1, m1, m2
    minpd            m0, m0, m3
    minpd            m1, m1, m3
    cvtpd2dq       xmm0, m0
    cvtpd2dq       xmm1, m1
%if cpuflag(avx)
    vmovdqa [dstq+lenq         ], xmm0
    vmovdqa [dstq+lenq+mmsize/2], xmm1
%else
    movq    [dstq+lenq         ], xmm0
    movq    [dstq+lenq+mmsize/2], xmm1
%endif
    sub            lenq, mmsize
    jge .loop
    REP_RET
%endmacro

INIT_XMM sse2
%define CVTDQ2PD cvtdq2pd
SCALE_SAMPLES_S32
%if HAVE_AVX_EXTERNAL
%define CVTDQ2PD vcvtdq2pd
INIT_YMM avx
SCALE_SAMPLES_S32
%endif
%undef CVTDQ2PD

; NOTE: This is not bit-identical with the C version because it clips to
;       [-INT_MAX, INT_MAX] instead of [INT_MIN, INT_MAX]

INIT_XMM ssse3, atom
cglobal scale_samples_s32, 4,4,8, dst, src, len, volume
    movd        m4, volumem
    pshufd      m4, m4, 0
    mova        m5, [pq_128]
    pxor        m6, m6
    lea       lenq, [lend*4-mmsize]
.loop:
    ; src[i] = av_clipl_int32((src[i] * volume + 128) >> 8);
    mova        m7, [srcq+lenq]
    pabsd       m3, m7
    pshufd      m0, m3, q0100
    pshufd      m1, m3, q0302
    pmuludq     m0, m4
    pmuludq     m1, m4
    paddq       m0, m5
    paddq       m1, m5
    psrlq       m0, 7
    psrlq       m1, 7
    shufps      m2, m0, m1, q3131
    shufps      m0, m0, m1, q2020
    pcmpgtd     m2, m6
    por         m0, m2
    psrld       m0, 1
    psignd      m0, m7
    mova  [dstq+lenq], m0
    sub       lenq, mmsize
    jge .loop
    REP_RET
