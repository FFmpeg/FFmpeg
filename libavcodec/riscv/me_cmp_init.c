/*
 * Copyright (c) 2024 Institue of Software Chinese Academy of Sciences (ISCAS).
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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/riscv/cpu.h"
#include "libavcodec/me_cmp.h"
#include "libavcodec/mpegvideoenc.h"

int ff_pix_abs16_rvv(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                              ptrdiff_t stride, int h);
int ff_pix_abs8_rvv(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                             ptrdiff_t stride, int h);
int ff_pix_abs16_x2_rvv(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                         ptrdiff_t stride, int h);
int ff_pix_abs8_x2_rvv(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                         ptrdiff_t stride, int h);
int ff_pix_abs16_y2_rvv(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                          ptrdiff_t stride, int h);
int ff_pix_abs8_y2_rvv(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                          ptrdiff_t stride, int h);

int ff_sse16_rvv(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                   ptrdiff_t stride, int h);
int ff_sse8_rvv(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                   ptrdiff_t stride, int h);
int ff_sse4_rvv(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                   ptrdiff_t stride, int h);

int ff_vsse16_rvv(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2, ptrdiff_t stride, int h);
int ff_vsse8_rvv(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2, ptrdiff_t stride, int h);
int ff_vsse_intra16_rvv(MPVEncContext *c, const uint8_t *s, const uint8_t *dummy, ptrdiff_t stride, int h);
int ff_vsse_intra8_rvv(MPVEncContext *c, const uint8_t *s, const uint8_t *dummy, ptrdiff_t stride, int h);
int ff_vsad16_rvv(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2, ptrdiff_t stride, int h);
int ff_vsad8_rvv(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2, ptrdiff_t stride, int h);
int ff_vsad_intra16_rvv(MPVEncContext *c, const uint8_t *s, const uint8_t *dummy, ptrdiff_t stride, int h);
int ff_vsad_intra8_rvv(MPVEncContext *c, const uint8_t *s, const uint8_t *dummy, ptrdiff_t stride, int h);
int ff_nsse16_rvv(int multiplier, const uint8_t *s1, const uint8_t *s2,
                    ptrdiff_t stride, int h);
int ff_nsse8_rvv(int multiplier, const uint8_t *s1, const uint8_t *s2,
                    ptrdiff_t stride, int h);

static int nsse16_rvv_wrapper(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2,
                        ptrdiff_t stride, int h)
{
    if (c)
        return ff_nsse16_rvv(c->c.avctx->nsse_weight, s1, s2, stride, h);
    else
        return ff_nsse16_rvv(8, s1, s2, stride, h);
}

static int nsse8_rvv_wrapper(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2,
                        ptrdiff_t stride, int h)
{
    if (c)
        return ff_nsse8_rvv(c->c.avctx->nsse_weight, s1, s2, stride, h);
    else
        return ff_nsse8_rvv(8, s1, s2, stride, h);
}

av_cold void ff_me_cmp_init_riscv(MECmpContext *c, AVCodecContext *avctx)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_I32 && ff_rv_vlen_least(128)) {
        c->pix_abs[0][0] = ff_pix_abs16_rvv;
        c->sad[0] = ff_pix_abs16_rvv;
        c->pix_abs[1][0] = ff_pix_abs8_rvv;
        c->sad[1] = ff_pix_abs8_rvv;
        c->pix_abs[0][1] = ff_pix_abs16_x2_rvv;
        c->pix_abs[1][1] = ff_pix_abs8_x2_rvv;
        c->pix_abs[0][2] = ff_pix_abs16_y2_rvv;
        c->pix_abs[1][2] = ff_pix_abs8_y2_rvv;

        c->sse[0] = ff_sse16_rvv;
        c->sse[1] = ff_sse8_rvv;
        c->sse[2] = ff_sse4_rvv;

        c->vsse[0] = ff_vsse16_rvv;
        c->vsse[1] = ff_vsse8_rvv;
        c->vsse[4] = ff_vsse_intra16_rvv;
        c->vsse[5] = ff_vsse_intra8_rvv;
        c->vsad[0] = ff_vsad16_rvv;
        c->vsad[1] = ff_vsad8_rvv;
        c->vsad[4] = ff_vsad_intra16_rvv;
        c->vsad[5] = ff_vsad_intra8_rvv;

        c->nsse[0] = nsse16_rvv_wrapper;
        c->nsse[1] = nsse8_rvv_wrapper;
    }
#endif
}
