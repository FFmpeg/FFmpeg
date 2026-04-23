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

#ifndef AVFILTER_LIMITER_INIT_H
#define AVFILTER_LIMITER_INIT_H

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "limiter.h"

#define LIMITER(n, type)                                        \
static void limiter##n(const uint8_t *ssrc, uint8_t *ddst,      \
                       ptrdiff_t slinesize, ptrdiff_t dlinesize,\
                       int w, int h, int min, int max)          \
{                                                               \
    const type *src = (const type *)ssrc;                       \
    type *dst = (type *)ddst;                                   \
                                                                \
    dlinesize /= sizeof(type);                                  \
    slinesize /= sizeof(type);                                  \
                                                                \
    for (int y = 0; y < h; y++) {                               \
        for (int x = 0; x < w; x++) {                           \
            dst[x] = av_clip(src[x], min, max);                 \
        }                                                       \
                                                                \
        dst += dlinesize;                                       \
        src += slinesize;                                       \
    }                                                           \
}

LIMITER(8,  uint8_t)
LIMITER(16, uint16_t)

static av_unused void ff_limiter_init(LimiterContext *s, int depth)
{
    if (depth == 8) {
        s->dsp.limiter = limiter8;
    } else {
        s->dsp.limiter = limiter16;
    }

#if ARCH_X86 && HAVE_X86ASM
    ff_limiter_init_x86(&s->dsp, depth);
#endif

}

#endif /* AVFILTER_LIMITER_INIT_H */
