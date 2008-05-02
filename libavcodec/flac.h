/*
 * FLAC (Free Lossless Audio Codec) decoder/demuxer common functions
 * Copyright (c) 2008 Justin Ruggles
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
 * @file flac.h
 * FLAC (Free Lossless Audio Codec) decoder/demuxer common functions
 */

#ifndef FFMPEG_FLAC_H
#define FFMPEG_FLAC_H

#include "avcodec.h"

/**
 * Data needed from the Streaminfo header for use by the raw FLAC demuxer
 * and/or the FLAC decoder.
 */
#define FLACSTREAMINFO \
    int min_blocksize;      /**< minimum block size, in samples          */\
    int max_blocksize;      /**< maximum block size, in samples          */\
    int max_framesize;      /**< maximum frame size, in bytes            */\
    int samplerate;         /**< sample rate                             */\
    int channels;           /**< number of channels                      */\
    int bps;                /**< bits-per-sample                         */\

typedef struct FLACStreaminfo {
    FLACSTREAMINFO
} FLACStreaminfo;

/**
 * Parse the Streaminfo metadata block
 * @param[out] avctx   codec context to set basic stream parameters
 * @param[out] s       where parsed information is stored
 * @param[in]  buffer  pointer to start of 34-byte streaminfo data
 */
void ff_flac_parse_streaminfo(AVCodecContext *avctx, struct FLACStreaminfo *s,
                              const uint8_t *buffer);

#endif /* FFMPEG_FLAC_H */
