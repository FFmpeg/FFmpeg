;*****************************************************************************
;* x86-optimized functions for atadenoise filter
;*
;* Copyright (C) 2019 Paul B Mahol
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

%if ARCH_X86_64

SECTION_RODATA
pw_one:  times 8 dw 1

SECTION .text

;------------------------------------------------------------------------------
; void ff_filter_row(const uint8_t *src, uint8_t *dst,
;                    const uint8_t **srcf,
;                    int w, int mid, int size,
;                    int thra, int thrb)
;------------------------------------------------------------------------------

INIT_XMM sse4
cglobal atadenoise_filter_row8, 6,10,13, src, dst, srcf, w, mid, size, i, j, srcfx, x
    movsxdifnidn    wq, wd
    movsxdifnidn  midq, midd
    movsxdifnidn sizeq, sized
    add           srcq, wq
    add           dstq, wq
    mov             xq, wq
    dec          sizeq
    neg             xq
    movd            m4, r6m
    SPLATW          m4, m4
    movd            m5, r7m
    SPLATW          m5, m5
    pxor            m2, m2
    pcmpeqw        m10, m10

    .loop:
        mov         iq, midq
        mov         jq, midq
        pxor        m3, m3
        pxor       m11, m11
        movq        m0, [srcq + xq]
        mova       m12, m10
        punpcklbw   m0, m2
        mova        m7, m0
        mova        m8, [pw_one]

        .loop0:
            inc              iq
            dec              jq

            mov          srcfxq, [srcfq + jq * 8]
            add          srcfxq, wq

            movq             m1, [srcfxq + xq]
            punpcklbw        m1, m2
            mova             m9, m1
            psubw            m1, m0
            pabsw            m1, m1
            paddw           m11, m1
            pcmpgtw          m1, m4
            mova             m6, m11
            pcmpgtw          m6, m5
            por              m6, m1
            pandn            m6, m12
            mova            m12, m6
            pand             m9, m6
            paddw            m7, m9
            psubw            m8, m6

            mov          srcfxq, [srcfq + iq * 8]
            add          srcfxq, wq

            movq             m1, [srcfxq + xq]
            punpcklbw        m1, m2
            mova             m9, m1
            psubw            m1, m0
            pabsw            m1, m1
            paddw            m3, m1
            pcmpgtw          m1, m4
            mova             m6, m3
            pcmpgtw          m6, m5
            por              m6, m1
            pandn            m6, m12
            ptest            m6, m6
            mova            m12, m6
            pand             m9, m6
            paddw            m7, m9
            psubw            m8, m6

            jz .finish

            cmp              iq, sizeq
            jl .loop0

    .finish:
        mova                 m9, m8
        psrlw                m9, 1
        paddw                m7, m9

        mova                 m1, m7
        mova                 m6, m8

        punpcklwd            m7, m2
        punpcklwd            m8, m2
        punpckhwd            m1, m2
        punpckhwd            m6, m2
        cvtdq2ps             m7, m7
        cvtdq2ps             m8, m8
        cvtdq2ps             m1, m1
        cvtdq2ps             m6, m6
        divps                m7, m8
        divps                m1, m6
        cvttps2dq            m7, m7
        cvttps2dq            m1, m1
        packssdw             m7, m1
        packuswb             m7, m7

        movq        [dstq + xq], m7

        add                  xq, mmsize/2
    jl .loop
    RET

INIT_XMM sse4
cglobal atadenoise_filter_row8_serial, 6,10,12, src, dst, srcf, w, mid, size, i, j, srcfx, x
    movsxdifnidn    wq, wd
    movsxdifnidn  midq, midd
    movsxdifnidn sizeq, sized
    add           srcq, wq
    add           dstq, wq
    mov             xq, wq
    dec          sizeq
    neg             xq
    movd            m4, r6m
    SPLATW          m4, m4
    movd            m5, r7m
    SPLATW          m5, m5
    pxor            m2, m2
    pcmpeqw        m10, m10

    .loop:
        mov         iq, midq
        mov         jq, midq
        pxor        m3, m3
        pxor       m11, m11
        movq        m0, [srcq + xq]
        punpcklbw   m0, m2
        mova        m7, m0
        mova        m8, [pw_one]
        mova       m11, m10

        .loop0:
            dec              jq

            mov          srcfxq, [srcfq + jq * 8]
            add          srcfxq, wq

            movq             m1, [srcfxq + xq]
            punpcklbw        m1, m2
            mova             m9, m1
            psubw            m1, m0
            pabsw            m1, m1
            paddw            m3, m1
            pcmpgtw          m1, m4
            pcmpgtw          m6, m3, m5
            por              m6, m1
            pandn            m6, m11
            ptest            m6, m6
            mova            m11, m6
            pand             m9, m6
            paddw            m7, m9
            psubw            m8, m6

            jz .end_loop0

            cmp              jq, 0
            jg .loop0

        .end_loop0:
            pxor        m3, m3
            mova       m11, m10

        .loop1:
            inc              iq

            mov          srcfxq, [srcfq + iq * 8]
            add          srcfxq, wq

            movq             m1, [srcfxq + xq]
            punpcklbw        m1, m2
            mova             m9, m1
            psubw            m1, m0
            pabsw            m1, m1
            paddw            m3, m1
            pcmpgtw          m1, m4
            mova             m6, m3
            pcmpgtw          m6, m5
            por              m6, m1
            pandn            m6, m11
            ptest            m6, m6
            mova            m11, m6
            pand             m9, m6
            paddw            m7, m9
            psubw            m8, m6

            jz .finish

            cmp              iq, sizeq
            jl .loop1

    .finish:
        mova                 m9, m8
        psrlw                m9, 1
        paddw                m7, m9

        mova                 m1, m7
        mova                 m6, m8

        punpcklwd            m7, m2
        punpcklwd            m8, m2
        punpckhwd            m1, m2
        punpckhwd            m6, m2
        cvtdq2ps             m7, m7
        cvtdq2ps             m8, m8
        cvtdq2ps             m1, m1
        cvtdq2ps             m6, m6
        divps                m7, m8
        divps                m1, m6
        cvttps2dq            m7, m7
        cvttps2dq            m1, m1
        packssdw             m7, m1
        packuswb             m7, m7

        movq        [dstq + xq], m7

        add                  xq, mmsize/2
    jl .loop
    RET

%endif
