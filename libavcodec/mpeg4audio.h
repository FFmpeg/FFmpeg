/*
 * MPEG-4 Audio common header
 * Copyright (c) 2008 Baptiste Coudurier <baptiste.coudurier@free.fr>
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

#ifndef FFMPEG_MPEG4AUDIO_H
#define FFMPEG_MPEG4AUDIO_H

#include <stdint.h>

typedef struct {
    int object_type;
    int sampling_index;
    int sample_rate;
    int chan_config;
    int sbr; //< -1 implicit, 1 presence
    int ext_object_type;
    int ext_sampling_index;
    int ext_sample_rate;
} MPEG4AudioConfig;

extern const int ff_mpeg4audio_sample_rates[16];
extern const uint8_t ff_mpeg4audio_channels[8];
/**
 * Parse MPEG-4 systems extradata to retrieve audio configuration.
 * @param[in] c        MPEG4AudioConfig structure to fill.
 * @param[in] buf      Extradata from container.
 * @param[in] buf_size Extradata size.
 * @return On error -1 is returned, on success AudioSpecificConfig bit index in extradata.
 */
int ff_mpeg4audio_get_config(MPEG4AudioConfig *c, const uint8_t *buf, int buf_size);

#endif /* FFMPEG_MPEGAUDIO_H */
