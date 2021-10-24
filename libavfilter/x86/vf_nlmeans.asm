;*****************************************************************************
;* x86-optimized functions for nlmeans filter
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

%if HAVE_AVX2_EXTERNAL && ARCH_X86_64

SECTION_RODATA 32

ending_lut: dd -1, -1, -1, -1, -1, -1, -1, -1,\
                0, -1, -1, -1, -1, -1, -1, -1,\
                0,  0, -1, -1, -1, -1, -1, -1,\
                0,  0,  0, -1, -1, -1, -1, -1,\
                0,  0,  0,  0, -1, -1, -1, -1,\
                0,  0,  0,  0,  0, -1, -1, -1,\
                0,  0,  0,  0,  0,  0, -1, -1,\
                0,  0,  0,  0,  0,  0,  0, -1,\
                0,  0,  0,  0,  0,  0,  0,  0

SECTION .text

; void ff_compute_weights_line(const uint32_t *const iia,
;                              const uint32_t *const iib,
;                              const uint32_t *const iid,
;                              const uint32_t *const iie,
;                              const uint8_t *const src,
;                              float *total,
;                              float *sum,
;                              const float *const lut,
;                              int max,
;                              int startx, int endx);

INIT_YMM avx2
cglobal compute_weights_line, 8, 13, 5, 0, iia, iib, iid, iie, src, total, sum, lut, x, startx, endx, mod, elut
    movsxd startxq, dword startxm
    movsxd   endxq, dword endxm
    VPBROADCASTD      m2, r8m

    mov      xq, startxq
    mov    modq, mmsize / 4
    lea   elutq, [ending_lut]

    vpcmpeqd  m4, m4

    .loop:
        mov    startxq, endxq
        sub    startxq, xq
        cmp    startxq, modq
        cmovge startxq, modq
        sal    startxq, 5

        movu   m0, [iieq + xq * 4]

        psubd  m0, [iidq + xq * 4]
        psubd  m0, [iibq + xq * 4]
        paddd  m0, [iiaq + xq * 4]
        por    m0, [elutq + startxq]
        pminud m0, m2
        pslld  m0, 2
        mova   m3, m4
        vgatherdps m1, [lutq + m0], m3

        pmovzxbd m0, [srcq + xq]
        cvtdq2ps m0, m0

        mulps m0, m1

        addps m1, [totalq + xq * 4]
        addps m0, [sumq + xq * 4]

        movups [totalq + xq * 4], m1
        movups [sumq + xq * 4], m0

        add xq, mmsize / 4
        cmp xq, endxq
        jl .loop
    RET

%endif
