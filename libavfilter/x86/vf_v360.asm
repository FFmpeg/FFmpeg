;*****************************************************************************
;* x86-optimized functions for v360 filter
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

%if HAVE_AVX2_EXTERNAL

SECTION_RODATA

pb_mask: db 0,4,8,12,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
pw_mask: db 0,1,4, 5, 8, 9,12,13,-1,-1,-1,-1,-1,-1,-1,-1
pd_255: times 4 dd 255
pd_65535: times 4 dd 65535

SECTION .text

; void ff_remap2_8bit_line_avx2(uint8_t *dst, int width, const uint8_t *src, ptrdiff_t in_linesize,
;                               const uint16_t *u, const uint16_t *v, const int16_t *ker);

INIT_YMM avx2
cglobal remap1_8bit_line, 6, 7, 6, dst, width, src, in_linesize, u, v, x
    movsxdifnidn widthq, widthd
    xor             xq, xq
    movd           xm0, in_linesized
    pcmpeqw         m4, m4
    VBROADCASTI128  m3, [pb_mask]
    vpbroadcastd    m0, xm0

    .loop:
        pmovsxwd   m1, [vq + xq * 2]
        pmovsxwd   m2, [uq + xq * 2]

        pmulld           m1, m0
        paddd            m1, m2
        mova             m2, m4
        vpgatherdd       m5, [srcq + m1], m2
        pshufb           m1, m5, m3
        vextracti128    xm2, m1, 1
        movd      [dstq+xq], xm1
        movd    [dstq+xq+4], xm2

        add   xq, mmsize / 4
        cmp   xq, widthq
        jl .loop
    RET

INIT_YMM avx2
cglobal remap1_16bit_line, 6, 7, 6, dst, width, src, in_linesize, u, v, x
    movsxdifnidn widthq, widthd
    xor             xq, xq
    movd           xm0, in_linesized
    pcmpeqw         m4, m4
    VBROADCASTI128  m3, [pw_mask]
    vpbroadcastd    m0, xm0

    .loop:
        pmovsxwd   m1, [vq + xq * 2]
        pmovsxwd   m2, [uq + xq * 2]

        pslld            m2, 0x1
        pmulld           m1, m0
        paddd            m1, m2
        mova             m2, m4
        vpgatherdd       m5, [srcq + m1], m2
        pshufb           m1, m5, m3
        vextracti128    xm2, m1, 1
        movq    [dstq+xq*2], xm1
        movq  [dstq+xq*2+8], xm2

        add   xq, mmsize / 4
        cmp   xq, widthq
        jl .loop
    RET

INIT_YMM avx2
cglobal remap2_8bit_line, 7, 8, 8, dst, width, src, in_linesize, u, v, ker, x
    movsxdifnidn widthq, widthd
    movd           xm0, in_linesized
%if ARCH_X86_32
DEFINE_ARGS dst, width, src, x, u, v, ker
%endif
    xor             xq, xq
    pcmpeqw         m7, m7
    vpbroadcastd    m0, xm0
    vpbroadcastd    m6, [pd_255]

    .loop:
        pmovsxwd   m1, [kerq + xq * 8]
        pmovsxwd   m2, [vq + xq * 8]
        pmovsxwd   m3, [uq + xq * 8]

        pmulld          m4, m2, m0
        paddd           m4, m3
        mova            m3, m7
        vpgatherdd      m2, [srcq + m4], m3
        pand            m2, m6
        pmulld          m2, m1
        phaddd          m2, m2
        phaddd          m1, m2, m2
        psrld           m1, m1, 0xe
        vextracti128   xm2, m1, 1

        pextrb   [dstq+xq], xm1, 0
        pextrb [dstq+xq+1], xm2, 0

        add   xq, mmsize / 16
        cmp   xq, widthq
        jl .loop
    RET

INIT_YMM avx2
cglobal remap2_16bit_line, 7, 8, 8, dst, width, src, in_linesize, u, v, ker, x
    movsxdifnidn widthq, widthd
    movd           xm0, in_linesized
%if ARCH_X86_32
DEFINE_ARGS dst, width, src, x, u, v, ker
%endif
    xor             xq, xq
    pcmpeqw         m7, m7
    vpbroadcastd    m0, xm0
    vpbroadcastd    m6, [pd_65535]

    .loop:
        pmovsxwd   m1, [kerq + xq * 8]
        pmovsxwd   m2, [vq + xq * 8]
        pmovsxwd   m3, [uq + xq * 8]

        pslld           m3, 0x1
        pmulld          m4, m2, m0
        paddd           m4, m3
        mova            m3, m7
        vpgatherdd      m2, [srcq + m4], m3
        pand            m2, m6
        pmulld          m2, m1
        phaddd          m2, m2
        phaddd          m1, m2, m2
        psrld           m1, m1, 0xe
        vextracti128   xm2, m1, 1

        pextrw   [dstq+xq*2], xm1, 0
        pextrw [dstq+xq*2+2], xm2, 0

        add   xq, mmsize / 16
        cmp   xq, widthq
        jl .loop
    RET

%if ARCH_X86_64

INIT_YMM avx2
cglobal remap4_8bit_line, 7, 9, 11, dst, width, src, in_linesize, u, v, ker, x, y
    movsxdifnidn widthq, widthd
    xor             yq, yq
    xor             xq, xq
    movd           xm0, in_linesized
    pcmpeqw         m7, m7
    vpbroadcastd    m0, xm0
    vpbroadcastd    m6, [pd_255]

    .loop:
        pmovsxwd   m1, [kerq + yq]
        pmovsxwd   m5, [kerq + yq + 16]
        pmovsxwd   m2, [vq + yq]
        pmovsxwd   m8, [vq + yq + 16]
        pmovsxwd   m3, [uq + yq]
        pmovsxwd   m9, [uq + yq + 16]

        pmulld          m4, m2, m0
        pmulld         m10, m8, m0
        paddd           m4, m3
        paddd           m10, m9
        mova            m3, m7
        vpgatherdd      m2, [srcq + m4], m3
        mova            m3, m7
        vpgatherdd      m4, [srcq + m10], m3
        pand            m2, m6
        pand            m4, m6
        pmulld          m2, m1
        pmulld          m4, m5

        paddd           m2, m4
        HADDD           m2, m1
        psrld           m2, m2, 0xe
        packuswb        m2, m2

        pextrb   [dstq+xq], xm2, 0

        add   xq, 1
        add   yq, 32
        cmp   xq, widthq
        jl .loop
    RET

%endif
%endif
