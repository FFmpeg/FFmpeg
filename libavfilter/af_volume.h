/*
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

/**
 * @file
 * audio volume filter
 */

#ifndef AVFILTER_AF_VOLUME_H
#define AVFILTER_AF_VOLUME_H

#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

enum PrecisionType {
    PRECISION_FIXED = 0,
    PRECISION_FLOAT,
    PRECISION_DOUBLE,
};

enum ReplayGainType {
    REPLAYGAIN_DROP,
    REPLAYGAIN_IGNORE,
    REPLAYGAIN_TRACK,
    REPLAYGAIN_ALBUM,
};

typedef struct VolumeContext {
    const AVClass *class;
    AVFloatDSPContext fdsp;
    enum PrecisionType precision;
    enum ReplayGainType replaygain;
    double replaygain_preamp;
    int    replaygain_noclip;
    double volume;
    int    volume_i;
    int    channels;
    int    planes;
    enum AVSampleFormat sample_fmt;

    void (*scale_samples)(uint8_t *dst, const uint8_t *src, int nb_samples,
                          int volume);
    int samples_align;
} VolumeContext;

void ff_volume_init_x86(VolumeContext *vol);

#endif /* AVFILTER_AF_VOLUME_H */
