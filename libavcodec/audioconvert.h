/*
 * audio conversion
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2008 Peter Ross
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_AUDIOCONVERT_H
#define AVCODEC_AUDIOCONVERT_H

/**
 * @file
 * Audio format conversion routines
 */


#include "libavutil/cpu.h"
#include "avcodec.h"
#include "libavutil/audioconvert.h"

struct AVAudioConvert;
typedef struct AVAudioConvert AVAudioConvert;

/**
 * Create an audio sample format converter context
 * @param out_fmt Output sample format
 * @param out_channels Number of output channels
 * @param in_fmt Input sample format
 * @param in_channels Number of input channels
 * @param[in] matrix Channel mixing matrix (of dimension in_channel*out_channels). Set to NULL to ignore.
 * @param flags See AV_CPU_FLAG_xx
 * @return NULL on error
 */
AVAudioConvert *av_audio_convert_alloc(enum AVSampleFormat out_fmt, int out_channels,
                                       enum AVSampleFormat in_fmt, int in_channels,
                                       const float *matrix, int flags);

/**
 * Free audio sample format converter context
 */
void av_audio_convert_free(AVAudioConvert *ctx);

/**
 * Convert between audio sample formats
 * @param[in] out array of output buffers for each channel. set to NULL to ignore processing of the given channel.
 * @param[in] out_stride distance between consecutive output samples (measured in bytes)
 * @param[in] in array of input buffers for each channel
 * @param[in] in_stride distance between consecutive input samples (measured in bytes)
 * @param len length of audio frame size (measured in samples)
 */
int av_audio_convert(AVAudioConvert *ctx,
                           void * const out[6], const int out_stride[6],
                     const void * const  in[6], const int  in_stride[6], int len);

#endif /* AVCODEC_AUDIOCONVERT_H */
