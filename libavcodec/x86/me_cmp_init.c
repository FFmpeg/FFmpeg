/*
 * SIMD-optimized motion estimation
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/me_cmp.h"
#include "libavcodec/mpegvideo.h"

#if HAVE_INLINE_ASM

static int sse8_mmx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                    ptrdiff_t stride, int h)
{
    int tmp;

    __asm__ volatile (
        "movl         %4, %%ecx          \n"
        "shr          $1, %%ecx          \n"
        "pxor      %%mm0, %%mm0          \n" /* mm0 = 0 */
        "pxor      %%mm7, %%mm7          \n" /* mm7 holds the sum */
        "1:                              \n"
        "movq       (%0), %%mm1          \n" /* mm1 = pix1[0][0 - 7] */
        "movq       (%1), %%mm2          \n" /* mm2 = pix2[0][0 - 7] */
        "movq   (%0, %3), %%mm3          \n" /* mm3 = pix1[1][0 - 7] */
        "movq   (%1, %3), %%mm4          \n" /* mm4 = pix2[1][0 - 7] */

        /* todo: mm1-mm2, mm3-mm4 */
        /* algo: subtract mm1 from mm2 with saturation and vice versa */
        /*       OR the results to get absolute difference */
        "movq      %%mm1, %%mm5          \n"
        "movq      %%mm3, %%mm6          \n"
        "psubusb   %%mm2, %%mm1          \n"
        "psubusb   %%mm4, %%mm3          \n"
        "psubusb   %%mm5, %%mm2          \n"
        "psubusb   %%mm6, %%mm4          \n"

        "por       %%mm1, %%mm2          \n"
        "por       %%mm3, %%mm4          \n"

        /* now convert to 16-bit vectors so we can square them */
        "movq      %%mm2, %%mm1          \n"
        "movq      %%mm4, %%mm3          \n"

        "punpckhbw %%mm0, %%mm2          \n"
        "punpckhbw %%mm0, %%mm4          \n"
        "punpcklbw %%mm0, %%mm1          \n" /* mm1 now spread over (mm1, mm2) */
        "punpcklbw %%mm0, %%mm3          \n" /* mm4 now spread over (mm3, mm4) */

        "pmaddwd   %%mm2, %%mm2          \n"
        "pmaddwd   %%mm4, %%mm4          \n"
        "pmaddwd   %%mm1, %%mm1          \n"
        "pmaddwd   %%mm3, %%mm3          \n"

        "lea (%0, %3, 2), %0             \n" /* pix1 += 2 * stride */
        "lea (%1, %3, 2), %1             \n" /* pix2 += 2 * stride */

        "paddd     %%mm2, %%mm1          \n"
        "paddd     %%mm4, %%mm3          \n"
        "paddd     %%mm1, %%mm7          \n"
        "paddd     %%mm3, %%mm7          \n"

        "decl      %%ecx                 \n"
        "jnz       1b                    \n"

        "movq      %%mm7, %%mm1          \n"
        "psrlq       $32, %%mm7          \n" /* shift hi dword to lo */
        "paddd     %%mm7, %%mm1          \n"
        "movd      %%mm1, %2             \n"
        : "+r" (pix1), "+r" (pix2), "=r" (tmp)
        : "r" (stride), "m" (h)
        : "%ecx");

    return tmp;
}

static int sse16_mmx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                     ptrdiff_t stride, int h)
{
    int tmp;

    __asm__ volatile (
        "movl %4, %%ecx\n"
        "pxor %%mm0, %%mm0\n"    /* mm0 = 0 */
        "pxor %%mm7, %%mm7\n"    /* mm7 holds the sum */
        "1:\n"
        "movq (%0), %%mm1\n"     /* mm1 = pix1[0 -  7] */
        "movq (%1), %%mm2\n"     /* mm2 = pix2[0 -  7] */
        "movq 8(%0), %%mm3\n"    /* mm3 = pix1[8 - 15] */
        "movq 8(%1), %%mm4\n"    /* mm4 = pix2[8 - 15] */

        /* todo: mm1-mm2, mm3-mm4 */
        /* algo: subtract mm1 from mm2 with saturation and vice versa */
        /*       OR the results to get absolute difference */
        "movq %%mm1, %%mm5\n"
        "movq %%mm3, %%mm6\n"
        "psubusb %%mm2, %%mm1\n"
        "psubusb %%mm4, %%mm3\n"
        "psubusb %%mm5, %%mm2\n"
        "psubusb %%mm6, %%mm4\n"

        "por %%mm1, %%mm2\n"
        "por %%mm3, %%mm4\n"

        /* now convert to 16-bit vectors so we can square them */
        "movq %%mm2, %%mm1\n"
        "movq %%mm4, %%mm3\n"

        "punpckhbw %%mm0, %%mm2\n"
        "punpckhbw %%mm0, %%mm4\n"
        "punpcklbw %%mm0, %%mm1\n" /* mm1 now spread over (mm1, mm2) */
        "punpcklbw %%mm0, %%mm3\n" /* mm4 now spread over (mm3, mm4) */

        "pmaddwd %%mm2, %%mm2\n"
        "pmaddwd %%mm4, %%mm4\n"
        "pmaddwd %%mm1, %%mm1\n"
        "pmaddwd %%mm3, %%mm3\n"

        "add %3, %0\n"
        "add %3, %1\n"

        "paddd %%mm2, %%mm1\n"
        "paddd %%mm4, %%mm3\n"
        "paddd %%mm1, %%mm7\n"
        "paddd %%mm3, %%mm7\n"

        "decl %%ecx\n"
        "jnz 1b\n"

        "movq %%mm7, %%mm1\n"
        "psrlq $32, %%mm7\n"    /* shift hi dword to lo */
        "paddd %%mm7, %%mm1\n"
        "movd %%mm1, %2\n"
        : "+r" (pix1), "+r" (pix2), "=r" (tmp)
        : "r" (stride), "m" (h)
        : "%ecx");

    return tmp;
}

