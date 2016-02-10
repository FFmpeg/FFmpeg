/*
 * Copyright (C) 2007 Marco Gerards <marco@gnu.org>
 * Copyright (C) 2016 Open Broadcast Systems Ltd.
 * Author        2016 Rostislav Pehlivanov <atomnuker@gmail.com>
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
#include "libavutil/mem.h"
#include "vc2enc_dwt.h"

/* Since the transforms spit out interleaved coefficients, this function
 * rearranges the coefficients into the more traditional subdivision,
 * making it easier to encode and perform another level. */
static av_always_inline void deinterleave(dwtcoef *linell, ptrdiff_t stride,
                                          int width, int height, dwtcoef *synthl)
{
    int x, y;
    ptrdiff_t synthw = width << 1;
    dwtcoef *linehl = linell + width;
    dwtcoef *linelh = linell + height*stride;
    dwtcoef *linehh = linelh + width;

    /* Deinterleave the coefficients. */
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            linell[x] = synthl[(x << 1)];
            linehl[x] = synthl[(x << 1) + 1];
            linelh[x] = synthl[(x << 1) + synthw];
            linehh[x] = synthl[(x << 1) + synthw + 1];
        }
        synthl += synthw << 1;
        linell += stride;
        linelh += stride;
        linehl += stride;
        linehh += stride;
    }
}

static void vc2_subband_dwt_97(VC2TransformContext *t, dwtcoef *data,
                               ptrdiff_t stride, int width, int height)
{
    int x, y;
    dwtcoef *datal = data, *synth = t->buffer, *synthl = synth;
    const ptrdiff_t synth_width  = width  << 1;
    const ptrdiff_t synth_height = height << 1;

    /*
     * Shift in one bit that is used for additional precision and copy
     * the data to the buffer.
     */
    for (y = 0; y < synth_height; y++) {
        for (x = 0; x < synth_width; x++)
            synthl[x] = datal[x] << 1;
        synthl += synth_width;
        datal += stride;
    }

    /* Horizontal synthesis. */
    synthl = synth;
    for (y = 0; y < synth_height; y++) {
        /* Lifting stage 2. */
        synthl[1] -= (8*synthl[0] + 9*synthl[2] - synthl[4] + 8) >> 4;
        for (x = 1; x < width - 2; x++)
            synthl[2*x + 1] -= (9*synthl[2*x] + 9*synthl[2*x + 2] - synthl[2*x + 4] -
                                synthl[2 * x - 2] + 8) >> 4;
        synthl[synth_width - 1] -= (17*synthl[synth_width - 2] -
                                    synthl[synth_width - 4] + 8) >> 4;
        synthl[synth_width - 3] -= (8*synthl[synth_width - 2] +
                                    9*synthl[synth_width - 4] -
                                    synthl[synth_width - 6] + 8) >> 4;
        /* Lifting stage 1. */
        synthl[0] += (synthl[1] + synthl[1] + 2) >> 2;
        for (x = 1; x < width - 1; x++)
            synthl[2*x] += (synthl[2*x - 1] + synthl[2*x + 1] + 2) >> 2;

        synthl[synth_width - 2] += (synthl[synth_width - 3] +
                                    synthl[synth_width - 1] + 2) >> 2;
        synthl += synth_width;
    }

    /* Vertical synthesis: Lifting stage 2. */
    synthl = synth + synth_width;
    for (x = 0; x < synth_width; x++)
        synthl[x] -= (8*synthl[x - synth_width] + 9*synthl[x + synth_width] -
                      synthl[x + 3 * synth_width] + 8) >> 4;

    synthl = synth + (synth_width << 1);
    for (y = 1; y < height - 2; y++) {
        for (x = 0; x < synth_width; x++)
            synthl[x + synth_width] -= (9*synthl[x] +
                                        9*synthl[x + 2 * synth_width] -
                                        synthl[x - 2 * synth_width] -
                                        synthl[x + 4 * synth_width] + 8) >> 4;
        synthl += synth_width << 1;
    }

    synthl = synth + (synth_height - 1) * synth_width;
    for (x = 0; x < synth_width; x++) {
        synthl[x] -= (17*synthl[x - synth_width] -
                      synthl[x - 3*synth_width] + 8) >> 4;
                      synthl[x - 2*synth_width] -= (9*synthl[x - 3*synth_width] +
                      8*synthl[x - 1*synth_width] - synthl[x - 5*synth_width] + 8) >> 4;
    }

    /* Vertical synthesis: Lifting stage 1. */
    synthl = synth;
    for (x = 0; x < synth_width; x++)
        synthl[x] += (synthl[x + synth_width] + synthl[x + synth_width] + 2) >> 2;

    synthl = synth + (synth_width << 1);
    for (y = 1; y < height - 1; y++) {
        for (x = 0; x < synth_width; x++)
            synthl[x] += (synthl[x - synth_width] + synthl[x + synth_width] + 2) >> 2;
        synthl += synth_width << 1;
    }

    synthl = synth + (synth_height - 2) * synth_width;
    for (x = 0; x < synth_width; x++)
        synthl[x] += (synthl[x - synth_width] + synthl[x + synth_width] + 2) >> 2;

    deinterleave(data, stride, width, height, synth);
}

