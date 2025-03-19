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
#include "libavutil/mem_internal.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/me_cmp.h"
#include "libavcodec/mpegvideoenc.h"

int ff_sum_abs_dctelem_sse2(const int16_t *block);
int ff_sum_abs_dctelem_ssse3(const int16_t *block);
int ff_sse8_mmx(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                ptrdiff_t stride, int h);
int ff_sse16_mmx(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                 ptrdiff_t stride, int h);
int ff_sse16_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                  ptrdiff_t stride, int h);
int ff_hf_noise8_mmx(const uint8_t *pix1, ptrdiff_t stride, int h);
int ff_hf_noise16_mmx(const uint8_t *pix1, ptrdiff_t stride, int h);
int ff_sad8_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                   ptrdiff_t stride, int h);
int ff_sad16_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                    ptrdiff_t stride, int h);
int ff_sad16_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                  ptrdiff_t stride, int h);
int ff_sad8_x2_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                      ptrdiff_t stride, int h);
int ff_sad16_x2_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                       ptrdiff_t stride, int h);
int ff_sad16_x2_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                     ptrdiff_t stride, int h);
int ff_sad8_y2_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                      ptrdiff_t stride, int h);
int ff_sad16_y2_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                       ptrdiff_t stride, int h);
int ff_sad16_y2_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                     ptrdiff_t stride, int h);
int ff_sad8_approx_xy2_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                              ptrdiff_t stride, int h);
int ff_sad16_approx_xy2_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                               ptrdiff_t stride, int h);
int ff_sad16_approx_xy2_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                             ptrdiff_t stride, int h);
int ff_vsad_intra8_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                          ptrdiff_t stride, int h);
int ff_vsad_intra16_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                           ptrdiff_t stride, int h);
int ff_vsad_intra16_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                         ptrdiff_t stride, int h);
int ff_vsad8_approx_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                    ptrdiff_t stride, int h);
int ff_vsad16_approx_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                     ptrdiff_t stride, int h);
int ff_vsad16_approx_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                   ptrdiff_t stride, int h);

#define hadamard_func(cpu)                                                       \
    int ff_hadamard8_diff_ ## cpu(MPVEncContext *s, const uint8_t *src1,         \
                                  const uint8_t *src2, ptrdiff_t stride, int h); \
    int ff_hadamard8_diff16_ ## cpu(MPVEncContext *s, const uint8_t *src1,       \
                                    const uint8_t *src2, ptrdiff_t stride, int h);

hadamard_func(mmxext)
hadamard_func(sse2)
hadamard_func(ssse3)

#if HAVE_X86ASM
static int nsse16_mmx(MPVEncContext *c, const uint8_t *pix1, const uint8_t *pix2,
                      ptrdiff_t stride, int h)
{
    int score1, score2;

    if (c)
        score1 = c->sse_cmp[0](c, pix1, pix2, stride, h);
    else
        score1 = ff_sse16_mmx(c, pix1, pix2, stride, h);
    score2 = ff_hf_noise16_mmx(pix1, stride, h) + ff_hf_noise8_mmx(pix1+8, stride, h)
           - ff_hf_noise16_mmx(pix2, stride, h) - ff_hf_noise8_mmx(pix2+8, stride, h);

    if (c)
        return score1 + FFABS(score2) * c->c.avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

static int nsse8_mmx(MPVEncContext *c, const uint8_t *pix1, const uint8_t *pix2,
                     ptrdiff_t stride, int h)
{
    int score1 = ff_sse8_mmx(c, pix1, pix2, stride, h);
    int score2 = ff_hf_noise8_mmx(pix1, stride, h) -
                 ff_hf_noise8_mmx(pix2, stride, h);

    if (c)
        return score1 + FFABS(score2) * c->c.avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

#endif /* HAVE_X86ASM */

#if HAVE_INLINE_ASM

DECLARE_ASM_CONST(8, uint64_t, round_tab)[3] = {
    0x0000000000000000ULL,
    0x0001000100010001ULL,
    0x0002000200020002ULL,
};

static inline void sad8_4_mmx(const uint8_t *blk1, const uint8_t *blk2,
                              ptrdiff_t stride, int h)
{
    x86_reg len = -stride * h;
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
        "movq %5, %%mm5                 \n\t"
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

#define PIX_SADXY(suf)                                                  \
static int sad8_xy2_ ## suf(MPVEncContext *v, const uint8_t *blk2,      \
                            const uint8_t *blk1, ptrdiff_t stride, int h) \
{                                                                       \
    __asm__ volatile (                                                  \
        "pxor %%mm7, %%mm7     \n\t"                                    \
        "pxor %%mm6, %%mm6     \n\t"                                    \
        ::);                                                            \
                                                                        \
    sad8_4_ ## suf(blk1, blk2, stride, h);                              \
                                                                        \
    return sum_ ## suf();                                               \
}                                                                       \
                                                                        \
static int sad16_xy2_ ## suf(MPVEncContext *v, const uint8_t *blk2,     \
                             const uint8_t *blk1, ptrdiff_t stride, int h) \
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

PIX_SADXY(mmx)

#endif /* HAVE_INLINE_ASM */

av_cold void ff_me_cmp_init_x86(MECmpContext *c, AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_INLINE_ASM
    if (INLINE_MMX(cpu_flags)) {
        c->pix_abs[0][3] = sad16_xy2_mmx;
        c->pix_abs[1][3] = sad8_xy2_mmx;
    }

#endif /* HAVE_INLINE_ASM */

    if (EXTERNAL_MMX(cpu_flags)) {
        c->sse[1]            = ff_sse8_mmx;
#if HAVE_X86ASM
        c->nsse[0]           = nsse16_mmx;
        c->nsse[1]           = nsse8_mmx;
#endif
    }

    if (EXTERNAL_MMXEXT(cpu_flags)) {
#if !HAVE_ALIGNED_STACK
        c->hadamard8_diff[0] = ff_hadamard8_diff16_mmxext;
        c->hadamard8_diff[1] = ff_hadamard8_diff_mmxext;
#endif

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

        if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
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
            if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
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