static int hf_noise8_mmx(uint8_t *pix1, ptrdiff_t stride, int h)
{
    int tmp;

    __asm__ volatile (
        "movl %3, %%ecx\n"
        "pxor %%mm7, %%mm7\n"
        "pxor %%mm6, %%mm6\n"

        "movq (%0), %%mm0\n"
        "movq %%mm0, %%mm1\n"
        "psllq $8, %%mm0\n"
        "psrlq $8, %%mm1\n"
        "psrlq $8, %%mm0\n"
        "movq %%mm0, %%mm2\n"
        "movq %%mm1, %%mm3\n"
        "punpcklbw %%mm7, %%mm0\n"
        "punpcklbw %%mm7, %%mm1\n"
        "punpckhbw %%mm7, %%mm2\n"
        "punpckhbw %%mm7, %%mm3\n"
        "psubw %%mm1, %%mm0\n"
        "psubw %%mm3, %%mm2\n"

        "add %2, %0\n"

        "movq (%0), %%mm4\n"
        "movq %%mm4, %%mm1\n"
        "psllq $8, %%mm4\n"
        "psrlq $8, %%mm1\n"
        "psrlq $8, %%mm4\n"
        "movq %%mm4, %%mm5\n"
        "movq %%mm1, %%mm3\n"
        "punpcklbw %%mm7, %%mm4\n"
        "punpcklbw %%mm7, %%mm1\n"
        "punpckhbw %%mm7, %%mm5\n"
        "punpckhbw %%mm7, %%mm3\n"
        "psubw %%mm1, %%mm4\n"
        "psubw %%mm3, %%mm5\n"
        "psubw %%mm4, %%mm0\n"
        "psubw %%mm5, %%mm2\n"
        "pxor %%mm3, %%mm3\n"
        "pxor %%mm1, %%mm1\n"
        "pcmpgtw %%mm0, %%mm3\n\t"
        "pcmpgtw %%mm2, %%mm1\n\t"
        "pxor %%mm3, %%mm0\n"
        "pxor %%mm1, %%mm2\n"
        "psubw %%mm3, %%mm0\n"
        "psubw %%mm1, %%mm2\n"
        "paddw %%mm0, %%mm2\n"
        "paddw %%mm2, %%mm6\n"

        "add %2, %0\n"
        "1:\n"

        "movq (%0), %%mm0\n"
        "movq %%mm0, %%mm1\n"
        "psllq $8, %%mm0\n"
        "psrlq $8, %%mm1\n"
        "psrlq $8, %%mm0\n"
        "movq %%mm0, %%mm2\n"
        "movq %%mm1, %%mm3\n"
        "punpcklbw %%mm7, %%mm0\n"
        "punpcklbw %%mm7, %%mm1\n"
        "punpckhbw %%mm7, %%mm2\n"
        "punpckhbw %%mm7, %%mm3\n"
        "psubw %%mm1, %%mm0\n"
        "psubw %%mm3, %%mm2\n"
        "psubw %%mm0, %%mm4\n"
        "psubw %%mm2, %%mm5\n"
        "pxor  %%mm3, %%mm3\n"
        "pxor  %%mm1, %%mm1\n"
        "pcmpgtw %%mm4, %%mm3\n\t"
        "pcmpgtw %%mm5, %%mm1\n\t"
        "pxor  %%mm3, %%mm4\n"
        "pxor  %%mm1, %%mm5\n"
        "psubw %%mm3, %%mm4\n"
        "psubw %%mm1, %%mm5\n"
        "paddw %%mm4, %%mm5\n"
        "paddw %%mm5, %%mm6\n"

        "add %2, %0\n"

        "movq (%0), %%mm4\n"
        "movq      %%mm4, %%mm1\n"
        "psllq $8, %%mm4\n"
        "psrlq $8, %%mm1\n"
        "psrlq $8, %%mm4\n"
        "movq      %%mm4, %%mm5\n"
        "movq      %%mm1, %%mm3\n"
        "punpcklbw %%mm7, %%mm4\n"
        "punpcklbw %%mm7, %%mm1\n"
        "punpckhbw %%mm7, %%mm5\n"
        "punpckhbw %%mm7, %%mm3\n"
        "psubw     %%mm1, %%mm4\n"
        "psubw     %%mm3, %%mm5\n"
        "psubw     %%mm4, %%mm0\n"
        "psubw     %%mm5, %%mm2\n"
        "pxor      %%mm3, %%mm3\n"
        "pxor      %%mm1, %%mm1\n"
        "pcmpgtw   %%mm0, %%mm3\n\t"
        "pcmpgtw   %%mm2, %%mm1\n\t"
        "pxor      %%mm3, %%mm0\n"
        "pxor      %%mm1, %%mm2\n"
        "psubw     %%mm3, %%mm0\n"
        "psubw     %%mm1, %%mm2\n"
        "paddw     %%mm0, %%mm2\n"
        "paddw     %%mm2, %%mm6\n"

        "add  %2, %0\n"
        "subl $2, %%ecx\n"
        " jnz 1b\n"

        "movq      %%mm6, %%mm0\n"
        "punpcklwd %%mm7, %%mm0\n"
        "punpckhwd %%mm7, %%mm6\n"
        "paddd     %%mm0, %%mm6\n"

        "movq  %%mm6, %%mm0\n"
        "psrlq $32,   %%mm6\n"
        "paddd %%mm6, %%mm0\n"
        "movd  %%mm0, %1\n"
        : "+r" (pix1), "=r" (tmp)
        : "r" (stride), "g" (h - 2)
        : "%ecx");

    return tmp;
}

