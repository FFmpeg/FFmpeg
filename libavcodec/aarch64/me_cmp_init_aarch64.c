/*
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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/mpegvideoenc.h"

int ff_pix_abs16_neon(MPVEncContext *s, const uint8_t *blk1, const uint8_t *blk2,
                      ptrdiff_t stride, int h);
int ff_pix_abs16_xy2_neon(MPVEncContext *s, const uint8_t *blk1, const uint8_t *blk2,
                          ptrdiff_t stride, int h);
int ff_pix_abs16_x2_neon(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                         ptrdiff_t stride, int h);
int ff_pix_abs16_y2_neon(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                         ptrdiff_t stride, int h);
int ff_pix_abs8_neon(MPVEncContext *s, const uint8_t *blk1, const uint8_t *blk2,
                     ptrdiff_t stride, int h);

int sse16_neon(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
               ptrdiff_t stride, int h);
int sse8_neon(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
              ptrdiff_t stride, int h);
int sse4_neon(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
              ptrdiff_t stride, int h);

int vsad16_neon(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2,
                ptrdiff_t stride, int h);
int vsad_intra16_neon(MPVEncContext *c, const uint8_t *s, const uint8_t *dummy,
                      ptrdiff_t stride, int h) ;
int vsad_intra8_neon(MPVEncContext *c, const uint8_t *s, const uint8_t *dummy,
                     ptrdiff_t stride, int h) ;
int vsse16_neon(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2,
                ptrdiff_t stride, int h);
int vsse_intra16_neon(MPVEncContext *c, const uint8_t *s, const uint8_t *dummy,
                      ptrdiff_t stride, int h);
int nsse16_neon(int multiplier, const uint8_t *s, const uint8_t *s2,
                ptrdiff_t stride, int h);
int nsse16_neon_wrapper(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2,
                        ptrdiff_t stride, int h);
int pix_median_abs16_neon(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                          ptrdiff_t stride, int h);
int pix_median_abs8_neon(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                         ptrdiff_t stride, int h);
int ff_pix_abs8_x2_neon(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                        ptrdiff_t stride, int h);
int ff_pix_abs8_y2_neon(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                        ptrdiff_t stride, int h);
int ff_pix_abs8_xy2_neon(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                         ptrdiff_t stride, int h);

int nsse8_neon(int multiplier, const uint8_t *s, const uint8_t *s2,
               ptrdiff_t stride, int h);
int nsse8_neon_wrapper(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2,
                       ptrdiff_t stride, int h);

int vsse8_neon(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2,
               ptrdiff_t stride, int h);

int vsse_intra8_neon(MPVEncContext *c, const uint8_t *s, const uint8_t *dummy,
                     ptrdiff_t stride, int h);

#if HAVE_DOTPROD
int sse16_neon_dotprod(MPVEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                       ptrdiff_t stride, int h);
int vsse_intra16_neon_dotprod(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2,
                              ptrdiff_t stride, int h);
#endif

av_cold void ff_me_cmp_init_aarch64(MECmpContext *c, AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        c->pix_abs[0][0] = ff_pix_abs16_neon;
        c->pix_abs[0][1] = ff_pix_abs16_x2_neon;
        c->pix_abs[0][2] = ff_pix_abs16_y2_neon;
        c->pix_abs[0][3] = ff_pix_abs16_xy2_neon;
        c->pix_abs[1][0] = ff_pix_abs8_neon;
        c->pix_abs[1][1] = ff_pix_abs8_x2_neon;
        c->pix_abs[1][2] = ff_pix_abs8_y2_neon;
        c->pix_abs[1][3] = ff_pix_abs8_xy2_neon;

        c->sad[0] = ff_pix_abs16_neon;
        c->sad[1] = ff_pix_abs8_neon;
        c->sse[0] = sse16_neon;
        c->sse[1] = sse8_neon;
        c->sse[2] = sse4_neon;

        c->vsad[0] = vsad16_neon;
        c->vsad[4] = vsad_intra16_neon;
        c->vsad[5] = vsad_intra8_neon;

        c->vsse[0] = vsse16_neon;
        c->vsse[1] = vsse8_neon;

        c->vsse[4] = vsse_intra16_neon;
        c->vsse[5] = vsse_intra8_neon;

        c->nsse[0] = nsse16_neon_wrapper;
        c->nsse[1] = nsse8_neon_wrapper;

        c->median_sad[0] = pix_median_abs16_neon;
        c->median_sad[1] = pix_median_abs8_neon;
    }

#if HAVE_DOTPROD
    if (have_dotprod(cpu_flags)) {
        c->sse[0] = sse16_neon_dotprod;
        c->vsse[4] = vsse_intra16_neon_dotprod;
    }
#endif
}

int nsse16_neon_wrapper(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2,
                        ptrdiff_t stride, int h)
{
    if (c)
        return nsse16_neon(c->c.avctx->nsse_weight, s1, s2, stride, h);
    else
        return nsse16_neon(8, s1, s2, stride, h);
}

int nsse8_neon_wrapper(MPVEncContext *c, const uint8_t *s1, const uint8_t *s2,
                       ptrdiff_t stride, int h)
{
    if (c)
        return nsse8_neon(c->c.avctx->nsse_weight, s1, s2, stride, h);
    else
        return nsse8_neon(8, s1, s2, stride, h);
}
