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

#include "cfhdencdsp.h"

static av_always_inline void filter(int16_t *input, ptrdiff_t in_stride,
                          int16_t *low, ptrdiff_t low_stride,
                          int16_t *high, ptrdiff_t high_stride,
                          int len)
{
    low[(0>>1) * low_stride]   = av_clip_int16(input[0*in_stride] + input[1*in_stride]);
    high[(0>>1) * high_stride] = av_clip_int16((5 * input[0*in_stride] - 11 * input[1*in_stride] +
                                                4 * input[2*in_stride] +  4 * input[3*in_stride] -
                                                1 * input[4*in_stride] -  1 * input[5*in_stride] + 4) >> 3);

    for (int i = 2; i < len - 2; i += 2) {
        low[(i>>1) * low_stride]   = av_clip_int16(input[i*in_stride] + input[(i+1)*in_stride]);
        high[(i>>1) * high_stride] = av_clip_int16(((-input[(i-2)*in_stride] - input[(i-1)*in_stride] +
                                                      input[(i+2)*in_stride] + input[(i+3)*in_stride] + 4) >> 3) +
                                                      input[(i+0)*in_stride] - input[(i+1)*in_stride]);
    }

    low[((len-2)>>1) * low_stride]   = av_clip_int16(input[((len-2)+0)*in_stride] + input[((len-2)+1)*in_stride]);
    high[((len-2)>>1) * high_stride] = av_clip_int16((11* input[((len-2)+0)*in_stride] - 5 * input[((len-2)+1)*in_stride] -
                                                      4 * input[((len-2)-1)*in_stride] - 4 * input[((len-2)-2)*in_stride] +
                                                      1 * input[((len-2)-3)*in_stride] + 1 * input[((len-2)-4)*in_stride] + 4) >> 3);
}

static void horiz_filter(int16_t *input, int16_t *low, int16_t *high,
                         ptrdiff_t in_stride, ptrdiff_t low_stride,
                         ptrdiff_t high_stride,
                         int width, int height)
{
    for (int i = 0; i < height; i++) {
        filter(input, 1, low, 1, high, 1, width);
        input += in_stride;
        low += low_stride;
        high += high_stride;
    }
}

static void vert_filter(int16_t *input, int16_t *low, int16_t *high,
                        ptrdiff_t in_stride, ptrdiff_t low_stride,
                        ptrdiff_t high_stride,
                        int width, int height)
{
    for (int i = 0; i < width; i++)
        filter(&input[i], in_stride, &low[i], low_stride, &high[i], high_stride, height);
}

av_cold void ff_cfhdencdsp_init(CFHDEncDSPContext *c)
{
    c->horiz_filter = horiz_filter;
    c->vert_filter = vert_filter;

#if ARCH_X86
    ff_cfhdencdsp_init_x86(c);
#endif
}
