/*
 * Copyright (c) 2022 Thilo Borgmann <thilo.borgmann _at_ mail.de>
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
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 */

#include "libavutil/avassert.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

#undef pixel
#if DEPTH == 8
#define pixel uint8_t
#else
#define pixel uint16_t
#endif

#undef fn
#undef fn2
#undef fn3
#define fn3(a,b)   ff_##a##_##b
#define fn2(a,b)   fn3(a,b)
#define fn(a)      fn2(a, DEPTH)

void fn(sobel)(int w, int h,
               uint16_t *dst, int dst_linesize,
               int8_t *dir, int dir_linesize,
               const uint8_t *src, int src_linesize, int src_stride)
{
    pixel *srcp = (pixel *)src;

    src_stride   /= sizeof(pixel);
    src_linesize /= sizeof(pixel);
    dst_linesize /= sizeof(pixel);

    for (int j = 1; j < h - 1; j++) {
        dst  += dst_linesize;
        dir  += dir_linesize;
        srcp += src_linesize;
        for (int i = 1; i < w - 1; i++) {
            const int gx =
                -1*srcp[-src_linesize + (i-1)*src_stride] + 1*srcp[-src_linesize + (i+1)*src_stride]
                -2*srcp[                (i-1)*src_stride] + 2*srcp[                (i+1)*src_stride]
                -1*srcp[ src_linesize + (i-1)*src_stride] + 1*srcp[ src_linesize + (i+1)*src_stride];
            const int gy =
                -1*srcp[-src_linesize + (i-1)*src_stride] + 1*srcp[ src_linesize + (i-1)*src_stride]
                -2*srcp[-src_linesize + (i  )*src_stride] + 2*srcp[ src_linesize + (i  )*src_stride]
                -1*srcp[-src_linesize + (i+1)*src_stride] + 1*srcp[ src_linesize + (i+1)*src_stride];

            dst[i] = FFABS(gx) + FFABS(gy);
            dir[i] = get_rounded_direction(gx, gy);
        }
    }
}

void fn(gaussian_blur)(int w, int h,
                       uint8_t *dst, int dst_linesize,
                       const uint8_t *src, int src_linesize, int src_stride)
{
    int j;
    pixel *srcp = (pixel *)src;
    pixel *dstp = (pixel *)dst;

    src_stride   /= sizeof(pixel);
    src_linesize /= sizeof(pixel);
    dst_linesize /= sizeof(pixel);

    for (j = 0; j < FFMIN(h, 2); j++) {
        memcpy(dstp, srcp, w*sizeof(pixel));
        dstp += dst_linesize;
        srcp += src_linesize;
    }

    for (; j < h - 2; j++) {
        int i;
        for (i = 0; i < FFMIN(w, 2); i++)
            dstp[i] = srcp[i*src_stride];
        for (; i < w - 2; i++) {
            /* Gaussian mask of size 5x5 with sigma = 1.4 */
            dstp[i] = ((srcp[-2*src_linesize + (i-2)*src_stride] + srcp[2*src_linesize + (i-2)*src_stride]) * 2
                     + (srcp[-2*src_linesize + (i-1)*src_stride] + srcp[2*src_linesize + (i-1)*src_stride]) * 4
                     + (srcp[-2*src_linesize + (i  )*src_stride] + srcp[2*src_linesize + (i  )*src_stride]) * 5
                     + (srcp[-2*src_linesize + (i+1)*src_stride] + srcp[2*src_linesize + (i+1)*src_stride]) * 4
                     + (srcp[-2*src_linesize + (i+2)*src_stride] + srcp[2*src_linesize + (i+2)*src_stride]) * 2

                     + (srcp[  -src_linesize + (i-2)*src_stride] + srcp[  src_linesize + (i-2)*src_stride]) *  4
                     + (srcp[  -src_linesize + (i-1)*src_stride] + srcp[  src_linesize + (i-1)*src_stride]) *  9
                     + (srcp[  -src_linesize + (i  )*src_stride] + srcp[  src_linesize + (i  )*src_stride]) * 12
                     + (srcp[  -src_linesize + (i+1)*src_stride] + srcp[  src_linesize + (i+1)*src_stride]) *  9
                     + (srcp[  -src_linesize + (i+2)*src_stride] + srcp[  src_linesize + (i+2)*src_stride]) *  4

                     + srcp[(i-2)*src_stride] *  5
                     + srcp[(i-1)*src_stride] * 12
                     + srcp[(i  )*src_stride] * 15
                     + srcp[(i+1)*src_stride] * 12
                     + srcp[(i+2)*src_stride] *  5) / 159;
        }
        for (; i < w; i++)
            dstp[i] = srcp[i*src_stride];

        dstp += dst_linesize;
        srcp += src_linesize;
    }
    for (; j < h; j++) {
        memcpy(dstp, srcp, w*sizeof(pixel));
        dstp += dst_linesize;
        srcp += src_linesize;
    }
}
