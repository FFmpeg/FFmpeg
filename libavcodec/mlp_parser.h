/*
 * MLP parser prototypes
 * Copyright (c) 2007 Ian Caulfield
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
 * @file mlp_parser.h
 * MLP parser prototypes
 */

#ifndef FFMPEG_MLP_PARSER_H
#define FFMPEG_MLP_PARSER_H

#include <inttypes.h>

typedef struct MLPHeaderInfo
{
    int stream_type;            ///< 0xBB for MLP, 0xBA for TrueHD

    int group1_bits;            ///< The bit depth of the first substream
    int group2_bits;            ///< Bit depth of the second substream (MLP only)

    int group1_samplerate;      ///< Sample rate of first substream
    int group2_samplerate;      ///< Sample rate of second substream (MLP only)

    int channels_mlp;           ///< Channel arrangement for MLP streams
    int channels_thd_stream1;   ///< Channel arrangement for substream 1 of TrueHD streams (5.1)
    int channels_thd_stream2;   ///< Channel arrangement for substream 2 of TrueHD streams (7.1)

    int access_unit_size;       ///< Number of samples per coded frame
    int access_unit_size_pow2;  ///< Next power of two above number of samples per frame

    int is_vbr;                 ///< Stream is VBR instead of CBR
    int peak_bitrate;           ///< Peak bitrate for VBR, actual bitrate (==peak) for CBR

    int num_substreams;         ///< Number of substreams within stream
} MLPHeaderInfo;


int ff_mlp_read_major_sync(void *log, MLPHeaderInfo *mh, const uint8_t *buf,
                           unsigned int buf_size);

#endif /* FFMPEG_MLP_PARSER_H */

