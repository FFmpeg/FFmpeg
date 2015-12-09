/*
 * Copyright (C) 2004-2010 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2008 David Conrad
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
#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "dirac_dwt.h"
#include "libavcodec/x86/dirac_dwt.h"

#define TEMPLATE_8bit
#include "dirac_dwt_template.c"
#undef TEMPLATE_8bit

#define TEMPLATE_10bit
#include "dirac_dwt_template.c"
#undef TEMPLATE_10bit

#define TEMPLATE_12bit
#include "dirac_dwt_template.c"
#undef TEMPLATE_12bit

int ff_spatial_idwt_init2(DWTContext *d, uint8_t *buffer, int width, int height,
                          int stride, enum dwt_type type, int decomposition_count,
                          uint8_t *temp, int bit_depth)
{
    int ret = 0;

    if (bit_depth == 8)
        ret = ff_spatial_idwt_init2_8bit(d, buffer, width, height, stride, type, decomposition_count, temp);
    else if (bit_depth == 10)
        ret = ff_spatial_idwt_init2_10bit(d, buffer, width, height, stride, type, decomposition_count, temp);
    else if (bit_depth == 12)
        ret = ff_spatial_idwt_init2_12bit(d, buffer, width, height, stride, type, decomposition_count, temp);
    else
        av_log(NULL, AV_LOG_WARNING, "Unsupported bit depth = %i\n", bit_depth);

    if (ret) {
        av_log(NULL, AV_LOG_ERROR, "Unknown wavelet type %d\n", type);
        return AVERROR_INVALIDDATA;
    }

    if (bit_depth == 8 && HAVE_MMX)
        ff_spatial_idwt_init_mmx(d, type);
    return 0;
}

void ff_spatial_idwt_slice2(DWTContext *d, int y)
{
    int level, support = d->support;

    for (level = d->decomposition_count-1; level >= 0; level--) {
        int wl = d->width  >> level;
        int hl = d->height >> level;
        int stride_l = d->stride << level;

        while (d->cs[level].y <= FFMIN((y>>level)+support, hl))
            d->spatial_compose(d, level, wl, hl, stride_l);
    }
}
