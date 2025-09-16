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

#ifndef AVFILTER_IDETDSP_H
#define AVFILTER_IDETDSP_H

#include <stdint.h>

typedef int (*ff_idet_filter_func)(const uint8_t *a, const uint8_t *b, const uint8_t *c, int w);

typedef struct IDETDSPContext {
    ff_idet_filter_func filter_line;
} IDETDSPContext;

void ff_idet_dsp_init(IDETDSPContext *idet, int depth);

void ff_idet_dsp_init_x86(IDETDSPContext *idet, int depth);

/* main fall-back for left-over */
int ff_idet_filter_line_c(const uint8_t *a, const uint8_t *b, const uint8_t *c, int w);
int ff_idet_filter_line_c_16bit(const uint8_t *a, const uint8_t *b, const uint8_t *c, int w);

#endif /* AVFILTER_IDETDSP_H */
