/*
 * Copyright (c) 2015-2016 Kieran Kunhya <kieran@kunhya.com>
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
#include "libavutil/common.h"

#include "cfhddsp.h"

static av_always_inline void filter(int16_t *output, ptrdiff_t out_stride,
                          const int16_t *low, ptrdiff_t low_stride,
                          const int16_t *high, ptrdiff_t high_stride,
                          int len, int clip)
{
    int16_t tmp;
    int i;

    tmp = (11*low[0*low_stride] - 4*low[1*low_stride] + low[2*low_stride] + 4) >> 3;
    output[(2*0+0)*out_stride] = (tmp + high[0*high_stride]) >> 1;
    if (clip)
        output[(2*0+0)*out_stride] = av_clip_uintp2_c(output[(2*0+0)*out_stride], clip);

    tmp = ( 5*low[0*low_stride] + 4*low[1*low_stride] - low[2*low_stride] + 4) >> 3;
    output[(2*0+1)*out_stride] = (tmp - high[0*high_stride]) >> 1;
    if (clip)
        output[(2*0+1)*out_stride] = av_clip_uintp2_c(output[(2*0+1)*out_stride], clip);

    for (i = 1; i < len - 1; i++) {
        tmp = (low[(i-1)*low_stride] - low[(i+1)*low_stride] + 4) >> 3;
        output[(2*i+0)*out_stride] = (tmp + low[i*low_stride] + high[i*high_stride]) >> 1;
        if (clip)
            output[(2*i+0)*out_stride] = av_clip_uintp2_c(output[(2*i+0)*out_stride], clip);

        tmp = (low[(i+1)*low_stride] - low[(i-1)*low_stride] + 4) >> 3;
        output[(2*i+1)*out_stride] = (tmp + low[i*low_stride] - high[i*high_stride]) >> 1;
        if (clip)
            output[(2*i+1)*out_stride] = av_clip_uintp2_c(output[(2*i+1)*out_stride], clip);
    }

    tmp = ( 5*low[i*low_stride] + 4*low[(i-1)*low_stride] - low[(i-2)*low_stride] + 4) >> 3;
    output[(2*i+0)*out_stride] = (tmp + high[i*high_stride]) >> 1;
    if (clip)
        output[(2*i+0)*out_stride] = av_clip_uintp2_c(output[(2*i+0)*out_stride], clip);

    tmp = (11*low[i*low_stride] - 4*low[(i-1)*low_stride] + low[(i-2)*low_stride] + 4) >> 3;
    output[(2*i+1)*out_stride] = (tmp - high[i*high_stride]) >> 1;
    if (clip)
        output[(2*i+1)*out_stride] = av_clip_uintp2_c(output[(2*i+1)*out_stride], clip);
}

static void vert_filter(int16_t *output, ptrdiff_t out_stride,
                        const int16_t *low, ptrdiff_t low_stride,
                        const int16_t *high, ptrdiff_t high_stride,
                        int width, int height)
{
    for (int i = 0; i < width; i++) {
        filter(output, out_stride, low, low_stride, high, high_stride, height, 0);
        low++;
        high++;
        output++;
    }
}

static void horiz_filter(int16_t *output, ptrdiff_t ostride,
                         const int16_t *low, ptrdiff_t lstride,
                         const int16_t *high, ptrdiff_t hstride,
                         int width, int height)
{
    for (int i = 0; i < height; i++) {
        filter(output, 1, low, 1, high, 1, width, 0);
        low    += lstride;
        high   += hstride;
        output += ostride * 2;
    }
}

static void horiz_filter_clip(int16_t *output, const int16_t *low, const int16_t *high,
                              int width, int clip)
{
    filter(output, 1, low, 1, high, 1, width, clip);
}

static void horiz_filter_clip_bayer(int16_t *output, const int16_t *low, const int16_t *high,
                                    int width, int clip)
{
    filter(output, 2, low, 1, high, 1, width, clip);
}

av_cold void ff_cfhddsp_init(CFHDDSPContext *c, int depth, int bayer)
{
    c->horiz_filter = horiz_filter;
    c->vert_filter = vert_filter;

    if (bayer)
        c->horiz_filter_clip = horiz_filter_clip_bayer;
    else
        c->horiz_filter_clip = horiz_filter_clip;

    if (ARCH_X86)
        ff_cfhddsp_init_x86(c, depth, bayer);
}
