/*
 * SIMD-optimized motion estimation
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
#include "libavcodec/me_cmp.h"
#include "libavcodec/mpegvideo.h"

int ff_sum_abs_dctelem_mmx(int16_t *block);
int ff_sum_abs_dctelem_mmxext(int16_t *block);
int ff_sum_abs_dctelem_sse2(int16_t *block);
int ff_sum_abs_dctelem_ssse3(int16_t *block);
int ff_sse8_mmx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                ptrdiff_t stride, int h);
int ff_sse16_mmx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                 ptrdiff_t stride, int h);
int ff_sse16_sse2(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                  ptrdiff_t stride, int h);
int ff_hf_noise8_mmx(uint8_t *pix1, ptrdiff_t stride, int h);
int ff_hf_noise16_mmx(uint8_t *pix1, ptrdiff_t stride, int h);
int ff_sad8_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                   ptrdiff_t stride, int h);
int ff_sad16_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                    ptrdiff_t stride, int h);
int ff_sad16_sse2(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                  ptrdiff_t stride, int h);
int ff_sad8_x2_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                      ptrdiff_t stride, int h);
int ff_sad16_x2_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                       ptrdiff_t stride, int h);
int ff_sad16_x2_sse2(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                     ptrdiff_t stride, int h);
int ff_sad8_y2_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                      ptrdiff_t stride, int h);
int ff_sad16_y2_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                       ptrdiff_t stride, int h);
int ff_sad16_y2_sse2(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                     ptrdiff_t stride, int h);
int ff_sad8_approx_xy2_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                              ptrdiff_t stride, int h);
int ff_sad16_approx_xy2_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                               ptrdiff_t stride, int h);
int ff_sad16_approx_xy2_sse2(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                             ptrdiff_t stride, int h);
int ff_vsad_intra8_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                          ptrdiff_t stride, int h);
int ff_vsad_intra16_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                           ptrdiff_t stride, int h);
int ff_vsad_intra16_sse2(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                         ptrdiff_t stride, int h);
int ff_vsad8_approx_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                    ptrdiff_t stride, int h);
int ff_vsad16_approx_mmxext(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                     ptrdiff_t stride, int h);
int ff_vsad16_approx_sse2(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
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

#if HAVE_YASM
static int nsse16_mmx(MpegEncContext *c, uint8_t *pix1, uint8_t *pix2,
                      ptrdiff_t stride, int h)
{
    int score1, score2;

    if (c)
        score1 = c->mecc.sse[0](c, pix1, pix2, stride, h);
    else
        score1 = ff_sse16_mmx(c, pix1, pix2, stride, h);
    score2 = ff_hf_noise16_mmx(pix1, stride, h) + ff_hf_noise8_mmx(pix1+8, stride, h)
           - ff_hf_noise16_mmx(pix2, stride, h) - ff_hf_noise8_mmx(pix2+8, stride, h);

    if (c)
        return score1 + FFABS(score2) * c->avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

static int nsse8_mmx(MpegEncContext *c, uint8_t *pix1, uint8_t *pix2,
                     ptrdiff_t stride, int h)
{
    int score1 = ff_sse8_mmx(c, pix1, pix2, stride, h);
    int score2 = ff_hf_noise8_mmx(pix1, stride, h) -
                 ff_hf_noise8_mmx(pix2, stride, h);

    if (c)
        return score1 + FFABS(score2) * c->avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

#endif /* HAVE_YASM */

#if HAVE_INLINE_ASM

static int vsad_intra16_mmx(MpegEncContext *v, uint8_t *pix, uint8_t *dummy,
                            ptrdiff_t stride, int h)
{
    int tmp;

    av_assert2((((int) pix) & 7) == 0);
    av_assert2((stride & 7) == 0);

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

static int vsad16_mmx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                      ptrdiff_t stride, int h)
{
    int tmp;

    av_assert2((((int) pix1) & 7) == 0);
    av_assert2((((int) pix2) & 7) == 0);
    av_assert2((stride & 7) == 0);

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

DECLARE_ASM_CONST(8, uint64_t, round_tab)[3] = {
    0x0000000000000000ULL,
    0x0001000100010001ULL,
    0x0002000200020002ULL,
};

static inline void sad8_1_mmx(uint8_t *blk1, uint8_t *blk2,
                              ptrdiff_t stride, int h)
{
    x86_reg len = -stride * h;
    __asm__ volatile (
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1, %%"REG_a"), %%mm0    \n\t"
        "movq (%2, %%"REG_a"), %%mm2    \n\t"
        "movq (%2, %%"REG_a"), %%mm4    \n\t"
        "add %3, %%"REG_a"              \n\t"
        "psubusb %%mm0, %%mm2           \n\t"
        "psubusb %%mm4, %%mm0           \n\t"
        "movq (%1, %%"REG_a"), %%mm1    \n\t"
        "movq (%2, %%"REG_a"), %%mm3    \n\t"
        "movq (%2, %%"REG_a"), %%mm5    \n\t"
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
        "add %3, %%"REG_a"              \n\t"
        " js 1b                         \n\t"
        : "+a" (len)
        : "r" (blk1 - len), "r" (blk2 - len), "r" (stride));
}

