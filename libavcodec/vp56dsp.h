/*
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
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

#ifndef AVCODEC_VP56DSP_H
#define AVCODEC_VP56DSP_H

#include <stdint.h>

typedef struct VP56DSPContext {
    void (*edge_filter_hor)(uint8_t *yuv, int stride, int t);
    void (*edge_filter_ver)(uint8_t *yuv, int stride, int t);
} VP56DSPContext;

void ff_vp56dsp_init(VP56DSPContext *s, enum CodecID codec);

#endif /* AVCODEC_VP56DSP_H */
