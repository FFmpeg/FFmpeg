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

#ifndef AVFILTER_IDET_H
#define AVFILTER_IDET_H

#include "libavutil/pixdesc.h"
#include "avfilter.h"

#define HIST_SIZE 4

typedef int (*ff_idet_filter_func)(const uint8_t *a, const uint8_t *b, const uint8_t *c, int w);

typedef enum {
    TFF,
    BFF,
    PROGRSSIVE,
    UNDETERMINED,
} Type;

typedef struct {
    const AVClass *class;
    float interlace_threshold;
    float progressive_threshold;

    Type last_type;
    int prestat[4];
    int poststat[4];

    uint8_t history[HIST_SIZE];

    AVFrame *cur;
    AVFrame *next;
    AVFrame *prev;
    ff_idet_filter_func filter_line;

    const AVPixFmtDescriptor *csp;
} IDETContext;

void ff_idet_init_x86(IDETContext *idet, int for_16b);

/* main fall-back for left-over */
int ff_idet_filter_line_c(const uint8_t *a, const uint8_t *b, const uint8_t *c, int w);
int ff_idet_filter_line_c_16bit(const uint16_t *a, const uint16_t *b, const uint16_t *c, int w);

#endif