static int hf_noise16_mmx(uint8_t *pix1, ptrdiff_t stride, int h)
{
    int tmp;
    uint8_t *pix = pix1;

    __asm__ volatile (
        "movl %3, %%ecx\n"
        "pxor %%mm7, %%mm7\n"
        "pxor %%mm6, %%mm6\n"

        "movq (%0), %%mm0\n"
        "movq 1(%0), %%mm1\n"
        "movq %%mm0, %%mm2\n"
        "movq %%mm1, %%mm3\n"
        "punpcklbw %%mm7, %%mm0\n"
        "punpcklbw %%mm7, %%mm1\n"
        "punpckhbw %%mm7, %%mm2\n"
        "punpckhbw %%mm7, %%mm3\n"
        "psubw %%mm1, %%mm0\n"
        "psubw %%mm3, %%mm2\n"

        "add %2, %0\n"

        "movq (%0), %%mm4\n"
        "movq 1(%0), %%mm1\n"
        "movq %%mm4, %%mm5\n"
        "movq %%mm1, %%mm3\n"
        "punpcklbw %%mm7, %%mm4\n"
        "punpcklbw %%mm7, %%mm1\n"
        "punpckhbw %%mm7, %%mm5\n"
        "punpckhbw %%mm7, %%mm3\n"
        "psubw %%mm1, %%mm4\n"
        "psubw %%mm3, %%mm5\n"
        "psubw %%mm4, %%mm0\n"
        "psubw %%mm5, %%mm2\n"
        "pxor %%mm3, %%mm3\n"
        "pxor %%mm1, %%mm1\n"
        "pcmpgtw %%mm0, %%mm3\n\t"
        "pcmpgtw %%mm2, %%mm1\n\t"
        "pxor %%mm3, %%mm0\n"
        "pxor %%mm1, %%mm2\n"
        "psubw %%mm3, %%mm0\n"
        "psubw %%mm1, %%mm2\n"
        "paddw %%mm0, %%mm2\n"
        "paddw %%mm2, %%mm6\n"

        "add %2, %0\n"
        "1:\n"

        "movq (%0), %%mm0\n"
        "movq 1(%0), %%mm1\n"
        "movq %%mm0, %%mm2\n"
        "movq %%mm1, %%mm3\n"
        "punpcklbw %%mm7, %%mm0\n"
        "punpcklbw %%mm7, %%mm1\n"
        "punpckhbw %%mm7, %%mm2\n"
        "punpckhbw %%mm7, %%mm3\n"
        "psubw %%mm1, %%mm0\n"
        "psubw %%mm3, %%mm2\n"
        "psubw %%mm0, %%mm4\n"
        "psubw %%mm2, %%mm5\n"
        "pxor %%mm3, %%mm3\n"
        "pxor %%mm1, %%mm1\n"
        "pcmpgtw %%mm4, %%mm3\n\t"
        "pcmpgtw %%mm5, %%mm1\n\t"
        "pxor %%mm3, %%mm4\n"
        "pxor %%mm1, %%mm5\n"
        "psubw %%mm3, %%mm4\n"
        "psubw %%mm1, %%mm5\n"
        "paddw %%mm4, %%mm5\n"
        "paddw %%mm5, %%mm6\n"

        "add %2, %0\n"

        "movq (%0), %%mm4\n"
        "movq 1(%0), %%mm1\n"
        "movq %%mm4, %%mm5\n"
        "movq %%mm1, %%mm3\n"
        "punpcklbw %%mm7, %%mm4\n"
        "punpcklbw %%mm7, %%mm1\n"
        "punpckhbw %%mm7, %%mm5\n"
        "punpckhbw %%mm7, %%mm3\n"
        "psubw %%mm1, %%mm4\n"
        "psubw %%mm3, %%mm5\n"
        "psubw %%mm4, %%mm0\n"
        "psubw %%mm5, %%mm2\n"
        "pxor %%mm3, %%mm3\n"
        "pxor %%mm1, %%mm1\n"
        "pcmpgtw %%mm0, %%mm3\n\t"
        "pcmpgtw %%mm2, %%mm1\n\t"
        "pxor %%mm3, %%mm0\n"
        "pxor %%mm1, %%mm2\n"
        "psubw %%mm3, %%mm0\n"
        "psubw %%mm1, %%mm2\n"
        "paddw %%mm0, %%mm2\n"
        "paddw %%mm2, %%mm6\n"

        "add %2, %0\n"
        "subl $2, %%ecx\n"
        " jnz 1b\n"

        "movq %%mm6, %%mm0\n"
        "punpcklwd %%mm7, %%mm0\n"
        "punpckhwd %%mm7, %%mm6\n"
        "paddd %%mm0, %%mm6\n"

        "movq %%mm6, %%mm0\n"
        "psrlq $32, %%mm6\n"
        "paddd %%mm6, %%mm0\n"
        "movd %%mm0, %1\n"
        : "+r" (pix1), "=r" (tmp)
        : "r" (stride), "g" (h - 2)
        : "%ecx");

    return tmp + hf_noise8_mmx(pix + 8, stride, h);
}

