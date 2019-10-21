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

%if ARCH_X86_64

%include "libavutil/x86/x86util.asm"

SECTION_RODATA
pw_one:  times 8 dw 1
pw_ones: times 8 dw 65535

SECTION .text

;------------------------------------------------------------------------------
; void ff_filter_row(const uint8_t *src, uint8_t *dst,
;                    const uint8_t **srcf,
;                    int w, int mid, int size,
;                    int thra, int thrb)
;------------------------------------------------------------------------------

INIT_XMM sse4
cglobal atadenoise_filter_row8, 8,10,13, src, dst, srcf, w, mid, size, i, j, srcfx, x
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
    mova           m10, [pw_ones]

    .loop:
        mov         iq, midq
        mov         jq, midq
        pxor        m3, m3
        pxor       m11, m11
        movu        m0, [srcq + xq]
        punpcklbw   m0, m2
        mova        m7, m0
        mova        m8, [pw_one]
        mova       m12, [pw_ones]

        .loop0:
            inc              iq
            dec              jq

            mov          srcfxq, [srcfq + jq * 8]
            add          srcfxq, wq

            movu             m1, [srcfxq + xq]
            punpcklbw        m1, m2
            mova             m9, m1
            psubw            m1, m0
            pabsw            m1, m1
            paddw           m11, m1
            pcmpgtw          m1, m4
            mova             m6, m11
            pcmpgtw          m6, m5
            por              m6, m1
            pxor             m6, m10
            pand            m12, m6
            pand             m9, m12
            paddw            m7, m9
            mova             m6, m12
            psrlw            m6, 15
            paddw            m8, m6

            mov          srcfxq, [srcfq + iq * 8]
            add          srcfxq, wq

            movu             m1, [srcfxq + xq]
            punpcklbw        m1, m2
            mova             m9, m1
            psubw            m1, m0
            pabsw            m1, m1
            paddw            m3, m1
            pcmpgtw          m1, m4
            mova             m6, m3
            pcmpgtw          m6, m5
            por              m6, m1
            pxor             m6, m10
            pand            m12, m6
            pand             m9, m12
            paddw            m7, m9
            mova             m6, m12
            psrlw            m6, 15
            paddw            m8, m6

            ptest           m12, m12
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
        cvtdq2ps             m7, m7
        cvtdq2ps             m8, m8
        divps                m7, m8
        cvttps2dq            m7, m7
        packssdw             m7, m7
        packuswb             m7, m7

        movd        [dstq + xq], m7

        punpckhwd            m1, m2
        punpckhwd            m6, m2
        cvtdq2ps             m1, m1
        cvtdq2ps             m6, m6
        divps                m1, m6
        cvttps2dq            m1, m1
        packssdw             m1, m1
        packuswb             m1, m1

        movd    [dstq + xq + 4], m1

        add                  xq, mmsize/2
    jl .loop
    RET

INIT_XMM sse4
cglobal atadenoise_filter_row8_serial, 8,10,13, src, dst, srcf, w, mid, size, i, j, srcfx, x
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
    mova           m10, [pw_ones]

    .loop:
        mov         iq, midq
        mov         jq, midq
        pxor        m3, m3
        pxor       m11, m11
        movu        m0, [srcq + xq]
        punpcklbw   m0, m2
        mova        m7, m0
        mova        m8, [pw_one]
        mova       m12, [pw_ones]

        .loop0:
            dec              jq

            mov          srcfxq, [srcfq + jq * 8]
            add          srcfxq, wq

            movu             m1, [srcfxq + xq]
            punpcklbw        m1, m2
            mova             m9, m1
            psubw            m1, m0
            pabsw            m1, m1
            paddw           m11, m1
            pcmpgtw          m1, m4
            mova             m6, m11
            pcmpgtw          m6, m5
            por              m6, m1
            pxor             m6, m10
            pand            m12, m6
            pand             m9, m12
            paddw            m7, m9
            mova             m6, m12
            psrlw            m6, 15
            paddw            m8, m6

            ptest           m12, m12
            jz .end_loop0

            cmp              jq, 0
            jg .loop0

        .end_loop0:
            mova       m12, [pw_ones]

        .loop1:
            inc              iq

            mov          srcfxq, [srcfq + iq * 8]
            add          srcfxq, wq

            movu             m1, [srcfxq + xq]
            punpcklbw        m1, m2
            mova             m9, m1
            psubw            m1, m0
            pabsw            m1, m1
            paddw            m3, m1
            pcmpgtw          m1, m4
            mova             m6, m3
            pcmpgtw          m6, m5
            por              m6, m1
            pxor             m6, m10
            pand            m12, m6
            pand             m9, m12
            paddw            m7, m9
            mova             m6, m12
            psrlw            m6, 15
            paddw            m8, m6

            ptest           m12, m12
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
        cvtdq2ps             m7, m7
        cvtdq2ps             m8, m8
        divps                m7, m8
        cvttps2dq            m7, m7
        packssdw             m7, m7
        packuswb             m7, m7

        movd        [dstq + xq], m7

        punpckhwd            m1, m2
        punpckhwd            m6, m2
        cvtdq2ps             m1, m1
        cvtdq2ps             m6, m6
        divps                m1, m6
        cvttps2dq            m1, m1
        packssdw             m1, m1
        packuswb             m1, m1

        movd    [dstq + xq + 4], m1

        add                  xq, mmsize/2
    jl .loop
    RET

%endif