static inline void sad8_2_mmx(uint8_t *blk1a, uint8_t *blk1b, uint8_t *blk2,
                              ptrdiff_t stride, int h)
{
    x86_reg len = -stride * h;
    __asm__ volatile (
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq (%1, %%"REG_a"), %%mm0    \n\t"
        "movq (%2, %%"REG_a"), %%mm1    \n\t"
        "movq (%1, %%"REG_a"), %%mm2    \n\t"
        "movq (%2, %%"REG_a"), %%mm3    \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpcklbw %%mm7, %%mm1         \n\t"
        "punpckhbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "paddw %%mm0, %%mm1             \n\t"
        "paddw %%mm2, %%mm3             \n\t"
        "movq (%3, %%"REG_a"), %%mm4    \n\t"
        "movq (%3, %%"REG_a"), %%mm2    \n\t"
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
        "add %4, %%"REG_a"              \n\t"
        " js 1b                         \n\t"
        : "+a" (len)
        : "r" (blk1a - len), "r" (blk1b - len), "r" (blk2 - len),
          "r" (stride));
}

static inline void sad8_4_mmx(uint8_t *blk1, uint8_t *blk2,
                              ptrdiff_t stride, int h)
{
    x86_reg len = -stride * h;
    __asm__ volatile (
        "movq  (%1, %%"REG_a"), %%mm0   \n\t"
        "movq 1(%1, %%"REG_a"), %%mm2   \n\t"
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
        "movq  (%2, %%"REG_a"), %%mm2   \n\t"
        "movq 1(%2, %%"REG_a"), %%mm4   \n\t"
        "movq %%mm2, %%mm3              \n\t"
        "movq %%mm4, %%mm5              \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "punpcklbw %%mm7, %%mm4         \n\t"
        "punpckhbw %%mm7, %%mm5         \n\t"
        "paddw %%mm4, %%mm2             \n\t"
        "paddw %%mm5, %%mm3             \n\t"
        "movq %5, %%mm5                 \n\t"
        "paddw %%mm2, %%mm0             \n\t"
        "paddw %%mm3, %%mm1             \n\t"
        "paddw %%mm5, %%mm0             \n\t"
        "paddw %%mm5, %%mm1             \n\t"
        "movq (%3, %%"REG_a"), %%mm4    \n\t"
        "movq (%3, %%"REG_a"), %%mm5    \n\t"
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
        "add %4, %%"REG_a"              \n\t"
        " js 1b                         \n\t"
        : "+a" (len)
        : "r" (blk1 - len), "r" (blk1 - len + stride), "r" (blk2 - len),
          "r" (stride), "m" (round_tab[2]));
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
    av_assert2(h == 8);                                                     \
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
    av_assert2(h == 8);                                                     \
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
    av_assert2(h == 8);                                                     \
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
    av_assert2(h == 8);                                                     \
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

#endif /* HAVE_INLINE_ASM */

av_cold void ff_me_cmp_init_x86(MECmpContext *c, AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_INLINE_ASM
    if (INLINE_MMX(cpu_flags)) {
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

        c->vsad[4] = vsad_intra16_mmx;

        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->vsad[0] = vsad16_mmx;
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

        c->sad[0] = ff_sad16_mmxext;
        c->sad[1] = ff_sad8_mmxext;

        c->pix_abs[0][0] = ff_sad16_mmxext;
        c->pix_abs[0][1] = ff_sad16_x2_mmxext;
        c->pix_abs[0][2] = ff_sad16_y2_mmxext;
        c->pix_abs[1][0] = ff_sad8_mmxext;
        c->pix_abs[1][1] = ff_sad8_x2_mmxext;
        c->pix_abs[1][2] = ff_sad8_y2_mmxext;

        c->vsad[4] = ff_vsad_intra16_mmxext;
        c->vsad[5] = ff_vsad_intra8_mmxext;

        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->pix_abs[0][3] = ff_sad16_approx_xy2_mmxext;
            c->pix_abs[1][3] = ff_sad8_approx_xy2_mmxext;

            c->vsad[0] = ff_vsad16_approx_mmxext;
            c->vsad[1] = ff_vsad8_approx_mmxext;
        }
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->sse[0] = ff_sse16_sse2;
        c->sum_abs_dctelem   = ff_sum_abs_dctelem_sse2;

#if HAVE_ALIGNED_STACK
        c->hadamard8_diff[0] = ff_hadamard8_diff16_sse2;
        c->hadamard8_diff[1] = ff_hadamard8_diff_sse2;
#endif
        if (!(cpu_flags & AV_CPU_FLAG_SSE2SLOW) && avctx->codec_id != AV_CODEC_ID_SNOW) {
            c->sad[0]        = ff_sad16_sse2;
            c->pix_abs[0][0] = ff_sad16_sse2;
            c->pix_abs[0][1] = ff_sad16_x2_sse2;
            c->pix_abs[0][2] = ff_sad16_y2_sse2;

            c->vsad[4]       = ff_vsad_intra16_sse2;
            if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
                c->pix_abs[0][3] = ff_sad16_approx_xy2_sse2;
                c->vsad[0]       = ff_vsad16_approx_sse2;
            }
        }
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        c->sum_abs_dctelem   = ff_sum_abs_dctelem_ssse3;
#if HAVE_ALIGNED_STACK
        c->hadamard8_diff[0] = ff_hadamard8_diff16_ssse3;
        c->hadamard8_diff[1] = ff_hadamard8_diff_ssse3;
#endif
    }
}