static int nsse16_mmx(MpegEncContext *c, uint8_t *pix1, uint8_t *pix2,
                      ptrdiff_t stride, int h)
{
    int score1, score2;

    if (c)
        score1 = c->mecc.sse[0](c, pix1, pix2, stride, h);
    else
        score1 = sse16_mmx(c, pix1, pix2, stride, h);
    score2 = hf_noise16_mmx(pix1, stride, h) -
             hf_noise16_mmx(pix2, stride, h);

    if (c)
        return score1 + FFABS(score2) * c->avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

static int nsse8_mmx(MpegEncContext *c, uint8_t *pix1, uint8_t *pix2,
                     ptrdiff_t stride, int h)
{
    int score1 = sse8_mmx(c, pix1, pix2, stride, h);
    int score2 = hf_noise8_mmx(pix1, stride, h) -
                 hf_noise8_mmx(pix2, stride, h);

    if (c)
        return score1 + FFABS(score2) * c->avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

static int vsad_intra16_mmx(MpegEncContext *v, uint8_t *pix, uint8_t *dummy,
                            ptrdiff_t stride, int h)
{
    int tmp;

    assert((((int) pix) & 7) == 0);
    assert((stride & 7) == 0);

#define SUM(in0, in1, out0, out1)               \
    "movq (%0), %%mm2\n"                        \
    "movq 8(%0), %%mm3\n"                       \
    "add %2,%0\n"                               \
    "movq %%mm2, " #out0 "\n"                   \
    "movq %%mm3, " #out1 "\n"                   \
    "psubusb " #in0 ", %%mm2\n"                 \
    "psubusb " #in1 ", %%mm3\n"                 \
    "psubusb " #out0 ", " #in0 "\n"             \
    "psubusb " #out1 ", " #in1 "\n"             \
    "por %%mm2, " #in0 "\n"                     \
    "por %%mm3, " #in1 "\n"                     \
    "movq " #in0 ", %%mm2\n"                    \
    "movq " #in1 ", %%mm3\n"                    \
    "punpcklbw %%mm7, " #in0 "\n"               \
    "punpcklbw %%mm7, " #in1 "\n"               \
    "punpckhbw %%mm7, %%mm2\n"                  \
    "punpckhbw %%mm7, %%mm3\n"                  \
    "paddw " #in1 ", " #in0 "\n"                \
    "paddw %%mm3, %%mm2\n"                      \
    "paddw %%mm2, " #in0 "\n"                   \
    "paddw " #in0 ", %%mm6\n"


    __asm__ volatile (
        "movl    %3, %%ecx\n"
        "pxor %%mm6, %%mm6\n"
        "pxor %%mm7, %%mm7\n"
        "movq  (%0), %%mm0\n"
        "movq 8(%0), %%mm1\n"
        "add %2, %0\n"
        "jmp 2f\n"
        "1:\n"

        SUM(%%mm4, %%mm5, %%mm0, %%mm1)
        "2:\n"
        SUM(%%mm0, %%mm1, %%mm4, %%mm5)

        "subl $2, %%ecx\n"
        "jnz 1b\n"

        "movq  %%mm6, %%mm0\n"
        "psrlq $32,   %%mm6\n"
        "paddw %%mm6, %%mm0\n"
        "movq  %%mm0, %%mm6\n"
        "psrlq $16,   %%mm0\n"
        "paddw %%mm6, %%mm0\n"
        "movd  %%mm0, %1\n"
        : "+r" (pix), "=r" (tmp)
        : "r" (stride), "m" (h)
        : "%ecx");

    return tmp & 0xFFFF;
}
#undef SUM

static int vsad_intra16_mmxext(MpegEncContext *v, uint8_t *pix, uint8_t *dummy,
                               ptrdiff_t stride, int h)
{
    int tmp;

    assert((((int) pix) & 7) == 0);
    assert((stride & 7) == 0);

#define SUM(in0, in1, out0, out1)               \
    "movq (%0), " #out0 "\n"                    \
    "movq 8(%0), " #out1 "\n"                   \
    "add %2, %0\n"                              \
    "psadbw " #out0 ", " #in0 "\n"              \
    "psadbw " #out1 ", " #in1 "\n"              \
    "paddw " #in1 ", " #in0 "\n"                \
    "paddw " #in0 ", %%mm6\n"

    __asm__ volatile (
        "movl %3, %%ecx\n"
        "pxor %%mm6, %%mm6\n"
        "pxor %%mm7, %%mm7\n"
        "movq (%0), %%mm0\n"
        "movq 8(%0), %%mm1\n"
        "add %2, %0\n"
        "jmp 2f\n"
        "1:\n"

        SUM(%%mm4, %%mm5, %%mm0, %%mm1)
        "2:\n"
        SUM(%%mm0, %%mm1, %%mm4, %%mm5)

        "subl $2, %%ecx\n"
        "jnz 1b\n"

        "movd %%mm6, %1\n"
        : "+r" (pix), "=r" (tmp)
        : "r" (stride), "m" (h)
        : "%ecx");

    return tmp;
}
#undef SUM

static int vsad16_mmx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                      ptrdiff_t stride, int h)
{
    int tmp;

    assert((((int) pix1) & 7) == 0);
    assert((((int) pix2) & 7) == 0);
    assert((stride & 7) == 0);

#define SUM(in0, in1, out0, out1)       \
    "movq (%0), %%mm2\n"                \
    "movq (%1), " #out0 "\n"            \
    "movq 8(%0), %%mm3\n"               \
    "movq 8(%1), " #out1 "\n"           \
    "add %3, %0\n"                      \
    "add %3, %1\n"                      \
    "psubb " #out0 ", %%mm2\n"          \
    "psubb " #out1 ", %%mm3\n"          \
    "pxor %%mm7, %%mm2\n"               \
    "pxor %%mm7, %%mm3\n"               \
    "movq %%mm2, " #out0 "\n"           \
    "movq %%mm3, " #out1 "\n"           \
    "psubusb " #in0 ", %%mm2\n"         \
    "psubusb " #in1 ", %%mm3\n"         \
    "psubusb " #out0 ", " #in0 "\n"     \
    "psubusb " #out1 ", " #in1 "\n"     \
    "por %%mm2, " #in0 "\n"             \
    "por %%mm3, " #in1 "\n"             \
    "movq " #in0 ", %%mm2\n"            \
    "movq " #in1 ", %%mm3\n"            \
    "punpcklbw %%mm7, " #in0 "\n"       \
    "punpcklbw %%mm7, " #in1 "\n"       \
    "punpckhbw %%mm7, %%mm2\n"          \
    "punpckhbw %%mm7, %%mm3\n"          \
    "paddw " #in1 ", " #in0 "\n"        \
    "paddw %%mm3, %%mm2\n"              \
    "paddw %%mm2, " #in0 "\n"           \
    "paddw " #in0 ", %%mm6\n"


    __asm__ volatile (
        "movl %4, %%ecx\n"
        "pxor %%mm6, %%mm6\n"
        "pcmpeqw %%mm7, %%mm7\n"
        "psllw $15, %%mm7\n"
        "packsswb %%mm7, %%mm7\n"
        "movq (%0), %%mm0\n"
        "movq (%1), %%mm2\n"
        "movq 8(%0), %%mm1\n"
        "movq 8(%1), %%mm3\n"
        "add %3, %0\n"
        "add %3, %1\n"
        "psubb %%mm2, %%mm0\n"
        "psubb %%mm3, %%mm1\n"
        "pxor %%mm7, %%mm0\n"
        "pxor %%mm7, %%mm1\n"
        "jmp 2f\n"
        "1:\n"

        SUM(%%mm4, %%mm5, %%mm0, %%mm1)
        "2:\n"
        SUM(%%mm0, %%mm1, %%mm4, %%mm5)

        "subl $2, %%ecx\n"
        "jnz 1b\n"

        "movq %%mm6, %%mm0\n"
        "psrlq $32, %%mm6\n"
        "paddw %%mm6, %%mm0\n"
        "movq %%mm0, %%mm6\n"
        "psrlq $16, %%mm0\n"
        "paddw %%mm6, %%mm0\n"
        "movd %%mm0, %2\n"
        : "+r" (pix1), "+r" (pix2), "=r" (tmp)
        : "r" (stride), "m" (h)
        : "%ecx");

    return tmp & 0x7FFF;
}
#undef SUM

static int vsad16_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                         ptrdiff_t stride, int h)
{
    int tmp;

    assert((((int) pix1) & 7) == 0);
    assert((((int) pix2) & 7) == 0);
    assert((stride & 7) == 0);

#define SUM(in0, in1, out0, out1)               \
    "movq (%0), " #out0 "\n"                    \
    "movq (%1), %%mm2\n"                        \
    "movq 8(%0), " #out1 "\n"                   \
    "movq 8(%1), %%mm3\n"                       \
    "add %3, %0\n"                              \
    "add %3, %1\n"                              \
    "psubb %%mm2, " #out0 "\n"                  \
    "psubb %%mm3, " #out1 "\n"                  \
    "pxor %%mm7, " #out0 "\n"                   \
    "pxor %%mm7, " #out1 "\n"                   \
    "psadbw " #out0 ", " #in0 "\n"              \
    "psadbw " #out1 ", " #in1 "\n"              \
    "paddw " #in1 ", " #in0 "\n"                \
    "paddw " #in0 ", %%mm6\n    "

    __asm__ volatile (
        "movl %4, %%ecx\n"
        "pxor %%mm6, %%mm6\n"
        "pcmpeqw %%mm7, %%mm7\n"
        "psllw $15, %%mm7\n"
        "packsswb %%mm7, %%mm7\n"
        "movq (%0), %%mm0\n"
        "movq (%1), %%mm2\n"
        "movq 8(%0), %%mm1\n"
        "movq 8(%1), %%mm3\n"
        "add %3, %0\n"
        "add %3, %1\n"
        "psubb %%mm2, %%mm0\n"
        "psubb %%mm3, %%mm1\n"
        "pxor %%mm7, %%mm0\n"
        "pxor %%mm7, %%mm1\n"
        "jmp 2f\n"
        "1:\n"

        SUM(%%mm4, %%mm5, %%mm0, %%mm1)
        "2:\n"
        SUM(%%mm0, %%mm1, %%mm4, %%mm5)

        "subl $2, %%ecx\n"
        "jnz 1b\n"

        "movd %%mm6, %2\n"
        : "+r" (pix1), "+r" (pix2), "=r" (tmp)
        : "r" (stride), "m" (h)
        : "%ecx");

    return tmp;
}
#undef SUM