static void vc2_subband_dwt_53(VC2TransformContext *t, dwtcoef *data,
                               ptrdiff_t stride, int width, int height)
{
    int x, y;
    dwtcoef *synth = t->buffer, *synthl = synth, *datal = data;
    const ptrdiff_t synth_width  = width  << 1;
    const ptrdiff_t synth_height = height << 1;

    /*
     * Shift in one bit that is used for additional precision and copy
     * the data to the buffer.
     */
    for (y = 0; y < synth_height; y++) {
        for (x = 0; x < synth_width; x++)
            synthl[x] = datal[x] << 1;
        synthl += synth_width;
        datal  += stride;
    }

    /* Horizontal synthesis. */
    synthl = synth;
    for (y = 0; y < synth_height; y++) {
        /* Lifting stage 2. */
        for (x = 0; x < width - 1; x++)
            synthl[2 * x + 1] -= (synthl[2 * x] + synthl[2 * x + 2] + 1) >> 1;

        synthl[synth_width - 1] -= (2*synthl[synth_width - 2] + 1) >> 1;

        /* Lifting stage 1. */
        synthl[0] += (2*synthl[1] + 2) >> 2;
        for (x = 1; x < width - 1; x++)
            synthl[2 * x] += (synthl[2 * x - 1] + synthl[2 * x + 1] + 2) >> 2;

        synthl[synth_width - 2] += (synthl[synth_width - 3] + synthl[synth_width - 1] + 2) >> 2;

        synthl += synth_width;
    }

    /* Vertical synthesis: Lifting stage 2. */
    synthl = synth + synth_width;
    for (x = 0; x < synth_width; x++)
        synthl[x] -= (synthl[x - synth_width] + synthl[x + synth_width] + 1) >> 1;

    synthl = synth + (synth_width << 1);
    for (y = 1; y < height - 1; y++) {
        for (x = 0; x < synth_width; x++)
            synthl[x + synth_width] -= (synthl[x] + synthl[x + synth_width * 2] + 1) >> 1;
        synthl += (synth_width << 1);
    }

    synthl = synth + (synth_height - 1) * synth_width;
    for (x = 0; x < synth_width; x++)
        synthl[x] -= (2*synthl[x - synth_width] + 1) >> 1;

    /* Vertical synthesis: Lifting stage 1. */
    synthl = synth;
    for (x = 0; x < synth_width; x++)
        synthl[x] += (2*synthl[synth_width + x] + 2) >> 2;

    synthl = synth + (synth_width << 1);
    for (y = 1; y < height - 1; y++) {
        for (x = 0; x < synth_width; x++)
            synthl[x] += (synthl[x + synth_width] + synthl[x - synth_width] + 2) >> 2;
        synthl += (synth_width << 1);
    }

    synthl = synth + (synth_height - 2)*synth_width;
    for (x = 0; x < synth_width; x++)
        synthl[x] += (synthl[x - synth_width] + synthl[x + synth_width] + 2) >> 2;


    deinterleave(data, stride, width, height, synth);
}

av_cold int ff_vc2enc_init_transforms(VC2TransformContext *s, int p_width, int p_height)
{
    s->vc2_subband_dwt[VC2_TRANSFORM_9_7]    = vc2_subband_dwt_97;
    s->vc2_subband_dwt[VC2_TRANSFORM_5_3]    = vc2_subband_dwt_53;

    s->buffer = av_malloc(2*p_width*p_height*sizeof(dwtcoef));
    if (!s->buffer)
        return 1;

    return 0;
}

av_cold void ff_vc2enc_free_transforms(VC2TransformContext *s)
{
    av_freep(&s->buffer);
}
