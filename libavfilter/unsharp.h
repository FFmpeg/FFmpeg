/*
 * Copyright (C) 2013 Wei Gao <weigao@multicorewareinc.com>
 * Copyright (C) 2013 Lenny Wang
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

#ifndef AVFILTER_UNSHARP_H
#define AVFILTER_UNSHARP_H

#include "config.h"
#include "avfilter.h"

#define MIN_MATRIX_SIZE 3
#define MAX_MATRIX_SIZE 63


typedef struct UnsharpFilterParam {
    int msize_x;                             ///< matrix width
    int msize_y;                             ///< matrix height
    int amount;                              ///< effect amount
    int steps_x;                             ///< horizontal step count
    int steps_y;                             ///< vertical step count
    int scalebits;                           ///< bits to shift pixel
    int32_t halfscale;                       ///< amount to add to pixel
    uint32_t *sr;        ///< finite state machine storage within a row
    uint32_t **sc;       ///< finite state machine storage across rows
} UnsharpFilterParam;

typedef struct UnsharpContext {
    const AVClass *class;
    int lmsize_x, lmsize_y, cmsize_x, cmsize_y;
    float lamount, camount;
    UnsharpFilterParam luma;   ///< luma parameters (width, height, amount)
    UnsharpFilterParam chroma; ///< chroma parameters (width, height, amount)
    int hsub, vsub;
    int bitdepth;
    int bps;
    int nb_threads;
    int opencl;
    int (* apply_unsharp)(AVFilterContext *ctx, AVFrame *in, AVFrame *out);
    int (* unsharp_slice)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} UnsharpContext;

#endif /* AVFILTER_UNSHARP_H */
