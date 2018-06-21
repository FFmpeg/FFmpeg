;*****************************************************************************
;* x86-optimized functions for fspp filter
;*
;* Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
;* Copyright (C) 2005 Nikolaj Poroshin <porosh3@psu.ru>
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License as published by
;* the Free Software Foundation; either version 2 of the License, or
;* (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;* GNU General Public License for more details.
;*
;* You should have received a copy of the GNU General Public License along
;* with FFmpeg; if not, write to the Free Software Foundation, Inc.,
;* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

pb_dither: db 0,  48,  12,  60,   3,  51,  15,  63, 32,  16,  44,  28,  35,  19,  47,  31, \
              8,  56,   4,  52,  11,  59,   7,  55, 40,  24,  36,  20,  43,  27,  39,  23, \
              2,  50,  14,  62,   1,  49,  13,  61, 34,  18,  46,  30,  33,  17,  45,  29, \
             10,  58,   6,  54,   9,  57,   5,  53, 42,  26,  38,  22,  41,  25,  37,  21
pw_187E: times 4 dw 0x187E ; FIX64(0.382683433, 14)
pw_22A3: times 4 dw 0x22A3 ; FIX64(1.082392200, 13)
pw_2D41: times 4 dw 0x2D41 ; FIX64(1.414213562, 13)
pw_539F: times 4 dw 0x539F ; FIX64(1.306562965, 14)
pw_5A82: times 4 dw 0x5A82 ; FIX64(1.414213562, 14)
pw_3B21: times 4 dw 0x3B21 ; FIX64(1.847759065, 13)
pw_AC62: times 4 dw 0xAC62 ; FIX64(-2.613125930, 13)
pw_3642: times 4 dw 0x3642 ; FIX64(0.847759065, 14)
pw_2441: times 4 dw 0x2441 ; FIX64(0.566454497, 14)
pw_0CBB: times 4 dw 0x0CBB ; FIX64(0.198912367, 14)
pw_4:    times 4 dw 4
pw_2:    times 4 dw 2

SECTION .text

%define DCTSIZE 8

INIT_MMX mmx

;void ff_store_slice_mmx(uint8_t *dst, int16_t *src,
;                        ptrdiff_t dst_stride, ptrdiff_t src_stride,
;                        ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale)
%if ARCH_X86_64
cglobal store_slice, 7, 9, 0, dst, src, dst_stride, src_stride, width, dither_height, dither, tmp, tmp2
%else
cglobal store_slice, 2, 7, 0, dst, src, width, dither_height, dither, tmp, tmp2
%define dst_strideq r2m
%define src_strideq r3m
    mov       widthq, r4m
    mov       dither_heightq, r5m
    mov       ditherq, r6m ; log2_scale
%endif
    add       widthq, 7
    mov       tmpq, src_strideq
    and       widthq, ~7
    sub       dst_strideq, widthq
    movd      m5, ditherd ; log2_scale
    xor       ditherq, -1 ; log2_scale
    mov       tmp2q, tmpq
    add       ditherq, 7 ; log2_scale
    neg       tmpq
    sub       tmp2q, widthq
    movd      m2, ditherd ; log2_scale
    add       tmp2q, tmp2q
    lea       ditherq, [pb_dither]
    mov       src_strideq, tmp2q
    shl       tmpq, 4
    lea       dither_heightq, [ditherq+dither_heightq*8]
    pxor      m7, m7

.loop_height:
    movq      m3, [ditherq]
    movq      m4, m3
    punpcklbw m3, m7
    punpckhbw m4, m7
    mov       tmp2q, widthq
    psraw     m3, m5
    psraw     m4, m5

.loop_width:
    movq      [srcq+tmpq], m7
    movq      m0, [srcq]
    movq      m1, [srcq+8]
    movq      [srcq+tmpq+8], m7
    paddw     m0, m3
    paddw     m1, m4
    movq      [srcq], m7
    psraw     m0, m2
    psraw     m1, m2
    movq      [srcq+8], m7
    packuswb  m0, m1
    add       srcq, 16
    movq      [dstq], m0
    add       dstq, 8
    sub       tmp2q, 8
    jg .loop_width

    add       srcq, src_strideq
    add       ditherq, 8
    add       dstq, dst_strideq
    cmp       ditherq, dither_heightq
    jl .loop_height
    RET

;void ff_store_slice2_mmx(uint8_t *dst, int16_t *src,
;                         ptrdiff_t dst_stride, ptrdiff_t src_stride,
;                         ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale)
%if ARCH_X86_64
cglobal store_slice2, 7, 9, 0, dst, src, dst_stride, src_stride, width, dither_height, dither, tmp, tmp2
%else
cglobal store_slice2, 0, 7, 0, dst, src, width, dither_height, dither, tmp, tmp2
%define dst_strideq r2m
%define src_strideq r3m
    mov       dstq, dstm
    mov       srcq, srcm
    mov       widthq, r4m
    mov       dither_heightq, r5m
    mov       ditherq, r6m ; log2_scale
%endif
    add       widthq, 7
    mov       tmpq, src_strideq
    and       widthq, ~7
    sub       dst_strideq, widthq
    movd      m5, ditherd ; log2_scale
    xor       ditherq, -1 ; log2_scale
    mov       tmp2q, tmpq
    add       ditherq, 7 ; log2_scale
    sub       tmp2q, widthq
    movd      m2, ditherd ; log2_scale
    add       tmp2q, tmp2q
    lea       ditherq, [pb_dither]
    mov       src_strideq, tmp2q
    shl       tmpq, 5
    lea       dither_heightq, [ditherq+dither_heightq*8]
    pxor      m7, m7

.loop_height:
    movq      m3, [ditherq]
    movq      m4, m3
    punpcklbw m3, m7
    punpckhbw m4, m7
    mov       tmp2q,widthq
    psraw     m3, m5
    psraw     m4, m5

.loop_width:
    movq      m0, [srcq]
    movq      m1, [srcq+8]
    paddw     m0, m3
    paddw     m0, [srcq+tmpq]
    paddw     m1, m4
    movq      m6, [srcq+tmpq+8]
    movq      [srcq+tmpq], m7
    psraw     m0, m2
    paddw     m1, m6
    movq      [srcq+tmpq+8], m7
    psraw     m1, m2
    packuswb  m0, m1
    movq      [dstq], m0
    add       srcq, 16
    add       dstq, 8
    sub       tmp2q, 8
    jg .loop_width

    add       srcq, src_strideq
    add       ditherq, 8
    add       dstq, dst_strideq
    cmp       ditherq, dither_heightq
    jl .loop_height
    RET

;void ff_mul_thrmat_mmx(int16_t *thr_adr_noq, int16_t *thr_adr, int q);
cglobal mul_thrmat, 3, 3, 0, thrn, thr, q
    movd      m7, qd
    movq      m0, [thrnq]
    punpcklwd m7, m7
    movq      m1, [thrnq+8]
    punpckldq m7, m7
    pmullw    m0, m7
    movq      m2, [thrnq+8*2]
    pmullw    m1, m7
    movq      m3, [thrnq+8*3]
    pmullw    m2, m7
    movq      [thrq], m0
    movq      m4, [thrnq+8*4]
    pmullw    m3, m7
    movq      [thrq+8], m1
    movq      m5, [thrnq+8*5]
    pmullw    m4, m7
    movq      [thrq+8*2], m2
    movq      m6, [thrnq+8*6]
    pmullw    m5, m7
    movq      [thrq+8*3], m3
    movq      m0, [thrnq+8*7]
    pmullw    m6, m7
    movq      [thrq+8*4], m4
    movq      m1, [thrnq+8*7+8]
    pmullw    m0, m7
    movq      [thrq+8*5], m5
    movq      m2, [thrnq+8*7+8*2]
    pmullw    m1, m7
    movq      [thrq+8*6], m6
    movq      m3, [thrnq+8*7+8*3]
    pmullw    m2, m7
    movq      [thrq+8*7], m0
    movq      m4, [thrnq+8*7+8*4]
    pmullw    m3, m7
    movq      [thrq+8*7+8], m1
    movq      m5, [thrnq+8*7+8*5]
    pmullw    m4, m7
    movq      [thrq+8*7+8*2], m2
    movq      m6, [thrnq+8*7+8*6]
    pmullw    m5, m7
    movq      [thrq+8*7+8*3], m3
    movq      m0, [thrnq+14*8]
    pmullw    m6, m7
    movq      [thrq+8*7+8*4], m4
    movq      m1, [thrnq+14*8+8]
    pmullw    m0, m7
    movq      [thrq+8*7+8*5], m5
    pmullw    m1, m7
    movq      [thrq+8*7+8*6], m6
    movq      [thrq+14*8], m0
    movq      [thrq+14*8+8], m1
    RET

%macro COLUMN_FDCT 1-3 0, 0
    movq      m1, [srcq+DCTSIZE*0*2]
    movq      m7, [srcq+DCTSIZE*3*2]
    movq      m0, m1
    paddw     m1, [srcq+DCTSIZE*7*2]
    movq      m3, m7
    paddw     m7, [srcq+DCTSIZE*4*2]
    movq      m5, m1
    movq      m6, [srcq+DCTSIZE*1*2]
    psubw     m1, m7
    movq      m2, [srcq+DCTSIZE*2*2]
    movq      m4, m6
    paddw     m6, [srcq+DCTSIZE*6*2]
    paddw     m5, m7
    paddw     m2, [srcq+DCTSIZE*5*2]
    movq      m7, m6
    paddw     m6, m2
    psubw     m7, m2
    movq      m2, m5
    paddw     m5, m6
    psubw     m2, m6
    paddw     m7, m1
    movq      m6, [thrq+4*16+%2]
    psllw     m7, 2
    psubw     m5, [thrq+%2]
    psubw     m2, m6
    paddusw   m5, [thrq+%2]
    paddusw   m2, m6
    pmulhw    m7, [pw_2D41]
    paddw     m5, [thrq+%2]
    paddw     m2, m6
    psubusw   m5, [thrq+%2]
    psubusw   m2, m6
    paddw     m5, [pw_2]
    movq      m6, m2
    paddw     m2, m5
    psubw     m5, m6
    movq      m6, m1
    paddw     m1, m7
    psubw     m1, [thrq+2*16+%2]
    psubw     m6, m7
    movq      m7, [thrq+6*16+%2]
    psraw     m5, 2
    paddusw   m1, [thrq+2*16+%2]
    psubw     m6, m7
    paddw     m1, [thrq+2*16+%2]
    paddusw   m6, m7
    psubusw   m1, [thrq+2*16+%2]
    paddw     m6, m7
    psubw     m3, [srcq+DCTSIZE*4*2]
    psubusw   m6, m7
    movq      m7, m1
    psraw     m2, 2
    psubw     m4, [srcq+DCTSIZE*6*2]
    psubw     m1, m6
    psubw     m0, [srcq+DCTSIZE*7*2]
    paddw     m6, m7
    psraw     m6, 2
    movq      m7, m2
    pmulhw    m1, [pw_5A82]
    paddw     m2, m6
    movq      [rsp], m2
    psubw     m7, m6
    movq      m2, [srcq+DCTSIZE*2*2]
    psubw     m1, m6
    psubw     m2, [srcq+DCTSIZE*5*2]
    movq      m6, m5
    movq      [rsp+8*3], m7
    paddw     m3, m2
    paddw     m2, m4
    paddw     m4, m0
    movq      m7, m3
    psubw     m3, m4
    psllw     m3, 2
    psllw     m7, 2
    pmulhw    m3, [pw_187E]
    psllw     m4, 2
    pmulhw    m7, [pw_22A3]
    psllw     m2, 2
    pmulhw    m4, [pw_539F]
    paddw     m5, m1
    pmulhw    m2, [pw_2D41]
    psubw     m6, m1
    paddw     m7, m3
    movq      [rsp+8], m5
    paddw     m4, m3
    movq      m3, [thrq+3*16+%2]
    movq      m1, m0
    movq      [rsp+8*2], m6
    psubw     m1, m2
    paddw     m0, m2
    movq      m5, m1
    movq      m2, [thrq+5*16+%2]
    psubw     m1, m7
    paddw     m5, m7
    psubw     m1, m3
    movq      m7, [thrq+16+%2]
    psubw     m5, m2
    movq      m6, m0
    paddw     m0, m4
    paddusw   m1, m3
    psubw     m6, m4
    movq      m4, [thrq+7*16+%2]
    psubw     m0, m7
    psubw     m6, m4
    paddusw   m5, m2
    paddusw   m6, m4
    paddw     m1, m3
    paddw     m5, m2
    paddw     m6, m4
    psubusw   m1, m3
    psubusw   m5, m2
    psubusw   m6, m4
    movq      m4, m1
    por       m4, m5
    paddusw   m0, m7
    por       m4, m6
    paddw     m0, m7
    packssdw  m4, m4
    psubusw   m0, m7
    movd      tmpd, m4
    or        tmpd, tmpd
    jnz %1
    movq      m4, [rsp]
    movq      m1, m0
    pmulhw    m0, [pw_3642]
    movq      m2, m1
    movq      m5, [outq+DCTSIZE*0*2]
    movq      m3, m2
    pmulhw    m1, [pw_2441]
    paddw     m5, m4
    movq      m6, [rsp+8]
    psraw     m3, 2
    pmulhw    m2, [pw_0CBB]
    psubw     m4, m3
    movq      m7, [outq+DCTSIZE*1*2]
    paddw     m5, m3
    movq      [outq+DCTSIZE*7*2], m4
    paddw     m7, m6
    movq      m3, [rsp+8*2]
    psubw     m6, m0
    movq      m4, [outq+DCTSIZE*2*2]
    paddw     m7, m0
    movq      [outq], m5
    paddw     m4, m3
    movq      [outq+DCTSIZE*6*2], m6
    psubw     m3, m1
    movq      m5, [outq+DCTSIZE*5*2]
    paddw     m4, m1
    movq      m6, [outq+DCTSIZE*3*2]
    paddw     m5, m3
    movq      m0, [rsp+8*3]
    add       srcq, 8+%3
    movq      [outq+DCTSIZE*1*2], m7
    paddw     m6, m0
    movq      [outq+DCTSIZE*2*2], m4
    psubw     m0, m2
    movq      m7, [outq+DCTSIZE*4*2]
    paddw     m6, m2
    movq      [outq+DCTSIZE*5*2], m5
    paddw     m7, m0
    movq      [outq+DCTSIZE*3*2], m6
    movq      [outq+DCTSIZE*4*2], m7
    add       outq, 8+%3
%endmacro

%macro COLUMN_IDCT 0-1 0
    movq      m3, m5
    psubw     m5, m1
    psllw     m5, 1
    paddw     m3, m1
    movq      m2, m0
    psubw     m0, m6
    movq      m1, m5
    psllw     m0, 1
    pmulhw    m1, [pw_AC62]
    paddw     m5, m0
    pmulhw    m5, [pw_3B21]
    paddw     m2, m6
    pmulhw    m0, [pw_22A3]
    movq      m7, m2
    movq      m4, [rsp]
    psubw     m2, m3
    psllw     m2, 1
    paddw     m7, m3
    pmulhw    m2, [pw_2D41]
    movq      m6, m4
    psraw     m7, 2
    paddw     m4, [outq]
    psubw     m6, m7
    movq      m3, [rsp+8]
    paddw     m4, m7
    movq      [outq+DCTSIZE*7*2], m6
    paddw     m1, m5
    movq      [outq], m4
    psubw     m1, m7
    movq      m7, [rsp+8*2]
    psubw     m0, m5
    movq      m6, [rsp+8*3]
    movq      m5, m3
    paddw     m3, [outq+DCTSIZE*1*2]
    psubw     m5, m1
    psubw     m2, m1
    paddw     m3, m1
    movq      [outq+DCTSIZE*6*2], m5
    movq      m4, m7
    paddw     m7, [outq+DCTSIZE*2*2]
    psubw     m4, m2
    paddw     m4, [outq+DCTSIZE*5*2]
    paddw     m7, m2
    movq      [outq+DCTSIZE*1*2], m3
    paddw     m0, m2
    movq      [outq+DCTSIZE*2*2], m7
    movq      m1, m6
    paddw     m6, [outq+DCTSIZE*4*2]
    psubw     m1, m0
    paddw     m1, [outq+DCTSIZE*3*2]
    paddw     m6, m0
    movq      [outq+DCTSIZE*5*2], m4
    add       srcq, 8+%1
    movq      [outq+DCTSIZE*4*2], m6
    movq      [outq+DCTSIZE*3*2], m1
    add       outq, 8+%1
%endmacro

;void ff_column_fidct_mmx(int16_t *thr_adr, int16_t *data, int16_t *output, int cnt);
cglobal column_fidct, 4, 5, 0, 32, thr, src, out, cnt, tmp
.fdct1:
    COLUMN_FDCT .idct1
    jmp .fdct2

.idct1:
    COLUMN_IDCT

.fdct2:
    COLUMN_FDCT .idct2, 8, 16
    sub    cntd, 2
    jg .fdct1
    RET

.idct2:
    COLUMN_IDCT 16
    sub    cntd, 2
    jg .fdct1
    RET

;void ff_row_idct_mmx(int16_t *workspace, int16_t *output_adr, ptrdiff_t output_stride, int cnt);
cglobal row_idct, 4, 5, 0, 16, src, dst, stride, cnt, stride3
    add       strideq, strideq
    lea       stride3q, [strideq+strideq*2]
.loop:
    movq      m0, [srcq+DCTSIZE*0*2]
    movq      m1, [srcq+DCTSIZE*1*2]
    movq      m4, m0
    movq      m2, [srcq+DCTSIZE*2*2]
    punpcklwd m0, m1
    movq      m3, [srcq+DCTSIZE*3*2]
    punpckhwd m4, m1
    movq      m7, m2
    punpcklwd m2, m3
    movq      m6, m0
    punpckldq m0, m2
    punpckhdq m6, m2
    movq      m5, m0
    punpckhwd m7, m3
    psubw     m0, m6
    pmulhw    m0, [pw_5A82]
    movq      m2, m4
    punpckldq m4, m7
    paddw     m5, m6
    punpckhdq m2, m7
    movq      m1, m4
    psllw     m0, 2
    paddw     m4, m2
    movq      m3, [srcq+DCTSIZE*0*2+8]
    psubw     m1, m2
    movq      m2, [srcq+DCTSIZE*1*2+8]
    psubw     m0, m5
    movq      m6, m4
    paddw     m4, m5
    psubw     m6, m5
    movq      m7, m1
    movq      m5, [srcq+DCTSIZE*2*2+8]
    paddw     m1, m0
    movq      [rsp], m4
    movq      m4, m3
    movq      [rsp+8], m6
    punpcklwd m3, m2
    movq      m6, [srcq+DCTSIZE*3*2+8]
    punpckhwd m4, m2
    movq      m2, m5
    punpcklwd m5, m6
    psubw     m7, m0
    punpckhwd m2, m6
    movq      m0, m3
    punpckldq m3, m5
    punpckhdq m0, m5
    movq      m5, m4
    movq      m6, m3
    punpckldq m4, m2
    psubw     m3, m0
    punpckhdq m5, m2
    paddw     m6, m0
    movq      m2, m4
    movq      m0, m3
    psubw     m4, m5
    pmulhw    m0, [pw_AC62]
    paddw     m3, m4
    pmulhw    m3, [pw_3B21]
    paddw     m2, m5
    pmulhw    m4, [pw_22A3]
    movq      m5, m2
    psubw     m2, m6
    paddw     m5, m6
    pmulhw    m2, [pw_2D41]
    paddw     m0, m3
    psllw     m0, 3
    psubw     m4, m3
    movq      m6, [rsp]
    movq      m3, m1
    psllw     m4, 3
    psubw     m0, m5
    psllw     m2, 3
    paddw     m1, m0
    psubw     m2, m0
    psubw     m3, m0
    paddw     m4, m2
    movq      m0, m7
    paddw     m7, m2
    psubw     m0, m2
    movq      m2, [pw_4]
    psubw     m6, m5
    paddw     m5, [rsp]
    paddw     m1, m2
    paddw     m5, m2
    psraw     m1, 3
    paddw     m7, m2
    psraw     m5, 3
    paddw     m5, [dstq]
    psraw     m7, 3
    paddw     m1, [dstq+strideq*1]
    paddw     m0, m2
    paddw     m7, [dstq+strideq*2]
    paddw     m3, m2
    movq      [dstq], m5
    paddw     m6, m2
    movq      [dstq+strideq*1], m1
    psraw     m0, 3
    movq      [dstq+strideq*2], m7
    add       dstq, stride3q
    movq      m5, [rsp+8]
    psraw     m3, 3
    paddw     m0, [dstq+strideq*2]
    psubw     m5, m4
    paddw     m3, [dstq+stride3q*1]
    psraw     m6, 3
    paddw     m4, [rsp+8]
    paddw     m5, m2
    paddw     m6, [dstq+strideq*4]
    paddw     m4, m2
    movq      [dstq+strideq*2], m0
    psraw     m5, 3
    paddw     m5, [dstq]
    psraw     m4, 3
    paddw     m4, [dstq+strideq*1]
    add       srcq, DCTSIZE*2*4
    movq      [dstq+stride3q*1], m3
    movq      [dstq+strideq*4], m6
    movq      [dstq], m5
    movq      [dstq+strideq*1], m4
    sub       dstq, stride3q
    add       dstq, 8
    dec       r3d
    jnz .loop
    RET

;void ff_row_fdct_mmx(int16_t *data, const uint8_t *pixels, ptrdiff_t line_size, int cnt);
cglobal row_fdct, 4, 5, 0, 16, src, pix, stride, cnt, stride3
    lea       stride3q, [strideq+strideq*2]
.loop:
    movd      m0, [pixq]
    pxor      m7, m7
    movd      m1, [pixq+strideq*1]
    punpcklbw m0, m7
    movd      m2, [pixq+strideq*2]
    punpcklbw m1, m7
    punpcklbw m2, m7
    add       pixq,stride3q
    movq      m5, m0
    movd      m3, [pixq+strideq*4]
    movq      m6, m1
    movd      m4, [pixq+stride3q*1]
    punpcklbw m3, m7
    psubw     m5, m3
    punpcklbw m4, m7
    paddw     m0, m3
    psubw     m6, m4
    movd      m3, [pixq+strideq*2]
    paddw     m1, m4
    movq      [rsp], m5
    punpcklbw m3, m7
    movq      [rsp+8], m6
    movq      m4, m2
    movd      m5, [pixq]
    paddw     m2, m3
    movd      m6, [pixq+strideq*1]
    punpcklbw m5, m7
    psubw     m4, m3
    punpcklbw m6, m7
    movq      m3, m5
    paddw     m5, m6
    psubw     m3, m6
    movq      m6, m0
    movq      m7, m1
    psubw     m0, m5
    psubw     m1, m2
    paddw     m7, m2
    paddw     m1, m0
    movq      m2, m7
    psllw     m1, 2
    paddw     m6, m5
    pmulhw    m1, [pw_2D41]
    paddw     m7, m6
    psubw     m6, m2
    movq      m5, m0
    movq      m2, m7
    punpcklwd m7, m6
    paddw     m0, m1
    punpckhwd m2, m6
    psubw     m5, m1
    movq      m6, m0
    movq      m1, [rsp+8]
    punpcklwd m0, m5
    punpckhwd m6, m5
    movq      m5, m0
    punpckldq m0, m7
    paddw     m3, m4
    punpckhdq m5, m7
    movq      m7, m6
    movq      [srcq+DCTSIZE*0*2], m0
    punpckldq m6, m2
    movq      [srcq+DCTSIZE*1*2], m5
    punpckhdq m7, m2
    movq      [srcq+DCTSIZE*2*2], m6
    paddw     m4, m1
    movq      [srcq+DCTSIZE*3*2], m7
    psllw     m3, 2
    movq      m2, [rsp]
    psllw     m4, 2
    pmulhw    m4, [pw_2D41]
    paddw     m1, m2
    psllw     m1, 2
    movq      m0, m3
    pmulhw    m0, [pw_22A3]
    psubw     m3, m1
    pmulhw    m3, [pw_187E]
    movq      m5, m2
    pmulhw    m1, [pw_539F]
    psubw     m2, m4
    paddw     m5, m4
    movq      m6, m2
    paddw     m0, m3
    movq      m7, m5
    paddw     m2, m0
    psubw     m6, m0
    movq      m4, m2
    paddw     m1, m3
    punpcklwd m2, m6
    paddw     m5, m1
    punpckhwd m4, m6
    psubw     m7, m1
    movq      m6, m5
    punpcklwd m5, m7
    punpckhwd m6, m7
    movq      m7, m2
    punpckldq m2, m5
    sub       pixq, stride3q
    punpckhdq m7, m5
    movq      m5, m4
    movq      [srcq+DCTSIZE*0*2+8], m2
    punpckldq m4, m6
    movq      [srcq+DCTSIZE*1*2+8], m7
    punpckhdq m5, m6
    movq      [srcq+DCTSIZE*2*2+8], m4
    add       pixq, 4
    movq      [srcq+DCTSIZE*3*2+8], m5
    add       srcq, DCTSIZE*4*2
    dec       cntd
    jnz .loop
    RET
