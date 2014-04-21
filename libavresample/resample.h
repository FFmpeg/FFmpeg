/*
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVRESAMPLE_RESAMPLE_H
#define AVRESAMPLE_RESAMPLE_H

#include "avresample.h"
#include "internal.h"
#include "audio_data.h"

struct ResampleContext {
    AVAudioResampleContext *avr;
    AudioData *buffer;
    uint8_t *filter_bank;
    int filter_length;
    int ideal_dst_incr;
    int dst_incr;
    unsigned int index;
    int frac;
    int src_incr;
    int compensation_distance;
    int phase_shift;
    int phase_mask;
    int linear;
    enum AVResampleFilterType filter_type;
    int kaiser_beta;
    void (*set_filter)(void *filter, double *tab, int phase, int tap_count);
    void (*resample_one)(struct ResampleContext *c, void *dst0,
                         int dst_index, const void *src0,
                         unsigned int index, int frac);
    void (*resample_nearest)(void *dst0, int dst_index,
                             const void *src0, unsigned int index);
    int padding_size;
    int initial_padding_filled;
    int initial_padding_samples;
    int final_padding_filled;
    int final_padding_samples;
};

/**
 * Allocate and initialize a ResampleContext.
 *
 * The parameters in the AVAudioResampleContext are used to initialize the
 * ResampleContext.
 *
 * @param avr  AVAudioResampleContext
 * @return     newly-allocated ResampleContext
 */
ResampleContext *ff_audio_resample_init(AVAudioResampleContext *avr);

/**
 * Free a ResampleContext.
 *
 * @param c  ResampleContext
 */
void ff_audio_resample_free(ResampleContext **c);

/**
 * Resample audio data.
 *
 * Changes the sample rate.
 *
 * @par
 * All samples in the source data may not be consumed depending on the
 * resampling parameters and the size of the output buffer. The unconsumed
 * samples are automatically added to the start of the source in the next call.
 * If the destination data can be reallocated, that may be done in this function
 * in order to fit all available output. If it cannot be reallocated, fewer
 * input samples will be consumed in order to have the output fit in the
 * destination data buffers.
 *
 * @param c         ResampleContext
 * @param dst       destination audio data
 * @param src       source audio data
 * @return          0 on success, negative AVERROR code on failure
 */
int ff_audio_resample(ResampleContext *c, AudioData *dst, AudioData *src);

#endif /* AVRESAMPLE_RESAMPLE_H */
