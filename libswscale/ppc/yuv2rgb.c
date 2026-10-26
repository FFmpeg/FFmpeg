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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"

#include "libswscale/swscale_internal.h"

#if HAVE_ALTIVEC
av_cold int ff_sws_init_altivec_bufs(SwsInternal *c)
{
    const SwsContext *const sws = &c->opts;

    c->vYCoeffsBank = av_malloc_array(sws->dst_h, c->vLumFilterSize * sizeof(*c->vYCoeffsBank));
    c->vCCoeffsBank = av_malloc_array(c->chrDstH, c->vChrFilterSize * sizeof(*c->vCCoeffsBank));
    if (!c->vYCoeffsBank || !c->vCCoeffsBank)
        return AVERROR(ENOMEM);

    for (int i = 0; i < c->vLumFilterSize * sws->dst_h; ++i) {
        short *p = (short *)&c->vYCoeffsBank[i];
        for (int j = 0; j < 8; ++j)
            p[j] = c->vLumFilter[i];
    }

    for (int i = 0; i < c->vChrFilterSize * c->chrDstH; ++i) {
        short *p = (short *)&c->vCCoeffsBank[i];
        for (int j = 0; j < 8; ++j)
            p[j] = c->vChrFilter[i];
    }

    return 0;
}

av_cold void ff_sws_free_altivec_bufs(SwsInternal *c)
{
    av_freep(&c->vYCoeffsBank);
    av_freep(&c->vCCoeffsBank);
}
#endif /* HAVE_ALTIVEC */
