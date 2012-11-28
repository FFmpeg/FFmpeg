/*
 * Copyright (c) Stefano Sabatini | stefasab at gmail.com
 * Copyright (c) S.N. Hemanth Meenakshisundaram | smeenaks at ucsd.edu
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

#ifndef AVFILTER_AUDIO_H
#define AVFILTER_AUDIO_H

#include "avfilter.h"
#include "internal.h"

static const enum AVSampleFormat ff_packed_sample_fmts_array[] = {
    AV_SAMPLE_FMT_U8,
    AV_SAMPLE_FMT_S16,
    AV_SAMPLE_FMT_S32,
    AV_SAMPLE_FMT_FLT,
    AV_SAMPLE_FMT_DBL,
    AV_SAMPLE_FMT_NONE
};

static const enum AVSampleFormat ff_planar_sample_fmts_array[] = {
    AV_SAMPLE_FMT_U8P,
    AV_SAMPLE_FMT_S16P,
    AV_SAMPLE_FMT_S32P,
    AV_SAMPLE_FMT_FLTP,
    AV_SAMPLE_FMT_DBLP,
    AV_SAMPLE_FMT_NONE
};

/** default handler for get_audio_buffer() for audio inputs */
AVFilterBufferRef *ff_default_get_audio_buffer(AVFilterLink *link, int perms,
                                                     int nb_samples);

/** get_audio_buffer() handler for filters which simply pass audio along */
AVFilterBufferRef *ff_null_get_audio_buffer(AVFilterLink *link, int perms,
                                                  int nb_samples);

/**
 * Request an audio samples buffer with a specific set of permissions.
 *
 * @param link           the output link to the filter from which the buffer will
 *                       be requested
 * @param perms          the required access permissions
 * @param nb_samples     the number of samples per channel
 * @return               A reference to the samples. This must be unreferenced with
 *                       avfilter_unref_buffer when you are finished with it.
 */
AVFilterBufferRef *ff_get_audio_buffer(AVFilterLink *link, int perms,
                                             int nb_samples);

/**
 * Send a buffer of audio samples to the next filter.
 *
 * @param link       the output link over which the audio samples are being sent
 * @param samplesref a reference to the buffer of audio samples being sent. The
 *                   receiving filter will free this reference when it no longer
 *                   needs it or pass it on to the next filter.
 *
 * @return >= 0 on success, a negative AVERROR on error. The receiving filter
 * is responsible for unreferencing samplesref in case of error.
 */
int ff_filter_samples(AVFilterLink *link, AVFilterBufferRef *samplesref);

/**
 * Send a buffer of audio samples to the next link, without checking
 * min_samples.
 */
int ff_filter_samples_framed(AVFilterLink *link,
                             AVFilterBufferRef *samplesref);

#endif /* AVFILTER_AUDIO_H */
