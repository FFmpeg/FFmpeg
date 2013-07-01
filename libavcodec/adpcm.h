/*
 * Copyright (c) 2001-2003 The ffmpeg Project
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

/**
 * @file
 * ADPCM encoder/decoder common header.
 */

#ifndef AVCODEC_ADPCM_H
#define AVCODEC_ADPCM_H

#include <stdint.h>

#define BLKSIZE 1024

typedef struct ADPCMChannelStatus {
    int predictor;
    int16_t step_index;
    int step;
    /* for encoding */
    int prev_sample;

    /* MS version */
    int sample1;
    int sample2;
    int coeff1;
    int coeff2;
    int idelta;
} ADPCMChannelStatus;

#endif /* AVCODEC_ADPCM_H */
