/*
 * Loongson LSX optimized swscale
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 * Contributed by Lu Wang <wanglu@loongson.cn>
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

#include "swscale_loongarch.h"

void ff_hscale_16_to_15_lsx(SwsInternal *c, int16_t *_dst, int dstW,
                            const uint8_t *_src, const int16_t *filter,
                            const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int sh              = desc->comp[0].depth - 1;

    if (sh < 15) {
        sh = isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8 ? 13 :
                      (desc->comp[0].depth - 1);
    } else if (desc->flags && AV_PIX_FMT_FLAG_FLOAT) {
        sh = 15;
    }
    ff_hscale_16_to_15_sub_lsx(c, _dst, dstW, _src, filter, filterPos, filterSize, sh);
}

void ff_hscale_16_to_19_lsx(SwsInternal *c, int16_t *_dst, int dstW,
                            const uint8_t *_src, const int16_t *filter,
                            const int32_t *filterPos, int filterSize)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(c->srcFormat);
    int bits            = desc->comp[0].depth - 1;
    int sh              = bits - 4;

    if ((isAnyRGB(c->srcFormat) || c->srcFormat==AV_PIX_FMT_PAL8) && desc->comp[0].depth<16) {

        sh = 9;
    } else if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) { /* float input are process like uint 16bpc */
        sh = 16 - 1 - 4;
    }
    ff_hscale_16_to_19_sub_lsx(c, _dst, dstW, _src, filter, filterPos, filterSize, sh);
}
