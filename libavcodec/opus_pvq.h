/*
 * Copyright (c) 2012 Andrew D'Addesio
 * Copyright (c) 2013-2014 Mozilla Corporation
 * Copyright (c) 2016 Rostislav Pehlivanov <atomnuker@gmail.com>
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

#ifndef AVCODEC_OPUS_PVQ_H
#define AVCODEC_OPUS_PVQ_H

#include "opus.h"
#include "opus_celt.h"

/* Decodes a band using PVQ */
uint32_t ff_celt_decode_band(CeltFrame *f, OpusRangeCoder *rc, const int band,
                             float *X, float *Y, int N, int b, uint32_t blocks,
                             float *lowband, int duration, float *lowband_out, int level,
                             float gain, float *lowband_scratch, int fill);

/* Encodes a band using PVQ */
uint32_t ff_celt_encode_band(CeltFrame *f, OpusRangeCoder *rc, const int band,
                             float *X, float *Y, int N, int b, uint32_t blocks,
                             float *lowband, int duration, float *lowband_out, int level,
                             float gain, float *lowband_scratch, int fill);

#endif /* AVCODEC_OPUS_PVQ_H */
