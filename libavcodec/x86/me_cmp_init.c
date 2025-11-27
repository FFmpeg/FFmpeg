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
#include "libavutil/x86/cpu.h"
#include "libavcodec/me_cmp.h"
#include "libavcodec/mpegvideoenc.h"

int ff_sum_abs_dctelem_sse2(const int16_t *block);
int ff_sum_abs_dctelem_ssse3(const int16_t *block);
int ff_sse8_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                 ptrdiff_t stride, int h);
int ff_sse16_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                  ptrdiff_t stride, int h);
int ff_hf_noise8_ssse3(const uint8_t *pix1, ptrdiff_t stride, int h);
int ff_hf_noise16_ssse3(const uint8_t *pix1, ptrdiff_t stride, int h);
int ff_sad8_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                   ptrdiff_t stride, int h);
int ff_sad16_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                  ptrdiff_t stride, int h);
int ff_sad16u_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                   ptrdiff_t stride, int h);
int ff_sad8_x2_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                      ptrdiff_t stride, int h);
int ff_sad16_x2_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                     ptrdiff_t stride, int h);
int ff_sad8_y2_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                      ptrdiff_t stride, int h);
int ff_sad16_y2_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                     ptrdiff_t stride, int h);
int ff_sad8_approx_xy2_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                              ptrdiff_t stride, int h);
int ff_sad8_xy2_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                     ptrdiff_t stride, int h);
int ff_sad16_approx_xy2_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                             ptrdiff_t stride, int h);
int ff_sad16_xy2_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                      ptrdiff_t stride, int h);
int ff_vsad_intra8_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                          ptrdiff_t stride, int h);
int ff_vsad_intra16_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                         ptrdiff_t stride, int h);
int ff_vsad_intra16u_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                          ptrdiff_t stride, int h);
int ff_vsad8_approx_mmxext(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                    ptrdiff_t stride, int h);
int ff_vsad16_approx_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                   ptrdiff_t stride, int h);
int ff_vsad16u_approx_sse2(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                           ptrdiff_t stride, int h);

#define hadamard_func(cpu)                                                       \
    int ff_hadamard8_diff_ ## cpu(MPVEncContext *s, const uint8_t *src1,         \
                                  const uint8_t *src2, ptrdiff_t stride, int h); \
    int ff_hadamard8_diff16_ ## cpu(MPVEncContext *s, const uint8_t *src1,       \
                                    const uint8_t *src2, ptrdiff_t stride, int h);

hadamard_func(sse2)
hadamard_func(ssse3)

static int nsse16_ssse3(MPVEncContext *c, const uint8_t *pix1, const uint8_t *pix2,
                        ptrdiff_t stride, int h)
{
    int score1 = ff_sse16_sse2(c, pix1, pix2, stride, h);
    int score2 = ff_hf_noise16_ssse3(pix1, stride, h) -
                 ff_hf_noise16_ssse3(pix2, stride, h);

    if (c)
        return score1 + FFABS(score2) * c->c.avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

static int nsse8_ssse3(MPVEncContext *c, const uint8_t *pix1, const uint8_t *pix2,
                       ptrdiff_t stride, int h)
{
    int score1 = ff_sse8_sse2(c, pix1, pix2, stride, h);
    int score2 = ff_hf_noise8_ssse3(pix1, stride, h) -
                 ff_hf_noise8_ssse3(pix2, stride, h);

    if (c)
        return score1 + FFABS(score2) * c->c.avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

av_cold void ff_me_cmp_init_x86(MECmpContext *c, AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        c->sad[1] = ff_sad8_mmxext;

        c->pix_abs[1][0] = ff_sad8_mmxext;
        c->pix_abs[1][1] = ff_sad8_x2_mmxext;
        c->pix_abs[1][2] = ff_sad8_y2_mmxext;

        c->vsad[5] = ff_vsad_intra8_mmxext;

        if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
            c->pix_abs[1][3] = ff_sad8_approx_xy2_mmxext;

            c->vsad[1] = ff_vsad8_approx_mmxext;
        }
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->sse[0] = ff_sse16_sse2;
        c->sse[1]            = ff_sse8_sse2;
        c->sum_abs_dctelem   = ff_sum_abs_dctelem_sse2;

        c->pix_abs[0][0] = ff_sad16_sse2;
        c->pix_abs[0][1] = ff_sad16_x2_sse2;
        c->pix_abs[0][2] = ff_sad16_y2_sse2;
        c->pix_abs[0][3] = ff_sad16_xy2_sse2;

        c->hadamard8_diff[0] = ff_hadamard8_diff16_sse2;
        c->hadamard8_diff[1] = ff_hadamard8_diff_sse2;
        if (avctx->codec_id != AV_CODEC_ID_SNOW) {
            c->sad[0]        = ff_sad16_sse2;

            c->vsad[4]       = ff_vsad_intra16_sse2;
            if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
                c->vsad[0]       = ff_vsad16_approx_sse2;
            }
        } else {
            // Snow does not abide by the alignment requirements
            // of blk1, so we use special versions without them for it.
            c->sad[0]        = ff_sad16u_sse2;

            c->vsad[4]       = ff_vsad_intra16u_sse2;
            if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
                c->vsad[0]       = ff_vsad16u_approx_sse2;
            }
        }
        if (avctx->flags & AV_CODEC_FLAG_BITEXACT) {
            c->pix_abs[1][3] = ff_sad8_xy2_sse2;
        } else {
            c->pix_abs[0][3] = ff_sad16_approx_xy2_sse2;
        }
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        c->nsse[0]           = nsse16_ssse3;
        c->nsse[1]           = nsse8_ssse3;

        c->sum_abs_dctelem   = ff_sum_abs_dctelem_ssse3;
        c->hadamard8_diff[0] = ff_hadamard8_diff16_ssse3;
        c->hadamard8_diff[1] = ff_hadamard8_diff_ssse3;
    }
}