#define MMABS_MMX(a,z)                          \
    "pxor "    #z ", " #z "             \n\t"   \
    "pcmpgtw " #a ", " #z "             \n\t"   \
    "pxor "    #z ", " #a "             \n\t"   \
    "psubw "   #z ", " #a "             \n\t"

#define MMABS_MMXEXT(a, z)                      \
    "pxor "    #z ", " #z "             \n\t"   \
    "psubw "   #a ", " #z "             \n\t"   \
    "pmaxsw "  #z ", " #a "             \n\t"

#define MMABS_SSSE3(a,z)                        \
    "pabsw "   #a ", " #a "             \n\t"

#define MMABS_SUM(a,z, sum)                     \
    MMABS(a,z)                                  \
    "paddusw " #a ", " #sum "           \n\t"

/* FIXME: HSUM_* saturates at 64k, while an 8x8 hadamard or dct block can get
 * up to about 100k on extreme inputs. But that's very unlikely to occur in
 * natural video, and it's even more unlikely to not have any alternative
 * mvs/modes with lower cost. */
#define HSUM_MMX(a, t, dst)                     \
    "movq    " #a ", " #t "             \n\t"   \
    "psrlq      $32, " #a "             \n\t"   \
    "paddusw " #t ", " #a "             \n\t"   \
    "movq    " #a ", " #t "             \n\t"   \
    "psrlq      $16, " #a "             \n\t"   \
    "paddusw " #t ", " #a "             \n\t"   \
    "movd    " #a ", " #dst "           \n\t"   \

#define HSUM_MMXEXT(a, t, dst)                  \
    "pshufw   $0x0E, " #a ", " #t "     \n\t"   \
    "paddusw " #t ", " #a "             \n\t"   \
    "pshufw   $0x01, " #a ", " #t "     \n\t"   \
    "paddusw " #t ", " #a "             \n\t"   \
    "movd    " #a ", " #dst "           \n\t"   \

#define HSUM_SSE2(a, t, dst)                    \
    "movhlps " #a ", " #t "             \n\t"   \
    "paddusw " #t ", " #a "             \n\t"   \
    "pshuflw  $0x0E, " #a ", " #t "     \n\t"   \
    "paddusw " #t ", " #a "             \n\t"   \
    "pshuflw  $0x01, " #a ", " #t "     \n\t"   \
    "paddusw " #t ", " #a "             \n\t"   \
    "movd    " #a ", " #dst "           \n\t"   \

