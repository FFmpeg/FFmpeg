/*
 * MMX optimized DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/mpegvideo.h"
#include "dsputil_x86.h"

int ff_sum_abs_dctelem_mmx(int16_t *block);
int ff_sum_abs_dctelem_mmxext(int16_t *block);
int ff_sum_abs_dctelem_sse2(int16_t *block);
int ff_sum_abs_dctelem_ssse3(int16_t *block);
int ff_sse8_mmx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                int line_size, int h);
int ff_sse16_mmx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                 int line_size, int h);
int ff_sse16_sse2(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                  int line_size, int h);
int ff_hf_noise8_mmx(uint8_t *pix1, int lsize, int h);
int ff_hf_noise16_mmx(uint8_t *pix1, int lsize, int h);

#define hadamard_func(cpu)                                              \
    int ff_hadamard8_diff_ ## cpu(MpegEncContext *s, uint8_t *src1,     \
                                  uint8_t *src2, int stride, int h);    \
    int ff_hadamard8_diff16_ ## cpu(MpegEncContext *s, uint8_t *src1,   \
                                    uint8_t *src2, int stride, int h);

hadamard_func(mmx)
hadamard_func(mmxext)
hadamard_func(sse2)
hadamard_func(ssse3)

#if HAVE_YASM
static int nsse16_mmx(MpegEncContext *c, uint8_t *pix1, uint8_t *pix2,
                      int line_size, int h)
{
    int score1, score2;

    if (c)
        score1 = c->dsp.sse[0](c, pix1, pix2, line_size, h);
    else
        score1 = ff_sse16_mmx(c, pix1, pix2, line_size, h);
    score2 = ff_hf_noise16_mmx(pix1, line_size, h) + ff_hf_noise8_mmx(pix1+8, line_size, h)
           - ff_hf_noise16_mmx(pix2, line_size, h) - ff_hf_noise8_mmx(pix2+8, line_size, h);

    if (c)
        return score1 + FFABS(score2) * c->avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

static int nsse8_mmx(MpegEncContext *c, uint8_t *pix1, uint8_t *pix2,
                     int line_size, int h)
{
    int score1 = ff_sse8_mmx(c, pix1, pix2, line_size, h);
    int score2 = ff_hf_noise8_mmx(pix1, line_size, h) -
                 ff_hf_noise8_mmx(pix2, line_size, h);

    if (c)
        return score1 + FFABS(score2) * c->avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

#endif /* HAVE_YASM */

#if HAVE_INLINE_ASM

static int vsad_intra16_mmx(MpegEncContext *v, uint8_t *pix, uint8_t *dummy,
                            int line_size, int h)
{
    int tmp;

    av_assert2((((int) pix) & 7) == 0);
    av_assert2((line_size & 7) == 0);

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
        : "r" ((x86_reg) line_size), "m" (h)
        : "%ecx");

    return tmp & 0xFFFF;
}
#undef SUM

static int vsad_intra16_mmxext(MpegEncContext *v, uint8_t *pix, uint8_t *dummy,
                               int line_size, int h)
{
    int tmp;

    av_assert2((((int) pix) & 7) == 0);
    av_assert2((line_size & 7) == 0);

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
        : "r" ((x86_reg) line_size), "m" (h)
        : "%ecx");

    return tmp;
}
#undef SUM

static int vsad16_mmx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                      int line_size, int h)
{
    int tmp;

    av_assert2((((int) pix1) & 7) == 0);
    av_assert2((((int) pix2) & 7) == 0);
    av_assert2((line_size & 7) == 0);

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
        : "r" ((x86_reg) line_size), "m" (h)
        : "%ecx");

    return tmp & 0x7FFF;
}
#undef SUM

static int vsad16_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                         int line_size, int h)
{
    int tmp;

    av_assert2((((int) pix1) & 7) == 0);
    av_assert2((((int) pix2) & 7) == 0);
    av_assert2((line_size & 7) == 0);

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
        : "r" ((x86_reg) line_size), "m" (h)
        : "%ecx");

    return tmp;
}
#undef SUM


#endif /* HAVE_INLINE_ASM */

av_cold void ff_dsputil_init_x86(DSPContext *c, AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_INLINE_ASM
    if (INLINE_MMX(cpu_flags)) {
        c->vsad[4] = vsad_intra16_mmx;

        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->vsad[0]      = vsad16_mmx;
        }
    }

    if (INLINE_MMXEXT(cpu_flags)) {
        c->vsad[4]         = vsad_intra16_mmxext;

        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->vsad[0] = vsad16_mmxext;
        }
    }
#endif /* HAVE_INLINE_ASM */

    if (EXTERNAL_MMX(cpu_flags)) {
        c->hadamard8_diff[0] = ff_hadamard8_diff16_mmx;
        c->hadamard8_diff[1] = ff_hadamard8_diff_mmx;
        c->sum_abs_dctelem   = ff_sum_abs_dctelem_mmx;
        c->sse[0]            = ff_sse16_mmx;
        c->sse[1]            = ff_sse8_mmx;
#if HAVE_YASM
        c->nsse[0]           = nsse16_mmx;
        c->nsse[1]           = nsse8_mmx;
#endif
    }

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        c->hadamard8_diff[0] = ff_hadamard8_diff16_mmxext;
        c->hadamard8_diff[1] = ff_hadamard8_diff_mmxext;
        c->sum_abs_dctelem   = ff_sum_abs_dctelem_mmxext;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->sse[0] = ff_sse16_sse2;
        c->sum_abs_dctelem   = ff_sum_abs_dctelem_sse2;

#if HAVE_ALIGNED_STACK
        c->hadamard8_diff[0] = ff_hadamard8_diff16_sse2;
        c->hadamard8_diff[1] = ff_hadamard8_diff_sse2;
#endif
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        c->sum_abs_dctelem   = ff_sum_abs_dctelem_ssse3;
#if HAVE_ALIGNED_STACK
        c->hadamard8_diff[0] = ff_hadamard8_diff16_ssse3;
        c->hadamard8_diff[1] = ff_hadamard8_diff_ssse3;
#endif
    }

    ff_dsputil_init_pix_mmx(c, avctx);
}
