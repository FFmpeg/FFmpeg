/*
 * ADTS muxer.
 * Copyright (c) 2006 Baptiste Coudurier <baptiste.coudurier@smartjog.com>
 *                    Mans Rullgard <mans@mansr.com>
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

#ifndef AVFORMAT_ADTS_H
#define AVFORMAT_ADTS_H

#include "avformat.h"
#include "libavcodec/mpeg4audio.h"

#define ADTS_HEADER_SIZE 7

typedef struct {
    int write_adts;
    int objecttype;
    int sample_rate_index;
    int channel_conf;
    int pce_size;
    uint8_t pce_data[MAX_PCE_SIZE];
} ADTSContext;

int ff_adts_write_frame_header(ADTSContext *ctx, uint8_t *buf,
                               int size, int pce_size);
int ff_adts_decode_extradata(AVFormatContext *s, ADTSContext *adts,
                             uint8_t *buf, int size);

#endif /* AVFORMAT_ADTS_H */