#define DCT_SAD4(m, mm, o)                      \
    "mov"#m" "#o" +  0(%1), " #mm "2    \n\t"   \
    "mov"#m" "#o" + 16(%1), " #mm "3    \n\t"   \
    "mov"#m" "#o" + 32(%1), " #mm "4    \n\t"   \
    "mov"#m" "#o" + 48(%1), " #mm "5    \n\t"   \
    MMABS_SUM(mm ## 2, mm ## 6, mm ## 0)        \
    MMABS_SUM(mm ## 3, mm ## 7, mm ## 1)        \
    MMABS_SUM(mm ## 4, mm ## 6, mm ## 0)        \
    MMABS_SUM(mm ## 5, mm ## 7, mm ## 1)        \

#define DCT_SAD_MMX                             \
    "pxor    %%mm0, %%mm0               \n\t"   \
    "pxor    %%mm1, %%mm1               \n\t"   \
    DCT_SAD4(q, %%mm, 0)                        \
    DCT_SAD4(q, %%mm, 8)                        \
    DCT_SAD4(q, %%mm, 64)                       \
    DCT_SAD4(q, %%mm, 72)                       \
    "paddusw %%mm1, %%mm0               \n\t"   \
    HSUM(%%mm0, %%mm1, %0)

#define DCT_SAD_SSE2                            \
    "pxor    %%xmm0, %%xmm0             \n\t"   \
    "pxor    %%xmm1, %%xmm1             \n\t"   \
    DCT_SAD4(dqa, %%xmm, 0)                     \
    DCT_SAD4(dqa, %%xmm, 64)                    \
    "paddusw %%xmm1, %%xmm0             \n\t"   \
    HSUM(%%xmm0, %%xmm1, %0)

#define DCT_SAD_FUNC(cpu)                           \
static int sum_abs_dctelem_ ## cpu(int16_t *block)  \
{                                                   \
    int sum;                                        \
    __asm__ volatile (                              \
        DCT_SAD                                     \
        :"=r"(sum)                                  \
        :"r"(block));                               \
    return sum & 0xFFFF;                            \
}

#define DCT_SAD         DCT_SAD_MMX
#define HSUM(a, t, dst) HSUM_MMX(a, t, dst)
#define MMABS(a, z)     MMABS_MMX(a, z)
DCT_SAD_FUNC(mmx)
#undef MMABS
#undef HSUM

#define HSUM(a, t, dst) HSUM_MMXEXT(a, t, dst)
#define MMABS(a, z)     MMABS_MMXEXT(a, z)
DCT_SAD_FUNC(mmxext)
#undef HSUM
#undef DCT_SAD

#define DCT_SAD         DCT_SAD_SSE2
#define HSUM(a, t, dst) HSUM_SSE2(a, t, dst)
DCT_SAD_FUNC(sse2)
#undef MMABS

#if HAVE_SSSE3_INLINE
#define MMABS(a, z)     MMABS_SSSE3(a, z)
DCT_SAD_FUNC(ssse3)
#undef MMABS
#endif
#undef HSUM
#undef DCT_SAD


DECLARE_ASM_CONST(8, uint64_t, round_tab)[3] = {
    0x0000000000000000ULL,
    0x0001000100010001ULL,
    0x0002000200020002ULL,
};

DECLARE_ASM_CONST(8, uint64_t, bone) = 0x0101010101010101LL;

static inline void sad8_1_mmx(uint8_t *blk1, uint8_t *blk2,
                              ptrdiff_t stride, int h)
{
    x86_reg len = -(stride * h);
    __asm__ volatile (
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1, %%"FF_REG_a"), %%mm0 \n\t"
        "movq (%2, %%"FF_REG_a"), %%mm2 \n\t"
        "movq (%2, %%"FF_REG_a"), %%mm4 \n\t"
        "add %3, %%"FF_REG_a"           \n\t"
        "psubusb %%mm0, %%mm2           \n\t"
        "psubusb %%mm4, %%mm0           \n\t"
        "movq (%1, %%"FF_REG_a"), %%mm1 \n\t"
        "movq (%2, %%"FF_REG_a"), %%mm3 \n\t"
        "movq (%2, %%"FF_REG_a"), %%mm5 \n\t"
        "psubusb %%mm1, %%mm3           \n\t"
        "psubusb %%mm5, %%mm1           \n\t"
        "por %%mm2, %%mm0               \n\t"
        "por %%mm1, %%mm3               \n\t"
        "movq %%mm0, %%mm1              \n\t"
        "movq %%mm3, %%mm2              \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpcklbw %%mm7, %%mm3         \n\t"
        "punpckhbw %%mm7, %%mm2         \n\t"
        "paddw %%mm1, %%mm0             \n\t"
        "paddw %%mm3, %%mm2             \n\t"
        "paddw %%mm2, %%mm0             \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "add %3, %%"FF_REG_a"           \n\t"
        " js 1b                         \n\t"
        : "+a" (len)
        : "r" (blk1 - len), "r" (blk2 - len), "r" (stride));
}

static inline void sad8_1_mmxext(uint8_t *blk1, uint8_t *blk2,
                                 ptrdiff_t stride, int h)
{
    __asm__ volatile (
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1), %%mm0               \n\t"
        "movq (%1, %3), %%mm1           \n\t"
        "psadbw (%2), %%mm0             \n\t"
        "psadbw (%2, %3), %%mm1         \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "paddw %%mm1, %%mm6             \n\t"
        "lea (%1,%3,2), %1              \n\t"
        "lea (%2,%3,2), %2              \n\t"
        "sub $2, %0                     \n\t"
        " jg 1b                         \n\t"
        : "+r" (h), "+r" (blk1), "+r" (blk2)
        : "r" (stride));
}

static int sad16_sse2(MpegEncContext *v, uint8_t *blk2, uint8_t *blk1,
                      ptrdiff_t stride, int h)
{
    int ret;
    __asm__ volatile (
        "pxor %%xmm2, %%xmm2            \n\t"
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movdqu (%1), %%xmm0            \n\t"
        "movdqu (%1, %4), %%xmm1        \n\t"
        "psadbw (%2), %%xmm0            \n\t"
        "psadbw (%2, %4), %%xmm1        \n\t"
        "paddw %%xmm0, %%xmm2           \n\t"
        "paddw %%xmm1, %%xmm2           \n\t"
        "lea (%1,%4,2), %1              \n\t"
        "lea (%2,%4,2), %2              \n\t"
        "sub $2, %0                     \n\t"
        " jg 1b                         \n\t"
        "movhlps %%xmm2, %%xmm0         \n\t"
        "paddw   %%xmm0, %%xmm2         \n\t"
        "movd    %%xmm2, %3             \n\t"
        : "+r" (h), "+r" (blk1), "+r" (blk2), "=r" (ret)
        : "r" (stride));
    return ret;
}

static inline void sad8_x2a_mmxext(uint8_t *blk1, uint8_t *blk2,
                                   ptrdiff_t stride, int h)
{
    __asm__ volatile (
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1), %%mm0               \n\t"
        "movq (%1, %3), %%mm1           \n\t"
        "pavgb 1(%1), %%mm0             \n\t"
        "pavgb 1(%1, %3), %%mm1         \n\t"
        "psadbw (%2), %%mm0             \n\t"
        "psadbw (%2, %3), %%mm1         \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "paddw %%mm1, %%mm6             \n\t"
        "lea (%1,%3,2), %1              \n\t"
        "lea (%2,%3,2), %2              \n\t"
        "sub $2, %0                     \n\t"
        " jg 1b                         \n\t"
        : "+r" (h), "+r" (blk1), "+r" (blk2)
        : "r" (stride));
}

static inline void sad8_y2a_mmxext(uint8_t *blk1, uint8_t *blk2,
                                   ptrdiff_t stride, int h)
{
    __asm__ volatile (
        "movq (%1), %%mm0               \n\t"
        "add %3, %1                     \n\t"
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1), %%mm1               \n\t"
        "movq (%1, %3), %%mm2           \n\t"
        "pavgb %%mm1, %%mm0             \n\t"
        "pavgb %%mm2, %%mm1             \n\t"
        "psadbw (%2), %%mm0             \n\t"
        "psadbw (%2, %3), %%mm1         \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "paddw %%mm1, %%mm6             \n\t"
        "movq %%mm2, %%mm0              \n\t"
        "lea (%1,%3,2), %1              \n\t"
        "lea (%2,%3,2), %2              \n\t"
        "sub $2, %0                     \n\t"
        " jg 1b                         \n\t"
        : "+r" (h), "+r" (blk1), "+r" (blk2)
        : "r" (stride));
}

static inline void sad8_4_mmxext(uint8_t *blk1, uint8_t *blk2,
                                 ptrdiff_t stride, int h)
{
    __asm__ volatile (
        "movq "MANGLE(bone)", %%mm5     \n\t"
        "movq (%1), %%mm0               \n\t"
        "pavgb 1(%1), %%mm0             \n\t"
        "add %3, %1                     \n\t"
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1), %%mm1               \n\t"
        "movq (%1,%3), %%mm2            \n\t"
        "pavgb 1(%1), %%mm1             \n\t"
        "pavgb 1(%1,%3), %%mm2          \n\t"
        "psubusb %%mm5, %%mm1           \n\t"
        "pavgb %%mm1, %%mm0             \n\t"
        "pavgb %%mm2, %%mm1             \n\t"
        "psadbw (%2), %%mm0             \n\t"
        "psadbw (%2,%3), %%mm1          \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "paddw %%mm1, %%mm6             \n\t"
        "movq %%mm2, %%mm0              \n\t"
        "lea (%1,%3,2), %1              \n\t"
        "lea (%2,%3,2), %2              \n\t"
        "sub $2, %0                     \n\t"
        " jg 1b                         \n\t"
        : "+r" (h), "+r" (blk1), "+r" (blk2)
        : "r" (stride));
}

