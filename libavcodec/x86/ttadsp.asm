;******************************************************************************
;* TTA DSP SIMD optimizations
;*
;* Copyright (C) 2014 James Almer
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

pd_n0113: dd ~0, ~1, ~1, ~3
pd_1224:  dd 1, 2, 2, 4

SECTION .text

%macro TTA_FILTER 2
INIT_XMM %1
cglobal tta_filter_process, 5,5,%2, qm, dx, dl, error, in, shift, round
    mova       m2, [qmq       ]
    mova       m3, [qmq + 0x10]
    mova       m4, [dxq       ]
    mova       m5, [dxq + 0x10]

    movd       m6, [errorq]         ; if (filter->error < 0) {
    SPLATD     m6                   ;     for (int i = 0; i < 8; i++)
    psignd     m0, m4, m6           ;         filter->qm[i] -= filter->dx[i];
    psignd     m1, m5, m6           ; } else if (filter->error > 0) {
    paddd      m2, m0               ;     for (int i = 0; i < 8; i++)
    paddd      m3, m1               ;         filter->qm[i] += filter->dx[i];
    mova       [qmq       ], m2     ; }
    mova       [qmq + 0x10], m3     ;

    mova       m0, [dlq       ]
    mova       m1, [dlq + 0x10]

%if cpuflag(sse4)
    pmulld     m2, m0
    pmulld     m3, m1
%else
    pshufd     m6, m0, 0xb1
    pshufd     m7, m2, 0xb1
    pmuludq    m6, m7
    pshufd     m6, m6, 0xd8
    pmuludq    m2, m0
    pshufd     m2, m2, 0xd8
    punpckldq  m2, m6

    pshufd     m6, m1, 0xb1
    pshufd     m7, m3, 0xb1
    pmuludq    m6, m7
    pshufd     m6, m6, 0xd8
    pmuludq    m3, m1
    pshufd     m3, m3, 0xd8
    punpckldq  m3, m6
%endif
    ; Using horizontal add (phaddd) seems to be slower than shuffling stuff around
    paddd      m2, m3               ; int sum = filter->round +
                                    ;           filter->dl[0] * filter->qm[0] +
    pshufd     m3, m2, 0xe          ;           filter->dl[1] * filter->qm[1] +
    paddd      m2, m3               ;           filter->dl[2] * filter->qm[2] +
                                    ;           filter->dl[3] * filter->qm[3] +
    movd       m6, roundm           ;           filter->dl[4] * filter->qm[4] +
    paddd      m6, m2               ;           filter->dl[5] * filter->qm[5] +
    pshufd     m2, m2, 0x1          ;           filter->dl[6] * filter->qm[6] +
    paddd      m6, m2               ;           filter->dl[7] * filter->qm[7];

    palignr    m5, m4, 4            ; filter->dx[0] = filter->dx[1]; filter->dx[1] = filter->dx[2];
                                    ; filter->dx[2] = filter->dx[3]; filter->dx[3] = filter->dx[4];

    palignr    m2, m1, m0, 4        ; filter->dl[0] = filter->dl[1]; filter->dl[1] = filter->dl[2];
                                    ; filter->dl[2] = filter->dl[3]; filter->dl[3] = filter->dl[4];

    psrad      m4, m1, 30           ; filter->dx[4] = ((filter->dl[4] >> 30) | 1);
    por        m4, [pd_1224 ]       ; filter->dx[5] = ((filter->dl[5] >> 30) | 2) & ~1;
    pand       m4, [pd_n0113]       ; filter->dx[6] = ((filter->dl[6] >> 30) | 2) & ~1;
                                    ; filter->dx[7] = ((filter->dl[7] >> 30) | 4) & ~3;

    mova       [dlq       ], m2
    mova       [dxq       ], m5
    mova       [dxq + 0x10], m4
    movd       m0, [inq]            ; filter->error = *in;
    movd       [errorq], m0         ;

    movd       m2, shiftm           ; *in += (sum >> filter->shift);
    psrad      m6, m2               ;
    paddd      m0, m6               ;
    movd       [inq], m0            ;

    psrldq     m1, 4                ;
    pslldq     m0, 12               ; filter->dl[4] = -filter->dl[5];
    pshufd     m0, m0, 0xf0         ; filter->dl[5] = -filter->dl[6];
    psubd      m0, m1               ; filter->dl[6] = *in - filter->dl[7];
    psrldq     m1, m0, 4            ; filter->dl[7] = *in;
    pshufd     m1, m1, 0xf4         ; filter->dl[5] += filter->dl[6];
    paddd      m0, m1               ; filter->dl[4] += filter->dl[5];
    psrldq     m1, 4                ;
    paddd      m0, m1               ;
    mova       [dlq + 0x10], m0     ;
    RET
%endmacro

TTA_FILTER ssse3, 8
TTA_FILTER sse4,  7
