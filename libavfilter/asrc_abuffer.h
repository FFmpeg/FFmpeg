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

#ifndef AVFILTER_ASRC_ABUFFER_H
#define AVFILTER_ASRC_ABUFFER_H

#include "avfilter.h"

/**
 * @file
 * memory buffer source for audio
 *
 * @deprecated use buffersrc.h instead.
 */

/**
 * Queue an audio buffer to the audio buffer source.
 *
 * @param abuffersrc audio source buffer context
 * @param data pointers to the samples planes
 * @param linesize linesizes of each audio buffer plane
 * @param nb_samples number of samples per channel
 * @param sample_fmt sample format of the audio data
 * @param ch_layout channel layout of the audio data
 * @param planar flag to indicate if audio data is planar or packed
 * @param pts presentation timestamp of the audio buffer
 * @param flags unused
 *
 * @deprecated use av_buffersrc_add_ref() instead.
 */
attribute_deprecated
int av_asrc_buffer_add_samples(AVFilterContext *abuffersrc,
                               uint8_t *data[8], int linesize[8],
                               int nb_samples, int sample_rate,
                               int sample_fmt, int64_t ch_layout, int planar,
                               int64_t pts, int av_unused flags);

/**
 * Queue an audio buffer to the audio buffer source.
 *
 * This is similar to av_asrc_buffer_add_samples(), but the samples
 * are stored in a buffer with known size.
 *
 * @param abuffersrc audio source buffer context
 * @param buf pointer to the samples data, packed is assumed
 * @param size the size in bytes of the buffer, it must contain an
 * integer number of samples
 * @param sample_fmt sample format of the audio data
 * @param ch_layout channel layout of the audio data
 * @param pts presentation timestamp of the audio buffer
 * @param flags unused
 *
 * @deprecated use av_buffersrc_add_ref() instead.
 */
attribute_deprecated
int av_asrc_buffer_add_buffer(AVFilterContext *abuffersrc,
                              uint8_t *buf, int buf_size,
                              int sample_rate,
                              int sample_fmt, int64_t ch_layout, int planar,
                              int64_t pts, int av_unused flags);

/**
 * Queue an audio buffer to the audio buffer source.
 *
 * @param abuffersrc audio source buffer context
 * @param samplesref buffer ref to queue
 * @param flags unused
 *
 * @deprecated use av_buffersrc_add_ref() instead.
 */
attribute_deprecated
int av_asrc_buffer_add_audio_buffer_ref(AVFilterContext *abuffersrc,
                                        AVFilterBufferRef *samplesref,
                                        int av_unused flags);

#endif /* AVFILTER_ASRC_ABUFFER_H */