static inline void sad8_2_mmx(uint8_t *blk1a, uint8_t *blk1b, uint8_t *blk2,
                              ptrdiff_t stride, int h)
{
    x86_reg len = -(stride * h);
    __asm__ volatile (
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1, %%"FF_REG_a"), %%mm0 \n\t"
        "movq (%2, %%"FF_REG_a"), %%mm1 \n\t"
        "movq (%1, %%"FF_REG_a"), %%mm2 \n\t"
        "movq (%2, %%"FF_REG_a"), %%mm3 \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpcklbw %%mm7, %%mm1         \n\t"
        "punpckhbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "paddw %%mm0, %%mm1             \n\t"
        "paddw %%mm2, %%mm3             \n\t"
        "movq (%3, %%"FF_REG_a"), %%mm4 \n\t"
        "movq (%3, %%"FF_REG_a"), %%mm2 \n\t"
        "paddw %%mm5, %%mm1             \n\t"
        "paddw %%mm5, %%mm3             \n\t"
        "psrlw $1, %%mm1                \n\t"
        "psrlw $1, %%mm3                \n\t"
        "packuswb %%mm3, %%mm1          \n\t"
        "psubusb %%mm1, %%mm4           \n\t"
        "psubusb %%mm2, %%mm1           \n\t"
        "por %%mm4, %%mm1               \n\t"
        "movq %%mm1, %%mm0              \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "paddw %%mm1, %%mm0             \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "add %4, %%"FF_REG_a"           \n\t"
        " js 1b                         \n\t"
        : "+a" (len)
        : "r" (blk1a - len), "r" (blk1b - len), "r" (blk2 - len),
          "r" (stride));
}

static inline void sad8_4_mmx(uint8_t *blk1, uint8_t *blk2,
                              ptrdiff_t stride, int h)
{
    x86_reg len = -(stride * h);
    __asm__ volatile (
        "movq  (%1, %%"FF_REG_a"), %%mm0\n\t"
        "movq 1(%1, %%"FF_REG_a"), %%mm2\n\t"
        "movq %%mm0, %%mm1              \n\t"
        "movq %%mm2, %%mm3              \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "paddw %%mm2, %%mm0             \n\t"
        "paddw %%mm3, %%mm1             \n\t"
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq  (%2, %%"FF_REG_a"), %%mm2\n\t"
        "movq 1(%2, %%"FF_REG_a"), %%mm4\n\t"
        "movq %%mm2, %%mm3              \n\t"
        "movq %%mm4, %%mm5              \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "punpcklbw %%mm7, %%mm4         \n\t"
        "punpckhbw %%mm7, %%mm5         \n\t"
        "paddw %%mm4, %%mm2             \n\t"
        "paddw %%mm5, %%mm3             \n\t"
        "movq 16+"MANGLE(round_tab)", %%mm5 \n\t"
        "paddw %%mm2, %%mm0             \n\t"
        "paddw %%mm3, %%mm1             \n\t"
        "paddw %%mm5, %%mm0             \n\t"
        "paddw %%mm5, %%mm1             \n\t"
        "movq (%3, %%"FF_REG_a"), %%mm4 \n\t"
        "movq (%3, %%"FF_REG_a"), %%mm5 \n\t"
        "psrlw $2, %%mm0                \n\t"
        "psrlw $2, %%mm1                \n\t"
        "packuswb %%mm1, %%mm0          \n\t"
        "psubusb %%mm0, %%mm4           \n\t"
        "psubusb %%mm5, %%mm0           \n\t"
        "por %%mm4, %%mm0               \n\t"
        "movq %%mm0, %%mm4              \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpckhbw %%mm7, %%mm4         \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "paddw %%mm4, %%mm6             \n\t"
        "movq  %%mm2, %%mm0             \n\t"
        "movq  %%mm3, %%mm1             \n\t"
        "add %4, %%"FF_REG_a"           \n\t"
        " js 1b                         \n\t"
        : "+a" (len)
        : "r" (blk1 - len), "r" (blk1 - len + stride), "r" (blk2 - len),
          "r" (stride));
}

static inline int sum_mmx(void)
{
    int ret;
    __asm__ volatile (
        "movq %%mm6, %%mm0              \n\t"
        "psrlq $32, %%mm6               \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "movq %%mm6, %%mm0              \n\t"
        "psrlq $16, %%mm6               \n\t"
        "paddw %%mm0, %%mm6             \n\t"
        "movd %%mm6, %0                 \n\t"
        : "=r" (ret));
    return ret & 0xFFFF;
}

static inline int sum_mmxext(void)
{
    int ret;
    __asm__ volatile (
        "movd %%mm6, %0                 \n\t"
        : "=r" (ret));
    return ret;
}

static inline void sad8_x2a_mmx(uint8_t *blk1, uint8_t *blk2,
                                ptrdiff_t stride, int h)
{
    sad8_2_mmx(blk1, blk1 + 1, blk2, stride, h);
}

static inline void sad8_y2a_mmx(uint8_t *blk1, uint8_t *blk2,
                                ptrdiff_t stride, int h)
{
    sad8_2_mmx(blk1, blk1 + stride, blk2, stride, h);
}

