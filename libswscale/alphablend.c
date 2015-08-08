/*
 * Copyright (C) 2015 Michael Niedermayer <michaelni@gmx.at>
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

#include "swscale_internal.h"

int ff_sws_alphablendaway(SwsContext *c, const uint8_t *src[],
                          int srcStride[], int srcSliceY, int srcSliceH,
                          uint8_t *dst[], int dstStride[])
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int nb_components = desc->nb_components;
    int plane, x, y;
    int plane_count = isGray(c->srcFormat) ? 1 : 3;
    int sixteen_bits = desc->comp[0].depth_minus1 >= 8;
    unsigned off    = 1<<desc->comp[0].depth_minus1;
    unsigned shift  = desc->comp[0].depth_minus1 + 1;
    unsigned max    = (1<<shift) - 1;

    av_assert0(plane_count == nb_components - 1);
    if (desc->flags & AV_PIX_FMT_FLAG_PLANAR) {
        for (plane = 0; plane < plane_count; plane++) {
            int w = plane ? c->chrSrcW : c->srcW;
            int y_subsample = plane ? desc->log2_chroma_h: 0;
            for (y = srcSliceY >> y_subsample; y < FF_CEIL_RSHIFT(srcSliceH, y_subsample); y++) {
                if (sixteen_bits) {
                    const uint16_t *s = src[plane      ] + srcStride[plane] * y;
                    const uint16_t *a = src[plane_count] + srcStride[plane_count] * y;
                          uint16_t *d = dst[plane      ] + dstStride[plane] * y;
                    unsigned target = plane && !(desc->flags & AV_PIX_FMT_FLAG_RGB) ? 1<<desc->comp[0].depth_minus1 : 0;
                    if ((!isBE(c->dstFormat)) == !HAVE_BIGENDIAN) {
                        for (x = 0; x < w; x++) {
                            unsigned u = s[x]*a[x] + target*(max-a[x]) + off;
                            d[x] = av_clip((u + (u >> shift)) >> shift, 0, max);
                        }
                    } else {
                        for (x = 0; x < w; x++) {
                            unsigned aswap =av_bswap16(a[x]);
                            unsigned u = av_bswap16(s[x])*aswap + target*(max-aswap) + off;
                            d[x] = av_clip((u + (u >> shift)) >> shift, 0, max);
                        }
                    }
                } else {
                    const uint8_t *s = src[plane      ] + srcStride[plane] * y;
                    const uint8_t *a = src[plane_count] + srcStride[plane_count] * y;
                          uint8_t *d = dst[plane      ] + dstStride[plane] * y;
                    unsigned target = plane && !(desc->flags & AV_PIX_FMT_FLAG_RGB) ? 128 : 0;
                    for (x = 0; x < w; x++) {
                        unsigned u = s[x]*a[x] + target*(255-a[x]) + 128;
                        d[x] = (257*u) >> 16;
                    }
                }
            }
        }
    }

    return 0;
}