#define PIX_SAD(suf)                                                    \
static int sad8_ ## suf(MpegEncContext *v, uint8_t *blk2,               \
                        uint8_t *blk1, ptrdiff_t stride, int h)         \
{                                                                       \
    assert(h == 8);                                                     \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        :);                                                             \
                                                                        \
    sad8_1_ ## suf(blk1, blk2, stride, 8);                              \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad8_x2_ ## suf(MpegEncContext *v, uint8_t *blk2,            \
                           uint8_t *blk1, ptrdiff_t stride, int h)      \
{                                                                       \
    assert(h == 8);                                                     \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        "movq %0, %%mm5        \n\t"                                    \
        :: "m" (round_tab[1]));                                         \
                                                                        \
    sad8_x2a_ ## suf(blk1, blk2, stride, 8);                            \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad8_y2_ ## suf(MpegEncContext *v, uint8_t *blk2,            \
                           uint8_t *blk1, ptrdiff_t stride, int h)      \
{                                                                       \
    assert(h == 8);                                                     \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        "movq %0, %%mm5        \n\t"                                    \
        :: "m" (round_tab[1]));                                         \
                                                                        \
    sad8_y2a_ ## suf(blk1, blk2, stride, 8);                            \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad8_xy2_ ## suf(MpegEncContext *v, uint8_t *blk2,           \
                            uint8_t *blk1, ptrdiff_t stride, int h)     \
{                                                                       \
    assert(h == 8);                                                     \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        ::);                                                            \
                                                                        \
    sad8_4_ ## suf(blk1, blk2, stride, 8);                              \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad16_ ## suf(MpegEncContext *v, uint8_t *blk2,              \
                         uint8_t *blk1, ptrdiff_t stride, int h)        \
{                                                                       \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        :);                                                             \
                                                                        \
    sad8_1_ ## suf(blk1,     blk2,     stride, h);                      \
    sad8_1_ ## suf(blk1 + 8, blk2 + 8, stride, h);                      \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad16_x2_ ## suf(MpegEncContext *v, uint8_t *blk2,           \
                            uint8_t *blk1, ptrdiff_t stride, int h)     \
{                                                                       \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        "movq %0, %%mm5        \n\t"                                    \
        :: "m" (round_tab[1]));                                         \
                                                                        \
    sad8_x2a_ ## suf(blk1,     blk2,     stride, h);                    \
    sad8_x2a_ ## suf(blk1 + 8, blk2 + 8, stride, h);                    \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad16_y2_ ## suf(MpegEncContext *v, uint8_t *blk2,           \
                            uint8_t *blk1, ptrdiff_t stride, int h)     \
{                                                                       \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        "movq %0, %%mm5        \n\t"                                    \
        :: "m" (round_tab[1]));                                         \
                                                                        \
    sad8_y2a_ ## suf(blk1,     blk2,     stride, h);                    \
    sad8_y2a_ ## suf(blk1 + 8, blk2 + 8, stride, h);                    \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad16_xy2_ ## suf(MpegEncContext *v, uint8_t *blk2,          \
                             uint8_t *blk1, ptrdiff_t stride, int h)    \
{                                                                       \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        ::);                                                            \
                                                                        \
    sad8_4_ ## suf(blk1,     blk2,     stride, h);                      \
    sad8_4_ ## suf(blk1 + 8, blk2 + 8, stride, h);                      \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \

PIX_SAD(mmx)
PIX_SAD(mmxext)

#endif /* HAVE_INLINE_ASM */

int ff_sse16_sse2(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                  ptrdiff_t stride, int h);

#define hadamard_func(cpu)                                                    \
    int ff_hadamard8_diff_ ## cpu(MpegEncContext *s, uint8_t *src1,           \
                                  uint8_t *src2, ptrdiff_t stride, int h);    \
    int ff_hadamard8_diff16_ ## cpu(MpegEncContext *s, uint8_t *src1,         \
                                    uint8_t *src2, ptrdiff_t stride, int h);

hadamard_func(mmx)
hadamard_func(mmxext)
hadamard_func(sse2)
hadamard_func(ssse3)

av_cold void ff_me_cmp_init_x86(MECmpContext *c, AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_INLINE_ASM
    if (INLINE_MMX(cpu_flags)) {
        c->sum_abs_dctelem = sum_abs_dctelem_mmx;

        c->pix_abs[0][0] = sad16_mmx;
        c->pix_abs[0][1] = sad16_x2_mmx;
        c->pix_abs[0][2] = sad16_y2_mmx;
        c->pix_abs[0][3] = sad16_xy2_mmx;
        c->pix_abs[1][0] = sad8_mmx;
        c->pix_abs[1][1] = sad8_x2_mmx;
        c->pix_abs[1][2] = sad8_y2_mmx;
        c->pix_abs[1][3] = sad8_xy2_mmx;

        c->sad[0] = sad16_mmx;
        c->sad[1] = sad8_mmx;

        c->sse[0]  = sse16_mmx;
        c->sse[1]  = sse8_mmx;
        c->vsad[4] = vsad_intra16_mmx;

        c->nsse[0] = nsse16_mmx;
        c->nsse[1] = nsse8_mmx;

        if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
            c->vsad[0] = vsad16_mmx;
        }
    }

    if (INLINE_MMXEXT(cpu_flags)) {
        c->sum_abs_dctelem = sum_abs_dctelem_mmxext;

        c->vsad[4] = vsad_intra16_mmxext;

        c->pix_abs[0][0] = sad16_mmxext;
        c->pix_abs[1][0] = sad8_mmxext;

        c->sad[0] = sad16_mmxext;
        c->sad[1] = sad8_mmxext;

        if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
            c->pix_abs[0][1] = sad16_x2_mmxext;
            c->pix_abs[0][2] = sad16_y2_mmxext;
            c->pix_abs[0][3] = sad16_xy2_mmxext;
            c->pix_abs[1][1] = sad8_x2_mmxext;
            c->pix_abs[1][2] = sad8_y2_mmxext;
            c->pix_abs[1][3] = sad8_xy2_mmxext;

            c->vsad[0] = vsad16_mmxext;
        }
    }

    if (INLINE_SSE2(cpu_flags)) {
        c->sum_abs_dctelem = sum_abs_dctelem_sse2;
    }

    if (INLINE_SSE2(cpu_flags) && !(cpu_flags & AV_CPU_FLAG_3DNOW)) {
        c->sad[0] = sad16_sse2;
    }

#if HAVE_SSSE3_INLINE
    if (INLINE_SSSE3(cpu_flags)) {
        c->sum_abs_dctelem = sum_abs_dctelem_ssse3;
    }
#endif
#endif /* HAVE_INLINE_ASM */

    if (EXTERNAL_MMX(cpu_flags)) {
        c->hadamard8_diff[0] = ff_hadamard8_diff16_mmx;
        c->hadamard8_diff[1] = ff_hadamard8_diff_mmx;
    }

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        c->hadamard8_diff[0] = ff_hadamard8_diff16_mmxext;
        c->hadamard8_diff[1] = ff_hadamard8_diff_mmxext;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->sse[0] = ff_sse16_sse2;

#if HAVE_ALIGNED_STACK
        c->hadamard8_diff[0] = ff_hadamard8_diff16_sse2;
        c->hadamard8_diff[1] = ff_hadamard8_diff_sse2;
#endif
    }

    if (EXTERNAL_SSSE3(cpu_flags) && HAVE_ALIGNED_STACK) {
        c->hadamard8_diff[0] = ff_hadamard8_diff16_ssse3;
        c->hadamard8_diff[1] = ff_hadamard8_diff_ssse3;
    }
}
